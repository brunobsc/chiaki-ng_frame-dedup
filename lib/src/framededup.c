// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/framededup.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUB 4      // luma subsampling for the reference picture
#define BLOCK 4    // block size in subsampled pixels (= 16x16 source pixels)
_Static_assert(SUB == 4, "8-bit fast path assumes SUB == 4");

static double env_double(const char *name, double def)
{
	const char *v = getenv(name);
	if(!v || !*v)
		return def;
	char *end = NULL;
	double d = strtod(v, &end);
	return end && end != v ? d : def;
}

static long env_long(const char *name, long def)
{
	const char *v = getenv(name);
	if(!v || !*v)
		return def;
	char *end = NULL;
	long l = strtol(v, &end, 10);
	return end && end != v ? l : def;
}

CHIAKI_EXPORT void chiaki_frame_dedup_init_with(ChiakiFrameDedup *dedup, bool hevc, bool enabled,
		double ratio, size_t min_bytes, unsigned int max_drop, bool debug)
{
	memset(dedup, 0, sizeof(*dedup));
	dedup->hevc = hevc;
	dedup->enabled = enabled;
	dedup->ratio = ratio > 0.0 && ratio < 1.0 ? ratio : 0.10;
	dedup->min_bytes = min_bytes;
	dedup->max_drop = max_drop > 0 ? max_drop : 1;
	dedup->debug = debug;
	dedup->pixel_threshold = 4.0;
	for(unsigned int i = 0; i < CHIAKI_DEDUP_TAGS; i++)
		dedup->tag_pts[i] = INT64_MIN;
}

CHIAKI_EXPORT void chiaki_frame_dedup_init(ChiakiFrameDedup *dedup, bool hevc)
{
	long max_drop = env_long("CHIAKI_DEDUP_MAX_DROP", 3);
	long min_bytes = env_long("CHIAKI_DEDUP_MIN_BYTES", 256);
	chiaki_frame_dedup_init_with(dedup, hevc,
			env_long("CHIAKI_DEDUP", 1) != 0,
			env_double("CHIAKI_DEDUP_RATIO", 0.10),
			min_bytes > 0 ? (size_t)min_bytes : 0,
			max_drop > 0 ? (unsigned int)max_drop : 3,
			env_long("CHIAKI_DEDUP_DEBUG", 0) != 0);
	double px = env_double("CHIAKI_DEDUP_PIXEL", 4.0);
	dedup->pixel_threshold = px >= 0.0 ? px : 4.0;
}

CHIAKI_EXPORT void chiaki_frame_dedup_fini(ChiakiFrameDedup *dedup)
{
	free(dedup->ref);
	dedup->ref = NULL;
	dedup->ref_valid = false;
}

// Type of the first NAL unit after an Annex B start code, or -1.
static int first_nal_type(const uint8_t *buf, size_t size, bool hevc)
{
	for(size_t i = 0; i + 3 < size && i < 64; i++)
	{
		if(buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
		{
			uint8_t b = buf[i + 3];
			return hevc ? (b >> 1) & 0x3f : b & 0x1f;
		}
	}
	return -1;
}

// Parameter sets or IDR/IRAP: never a repeated picture, and not representative
// of normal P-frame sizes either.
static bool is_parameter_or_key(int nal, bool hevc)
{
	if(nal < 0)
		return false;
	if(hevc)
		return (nal >= 16 && nal <= 23) || (nal >= 32 && nal <= 34);
	return nal == 5 || nal == 7 || nal == 8;
}

static int cmp_size(const void *a, const void *b)
{
	size_t x = *(const size_t *)a, y = *(const size_t *)b;
	return x < y ? -1 : x > y;
}

CHIAKI_EXPORT size_t chiaki_frame_dedup_median(const ChiakiFrameDedup *dedup)
{
	if(dedup->window_count < CHIAKI_DEDUP_WARMUP)
		return 0;
	size_t tmp[CHIAKI_DEDUP_WINDOW];
	memcpy(tmp, dedup->window, sizeof(size_t) * dedup->window_count);
	qsort(tmp, dedup->window_count, sizeof(size_t), cmp_size);
	return tmp[dedup->window_count / 2];
}

static void window_push(ChiakiFrameDedup *dedup, size_t size)
{
	dedup->window[dedup->window_pos] = size;
	dedup->window_pos = (dedup->window_pos + 1) % CHIAKI_DEDUP_WINDOW;
	if(dedup->window_count < CHIAKI_DEDUP_WINDOW)
		dedup->window_count++;
}

CHIAKI_EXPORT int chiaki_frame_dedup_classify(ChiakiFrameDedup *dedup, const uint8_t *buf, size_t buf_size,
		int32_t frames_lost, bool recovered)
{
	if(!dedup->enabled)
		return CHIAKI_DEDUP_SAMPLE_SHOW;

	dedup->samples_seen++;
	dedup->stat_in++;
	int nal = first_nal_type(buf, buf_size, dedup->hevc);
	int decision;

	if(is_parameter_or_key(nal, dedup->hevc))
		decision = CHIAKI_DEDUP_SAMPLE_SHOW;
	else if(frames_lost > 0 || recovered)
	{
		decision = CHIAKI_DEDUP_SAMPLE_SHOW;
		window_push(dedup, buf_size);
	}
	else
	{
		size_t median = chiaki_frame_dedup_median(dedup);
		size_t threshold = (size_t)((double)median * dedup->ratio);
		if(threshold < dedup->min_bytes)
			threshold = dedup->min_bytes;
		if(buf_size < threshold)
			decision = CHIAKI_DEDUP_SAMPLE_REPEAT;
		else
		{
			decision = CHIAKI_DEDUP_SAMPLE_CHECK;
			window_push(dedup, buf_size);
		}
	}

	if(dedup->debug)
		fprintf(stderr, "[dedup] sample size=%zu median=%zu nal=%d -> %s\n",
				buf_size, chiaki_frame_dedup_median(dedup), nal,
				decision == CHIAKI_DEDUP_SAMPLE_SHOW ? "show" : decision == CHIAKI_DEDUP_SAMPLE_REPEAT ? "tiny-repeat" : "check-picture");
	return decision;
}

// Apply the consecutive-drop limit to a "this is a repeat" verdict.
static bool finish(ChiakiFrameDedup *dedup, bool repeat, const char *why)
{
	if(repeat && dedup->consecutive_drops >= dedup->max_drop)
	{
		dedup->stat_forced++;
		repeat = false;
		why = "forced-show";
	}
	if(repeat)
	{
		dedup->consecutive_drops++;
		dedup->stat_dropped++;
	}
	else
		dedup->consecutive_drops = 0;
	if(dedup->debug)
		fprintf(stderr, "[dedup] picture -> %s\n", why);
	return repeat;
}

CHIAKI_EXPORT bool chiaki_frame_dedup_resolve_without_picture(ChiakiFrameDedup *dedup, int sample_decision)
{
	if(!dedup->enabled)
		return false;
	if(sample_decision == CHIAKI_DEDUP_SAMPLE_REPEAT)
		return finish(dedup, true, "tiny-repeat");
	// No picture to compare: show it, and the reference can no longer be trusted.
	dedup->ref_valid = false;
	return finish(dedup, false, "no-picture");
}

static inline int sample8(const uint8_t *row, int x, int bytes, int shift)
{
	if(bytes == 1)
		return row[x];
	unsigned v = (unsigned)row[2 * x] | ((unsigned)row[2 * x + 1] << 8);
	return (int)(v >> shift) & 0xff;
}

CHIAKI_EXPORT bool chiaki_frame_dedup_picture(ChiakiFrameDedup *dedup, const uint8_t *luma, int linesize,
		int width, int height, int bytes_per_sample, int shift_to_8bit, bool may_repeat)
{
	int w = width / SUB, h = height / SUB;
	if(w < BLOCK || h < BLOCK || !luma)
		return chiaki_frame_dedup_resolve_without_picture(dedup, CHIAKI_DEDUP_SAMPLE_CHECK);

	if(!dedup->ref || dedup->ref_w != w || dedup->ref_h != h)
	{
		free(dedup->ref);
		dedup->ref = malloc((size_t)w * (size_t)h);
		dedup->ref_w = w;
		dedup->ref_h = h;
		dedup->ref_valid = false;
		if(!dedup->ref)
			return finish(dedup, false, "no-memory");
	}

	// Compare block-wise against the reference while refreshing it.
	bool had_ref = dedup->ref_valid;
	double worst = 0.0;
	int bw = w / BLOCK, bh = h / BLOCK;
	for(int by = 0; by < bh; by++)
	{
		for(int bx = 0; bx < bw; bx++)
		{
			int sum = 0;
			for(int y = by * BLOCK; y < (by + 1) * BLOCK; y++)
			{
				uint8_t *ref_row = dedup->ref + (size_t)y * (size_t)w;
				for(int x = bx * BLOCK; x < (bx + 1) * BLOCK; x++)
				{
					// box average of the SUB x SUB source pixels of this cell
					int acc = 0;
					if(bytes_per_sample == 1)
					{
						const uint8_t *p = luma + (size_t)(y * SUB) * (size_t)linesize + (size_t)(x * SUB);
						for(int sy = 0; sy < SUB; sy++, p += linesize)
							acc += p[0] + p[1] + p[2] + p[3];
					}
					else
					{
						for(int sy = 0; sy < SUB; sy++)
						{
							const uint8_t *row = luma + (size_t)(y * SUB + sy) * (size_t)linesize;
							for(int sx = 0; sx < SUB; sx++)
								acc += sample8(row, x * SUB + sx, bytes_per_sample, shift_to_8bit);
						}
					}
					int v = (acc + SUB * SUB / 2) / (SUB * SUB);
					sum += abs(v - ref_row[x]);
					ref_row[x] = (uint8_t)v;
				}
			}
			double mad = (double)sum / (BLOCK * BLOCK);
			if(mad > worst)
				worst = mad;
		}
	}
	dedup->ref_valid = true;

	if(dedup->debug)
		fprintf(stderr, "[dedup] picture worst-block diff=%.2f (threshold %.2f)\n", worst, dedup->pixel_threshold);
	if(!may_repeat)
		return finish(dedup, false, "show");
	if(!had_ref || dedup->pixel_threshold <= 0.0)
		return finish(dedup, false, "first-picture");
	return finish(dedup, worst <= dedup->pixel_threshold, worst <= dedup->pixel_threshold ? "picture-repeat" : "picture-changed");
}

CHIAKI_EXPORT void chiaki_frame_dedup_tag(ChiakiFrameDedup *dedup, int64_t pts, int sample_decision)
{
	dedup->tag_pts[dedup->tag_pos] = pts;
	dedup->tag_dec[dedup->tag_pos] = (int8_t)sample_decision;
	dedup->tag_pos = (dedup->tag_pos + 1) % CHIAKI_DEDUP_TAGS;
}

CHIAKI_EXPORT int chiaki_frame_dedup_lookup(ChiakiFrameDedup *dedup, int64_t pts)
{
	for(unsigned int i = 0; i < CHIAKI_DEDUP_TAGS; i++)
		if(dedup->tag_pts[i] == pts)
			return dedup->tag_dec[i];
	return CHIAKI_DEDUP_SAMPLE_SHOW;
}

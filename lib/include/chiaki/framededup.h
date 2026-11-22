// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

/*
 * Automatic removal of repeated frames.
 *
 * When a game renders below the stream rate (e.g. a game at 25-40 fps streamed
 * at 60 fps), the console re-sends the previous picture. The encoder turns a
 * repeated picture into a tiny all-skip P-frame. This module classifies each
 * encoded sample by its size against the median size of recent real frames, so
 * the GUI can avoid presenting duplicates. The decoder still decodes every
 * sample (they are references); only presentation is skipped. That gives a
 * driver-level frame generator (e.g. NVIDIA Smooth Motion) real, distinct
 * frames to interpolate between.
 *
 * Configured through environment variables, read once at init:
 *   CHIAKI_DEDUP=0             disable (default on)
 *   CHIAKI_DEDUP_RATIO=0.10    duplicate if size < ratio * median(real frames)
 *   CHIAKI_DEDUP_MIN_BYTES=256 always duplicate below this size
 *   CHIAKI_DEDUP_MAX_DROP=3    never drop more than this many in a row
 *   CHIAKI_DEDUP_PIXEL=4.0     picture compare: duplicate if no 16x16 block
 *                              changed more than this (mean abs diff, 0-255)
 *                              0 disables picture compare (size only)
 *   CHIAKI_DEDUP_DEBUG=1       print every decision to stderr
 *
 * Two stages: a tiny encoded sample (all-skip P-frame) is a repeat outright.
 * Otherwise the decoded luma is compared block-wise with the previous decoded
 * picture, because encoders often spend spare bits refining a repeated picture,
 * which makes repeats large but visually unchanged.
 */

#ifndef CHIAKI_FRAMEDEDUP_H
#define CHIAKI_FRAMEDEDUP_H

#include <chiaki/common.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHIAKI_DEDUP_WINDOW 32
#define CHIAKI_DEDUP_TAGS 32
#define CHIAKI_DEDUP_WARMUP 8

typedef struct chiaki_frame_dedup_t
{
	bool enabled;
	bool debug;
	bool hevc;
	double ratio;
	size_t min_bytes;
	unsigned int max_drop;
	double pixel_threshold;

	// previous decoded luma, 4x4 box-averaged, 8-bit
	uint8_t *ref;
	int ref_w, ref_h;
	bool ref_valid;

	size_t window[CHIAKI_DEDUP_WINDOW];
	unsigned int window_count;
	unsigned int window_pos;
	unsigned int consecutive_drops;
	uint64_t samples_seen;

	// pts -> duplicate decision, read back when the decoded frame comes out
	int64_t tag_pts[CHIAKI_DEDUP_TAGS];
	int8_t tag_dec[CHIAKI_DEDUP_TAGS];
	unsigned int tag_pos;

	// stats, reset by chiaki_frame_dedup_take_stats
	uint64_t stat_in;
	uint64_t stat_dropped;
	uint64_t stat_forced;
} ChiakiFrameDedup;

/** Reads the environment. hevc selects how NAL headers are parsed. */
CHIAKI_EXPORT void chiaki_frame_dedup_init(ChiakiFrameDedup *dedup, bool hevc);

/** Same as init, but with explicit settings (for tests). */
CHIAKI_EXPORT void chiaki_frame_dedup_init_with(ChiakiFrameDedup *dedup, bool hevc, bool enabled,
		double ratio, size_t min_bytes, unsigned int max_drop, bool debug);

CHIAKI_EXPORT void chiaki_frame_dedup_fini(ChiakiFrameDedup *dedup);

/** Sample decision for a packet, from chiaki_frame_dedup_classify. */
typedef enum {
	CHIAKI_DEDUP_SAMPLE_SHOW = 0,      // keep, no picture check needed (key, after loss, forced)
	CHIAKI_DEDUP_SAMPLE_REPEAT = 1,    // tiny sample: repeat without looking
	CHIAKI_DEDUP_SAMPLE_CHECK = 2      // compare the decoded picture
} ChiakiDedupSample;

/**
 * Stage 2: compare a decoded luma plane with the previous one and update the
 * reference. bytes_per_sample is 1 (8-bit) or 2 (10/16-bit, little endian, MSB
 * aligned as in P010 or LSB aligned as in yuv420p10 - pass shift accordingly).
 * Returns true if the picture is a repeat (never when may_repeat is false, the
 * reference is still refreshed). The consecutive-drop limit applies.
 */
CHIAKI_EXPORT bool chiaki_frame_dedup_picture(ChiakiFrameDedup *dedup, const uint8_t *luma, int linesize,
		int width, int height, int bytes_per_sample, int shift_to_8bit, bool may_repeat);

/** Resolve a sample decision with no picture (picture unavailable). */
CHIAKI_EXPORT bool chiaki_frame_dedup_resolve_without_picture(ChiakiFrameDedup *dedup, int sample_decision);

/**
 * Stage 1: classify one encoded sample (returns a ChiakiDedupSample).
 * frames_lost > 0 or recovered force a keep, so loss reporting and recovery
 * never get swallowed.
 */
CHIAKI_EXPORT int chiaki_frame_dedup_classify(ChiakiFrameDedup *dedup, const uint8_t *buf, size_t buf_size,
		int32_t frames_lost, bool recovered);

/** Remember the sample decision for the packet with this pts. */
CHIAKI_EXPORT void chiaki_frame_dedup_tag(ChiakiFrameDedup *dedup, int64_t pts, int sample_decision);

/** Look up the sample decision for a decoded frame's pts (SHOW if unknown). */
CHIAKI_EXPORT int chiaki_frame_dedup_lookup(ChiakiFrameDedup *dedup, int64_t pts);

/** Current median of real frame sizes (0 while warming up). */
CHIAKI_EXPORT size_t chiaki_frame_dedup_median(const ChiakiFrameDedup *dedup);

#ifdef __cplusplus
}
#endif

#endif // CHIAKI_FRAMEDEDUP_H

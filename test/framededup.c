// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <munit.h>

#include <chiaki/framededup.h>

#include <stdlib.h>

static const uint8_t p_slice[] = { 0, 0, 1, 0x02, 0x01 }; // HEVC TRAIL_R
static const uint8_t vps[] = { 0, 0, 1, 0x40, 0x01 };     // HEVC VPS

static void warm(ChiakiFrameDedup *d)
{
	for(int i = 0; i < CHIAKI_DEDUP_WARMUP; i++)
		munit_assert_int(chiaki_frame_dedup_classify(d, p_slice, 40000, 0, false), ==, CHIAKI_DEDUP_SAMPLE_CHECK);
}

#define W 256
#define H 144
static uint8_t pic[H][W];

static void draw(int offset, int noise_seed)
{
	srand((unsigned)noise_seed);
	for(int y = 0; y < H; y++)
		for(int x = 0; x < W; x++)
			pic[y][x] = (uint8_t)(((x + offset) / 8 + y / 8) % 2 ? 200 : 40) + (uint8_t)(noise_seed ? rand() % 3 : 0);
}

static bool show_pic(ChiakiFrameDedup *d)
{
	return chiaki_frame_dedup_picture(d, &pic[0][0], W, W, H, 1, 0, true);
}

static MunitResult test_disabled(const MunitParameter params[], void *user)
{
	ChiakiFrameDedup d;
	chiaki_frame_dedup_init_with(&d, true, false, 0.1, 256, 3, false);
	munit_assert_int(chiaki_frame_dedup_classify(&d, p_slice, 10, 0, false), ==, CHIAKI_DEDUP_SAMPLE_SHOW);
	munit_assert_false(chiaki_frame_dedup_resolve_without_picture(&d, CHIAKI_DEDUP_SAMPLE_REPEAT));
	return MUNIT_OK;
}

static MunitResult test_sample_stage(const MunitParameter params[], void *user)
{
	ChiakiFrameDedup d;
	chiaki_frame_dedup_init_with(&d, true, true, 0.1, 256, 3, false);
	warm(&d);
	munit_assert_int(chiaki_frame_dedup_classify(&d, p_slice, 200, 0, false), ==, CHIAKI_DEDUP_SAMPLE_REPEAT);
	munit_assert_int(chiaki_frame_dedup_classify(&d, p_slice, 30000, 0, false), ==, CHIAKI_DEDUP_SAMPLE_CHECK);
	munit_assert_int(chiaki_frame_dedup_classify(&d, vps, 100, 0, false), ==, CHIAKI_DEDUP_SAMPLE_SHOW);
	munit_assert_int(chiaki_frame_dedup_classify(&d, p_slice, 100, 1, false), ==, CHIAKI_DEDUP_SAMPLE_SHOW);
	munit_assert_int(chiaki_frame_dedup_classify(&d, p_slice, 100, 0, true), ==, CHIAKI_DEDUP_SAMPLE_SHOW);
	chiaki_frame_dedup_fini(&d);
	return MUNIT_OK;
}

static MunitResult test_picture_stage(const MunitParameter params[], void *user)
{
	ChiakiFrameDedup d;
	chiaki_frame_dedup_init_with(&d, true, true, 0.1, 256, 3, false);
	draw(0, 0);
	munit_assert_false(show_pic(&d)); // first picture
	draw(0, 7);
	munit_assert_true(show_pic(&d));  // same picture, refinement noise
	draw(0, 9);
	munit_assert_true(show_pic(&d));
	draw(3, 0);
	munit_assert_false(show_pic(&d)); // moved
	// a small change in one corner must count as new
	draw(3, 0);
	for(int y = 0; y < 16; y++)
		for(int x = 0; x < 16; x++)
			pic[y][x] = 255;
	munit_assert_false(show_pic(&d));
	// may_repeat=false refreshes but never drops
	munit_assert_false(chiaki_frame_dedup_picture(&d, &pic[0][0], W, W, H, 1, 0, false));
	chiaki_frame_dedup_fini(&d);
	return MUNIT_OK;
}

static MunitResult test_max_drop(const MunitParameter params[], void *user)
{
	ChiakiFrameDedup d;
	chiaki_frame_dedup_init_with(&d, true, true, 0.1, 256, 2, false);
	munit_assert_true(chiaki_frame_dedup_resolve_without_picture(&d, CHIAKI_DEDUP_SAMPLE_REPEAT));
	munit_assert_true(chiaki_frame_dedup_resolve_without_picture(&d, CHIAKI_DEDUP_SAMPLE_REPEAT));
	munit_assert_false(chiaki_frame_dedup_resolve_without_picture(&d, CHIAKI_DEDUP_SAMPLE_REPEAT)); // forced
	munit_assert_true(chiaki_frame_dedup_resolve_without_picture(&d, CHIAKI_DEDUP_SAMPLE_REPEAT));
	return MUNIT_OK;
}

static MunitResult test_tags(const MunitParameter params[], void *user)
{
	ChiakiFrameDedup d;
	chiaki_frame_dedup_init_with(&d, true, true, 0.1, 256, 3, false);
	chiaki_frame_dedup_tag(&d, 1000, CHIAKI_DEDUP_SAMPLE_REPEAT);
	chiaki_frame_dedup_tag(&d, 2000, CHIAKI_DEDUP_SAMPLE_CHECK);
	munit_assert_int(chiaki_frame_dedup_lookup(&d, 1000), ==, CHIAKI_DEDUP_SAMPLE_REPEAT);
	munit_assert_int(chiaki_frame_dedup_lookup(&d, 2000), ==, CHIAKI_DEDUP_SAMPLE_CHECK);
	munit_assert_int(chiaki_frame_dedup_lookup(&d, 3000), ==, CHIAKI_DEDUP_SAMPLE_SHOW);
	return MUNIT_OK;
}

MunitTest tests_framededup[] = {
	{ "/disabled", test_disabled, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/sample_stage", test_sample_stage, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/picture_stage", test_picture_stage, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/max_drop", test_max_drop, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/tags", test_tags, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

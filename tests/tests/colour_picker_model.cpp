// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#include "colour_picker_model.h"

#include <gtest/gtest.h>

TEST(colour_picker_model, converts_ass_alpha_and_opacity) {
	EXPECT_EQ(100, colour_picker::AssAlphaToOpacity(0));
	EXPECT_EQ(0, colour_picker::AssAlphaToOpacity(255));
	EXPECT_EQ(50, colour_picker::AssAlphaToOpacity(128));
	EXPECT_EQ(0, colour_picker::OpacityToAssAlpha(100));
	EXPECT_EQ(255, colour_picker::OpacityToAssAlpha(0));
	EXPECT_EQ(128, colour_picker::OpacityToAssAlpha(50));
}

TEST(colour_picker_model, converts_rgb_hex_and_ass_bgr) {
	auto rgb = colour_picker::ParseHex("#FF7FA8");
	auto ass = colour_picker::ParseHex("&HA87FFF&");
	ASSERT_TRUE(rgb);
	ASSERT_TRUE(ass);
	EXPECT_EQ(*rgb, *ass);
	EXPECT_EQ(agi::Color(255, 127, 168), *rgb);
	EXPECT_EQ("&HA87FFF&", colour_picker::ToAssBgr(*rgb));
	EXPECT_EQ("#FF7FA8", colour_picker::ToRgbHex(*ass));
	EXPECT_FALSE(colour_picker::ParseHex("#FF7F"));
	EXPECT_FALSE(colour_picker::ParseHex("&HNOPE00&"));
}

TEST(colour_picker_model, maps_preview_points_to_original_pixels) {
	auto placement = colour_picker::FitImage(200, 200, 400, 200);
	EXPECT_EQ(0, placement.x);
	EXPECT_EQ(50, placement.y);
	EXPECT_EQ(200, placement.width);
	EXPECT_EQ(100, placement.height);

	EXPECT_FALSE(colour_picker::MapPreviewPoint(200, 200, 400, 200, 10, 49));
	auto top_left = colour_picker::MapPreviewPoint(200, 200, 400, 200, 0, 50);
	auto bottom_right = colour_picker::MapPreviewPoint(200, 200, 400, 200, 199, 149);
	ASSERT_TRUE(top_left);
	ASSERT_TRUE(bottom_right);
	EXPECT_EQ((std::pair{0, 0}), *top_left);
	EXPECT_EQ((std::pair{399, 199}), *bottom_right);

	auto single_pixel = colour_picker::MapPreviewPoint(1, 1, 1, 1, 0, 0);
	ASSERT_TRUE(single_pixel);
	EXPECT_EQ((std::pair{0, 0}), *single_pixel);
}

TEST(colour_picker_model, ranks_by_frequency_then_first_seen_and_deduplicates) {
	agi::Color red(255, 0, 0);
	agi::Color green(0, 255, 0);
	agi::Color blue(0, 0, 255);
	auto ranked = colour_picker::RankColours(
		{red, green, blue, green, red, green, green, agi::Color(255, 0, 0, 200)}, 2);
	ASSERT_EQ(2u, ranked.size());
	EXPECT_EQ(green, ranked[0]);
	EXPECT_EQ(red, ranked[1]);

	auto tied = colour_picker::RankColours(
		{red, green, green, agi::Color(255, 0, 0, 200)}, 2);
	ASSERT_EQ(2u, tied.size());
	EXPECT_EQ(red, tied[0]);
	EXPECT_EQ(green, tied[1]);
}

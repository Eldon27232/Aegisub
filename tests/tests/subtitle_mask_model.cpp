// Copyright (c) 2026, Aegisub Localization Edition contributors

#include "main.h"

#include "subtitle_mask_model.h"

using namespace ass::subtitle_mask;

TEST(subtitle_mask_model, state_change_splits_only_at_a_new_frame_boundary) {
	auto middle = PlanStateChange(0, 9, 1);
	EXPECT_TRUE(middle.valid);
	EXPECT_TRUE(middle.split);
	EXPECT_EQ(0, middle.before_start);
	EXPECT_EQ(0, middle.before_end);
	EXPECT_EQ(1, middle.active_start);
	EXPECT_EQ(9, middle.active_end);

	auto boundary = PlanStateChange(1, 9, 1);
	EXPECT_TRUE(boundary.valid);
	EXPECT_FALSE(boundary.split);
	EXPECT_FALSE(PlanStateChange(1, 9, 10).valid);
}

TEST(subtitle_mask_model, rectangle_replaces_clip_and_cancel_restores_other_ass) {
	auto source = "{\\an8\\clip(1,2,3,4)\\vendor(foo)}字幕";
	auto masked = SetInverseClip(source, {{10, 20}, {80, 20}, {80, 60}, {10, 60}}, true);
	EXPECT_EQ(std::string::npos, masked.find("\\clip("));
	EXPECT_NE(std::string::npos, masked.find("\\iclip(10,20,80,60)"));
	EXPECT_NE(std::string::npos, masked.find("\\vendor(foo)"));
	EXPECT_EQ((std::vector<Point>{{10, 20}, {80, 20}, {80, 60}, {10, 60}}), ParseInverseClip(masked));

	auto cancelled = RemoveInverseClip(masked);
	EXPECT_EQ(std::string::npos, cancelled.find("\\iclip"));
	EXPECT_NE(std::string::npos, cancelled.find("\\vendor(foo)"));
	EXPECT_NE(std::string::npos, cancelled.find("字幕"));
}

TEST(subtitle_mask_model, vector_clip_and_visual_transform_remain_standard_ass) {
	auto masked = SetInverseClip("plain", {{0, 0}, {20, 0}, {10, 20}}, false);
	EXPECT_TRUE(masked.starts_with("{\\iclip(m 0 0 l 20 0 10 20)}"));
	EXPECT_EQ(3U, ParseInverseClip(masked).size());
	auto transformed = Transform(ParseInverseClip(masked), 2.0, 90.0);
	EXPECT_EQ(3U, transformed.size());
	auto updated = SetInverseClip(masked, transformed, false);
	auto first = updated.find("\\iclip");
	ASSERT_NE(std::string::npos, first);
	EXPECT_EQ(std::string::npos, updated.find("\\iclip", first + 1));
}

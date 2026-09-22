// Copyright (c) 2026, Aegisub Localization Edition contributors

#include "main.h"

#include "screen_mask_model.h"

using namespace ass::screen_mask;

TEST(screen_mask_model, resolves_every_duration_mode_in_frame_space) {
	FrameRangeRequest request;
	request.current_frame = 100;
	request.maximum_frame = 200;

	EXPECT_EQ((FrameRange{100, 100}), ResolveFrameRange(request));
	request.mode = DurationMode::FrameCount;
	request.frame_count = 5;
	EXPECT_EQ((FrameRange{100, 104}), ResolveFrameRange(request));
	request.mode = DurationMode::UntilFrame;
	request.end_frame = 140;
	EXPECT_EQ((FrameRange{100, 140}), ResolveFrameRange(request));
	request.mode = DurationMode::UntilSubtitleEnd;
	request.subtitle_end_frame = 180;
	EXPECT_EQ((FrameRange{100, 180}), ResolveFrameRange(request));
	request.mode = DurationMode::CustomRange;
	request.custom_start_frame = 175;
	request.custom_end_frame = 150;
	EXPECT_EQ((FrameRange{150, 175}), ResolveFrameRange(request));
}

TEST(screen_mask_model, emits_standard_ass_drawing_with_human_opacity) {
	auto text = BuildText({{10, 20}, {110, 20}, {110, 70}, {10, 70}}, agi::Color(255, 127, 168), 75);
	EXPECT_NE(std::string::npos, text.find("\\p1"));
	EXPECT_NE(std::string::npos, text.find("\\1c&HA87FFF&"));
	EXPECT_NE(std::string::npos, text.find("\\1a&H40&"));
	EXPECT_NE(std::string::npos, text.find("m -50 -25 l 50 -25 50 25 -50 25"));

	auto geometry = ParseGeometry(text);
	EXPECT_EQ((Point{60, 45}), geometry.position);
	EXPECT_EQ((std::vector<Point>{{10, 20}, {110, 20}, {110, 70}, {10, 70}}), geometry.points);
}

TEST(screen_mask_model, point_edit_preserves_unrelated_ass_content) {
	auto source = BuildText({{0, 0}, {20, 0}, {10, 20}}, agi::Color(0, 0, 0), 100);
	source.insert(source.find("\\p1"), "\\frz15\\vendor(raw)");
	auto updated = UpdateGeometry(source, {{5, 10}, {30, 10}, {20, 40}});
	EXPECT_NE(std::string::npos, updated.find("\\frz15"));
	EXPECT_NE(std::string::npos, updated.find("\\vendor(raw)"));
	EXPECT_EQ((std::vector<Point>{{5, 10}, {30, 10}, {20, 40}}), ParseGeometry(updated).points);
	auto recoloured = UpdateAppearance(updated, agi::Color(12, 34, 56), 50);
	EXPECT_NE(std::string::npos, recoloured.find("\\1c&H38220C&"));
	EXPECT_NE(std::string::npos, recoloured.find("\\1a&H80&"));
	EXPECT_NE(std::string::npos, recoloured.find("\\vendor(raw)"));
}

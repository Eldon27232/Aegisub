// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <libaegisub/color.h>

#include <string>
#include <string_view>
#include <vector>

namespace ass::screen_mask {

enum class DurationMode {
	CurrentFrame,
	FrameCount,
	UntilFrame,
	UntilSubtitleEnd,
	CustomRange
};

struct FrameRangeRequest {
	DurationMode mode = DurationMode::CurrentFrame;
	int current_frame = 0;
	int frame_count = 1;
	int end_frame = 0;
	int subtitle_end_frame = 0;
	int custom_start_frame = 0;
	int custom_end_frame = 0;
	int maximum_frame = 0;
};

struct FrameRange {
	int start = 0;
	int end = 0;
	bool operator==(FrameRange const&) const noexcept = default;
};

struct Point {
	double x = 0.0;
	double y = 0.0;
	bool operator==(Point const&) const noexcept = default;
};

struct Geometry {
	Point position;
	std::vector<Point> points;
};

/// Resolve duration choices entirely in video-frame space.
FrameRange ResolveFrameRange(FrameRangeRequest request);

/// Build a standard ASS Drawing line. Opacity is the familiar 0-100% scale.
std::string BuildText(std::vector<Point> const& absolute_points, agi::Color colour, int opacity);

/// Read the position and Drawing points from an ASS mask created by BuildText.
Geometry ParseGeometry(std::string_view source);

/// Change only the mask position and Drawing payload, preserving other ASS tags.
std::string UpdateGeometry(std::string_view source, std::vector<Point> const& absolute_points);

/// Change the reusable colour/opacity tags without rewriting other ASS syntax.
std::string UpdateAppearance(std::string_view source, agi::Color colour, int opacity);

} // namespace ass::screen_mask

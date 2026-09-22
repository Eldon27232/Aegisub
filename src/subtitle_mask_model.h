// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "screen_mask_model.h"

#include <string>
#include <string_view>
#include <vector>

namespace ass::subtitle_mask {

using Point = screen_mask::Point;

struct StateChangePlan {
	bool valid = false;
	bool split = false;
	int before_start = 0;
	int before_end = -1;
	int active_start = 0;
	int active_end = 0;
};

/// Plan a hold-state change at an exact video frame.
StateChangePlan PlanStateChange(int line_start_frame, int line_end_frame, int current_frame);

/// Replace the effective top-level clip with a standard inverse clip.
std::string SetInverseClip(std::string_view source, std::vector<Point> const& points, bool rectangle);
/// Remove the top-level inverse clip while retaining unrelated ASS syntax.
std::string RemoveInverseClip(std::string_view source);
/// Read a rectangular or vector inverse clip as absolute script coordinates.
std::vector<Point> ParseInverseClip(std::string_view source);

/// Scale and rotate a shape around its bounding-box centre.
std::vector<Point> Transform(std::vector<Point> const& points, double scale, double degrees);

} // namespace ass::subtitle_mask

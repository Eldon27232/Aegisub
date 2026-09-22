// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "frame_segment_model.h"

namespace ass::frame_segment {

StateChangePlan PlanStateChange(int line_start_frame, int line_end_frame, int current_frame) {
	StateChangePlan result;
	if (line_end_frame < line_start_frame || current_frame < line_start_frame || current_frame > line_end_frame)
		return result;
	result.valid = true;
	result.active_start = current_frame;
	result.active_end = line_end_frame;
	if (current_frame > line_start_frame) {
		result.split = true;
		result.before_start = line_start_frame;
		result.before_end = current_frame - 1;
	}
	return result;
}

} // namespace ass::frame_segment

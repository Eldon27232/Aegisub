// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

namespace ass::frame_segment {

/// Property-independent plan for changing a held visual state at a video frame.
struct StateChangePlan {
	bool valid = false;
	bool split = false;
	int before_start = 0;
	int before_end = -1;
	int active_start = 0;
	int active_end = 0;
};

/// Plan against inclusive, exact video-frame boundaries. At an existing
/// boundary the current segment is reused rather than split again.
StateChangePlan PlanStateChange(int line_start_frame, int line_end_frame, int current_frame);

} // namespace ass::frame_segment

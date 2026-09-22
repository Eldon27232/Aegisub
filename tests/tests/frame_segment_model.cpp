// Copyright (c) 2026, Aegisub Localization Edition contributors

#include "main.h"

#include "frame_segment_model.h"

using ass::frame_segment::PlanStateChange;

TEST(frame_segment_model, creates_hold_segments_on_exact_frame_boundaries) {
	auto plan = PlanStateChange(100, 199, 125);
	ASSERT_TRUE(plan.valid);
	EXPECT_TRUE(plan.split);
	EXPECT_EQ(100, plan.before_start);
	EXPECT_EQ(124, plan.before_end);
	EXPECT_EQ(125, plan.active_start);
	EXPECT_EQ(199, plan.active_end);
}

TEST(frame_segment_model, reuses_the_same_boundary_and_splits_at_a_later_frame) {
	auto same_boundary = PlanStateChange(125, 199, 125);
	ASSERT_TRUE(same_boundary.valid);
	EXPECT_FALSE(same_boundary.split);
	EXPECT_EQ(125, same_boundary.active_start);
	EXPECT_EQ(199, same_boundary.active_end);

	auto later = PlanStateChange(same_boundary.active_start, same_boundary.active_end, 150);
	ASSERT_TRUE(later.valid);
	EXPECT_TRUE(later.split);
	EXPECT_EQ(149, later.before_end);
	EXPECT_EQ(150, later.active_start);
}

TEST(frame_segment_model, rejects_frames_outside_the_line) {
	EXPECT_FALSE(PlanStateChange(100, 199, 99).valid);
	EXPECT_FALSE(PlanStateChange(100, 199, 200).valid);
	EXPECT_FALSE(PlanStateChange(200, 199, 200).valid);
}

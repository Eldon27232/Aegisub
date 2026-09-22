// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "screen_mask_model.h"
#include "visual_tool.h"

#include <libaegisub/color.h>

#include <vector>

class wxCommandEvent;
class wxToolBar;

class VisualToolScreenMask final : public VisualToolBase {
public:
	struct Settings {
		ass::screen_mask::DurationMode duration = ass::screen_mask::DurationMode::CurrentFrame;
		int frame_count = 2;
		int until_frame = 0;
		int custom_start_frame = 0;
		int custom_end_frame = 0;
		agi::Color colour{0, 0, 0};
		int opacity = 100;
		int layer = 100;
	};

private:

	enum class ShapeMode { Rectangle, Polygon };

	wxToolBar *toolbar = nullptr;
	ShapeMode shape_mode = ShapeMode::Rectangle;
	Settings settings;
	int subtitle_end_frame = 0;

	std::vector<ass::screen_mask::Point> points;
	std::vector<ass::screen_mask::Point> pending_points;
	std::vector<ass::screen_mask::Point> drag_original;
	Vector2D press_position;
	bool rectangle_drawing = false;
	bool moving_shape = false;
	int dragged_point = -1;

	void DoRefresh() override;
	void OnTool(wxCommandEvent& event);
	void OpenSettings();
	void CreateMask(std::vector<ass::screen_mask::Point> const& new_points);
	void UpdateMask();
	int HitPoint(Vector2D display_point) const;
	bool ContainsPoint(ass::screen_mask::Point point) const;
	ass::screen_mask::Point ScriptPoint(Vector2D display_point) const;
	Vector2D DisplayPoint(ass::screen_mask::Point point) const;
	void DrawPolygon(std::vector<ass::screen_mask::Point> const& polygon, bool close, bool controls);

public:
	VisualToolScreenMask(VideoDisplay *parent, agi::Context *context);
	void SetToolbar(wxToolBar *toolbar) override;
	void SetSubTool(int subtool) override;
	int GetSubTool() override;
	void OnMouseEvent(wxMouseEvent& event) override;
	void Draw() override;
};

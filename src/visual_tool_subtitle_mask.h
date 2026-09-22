// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include "subtitle_mask_model.h"
#include "visual_tool.h"

#include <vector>
#include <memory>

class wxCommandEvent;
class wxToolBar;
class wxMiniFrame;
class wxChoice;
class wxStaticText;

class VisualToolSubtitleMask final : public VisualToolBase {
	using Point = ass::subtitle_mask::Point;
	enum class ShapeMode { Rectangle, Polygon, Select };

	wxToolBar *toolbar = nullptr;
	wxMiniFrame *panel = nullptr;
	wxChoice *shape_choice = nullptr;
	wxStaticText *frame_label = nullptr;
	std::shared_ptr<int> panel_lifetime = std::make_shared<int>(0);
	ShapeMode shape_mode = ShapeMode::Rectangle;
	std::vector<Point> points;
	std::vector<Point> pending_points;
	std::vector<Point> drag_original;
	Vector2D press_position;
	bool rectangle_drawing = false;
	bool moving_shape = false;
	int dragged_point = -1;

	void DoRefresh() override;
	void OnFrameChanged() override;
	void FinishPolygon();
	void OnTool(wxCommandEvent& event);
	void ApplyMask(std::vector<Point> const& new_points, bool rectangle, wxString const& message);
	void CancelMask();
	void TransformMask(double scale, double degrees, wxString const& message);
	void UpdateMask();
	int HitPoint(Vector2D display_point) const;
	bool ContainsPoint(Point point) const;
	Point ScriptPoint(Vector2D display_point) const;
	Vector2D DisplayPoint(Point point) const;
	void DrawPolygon(std::vector<Point> const& polygon, bool close, bool controls);

public:
	VisualToolSubtitleMask(VideoDisplay *parent, agi::Context *context);
	~VisualToolSubtitleMask() override;
	void SetToolbar(wxToolBar *toolbar) override;
	void SetSubTool(int subtool) override;
	int GetSubTool() override;
	void OnMouseEvent(wxMouseEvent& event) override;
	void Draw() override;
};

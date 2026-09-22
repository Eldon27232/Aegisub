// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "visual_tool_subtitle_mask.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "fold_controller.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"

#include <algorithm>
#include <cmath>

#include <wx/button.h>
#include <wx/toolbar.h>

namespace {
enum {
	TOOL_RECTANGLE = wxID_HIGHEST + 6200,
	TOOL_POLYGON
};
}

VisualToolSubtitleMask::VisualToolSubtitleMask(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context)
{
	DoRefresh();
}

void VisualToolSubtitleMask::SetToolbar(wxToolBar *new_toolbar) {
	toolbar = new_toolbar;
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolbar->AddSeparator();
	toolbar->AddTool(TOOL_RECTANGLE, _("Rectangle subtitle mask"), GETBUNDLE(visual_clip, icon_size),
		_("Drag a rectangle to hide only this area of the selected subtitle"), wxITEM_CHECK);
	toolbar->AddTool(TOOL_POLYGON, _("Path subtitle mask"), GETBUNDLE(visual_vector_clip, icon_size),
		_("Click to add points, double-click to finish, or right-click to cancel drawing"), wxITEM_CHECK);

	auto add_button = [&](wxString const& label, wxString const& tip, auto action) {
		auto *button = new wxButton(toolbar, wxID_ANY, label);
		button->SetToolTip(tip);
		button->Bind(wxEVT_BUTTON, action);
		toolbar->AddControl(button);
	};
	add_button(_("Shrink"), _("Shrink by 10% around the mask center"), [this](wxCommandEvent&) {
		TransformMask(0.9, 0.0, _("Shrink subtitle mask"));
	});
	add_button(_("Enlarge"), _("Enlarge by 10% around the mask center"), [this](wxCommandEvent&) {
		TransformMask(1.1, 0.0, _("Enlarge subtitle mask"));
	});
	add_button(_("Rotate left"), _("Rotate the mask 5 degrees counterclockwise"), [this](wxCommandEvent&) {
		TransformMask(1.0, -5.0, _("Rotate subtitle mask"));
	});
	add_button(_("Rotate right"), _("Rotate the mask 5 degrees clockwise"), [this](wxCommandEvent&) {
		TransformMask(1.0, 5.0, _("Rotate subtitle mask"));
	});
	add_button(_("Cancel mask"), _("Cancel this subtitle mask from the current frame"), [this](wxCommandEvent&) {
		CancelMask();
	});

	toolbar->Bind(wxEVT_TOOL, &VisualToolSubtitleMask::OnTool, this, TOOL_RECTANGLE, TOOL_POLYGON);
	SetSubTool(static_cast<int>(shape_mode));
	toolbar->Realize();
	toolbar->Show(true);
}

void VisualToolSubtitleMask::SetSubTool(int subtool) {
	shape_mode = subtool == 1 ? ShapeMode::Polygon : ShapeMode::Rectangle;
	pending_points.clear();
	rectangle_drawing = false;
	if (toolbar) {
		toolbar->ToggleTool(TOOL_RECTANGLE, shape_mode == ShapeMode::Rectangle);
		toolbar->ToggleTool(TOOL_POLYGON, shape_mode == ShapeMode::Polygon);
	}
	parent->Render();
}

int VisualToolSubtitleMask::GetSubTool() {
	return static_cast<int>(shape_mode);
}

void VisualToolSubtitleMask::OnTool(wxCommandEvent& event) {
	SetSubTool(event.GetId() == TOOL_POLYGON ? 1 : 0);
}

VisualToolSubtitleMask::Point VisualToolSubtitleMask::ScriptPoint(Vector2D display_point) const {
	auto point = ToScriptCoords(display_point);
	return {point.X(), point.Y()};
}

Vector2D VisualToolSubtitleMask::DisplayPoint(Point point) const {
	return FromScriptCoords(Vector2D(point.x, point.y));
}

int VisualToolSubtitleMask::HitPoint(Vector2D display_point) const {
	for (size_t i = 0; i < points.size(); ++i) {
		auto delta = DisplayPoint(points[i]) - display_point;
		if (delta.SquareLen() <= 100.0f) return static_cast<int>(i);
	}
	return -1;
}

bool VisualToolSubtitleMask::ContainsPoint(Point point) const {
	if (points.size() < 3) return false;
	bool inside = false;
	for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
		auto const& a = points[i];
		auto const& b = points[j];
		if (((a.y > point.y) != (b.y > point.y)) &&
			(point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x))
			inside = !inside;
	}
	return inside;
}

void VisualToolSubtitleMask::ApplyMask(std::vector<Point> const& new_points, bool rectangle, wxString const& message) {
	if (!active_line || new_points.size() < 3) return;

	std::string changed = ass::subtitle_mask::SetInverseClip(active_line->Text.get(), new_points, rectangle);
	if (changed == active_line->Text.get()) return;

	int line_start = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
	int line_end = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
	int current = c->videoController->GetFrameN();
	auto plan = ass::subtitle_mask::PlanStateChange(line_start, line_end, current);
	if (!plan.valid) return;

	bool split = plan.split;
	if (split) {
		bool was_grouped = active_line->Fold.hasFold() || active_line->Fold.getFoldOpener();
		bool was_opener = active_line->Fold.hasFold() && !active_line->Fold.isEnd();
		bool was_ender = active_line->Fold.hasFold() && active_line->Fold.isEnd();
		auto *before = new AssDialogue(*active_line);
		before->End = c->videoController->TimeAtFrame(plan.before_end, agi::vfr::END);
		active_line->Start = c->videoController->TimeAtFrame(plan.active_start, agi::vfr::START);
		c->ass->Events.insert(c->ass->iterator_to(*active_line), *before);
		if (was_opener) c->ass->DeleteExtradataValue(*active_line, folds_key);
		if (was_ender) c->ass->DeleteExtradataValue(*before, folds_key);
		if (!was_grouped) c->foldController->AddAutomaticFold(*before, *active_line, false);
	}
	active_line->Text = std::move(changed);
	points = new_points;

	int flags = AssFile::COMMIT_DIAG_TEXT;
	if (split) flags |= AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_TIME | AssFile::COMMIT_FOLD;
	file_changed_connection.Block();
	commit_id = c->ass->Commit(message, flags, split ? -1 : commit_id, split ? nullptr : active_line);
	file_changed_connection.Unblock();
	parent->Render();
}

void VisualToolSubtitleMask::CancelMask() {
	if (!active_line) return;
	std::string changed = ass::subtitle_mask::RemoveInverseClip(active_line->Text.get());
	if (changed == active_line->Text.get()) return;

	int line_start = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
	int line_end = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
	int current = c->videoController->GetFrameN();
	auto plan = ass::subtitle_mask::PlanStateChange(line_start, line_end, current);
	if (!plan.valid) return;

	if (plan.split) {
		bool was_grouped = active_line->Fold.hasFold() || active_line->Fold.getFoldOpener();
		bool was_opener = active_line->Fold.hasFold() && !active_line->Fold.isEnd();
		bool was_ender = active_line->Fold.hasFold() && active_line->Fold.isEnd();
		auto *before = new AssDialogue(*active_line);
		before->End = c->videoController->TimeAtFrame(plan.before_end, agi::vfr::END);
		active_line->Start = c->videoController->TimeAtFrame(plan.active_start, agi::vfr::START);
		c->ass->Events.insert(c->ass->iterator_to(*active_line), *before);
		if (was_opener) c->ass->DeleteExtradataValue(*active_line, folds_key);
		if (was_ender) c->ass->DeleteExtradataValue(*before, folds_key);
		if (!was_grouped) c->foldController->AddAutomaticFold(*before, *active_line, false);
	}
	active_line->Text = std::move(changed);
	points.clear();
	int flags = AssFile::COMMIT_DIAG_TEXT;
	if (plan.split) flags |= AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_TIME | AssFile::COMMIT_FOLD;
	file_changed_connection.Block();
	commit_id = c->ass->Commit(_("Cancel subtitle mask"), flags, -1, plan.split ? nullptr : active_line);
	file_changed_connection.Unblock();
	parent->Render();
}

void VisualToolSubtitleMask::TransformMask(double scale, double degrees, wxString const& message) {
	if (points.size() < 3) return;
	auto transformed = ass::subtitle_mask::Transform(points, scale, degrees);
	ApplyMask(transformed, false, message);
}

void VisualToolSubtitleMask::UpdateMask() {
	ApplyMask(points, false, _("Adjust subtitle mask"));
}

void VisualToolSubtitleMask::DoRefresh() {
	points.clear();
	if (active_line)
		points = ass::subtitle_mask::ParseInverseClip(active_line->Text.get());
}

void VisualToolSubtitleMask::OnMouseEvent(wxMouseEvent& event) {
	mouse_pos = event.GetPosition();
	shift_down = event.ShiftDown();
	ctrl_down = event.CmdDown();
	alt_down = event.AltDown();

	if (event.RightDown()) {
		pending_points.clear();
		rectangle_drawing = false;
		if (parent->HasCapture()) parent->ReleaseMouse();
		parent->Render();
		return;
	}

	if (event.LeftDClick() && shape_mode == ShapeMode::Polygon) {
		auto point = ScriptPoint(mouse_pos);
		if (pending_points.empty() || std::abs(pending_points.back().x - point.x) > 0.5 ||
			std::abs(pending_points.back().y - point.y) > 0.5)
			pending_points.push_back(point);
		if (pending_points.size() >= 3)
			ApplyMask(pending_points, false, _("Create subtitle mask"));
		pending_points.clear();
		parent->Render();
		return;
	}

	if (event.LeftDown()) {
		press_position = mouse_pos;
		dragged_point = pending_points.empty() ? HitPoint(mouse_pos) : -1;
		if (dragged_point >= 0) {
			drag_original = points;
			parent->CaptureMouse();
		}
		else if (pending_points.empty() && !points.empty() && ContainsPoint(ScriptPoint(mouse_pos))) {
			moving_shape = true;
			drag_original = points;
			parent->CaptureMouse();
		}
		else if (shape_mode == ShapeMode::Rectangle) {
			rectangle_drawing = true;
			parent->CaptureMouse();
		}
		else {
			pending_points.push_back(ScriptPoint(mouse_pos));
		}
		parent->Render();
		return;
	}

	if (event.Dragging() && event.LeftIsDown()) {
		if (dragged_point >= 0) {
			points = drag_original;
			points[dragged_point] = ScriptPoint(mouse_pos);
			UpdateMask();
		}
		else if (moving_shape) {
			auto start = ScriptPoint(press_position);
			auto current = ScriptPoint(mouse_pos);
			points = drag_original;
			for (auto& point : points) {
				point.x += current.x - start.x;
				point.y += current.y - start.y;
			}
			UpdateMask();
		}
		parent->Render();
		return;
	}

	if (event.LeftUp()) {
		if (rectangle_drawing) {
			auto a = ScriptPoint(press_position);
			auto b = ScriptPoint(mouse_pos);
			if (std::abs(a.x - b.x) >= 2.0 && std::abs(a.y - b.y) >= 2.0)
				ApplyMask({{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}}, true,
					_("Create subtitle mask"));
		}
		rectangle_drawing = false;
		moving_shape = false;
		dragged_point = -1;
		commit_id = -1;
		if (parent->HasCapture()) parent->ReleaseMouse();
		parent->SetFocus();
		parent->Render();
		return;
	}

	if (event.Leaving()) mouse_pos = Vector2D();
	parent->Render();
}

void VisualToolSubtitleMask::DrawPolygon(std::vector<Point> const& polygon, bool close, bool controls) {
	if (polygon.empty()) return;
	auto colour = to_wx(line_color_primary_opt->GetColor());
	gl.SetLineColour(colour, 1.0f, 2);
	for (size_t i = 1; i < polygon.size(); ++i)
		gl.DrawLine(DisplayPoint(polygon[i - 1]), DisplayPoint(polygon[i]));
	if (close && polygon.size() > 2)
		gl.DrawLine(DisplayPoint(polygon.back()), DisplayPoint(polygon.front()));
	if (controls) {
		gl.SetFillColour(colour, 0.45f);
		for (auto const& point : polygon) gl.DrawCircle(DisplayPoint(point), 5);
	}
}

void VisualToolSubtitleMask::Draw() {
	DrawPolygon(points, true, true);
	DrawPolygon(pending_points, false, true);
	if (!pending_points.empty() && mouse_pos)
		gl.DrawDashedLine(DisplayPoint(pending_points.back()), mouse_pos, 6);
	if (rectangle_drawing) {
		auto colour = to_wx(line_color_primary_opt->GetColor());
		gl.SetLineColour(colour, 1.0f, 2);
		gl.SetFillColour(colour, 0.18f);
		gl.DrawRectangle(press_position, mouse_pos);
	}
}

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
#include "theme.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"

#include <algorithm>
#include <cmath>

#include <wx/button.h>
#include <wx/toolbar.h>
#include <wx/minifram.h>
#include <wx/display.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

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

VisualToolSubtitleMask::~VisualToolSubtitleMask() {
	panel_lifetime.reset();
	if (panel) panel->Destroy();
	if (parent->HasCapture()) parent->ReleaseMouse();
}

void VisualToolSubtitleMask::SetToolbar(wxToolBar *new_toolbar) {
	toolbar = new_toolbar;
	panel = new wxMiniFrame(wxGetTopLevelParent(parent), wxID_ANY, _("Subtitle occlusion"), wxDefaultPosition,
		wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT);
	panel->SetName("SubtitleMaskPanel");
	auto life = std::weak_ptr<int>(panel_lifetime);
	auto outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(new wxStaticText(panel, wxID_ANY, _("Hide an area of the selected subtitle, not the video.")), 0, wxALL, 10);
	shape_choice = new wxChoice(panel, wxID_ANY);
	for (auto const& name : {_("Select / adjust"), _("Rectangle"), _("Polygon / path")}) shape_choice->Append(name);
	shape_choice->SetSelection(points.empty() ? 1 : 0);
	shape_mode = points.empty() ? ShapeMode::Rectangle : ShapeMode::Select;
	outer->Add(shape_choice, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
	shape_choice->Bind(wxEVT_CHOICE, [this, life](wxCommandEvent&) {
		if (life.expired()) return;
		int index = shape_choice->GetSelection();
		SetSubTool(index == 0 ? 2 : index - 1);
	});
	auto buttons = new wxGridSizer(2, 5, 5);
	auto button = [&](wxString const& label, auto action) {
		auto control = new wxButton(panel, wxID_ANY, label);
		control->Bind(wxEVT_BUTTON, [life, action](wxCommandEvent&) { if (!life.expired()) action(); });
		buttons->Add(control, 0, wxEXPAND);
	};
	button(_("Close polygon"), [this] { FinishPolygon(); });
	button(_("Cancel mask"), [this] { CancelMask(); });
	button(_("Shrink"), [this] { TransformMask(0.9, 0.0, _("Shrink subtitle mask")); });
	button(_("Enlarge"), [this] { TransformMask(1.1, 0.0, _("Enlarge subtitle mask")); });
	button(_("Rotate left"), [this] { TransformMask(1.0, -5.0, _("Rotate subtitle mask")); });
	button(_("Rotate right"), [this] { TransformMask(1.0, 5.0, _("Rotate subtitle mask")); });
	outer->Add(buttons, 0, wxALL | wxEXPAND, 10);
	frame_label = new wxStaticText(panel, wxID_ANY, "");
	outer->Add(frame_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
	outer->Add(new wxStaticText(panel, wxID_ANY,
		_("Changes replace the mask from this frame onward.\nDrag the shape or its points to adjust it.")), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
	OnFrameChanged();
	panel->SetSizerAndFit(outer);
	panel->Bind(wxEVT_CLOSE_WINDOW, [this, life](wxCloseEvent&) { if (!life.expired()) panel->Hide(); });
	theme::Apply(panel);
	auto anchor = parent->ClientToScreen(wxPoint(parent->GetClientSize().x, 0));
	int display = wxDisplay::GetFromWindow(parent);
	if (display != wxNOT_FOUND) {
		auto area = wxDisplay(display).GetClientArea();
		anchor.x = std::clamp(anchor.x, area.x, std::max(area.x, area.GetRight() - panel->GetSize().x));
		anchor.y = std::clamp(anchor.y, area.y, std::max(area.y, area.GetBottom() - panel->GetSize().y));
	}
	panel->Move(anchor);
	panel->Show();
}

void VisualToolSubtitleMask::OnFrameChanged() {
	if (frame_label) frame_label->SetLabel(wxString::Format(_("Current frame: %d - until subtitle end"), frame_number));
}

void VisualToolSubtitleMask::FinishPolygon() {
	if (pending_points.size() < 3) return;
	ApplyMask(pending_points, false, _("Create subtitle mask"));
	SetSubTool(2);
	parent->Render();
}

void VisualToolSubtitleMask::SetSubTool(int subtool) {
	shape_mode = subtool == 2 ? ShapeMode::Select : subtool == 1 ? ShapeMode::Polygon : ShapeMode::Rectangle;
	pending_points.clear();
	rectangle_drawing = false;
	if (shape_choice) shape_choice->SetSelection(shape_mode == ShapeMode::Select ? 0 : static_cast<int>(shape_mode) + 1);
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
		FinishPolygon();
		pending_points.clear();
		parent->Render();
		return;
	}

	if (event.LeftDown()) {
		if (shape_mode == ShapeMode::Polygon && pending_points.size() >= 3 &&
			(DisplayPoint(pending_points.front()) - mouse_pos).SquareLen() <= 100.0f) {
			FinishPolygon(); return;
		}
		press_position = mouse_pos;
		dragged_point = shape_mode == ShapeMode::Select ? HitPoint(mouse_pos) : -1;
		if (dragged_point >= 0) {
			drag_original = points;
			parent->CaptureMouse();
		}
		else if (shape_mode == ShapeMode::Select && !points.empty() && ContainsPoint(ScriptPoint(mouse_pos))) {
			moving_shape = true;
			drag_original = points;
			parent->CaptureMouse();
		}
		else if (shape_mode == ShapeMode::Rectangle) {
			rectangle_drawing = true;
			parent->CaptureMouse();
		}
		else if (shape_mode == ShapeMode::Polygon) {
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
			if (std::abs(a.x - b.x) >= 2.0 && std::abs(a.y - b.y) >= 2.0) {
				ApplyMask({{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}}, true,
					_("Create subtitle mask"));
				SetSubTool(2);
			}
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

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "visual_tool_screen_mask.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_override_ast.h"
#include "async_video_provider.h"
#include "colour_button.h"
#include "colour_picker_model.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "include/aegisub/video_provider.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "project.h"
#include "selection_controller.h"
#include "theme.h"
#include "timeedit_ctrl.h"
#include "video_controller.h"
#include "video_display.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/minifram.h>
#include <wx/display.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>

namespace {
constexpr char SCREEN_MASK_EFFECT[] = "Aegisub Localization Screen Mask";
enum {
	TOOL_RECTANGLE = wxID_HIGHEST + 6100,
	TOOL_POLYGON
};

using ass::screen_mask::DurationMode;


}

VisualToolScreenMask::VisualToolScreenMask(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context)
{
	settings.until_frame = frame_number;
	settings.custom_start_frame = frame_number;
	settings.custom_end_frame = frame_number;
	auto *line = context->selectionController->GetActiveLine();
	subtitle_end_frame = line
		? context->videoController->FrameAtTime(line->End, agi::vfr::END)
		: frame_number;
	DoRefresh();
}

VisualToolScreenMask::~VisualToolScreenMask() {
	panel_lifetime.reset();
	if (panel) panel->Destroy();
	if (parent->HasCapture()) parent->ReleaseMouse();
}

void VisualToolScreenMask::SetToolbar(wxToolBar *new_toolbar) {
	toolbar = new_toolbar;
	OpenSettings();
}

void VisualToolScreenMask::SetSubTool(int subtool) {
	shape_mode = subtool == 2 ? ShapeMode::Select : subtool == 1 ? ShapeMode::Polygon : ShapeMode::Rectangle;
	pending_points.clear();
	rectangle_drawing = false;
	if (shape_choice) shape_choice->SetSelection(shape_mode == ShapeMode::Select ? 0 : static_cast<int>(shape_mode) + 1);
	parent->Render();
}

int VisualToolScreenMask::GetSubTool() {
	return static_cast<int>(shape_mode);
}

void VisualToolScreenMask::OnTool(wxCommandEvent& event) {
	SetSubTool(event.GetId() == TOOL_POLYGON ? 1 : 0);
}

void VisualToolScreenMask::OpenSettings() {
	if (panel) { panel->Show(); panel->Raise(); return; }
	auto provider = c->project->VideoProvider();
	if (!provider) return;
	int maximum = provider->GetFrameCount() - 1;
	panel = new wxMiniFrame(wxGetTopLevelParent(parent), wxID_ANY, _("Screen mask"), wxDefaultPosition,
		wxDefaultSize, wxCAPTION | wxCLOSE_BOX | wxFRAME_TOOL_WINDOW | wxFRAME_FLOAT_ON_PARENT);
	panel->SetName("ScreenMaskPanel");
	auto life = std::weak_ptr<int>(panel_lifetime);
	auto outer = new wxBoxSizer(wxVERTICAL);
	auto grid = new wxFlexGridSizer(2, 6, 8);
	grid->AddGrowableCol(1);
	auto row = [&](wxString const& label, wxWindow *control) {
		grid->Add(new wxStaticText(panel, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(control, 1, wxEXPAND);
	};
	shape_choice = new wxChoice(panel, wxID_ANY);
	for (auto const& name : {_("Select / adjust"), _("Rectangle"), _("Polygon / path")}) shape_choice->Append(name);
	shape_choice->SetSelection(points.empty() ? 1 : 0);
	shape_mode = points.empty() ? ShapeMode::Rectangle : ShapeMode::Select;
	row(_("Tool"), shape_choice);
	shape_choice->Bind(wxEVT_CHOICE, [this, life](wxCommandEvent&) {
		if (life.expired()) return;
		int index = shape_choice->GetSelection();
		SetSubTool(index == 0 ? 2 : index - 1);
	});
	auto finish = new wxButton(panel, wxID_ANY, _("Close polygon"));
	row(_("Path"), finish);
	finish->Bind(wxEVT_BUTTON, [this, life](wxCommandEvent&) { if (!life.expired()) FinishPolygon(); });
	auto color = new ColourButton(panel, wxSize(90, 20), true, settings.colour, wxDefaultValidator, c);
	row(_("Fill color"), color);
	auto opacity = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, settings.opacity);
	row(_("Opacity (%)"), opacity);
	auto duration = new wxChoice(panel, wxID_ANY);
	for (auto const& name : {_("1 frame"), _("N frames"), _("Until time"), _("Until subtitle end"), _("Custom frame range")}) duration->Append(name);
	duration->SetSelection(static_cast<int>(settings.duration));
	row(_("Duration"), duration);
	auto frames = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, maximum + 1, 1);
	frames->SetName("ScreenMaskFrames");
	row(_("Number of frames"), frames);
	auto until = new TimeEdit(panel, wxID_ANY, c,
		agi::Time(c->videoController->TimeAtFrame(settings.until_frame, agi::vfr::END)).GetAssFormatted(), wxDefaultSize, true);
	row(_("Specified end time"), until);
	auto start = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, settings.custom_start_frame);
	auto end = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, maximum, settings.custom_end_frame);
	row(_("Custom start frame"), start);
	row(_("Custom end frame"), end);
	auto layer = new wxSpinCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 9999, settings.layer);
	row(_("Layer"), layer);
	auto enabled = [duration, frames, until, start, end] {
		auto mode = static_cast<DurationMode>(duration->GetSelection());
		frames->Enable(mode == DurationMode::CurrentFrame || mode == DurationMode::FrameCount);
		until->Enable(mode == DurationMode::UntilFrame);
		start->Enable(mode == DurationMode::CustomRange);
		end->Enable(mode == DurationMode::CustomRange);
	};
	auto apply = [this, life, duration, frames, until, start, end, color, opacity, layer](bool timing) {
		if (life.expired()) return;
		settings.duration = static_cast<DurationMode>(duration->GetSelection());
		settings.frame_count = frames->GetValue();
		settings.until_frame = until->GetFrame();
		settings.custom_start_frame = start->GetValue();
		settings.custom_end_frame = end->GetValue();
		settings.colour = color->GetColor();
		settings.opacity = opacity->GetValue();
		settings.colour.a = colour_picker::OpacityToAssAlpha(settings.opacity);
		color->SetColor(settings.colour);
		settings.layer = layer->GetValue();
		ApplySettings(timing);
	};
	color->Bind(EVT_COLOR, [opacity, apply](ValueEvent<agi::Color>& event) {
		wxEventBlocker block(opacity);
		opacity->SetValue(colour_picker::AssAlphaToOpacity(event.Get().a));
		apply(false);
	});
	opacity->Bind(wxEVT_SPINCTRL, [apply](wxCommandEvent&) { apply(false); });
	opacity->Bind(wxEVT_TEXT, [apply](wxCommandEvent&) { apply(false); });
	layer->Bind(wxEVT_SPINCTRL, [apply](wxCommandEvent&) { apply(false); });
	layer->Bind(wxEVT_TEXT, [apply](wxCommandEvent&) { apply(false); });
	duration->Bind(wxEVT_CHOICE, [apply, enabled, frames, duration](wxCommandEvent&) {
		if (duration->GetSelection() == 0) { wxEventBlocker block(frames); frames->SetValue(1); }
		enabled(); apply(true);
	});
	auto frame_change = [apply, duration](wxCommandEvent&) { duration->SetSelection(1); apply(true); };
	frames->Bind(wxEVT_SPINCTRL, frame_change);
	frames->Bind(wxEVT_TEXT, frame_change);
	until->Bind(wxEVT_TEXT, [apply](wxCommandEvent&) { apply(true); });
	for (auto control : {start, end}) {
		control->Bind(wxEVT_SPINCTRL, [apply](wxCommandEvent&) { apply(true); });
		control->Bind(wxEVT_TEXT, [apply](wxCommandEvent&) { apply(true); });
	}
	sync_panel = [this, color, opacity, layer, duration, frames, until, start, end, enabled] {
		auto set = [](wxSpinCtrl *control, int value) { wxEventBlocker block(control); control->SetValue(value); };
		color->SetColor(settings.colour);
		set(opacity, settings.opacity); set(layer, settings.layer);
		set(frames, settings.frame_count); set(start, settings.custom_start_frame); set(end, settings.custom_end_frame);
		duration->SetSelection(static_cast<int>(settings.duration));
		enabled();
	};
	sync_panel();
	outer->Add(grid, 1, wxALL | wxEXPAND, 10);
	outer->Add(new wxStaticText(panel, wxID_ANY, _("Drag a rectangle.\nFor a path, click points, then click the first point.")), 0, wxALL, 10);
	panel->SetSizerAndFit(outer);
	panel->Bind(wxEVT_CLOSE_WINDOW, [this, life](wxCloseEvent&) { if (!life.expired()) panel->Hide(); });
	enabled();
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

void VisualToolScreenMask::ApplySettings(bool timing) {
	if (active_line && active_line->Effect.get() == SCREEN_MASK_EFFECT) {
		active_line->Text = ass::screen_mask::UpdateAppearance(active_line->Text.get(), settings.colour, settings.opacity);
		active_line->Layer = settings.layer;
		if (timing) {
			ass::screen_mask::FrameRangeRequest request;
			request.mode = settings.duration;
			request.current_frame = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
			request.frame_count = settings.frame_count;
			request.end_frame = settings.until_frame;
			request.subtitle_end_frame = subtitle_end_frame;
			request.custom_start_frame = settings.custom_start_frame;
			request.custom_end_frame = settings.custom_end_frame;
			request.maximum_frame = c->project->VideoProvider()->GetFrameCount() - 1;
			auto range = ass::screen_mask::ResolveFrameRange(request);
			active_line->Start = c->videoController->TimeAtFrame(range.start, agi::vfr::START);
			active_line->End = c->videoController->TimeAtFrame(range.end, agi::vfr::END);
		}
		file_changed_connection.Block();
		commit_id = c->ass->Commit(_("Change screen mask appearance"), AssFile::COMMIT_DIAG_FULL, commit_id, active_line);
		file_changed_connection.Unblock();
	}
	parent->Render();
}

void VisualToolScreenMask::FinishPolygon() {
	if (pending_points.size() >= 3) CreateMask(pending_points);
	pending_points.clear();
	parent->Render();
}

ass::screen_mask::Point VisualToolScreenMask::ScriptPoint(Vector2D display_point) const {
	auto point = ToScriptCoords(display_point);
	return {point.X(), point.Y()};
}

Vector2D VisualToolScreenMask::DisplayPoint(ass::screen_mask::Point point) const {
	return FromScriptCoords(Vector2D(point.x, point.y));
}

int VisualToolScreenMask::HitPoint(Vector2D display_point) const {
	for (size_t i = 0; i < points.size(); ++i) {
		auto delta = DisplayPoint(points[i]) - display_point;
		if (delta.SquareLen() <= 100.0f) return static_cast<int>(i);
	}
	return -1;
}

bool VisualToolScreenMask::ContainsPoint(ass::screen_mask::Point point) const {
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

void VisualToolScreenMask::CreateMask(std::vector<ass::screen_mask::Point> const& new_points) {
	if (new_points.size() < 3) return;
	auto provider = c->project->VideoProvider();
	if (!provider) return;

	ass::screen_mask::FrameRangeRequest request;
	request.mode = settings.duration;
	request.current_frame = c->videoController->GetFrameN();
	request.frame_count = settings.frame_count;
	request.end_frame = settings.until_frame;
	request.subtitle_end_frame = subtitle_end_frame;
	request.custom_start_frame = settings.custom_start_frame;
	request.custom_end_frame = settings.custom_end_frame;
	request.maximum_frame = provider->GetFrameCount() - 1;
	auto range = ass::screen_mask::ResolveFrameRange(request);

	auto *anchor = c->selectionController->GetActiveLine();
	auto *created = new AssDialogue;
	if (anchor) created->Style = anchor->Style;
	created->Layer = settings.layer;
	created->Start = c->videoController->TimeAtFrame(range.start, agi::vfr::START);
	created->End = c->videoController->TimeAtFrame(range.end, agi::vfr::END);
	created->Effect = SCREEN_MASK_EFFECT;
	created->Text = ass::screen_mask::BuildText(new_points, settings.colour, settings.opacity);
	if (anchor)
		c->ass->Events.insert(++c->ass->iterator_to(*anchor), *created);
	else
		c->ass->Events.push_back(*created);
	c->ass->Commit(_("Create screen mask"), AssFile::COMMIT_DIAG_ADDREM);
	c->selectionController->SetSelectionAndActive({created}, created);
	points = new_points;
	SetSubTool(2);
	parent->Render();
}

void VisualToolScreenMask::UpdateMask() {
	if (!active_line || active_line->Effect.get() != SCREEN_MASK_EFFECT || points.size() < 3) return;
	active_line->Text = ass::screen_mask::UpdateGeometry(active_line->Text.get(), points);
	Commit(_("Adjust screen mask"));
}

void VisualToolScreenMask::DoRefresh() {
	points.clear();
	if (!active_line || active_line->Effect.get() != SCREEN_MASK_EFFECT) return;
	points = ass::screen_mask::ParseGeometry(active_line->Text.get()).points;
	auto document = ass::ast::Document::Parse(active_line->Text.get());
	auto colors = document.FindTags("\\1c", false);
	if (!colors.empty() && !colors.front()->Arguments().empty())
		if (auto value = colour_picker::ParseHex(colors.front()->Arguments()[0])) settings.colour = *value;
	auto alphas = document.FindTags("\\1a", false);
	if (!alphas.empty() && !alphas.front()->Arguments().empty()) {
		auto const& raw = alphas.front()->Arguments()[0];
		if (raw.size() >= 3 && raw[0] == '&') settings.colour.a = std::strtoul(raw.c_str() + 2, nullptr, 16);
	}
	settings.opacity = colour_picker::AssAlphaToOpacity(settings.colour.a);
	settings.layer = active_line->Layer;
	settings.custom_start_frame = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
	settings.custom_end_frame = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
	settings.frame_count = settings.custom_end_frame - settings.custom_start_frame + 1;
	settings.duration = settings.frame_count == 1 ? DurationMode::CurrentFrame : DurationMode::FrameCount;
	if (sync_panel) sync_panel();
}

void VisualToolScreenMask::OnMouseEvent(wxMouseEvent& event) {
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
		if (pending_points.empty() || std::abs(pending_points.back().x - point.x) > 0.5 || std::abs(pending_points.back().y - point.y) > 0.5)
			pending_points.push_back(point);
		if (pending_points.size() >= 3) CreateMask(pending_points);
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
			if (std::abs(a.x - b.x) >= 2.0 && std::abs(a.y - b.y) >= 2.0)
				CreateMask({{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}});
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

void VisualToolScreenMask::DrawPolygon(std::vector<ass::screen_mask::Point> const& polygon, bool close, bool controls) {
	if (polygon.empty()) return;
	auto line_colour = theme::GetPalette().accent;
	gl.SetLineColour(line_colour, 1.0f, 2);
	for (size_t i = 1; i < polygon.size(); ++i)
		gl.DrawLine(DisplayPoint(polygon[i - 1]), DisplayPoint(polygon[i]));
	if (close && polygon.size() > 2)
		gl.DrawLine(DisplayPoint(polygon.back()), DisplayPoint(polygon.front()));
	if (controls) {
		gl.SetFillColour(line_colour, 0.45f);
		for (auto const& point : polygon) gl.DrawCircle(DisplayPoint(point), 5);
	}
}

void VisualToolScreenMask::Draw() {
	DrawPolygon(points, true, true);
	DrawPolygon(pending_points, false, true);
	if (!pending_points.empty() && mouse_pos)
		gl.DrawDashedLine(DisplayPoint(pending_points.back()), mouse_pos, 6);
	if (rectangle_drawing) {
		gl.SetLineColour(theme::GetPalette().accent, 1.0f, 2);
		gl.SetFillColour(to_wx(settings.colour), settings.opacity / 300.0f);
		gl.DrawRectangle(press_position, mouse_pos);
	}
}

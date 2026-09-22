// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "visual_tool_screen_mask.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "async_video_provider.h"
#include "colour_button.h"
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

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
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

class ScreenMaskSettingsDialog final : public wxDialog {
	VisualToolScreenMask::Settings value;
	agi::Context *context;
	wxChoice *duration;
	wxSpinCtrl *frame_count;
	TimeEdit *until_time;
	wxSpinCtrl *custom_start;
	wxSpinCtrl *custom_end;
	ColourButton *colour;
	wxSpinCtrl *opacity;
	wxSpinCtrl *layer;

	void UpdateEnabled() {
		auto selected = static_cast<DurationMode>(duration->GetSelection());
		frame_count->Enable(selected == DurationMode::FrameCount);
		until_time->Enable(selected == DurationMode::UntilFrame);
		custom_start->Enable(selected == DurationMode::CustomRange);
		custom_end->Enable(selected == DurationMode::CustomRange);
	}

public:
	ScreenMaskSettingsDialog(wxWindow *parent, agi::Context *context,
		VisualToolScreenMask::Settings const& initial, int maximum_frame)
	: wxDialog(parent, wxID_ANY, _("Screen mask settings"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, value(initial)
	, context(context)
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		auto *grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(10));
		grid->AddGrowableCol(1, 1);

		grid->Add(new wxStaticText(this, wxID_ANY, _("Duration")), wxSizerFlags().CenterVertical());
		duration = new wxChoice(this, wxID_ANY);
		duration->Append(_("Current frame only (default)"));
		duration->Append(_("Multiple frames from the current frame"));
		duration->Append(_("From the current frame to a specified time"));
		duration->Append(_("From the current frame to the end of the current subtitle"));
		duration->Append(_("Custom start and end frames"));
		duration->SetSelection(static_cast<int>(value.duration));
		grid->Add(duration, wxSizerFlags(1).Expand());

		grid->Add(new wxStaticText(this, wxID_ANY, _("Number of frames")), wxSizerFlags().CenterVertical());
		frame_count = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 1, std::max(1, maximum_frame + 1), value.frame_count);
		grid->Add(frame_count, wxSizerFlags(1).Expand());

		grid->Add(new wxStaticText(this, wxID_ANY, _("Specified end time")), wxSizerFlags().CenterVertical());
		until_time = new TimeEdit(this, wxID_ANY, context,
			agi::Time(context->videoController->TimeAtFrame(value.until_frame, agi::vfr::END)).GetAssFormatted(),
			wxDefaultSize, true);
		grid->Add(until_time, wxSizerFlags(1).Expand());

		grid->Add(new wxStaticText(this, wxID_ANY, _("Custom start frame")), wxSizerFlags().CenterVertical());
		custom_start = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 0, maximum_frame, value.custom_start_frame);
		grid->Add(custom_start, wxSizerFlags(1).Expand());
		grid->Add(new wxStaticText(this, wxID_ANY, _("Custom end frame")), wxSizerFlags().CenterVertical());
		custom_end = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 0, maximum_frame, value.custom_end_frame);
		grid->Add(custom_end, wxSizerFlags(1).Expand());

		grid->Add(new wxStaticText(this, wxID_ANY, _("Mask color")), wxSizerFlags().CenterVertical());
		colour = new ColourButton(this, FromDIP(wxSize(80, 18)), false, value.colour, wxDefaultValidator, context);
		grid->Add(colour, wxSizerFlags().Left());
		grid->Add(new wxStaticText(this, wxID_ANY, _("Opacity (0-100%)")), wxSizerFlags().CenterVertical());
		opacity = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 0, 100, value.opacity);
		grid->Add(opacity, wxSizerFlags(1).Expand());
		grid->Add(new wxStaticText(this, wxID_ANY, _("Layer")), wxSizerFlags().CenterVertical());
		layer = new wxSpinCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, 0, 9999, value.layer);
		grid->Add(layer, wxSizerFlags(1).Expand());

		sizer->Add(new wxStaticText(this, wxID_ANY,
			_("The color button uses the shared palette, eyedropper, recent colors, and frequently used project colors.")),
			wxSizerFlags().Expand().Border(wxALL));
		sizer->Add(grid, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
		sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL));
		SetSizerAndFit(sizer);
		SetMinSize(FromDIP(wxSize(520, -1)));
		duration->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateEnabled(); });
		UpdateEnabled();
		theme::Apply(this);
	}

	VisualToolScreenMask::Settings Values() const {
		auto result = value;
		result.duration = static_cast<DurationMode>(duration->GetSelection());
		result.frame_count = frame_count->GetValue();
		result.until_frame = until_time->GetFrame();
		result.custom_start_frame = custom_start->GetValue();
		result.custom_end_frame = custom_end->GetValue();
		result.colour = colour->GetColor();
		result.opacity = opacity->GetValue();
		result.layer = layer->GetValue();
		return result;
	}
};
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

void VisualToolScreenMask::SetToolbar(wxToolBar *new_toolbar) {
	toolbar = new_toolbar;
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolbar->AddSeparator();
	toolbar->AddTool(TOOL_RECTANGLE, _("Rectangle mask"), GETBUNDLE(visual_clip, icon_size),
		_("Drag to draw a rectangular screen mask"), wxITEM_CHECK);
	toolbar->AddTool(TOOL_POLYGON, _("Polygon mask"), GETBUNDLE(visual_vector_clip, icon_size),
		_("Click to add points, double-click to finish the polygon, or right-click to cancel"), wxITEM_CHECK);
	auto *settings_button = new wxButton(toolbar, wxID_ANY, _("Color / Time / Layer"));
	settings_button->SetToolTip(_("Set mask color, opacity, layer, and frame-based duration"));
	toolbar->AddControl(settings_button);
	toolbar->Bind(wxEVT_TOOL, &VisualToolScreenMask::OnTool, this, TOOL_RECTANGLE, TOOL_POLYGON);
	settings_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenSettings(); });
	SetSubTool(static_cast<int>(shape_mode));
	toolbar->Realize();
	toolbar->Show(true);
}

void VisualToolScreenMask::SetSubTool(int subtool) {
	shape_mode = subtool == 1 ? ShapeMode::Polygon : ShapeMode::Rectangle;
	pending_points.clear();
	rectangle_drawing = false;
	if (toolbar) {
		toolbar->ToggleTool(TOOL_RECTANGLE, shape_mode == ShapeMode::Rectangle);
		toolbar->ToggleTool(TOOL_POLYGON, shape_mode == ShapeMode::Polygon);
	}
	parent->Render();
}

int VisualToolScreenMask::GetSubTool() {
	return static_cast<int>(shape_mode);
}

void VisualToolScreenMask::OnTool(wxCommandEvent& event) {
	SetSubTool(event.GetId() == TOOL_POLYGON ? 1 : 0);
}

void VisualToolScreenMask::OpenSettings() {
	auto provider = c->project->VideoProvider();
	if (!provider) return;
	ScreenMaskSettingsDialog dialog(toolbar, c, settings, provider->GetFrameCount() - 1);
	if (dialog.ShowModal() != wxID_OK) return;
	settings = dialog.Values();
	if (active_line && active_line->Effect.get() == SCREEN_MASK_EFFECT) {
		active_line->Text = ass::screen_mask::UpdateAppearance(active_line->Text.get(), settings.colour, settings.opacity);
		active_line->Layer = settings.layer;
		c->ass->Commit(_("Change screen mask appearance"), AssFile::COMMIT_DIAG_FULL, -1, active_line);
	}
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
	auto line_colour = to_wx(settings.colour);
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
		gl.SetLineColour(to_wx(settings.colour), 1.0f, 2);
		gl.SetFillColour(to_wx(settings.colour), settings.opacity / 300.0f);
		gl.DrawRectangle(press_position, mouse_pos);
	}
}

// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file visual_tool_drag.cpp
/// @brief Position all visible subtitles by dragging visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_drag.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "compat.h"
#include "fold_controller.h"
#include "frame_segment_model.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <boost/range/algorithm/binary_search.hpp>

#include <wx/toolbar.h>

namespace {
enum {
	TOOL_MOVE_OR_POS = wxID_HIGHEST + 6300,
	TOOL_FRAME_SEGMENTS
};
}

static const DraggableFeatureType DRAG_ORIGIN = DRAG_BIG_TRIANGLE;
static const DraggableFeatureType DRAG_START = DRAG_BIG_SQUARE;
static const DraggableFeatureType DRAG_END = DRAG_BIG_CIRCLE;

VisualToolDrag::VisualToolDrag(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualToolDragDraggableFeature>(parent, context)
{
	segment_by_frame = OPT_GET("Tool/Visual/Frame Segments")->GetBool();
	connections.push_back(c->selectionController->AddSelectionListener(&VisualToolDrag::OnSelectedSetChanged, this));
	auto const& sel_set = c->selectionController->GetSelectedSet();
	selection.insert(begin(selection), begin(sel_set), end(sel_set));
}

void VisualToolDrag::SetToolbar(wxToolBar *tb) {
	toolbar = tb;
	move_button_id = TOOL_MOVE_OR_POS;
	segment_button_id = TOOL_FRAME_SEGMENTS;
	toolbar->AddSeparator();
	toolbar->AddTool(move_button_id, _("Toggle between \\move and \\pos"), GETBUNDLE(visual_move_conv_move, OPT_GET("App/Toolbar Icon Size")->GetInt()));
	toolbar->AddTool(segment_button_id, _("Segment by frame"), GETBUNDLE(visual_move, OPT_GET("App/Toolbar Icon Size")->GetInt()),
		_("When enabled, dragging and arrow-key nudging creates a held state from the current exact video frame"), wxITEM_CHECK);
	toolbar->ToggleTool(segment_button_id, segment_by_frame);
	toolbar->Realize();
	toolbar->Show(true);

	toolbar->Bind(wxEVT_TOOL, &VisualToolDrag::OnSubTool, this);
}

void VisualToolDrag::UpdateToggleButtons() {
	bool to_move = true;
	if (active_line) {
		Vector2D p1, p2;
		int t1, t2;
		to_move = !GetLineMove(active_line, p1, p2, t1, t2);
	}

	if (to_move == button_is_move) return;

	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolbar->SetToolNormalBitmap(toolbar->GetToolByPos(1)->GetId(),
		to_move ? GETBUNDLE(visual_move_conv_move, icon_size) : GETBUNDLE(visual_move_conv_pos, icon_size));
	button_is_move = to_move;
}

void VisualToolDrag::OnSubTool(wxCommandEvent &event) {
	if (event.GetId() == segment_button_id) {
		segment_by_frame = toolbar->GetToolState(segment_button_id);
		OPT_SET("Tool/Visual/Frame Segments")->SetBool(segment_by_frame);
		return;
	}
	if (event.GetId() != move_button_id) return;

	// Toggle \move <-> \pos
	VideoController *vc = c->videoController.get();
	for (auto line : selection) {
		Vector2D p1, p2;
		int t1, t2;

		bool has_move = GetLineMove(line, p1, p2, t1, t2);

		if (has_move)
			SetOverride(line, "\\pos", p1.PStr());
		else {
			p1 = GetLinePosition(line);
			// Round the start and end times to exact frames
			int start = vc->TimeAtFrame(vc->FrameAtTime(line->Start, agi::vfr::START)) - line->Start;
			int end = vc->TimeAtFrame(vc->FrameAtTime(line->Start, agi::vfr::END)) - line->Start;
			SetOverride(line, "\\move", agi::format("(%s,%s,%d,%d)", p1.Str(), p1.Str(), start, end));
		}
	}

	Commit();
	OnFileChanged();
	UpdateToggleButtons();
}

void VisualToolDrag::OnLineChanged() {
	UpdateToggleButtons();
}

void VisualToolDrag::OnFileChanged() {
	/// @todo it should be possible to preserve the selection in some cases
	features.clear();
	sel_features.clear();
	primary = nullptr;
	active_feature = nullptr;

	for (auto& diag : c->ass->Events) {
		if (IsDisplayed(&diag))
			MakeFeatures(&diag);
	}

	UpdateToggleButtons();
}

void VisualToolDrag::OnFrameChanged() {
	if (primary && !IsDisplayed(primary->line))
		primary = nullptr;

	auto feat = features.begin();
	auto end = features.end();

	for (auto& diag : c->ass->Events) {
		if (IsDisplayed(&diag)) {
			// Features don't exist and should
			if (feat == end || feat->line != &diag)
				MakeFeatures(&diag, feat);
			// Move past already existing features for the line
			else
				while (feat != end && feat->line == &diag) ++feat;
		}
		else {
			// Remove all features for this line (if any)
			while (feat != end && feat->line == &diag) {
				if (&*feat == active_feature) active_feature = nullptr;
				feat->line = nullptr;
				RemoveSelection(&*feat);
				feat = features.erase(feat);
			}
		}
	}
}

template<class C, class T> static bool line_not_present(C const& set, T const& it) {
	return std::none_of(set.begin(), set.end(), [&](typename C::value_type const& cmp) {
		return cmp->line == it->line;
	});
}

void VisualToolDrag::OnSelectedSetChanged() {
	auto const& new_sel_set = c->selectionController->GetSelectedSet();
	std::vector<AssDialogue *> new_sel(begin(new_sel_set), end(new_sel_set));

	bool any_changed = false;
	for (auto it = features.begin(); it != features.end(); ) {
		bool was_selected = boost::binary_search(selection, it->line);
		bool is_selected = boost::binary_search(new_sel, it->line);
		if (was_selected && !is_selected) {
			sel_features.erase(&*it++);
			any_changed = true;
		}
		else {
			if (is_selected && !was_selected && it->type == DRAG_START && line_not_present(sel_features, it)) {
				sel_features.insert(&*it);
				any_changed = true;
			}
			++it;
		}
	}

	if (any_changed)
		parent->Render();
	selection = std::move(new_sel);
}

void VisualToolDrag::Draw() {
	DrawAllFeatures();

	// Load colors from options
	wxColour line_color = to_wx(line_color_primary_opt->GetColor());

	// Draw connecting lines
	for (auto& feature : features) {
		if (feature.type == DRAG_START) continue;

		Feature *p2 = &feature;
		Feature *p1 = feature.parent;

		// Move end marker has an arrow; origin doesn't
		bool has_arrow = p2->type == DRAG_END;
		int arrow_len = has_arrow ? 10 : 0;

		// Don't show the connecting line if the features are very close
		Vector2D direction = p2->pos - p1->pos;
		if (direction.SquareLen() < (20 + arrow_len) * (20 + arrow_len)) continue;

		direction = direction.Unit();
		// Get the start and end points of the line
		Vector2D start = p1->pos + direction * 10;
		Vector2D end = p2->pos - direction * (10 + arrow_len);

		if (has_arrow) {
			gl.SetLineColour(line_color, 0.8f, 2);

			// Arrow line
			gl.DrawLine(start, end);

			// Arrow head
			Vector2D t_half_base_w = Vector2D(-direction.Y(), direction.X()) * 4;
			gl.DrawTriangle(end + direction * arrow_len, end + t_half_base_w, end - t_half_base_w);
		}
		// Draw dashed line
		else {
			gl.SetLineColour(line_color, 0.5f, 2);
			gl.DrawDashedLine(start, end, 6);
		}
	}
}

void VisualToolDrag::MakeFeatures(AssDialogue *diag) {
	MakeFeatures(diag, features.end());
}

void VisualToolDrag::MakeFeatures(AssDialogue *diag, feature_list::iterator pos) {
	Vector2D p1 = FromScriptCoords(GetLinePosition(diag));

	// Create \pos feature
	auto feat = std::make_unique<Feature>();
	auto parent = feat.get();
	feat->pos = p1;
	feat->type = DRAG_START;
	feat->line = diag;

	if (boost::binary_search(selection, diag))
		sel_features.insert(feat.get());
	features.insert(pos, *feat.release());

	Vector2D p2;
	int t1, t2;

	// Create move destination feature
	if (GetLineMove(diag, p1, p2, t1, t2)) {
		feat = std::make_unique<Feature>();
		feat->pos = FromScriptCoords(p2);
		feat->layer = 1;
		feat->type = DRAG_END;
		feat->time = t2;
		feat->line = diag;
		feat->parent = parent;

		parent->time = t1;
		parent->parent = feat.get();

		features.insert(pos, *feat.release());
	}

	// Create org feature
	if (Vector2D org = GetLineOrigin(diag)) {
		feat = std::make_unique<Feature>();
		feat->pos = FromScriptCoords(org);
		feat->layer = -1;
		feat->type = DRAG_ORIGIN;
		feat->time = 0;
		feat->line = diag;
		feat->parent = parent;
		features.insert(pos, *feat.release());
	}
}

bool VisualToolDrag::InitializeDrag(Feature *feature) {
	primary = feature;
	pending_segment_change = false;

	// Set time of clicked feature to the current frame and shift all other
	// selected features by the same amount
	if (feature->type != DRAG_ORIGIN) {
		int time = c->videoController->TimeAtFrame(frame_number) - feature->line->Start;
		int change = time - feature->time;

		for (auto feat : sel_features)
			feat->time += change;
	}
	return true;
}

bool VisualToolDrag::EnsureFrameSegment(Feature *feature) {
	if (!segment_by_frame || !feature || !feature->line) return false;
	auto *line = feature->line;
	int start = c->videoController->FrameAtTime(line->Start, agi::vfr::START);
	int end = c->videoController->FrameAtTime(line->End, agi::vfr::END);
	auto plan = ass::frame_segment::PlanStateChange(start, end, frame_number);
	if (!plan.valid || !plan.split) return false;

	bool was_grouped = line->Fold.hasFold() || line->Fold.getFoldOpener();
	bool was_opener = line->Fold.hasFold() && !line->Fold.isEnd();
	bool was_ender = line->Fold.hasFold() && line->Fold.isEnd();
	auto *before = new AssDialogue(*line);
	before->End = c->videoController->TimeAtFrame(plan.before_end, agi::vfr::END);
	line->Start = c->videoController->TimeAtFrame(plan.active_start, agi::vfr::START);
	c->ass->Events.insert(c->ass->iterator_to(*line), *before);

	// A copied fold delimiter must stay on only one physical line. This keeps
	// an existing logical group flat while its segment count grows.
	if (was_opener)
		c->ass->DeleteExtradataValue(*line, folds_key);
	if (was_ender)
		c->ass->DeleteExtradataValue(*before, folds_key);
	if (!was_grouped)
		c->foldController->AddAutomaticFold(*before, *line, false);

	pending_segment_change = true;
	return true;
}

void VisualToolDrag::UpdateDrag(Feature *feature) {
	EnsureFrameSegment(feature);
	if (feature->type == DRAG_ORIGIN) {
		SetOverride(feature->line, "\\org", ToScriptCoords(feature->pos).PStr());
		return;
	}

	Feature *end_feature = feature->parent;
	if (feature->type == DRAG_END)
		std::swap(feature, end_feature);

	if (!feature->parent)
		SetOverride(feature->line, "\\pos", ToScriptCoords(feature->pos).PStr());
	else
		SetOverride(feature->line, "\\move", agi::format("(%s,%s,%d,%d)"
			, ToScriptCoords(feature->pos).Str()
			, ToScriptCoords(end_feature->pos).Str()
			, feature->time , end_feature->time));
}

void VisualToolDrag::Commit(wxString message) {
	file_changed_connection.Block();
	if (message.empty())
		message = segment_by_frame ? _("Adjust subtitle position by frame") : _("visual typesetting");
	int flags = AssFile::COMMIT_DIAG_TEXT;
	if (pending_segment_change)
		flags |= AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_TIME | AssFile::COMMIT_FOLD;
	commit_id = c->ass->Commit(message, flags, commit_id);
	pending_segment_change = false;
	file_changed_connection.Unblock();
}

bool VisualToolDrag::OnKeyDown(wxKeyEvent &event) {
	if (!primary || event.CmdDown() || event.AltDown() || event.ShiftDown()) return false;
	int dx = 0;
	int dy = 0;
	switch (event.GetKeyCode()) {
		case WXK_LEFT:  dx = -1; break;
		case WXK_RIGHT: dx = 1; break;
		case WXK_UP:    dy = -1; break;
		case WXK_DOWN:  dy = 1; break;
		default: return false;
	}

	auto script_position = ToScriptCoords(primary->pos) + Vector2D(dx, dy);
	primary->pos = FromScriptCoords(script_position);
	EnsureFrameSegment(primary);
	UpdateDrag(primary);
	Commit(_("Nudge subtitle position by 1 pixel"));
	commit_id = -1;
	parent->Render();
	return true;
}

void VisualToolDrag::OnDoubleClick() {
	Vector2D d = ToScriptCoords(mouse_pos) - (primary ? ToScriptCoords(primary->pos) : GetLinePosition(active_line));

	for (auto line : c->selectionController->GetSelectedSet()) {
		Vector2D p1, p2;
		int t1, t2;
		if (GetLineMove(line, p1, p2, t1, t2)) {
			if (t1 > 0 || t2 > 0)
				SetOverride(line, "\\move", agi::format("(%s,%s,%d,%d)", (p1 + d).Str(), (p2 + d).Str(), t1, t2));
			else
				SetOverride(line, "\\move", agi::format("(%s,%s)", (p1 + d).Str(), (p2 + d).Str()));
		}
		else
			SetOverride(line, "\\pos", (GetLinePosition(line) + d).PStr());

		if (Vector2D org = GetLineOrigin(line))
			SetOverride(line, "\\org", (org + d).PStr());
	}

	Commit(_("positioning"));

	OnFileChanged();
}

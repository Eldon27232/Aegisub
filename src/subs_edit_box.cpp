// Copyright (c) 2005, Rodrigo Braz Monteiro
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subs_edit_box.cpp
/// @brief Main subtitle editing area, including toolbars around the text control
/// @ingroup main_ui

#include "subs_edit_box.h"

#include "ass_block_editor_model.h"
#include "ass_override_ast.h"
#include "ass_style.h"
#include "colour_picker_model.h"
#include "dialogs.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "base_grid.h"
#include "command/command.h"
#include "compat.h"
#include "dialog_style_editor.h"
#include "dialog_ass_templates.h"
#include "flyweight_hash.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "initial_line_state.h"
#include "options.h"
#include "theme.h"
#include "placeholder_ctrl.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_ctrl.h"
#include "text_selection_controller.h"
#include "timeedit_ctrl.h"
#include "utils.h"
#include "validators.h"

#include <libaegisub/character_count.h>
#include <libaegisub/util.h>

#include <functional>
#include <optional>
#include <unordered_set>

#include <wx/bmpbuttn.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/wrapsizer.h>
#include <wx/weakref.h>
#include <cmath>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/dialog.h>
#include <wx/fontenum.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/radiobut.h>
#include <wx/srchctrl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/textdlg.h>

namespace {

/// Work around wxGTK's fondness for generating events from ChangeValue
void change_value(wxTextCtrl *ctrl, wxString const& value) {
	if (value != ctrl->GetValue())
		ctrl->ChangeValue(value);
}

wxString new_value([[maybe_unused]] wxComboBox *ctrl, [[maybe_unused]] wxCommandEvent &evt) {
#ifdef __WXGTK__
	return ctrl->GetValue();
#else
	return evt.GetString();
#endif
}

void time_edit_char_hook(wxKeyEvent &event) {
	// Force a modified event on Enter
	if (event.GetKeyCode() == WXK_RETURN) {
		TimeEdit *edit = static_cast<TimeEdit*>(event.GetEventObject());
		edit->SetValue(edit->GetValue());
	}
	else
		event.Skip();
}

class BlockFeatureDialog final : public wxDialog {
	wxSearchCtrl *search;
	wxListCtrl *list;
	std::vector<ass::blocks::Feature> filtered;

	void Refresh() {
		filtered = ass::blocks::Model::SearchFeatures(from_wx(search->GetValue()));
		list->Freeze();
		list->DeleteAllItems();
		for (size_t i = 0; i < filtered.size(); ++i) {
			auto const& feature = filtered[i];
			long row = list->InsertItem(static_cast<long>(i), to_wx(feature.category));
			list->SetItem(row, 1, to_wx(feature.name));
			list->SetItem(row, 2, "\\" + to_wx(feature.tag));
		}
		if (!filtered.empty()) list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		for (int column = 0; column < 3; ++column) list->SetColumnWidth(column, wxLIST_AUTOSIZE_USEHEADER);
		list->Thaw();
	}

public:
	explicit BlockFeatureDialog(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, _("Create ASS block"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		search = new wxSearchCtrl(this, wxID_ANY);
		search->SetDescriptiveText(_("Search localized feature name or ASS tag, for example: anchor, an, frz"));
		search->ShowCancelButton(true);
		sizer->Add(search, wxSizerFlags().Expand().Border(wxALL));

		list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(520, 360)),
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Category"));
		list->InsertColumn(1, _("Feature"));
		list->InsertColumn(2, _("ASS tag"));
		sizer->Add(list, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));
		sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL));
		SetSizerAndFit(sizer);
		SetMinSize(FromDIP(wxSize(520, 360)));

		search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Refresh(); });
		list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { EndModal(wxID_OK); });
		Refresh();
		search->SetFocus();
	}

	std::optional<ass::blocks::Feature> Selected() const {
		long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (row < 0 || static_cast<size_t>(row) >= filtered.size()) return std::nullopt;
		return filtered[static_cast<size_t>(row)];
	}
};

// Passing a pointer-to-member directly to a function sometimes does not work
// in VC++ 2015 Update 2, with it instead passing a null pointer
const auto AssDialogue_Actor = &AssDialogue::Actor;
const auto AssDialogue_Effect = &AssDialogue::Effect;
}

SubsEditBox::SubsEditBox(wxWindow *parent, agi::Context *context)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | (theme::IsDark() ? wxBORDER_STATIC : wxRAISED_BORDER), "SubsEditBox")
, c(context)
, block_model(std::make_unique<ass::blocks::Model>())
, undo_timer(GetEventHandler())
{
	using std::bind;

	// Top controls
	top_sizer = new wxBoxSizer(wxHORIZONTAL);

	comment_box = new wxCheckBox(this,-1,_("&Comment"));
	comment_box->SetToolTip(_("Comment this line out. Commented lines don't show up on screen."));
#ifdef __WXGTK__
	// Only supported in wxgtk
	comment_box->SetCanFocus(false);
#endif
	top_sizer->Add(comment_box, 0, wxRIGHT | wxALIGN_CENTER, 5);

	style_box = MakeComboBox("Default", wxCB_READONLY, &SubsEditBox::OnStyleChange, _("Style for this line"));

	style_edit_button = new wxButton(this, -1, _("Edit"), wxDefaultPosition,
		wxSize(GetTextExtent(_("Edit")).GetWidth() + 20, -1));
	style_edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (active_style) {
			wxArrayString font_list = wxFontEnumerator::GetFacenames();
			font_list.Sort();
			DialogStyleEditor(this, active_style, c, nullptr, "", font_list).ShowModal();
		}
	});
	top_sizer->Add(style_edit_button, wxSizerFlags().Center().Border(wxRIGHT));

	actor_box = new Placeholder<wxComboBox>(this, _("Actor"), wxSize(110, -1), wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Actor name for this speech. This is only for reference, and is mainly useless."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	top_sizer->Add(actor_box, wxSizerFlags(2).Center().Border(wxRIGHT));

	effect_box = new Placeholder<wxComboBox>(this, _("Effect"), wxSize(80,-1), wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Effect for this line. This can be used to store extra information for karaoke scripts, or for the effects supported by the renderer."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	top_sizer->Add(effect_box, 3, wxALIGN_CENTER, 5);

	char_count = new wxTextCtrl(this, -1, "0", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_CENTER);
	char_count->SetInitialSize(char_count->GetSizeFromText(wxS("000")));
	char_count->SetToolTip(_("Number of characters in the longest line of this subtitle."));
	top_sizer->Add(char_count, 0, wxALIGN_CENTER, 5);

	// Middle controls
	middle_left_sizer = new wxBoxSizer(wxHORIZONTAL);

	layer = new wxSpinCtrl(this,-1,"",wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,0,999,0);
	layer->SetToolTip(_("Layer number"));
	middle_left_sizer->Add(layer, wxSizerFlags().Center());
	middle_left_sizer->AddSpacer(5);

	start_time = MakeTimeCtrl(_("Start time"), TIME_START);
	end_time   = MakeTimeCtrl(_("End time"), TIME_END);
	middle_left_sizer->AddSpacer(5);
	duration   = MakeTimeCtrl(_("Line duration"), TIME_DURATION);
	middle_left_sizer->AddSpacer(5);

	margin[0] = MakeMarginCtrl(_("Left Margin (0 = default from style)"), 0, _("left margin change"));
	margin[1] = MakeMarginCtrl(_("Right Margin (0 = default from style)"), 1, _("right margin change"));
	margin[2] = MakeMarginCtrl(_("Vertical Margin (0 = default from style)"), 2, _("vertical margin change"));
	middle_left_sizer->AddSpacer(5);

	// Middle-bottom controls
	middle_right_sizer = new wxBoxSizer(wxHORIZONTAL);
	MakeButton("edit/style/bold");
	MakeButton("edit/style/italic");
	MakeButton("edit/style/underline");
	MakeButton("edit/style/strikeout");
	MakeButton("edit/font");
	middle_right_sizer->AddSpacer(5);
	MakeButton("edit/color/primary");
	MakeButton("edit/color/secondary");
	MakeButton("edit/color/outline");
	MakeButton("edit/color/shadow");
	middle_right_sizer->AddSpacer(5);
	MakeButton("grid/line/next/create");
	middle_right_sizer->AddSpacer(10);

	by_time = MakeRadio(_("T&ime"), true, _("Time by h:mm:ss.cs"));
	by_frame = MakeRadio(_("F&rame"), false, _("Time by frame number"));
	by_frame->Enable(false);

	split_box = new wxCheckBox(this,-1,_("Show Original"));
	split_box->SetToolTip(_("Show the contents of the subtitle line when it was first selected above the edit box. This is sometimes useful when editing subtitles or translating subtitles into another language."));
	split_box->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnSplit, this);
	auto *editing_modes = new wxBoxSizer(wxHORIZONTAL);
	editing_modes->Add(split_box, wxSizerFlags().Center());
	auto *frame_segments = new wxCheckBox(this, wxID_ANY, _("Segment by frame"));
	frame_segments->SetName("frame-segments-toggle");
	frame_segments->SetValue(OPT_GET("Tool/Visual/Frame Segments")->GetBool());
	frame_segments->SetToolTip(_("When enabled, dragging and arrow-key nudging creates a held state from the current exact video frame"));
	frame_segments->Bind(wxEVT_CHECKBOX, [frame_segments](wxCommandEvent&) {
		OPT_SET("Tool/Visual/Frame Segments")->SetBool(frame_segments->GetValue());
	});
	editing_modes->Add(frame_segments, wxSizerFlags().Center().Border(wxLEFT, 8));

	// Main sizer
	main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer,0,wxEXPAND | wxALL,3);
	main_sizer->Add(middle_left_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(editing_modes, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);

	// Block editor and optional raw ASS source
	block_panel = new wxPanel(this, wxID_ANY);
	auto *block_sizer = new wxBoxSizer(wxVERTICAL);
	auto *block_header = new wxBoxSizer(wxHORIZONTAL);
	auto *new_block = new wxButton(block_panel, wxID_ANY, _("New"));
	new_block->SetToolTip(_("Search and add ASS features such as positioning, font, color, animation, and clipping"));
	auto *templates = new wxButton(block_panel, wxID_ANY, _("Templates"));
	templates->SetToolTip(_("Save, categorize, or apply global and project subtitle templates"));
	show_ass_source = new wxCheckBox(block_panel, wxID_ANY, _("Show ASS source"));
	show_ass_source->SetToolTip(_("Show and directly edit the complete ASS source below the block editor"));
	block_header->Add(new_block, wxSizerFlags().Border(wxRIGHT));
	block_header->Add(templates, wxSizerFlags().Border(wxRIGHT));
	block_header->AddStretchSpacer();
	block_header->Add(show_ass_source, wxSizerFlags().Center());
	block_sizer->Add(block_header, wxSizerFlags().Expand().Border(wxBOTTOM, 3));

	block_list = new wxScrolledWindow(block_panel, wxID_ANY, wxDefaultPosition,
		FromDIP(wxSize(300, 160)), wxVSCROLL | wxTAB_TRAVERSAL);
	block_list->SetName("ass-block-workspace");
	block_list->SetScrollRate(0, FromDIP(18));
	block_list->SetSizer(new wxBoxSizer(wxVERTICAL));
	block_list->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { LayoutBlocks(); event.Skip(); });
	block_sizer->Add(block_list, wxSizerFlags(1).Expand());
	block_panel->SetSizer(block_sizer);
	main_sizer->Add(block_panel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);

	edit_ctrl = new SubsTextEditCtrl(this, FromDIP(wxSize(300,50)), (theme::IsDark() ? wxBORDER_SIMPLE : wxBORDER_SUNKEN), c);
	edit_ctrl->Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);

	secondary_editor = new wxTextCtrl(this, -1, "", wxDefaultPosition, FromDIP(wxSize(300,50)), (theme::IsDark() ? wxBORDER_SIMPLE : wxBORDER_SUNKEN) | wxTE_MULTILINE | wxTE_READONLY);

	main_sizer->Add(secondary_editor,1,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(edit_ctrl,1,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Hide(secondary_editor);
	main_sizer->Hide(edit_ctrl);

	bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
	bottom_sizer->Add(MakeBottomButton("edit/revert"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear/text"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/insert_original"));
	main_sizer->Add(bottom_sizer);
	main_sizer->Hide(bottom_sizer);

	SetSizerAndFit(main_sizer);

	edit_ctrl->Bind(wxEVT_STC_MODIFIED, &SubsEditBox::OnChange, this);
	edit_ctrl->SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT | wxSTC_STARTACTION);
	new_block->Bind(wxEVT_BUTTON, &SubsEditBox::OnBlockNew, this);
	templates->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ShowAssTemplateManager(this, c); });
	show_ass_source->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnShowAssSource, this);

	block_list->Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnBlockKeyDown, this);

	Bind(wxEVT_TEXT, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_SPINCTRL, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_CHECKBOX, &SubsEditBox::OnCommentChange, this, comment_box->GetId());

	Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);
	Bind(wxEVT_SIZE, &SubsEditBox::OnSize, this);
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { commit_id = -1; });

	wxSizeEvent evt;
	OnSize(evt);

	file_changed_slot = c->ass->AddCommitListener(&SubsEditBox::OnCommit, this);
	connections = agi::signal::make_vector({
		context->project->AddTimecodesListener(&SubsEditBox::UpdateFrameTiming, this),
		context->selectionController->AddActiveLineListener(&SubsEditBox::OnActiveLineChanged, this),
		context->selectionController->AddSelectionListener(&SubsEditBox::OnSelectedSetChanged, this),
		context->initialLineState->AddChangeListener(&SubsEditBox::OnLineInitialTextChanged, this),
		OPT_SUB("Tool/Visual/Frame Segments", [frame_segments](agi::OptionValue const& value) {
			frame_segments->SetValue(value.GetBool());
		}),
	 });

	context->textSelectionController->SetControl(edit_ctrl);
	block_list->SetFocus();

	bool show_original = OPT_GET("Subtitle/Show Original")->GetBool();
	if (show_original) {
		split_box->SetValue(true);
		DoOnSplit(true);
	}
}

SubsEditBox::~SubsEditBox() {
	c->textSelectionController->SetControl(nullptr);
}

wxTextCtrl *SubsEditBox::MakeMarginCtrl(wxString const& tooltip, int margin, wxString const& commit_msg) {
	wxTextCtrl *ctrl = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER, IntValidator(0, true));
	ctrl->SetInitialSize(ctrl->GetSizeFromText(wxS("0000")));
	ctrl->SetMaxLength(5);
	ctrl->SetToolTip(tooltip);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Center());

	Bind(wxEVT_TEXT, [=, this](wxCommandEvent&) {
		int value = agi::util::mid(-9999, atoi(ctrl->GetValue().utf8_str()), 99999);
		SetSelectedRows([&](AssDialogue *d) { d->Margin[margin] = value; },
			commit_msg, AssFile::COMMIT_DIAG_META);
	}, ctrl->GetId());

	return ctrl;
}

TimeEdit *SubsEditBox::MakeTimeCtrl(wxString const& tooltip, TimeField field) {
	TimeEdit *ctrl = new TimeEdit(this, -1, c, "", wxDefaultSize, field == TIME_END);
	ctrl->SetInitialSize(ctrl->GetSizeFromText(wxS(" 0:00:00.000 ")));
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_TEXT, [=, this](wxCommandEvent&) { CommitTimes(field); }, ctrl->GetId());
	ctrl->Bind(wxEVT_CHAR_HOOK, time_edit_char_hook);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Center());
	return ctrl;
}

void SubsEditBox::MakeButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxBitmapButton *btn = new wxBitmapButton(this, -1, command->Icon());
	tool_tip_bindings.emplace_back(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	middle_right_sizer->Add(btn, wxSizerFlags().Expand());
	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
}

wxButton *SubsEditBox::MakeBottomButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxButton *btn = new wxButton(this, -1, command->StrDisplay(c));
	tool_tip_bindings.emplace_back(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
	return btn;
}

wxComboBox *SubsEditBox::MakeComboBox(wxString const& initial_text, int style, void (SubsEditBox::*handler)(wxCommandEvent&), wxString const& tooltip) {
	wxString styles[] = { "Default" };
	wxComboBox *ctrl = new wxComboBox(this, -1, initial_text, wxDefaultPosition, wxSize(110,-1), 1, styles, style | wxTE_PROCESS_ENTER);
	ctrl->SetToolTip(tooltip);
	top_sizer->Add(ctrl, wxSizerFlags(2).Center().Border(wxRIGHT));
	Bind(wxEVT_COMBOBOX, handler, this, ctrl->GetId());
	return ctrl;
}

wxRadioButton *SubsEditBox::MakeRadio(wxString const& text, bool start, wxString const& tooltip) {
	wxRadioButton *ctrl = new wxRadioButton(this, -1, text, wxDefaultPosition, wxDefaultSize, start ? wxRB_GROUP : 0);
	ctrl->SetValue(start);
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_RADIOBUTTON, &SubsEditBox::OnFrameTimeRadio, this, ctrl->GetId());
	middle_right_sizer->Add(ctrl, wxSizerFlags().Expand().Border(wxRIGHT));
	return ctrl;
}

void SubsEditBox::OnCommit(int type) {
	wxEventBlocker blocker(this);

	initial_times.clear();

	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_STYLES) {
		wxString style = style_box->GetValue();
		style_box->Clear();
		style_box->Append(to_wx(c->ass->GetStyles()));
		style_box->Select(style_box->FindString(style));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
	}

	if (type == AssFile::COMMIT_NEW) {
		PopulateList(effect_box, AssDialogue_Effect);
		PopulateList(actor_box, AssDialogue_Actor);
		return;
	}
	else if (type & AssFile::COMMIT_STYLES)
		style_box->Select(style_box->FindString(to_wx(line->Style)));

	if (!(type ^ AssFile::COMMIT_ORDER)) return;

	SetControlsState(!!line);
	UpdateFields(type, true);
}

void SubsEditBox::UpdateFields(int type, bool repopulate_lists) {
	if (!line) return;

	if (type & AssFile::COMMIT_DIAG_TIME) {
		start_time->SetTime(line->Start);
		end_time->SetTime(line->End);
		SetDurationField();
	}

	if (type & AssFile::COMMIT_DIAG_TEXT) {
		edit_ctrl->SetTextTo(line->Text);
		block_model->SetSource(line->Text);
		RefreshBlocks();
		UpdateCharacterCount(line->Text);
	}

	if (type & AssFile::COMMIT_DIAG_META) {
		layer->SetValue(line->Layer);
		for (size_t i = 0; i < margin.size(); ++i)
			change_value(margin[i], std::to_wstring(line->Margin[i]));
		comment_box->SetValue(line->Comment);
		style_box->Select(style_box->FindString(to_wx(line->Style)));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
		style_edit_button->Enable(active_style != nullptr);

		if (repopulate_lists) PopulateList(effect_box, AssDialogue_Effect);
		effect_box->ChangeValue(to_wx(line->Effect));
		effect_box->SetStringSelection(to_wx(line->Effect));

		if (repopulate_lists) PopulateList(actor_box, AssDialogue_Actor);
		actor_box->ChangeValue(to_wx(line->Actor));
		actor_box->SetStringSelection(to_wx(line->Actor));
	}
}

void SubsEditBox::PopulateList(wxComboBox *combo, boost::flyweight<std::string> AssDialogue::*field) {
	wxEventBlocker blocker(this);

	std::unordered_set<boost::flyweight<std::string>> values;
	for (auto const& line : c->ass->Events) {
		auto const& value = line.*field;
		if (!value.get().empty())
			values.insert(value);
	}

	wxArrayString arrstr;
	arrstr.reserve(values.size());
	transform(values.begin(), values.end(), std::back_inserter(arrstr),
		(wxString (*)(std::string const&))to_wx);

	arrstr.Sort();

	combo->Freeze();
	long pos = combo->GetInsertionPoint();
	wxString value = combo->GetValue();

	combo->Set(arrstr);
	combo->ChangeValue(value);
	combo->SetStringSelection(value);
	combo->SetInsertionPoint(pos);
	combo->Thaw();
}

void SubsEditBox::OnActiveLineChanged(AssDialogue *new_line) {
	wxEventBlocker blocker(this);
	line = new_line;
	commit_id = -1;

	UpdateFields(AssFile::COMMIT_DIAG_FULL, false);
}

void SubsEditBox::OnSelectedSetChanged() {
	initial_times.clear();
}

void SubsEditBox::OnLineInitialTextChanged(std::string const& new_text) {
	if (split_box->IsChecked())
		secondary_editor->SetValue(to_wx(new_text));
}

void SubsEditBox::UpdateFrameTiming(agi::vfr::Framerate const& fps) {
	if (fps.IsLoaded()) {
		by_frame->Enable(true);
	}
	else {
		by_frame->Enable(false);
		by_time->SetValue(true);
		start_time->SetByFrame(false);
		end_time->SetByFrame(false);
		duration->SetByFrame(false);
		c->subsGrid->SetByFrame(false);
	}
}

void SubsEditBox::OnKeyDown(wxKeyEvent &event) {
	if (!osx::ime::process_key_event(edit_ctrl, event))
		hotkey::check("Subtitle Edit Box", c, event);
}

std::vector<size_t> SubsEditBox::SelectedBlocks() const {
	return block_selection;
}

void SubsEditBox::LayoutBlocks() {
	if (laying_out_blocks || !block_list->GetSizer()) return;
	laying_out_blocks = true;
	int width = std::max(100, block_list->GetClientSize().x - FromDIP(8));
	for (auto *card : block_cards) {
		for (auto *child : card->GetChildren()) {
			if (auto *text = dynamic_cast<wxTextCtrl*>(child))
				text->SetMinSize(wxSize(std::min(FromDIP(340), width - FromDIP(24)), -1));
		}
		auto *row = static_cast<wxWrapSizer*>(card->GetSizer());
		auto size = row->CalcMinSizeFromKnownDirection(wxHORIZONTAL, width, -1);
		card->SetMinSize(wxSize(0, size.y));
	}
	block_list->Layout();
	block_list->FitInside();
	laying_out_blocks = false;
}

void SubsEditBox::PaintBlockSelection() {
	for (size_t i = 0; i < block_cards.size(); ++i) {
		auto *card = block_cards[i];
		bool selected = std::find(block_selection.begin(), block_selection.end(), i) != block_selection.end();
		auto const& palette = theme::GetPalette();
		card->SetBackgroundColour(selected ? palette.selection : palette.control_background);
		card->SetForegroundColour(selected ? palette.selection_text : palette.text);
		for (auto *child : card->GetChildren()) {
			if (dynamic_cast<wxStaticText*>(child)) {
				child->SetBackgroundColour(card->GetBackgroundColour());
				child->SetForegroundColour(card->GetForegroundColour());
			}
		}
		card->Refresh();
	}
}

void SubsEditBox::SelectBlock(size_t row, bool control, bool shift) {
	if (row >= block_cards.size()) return;
	if (shift) {
		if (!control) block_selection.clear();
		for (size_t i = std::min(row, block_anchor); i <= std::max(row, block_anchor); ++i)
			block_selection.push_back(i);
	}
	else if (control) {
		auto it = std::find(block_selection.begin(), block_selection.end(), row);
		if (it == block_selection.end()) block_selection.push_back(row);
		else block_selection.erase(it);
		block_anchor = row;
	}
	else { block_selection = {row}; block_anchor = row; }
	std::sort(block_selection.begin(), block_selection.end());
	block_selection.erase(std::unique(block_selection.begin(), block_selection.end()), block_selection.end());
	PaintBlockSelection();
}

void SubsEditBox::RefreshBlocks() {
	if (changing_blocks) return;
	auto *focus = wxWindow::FindFocus();
	bool restore_selection_focus = focus == block_list || std::find(block_cards.begin(), block_cards.end(), focus) != block_cards.end();
	++block_generation;
	auto generation = block_generation;
	block_list->Freeze();
	block_cards.clear();
	block_list->GetSizer()->Clear(true);
	auto const& items = block_model->Items();
	block_selection.erase(std::remove_if(block_selection.begin(), block_selection.end(),
		[&](size_t n) { return n >= items.size(); }), block_selection.end());
	for (size_t i = 0; i < items.size(); ++i) {
		auto item = items[i];
		auto *card = new wxPanel(block_list, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
		card->SetName("ass-block-" + to_wx(item.label));
		card->SetBackgroundStyle(wxBG_STYLE_PAINT);
		block_cards.push_back(card);
		card->Bind(wxEVT_PAINT, [this, card, i](wxPaintEvent&) {
			wxAutoBufferedPaintDC dc(card);
			dc.SetBackground(wxBrush(theme::GetPalette().window_background)); dc.Clear();
			bool selected = std::find(block_selection.begin(), block_selection.end(), i) != block_selection.end();
			dc.SetBrush(wxBrush(card->GetBackgroundColour()));
			dc.SetPen(wxPen(selected ? theme::GetPalette().accent : theme::GetPalette().border, selected ? 2 : 1));
			dc.DrawRoundedRectangle(card->GetClientRect().Deflate(1), FromDIP(7));
		});
		auto select = [this, i, card](wxMouseEvent& e) {
			SelectBlock(i, e.CmdDown(), e.ShiftDown()); card->SetFocusIgnoringChildren();
		};
		card->Bind(wxEVT_LEFT_DOWN, select);
		card->Bind(wxEVT_CONTEXT_MENU, [this,i](wxContextMenuEvent&) { OnBlockContext(i); });
		auto *row = new wxWrapSizer(wxHORIZONTAL);
		auto *title = new wxStaticText(card, wxID_ANY, to_wx(item.label));
		title->SetFont(title->GetFont().Bold()); title->Bind(wxEVT_LEFT_DOWN, select);
		row->Add(title, wxSizerFlags().CenterVertical().Border(wxALL, 8));
		auto save = [this, i, generation](std::string const& source, bool refresh = false) {
			if (generation != block_generation || source.empty()) return;
			auto previous = block_model->Items();
			if (!block_model->Replace(i, source)) return;
			ApplyBlockChange(_("Edit ASS block"), false);
			auto const& current = block_model->Items();
			bool same_structure = previous.size() == current.size() && std::equal(previous.begin(), previous.end(), current.begin(),
				[](auto const& left, auto const& right) { return left.kind == right.kind && left.label == right.label; });
			if (!same_structure || refresh) {
				// Raw/text input can insert several tags. Invalidate stale card indices
				// immediately, then rebuild after the current input event has returned.
				auto next_generation = ++block_generation;
				CallAfter([this,next_generation] { if (next_generation == block_generation) RefreshBlocks(); });
			}
		};
		auto field = [card, row](wxString const& label, wxWindow *control) {
			auto *pair = new wxBoxSizer(wxHORIZONTAL);
			if (!label.empty()) pair->Add(new wxStaticText(card, wxID_ANY, label), wxSizerFlags().CenterVertical().Border(wxRIGHT, 4));
			pair->Add(control, wxSizerFlags().CenterVertical());
			row->Add(pair, wxSizerFlags().CenterVertical().Border(wxALL, 5));
		};
		if (item.kind == ass::blocks::ItemKind::Tag) {
			auto state = std::make_shared<ass::ast::OverrideBlock>(ass::ast::OverrideBlock::Parse(item.source, false));
			std::function<void(ass::ast::OverrideNode*, std::vector<size_t>)> controls;
			controls = [&, state, save](ass::ast::OverrideNode *node, std::vector<size_t> path) {
				if (!node) return;
				auto name = node->Name();
				if (!path.empty()) {
					ass::blocks::Model label; label.SetSource("{" + node->Serialize() + "}");
					row->Add(new wxStaticText(card, wxID_ANY, label.Items().empty() ? _("Raw ASS") : to_wx(label.Items()[0].label)), wxSizerFlags().CenterVertical().Border(wxALL, 6));
				}
				auto set = [state, node, save](size_t arg, std::string value) { node->SetArgument(arg, std::move(value)); save(state->Serialize()); };
				auto args = node->Arguments();
				if (node->Kind() == ass::ast::OverrideNodeKind::Raw || !node->IsKnownTag()) {
					auto *raw = new wxTextCtrl(card, wxID_ANY, to_wx(node->Serialize()), wxDefaultPosition, FromDIP(wxSize(260,-1)));
					field(_("Raw ASS"),raw);
					raw->Bind(wxEVT_KILL_FOCUS, [raw,node,state,save](wxFocusEvent& event) {
						event.Skip(); node->SetRaw(from_wx(raw->GetValue())); save(state->Serialize(), true);
					});
					return;
				}
				if (name == "\\an" || name == "\\a") {
					auto *choice = new wxChoice(card, wxID_ANY);
					for (auto label : {_("Top left"),_("Top"),_("Top right"),_("Left"),_("Center"),_("Right"),_("Bottom left"),_("Bottom"),_("Bottom right")}) choice->Append(label);
					long value=2; if(!args.empty()) to_wx(args[0]).ToLong(&value);
					if(name == "\\a") value=AssStyle::SsaToAss(value);
					int map[]={7,8,9,4,5,6,1,2,3}; for(int n=0;n<9;++n) if(map[n]==value) choice->SetSelection(n);
					choice->SetName("block-alignment"); field("",choice);
					choice->Bind(wxEVT_CHOICE,[choice,set,name](wxCommandEvent&) {int map[]={7,8,9,4,5,6,1,2,3}; int v=map[choice->GetSelection()]; set(0,std::to_string(name=="\\a"?AssStyle::AssToSsa(v):v));}); return;
				}
				if (name == "\\c" || name == "\\1c" || name == "\\2c" || name == "\\3c" || name == "\\4c") {
					auto *button = new wxButton(card, wxID_ANY, "", wxDefaultPosition, FromDIP(wxSize(70,28)));
					auto swatch = [button](agi::Color value) {
						wxBitmap bitmap(button->FromDIP(38), button->FromDIP(16));
						wxMemoryDC dc(bitmap); dc.SetBackground(wxBrush(to_wx(value))); dc.Clear(); dc.SelectObject(wxNullBitmap);
						button->SetBitmap(bitmap);
					};
					swatch(block_model->GetColour(i, path));
					button->SetName("block-color"); field("",button);
					button->Bind(wxEVT_BUTTON, [this, button, swatch, path, i, generation](wxCommandEvent&) {
						auto baseline = *block_model;
						wxWeakRef<SubsEditBox> weak(this);
						ShowColourPopup(button, baseline.GetColour(i, path), [weak,swatch,path,i,generation,baseline](agi::Color value) {
							if (!weak || generation != weak->block_generation) return;
							*weak->block_model = baseline;
							if (!weak->block_model->SetColour(i, path, value)) return;
							swatch(value);
							weak->ApplyBlockChange(_("Edit ASS block"), false);
						}, c, true, [weak,generation] {
							if (weak) weak->CallAfter([weak,generation] { if (weak && generation == weak->block_generation) weak->RefreshBlocks(); });
						});
					}); return;
				}
				if ((name=="\\b" || name=="\\i" || name=="\\u" || name=="\\s") && (args.empty() || args[0]=="0" || args[0]=="1" || args[0]=="-1")) {
					auto *check = new wxCheckBox(card,wxID_ANY,_("Enabled")); check->SetValue(!args.empty() && args[0]!="0"); field("",check);
					check->Bind(wxEVT_CHECKBOX,[check,set](wxCommandEvent&) {set(0,check->GetValue()?"1":"0");}); return;
				}
				if (name == "\\q") {
					auto *choice = new wxChoice(card, wxID_ANY);
					for (auto label : {_("Balanced, wider first line"), _("Wrap at line end"), _("No automatic wrapping"), _("Balanced, wider last line")}) choice->Append(label);
					long value=0; if(!args.empty()) to_wx(args[0]).ToLong(&value);
					choice->SetSelection(std::clamp(int(value),0,3)); field("",choice);
					choice->Bind(wxEVT_CHOICE,[choice,set](wxCommandEvent&) {set(0,std::to_string(choice->GetSelection()));}); return;
				}
				bool alpha = name=="\\alpha" || name=="\\1a" || name=="\\2a" || name=="\\3a" || name=="\\4a";
				if(name=="\\t" && node->Transform()) {
					// Expand only the private UI copy; opening the editor never rewrites the source.
					std::string start="0",end=std::to_string(line?int(line->End)-int(line->Start):1000),accel="1";
					if(args.size()==2) accel=args[0];
					if(args.size()>=3) {start=args[0];end=args[1];}
					if(args.size()==4) accel=args[2];
					node->SetArguments({start,end,accel,node->Transform()->Serialize()},true); args=node->Arguments();
				}
				if(name=="\\move" && args.size()==4) { args.push_back("0"); args.push_back(std::to_string(line?int(line->End)-int(line->Start):1000)); }
				size_t count = node->Transform() ? args.size()-1 : std::max<size_t>(1,args.size());
				for(size_t a=0;a<count;++a) {
					wxString label;
					if(name=="\\pos" || name=="\\org") label=a?"Y":"X";
					else if(name=="\\move") {wxString labels[]={_("From X"),_("From Y"),_("To X"),_("To Y"),_("Start (ms)"),_("End (ms)")}; if(a<6) label=labels[a];}
					else if(name=="\\t") {wxString labels[]={_("Start (ms)"),_("End (ms)"),_("Acceleration")}; if(a<3)label=labels[a];}
					else if(name=="\\fad") label=a?_("Fade out (ms)"):_("Fade in (ms)");
					else if(name=="\\fade") { wxString labels[]={_("Start opacity (%)"),_("Hold opacity (%)"),_("End opacity (%)"),_("Fade in start (ms)"),_("Fade in end (ms)"),_("Fade out start (ms)"),_("Fade out end (ms)")}; if(a<7) label=labels[a]; }
					else if(name=="\\fscx" || name=="\\fscy") label="%";
					else if(name=="\\frx" || name=="\\fry" || name=="\\frz" || name=="\\fr") label=_("Degrees");
					else if(name=="\\k" || name=="\\K" || name=="\\kf" || name=="\\ko" || name=="\\kt") label=_("Centiseconds");
					else if(count>1) label=wxString::Format("%s %d",_("Parameter"),int(a+1));
					std::string value=a<args.size()?args[a]:"";
					double number=0; bool numeric=to_wx(value).ToCDouble(&number);
					bool fade_alpha = name=="\\fade" && a<3;
					if (fade_alpha && numeric) number=colour_picker::AssAlphaToOpacity(std::clamp(int(number),0,255));
					if(alpha) { long n=0; wxString hex=to_wx(value); hex.Replace("&H","");hex.Replace("&","");hex.ToLong(&n,16); number=colour_picker::AssAlphaToOpacity(n);numeric=true;label=_("Opacity (%)"); }
					if(numeric && name!="\\fn" && name!="\\r") {
						auto *input=new wxSpinCtrlDouble(card,wxID_ANY,"",wxDefaultPosition,FromDIP(wxSize(94,-1)),wxSP_ARROW_KEYS,(alpha||fade_alpha)?0:-10000000,(alpha||fade_alpha)?100:10000000,number,1);
						input->SetDigits((alpha||fade_alpha)?0:2); input->SetName("block-parameter-"+to_wx(name.substr(1))+"-"+std::to_string(a));field(label,input);
						input->Bind(wxEVT_TEXT,[input,set,a,alpha,fade_alpha,node,args,name](wxCommandEvent&) { double n; if(!input->GetTextValue().ToCDouble(&n)||!std::isfinite(n)) return;
							if(name=="\\move" && node->Arguments().size()==4 && a>=4) node->SetArguments(args,true);
							set(a,alpha?from_wx(wxString::Format("&H%02X&",colour_picker::OpacityToAssAlpha(std::clamp(int(n),0,100)))):fade_alpha?std::to_string(colour_picker::OpacityToAssAlpha(std::clamp(int(n),0,100))):from_wx(wxString::FromCDouble(n,6))); });
					}
					else {
						auto *input=new wxTextCtrl(card,wxID_ANY,to_wx(value),wxDefaultPosition,FromDIP(wxSize(name=="\\fn"?180:240,-1)));field(label,input);
						input->Bind(wxEVT_TEXT,[input,set,a](wxCommandEvent&) {set(a,from_wx(input->GetValue()));});
					}
				}
				if (auto *nested = node->Transform()) for (size_t n=0; n<nested->Nodes().size(); ++n) { auto child_path = path; child_path.push_back(n); controls(nested->MutableNode(n), child_path); }
			};
			for(size_t n=0;n<state->Nodes().size();++n) controls(state->MutableNode(n),{});
		}
		else {
			auto *text = new wxTextCtrl(card, wxID_ANY, to_wx(item.source), wxDefaultPosition, FromDIP(wxSize(340, -1)));
			text->SetName(item.kind==ass::blocks::ItemKind::Raw?"block-raw-ass":"block-text"); field("",text);
			if (item.kind == ass::blocks::ItemKind::Text)
				text->Bind(wxEVT_TEXT,[text,save](wxCommandEvent&) {save(from_wx(text->GetValue()));});
			else text->Bind(wxEVT_KILL_FOCUS,[text,save](wxFocusEvent& event) {event.Skip(); save(from_wx(text->GetValue()), true);});
			text->Bind(wxEVT_KILL_FOCUS,[this,text,i,generation](wxFocusEvent& event) {
				event.Skip();
				if (!text->GetValue().empty()) return;
				CallAfter([this,i,generation] { if(generation==block_generation && block_model->Delete({i})) ApplyBlockChange(_("Delete ASS block")); });
			});
		}
		card->SetSizer(row);
		block_list->GetSizer()->Add(card,wxSizerFlags().Expand().Border(wxALL,4));
	}
	PaintBlockSelection();
	LayoutBlocks(); block_list->Thaw();
	if (restore_selection_focus) {
		if (!block_selection.empty()) block_cards[block_selection.front()]->SetFocusIgnoringChildren();
		else block_list->SetFocusIgnoringChildren();
	}
}

void SubsEditBox::ApplyBlockChange(wxString const& desc, bool rebuild) {
	changing_blocks = true;
	auto source = block_model->Serialize();
	edit_ctrl->SetTextTo(source);
	if (line) { CommitText(desc); UpdateCharacterCount(source); }
	changing_blocks = false;
	if (rebuild) RefreshBlocks();
}

void SubsEditBox::OnBlockNew(wxCommandEvent&) {
	BlockFeatureDialog dialog(this);
	if (dialog.ShowModal() != wxID_OK) return;
	auto feature = dialog.Selected();
	if (!feature) return;
	auto selected = SelectedBlocks();
	std::optional<size_t> after;
	if (!selected.empty()) after = selected.back();
	if (block_model->Insert(after, *feature))
		ApplyBlockChange(_("Add ASS block"));
}

void SubsEditBox::CopyBlocks() {
	auto source = block_model->Copy(SelectedBlocks());
	if (source.empty() || !wxTheClipboard->Open()) return;
	wxTheClipboard->SetData(new wxTextDataObject(to_wx(source)));
	wxTheClipboard->Close();
}

void SubsEditBox::CutBlocks() {
	auto selected = SelectedBlocks();
	auto source = block_model->Copy(selected);
	if (source.empty() || !wxTheClipboard->Open()) return;
	wxTheClipboard->SetData(new wxTextDataObject(to_wx(source)));
	wxTheClipboard->Close();
	if (block_model->Delete(std::move(selected)))
		ApplyBlockChange(_("Cut ASS block"));
}

void SubsEditBox::PasteBlocks() {
	if (!wxTheClipboard->Open()) return;
	wxTextDataObject data;
	bool available = wxTheClipboard->GetData(data);
	wxTheClipboard->Close();
	if (!available) return;
	auto selected = SelectedBlocks();
	std::optional<size_t> after;
	if (!selected.empty()) after = selected.back();
	if (block_model->Paste(after, from_wx(data.GetText())))
		ApplyBlockChange(_("Paste ASS block"));
}

void SubsEditBox::DeleteBlocks() {
	if (block_model->Delete(SelectedBlocks()))
		ApplyBlockChange(_("Delete ASS block"));
}

void SubsEditBox::OnBlockKeyDown(wxKeyEvent& event) {
	// Editing a field keeps normal text shortcuts; block shortcuts act on the card itself.
	auto *focus = wxWindow::FindFocus();
	if (focus && focus != block_list && !dynamic_cast<wxPanel*>(focus)) { event.Skip(); return; }
	if (event.ControlDown()) {
		switch (event.GetKeyCode()) {
			case 'C': CopyBlocks(); return;
			case 'X': CutBlocks(); return;
			case 'V': PasteBlocks(); return;
			default: break;
		}
	}
	if (event.GetKeyCode() == WXK_DELETE) {
		DeleteBlocks();
		return;
	}
	event.Skip();
}

void SubsEditBox::OnBlockContext(size_t row) {
	if (std::find(block_selection.begin(), block_selection.end(), row) == block_selection.end()) SelectBlock(row, false, false);
	wxMenu menu;
	menu.Append(wxID_COPY, _("Copy"));
	menu.Append(wxID_CUT, _("Cut"));
	menu.Append(wxID_PASTE, _("Paste"));
	menu.AppendSeparator();
	menu.Append(wxID_DELETE, _("Delete"));
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { CopyBlocks(); }, wxID_COPY);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { CutBlocks(); }, wxID_CUT);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { PasteBlocks(); }, wxID_PASTE);
	menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) { DeleteBlocks(); }, wxID_DELETE);
	PopupMenu(&menu);
}

void SubsEditBox::OnShowAssSource(wxCommandEvent&) {
	Freeze();
	main_sizer->Show(edit_ctrl, show_ass_source->IsChecked());
	main_sizer->Layout();
	if (auto *parent_sizer = GetParent()->GetSizer()) parent_sizer->Layout();
	Thaw();
}

void SubsEditBox::OnChange(wxStyledTextEvent &event) {
	if (changing_blocks) return;
	if (line && edit_ctrl->GetTextRaw().data() != line->Text.get()) {
		if (event.GetModificationType() & wxSTC_STARTACTION)
			commit_id = -1;
		CommitText(_("modify text"));
		UpdateCharacterCount(line->Text);
		block_model->SetSource(line->Text);
		RefreshBlocks();
	}
}

void SubsEditBox::Commit(wxString const& desc, int type, bool amend, AssDialogue *line) {
	file_changed_slot.Block();
	commit_id = c->ass->Commit(desc, type, (amend && desc == last_commit_type) ? commit_id : -1, line);
	file_changed_slot.Unblock();
	last_commit_type = desc;
	last_time_commit_type = -1;
	initial_times.clear();
	undo_timer.Start(30000, wxTIMER_ONE_SHOT);
}

template<class setter>
void SubsEditBox::SetSelectedRows(setter set, wxString const& desc, int type, bool amend) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for_each(sel.begin(), sel.end(), set);
	Commit(desc, type, amend, sel.size() == 1 ? *sel.begin() : nullptr);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend) {
	SetSelectedRows([&](AssDialogue *d) { d->*field = value; }, desc, type, amend);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend) {
	boost::flyweight<std::string> conv_value(from_wx(value));
	SetSelectedRows([&](AssDialogue *d) { d->*field = conv_value; }, desc, type, amend);
}

void SubsEditBox::CommitText(wxString const& desc) {
	auto data = edit_ctrl->GetTextRaw();
	SetSelectedRows(&AssDialogue::Text, boost::flyweight<std::string>(data.data(), data.length()), desc, AssFile::COMMIT_DIAG_TEXT, true);
}

void SubsEditBox::CommitTimes(TimeField field) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for (AssDialogue *d : sel) {
		if (!initial_times.count(d))
			initial_times[d] = {d->Start, d->End};

		switch (field) {
			case TIME_START:
				initial_times[d].first = d->Start = start_time->GetTime();
				d->End = std::max(d->Start, initial_times[d].second);
				break;

			case TIME_END:
				initial_times[d].second = d->End = end_time->GetTime();
				d->Start = std::min(d->End, initial_times[d].first);
				break;

			case TIME_DURATION:
				if (by_frame->GetValue()) {
					auto const& fps = c->project->Timecodes();
					d->End = fps.TimeAtFrame(fps.FrameAtTime(d->Start, agi::vfr::START) + duration->GetFrame() - 1, agi::vfr::END);
				}
				else
					d->End = d->Start + duration->GetTime();
				initial_times[d].second = d->End;
				break;
		}
	}

	start_time->SetTime(line->Start);
	end_time->SetTime(line->End);

	if (field != TIME_DURATION)
		SetDurationField();

	if (field != last_time_commit_type)
		commit_id = -1;

	last_time_commit_type = field;
	file_changed_slot.Block();
	commit_id = c->ass->Commit(_("modify times"), AssFile::COMMIT_DIAG_TIME, commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
	file_changed_slot.Unblock();
}

void SubsEditBox::SetDurationField() {
	// With VFR, the frame count calculated from the duration in time can be
	// completely wrong (since the duration is calculated as if it were a start
	// time), so we need to explicitly set it with the correct units.
	if (by_frame->GetValue())
		duration->SetFrame(end_time->GetFrame() - start_time->GetFrame() + 1);
	else
		duration->SetTime(end_time->GetTime() - start_time->GetTime());
}

void SubsEditBox::OnSize(wxSizeEvent &evt) {
	int availableWidth = GetVirtualSize().GetWidth();
	int midMin = middle_left_sizer->GetMinSize().GetWidth();
	int botMin = middle_right_sizer->GetMinSize().GetWidth();

	if (button_bar_split) {
		if (availableWidth > midMin + botMin) {
			GetSizer()->Detach(middle_right_sizer);
			middle_left_sizer->Add(middle_right_sizer,0,wxALIGN_CENTER_VERTICAL);
			button_bar_split = false;
		}
	}
	else {
		if (availableWidth < midMin) {
			middle_left_sizer->Detach(middle_right_sizer);
			GetSizer()->Insert(2,middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
			button_bar_split = true;
		}
	}

	evt.Skip();
}

void SubsEditBox::OnFrameTimeRadio(wxCommandEvent &event) {
	event.Skip();

	bool byFrame = by_frame->GetValue();
	start_time->SetByFrame(byFrame);
	end_time->SetByFrame(byFrame);
	duration->SetByFrame(byFrame);
	c->subsGrid->SetByFrame(byFrame);

	SetDurationField();
}

void SubsEditBox::SetControlsState(bool state) {
	if (state == controls_enabled) return;
	controls_enabled = state;

	Enable(state);
	if (!state) {
		wxEventBlocker blocker(this);
		edit_ctrl->SetTextTo("");
		block_model->SetSource("");
		RefreshBlocks();
	}
}

void SubsEditBox::OnSplit(wxCommandEvent&) {
	bool show_original = split_box->IsChecked();
	DoOnSplit(show_original);
	OPT_SET("Subtitle/Show Original")->SetBool(show_original);
}

void SubsEditBox::DoOnSplit(bool show_original) {
	Freeze();
	if (show_original)
		secondary_editor->SetValue(to_wx(c->initialLineState->GetInitialText()));

	GetSizer()->Show(secondary_editor, show_original);
	GetSizer()->Show(bottom_sizer, show_original);
	Fit();
	SetMinSize(GetSize());
	wxSizer* parent_sizer = GetParent()->GetSizer();
	if (parent_sizer) parent_sizer->Layout();
	Thaw();
}

void SubsEditBox::OnStyleChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Style, new_value(style_box, evt), _("style change"), AssFile::COMMIT_DIAG_META);
	active_style = c->ass->GetStyle(line->Style);
}

void SubsEditBox::OnActorChange(wxCommandEvent &evt) {
	bool amend = evt.GetEventType() == wxEVT_TEXT;
	SetSelectedRows(AssDialogue_Actor, new_value(actor_box, evt), _("actor change"), AssFile::COMMIT_DIAG_META, amend);
	PopulateList(actor_box, AssDialogue_Actor);
}

void SubsEditBox::OnLayerEnter(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Layer, evt.GetInt(), _("layer change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::OnEffectChange(wxCommandEvent &evt) {
	bool amend = evt.GetEventType() == wxEVT_TEXT;
	SetSelectedRows(AssDialogue_Effect, new_value(effect_box, evt), _("effect change"), AssFile::COMMIT_DIAG_META, amend);
	PopulateList(effect_box, AssDialogue_Effect);
}

void SubsEditBox::OnCommentChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Comment, !!evt.GetInt(), _("comment change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::CallCommand(const char *cmd_name) {
	cmd::call(cmd_name, c);
	edit_ctrl->SetFocus();
}

void SubsEditBox::UpdateCharacterCount(std::string const& text) {
	int ignore = agi::IGNORE_BLOCKS;
	if (OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool())
		ignore |= agi::IGNORE_WHITESPACE;
	if (OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool())
		ignore |= agi::IGNORE_PUNCTUATION;
	size_t length = agi::MaxLineLength(text, ignore);
	char_count->SetValue(std::to_wstring(length));
	size_t limit = (size_t)OPT_GET("Subtitle/Character Limit")->GetInt();
	if (limit && length > limit)
		char_count->SetBackgroundColour(theme::IsDark() ? theme::GetPalette().error_background : to_wx(OPT_GET("Colour/Subtitle/Syntax/Background/Error")->GetColor()));
	else
		char_count->SetBackgroundColour(theme::IsDark() ? theme::GetPalette().control_background : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
}

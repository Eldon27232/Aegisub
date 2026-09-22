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
	middle_right_sizer->Add(split_box, wxSizerFlags().Center().Left());

	// Main sizer
	main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(top_sizer,0,wxEXPAND | wxALL,3);
	main_sizer->Add(middle_left_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);

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

	block_list = new wxListCtrl(block_panel, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(300, 82)),
		wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
	block_list->InsertColumn(0, _("Feature block"));
	block_list->InsertColumn(1, _("ASS content"));
	block_list->InsertColumn(2, _("Category"));
	block_list->SetToolTip(_("Click to select; Ctrl/Shift for multiple selection; double-click to edit; supports copy, cut, paste, and Delete"));
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
	block_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &SubsEditBox::OnBlockActivate, this);
	block_list->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, &SubsEditBox::OnBlockContext, this);
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
	std::vector<size_t> result;
	long row = -1;
	while ((row = block_list->GetNextItem(row, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
		result.push_back(static_cast<size_t>(row));
	return result;
}

void SubsEditBox::RefreshBlocks() {
	block_list->Freeze();
	block_list->DeleteAllItems();
	auto const& items = block_model->Items();
	for (size_t i = 0; i < items.size(); ++i) {
		auto const& item = items[i];
		long row = block_list->InsertItem(static_cast<long>(i), to_wx(item.label));
		block_list->SetItem(row, 1, to_wx(item.source));
		block_list->SetItem(row, 2, to_wx(item.category));
	}
	block_list->SetColumnWidth(0, FromDIP(150));
	block_list->SetColumnWidth(2, FromDIP(90));
	block_list->SetColumnWidth(1, std::max(FromDIP(180), block_list->GetClientSize().x - FromDIP(250)));
	block_list->Thaw();
}

void SubsEditBox::ApplyBlockChange(wxString const& desc) {
	auto source = block_model->Serialize();
	edit_ctrl->SetTextTo(source);
	if (line) {
		commit_id = -1;
		CommitText(desc);
		UpdateCharacterCount(source);
	}
	RefreshBlocks();
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

void SubsEditBox::OnBlockActivate(wxListEvent& event) {
	long row = event.GetIndex();
	if (row < 0 || static_cast<size_t>(row) >= block_model->Items().size()) return;
	auto const& item = block_model->Items()[static_cast<size_t>(row)];
	wxTextEntryDialog dialog(this, _("Edit the ASS content for this block:"), _("Edit ASS block"),
		to_wx(item.source), wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE);
	dialog.SetSize(FromDIP(wxSize(520, 220)));
	if (dialog.ShowModal() == wxID_OK && block_model->Replace(static_cast<size_t>(row), from_wx(dialog.GetValue())))
		ApplyBlockChange(_("Edit ASS block"));
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

void SubsEditBox::OnBlockContext(wxListEvent& event) {
	if (event.GetIndex() >= 0 && !(block_list->GetItemState(event.GetIndex(), wxLIST_STATE_SELECTED) & wxLIST_STATE_SELECTED))
		block_list->SetItemState(event.GetIndex(), wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
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

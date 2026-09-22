// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "dialog_ass_templates.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_template_store.h"
#include "compat.h"
#include "colour_button.h"
#include "colour_picker_model.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "theme.h"

#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/path.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <optional>
#include <unordered_map>

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>

namespace {
using ass::templates::Entry;
using ass::templates::Scope;

class TemplateDetailsDialog final : public wxDialog {
	wxTextCtrl *name;
	wxTextCtrl *category;
	wxChoice *scope;

public:
	TemplateDetailsDialog(wxWindow *parent, wxString const& title,
		wxString const& initial_name = {}, wxString const& initial_category = _("Uncategorized"))
	: wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		auto *grid = new wxFlexGridSizer(2, FromDIP(8), FromDIP(8));
		grid->AddGrowableCol(1, 1);
		grid->Add(new wxStaticText(this, wxID_ANY, _("Template name")), wxSizerFlags().CenterVertical());
		name = new wxTextCtrl(this, wxID_ANY, initial_name);
		grid->Add(name, wxSizerFlags(1).Expand());
		grid->Add(new wxStaticText(this, wxID_ANY, _("Category")), wxSizerFlags().CenterVertical());
		category = new wxTextCtrl(this, wxID_ANY, initial_category);
		grid->Add(category, wxSizerFlags(1).Expand());
		grid->Add(new wxStaticText(this, wxID_ANY, _("Save scope")), wxSizerFlags().CenterVertical());
		scope = new wxChoice(this, wxID_ANY);
		scope->Append(_("Global template (available to all projects)"));
		scope->Append(_("Project template (saved with the current ASS file)"));
		scope->SetSelection(0);
		grid->Add(scope, wxSizerFlags(1).Expand());
		sizer->Add(grid, wxSizerFlags(1).Expand().Border(wxALL));
		sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL));
		SetSizerAndFit(sizer);
		SetMinSize(FromDIP(wxSize(440, -1)));
		name->SetFocus();
		name->SelectAll();
		theme::Apply(this);
	}

	std::string Name() const { return from_wx(name->GetValue()); }
	std::string Category() const { return from_wx(category->GetValue()); }
	Scope SelectedScope() const { return scope->GetSelection() == 1 ? Scope::Project : Scope::Global; }
};

class TemplateParametersDialog final : public wxDialog {
	std::vector<ass::templates::Parameter> parameters;
	std::vector<wxTextCtrl *> values;

public:
	TemplateParametersDialog(wxWindow *parent, std::string_view source, agi::Context *context)
	: wxDialog(parent, wxID_ANY, _("Edit parameters before applying template"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, parameters(ass::templates::ExtractParameters(source))
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(new wxStaticText(this, wxID_ANY,
		_("You can edit position, color, font size, animation timing, and text. Leave a value unchanged to keep it.")),
		wxSizerFlags().Expand().Border(wxALL));

		auto *scroll = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(620, 360)), wxVSCROLL);
		scroll->SetScrollRate(0, FromDIP(12));
		auto *grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
		grid->AddGrowableCol(1, 1);
		for (auto const& parameter : parameters) {
			grid->Add(new wxStaticText(scroll, wxID_ANY, to_wx(parameter.label)), wxSizerFlags().CenterVertical());
			auto *value = new wxTextCtrl(scroll, wxID_ANY, to_wx(parameter.value));
			values.push_back(value);
			bool color_parameter = false;
			for (auto tag : {"c", "1c", "2c", "3c", "4c"})
				color_parameter |= parameter.id.rfind(std::string("tag:\\") + tag + ":", 0) == 0;
			auto parsed_color = color_parameter ? colour_picker::ParseHex(parameter.value) : std::nullopt;
			if (parsed_color) {
				value->Hide();
				auto color = new ColourButton(scroll, FromDIP(wxSize(80, 18)), false, *parsed_color, wxDefaultValidator, context);
				color->Bind(EVT_COLOR, [value](ValueEvent<agi::Color>& event) {
					value->ChangeValue(to_wx(event.Get().GetAssOverrideFormatted()));
				});
				grid->Add(color, wxSizerFlags().Left());
			}
			else grid->Add(value, wxSizerFlags(1).Expand());
		}
		scroll->SetSizer(grid);
		sizer->Add(scroll, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));
		sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxALL));
		SetSizerAndFit(sizer);
		SetMinSize(FromDIP(wxSize(620, 420)));
		theme::Apply(this);
	}

	std::unordered_map<std::string, std::string> Values() const {
		std::unordered_map<std::string, std::string> result;
		for (size_t i = 0; i < parameters.size(); ++i)
			result.emplace(parameters[i].id, from_wx(values[i]->GetValue()));
		return result;
	}
};

class TemplateManagerDialog final : public wxDialog {
	agi::Context *context;
	agi::fs::path global_path;
	std::vector<Entry> entries;
	wxListCtrl *list;
	wxButton *apply_current;
	wxButton *apply_new;
	wxButton *rename;
	wxButton *change_category;
	wxButton *remove;

	void Load() {
		try {
			auto stream = agi::io::Open(global_path);
			std::string data{std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>()};
			while (!data.empty() && (data.back() == '\r' || data.back() == '\n')) data.pop_back();
			auto global = ass::templates::Decode(data, Scope::Global);
			entries.insert(entries.end(), std::make_move_iterator(global.begin()), std::make_move_iterator(global.end()));
		}
		catch (agi::fs::FileSystemError const&) {
		}

		auto project = ass::templates::Decode(context->ass->Properties.localization_templates, Scope::Project);
		entries.insert(entries.end(), std::make_move_iterator(project.begin()), std::make_move_iterator(project.end()));
	}

	bool SaveGlobal() {
		try {
			auto file = agi::io::Save(global_path);
			file.Get() << ass::templates::Encode(entries, Scope::Global);
			return true;
		}
		catch (agi::fs::FileSystemError const& error) {
			wxMessageBox(to_wx(error.GetMessage()), _("Failed to save global template"), wxOK | wxICON_ERROR, this);
			return false;
		}
	}

	void SaveProject() {
		bool any = std::any_of(entries.begin(), entries.end(), [](Entry const& entry) { return entry.scope == Scope::Project; });
		auto encoded = any ? ass::templates::Encode(entries, Scope::Project) : std::string{};
		if (encoded == context->ass->Properties.localization_templates) return;
		context->ass->Properties.localization_templates = std::move(encoded);
		context->ass->Commit(_("Update project subtitle templates"), AssFile::COMMIT_SCRIPTINFO);
	}

	void SaveScope(Scope scope) {
		if (scope == Scope::Global) SaveGlobal();
		else SaveProject();
	}

	std::optional<size_t> SelectedIndex() const {
		long row = list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (row < 0) return std::nullopt;
		auto index = static_cast<size_t>(list->GetItemData(row));
		return index < entries.size() ? std::optional<size_t>(index) : std::nullopt;
	}

	void UpdateButtons() {
		bool selected = SelectedIndex().has_value();
		apply_current->Enable(selected && context->selectionController->GetActiveLine());
		apply_new->Enable(selected && context->selectionController->GetActiveLine());
		rename->Enable(selected);
		change_category->Enable(selected);
		remove->Enable(selected);
	}

	void Refresh(std::string const& select_id = {}) {
		list->Freeze();
		list->DeleteAllItems();
		long select_row = -1;
		for (size_t i = 0; i < entries.size(); ++i) {
			auto const& entry = entries[i];
			long row = list->InsertItem(static_cast<long>(i), entry.scope == Scope::Global ? _("Global") : _("Project"));
			list->SetItem(row, 1, to_wx(entry.category));
			list->SetItem(row, 2, to_wx(entry.name));
			auto preview = to_wx(entry.text);
			if (preview.size() > 90) preview = preview.Left(90) + "…";
			list->SetItem(row, 3, preview);
			list->SetItemData(row, static_cast<long>(i));
			if (!select_id.empty() && entry.id == select_id) select_row = row;
		}
		if (select_row >= 0) {
			list->SetItemState(select_row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			list->EnsureVisible(select_row);
		}
		list->SetColumnWidth(0, FromDIP(70));
		list->SetColumnWidth(1, FromDIP(120));
		list->SetColumnWidth(2, FromDIP(160));
		list->SetColumnWidth(3, FromDIP(350));
		list->Thaw();
		UpdateButtons();
	}

	std::string NewId() const {
		auto value = std::chrono::steady_clock::now().time_since_epoch().count();
		std::string id = "template-" + std::to_string(value);
		while (std::any_of(entries.begin(), entries.end(), [&](Entry const& entry) { return entry.id == id; })) id += '-';
		return id;
	}

	std::optional<std::string> PreparedText(Entry const& entry) {
		auto parameters = ass::templates::ExtractParameters(entry.text);
		if (parameters.empty()) return entry.text;
		TemplateParametersDialog dialog(this, entry.text, context);
		if (dialog.ShowModal() != wxID_OK) return std::nullopt;
		return ass::templates::ApplyParameters(entry.text, dialog.Values());
	}

	void SaveCurrent(wxCommandEvent&) {
		auto *active = context->selectionController->GetActiveLine();
		if (!active) return;
		TemplateDetailsDialog dialog(this, _("Save current line as template"));
		if (dialog.ShowModal() != wxID_OK || dialog.Name().empty()) return;
		Entry entry{NewId(), dialog.Name(), dialog.Category(), active->Text.get(), dialog.SelectedScope()};
		entries.push_back(entry);
		SaveScope(entry.scope);
		Refresh(entry.id);
	}

	void Rename(wxCommandEvent&) {
		auto index = SelectedIndex();
		if (!index) return;
		wxTextEntryDialog dialog(this, _("Enter the new template name:"), _("Rename template"), to_wx(entries[*index].name));
		if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
		entries[*index].name = from_wx(dialog.GetValue());
		SaveScope(entries[*index].scope);
		Refresh(entries[*index].id);
	}

	void ChangeCategory(wxCommandEvent&) {
		auto index = SelectedIndex();
		if (!index) return;
		wxTextEntryDialog dialog(this, _("Enter the template category:"), _("Change template category"), to_wx(entries[*index].category));
		if (dialog.ShowModal() != wxID_OK) return;
		entries[*index].category = from_wx(dialog.GetValue());
		SaveScope(entries[*index].scope);
		Refresh(entries[*index].id);
	}

	void Remove(wxCommandEvent&) {
		auto index = SelectedIndex();
		if (!index) return;
		if (wxMessageBox(_("Are you sure you want to delete this template?"), _("Delete template"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) return;
		auto scope = entries[*index].scope;
		entries.erase(entries.begin() + *index);
		SaveScope(scope);
		Refresh();
	}

	void ApplyCurrent(wxCommandEvent&) {
		auto index = SelectedIndex();
		auto *active = context->selectionController->GetActiveLine();
		if (!index || !active) return;
		auto text = PreparedText(entries[*index]);
		if (!text) return;
		active->Text = *text;
		context->ass->Commit(_("Apply subtitle template"), AssFile::COMMIT_DIAG_TEXT, -1, active);
		EndModal(wxID_OK);
	}

	void ApplyNew(wxCommandEvent&) {
		auto index = SelectedIndex();
		auto *active = context->selectionController->GetActiveLine();
		if (!index || !active) return;
		auto text = PreparedText(entries[*index]);
		if (!text) return;

		auto *created = new AssDialogue;
		created->Layer = active->Layer;
		created->Style = active->Style;
		created->Start = active->End;
		created->End = created->Start + OPT_GET("Timing/Default Duration")->GetInt();
		created->Text = *text;
		context->ass->Events.insert(++context->ass->iterator_to(*active), *created);
		context->ass->Commit(_("Create subtitle and apply template"), AssFile::COMMIT_DIAG_ADDREM);
		context->selectionController->SetSelectionAndActive({created}, created);
		EndModal(wxID_OK);
	}

public:
	TemplateManagerDialog(wxWindow *parent, agi::Context *context)
	: wxDialog(parent, wxID_ANY, _("Subtitle templates"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, context(context)
	, global_path(config::path->Decode("?user/localization_templates.dat"))
	{
		Load();
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(720, 350)),
			wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES);
		list->InsertColumn(0, _("Scope"));
		list->InsertColumn(1, _("Category"));
		list->InsertColumn(2, _("Template name"));
		list->InsertColumn(3, _("Content preview"));
		sizer->Add(list, wxSizerFlags(1).Expand().Border(wxALL));

		auto *manage = new wxBoxSizer(wxHORIZONTAL);
		auto *save = new wxButton(this, wxID_ANY, _("Save current line"));
		rename = new wxButton(this, wxID_ANY, _("Rename"));
		change_category = new wxButton(this, wxID_ANY, _("Change category"));
		remove = new wxButton(this, wxID_ANY, _("Delete"));
		manage->Add(save, wxSizerFlags().Border(wxRIGHT));
		manage->Add(rename, wxSizerFlags().Border(wxRIGHT));
		manage->Add(change_category, wxSizerFlags().Border(wxRIGHT));
		manage->Add(remove);
		manage->AddStretchSpacer();
		apply_current = new wxButton(this, wxID_ANY, _("Apply to current line"));
		apply_new = new wxButton(this, wxID_ANY, _("Create subtitle and apply"));
		manage->Add(apply_current, wxSizerFlags().Border(wxRIGHT));
		manage->Add(apply_new);
		sizer->Add(manage, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM));
		sizer->Add(CreateSeparatedButtonSizer(wxCLOSE), wxSizerFlags().Expand().Border(wxALL));
		SetSizerAndFit(sizer);
		SetMinSize(FromDIP(wxSize(720, 430)));

		save->Bind(wxEVT_BUTTON, &TemplateManagerDialog::SaveCurrent, this);
		rename->Bind(wxEVT_BUTTON, &TemplateManagerDialog::Rename, this);
		change_category->Bind(wxEVT_BUTTON, &TemplateManagerDialog::ChangeCategory, this);
		remove->Bind(wxEVT_BUTTON, &TemplateManagerDialog::Remove, this);
		apply_current->Bind(wxEVT_BUTTON, &TemplateManagerDialog::ApplyCurrent, this);
		apply_new->Bind(wxEVT_BUTTON, &TemplateManagerDialog::ApplyNew, this);
		list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { UpdateButtons(); });
		list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { UpdateButtons(); });
		list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
			wxCommandEvent event;
			ApplyCurrent(event);
		});
		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
		Refresh();
		theme::Apply(this);
	}
};
}

void ShowAssTemplateManager(wxWindow *parent, agi::Context *context) {
	TemplateManagerDialog(parent, context).ShowModal();
}

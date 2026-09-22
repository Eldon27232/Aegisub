// Copyright (c) 2026, Aegisub Localization Edition contributors
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

#include "theme.h"

#include "options.h"

#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dataview.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/treectrl.h>
#include <wx/window.h>

#include <optional>

namespace theme {
namespace {

std::optional<ThemeId> active_theme;

bool UsesControlBackground(wxWindow *window) {
	return window->IsKindOf(wxCLASSINFO(wxTextCtrl))
		|| window->IsKindOf(wxCLASSINFO(wxComboBox))
		|| window->IsKindOf(wxCLASSINFO(wxChoice))
		|| window->IsKindOf(wxCLASSINFO(wxListBox))
		|| window->IsKindOf(wxCLASSINFO(wxListCtrl))
		|| window->IsKindOf(wxCLASSINFO(wxTreeCtrl))
		|| window->IsKindOf(wxCLASSINFO(wxDataViewCtrl))
		|| window->IsKindOf(wxCLASSINFO(wxSpinCtrl))
		|| window->IsKindOf(wxCLASSINFO(wxSpinCtrlDouble));
}

bool UsesWindowBackground(wxWindow *window) {
	return window->IsKindOf(wxCLASSINFO(wxTopLevelWindow))
		|| window->IsKindOf(wxCLASSINFO(wxPanel))
		|| window->IsKindOf(wxCLASSINFO(wxStaticBox))
		|| window->IsKindOf(wxCLASSINFO(wxStaticText));
}

ThemeId ReadConfiguredTheme() {
	if (!config::opt)
		return ThemeId::Light;

	try {
		return ParseTheme(OPT_GET("App/Theme")->GetString());
	}
	catch (...) {
		try {
			return OPT_GET("App/Dark Mode")->GetBool() ? ThemeId::DarkPlus : ThemeId::Light;
		}
		catch (...) {
			return ThemeId::Light;
		}
	}
}

void ApplyImpl(wxWindow *window, Palette const& palette) {
	if (UsesControlBackground(window)) {
		window->SetBackgroundColour(palette.control_background);
		window->SetForegroundColour(palette.text);
	}
	else if (UsesWindowBackground(window)) {
		window->SetBackgroundColour(palette.window_background);
		window->SetForegroundColour(palette.text);
	}

	for (wxWindow *child : window->GetChildren())
		ApplyImpl(child, palette);

	window->Refresh();
}

} // namespace

void Initialize() {
	active_theme = ReadConfiguredTheme();
}

ThemeId CurrentTheme() {
	return active_theme.value_or(ReadConfiguredTheme());
}

bool IsDark() {
	return IsDark(CurrentTheme());
}

Palette const& GetPalette() {
	return GetPalette(CurrentTheme());
}

void Apply(wxWindow *window) {
	if (!window)
		return;

	ApplyImpl(window, GetPalette());
}

} // namespace theme

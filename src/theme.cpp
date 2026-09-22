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

#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dataview.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/pen.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>
#include <wx/treectrl.h>
#include <wx/window.h>

#include <optional>
#include <unordered_set>
#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
#include <wx/msw/darkmode.h>
#endif

namespace theme {
namespace {

std::optional<ThemeId> active_theme;
std::unordered_set<wxWindow*> accent_bound;

wxColour Tint(wxColour base, wxColour accent, unsigned amount) {
	return wxColour((base.Red() * (100 - amount) + accent.Red() * amount) / 100,
		(base.Green() * (100 - amount) + accent.Green() * amount) / 100,
		(base.Blue() * (100 - amount) + accent.Blue() * amount) / 100);
}

void AccentControl(wxWindow *window, bool hover = false, bool pressed = false) {
	// A swatch displays document colour, not the interface accent.
	if (window->GetName() == "block-color") return;
	auto const& p = GetPalette();
	bool checked = false;
	if (auto check = dynamic_cast<wxCheckBox*>(window)) checked = check->GetValue();
	if (auto radio = dynamic_cast<wxRadioButton*>(window)) checked = radio->GetValue();
	bool input = dynamic_cast<wxTextCtrl*>(window) || dynamic_cast<wxSpinCtrl*>(window)
		|| dynamic_cast<wxSpinCtrlDouble*>(window) || dynamic_cast<wxChoice*>(window);
	bool focus = window->HasFocus();
	window->SetBackgroundColour(pressed || checked ? p.selection :
		hover || focus ? Tint(input ? p.control_background : p.window_background, p.accent, 25) :
		input ? p.control_background : p.window_background);
	window->SetForegroundColour(pressed || checked ? p.selection_text : p.text);
	window->Refresh();
}

void BindAccent(wxWindow *window) {
	if (!dynamic_cast<wxButton*>(window) && !dynamic_cast<wxCheckBox*>(window)
		&& !dynamic_cast<wxRadioButton*>(window) && !dynamic_cast<wxTextCtrl*>(window)
		&& !dynamic_cast<wxSpinCtrl*>(window) && !dynamic_cast<wxSpinCtrlDouble*>(window)
		&& !dynamic_cast<wxChoice*>(window)) return;
	AccentControl(window);
	if (!accent_bound.insert(window).second) return;
	window->Bind(wxEVT_ENTER_WINDOW, [window](wxMouseEvent& event) { AccentControl(window, true); event.Skip(); });
	window->Bind(wxEVT_LEAVE_WINDOW, [window](wxMouseEvent& event) { AccentControl(window); event.Skip(); });
	window->Bind(wxEVT_LEFT_DOWN, [window](wxMouseEvent& event) { AccentControl(window, true, true); event.Skip(); });
	window->Bind(wxEVT_LEFT_UP, [window](wxMouseEvent& event) { AccentControl(window, true); event.Skip(); });
	window->Bind(wxEVT_SET_FOCUS, [window](wxFocusEvent& event) { AccentControl(window, true); event.Skip(); });
	window->Bind(wxEVT_KILL_FOCUS, [window](wxFocusEvent& event) { AccentControl(window); event.Skip(); });
	window->Bind(wxEVT_CHECKBOX, [window](wxCommandEvent& event) { AccentControl(window); event.Skip(); });
	window->Bind(wxEVT_RADIOBUTTON, [window](wxCommandEvent& event) { AccentControl(window); event.Skip(); });
	window->Bind(wxEVT_DESTROY, [window](wxWindowDestroyEvent& event) { accent_bound.erase(window); event.Skip(); });
}

#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
class NativePalette final : public wxDarkModeSettings {
	wxColour GetColour(wxSystemColour index) override {
		auto const& p = GetPalette();
		switch (index) {
			case wxSYS_COLOUR_HIGHLIGHT: return p.selection;
			case wxSYS_COLOUR_HIGHLIGHTTEXT: return p.selection_text;
			case wxSYS_COLOUR_HOTLIGHT: return p.accent;
			case wxSYS_COLOUR_WINDOW: return p.control_background;
			case wxSYS_COLOUR_WINDOWTEXT: case wxSYS_COLOUR_BTNTEXT: return p.text;
			case wxSYS_COLOUR_BTNFACE: case wxSYS_COLOUR_MENU: return p.window_background;
			case wxSYS_COLOUR_GRAYTEXT: return p.muted_text;
			default: return wxDarkModeSettings::GetColour(index);
		}
	}
	wxColour GetMenuColour(wxMenuColour index) override {
		auto const& p = GetPalette();
		switch (index) {
			case wxMenuColour::StandardFg: return p.text;
			case wxMenuColour::StandardBg: return p.window_background;
			case wxMenuColour::DisabledFg: return p.muted_text;
			case wxMenuColour::HotBg: return p.selection;
		}
		return p.window_background;
	}
	wxPen GetBorderPen() override { return wxPen(GetPalette().border); }
};
#endif

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

	BindAccent(window);
	for (wxWindow *child : window->GetChildren())
		ApplyImpl(child, palette);

	window->Refresh();
}

} // namespace

void Initialize() {
	active_theme = ReadConfiguredTheme();
}

void InitializeNative() {
#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
	if (IsDark()) wxTheApp->MSWEnableDarkMode(wxApp::DarkMode_Always, new NativePalette);
#endif
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

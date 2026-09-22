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

#pragma once

#include <string_view>

#include <wx/colour.h>

namespace theme {

enum class ThemeId {
	DarkPlus,
	DeepGray,
	OledBlack,
	Light
};

/// Colours shared by native controls and Aegisub's custom-drawn surfaces.
/// Keeping these in one value object avoids each control inventing a slightly
/// different version of the selected theme.
struct Palette {
	wxColour window_background;
	wxColour control_background;
	wxColour text;
	wxColour muted_text;
	wxColour border;
	wxColour accent;
	wxColour selection;
	wxColour selection_text;
	wxColour error_background;

	wxColour grid_background;
	wxColour grid_header;
	wxColour grid_lines;
	wxColour grid_comment;
	wxColour grid_in_frame;
	wxColour grid_selected_comment;
	wxColour grid_fold_open;
	wxColour grid_fold_closed;
	wxColour grid_left_column;
	wxColour grid_text;
	wxColour grid_selected_text;
	wxColour grid_collision_text;
	wxColour grid_active_border;

	wxColour audio_background;
	wxColour audio_waveform;
	wxColour audio_waveform_inactive;
	wxColour audio_selection;
	wxColour audio_primary;
	wxColour audio_cursor;
	wxColour audio_keyframe;

	wxColour slider_selection;
	wxColour slider_border;
};

/// Parse a user-facing theme name. Names are case-insensitive and separators
/// are ignored. Unknown names deliberately fall back to the native Light theme.
ThemeId ParseTheme(std::string_view name);

/// Canonical name written to preferences.
std::string_view ThemeName(ThemeId id);

bool IsDark(ThemeId id);
Palette const& GetPalette(ThemeId id);

} // namespace theme

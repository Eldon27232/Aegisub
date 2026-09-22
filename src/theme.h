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

#include "theme_palette.h"

class wxWindow;

namespace theme {

/// Freeze the configured theme for this process. Theme changes in Preferences
/// are deliberately applied on the next launch, together with native controls.
void Initialize();

/// Keep native menus, selection, and focus colours in the same palette.
void InitializeNative();

/// Theme selected in App/Theme. This remains safe during early startup and
/// falls back to the native Light palette before preferences are available.
ThemeId CurrentTheme();
bool IsDark();
Palette const& GetPalette();

/// Apply the basic background and foreground colours to a complete native
/// window tree. Custom-drawn controls consume GetPalette() directly.
void Apply(wxWindow *window);

} // namespace theme

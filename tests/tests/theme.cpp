// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "theme_palette.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

double LinearChannel(unsigned char value) {
	double channel = value / 255.0;
	return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double Luminance(wxColour const& colour) {
	return 0.2126 * LinearChannel(colour.Red())
		+ 0.7152 * LinearChannel(colour.Green())
		+ 0.0722 * LinearChannel(colour.Blue());
}

double Contrast(wxColour const& first, wxColour const& second) {
	double first_luminance = Luminance(first);
	double second_luminance = Luminance(second);
	return (std::max(first_luminance, second_luminance) + 0.05)
		/ (std::min(first_luminance, second_luminance) + 0.05);
}

} // namespace

TEST(theme_palette, parses_all_canonical_names) {
	EXPECT_EQ(theme::ThemeId::DarkPlus, theme::ParseTheme("Dark+"));
	EXPECT_EQ(theme::ThemeId::DeepGray, theme::ParseTheme("Deep Gray"));
	EXPECT_EQ(theme::ThemeId::OledBlack, theme::ParseTheme("OLED Black"));
	EXPECT_EQ(theme::ThemeId::Light, theme::ParseTheme("Light"));

	EXPECT_EQ("Dark+", theme::ThemeName(theme::ThemeId::DarkPlus));
	EXPECT_EQ("Deep Gray", theme::ThemeName(theme::ThemeId::DeepGray));
	EXPECT_EQ("OLED Black", theme::ThemeName(theme::ThemeId::OledBlack));
	EXPECT_EQ("Light", theme::ThemeName(theme::ThemeId::Light));
}

TEST(theme_palette, parsing_is_forgiving_and_unknown_names_are_light) {
	EXPECT_EQ(theme::ThemeId::DarkPlus, theme::ParseTheme("  DARK PLUS  "));
	EXPECT_EQ(theme::ThemeId::DeepGray, theme::ParseTheme("deep-grey"));
	EXPECT_EQ(theme::ThemeId::OledBlack, theme::ParseTheme("oled_black"));
	EXPECT_EQ(theme::ThemeId::Light, theme::ParseTheme("not-a-theme"));
}

TEST(theme_palette, oled_uses_a_true_black_canvas) {
	auto const& palette = theme::GetPalette(theme::ThemeId::OledBlack);
	EXPECT_EQ(wxColour(0, 0, 0), palette.window_background);
	EXPECT_EQ(wxColour(0, 0, 0), palette.grid_background);
	EXPECT_EQ(wxColour(0, 0, 0), palette.audio_background);
}

TEST(theme_palette, identifies_light_and_dark_themes) {
	EXPECT_TRUE(theme::IsDark(theme::ThemeId::DarkPlus));
	EXPECT_TRUE(theme::IsDark(theme::ThemeId::DeepGray));
	EXPECT_TRUE(theme::IsDark(theme::ThemeId::OledBlack));
	EXPECT_FALSE(theme::IsDark(theme::ThemeId::Light));
}

TEST(theme_palette, text_has_readable_contrast_in_every_theme) {
	for (auto id : {theme::ThemeId::DarkPlus, theme::ThemeId::DeepGray,
	                theme::ThemeId::OledBlack, theme::ThemeId::Light}) {
		auto const& palette = theme::GetPalette(id);
		EXPECT_GE(Contrast(palette.text, palette.window_background), 4.5)
			<< theme::ThemeName(id);
		EXPECT_GE(Contrast(palette.text, palette.control_background), 4.5)
			<< theme::ThemeName(id);
		EXPECT_GE(Contrast(palette.selection_text, palette.selection), 4.5)
			<< theme::ThemeName(id);
	}
}

TEST(theme_palette, themes_have_distinct_accent_families) {
	auto const& blue = theme::GetPalette(theme::ThemeId::DarkPlus);
	auto const& grey = theme::GetPalette(theme::ThemeId::DeepGray);
	auto const& peach = theme::GetPalette(theme::ThemeId::OledBlack);
	auto const& jade = theme::GetPalette(theme::ThemeId::Light);
	EXPECT_GT(blue.accent.Blue(), blue.accent.Red() + 70);
	EXPECT_LT(std::abs(int(grey.accent.Red()) - int(grey.accent.Blue())), 15);
	EXPECT_GT(peach.accent.Red(), peach.accent.Blue() + 70);
	EXPECT_GT(jade.accent.Green(), jade.accent.Red() + 70);
	for (auto const* p : {&grey, &peach, &jade}) {
		EXPECT_NE(blue.selection, p->selection);
		EXPECT_EQ(p->accent, p->grid_active_border);
	}
}

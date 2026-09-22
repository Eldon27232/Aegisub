// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#pragma once

#include <libaegisub/color.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace colour_picker {

/// The ASS alpha channel is transparency (0 = opaque), while the UI exposes
/// opacity (100 = opaque). These helpers are the single conversion point.
int AssAlphaToOpacity(unsigned char alpha);
unsigned char OpacityToAssAlpha(int opacity);

/// Parse the two user-facing hexadecimal forms accepted by the picker.
/// Invalid/incomplete text is rejected rather than silently becoming black.
std::optional<agi::Color> ParseHex(std::string_view text);
std::string ToAssBgr(agi::Color color);
std::string ToRgbHex(agi::Color color);

struct ImagePlacement {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

/// Fit an image into a preview while preserving its aspect ratio.
ImagePlacement FitImage(int preview_width, int preview_height, int image_width, int image_height);

/// Convert a preview-space click into an original-image pixel. Letterbox
/// clicks return no value.
std::optional<std::pair<int, int>> MapPreviewPoint(
	int preview_width, int preview_height,
	int image_width, int image_height,
	int x, int y);

/// Rank project colour occurrences by frequency. Ties keep the order of first
/// appearance, duplicate RGB values are collapsed, and alpha does not split a
/// colour into multiple swatches (the picker preserves the current alpha).
std::vector<agi::Color> RankColours(std::vector<agi::Color> const& occurrences, std::size_t limit);

} // namespace colour_picker

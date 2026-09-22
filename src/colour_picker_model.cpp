// Copyright (c) 2026
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#include "colour_picker_model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace colour_picker {

int AssAlphaToOpacity(unsigned char alpha) {
	return ((255 - static_cast<int>(alpha)) * 100 + 127) / 255;
}

unsigned char OpacityToAssAlpha(int opacity) {
	opacity = std::clamp(opacity, 0, 100);
	return static_cast<unsigned char>(((100 - opacity) * 255 + 50) / 100);
}

namespace {

std::optional<unsigned int> parse_hex(std::string_view text) {
	unsigned int value = 0;
	auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
	if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
	return value;
}

} // namespace

std::optional<agi::Color> ParseHex(std::string_view text) {
	if (text.size() == 7 && text.front() == '#') {
		auto value = parse_hex(text.substr(1));
		if (!value) return std::nullopt;
		return agi::Color(
			static_cast<unsigned char>((*value >> 16) & 0xFF),
			static_cast<unsigned char>((*value >> 8) & 0xFF),
			static_cast<unsigned char>(*value & 0xFF));
	}

	if (text.size() >= 2 && text.front() == '&' && text[1] == 'H')
		text.remove_prefix(2);
	else if (text.size() >= 2 && text.front() == '&' && text[1] == 'h')
		text.remove_prefix(2);
	else
		return std::nullopt;

	if (!text.empty() && text.back() == '&') text.remove_suffix(1);
	if (text.size() != 6) return std::nullopt;

	auto value = parse_hex(text);
	if (!value) return std::nullopt;
	return agi::Color(
		static_cast<unsigned char>(*value & 0xFF),
		static_cast<unsigned char>((*value >> 8) & 0xFF),
		static_cast<unsigned char>((*value >> 16) & 0xFF));
}

std::string ToAssBgr(agi::Color color) {
	return color.GetAssOverrideFormatted();
}

std::string ToRgbHex(agi::Color color) {
	return color.GetHexFormatted();
}

ImagePlacement FitImage(int preview_width, int preview_height, int image_width, int image_height) {
	if (preview_width <= 0 || preview_height <= 0 || image_width <= 0 || image_height <= 0)
		return {};

	double scale = std::min(
		static_cast<double>(preview_width) / image_width,
		static_cast<double>(preview_height) / image_height);
	int width = std::max(1, static_cast<int>(std::lround(image_width * scale)));
	int height = std::max(1, static_cast<int>(std::lround(image_height * scale)));
	width = std::min(width, preview_width);
	height = std::min(height, preview_height);
	return {(preview_width - width) / 2, (preview_height - height) / 2, width, height};
}

std::optional<std::pair<int, int>> MapPreviewPoint(
	int preview_width, int preview_height,
	int image_width, int image_height,
	int x, int y)
{
	auto placement = FitImage(preview_width, preview_height, image_width, image_height);
	if (!placement.width || !placement.height ||
		x < placement.x || y < placement.y ||
		x >= placement.x + placement.width || y >= placement.y + placement.height)
		return std::nullopt;

	int image_x = placement.width > 1
		? (x - placement.x) * (image_width - 1) / (placement.width - 1)
		: 0;
	int image_y = placement.height > 1
		? (y - placement.y) * (image_height - 1) / (placement.height - 1)
		: 0;
	return std::pair{
		std::clamp(image_x, 0, image_width - 1),
		std::clamp(image_y, 0, image_height - 1)};
}

std::vector<agi::Color> RankColours(std::vector<agi::Color> const& occurrences, std::size_t limit) {
	struct Entry {
		agi::Color colour;
		std::size_t count = 0;
	};

	std::vector<Entry> entries;
	std::unordered_map<std::uint32_t, std::size_t> indices;
	entries.reserve(occurrences.size());

	for (std::size_t i = 0; i < occurrences.size(); ++i) {
		auto const& colour = occurrences[i];
		std::uint32_t key = (static_cast<std::uint32_t>(colour.r) << 16) |
			(static_cast<std::uint32_t>(colour.g) << 8) | colour.b;
		auto [it, inserted] = indices.emplace(key, entries.size());
		if (inserted)
			entries.push_back({agi::Color(colour.r, colour.g, colour.b), 1});
		else
			++entries[it->second].count;
	}

	std::stable_sort(entries.begin(), entries.end(), [](Entry const& left, Entry const& right) {
		return left.count > right.count;
	});

	std::vector<agi::Color> result;
	result.reserve(std::min(limit, entries.size()));
	for (auto const& entry : entries) {
		if (result.size() == limit) break;
		result.push_back(entry.colour);
	}
	return result;
}

} // namespace colour_picker

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "subtitle_mask_model.h"

#include "ass_override_ast.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ass::subtitle_mask {
namespace {

std::string number(double value) {
	if (std::abs(value - std::round(value)) < 0.001)
		return std::to_string(static_cast<long long>(std::llround(value)));
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::fixed << std::setprecision(2) << value;
	auto result = stream.str();
	while (result.size() > 1 && result.back() == '0') result.pop_back();
	if (!result.empty() && result.back() == '.') result.pop_back();
	return result;
}

std::string vector_drawing(std::vector<Point> const& points) {
	if (points.size() < 3) return {};
	std::string result = "m " + number(points.front().x) + " " + number(points.front().y) + " l";
	for (size_t i = 1; i < points.size(); ++i)
		result += " " + number(points[i].x) + " " + number(points[i].y);
	return result;
}

std::vector<Point> parse_vector(std::string const& source) {
	std::istringstream stream(source);
	stream.imbue(std::locale::classic());
	std::vector<Point> result;
	std::string token;
	while (stream >> token) {
		if (token == "m" || token == "n" || token == "l" || token == "b" || token == "s" || token == "p" || token == "c")
			continue;
		double x;
		try { x = std::stod(token); }
		catch (...) { continue; }
		double y;
		if (!(stream >> y)) break;
		result.push_back({x, y});
	}
	return result;
}

void erase_direct_clips(ast::OverrideBlock& block, bool include_normal_clip) {
	for (size_t i = block.Nodes().size(); i-- > 0;) {
		auto const& node = block.Nodes()[i];
		if (node.Kind() != ast::OverrideNodeKind::Tag) continue;
		if (node.Name() == "\\iclip" || (include_normal_clip && node.Name() == "\\clip"))
			block.EraseNode(i);
	}
}

} // namespace

StateChangePlan PlanStateChange(int line_start_frame, int line_end_frame, int current_frame) {
	StateChangePlan result;
	if (line_end_frame < line_start_frame || current_frame < line_start_frame || current_frame > line_end_frame)
		return result;
	result.valid = true;
	result.active_start = current_frame;
	result.active_end = line_end_frame;
	if (current_frame > line_start_frame) {
		result.split = true;
		result.before_start = line_start_frame;
		result.before_end = current_frame - 1;
	}
	return result;
}

std::string SetInverseClip(std::string_view source, std::vector<Point> const& points, bool rectangle) {
	if (points.size() < 3) return std::string(source);
	std::string value;
	if (rectangle) {
		double min_x = points.front().x, max_x = points.front().x;
		double min_y = points.front().y, max_y = points.front().y;
		for (auto const& point : points) {
			min_x = std::min(min_x, point.x); max_x = std::max(max_x, point.x);
			min_y = std::min(min_y, point.y); max_y = std::max(max_y, point.y);
		}
		value = "\\iclip(" + number(min_x) + "," + number(min_y) + "," + number(max_x) + "," + number(max_y) + ")";
	}
	else {
		value = "\\iclip(" + vector_drawing(points) + ")";
	}

	auto document = ast::Document::Parse(source);
	ast::OverrideBlock *first_block = nullptr;
	bool begins_with_override = !document.Segments().empty() && document.Segments().front().Kind() == ast::SegmentKind::Override;
	for (size_t i = 0; i < document.Segments().size(); ++i) {
		auto *segment = document.MutableSegment(i);
		if (!segment || segment->Kind() != ast::SegmentKind::Override || !segment->Block()) continue;
		if (!first_block) first_block = segment->Block();
		erase_direct_clips(*segment->Block(), true);
	}
	if (begins_with_override && first_block) {
		first_block->AppendTag(value);
		return document.Serialize();
	}
	return "{" + value + "}" + document.Serialize();
}

std::string RemoveInverseClip(std::string_view source) {
	auto document = ast::Document::Parse(source);
	for (size_t i = 0; i < document.Segments().size(); ++i) {
		auto *segment = document.MutableSegment(i);
		if (segment && segment->Kind() == ast::SegmentKind::Override && segment->Block())
			erase_direct_clips(*segment->Block(), false);
	}
	return document.Serialize();
}

std::vector<Point> ParseInverseClip(std::string_view source) {
	auto document = ast::Document::Parse(source);
	auto clips = document.FindTags("\\iclip", false);
	if (clips.empty()) return {};
	auto const& arguments = clips.front()->Arguments();
	if (arguments.size() == 4) {
		try {
			double x1 = std::stod(arguments[0]);
			double y1 = std::stod(arguments[1]);
			double x2 = std::stod(arguments[2]);
			double y2 = std::stod(arguments[3]);
			return {{x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}};
		}
		catch (...) { return {}; }
	}
	if (arguments.empty()) return {};
	return parse_vector(arguments.back());
}

std::vector<Point> Transform(std::vector<Point> const& points, double scale, double degrees) {
	if (points.empty()) return {};
	double min_x = points.front().x, max_x = points.front().x;
	double min_y = points.front().y, max_y = points.front().y;
	for (auto const& point : points) {
		min_x = std::min(min_x, point.x); max_x = std::max(max_x, point.x);
		min_y = std::min(min_y, point.y); max_y = std::max(max_y, point.y);
	}
	double cx = (min_x + max_x) / 2.0;
	double cy = (min_y + max_y) / 2.0;
	double radians = degrees * 3.14159265358979323846 / 180.0;
	double cosine = std::cos(radians);
	double sine = std::sin(radians);
	std::vector<Point> result;
	result.reserve(points.size());
	for (auto const& point : points) {
		double x = (point.x - cx) * scale;
		double y = (point.y - cy) * scale;
		result.push_back({cx + x * cosine - y * sine, cy + x * sine + y * cosine});
	}
	return result;
}

} // namespace ass::subtitle_mask

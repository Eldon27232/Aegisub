// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "screen_mask_model.h"

#include "ass_override_ast.h"

#include <libaegisub/format.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace ass::screen_mask {
namespace {

int clamp_frame(int frame, int maximum) {
	return std::clamp(frame, 0, std::max(0, maximum));
}

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

Geometry normalize(std::vector<Point> const& points) {
	Geometry result;
	if (points.empty()) return result;
	double min_x = points.front().x;
	double max_x = points.front().x;
	double min_y = points.front().y;
	double max_y = points.front().y;
	for (auto const& point : points) {
		min_x = std::min(min_x, point.x);
		max_x = std::max(max_x, point.x);
		min_y = std::min(min_y, point.y);
		max_y = std::max(max_y, point.y);
	}
	result.position = {(min_x + max_x) / 2.0, (min_y + max_y) / 2.0};
	for (auto const& point : points)
		result.points.push_back({point.x - result.position.x, point.y - result.position.y});
	return result;
}

std::string drawing(std::vector<Point> const& relative_points) {
	if (relative_points.size() < 3) return {};
	std::string result = "m " + number(relative_points.front().x) + " " + number(relative_points.front().y) + " l";
	for (size_t i = 1; i < relative_points.size(); ++i)
		result += " " + number(relative_points[i].x) + " " + number(relative_points[i].y);
	return result;
}

double parse_number(std::string const& source) {
	try { return std::stod(source); }
	catch (...) { return 0.0; }
}

std::vector<Point> parse_drawing(std::string const& source) {
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

} // namespace

FrameRange ResolveFrameRange(FrameRangeRequest request) {
	FrameRange result;
	result.start = clamp_frame(request.current_frame, request.maximum_frame);
	result.end = result.start;
	switch (request.mode) {
		case DurationMode::CurrentFrame:
			break;
		case DurationMode::FrameCount:
			result.end = clamp_frame(result.start + std::max(1, request.frame_count) - 1, request.maximum_frame);
			break;
		case DurationMode::UntilFrame:
			result.end = std::max(result.start, clamp_frame(request.end_frame, request.maximum_frame));
			break;
		case DurationMode::UntilSubtitleEnd:
			result.end = std::max(result.start, clamp_frame(request.subtitle_end_frame, request.maximum_frame));
			break;
		case DurationMode::CustomRange:
			result.start = clamp_frame(std::min(request.custom_start_frame, request.custom_end_frame), request.maximum_frame);
			result.end = clamp_frame(std::max(request.custom_start_frame, request.custom_end_frame), request.maximum_frame);
			break;
	}
	return result;
}

std::string BuildText(std::vector<Point> const& absolute_points, agi::Color colour, int opacity) {
	auto geometry = normalize(absolute_points);
	int alpha = static_cast<int>(std::lround((100 - std::clamp(opacity, 0, 100)) * 255.0 / 100.0));
	return agi::format("{\\an5\\pos(%s,%s)\\p1\\1c%s\\1a&H%02X&\\bord0\\shad0}%s{\\p0}",
		number(geometry.position.x), number(geometry.position.y), colour.GetAssOverrideFormatted(), alpha,
		drawing(geometry.points));
}

Geometry ParseGeometry(std::string_view source) {
	auto document = ast::Document::Parse(source);
	Geometry result;
	auto positions = document.FindTags("\\pos", false);
	if (!positions.empty() && positions.front()->Arguments().size() >= 2) {
		result.position.x = parse_number(positions.front()->Arguments()[0]);
		result.position.y = parse_number(positions.front()->Arguments()[1]);
	}
	for (auto const& segment : document.Segments()) {
		if (segment.Kind() != ast::SegmentKind::Drawing) continue;
		result.points = parse_drawing(segment.Text());
		for (auto& point : result.points) {
			point.x += result.position.x;
			point.y += result.position.y;
		}
		break;
	}
	return result;
}

std::string UpdateGeometry(std::string_view source, std::vector<Point> const& absolute_points) {
	auto geometry = normalize(absolute_points);
	auto document = ast::Document::Parse(source);
	auto positions = document.FindTags("\\pos", false);
	if (!positions.empty())
		positions.front()->SetArguments({number(geometry.position.x), number(geometry.position.y)}, true);
	for (size_t i = 0; i < document.Segments().size(); ++i) {
		auto *segment = document.MutableSegment(i);
		if (segment && segment->Kind() == ast::SegmentKind::Drawing) {
			segment->SetText(drawing(geometry.points));
			break;
		}
	}
	return document.Serialize();
}

std::string UpdateAppearance(std::string_view source, agi::Color colour, int opacity) {
	auto document = ast::Document::Parse(source);
	auto colours = document.FindTags("\\1c", false);
	if (!colours.empty()) colours.front()->SetArgument(0, colour.GetAssOverrideFormatted());
	int alpha = static_cast<int>(std::lround((100 - std::clamp(opacity, 0, 100)) * 255.0 / 100.0));
	auto alphas = document.FindTags("\\1a", false);
	if (!alphas.empty()) alphas.front()->SetArgument(0, agi::format("&H%02X&", alpha));
	return document.Serialize();
}

} // namespace ass::screen_mask

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_template_store.h"

#include "ass_override_ast.h"

#include <libaegisub/ass/string_codec.h>

#include <algorithm>
#include <iterator>
#include <map>

namespace ass::templates {
namespace {

std::string tag_label(std::string_view name) {
	static const std::map<std::string_view, std::string_view> labels = {
		{"\\an", "原点/对齐"}, {"\\pos", "位置"}, {"\\move", "移动"}, {"\\org", "旋转原点"},
		{"\\fn", "字体"}, {"\\fs", "字号"}, {"\\fsp", "字距"},
		{"\\fscx", "X 缩放"}, {"\\fscy", "Y 缩放"}, {"\\frx", "X 轴旋转"},
		{"\\fry", "Y 轴旋转"}, {"\\frz", "Z 轴旋转"}, {"\\fax", "X 轴倾斜"}, {"\\fay", "Y 轴倾斜"},
		{"\\c", "正文颜色"}, {"\\1c", "正文颜色"}, {"\\2c", "次要颜色"},
		{"\\3c", "描边颜色"}, {"\\4c", "阴影颜色"}, {"\\alpha", "全局透明度"},
		{"\\1a", "正文透明度"}, {"\\2a", "次要透明度"}, {"\\3a", "描边透明度"}, {"\\4a", "阴影透明度"},
		{"\\bord", "描边"}, {"\\xbord", "X 描边"}, {"\\ybord", "Y 描边"},
		{"\\shad", "阴影"}, {"\\xshad", "X 阴影"}, {"\\yshad", "Y 阴影"},
		{"\\blur", "模糊"}, {"\\be", "边缘模糊"}, {"\\t", "动画"},
		{"\\fad", "淡入淡出"}, {"\\fade", "高级淡入淡出"},
		{"\\clip", "裁剪"}, {"\\iclip", "反向裁剪"}, {"\\p", "Drawing 模式"}, {"\\pbo", "Drawing 基线"},
		{"\\k", "Karaoke"}, {"\\K", "Karaoke"}, {"\\kf", "Karaoke 填充"}, {"\\ko", "Karaoke 描边"}, {"\\kt", "Karaoke 时间"}
	};
	auto it = labels.find(name);
	return it == labels.end() ? std::string(name) : std::string(it->second);
}

std::string argument_label(std::string_view name, size_t argument, size_t count) {
	auto base = tag_label(name);
	if (name == "\\pos" || name == "\\org") return base + (argument == 0 ? " X" : " Y");
	if (name == "\\move") {
		static constexpr std::string_view labels[] = {"起点 X", "起点 Y", "终点 X", "终点 Y", "开始时间", "结束时间"};
		if (argument < std::size(labels)) return base + " " + std::string(labels[argument]);
	}
	if (name == "\\fad") return base + (argument == 0 ? " 淡入" : " 淡出");
	if (name == "\\t") return base + " 时间/加速 " + std::to_string(argument + 1);
	if (count == 1) return base;
	return base + " 参数 " + std::to_string(argument + 1);
}

void extract_block(ast::OverrideBlock const& block, std::vector<Parameter>& output,
	std::map<std::string, size_t>& occurrences) {
	for (auto const& node : block.Nodes()) {
		if (node.Kind() != ast::OverrideNodeKind::Tag || !node.IsKnownTag()) continue;
		auto const& name = node.Name();
		size_t occurrence = occurrences[name]++;
		auto const& arguments = node.Arguments();
		size_t count = arguments.size() - (node.Transform() && !arguments.empty() ? 1 : 0);
		for (size_t i = 0; i < count; ++i) {
			output.push_back({
				"tag:" + name + ":" + std::to_string(occurrence) + ":" + std::to_string(i),
				argument_label(name, i, count), arguments[i]
			});
		}
		if (node.Transform()) extract_block(*node.Transform(), output, occurrences);
	}
}

void apply_block(ast::OverrideBlock& block,
	std::unordered_map<std::string, std::string> const& values,
	std::map<std::string, size_t>& occurrences) {
	for (size_t node_index = 0; node_index < block.Nodes().size(); ++node_index) {
		auto *node = block.MutableNode(node_index);
		if (!node || node->Kind() != ast::OverrideNodeKind::Tag || !node->IsKnownTag()) continue;
		auto const name = node->Name();
		size_t occurrence = occurrences[name]++;
		size_t count = node->Arguments().size() - (node->Transform() && !node->Arguments().empty() ? 1 : 0);
		for (size_t i = 0; i < count; ++i) {
			auto id = "tag:" + name + ":" + std::to_string(occurrence) + ":" + std::to_string(i);
			auto value = values.find(id);
			if (value != values.end() && value->second != node->Arguments()[i])
				node->SetArgument(i, value->second);
		}
		if (node->Transform()) apply_block(*node->Transform(), values, occurrences);
	}
}

} // namespace

std::string Encode(std::vector<Entry> const& entries, Scope scope) {
	std::string result = "v1";
	for (auto const& entry : entries) {
		if (entry.scope != scope) continue;
		result += '|';
		result += agi::ass::inline_string_encode(entry.id);
		result += ':';
		result += agi::ass::inline_string_encode(entry.name);
		result += ':';
		result += agi::ass::inline_string_encode(entry.category);
		result += ':';
		result += agi::ass::inline_string_encode(entry.text);
	}
	return result;
}

std::vector<Entry> Decode(std::string_view source, Scope scope) {
	std::vector<Entry> result;
	if (source != "v1" && !source.starts_with("v1|")) return result;
	size_t begin = source.find('|');
	while (begin != std::string_view::npos) {
		++begin;
		size_t end = source.find('|', begin);
		auto record = source.substr(begin, end == std::string_view::npos ? source.size() - begin : end - begin);
		std::string_view fields[4];
		size_t field_begin = 0;
		bool valid = true;
		for (size_t i = 0; i < 3; ++i) {
			size_t separator = record.find(':', field_begin);
			if (separator == std::string_view::npos) { valid = false; break; }
			fields[i] = record.substr(field_begin, separator - field_begin);
			field_begin = separator + 1;
		}
		if (valid) {
			fields[3] = record.substr(field_begin);
			result.push_back({
				agi::ass::inline_string_decode(fields[0]),
				agi::ass::inline_string_decode(fields[1]),
				agi::ass::inline_string_decode(fields[2]),
				agi::ass::inline_string_decode(fields[3]), scope
			});
		}
		begin = end;
	}
	return result;
}

std::vector<Parameter> ExtractParameters(std::string_view source) {
	auto document = ast::Document::Parse(source);
	std::vector<Parameter> result;
	std::map<std::string, size_t> occurrences;
	size_t text_index = 0;
	for (auto const& segment : document.Segments()) {
		if (segment.Kind() == ast::SegmentKind::Override && segment.Block())
			extract_block(*segment.Block(), result, occurrences);
		else if (segment.Kind() == ast::SegmentKind::Text && !segment.Text().empty()) {
			result.push_back({"text:" + std::to_string(text_index), "正文 " + std::to_string(text_index + 1), segment.Text()});
			++text_index;
		}
	}
	return result;
}

std::string ApplyParameters(std::string_view source,
	std::unordered_map<std::string, std::string> const& values) {
	auto document = ast::Document::Parse(source);
	std::map<std::string, size_t> occurrences;
	size_t text_index = 0;
	for (size_t segment_index = 0; segment_index < document.Segments().size(); ++segment_index) {
		auto *segment = document.MutableSegment(segment_index);
		if (!segment) continue;
		if (segment->Kind() == ast::SegmentKind::Override && segment->Block())
			apply_block(*segment->Block(), values, occurrences);
		else if (segment->Kind() == ast::SegmentKind::Text && !segment->Text().empty()) {
			auto value = values.find("text:" + std::to_string(text_index));
			if (value != values.end() && value->second != segment->Text()) segment->SetText(value->second);
			++text_index;
		}
	}
	return document.Serialize();
}

} // namespace ass::templates

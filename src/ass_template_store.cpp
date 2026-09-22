// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_template_store.h"

#include "ass_override_ast.h"
#include "format.h"

#include <libaegisub/ass/string_codec.h>

#include <algorithm>
#include <iterator>
#include <map>

namespace ass::templates {
namespace {

std::string translated(wxString const& value) {
	return std::string(wxGetTranslation(value).utf8_str());
}

std::string tag_label(std::string_view name) {
	static const std::map<std::string_view, wxString> labels = {
		{"\\an", wxTRANSLATE("Anchor/Alignment")}, {"\\pos", wxTRANSLATE("Position")}, {"\\move", wxTRANSLATE("Move")}, {"\\org", wxTRANSLATE("Rotation origin")},
		{"\\fn", wxTRANSLATE("Font")}, {"\\fs", wxTRANSLATE("Font size")}, {"\\fsp", wxTRANSLATE("Character spacing")},
		{"\\fscx", wxTRANSLATE("X scale")}, {"\\fscy", wxTRANSLATE("Y scale")}, {"\\frx", wxTRANSLATE("X rotation")},
		{"\\fry", wxTRANSLATE("Y rotation")}, {"\\frz", wxTRANSLATE("Z rotation")}, {"\\fax", wxTRANSLATE("X shear")}, {"\\fay", wxTRANSLATE("Y shear")},
		{"\\c", wxTRANSLATE("Primary color")}, {"\\1c", wxTRANSLATE("Primary color")}, {"\\2c", wxTRANSLATE("Secondary color")},
		{"\\3c", wxTRANSLATE("Outline color")}, {"\\4c", wxTRANSLATE("Shadow color")}, {"\\alpha", wxTRANSLATE("Global opacity")},
		{"\\1a", wxTRANSLATE("Primary opacity")}, {"\\2a", wxTRANSLATE("Secondary opacity")}, {"\\3a", wxTRANSLATE("Outline opacity")}, {"\\4a", wxTRANSLATE("Shadow opacity")},
		{"\\bord", wxTRANSLATE("Outline")}, {"\\xbord", wxTRANSLATE("X outline")}, {"\\ybord", wxTRANSLATE("Y outline")},
		{"\\shad", wxTRANSLATE("Shadow")}, {"\\xshad", wxTRANSLATE("X shadow")}, {"\\yshad", wxTRANSLATE("Y shadow")},
		{"\\blur", wxTRANSLATE("Blur")}, {"\\be", wxTRANSLATE("Edge blur")}, {"\\t", wxTRANSLATE("Animation")},
		{"\\fad", wxTRANSLATE("Fade")}, {"\\fade", wxTRANSLATE("Advanced fade")},
		{"\\clip", wxTRANSLATE("Clip")}, {"\\iclip", wxTRANSLATE("Inverse clip")}, {"\\p", wxTRANSLATE("Drawing mode")}, {"\\pbo", wxTRANSLATE("Drawing baseline")},
		{"\\k", wxTRANSLATE("Karaoke")}, {"\\K", wxTRANSLATE("Karaoke")}, {"\\kf", wxTRANSLATE("Karaoke fill")}, {"\\ko", wxTRANSLATE("Karaoke outline")}, {"\\kt", wxTRANSLATE("Karaoke timing")}
	};
	auto it = labels.find(name);
	return it == labels.end() ? std::string(name) : translated(it->second);
}

std::string argument_label(std::string_view name, size_t argument, size_t count) {
	auto base = tag_label(name);
	if (name == "\\pos" || name == "\\org") return base + (argument == 0 ? " X" : " Y");
	if (name == "\\move") {
		static const std::string labels[] = {
			translated(_("Start X")), translated(_("Start Y")), translated(_("End X")),
			translated(_("End Y")), translated(_("Start time")), translated(_("End time"))
		};
		if (argument < std::size(labels)) return base + " " + labels[argument];
	}
	if (name == "\\fad") return base + " " + translated(argument == 0 ? _("Fade in") : _("Fade out"));
	if (name == "\\t") return base + " " + translated(_("Time/acceleration")) + " " + std::to_string(argument + 1);
	if (count == 1) return base;
	return base + " " + translated(_("Parameter")) + " " + std::to_string(argument + 1);
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

size_t leading_line_breaks(std::string_view text) {
	size_t length = 0;
	while (length + 1 < text.size() && text[length] == '\\' &&
		(text[length + 1] == 'n' || text[length + 1] == 'N'))
		length += 2;
	return length;
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

std::string MakeStructure(std::string_view source) {
	if (source.find(BodyPlaceholder) != std::string_view::npos)
		return std::string(source);

	auto document = ast::Document::Parse(source);
	std::string result;
	bool has_body = false;
	for (auto const& segment : document.Segments()) {
		if (segment.Kind() != ast::SegmentKind::Text) {
			result += segment.Serialize();
			continue;
		}
		if (has_body) continue;

		auto const& text = segment.Text();
		size_t prefix = leading_line_breaks(text);
		result.append(text, 0, prefix);
		if (prefix < text.size()) {
			result += BodyPlaceholder;
			has_body = true;
		}
	}
	if (!has_body) result += BodyPlaceholder;
	return result;
}

std::string ExtractBody(std::string_view source) {
	auto document = ast::Document::Parse(source);
	std::string result;
	bool has_body = false;
	for (auto const& segment : document.Segments()) {
		if (segment.Kind() != ast::SegmentKind::Text) continue;

		auto const& text = segment.Text();
		size_t prefix = has_body ? 0 : leading_line_breaks(text);
		if (prefix == text.size()) continue;
		result.append(text, prefix, std::string::npos);
		has_body = true;
	}
	return result;
}

std::string ApplyStructure(std::string_view structure, std::string_view body) {
	auto reusable = MakeStructure(structure);
	size_t placeholder = reusable.find(BodyPlaceholder);
	if (placeholder == std::string::npos) return reusable;

	std::string result;
	result.reserve(reusable.size() - BodyPlaceholder.size() + body.size());
	result.append(reusable, 0, placeholder);
	result += body;
	result.append(reusable, placeholder + BodyPlaceholder.size(), std::string::npos);
	return result;
}

std::vector<Parameter> ExtractParameters(std::string_view source) {
	auto document = ast::Document::Parse(source);
	std::vector<Parameter> result;
	std::map<std::string, size_t> occurrences;
	bool has_body_placeholder = source.find(BodyPlaceholder) != std::string_view::npos;
	size_t text_index = 0;
	for (auto const& segment : document.Segments()) {
		if (segment.Kind() == ast::SegmentKind::Override && segment.Block())
			extract_block(*segment.Block(), result, occurrences);
		else if (segment.Kind() == ast::SegmentKind::Text && !segment.Text().empty()) {
			if (!has_body_placeholder)
				result.push_back({"text:" + std::to_string(text_index), translated(_("Text")) + " " + std::to_string(text_index + 1), segment.Text()});
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

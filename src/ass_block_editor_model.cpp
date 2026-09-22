// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_block_editor_model.h"

#include "ass_override_ast.h"
#include "format.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>

namespace ass::blocks {
namespace {

std::string translated(wxString const& value) {
	return std::string(wxGetTranslation(value).utf8_str());
}

std::string lower_ascii(std::string_view value) {
	std::string result(value);
	for (char& c : result) {
		unsigned char byte = static_cast<unsigned char>(c);
		if (byte < 0x80) c = static_cast<char>(std::tolower(byte));
	}
	return result;
}

std::string label_for_tag(std::string_view name) {
	static const std::unordered_map<std::string_view, wxString> labels = {
		{"\\an", wxTRANSLATE("Anchor/Alignment")}, {"\\pos", wxTRANSLATE("Position")}, {"\\move", wxTRANSLATE("Move")}, {"\\org", wxTRANSLATE("Rotation origin")},
		{"\\fn", wxTRANSLATE("Font")}, {"\\fs", wxTRANSLATE("Font size")}, {"\\fs+", wxTRANSLATE("Increase font size")}, {"\\fs-", wxTRANSLATE("Decrease font size")},
		{"\\b", wxTRANSLATE("Bold")}, {"\\i", wxTRANSLATE("Italic")}, {"\\u", wxTRANSLATE("Underline")}, {"\\s", wxTRANSLATE("Strikeout")},
		{"\\fsp", wxTRANSLATE("Character spacing")}, {"\\fe", wxTRANSLATE("Font encoding")}, {"\\fscx", wxTRANSLATE("X scale")}, {"\\fscy", wxTRANSLATE("Y scale")},
		{"\\frx", wxTRANSLATE("X rotation")}, {"\\fry", wxTRANSLATE("Y rotation")}, {"\\frz", wxTRANSLATE("Z rotation")}, {"\\fr", wxTRANSLATE("Z rotation")},
		{"\\fax", wxTRANSLATE("X shear")}, {"\\fay", wxTRANSLATE("Y shear")},
		{"\\c", wxTRANSLATE("Primary color")}, {"\\1c", wxTRANSLATE("Primary color")}, {"\\2c", wxTRANSLATE("Secondary color")}, {"\\3c", wxTRANSLATE("Outline color")}, {"\\4c", wxTRANSLATE("Shadow color")},
		{"\\alpha", wxTRANSLATE("Global opacity")}, {"\\1a", wxTRANSLATE("Primary opacity")}, {"\\2a", wxTRANSLATE("Secondary opacity")}, {"\\3a", wxTRANSLATE("Outline opacity")}, {"\\4a", wxTRANSLATE("Shadow opacity")},
		{"\\bord", wxTRANSLATE("Outline width")}, {"\\xbord", wxTRANSLATE("X outline width")}, {"\\ybord", wxTRANSLATE("Y outline width")},
		{"\\shad", wxTRANSLATE("Shadow distance")}, {"\\xshad", wxTRANSLATE("X shadow distance")}, {"\\yshad", wxTRANSLATE("Y shadow distance")},
		{"\\blur", wxTRANSLATE("Gaussian blur")}, {"\\be", wxTRANSLATE("Edge blur")},
		{"\\t", wxTRANSLATE("Transform")}, {"\\fad", wxTRANSLATE("Fade")}, {"\\fade", wxTRANSLATE("Advanced fade")},
		{"\\clip", wxTRANSLATE("Clip")}, {"\\iclip", wxTRANSLATE("Inverse clip")}, {"\\p", wxTRANSLATE("Drawing mode")}, {"\\pbo", wxTRANSLATE("Drawing baseline")},
		{"\\r", wxTRANSLATE("Reset style")}, {"\\q", wxTRANSLATE("Wrapping style")},
		{"\\k", wxTRANSLATE("Karaoke")}, {"\\K", wxTRANSLATE("Karaoke sweep")}, {"\\kf", wxTRANSLATE("Karaoke fill")}, {"\\ko", wxTRANSLATE("Karaoke outline")}, {"\\kt", wxTRANSLATE("Karaoke timing")}
	};
	auto it = labels.find(name);
	return it == labels.end() ? translated(_("ASS tag")) : translated(it->second);
}

std::string category_for_tag(std::string_view name) {
	if (name == "\\an" || name == "\\pos" || name == "\\move" || name == "\\org") return translated(_("Positioning"));
	if (name == "\\fn" || name == "\\fs" || name == "\\fs+" || name == "\\fs-" || name == "\\b" || name == "\\i" || name == "\\u" || name == "\\s" || name == "\\fsp" || name == "\\fe") return translated(_("Font"));
	if (name == "\\fscx" || name == "\\fscy") return translated(_("Scale"));
	if (name == "\\frx" || name == "\\fry" || name == "\\frz" || name == "\\fr" || name == "\\fax" || name == "\\fay") return translated(_("Rotation/Perspective"));
	if (name == "\\c" || name == "\\1c" || name == "\\2c" || name == "\\3c" || name == "\\4c" || name == "\\alpha" || name == "\\1a" || name == "\\2a" || name == "\\3a" || name == "\\4a") return translated(_("Color"));
	if (name == "\\bord" || name == "\\xbord" || name == "\\ybord" || name == "\\shad" || name == "\\xshad" || name == "\\yshad" || name == "\\blur" || name == "\\be") return translated(_("Border"));
	if (name == "\\t" || name == "\\fad" || name == "\\fade") return translated(_("Animation"));
	if (name == "\\clip" || name == "\\iclip") return translated(_("Clip"));
	if (name == "\\p" || name == "\\pbo") return translated(_("Drawing"));
	if (name == "\\k" || name == "\\K" || name == "\\kf" || name == "\\ko" || name == "\\kt") return translated(_("Karaoke"));
	return translated(_("Advanced"));
}

template<class T>
void sort_unique(T& values) {
	std::sort(values.begin(), values.end());
	values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

std::vector<Model::Part> Model::ParseParts(std::string_view source) {
	std::vector<Part> result;
	auto document = ast::Document::Parse(source);
	result.reserve(document.Segments().size());
	for (auto const& segment : document.Segments()) {
		Part part;
		part.original = segment.Serialize();
		switch (segment.Kind()) {
			case ast::SegmentKind::Text: part.kind = PartKind::Text; break;
			case ast::SegmentKind::Comment: part.kind = PartKind::Comment; break;
			case ast::SegmentKind::Drawing: part.kind = PartKind::Drawing; break;
			case ast::SegmentKind::Override: {
				part.kind = PartKind::Override;
				if (auto const* block = segment.Block()) {
					part.nodes.reserve(block->Nodes().size());
					for (auto const& node : block->Nodes()) {
						part.nodes.push_back({
							node.Serialize(), node.Name(), node.IsKnownTag(),
							node.Kind() == ast::OverrideNodeKind::Tag
						});
					}
				}
				break;
			}
		}
		result.push_back(std::move(part));
	}
	return result;
}

std::string Model::SerializePart(Part const& part) {
	if (part.kind != PartKind::Override || !part.dirty) return part.original;
	std::string result = "{";
	for (auto const& node : part.nodes) result += node.source;
	result += '}';
	return result;
}

void Model::SetSource(std::string source) {
	original_ = std::move(source);
	parts_ = ParseParts(original_);
	dirty_ = false;
	RebuildItems();
}

std::string Model::Serialize() const {
	if (!dirty_) return original_;
	std::string result;
	for (auto const& part : parts_) result += SerializePart(part);
	return result;
}

void Model::RebuildItems() {
	items_.clear();
	locations_.clear();
	for (size_t part_index = 0; part_index < parts_.size(); ++part_index) {
		auto const& part = parts_[part_index];
		if (part.kind == PartKind::Override && !part.nodes.empty()) {
			for (size_t node_index = 0; node_index < part.nodes.size(); ++node_index) {
				auto const& node = part.nodes[node_index];
				Item item;
				item.source = node.source;
				if (node.tag && node.known) {
					item.kind = ItemKind::Tag;
					item.label = label_for_tag(node.name);
					item.category = category_for_tag(node.name);
				}
				else {
					item.kind = ItemKind::Raw;
					item.label = translated(_("Raw ASS"));
					item.category = translated(_("Advanced"));
				}
				items_.push_back(std::move(item));
				locations_.push_back({part_index, node_index, true});
			}
			continue;
		}

		Item item;
		item.source = SerializePart(part);
		switch (part.kind) {
			case PartKind::Text:
				item.kind = ItemKind::Text; item.label = translated(_("Text")); item.category = translated(_("Text")); break;
			case PartKind::Comment:
				item.kind = ItemKind::Comment; item.label = translated(_("Comment")); item.category = translated(_("Advanced")); break;
			case PartKind::Drawing:
				item.kind = ItemKind::Drawing; item.label = translated(_("ASS Drawing")); item.category = translated(_("Drawing")); break;
			case PartKind::Override:
				item.kind = ItemKind::Raw; item.label = translated(_("Raw ASS")); item.category = translated(_("Advanced")); break;
		}
		items_.push_back(std::move(item));
		locations_.push_back({part_index, 0, false});
	}
}

std::string Model::Copy(std::vector<size_t> selection) const {
	sort_unique(selection);
	selection.erase(std::remove_if(selection.begin(), selection.end(), [this](size_t i) {
		return i >= locations_.size();
	}), selection.end());

	std::string result;
	for (size_t offset = 0; offset < selection.size();) {
		size_t item_index = selection[offset];
		auto const& location = locations_[item_index];
		if (!location.is_node) {
			result += SerializePart(parts_[location.part]);
			++offset;
			continue;
		}

		result += '{';
		result += parts_[location.part].nodes[location.node].source;
		size_t last_node = location.node;
		++offset;
		while (offset < selection.size()) {
			auto const& next = locations_[selection[offset]];
			if (!next.is_node || next.part != location.part || next.node != last_node + 1) break;
			result += parts_[next.part].nodes[next.node].source;
			last_node = next.node;
			++offset;
		}
		result += '}';
	}
	return result;
}

std::string Model::Cut(std::vector<size_t> selection) {
	auto result = Copy(selection);
	Delete(std::move(selection));
	return result;
}

bool Model::Delete(std::vector<size_t> selection) {
	sort_unique(selection);
	std::vector<bool> delete_part(parts_.size());
	std::vector<std::vector<size_t>> delete_nodes(parts_.size());
	bool changed = false;
	for (size_t item : selection) {
		if (item >= locations_.size()) continue;
		auto const& location = locations_[item];
		changed = true;
		if (location.is_node) delete_nodes[location.part].push_back(location.node);
		else delete_part[location.part] = true;
	}
	if (!changed) return false;

	for (size_t part_index = parts_.size(); part_index-- > 0;) {
		if (delete_part[part_index]) {
			parts_.erase(parts_.begin() + part_index);
			continue;
		}
		auto& nodes = delete_nodes[part_index];
		if (nodes.empty()) continue;
		sort_unique(nodes);
		for (size_t i = nodes.size(); i-- > 0;)
			parts_[part_index].nodes.erase(parts_[part_index].nodes.begin() + nodes[i]);
		if (parts_[part_index].nodes.empty())
			parts_.erase(parts_.begin() + part_index);
		else
			parts_[part_index].dirty = true;
	}
	dirty_ = true;
	RebuildItems();
	return true;
}

bool Model::Paste(std::optional<size_t> after, std::string_view source) {
	if (source.empty()) return false;
	std::string normalized(source);
	if (normalized.front() == '\\' && normalized != "\\N" && normalized != "\\n" && normalized != "\\h")
		normalized = '{' + normalized + '}';
	auto inserted = ParseParts(normalized);
	if (inserted.empty()) return false;

	if (!after || *after >= locations_.size()) {
		parts_.insert(parts_.end(), std::make_move_iterator(inserted.begin()), std::make_move_iterator(inserted.end()));
	}
	else {
		auto location = locations_[*after];
		if (location.is_node && inserted.size() == 1 && inserted.front().kind == PartKind::Override && !inserted.front().nodes.empty()) {
			auto& part = parts_[location.part];
			part.nodes.insert(part.nodes.begin() + location.node + 1,
				std::make_move_iterator(inserted.front().nodes.begin()),
				std::make_move_iterator(inserted.front().nodes.end()));
			part.dirty = true;
		}
		else if (!location.is_node) {
			parts_.insert(parts_.begin() + location.part + 1,
				std::make_move_iterator(inserted.begin()), std::make_move_iterator(inserted.end()));
		}
		else {
			Part right = parts_[location.part];
			right.nodes.erase(right.nodes.begin(), right.nodes.begin() + location.node + 1);
			right.dirty = true;
			parts_[location.part].nodes.erase(parts_[location.part].nodes.begin() + location.node + 1, parts_[location.part].nodes.end());
			parts_[location.part].dirty = true;

			auto position = parts_.begin() + location.part + 1;
			position = parts_.insert(position,
				std::make_move_iterator(inserted.begin()), std::make_move_iterator(inserted.end()));
			position += inserted.size();
			if (!right.nodes.empty()) parts_.insert(position, std::move(right));
		}
	}
	dirty_ = true;
	RebuildItems();
	return true;
}

bool Model::Replace(size_t item, std::string_view source) {
	if (item >= locations_.size()) return false;
	auto location = locations_[item];
	if (source.empty()) return Delete({item});

	if (!location.is_node) {
		auto replacement = ParseParts(source);
		if (replacement.empty()) return false;
		parts_.erase(parts_.begin() + location.part);
		parts_.insert(parts_.begin() + location.part,
			std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
	}
	else {
		std::string body(source);
		if (body.size() >= 2 && body.front() == '{' && body.back() == '}')
			body = body.substr(1, body.size() - 2);
		auto parsed = ast::OverrideBlock::Parse('{' + body + '}');
		std::vector<Node> replacement;
		for (auto const& node : parsed.Nodes()) {
			replacement.push_back({node.Serialize(), node.Name(), node.IsKnownTag(), node.Kind() == ast::OverrideNodeKind::Tag});
		}
		if (replacement.empty()) return false;
		auto& part = parts_[location.part];
		part.nodes.erase(part.nodes.begin() + location.node);
		part.nodes.insert(part.nodes.begin() + location.node,
			std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
		part.dirty = true;
	}
	dirty_ = true;
	RebuildItems();
	return true;
}

bool Model::Insert(std::optional<size_t> after, Feature const& feature) {
	return Paste(after, feature.source);
}

std::vector<Feature> const& Model::Features() {
	static const std::vector<Feature> features = {
		{translated(_("Positioning")), translated(_("Anchor/Alignment")), "an", "{\\an8}"}, {translated(_("Positioning")), translated(_("Position")), "pos", "{\\pos(0,0)}"},
		{translated(_("Positioning")), translated(_("Move")), "move", "{\\move(0,0,100,100)}"}, {translated(_("Positioning")), translated(_("Rotation origin")), "org", "{\\org(0,0)}"},
		{translated(_("Font")), translated(_("Font")), "fn", "{\\fnArial}"}, {translated(_("Font")), translated(_("Font size")), "fs", "{\\fs50}"},
		{translated(_("Font")), translated(_("Bold")), "b", "{\\b1}"}, {translated(_("Font")), translated(_("Italic")), "i", "{\\i1}"},
		{translated(_("Font")), translated(_("Underline")), "u", "{\\u1}"}, {translated(_("Font")), translated(_("Strikeout")), "s", "{\\s1}"},
		{translated(_("Font")), translated(_("Character spacing")), "fsp", "{\\fsp0}"}, {translated(_("Font")), translated(_("Font encoding")), "fe", "{\\fe1}"},
		{translated(_("Scale")), translated(_("X scale")), "fscx", "{\\fscx100}"}, {translated(_("Scale")), translated(_("Y scale")), "fscy", "{\\fscy100}"},
		{translated(_("Rotation/Perspective")), translated(_("X rotation")), "frx", "{\\frx0}"}, {translated(_("Rotation/Perspective")), translated(_("Y rotation")), "fry", "{\\fry0}"},
		{translated(_("Rotation/Perspective")), translated(_("Z rotation")), "frz", "{\\frz0}"}, {translated(_("Rotation/Perspective")), translated(_("X shear")), "fax", "{\\fax0}"},
		{translated(_("Rotation/Perspective")), translated(_("Y shear")), "fay", "{\\fay0}"},
		{translated(_("Color")), translated(_("Primary color")), "1c", "{\\1c&HFFFFFF&}"}, {translated(_("Color")), translated(_("Secondary color")), "2c", "{\\2c&HFFFFFF&}"},
		{translated(_("Color")), translated(_("Outline color")), "3c", "{\\3c&H000000&}"}, {translated(_("Color")), translated(_("Shadow color")), "4c", "{\\4c&H000000&}"},
		{translated(_("Color")), translated(_("Global opacity")), "alpha", "{\\alpha&H00&}"}, {translated(_("Color")), translated(_("Primary opacity")), "1a", "{\\1a&H00&}"},
		{translated(_("Color")), translated(_("Secondary opacity")), "2a", "{\\2a&H00&}"}, {translated(_("Color")), translated(_("Outline opacity")), "3a", "{\\3a&H00&}"},
		{translated(_("Color")), translated(_("Shadow opacity")), "4a", "{\\4a&H00&}"},
		{translated(_("Border")), translated(_("Outline width")), "bord", "{\\bord2}"}, {translated(_("Border")), translated(_("X outline width")), "xbord", "{\\xbord2}"},
		{translated(_("Border")), translated(_("Y outline width")), "ybord", "{\\ybord2}"}, {translated(_("Border")), translated(_("Shadow distance")), "shad", "{\\shad2}"},
		{translated(_("Border")), translated(_("X shadow distance")), "xshad", "{\\xshad2}"}, {translated(_("Border")), translated(_("Y shadow distance")), "yshad", "{\\yshad2}"},
		{translated(_("Border")), translated(_("Gaussian blur")), "blur", "{\\blur1}"}, {translated(_("Border")), translated(_("Edge blur")), "be", "{\\be1}"},
		{translated(_("Animation")), translated(_("Transform")), "t", "{\\t(0,250,\\fscx100\\fscy100)}"},
		{translated(_("Animation")), translated(_("Fade")), "fad", "{\\fad(200,200)}"}, {translated(_("Animation")), translated(_("Advanced fade")), "fade", "{\\fade(255,0,255,0,200,800,1000)}"},
		{translated(_("Clip")), translated(_("Rectangle clip")), "clip", "{\\clip(0,0,100,100)}"}, {translated(_("Clip")), translated(_("Vector clip")), "clip", "{\\clip(m 0 0 l 100 0 100 100 0 100)}"},
		{translated(_("Clip")), translated(_("Inverse clip")), "iclip", "{\\iclip(0,0,100,100)}"},
		{translated(_("Drawing")), translated(_("ASS Drawing")), "p", "{\\p1}m 0 0 l 100 0 100 100 0 100{\\p0}"},
		{translated(_("Drawing")), translated(_("Drawing baseline")), "pbo", "{\\pbo0}"},
		{translated(_("Karaoke")), translated(_("Karaoke")), "k", "{\\k20}"}, {translated(_("Karaoke")), translated(_("Karaoke sweep")), "K", "{\\K20}"},
		{translated(_("Karaoke")), translated(_("Karaoke fill")), "kf", "{\\kf20}"}, {translated(_("Karaoke")), translated(_("Karaoke outline")), "ko", "{\\ko20}"},
		{translated(_("Karaoke")), translated(_("Karaoke timing")), "kt", "{\\kt20}"},
		{translated(_("Advanced")), translated(_("Reset style")), "r", "{\\r}"}, {translated(_("Advanced")), translated(_("Wrapping style")), "q", "{\\q2}"},
		{translated(_("Advanced")), translated(_("Hard line break")), "N", "\\N"}, {translated(_("Advanced")), translated(_("Soft line break")), "n", "\\n"}, {translated(_("Advanced")), translated(_("Non-breaking space")), "h", "\\h"}
	};
	return features;
}

std::vector<Feature> Model::SearchFeatures(std::string_view query) {
	auto needle = lower_ascii(query);
	std::vector<Feature> result;
	for (auto const& feature : Features()) {
		auto haystack = lower_ascii(feature.category + " " + feature.name + " " + feature.tag);
		if (needle.empty() || haystack.find(needle) != std::string::npos)
			result.push_back(feature);
	}
	return result;
}

} // namespace ass::blocks

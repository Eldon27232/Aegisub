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
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <sstream>
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

struct OriginRun {
	Origin origin;
	size_t length;
};

uint64_t source_hash(std::string_view source) {
	uint64_t hash = UINT64_C(14695981039346656037);
	for (unsigned char byte : source) {
		hash ^= byte;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

char origin_code(Origin origin) {
	switch (origin) {
		case Origin::Manual: return 'M';
		case Origin::Gui: return 'G';
		case Origin::Raw: return 'R';
	}
	return 'R';
}

std::optional<Origin> parse_origin(char code) {
	switch (code) {
		case 'M': return Origin::Manual;
		case 'G': return Origin::Gui;
		case 'R': return Origin::Raw;
		default: return {};
	}
}

std::optional<std::vector<OriginRun>> parse_origin_metadata(
	std::string_view source, std::string_view metadata) {
	if (metadata.substr(0, 3) != "v1;") return {};
	auto hash_end = metadata.find(';', 3);
	if (hash_end == std::string_view::npos || hash_end != 19) return {};
	uint64_t expected_hash = 0;
	auto hash_text = metadata.substr(3, hash_end - 3);
	auto [hash_cursor, hash_error] = std::from_chars(
		hash_text.data(), hash_text.data() + hash_text.size(), expected_hash, 16);
	if (hash_error != std::errc() || hash_cursor != hash_text.data() + hash_text.size()
		|| expected_hash != source_hash(source))
		return {};

	std::vector<OriginRun> runs;
	size_t offset = hash_end + 1;
	size_t covered = 0;
	while (offset < metadata.size()) {
		auto origin = parse_origin(metadata[offset++]);
		if (!origin) return {};
		auto end = metadata.find(',', offset);
		if (end == std::string_view::npos) end = metadata.size();
		if (end == offset) return {};
		size_t length = 0;
		auto [cursor, error] = std::from_chars(
			metadata.data() + offset, metadata.data() + end, length);
		if (error != std::errc() || cursor != metadata.data() + end
			|| length > source.size() - covered)
			return {};
		runs.push_back({*origin, length});
		covered += length;
		offset = end + (end < metadata.size());
	}
	if (runs.empty() || covered != source.size()) return {};
	return runs;
}

struct ColourLocation {
	ast::OverrideBlock *scope;
	size_t node;
	std::string alpha_tag;
};

agi::Color ass_colour_value(std::string_view value) {
	// The generic CSS/colour parser expects full-width colour strings; ASS
	// alpha is a two-digit hex value and ASS RGB also permits short hex values.
	auto first = value.find_first_not_of(" \t\r\n");
	if (first != std::string_view::npos) value.remove_prefix(first);
	auto last = value.find_last_not_of(" \t\r\n");
	if (last != std::string_view::npos) value = value.substr(0, last + 1);
	if (value.size() >= 2 && value[0] == '&' && (value[1] == 'H' || value[1] == 'h')) {
		value.remove_prefix(2);
		if (!value.empty() && value.back() == '&') value.remove_suffix(1);
		unsigned parsed = 0;
		auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed, 16);
		if (error == std::errc() && end == value.data() + value.size())
			return agi::Color(parsed & 0xFF, (parsed >> 8) & 0xFF, (parsed >> 16) & 0xFF);
		return {};
	}
	return agi::Color(value);
}

std::optional<ColourLocation> find_colour(ast::OverrideBlock& block, size_t node,
	std::vector<size_t> const& nested_path) {
	auto scope = &block;
	for (auto child : nested_path) {
		auto parent = scope->MutableNode(node);
		if (!parent || !parent->Transform()) return {};
		scope = parent->Transform();
		node = child;
	}
	auto target = scope->MutableNode(node);
	if (!target) return {};
	auto const& name = target->Name();
	if (name == "\\c") return ColourLocation{scope, node, "\\1a"};
	if (name == "\\1c" || name == "\\2c" || name == "\\3c" || name == "\\4c")
		return ColourLocation{scope, node, std::string("\\") + name[1] + "a"};
	return {};
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
			case ast::SegmentKind::Text:
				part.kind = PartKind::Text;
				part.origin = Origin::Manual;
				break;
			case ast::SegmentKind::Comment:
				part.kind = PartKind::Comment;
				part.origin = Origin::Raw;
				break;
			case ast::SegmentKind::Drawing:
				part.kind = PartKind::Drawing;
				part.origin = Origin::Raw;
				break;
			case ast::SegmentKind::Override: {
				part.kind = PartKind::Override;
				part.origin = Origin::Gui;
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

void Model::PreserveUnchangedNodeOrigins(std::vector<Node> const& before, std::vector<Node>& after) {
	std::vector<bool> used(before.size());
	for (auto& node : after) {
		for (size_t i = 0; i < before.size(); ++i) {
			if (used[i] || before[i].source != node.source) continue;
			node.origin = before[i].origin;
			used[i] = true;
			break;
		}
	}
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
	if (std::none_of(parts_.begin(), parts_.end(), [](Part const& part) {
		return part.kind == PartKind::Text;
	})) {
		Part part;
		part.kind = PartKind::Text;
		part.origin = Origin::Manual;
		parts_.push_back(std::move(part));
	}
	dirty_ = false;
	RebuildItems();
}

void Model::SetSourceWithManualSpan(std::string source, size_t offset, size_t length) {
	if (offset > source.size() || length > source.size() - offset) {
		original_ = std::move(source);
		parts_.clear();
		Part manual;
		manual.kind = PartKind::Text;
		manual.origin = Origin::Manual;
		manual.original = original_;
		parts_.push_back(std::move(manual));
		dirty_ = false;
		RebuildItems();
		return;
	}

	original_ = std::move(source);
	parts_.clear();
	auto append_structure = [this](std::string_view text) {
		auto parsed = ParseParts(text);
		for (auto& part : parsed) {
			if (part.kind != PartKind::Comment && part.kind != PartKind::Drawing &&
				part.kind != PartKind::Raw)
				part.origin = Origin::Gui;
		}
		parts_.insert(parts_.end(),
			std::make_move_iterator(parsed.begin()),
			std::make_move_iterator(parsed.end()));
	};

	auto view = std::string_view(original_);
	append_structure(view.substr(0, offset));
	Part manual;
	manual.kind = PartKind::Text;
	manual.origin = Origin::Manual;
	manual.original = std::string(view.substr(offset, length));
	parts_.push_back(std::move(manual));
	append_structure(view.substr(offset + length));
	dirty_ = false;
	RebuildItems();
}

void Model::SetStoredSource(std::string source, std::string_view origin_metadata) {
	if (origin_metadata.empty()) {
		SetSource(std::move(source));
		return;
	}
	original_ = std::move(source);
	parts_.clear();
	auto append_opaque = [this](std::string_view text, Origin origin) {
		Part part;
		part.kind = origin == Origin::Raw ? PartKind::Raw : PartKind::Text;
		part.origin = origin;
		part.original = std::string(text);
		parts_.push_back(std::move(part));
	};

	auto node_marker = origin_metadata.find('|');
	auto runs = parse_origin_metadata(original_, origin_metadata.substr(0, node_marker));
	if (!runs) {
		append_opaque(original_, Origin::Manual);
	}
	else {
		size_t offset = 0;
		for (auto const& run : *runs) {
			auto text = std::string_view(original_).substr(offset, run.length);
			if (run.origin != Origin::Gui) {
				append_opaque(text, run.origin);
			}
			else {
				auto parsed = ParseParts(text);
				for (auto& part : parsed) part.origin = Origin::Gui;
				parts_.insert(parts_.end(),
					std::make_move_iterator(parsed.begin()),
					std::make_move_iterator(parsed.end()));
			}
			offset += run.length;
		}
		if (node_marker != std::string_view::npos) {
			bool valid = true;
			std::vector<bool> seen(parts_.size());
			auto encoded = origin_metadata.substr(node_marker + 1);
			if (encoded.empty()) valid = false;
			while (valid && !encoded.empty()) {
				auto end = encoded.find(',');
				auto entry = encoded.substr(0, end);
				auto colon = entry.find(':');
				size_t index = 0;
				if (colon == std::string_view::npos) { valid = false; break; }
				auto [cursor, error] = std::from_chars(entry.data(), entry.data() + colon, index);
				if (error != std::errc() || cursor != entry.data() + colon
					|| index >= parts_.size() || seen[index]
					|| parts_[index].kind != PartKind::Override
					|| entry.size() - colon - 1 != parts_[index].nodes.size()) {
					valid = false;
					break;
				}
				seen[index] = true;
				for (size_t i = 0; i < parts_[index].nodes.size(); ++i) {
					auto origin = parse_origin(entry[colon + 1 + i]);
					if (!origin) { valid = false; break; }
					parts_[index].nodes[i].origin = *origin;
				}
				if (end == std::string_view::npos) break;
				encoded.remove_prefix(end + 1);
				if (encoded.empty()) valid = false;
			}
			if (!valid) {
				parts_.clear();
				append_opaque(original_, Origin::Manual);
			}
		}
	}
	if (parts_.empty()) append_opaque({}, Origin::Manual);
	dirty_ = false;
	RebuildItems();
}

bool Model::SetGuiFirstOverride(std::string source) {
	auto previous_source = Serialize();
	auto old_parsed = ParseParts(previous_source);
	auto new_parsed = ParseParts(source);
	if (new_parsed.empty() || new_parsed.front().kind != PartKind::Override)
		return false;

	size_t old_prefix = !old_parsed.empty() && old_parsed.front().kind == PartKind::Override
		? old_parsed.front().original.size() : 0;
	size_t new_prefix = new_parsed.front().original.size();
	if (std::string_view(previous_source).substr(old_prefix) != std::string_view(source).substr(new_prefix))
		return false;
	std::vector<Node> old_nodes;
	if (old_prefix) {
		old_nodes = old_parsed.front().nodes;
		for (auto const& part : parts_) {
			auto size = SerializePart(part).size();
			if (!size) continue;
			if (part.kind == PartKind::Override && size == old_prefix)
				old_nodes = part.nodes;
			else
				for (auto& node : old_nodes) node.origin = part.origin;
			break;
		}
	}

	std::vector<Part> tail;
	size_t remaining = old_prefix;
	for (auto const& existing : parts_) {
		Part part = existing;
		size_t size = SerializePart(part).size();
		if (!remaining) {
			tail.push_back(std::move(part));
		}
		else if (size <= remaining) {
			remaining -= size;
		}
		else {
			// A manually typed first override can be inside one opaque text span.
			if (part.kind != PartKind::Text && part.kind != PartKind::Raw) return false;
			part.original.erase(0, remaining);
			tail.push_back(std::move(part));
			remaining = 0;
		}
	}
	if (remaining) return false;

	Part prefix = std::move(new_parsed.front());
	prefix.origin = Origin::Gui;
	PreserveUnchangedNodeOrigins(old_nodes, prefix.nodes);
	parts_.clear();
	parts_.push_back(std::move(prefix));
	parts_.insert(parts_.end(), std::make_move_iterator(tail.begin()),
		std::make_move_iterator(tail.end()));
	original_ = std::move(source);
	dirty_ = false;
	RebuildItems();
	return true;
}

std::string Model::Serialize() const {
	if (!dirty_) return original_;
	std::string result;
	for (auto const& part : parts_) result += SerializePart(part);
	return result;
}

std::string Model::OriginMetadata() const {
	std::string source = Serialize();
	std::vector<OriginRun> runs;
	for (auto const& part : parts_) {
		size_t length = SerializePart(part).size();
		if (!runs.empty() && runs.back().origin == part.origin)
			runs.back().length += length;
		else
			runs.push_back({part.origin, length});
	}
	if (runs.empty()) runs.push_back({Origin::Manual, source.size()});

	std::ostringstream result;
	result << "v1;" << std::hex << std::setfill('0') << std::setw(16)
		<< source_hash(source) << ';' << std::dec;
	for (size_t i = 0; i < runs.size(); ++i) {
		if (i) result << ',';
		result << origin_code(runs[i].origin) << runs[i].length;
	}
	bool first_node_part = true;
	for (size_t i = 0; i < parts_.size(); ++i) {
		auto const& part = parts_[i];
		if (part.kind != PartKind::Override || std::all_of(part.nodes.begin(), part.nodes.end(),
			[](Node const& node) { return node.origin == Origin::Gui; })) continue;
		result << (first_node_part ? '|' : ',') << i << ':';
		for (auto const& node : part.nodes) result << origin_code(node.origin);
		first_node_part = false;
	}
	return result.str();
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
				if (node.tag && node.known && node.origin == Origin::Gui) {
					item.kind = ItemKind::Tag;
					item.origin = Origin::Gui;
					item.label = label_for_tag(node.name);
					item.category = category_for_tag(node.name);
				}
				else {
					item.kind = ItemKind::Raw;
					item.origin = node.origin == Origin::Manual ? Origin::Manual : Origin::Raw;
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
		item.origin = part.origin;
		switch (part.kind) {
			case PartKind::Text:
				item.kind = ItemKind::Text; item.label = translated(_("Text")); item.category = translated(_("Text")); break;
			case PartKind::Comment:
				item.kind = ItemKind::Comment; item.label = translated(_("Comment")); item.category = translated(_("Advanced")); break;
			case PartKind::Drawing:
				item.kind = ItemKind::Drawing; item.label = translated(_("ASS Drawing")); item.category = translated(_("Drawing")); break;
			case PartKind::Override:
				item.kind = ItemKind::Raw; item.label = translated(_("Raw ASS")); item.category = translated(_("Advanced")); break;
			case PartKind::Raw:
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
		else {
			parts_[part_index].dirty = true;
			parts_[part_index].origin = Origin::Gui;
		}
	}
	if (parts_.empty()) {
		Part part;
		part.kind = PartKind::Text;
		part.origin = Origin::Manual;
		parts_.push_back(std::move(part));
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
	for (auto& part : inserted) part.origin = Origin::Gui;

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
			part.origin = Origin::Gui;
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
			parts_[location.part].origin = Origin::Gui;

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
		for (auto& part : replacement) part.origin = Origin::Gui;
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
		part.origin = Origin::Gui;
	}
	dirty_ = true;
	RebuildItems();
	return true;
}

bool Model::ReplaceManual(size_t item, std::string_view source) {
	if (item >= locations_.size()) return false;
	auto location = locations_[item];
	if (location.is_node || parts_[location.part].kind != PartKind::Text) return false;

	Part replacement;
	replacement.kind = PartKind::Text;
	replacement.origin = Origin::Manual;
	replacement.original = std::string(source);
	parts_[location.part] = std::move(replacement);
	dirty_ = true;
	RebuildItems();
	return true;
}

bool Model::Insert(std::optional<size_t> after, Feature const& feature) {
	if (!after) {
		auto manual = std::find_if(parts_.begin(), parts_.end(), [](Part const& part) {
			return part.kind == PartKind::Text && part.origin == Origin::Manual;
		});
		if (manual != parts_.end()) {
			size_t manual_part = std::distance(parts_.begin(), manual);
			std::optional<size_t> preceding_item;
			for (size_t i = 0; i < locations_.size(); ++i) {
				if (locations_[i].part >= manual_part) break;
				preceding_item = i;
			}
			if (preceding_item) return Paste(preceding_item, feature.source);

			auto inserted = ParseParts(feature.source);
			if (inserted.empty()) return false;
			for (auto& part : inserted) part.origin = Origin::Gui;
			parts_.insert(parts_.begin() + manual_part,
				std::make_move_iterator(inserted.begin()),
				std::make_move_iterator(inserted.end()));
			dirty_ = true;
			RebuildItems();
			return true;
		}
	}
	return Paste(after, feature.source);
}

agi::Color Model::GetColour(size_t item, std::vector<size_t> nested_path) const {
	agi::Color colour(255, 255, 255, 0);
	if (item >= locations_.size() || !locations_[item].is_node) return colour;
	auto const& location = locations_[item];
	auto block = ast::OverrideBlock::Parse(SerializePart(parts_[location.part]));
	auto target = find_colour(block, location.node, nested_path);
	if (!target) return colour;
	auto node = target->scope->MutableNode(target->node);
	if (!node->Arguments().empty() && !node->Arguments()[0].empty())
		colour = ass_colour_value(node->Arguments()[0]);
	// Override colours carry RGB only. The last global/channel alpha in this
	// exact scope determines opacity; transform bodies do not leak into it.
	colour.a = 0;
	for (auto const& sibling : target->scope->Nodes()) {
		if (sibling.Name() == "\\r") colour.a = 0;
		else if (sibling.Name() == "\\alpha" || sibling.Name() == target->alpha_tag)
			colour.a = sibling.Arguments().empty() ? 0 : ass_colour_value(sibling.Arguments()[0]).r;
	}
	return colour;
}

bool Model::SetColour(size_t item, std::vector<size_t> nested_path, agi::Color colour) {
	if (item >= locations_.size() || !locations_[item].is_node) return false;
	auto const location = locations_[item];
	auto& part = parts_[location.part];
	auto block = ast::OverrideBlock::Parse(SerializePart(part));
	auto target = find_colour(block, location.node, nested_path);
	if (!target) return false;
	target->scope->MutableNode(target->node)->SetArgument(0, colour.GetAssOverrideFormatted());

	// Reuse the final effective channel alpha. If a later global alpha/reset
	// overrides it, append one channel-specific value after that override.
	// Subsequent live picker updates reuse this node rather than accumulating.
	std::optional<size_t> alpha_node;
	auto const& siblings = target->scope->Nodes();
	for (size_t i = 0; i < siblings.size(); ++i) {
		if (siblings[i].Name() == "\\alpha" || siblings[i].Name() == "\\r") alpha_node.reset();
		else if (siblings[i].Name() == target->alpha_tag) alpha_node = i;
	}
	auto alpha = agi::format("&H%02X&", int(colour.a));
	if (alpha_node)
		target->scope->MutableNode(*alpha_node)->SetArgument(0, alpha);
	else
		target->scope->AppendTag(target->alpha_tag + alpha);

	auto old_nodes = part.nodes;
	part.nodes.clear();
	for (auto const& node : block.Nodes())
		part.nodes.push_back({node.Serialize(), node.Name(), node.IsKnownTag(), node.Kind() == ast::OverrideNodeKind::Tag});
	PreserveUnchangedNodeOrigins(old_nodes, part.nodes);
	part.dirty = true;
	part.origin = Origin::Gui;
	dirty_ = true;
	RebuildItems();
	return true;
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

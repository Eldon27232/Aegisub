// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_block_editor_model.h"

#include "ass_override_ast.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_map>

namespace ass::blocks {
namespace {

std::string lower_ascii(std::string_view value) {
	std::string result(value);
	for (char& c : result) {
		unsigned char byte = static_cast<unsigned char>(c);
		if (byte < 0x80) c = static_cast<char>(std::tolower(byte));
	}
	return result;
}

std::string label_for_tag(std::string_view name) {
	static const std::unordered_map<std::string_view, std::string_view> labels = {
		{"\\an", "原点/对齐"}, {"\\pos", "位置"}, {"\\move", "移动"}, {"\\org", "旋转原点"},
		{"\\fn", "字体"}, {"\\fs", "字号"}, {"\\fs+", "增大字号"}, {"\\fs-", "减小字号"},
		{"\\b", "粗体"}, {"\\i", "斜体"}, {"\\u", "下划线"}, {"\\s", "删除线"},
		{"\\fsp", "字距"}, {"\\fe", "字体编码"}, {"\\fscx", "X 缩放"}, {"\\fscy", "Y 缩放"},
		{"\\frx", "X 轴旋转"}, {"\\fry", "Y 轴旋转"}, {"\\frz", "Z 轴旋转"}, {"\\fr", "Z 轴旋转"},
		{"\\fax", "X 轴倾斜"}, {"\\fay", "Y 轴倾斜"},
		{"\\c", "正文颜色"}, {"\\1c", "正文颜色"}, {"\\2c", "次要颜色"}, {"\\3c", "描边颜色"}, {"\\4c", "阴影颜色"},
		{"\\alpha", "全局透明度"}, {"\\1a", "正文透明度"}, {"\\2a", "次要透明度"}, {"\\3a", "描边透明度"}, {"\\4a", "阴影透明度"},
		{"\\bord", "描边宽度"}, {"\\xbord", "X 描边宽度"}, {"\\ybord", "Y 描边宽度"},
		{"\\shad", "阴影距离"}, {"\\xshad", "X 阴影距离"}, {"\\yshad", "Y 阴影距离"},
		{"\\blur", "高斯模糊"}, {"\\be", "边缘模糊"},
		{"\\t", "动画变换"}, {"\\fad", "淡入淡出"}, {"\\fade", "高级淡入淡出"},
		{"\\clip", "裁剪"}, {"\\iclip", "反向裁剪"}, {"\\p", "Drawing 模式"}, {"\\pbo", "Drawing 基线"},
		{"\\r", "重置样式"}, {"\\q", "换行方式"},
		{"\\k", "Karaoke"}, {"\\K", "Karaoke 平滑"}, {"\\kf", "Karaoke 填充"}, {"\\ko", "Karaoke 描边"}, {"\\kt", "Karaoke 时间"}
	};
	auto it = labels.find(name);
	return it == labels.end() ? "ASS 标签" : std::string(it->second);
}

std::string category_for_tag(std::string_view name) {
	if (name == "\\an" || name == "\\pos" || name == "\\move" || name == "\\org") return "定位";
	if (name == "\\fn" || name == "\\fs" || name == "\\fs+" || name == "\\fs-" || name == "\\b" || name == "\\i" || name == "\\u" || name == "\\s" || name == "\\fsp" || name == "\\fe") return "字体";
	if (name == "\\fscx" || name == "\\fscy") return "缩放";
	if (name == "\\frx" || name == "\\fry" || name == "\\frz" || name == "\\fr" || name == "\\fax" || name == "\\fay") return "旋转/透视";
	if (name == "\\c" || name == "\\1c" || name == "\\2c" || name == "\\3c" || name == "\\4c" || name == "\\alpha" || name == "\\1a" || name == "\\2a" || name == "\\3a" || name == "\\4a") return "颜色";
	if (name == "\\bord" || name == "\\xbord" || name == "\\ybord" || name == "\\shad" || name == "\\xshad" || name == "\\yshad" || name == "\\blur" || name == "\\be") return "边缘";
	if (name == "\\t" || name == "\\fad" || name == "\\fade") return "动画";
	if (name == "\\clip" || name == "\\iclip") return "裁剪";
	if (name == "\\p" || name == "\\pbo") return "Drawing";
	if (name == "\\k" || name == "\\K" || name == "\\kf" || name == "\\ko" || name == "\\kt") return "Karaoke";
	return "高级";
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
					item.label = "原始 ASS";
					item.category = "高级";
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
				item.kind = ItemKind::Text; item.label = "正文"; item.category = "正文"; break;
			case PartKind::Comment:
				item.kind = ItemKind::Comment; item.label = "注释"; item.category = "高级"; break;
			case PartKind::Drawing:
				item.kind = ItemKind::Drawing; item.label = "ASS Drawing"; item.category = "Drawing"; break;
			case PartKind::Override:
				item.kind = ItemKind::Raw; item.label = "原始 ASS"; item.category = "高级"; break;
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
		{"定位", "原点/对齐", "an", "{\\an8}"}, {"定位", "位置", "pos", "{\\pos(0,0)}"},
		{"定位", "移动", "move", "{\\move(0,0,100,100)}"}, {"定位", "旋转原点", "org", "{\\org(0,0)}"},
		{"字体", "字体", "fn", "{\\fnArial}"}, {"字体", "字号", "fs", "{\\fs50}"},
		{"字体", "粗体", "b", "{\\b1}"}, {"字体", "斜体", "i", "{\\i1}"},
		{"字体", "下划线", "u", "{\\u1}"}, {"字体", "删除线", "s", "{\\s1}"},
		{"字体", "字距", "fsp", "{\\fsp0}"}, {"字体", "字体编码", "fe", "{\\fe1}"},
		{"缩放", "X 缩放", "fscx", "{\\fscx100}"}, {"缩放", "Y 缩放", "fscy", "{\\fscy100}"},
		{"旋转/透视", "X 轴旋转", "frx", "{\\frx0}"}, {"旋转/透视", "Y 轴旋转", "fry", "{\\fry0}"},
		{"旋转/透视", "Z 轴旋转", "frz", "{\\frz0}"}, {"旋转/透视", "X 轴倾斜", "fax", "{\\fax0}"},
		{"旋转/透视", "Y 轴倾斜", "fay", "{\\fay0}"},
		{"颜色", "正文颜色", "1c", "{\\1c&HFFFFFF&}"}, {"颜色", "次要颜色", "2c", "{\\2c&HFFFFFF&}"},
		{"颜色", "描边颜色", "3c", "{\\3c&H000000&}"}, {"颜色", "阴影颜色", "4c", "{\\4c&H000000&}"},
		{"颜色", "全局透明度", "alpha", "{\\alpha&H00&}"}, {"颜色", "正文透明度", "1a", "{\\1a&H00&}"},
		{"颜色", "次要透明度", "2a", "{\\2a&H00&}"}, {"颜色", "描边透明度", "3a", "{\\3a&H00&}"},
		{"颜色", "阴影透明度", "4a", "{\\4a&H00&}"},
		{"边缘", "描边宽度", "bord", "{\\bord2}"}, {"边缘", "X 描边宽度", "xbord", "{\\xbord2}"},
		{"边缘", "Y 描边宽度", "ybord", "{\\ybord2}"}, {"边缘", "阴影距离", "shad", "{\\shad2}"},
		{"边缘", "X 阴影距离", "xshad", "{\\xshad2}"}, {"边缘", "Y 阴影距离", "yshad", "{\\yshad2}"},
		{"边缘", "高斯模糊", "blur", "{\\blur1}"}, {"边缘", "边缘模糊", "be", "{\\be1}"},
		{"动画", "动画变换", "t", "{\\t(0,250,\\fscx100\\fscy100)}"},
		{"动画", "淡入淡出", "fad", "{\\fad(200,200)}"}, {"动画", "高级淡入淡出", "fade", "{\\fade(255,0,255,0,200,800,1000)}"},
		{"裁剪", "矩形裁剪", "clip", "{\\clip(0,0,100,100)}"}, {"裁剪", "矢量裁剪", "clip", "{\\clip(m 0 0 l 100 0 100 100 0 100)}"},
		{"裁剪", "反向裁剪", "iclip", "{\\iclip(0,0,100,100)}"},
		{"Drawing", "ASS Drawing", "p", "{\\p1}m 0 0 l 100 0 100 100 0 100{\\p0}"},
		{"Drawing", "Drawing 基线", "pbo", "{\\pbo0}"},
		{"Karaoke", "Karaoke", "k", "{\\k20}"}, {"Karaoke", "Karaoke 平滑", "K", "{\\K20}"},
		{"Karaoke", "Karaoke 填充", "kf", "{\\kf20}"}, {"Karaoke", "Karaoke 描边", "ko", "{\\ko20}"},
		{"Karaoke", "Karaoke 时间", "kt", "{\\kt20}"},
		{"高级", "重置样式", "r", "{\\r}"}, {"高级", "换行方式", "q", "{\\q2}"},
		{"高级", "硬换行", "N", "\\N"}, {"高级", "软换行", "n", "\\n"}, {"高级", "不换行空格", "h", "\\h"}
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

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_override_ast.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <utility>

namespace ass::ast {
namespace {

// Kept longest-first where names share a prefix. This list is only used to
// expose structured names; the parser never discards tags which are not here.
constexpr std::array<std::string_view, 59> known_tags = {{
	"\\alpha", "\\xbord", "\\ybord", "\\xshad", "\\yshad", "\\iclip",
	"\\fscx", "\\fscy", "\\fade", "\\move", "\\clip", "\\blur",
	"\\bord", "\\shad", "\\pbo", "\\fad", "\\fsp", "\\frx", "\\fry",
	"\\frz", "\\fax", "\\fay", "\\fn", "\\fs+", "\\fs-", "\\fs",
	"\\org", "\\pos", "\\1c", "\\2c", "\\3c", "\\4c", "\\1a",
	"\\2a", "\\3a", "\\4a", "\\fe", "\\kt", "\\ko", "\\kf",
	"\\be", "\\fr", "\\an", "\\c", "\\b", "\\i", "\\u", "\\s",
	"\\a", "\\k", "\\K", "\\q", "\\p", "\\r", "\\t", "\\j",
	"\\N", "\\n", "\\h"
}};

std::string_view trim_view(std::string_view value) {
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
		value.remove_prefix(1);
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
		value.remove_suffix(1);
	return value;
}

std::vector<std::string> split_arguments(std::string_view source) {
	std::vector<std::string> result;
	size_t begin = 0;
	int depth = 0;
	for (size_t i = 0; i < source.size(); ++i) {
		switch (source[i]) {
			case '(':
				++depth;
				break;
			case ')':
				if (depth > 0) --depth;
				break;
			case ',':
				if (depth == 0) {
					auto value = trim_view(source.substr(begin, i - begin));
					result.emplace_back(value);
					begin = i + 1;
				}
				break;
			default:
				break;
		}
	}
	auto value = trim_view(source.substr(begin));
	result.emplace_back(value);
	return result;
}

std::string normalize_tag_name(std::string_view name) {
	std::string result(name);
	if (result.empty() || result.front() != '\\') result.insert(result.begin(), '\\');
	return result;
}

std::string parse_unknown_name(std::string_view raw) {
	if (raw.empty() || raw.front() != '\\') return {};

	size_t i = 1;
	while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i]))) ++i;
	while (i < raw.size() && std::isalpha(static_cast<unsigned char>(raw[i]))) ++i;
	if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) ++i;
	if (i == 1) {
		while (i < raw.size() && raw[i] != '(' && raw[i] != ',') ++i;
	}
	return std::string(raw.substr(0, i));
}

size_t find_matching_parenthesis(std::string_view source, size_t open) {
	int depth = 0;
	for (size_t i = open; i < source.size(); ++i) {
		if (source[i] == '(') ++depth;
		else if (source[i] == ')' && --depth == 0) return i;
	}
	return std::string_view::npos;
}

bool tag_name_equal(std::string_view lhs, std::string_view rhs) {
	if (!lhs.empty() && lhs.front() != '\\') {
		return lhs.size() + 1 == rhs.size()
			&& rhs.front() == '\\'
			&& rhs.substr(1) == lhs;
	}
	return lhs == rhs;
}

int drawing_scale_from(OverrideBlock const& block, int current) {
	for (auto const& node : block.Nodes()) {
		if (node.Kind() != OverrideNodeKind::Tag || node.Name() != "\\p") continue;
		if (node.Arguments().empty()) continue;
		auto value = trim_view(node.Arguments().front());
		int parsed = 0;
		auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
		if (result.ec == std::errc()) current = std::max(parsed, 0);
	}
	return current;
}

} // namespace

OverrideNode::OverrideNode() = default;

OverrideNode::OverrideNode(OverrideNode const& other)
: kind_(other.kind_)
, original_(other.original_)
, name_(other.name_)
, arguments_(other.arguments_)
, trailing_(other.trailing_)
, transform_(other.transform_ ? std::make_unique<OverrideBlock>(*other.transform_) : nullptr)
, known_tag_(other.known_tag_)
, parenthesized_(other.parenthesized_)
, dirty_(other.dirty_)
{
}

OverrideNode::OverrideNode(OverrideNode&& other) noexcept = default;

OverrideNode& OverrideNode::operator=(OverrideNode const& other) {
	if (this == &other) return *this;
	kind_ = other.kind_;
	original_ = other.original_;
	name_ = other.name_;
	arguments_ = other.arguments_;
	trailing_ = other.trailing_;
	transform_ = other.transform_ ? std::make_unique<OverrideBlock>(*other.transform_) : nullptr;
	known_tag_ = other.known_tag_;
	parenthesized_ = other.parenthesized_;
	dirty_ = other.dirty_;
	return *this;
}

OverrideNode& OverrideNode::operator=(OverrideNode&& other) noexcept = default;
OverrideNode::~OverrideNode() = default;

OverrideNode OverrideNode::Raw(std::string raw) {
	OverrideNode node;
	node.kind_ = OverrideNodeKind::Raw;
	node.original_ = std::move(raw);
	return node;
}

OverrideNode OverrideNode::ParseTag(std::string raw) {
	OverrideNode node;
	node.kind_ = OverrideNodeKind::Tag;
	node.original_ = std::move(raw);

	for (auto known : known_tags) {
		if (!node.original_.starts_with(known)) continue;
		// A longer alphabetic word is an unknown tag rather than (for example)
		// "\\unknown" being interpreted as "\\u" with argument "nknown".
		// The two standard tags whose unparenthesized values commonly start with
		// letters are intentional exceptions.
		if (node.original_.size() > known.size()
		    && std::isalpha(static_cast<unsigned char>(node.original_[known.size()]))
		    && known != "\\fn" && known != "\\r")
			continue;
		node.name_ = known;
		node.known_tag_ = true;
		break;
	}
	if (node.name_.empty()) node.name_ = parse_unknown_name(node.original_);
	if (node.name_.empty()) {
		node.kind_ = OverrideNodeKind::Raw;
		return node;
	}

	auto suffix = std::string_view(node.original_).substr(node.name_.size());
	size_t non_space = 0;
	while (non_space < suffix.size()
	       && std::isspace(static_cast<unsigned char>(suffix[non_space])))
		++non_space;

	if (non_space < suffix.size() && suffix[non_space] == '(') {
		auto close = find_matching_parenthesis(suffix, non_space);
		if (close != std::string_view::npos) {
			node.parenthesized_ = true;
			node.arguments_ = split_arguments(suffix.substr(non_space + 1, close - non_space - 1));
			node.trailing_ = std::string(suffix.substr(close + 1));
		}
		else {
			node.arguments_.emplace_back(trim_view(suffix));
		}
	}
	else if (!suffix.empty()) {
		node.arguments_.emplace_back(trim_view(suffix));
	}

	node.ParseTransform();
	return node;
}

void OverrideNode::ParseTransform() {
	transform_.reset();
	if (name_ != "\\t" || arguments_.empty()) return;
	auto body = trim_view(arguments_.back());
	if (body.find('\\') == std::string_view::npos) return;
	transform_ = std::make_unique<OverrideBlock>(OverrideBlock::Parse(body, false));
}

bool OverrideNode::IsDirty() const noexcept {
	return dirty_ || (transform_ && transform_->IsDirty());
}

void OverrideNode::SetArgument(size_t index, std::string value) {
	if (kind_ != OverrideNodeKind::Tag) return;
	if (arguments_.size() <= index) arguments_.resize(index + 1);
	arguments_[index] = std::move(value);
	dirty_ = true;
	if (name_ == "\\t" && index + 1 == arguments_.size()) ParseTransform();
}

void OverrideNode::SetArguments(std::vector<std::string> values, bool parenthesized) {
	if (kind_ != OverrideNodeKind::Tag) return;
	arguments_ = std::move(values);
	parenthesized_ = parenthesized;
	trailing_.clear();
	dirty_ = true;
	ParseTransform();
}

void OverrideNode::SetName(std::string name) {
	if (kind_ != OverrideNodeKind::Tag) return;
	name_ = normalize_tag_name(name);
	known_tag_ = std::find(known_tags.begin(), known_tags.end(), name_) != known_tags.end();
	dirty_ = true;
	ParseTransform();
}

void OverrideNode::SetRaw(std::string raw) {
	kind_ = OverrideNodeKind::Raw;
	original_ = std::move(raw);
	name_.clear();
	arguments_.clear();
	trailing_.clear();
	transform_.reset();
	known_tag_ = false;
	parenthesized_ = false;
	dirty_ = true;
}

std::string OverrideNode::Serialize() const {
	if (!IsDirty()) return original_;
	if (kind_ == OverrideNodeKind::Raw) return original_;

	std::string result = name_;
	if (parenthesized_) result += '(';
	for (size_t i = 0; i < arguments_.size(); ++i) {
		if (i) result += ',';
		if (transform_ && i + 1 == arguments_.size()) result += transform_->Serialize();
		else result += arguments_[i];
	}
	if (parenthesized_) result += ')';
	result += trailing_;
	return result;
}

OverrideBlock::OverrideBlock() = default;
OverrideBlock::OverrideBlock(OverrideBlock const& other) = default;
OverrideBlock::OverrideBlock(OverrideBlock&& other) noexcept = default;
OverrideBlock& OverrideBlock::operator=(OverrideBlock const& other) = default;
OverrideBlock& OverrideBlock::operator=(OverrideBlock&& other) noexcept = default;
OverrideBlock::~OverrideBlock() = default;

OverrideBlock OverrideBlock::Parse(std::string_view source, bool braced) {
	OverrideBlock block;
	block.original_ = std::string(source);
	block.braced_ = braced;

	std::string_view body = source;
	if (braced && body.size() >= 2 && body.front() == '{' && body.back() == '}') {
		body.remove_prefix(1);
		body.remove_suffix(1);
	}

	size_t node_begin = 0;
	size_t tag_begin = std::string_view::npos;
	int depth = 0;
	for (size_t i = 0; i < body.size(); ++i) {
		if (body[i] == '(') {
			++depth;
			continue;
		}
		if (body[i] == ')') {
			if (depth > 0) --depth;
			continue;
		}
		if (body[i] != '\\' || depth != 0) continue;

		if (tag_begin != std::string_view::npos) {
			block.nodes_.push_back(OverrideNode::ParseTag(std::string(body.substr(tag_begin, i - tag_begin))));
		}
		else if (i > node_begin) {
			block.nodes_.push_back(OverrideNode::Raw(std::string(body.substr(node_begin, i - node_begin))));
		}
		tag_begin = i;
		node_begin = i;
	}

	if (tag_begin != std::string_view::npos)
		block.nodes_.push_back(OverrideNode::ParseTag(std::string(body.substr(tag_begin))));
	else if (!body.empty())
		block.nodes_.push_back(OverrideNode::Raw(std::string(body)));

	return block;
}

bool OverrideBlock::IsDirty() const noexcept {
	return structure_dirty_ || std::any_of(nodes_.begin(), nodes_.end(), [](OverrideNode const& node) {
		return node.IsDirty();
	});
}

OverrideNode* OverrideBlock::MutableNode(size_t index) noexcept {
	return index < nodes_.size() ? &nodes_[index] : nullptr;
}

OverrideNode& OverrideBlock::AppendTag(std::string source) {
	return InsertTag(nodes_.size(), std::move(source));
}

OverrideNode& OverrideBlock::InsertTag(size_t node_index, std::string source) {
	node_index = std::min(node_index, nodes_.size());
	if (source.empty() || source.front() != '\\') source.insert(source.begin(), '\\');
	auto node = OverrideNode::ParseTag(std::move(source));
	node.dirty_ = true;
	auto it = nodes_.insert(nodes_.begin() + node_index, std::move(node));
	structure_dirty_ = true;
	return *it;
}

void OverrideBlock::EraseNode(size_t node_index) {
	if (node_index >= nodes_.size()) return;
	nodes_.erase(nodes_.begin() + node_index);
	structure_dirty_ = true;
}

std::vector<OverrideNode*> OverrideBlock::FindTags(std::string_view name, bool recursive) {
	std::vector<OverrideNode*> result;
	for (auto& node : nodes_) {
		if (node.Kind() != OverrideNodeKind::Tag) continue;
		if (tag_name_equal(name, node.Name())) result.push_back(&node);
		if (recursive && node.Transform()) {
			auto nested = node.Transform()->FindTags(name, true);
			result.insert(result.end(), nested.begin(), nested.end());
		}
	}
	return result;
}

std::vector<OverrideNode const*> OverrideBlock::FindTags(std::string_view name, bool recursive) const {
	std::vector<OverrideNode const*> result;
	for (auto const& node : nodes_) {
		if (node.Kind() != OverrideNodeKind::Tag) continue;
		if (tag_name_equal(name, node.Name())) result.push_back(&node);
		if (recursive && node.Transform()) {
			auto nested = node.Transform()->FindTags(name, true);
			result.insert(result.end(), nested.begin(), nested.end());
		}
	}
	return result;
}

std::string OverrideBlock::Serialize() const {
	if (!IsDirty()) return original_;
	std::string result;
	if (braced_) result += '{';
	for (auto const& node : nodes_) result += node.Serialize();
	if (braced_) result += '}';
	return result;
}

Segment::Segment() = default;

Segment::Segment(Segment const& other)
: kind_(other.kind_)
, original_(other.original_)
, text_(other.text_)
, block_(other.block_ ? std::make_unique<OverrideBlock>(*other.block_) : nullptr)
, drawing_scale_(other.drawing_scale_)
, dirty_(other.dirty_)
{
}

Segment::Segment(Segment&& other) noexcept = default;

Segment& Segment::operator=(Segment const& other) {
	if (this == &other) return *this;
	kind_ = other.kind_;
	original_ = other.original_;
	text_ = other.text_;
	block_ = other.block_ ? std::make_unique<OverrideBlock>(*other.block_) : nullptr;
	drawing_scale_ = other.drawing_scale_;
	dirty_ = other.dirty_;
	return *this;
}

Segment& Segment::operator=(Segment&& other) noexcept = default;
Segment::~Segment() = default;

bool Segment::IsDirty() const noexcept {
	return dirty_ || (block_ && block_->IsDirty());
}

void Segment::SetText(std::string text) {
	if (kind_ == SegmentKind::Override) return;
	text_ = std::move(text);
	dirty_ = true;
}

std::string Segment::Serialize() const {
	if (!IsDirty()) return original_;
	if (kind_ == SegmentKind::Override) return block_ ? block_->Serialize() : original_;
	if (kind_ == SegmentKind::Comment) return '{' + text_ + '}';
	return text_;
}

Document Document::Parse(std::string_view source) {
	Document document;
	document.original_ = std::string(source);
	int drawing_scale = 0;

	for (size_t current = 0; current < source.size();) {
		if (source[current] == '{') {
			auto close = source.find('}', current + 1);
			if (close != std::string_view::npos) {
				auto raw = source.substr(current, close - current + 1);
				auto body = raw.substr(1, raw.size() - 2);
				Segment segment;
				segment.original_ = std::string(raw);
				if (body.find('\\') == std::string_view::npos) {
					segment.kind_ = SegmentKind::Comment;
					segment.text_ = std::string(body);
				}
				else {
					segment.kind_ = SegmentKind::Override;
					segment.block_ = std::make_unique<OverrideBlock>(OverrideBlock::Parse(raw));
					drawing_scale = drawing_scale_from(*segment.block_, drawing_scale);
				}
				document.segments_.push_back(std::move(segment));
				current = close + 1;
				continue;
			}
		}

		auto next = source.find('{', current + 1);
		if (next == std::string_view::npos) next = source.size();
		Segment segment;
		segment.kind_ = drawing_scale > 0 ? SegmentKind::Drawing : SegmentKind::Text;
		segment.original_ = std::string(source.substr(current, next - current));
		segment.text_ = segment.original_;
		segment.drawing_scale_ = drawing_scale;
		document.segments_.push_back(std::move(segment));
		current = next;
	}

	return document;
}

bool Document::IsDirty() const noexcept {
	return std::any_of(segments_.begin(), segments_.end(), [](Segment const& segment) {
		return segment.IsDirty();
	});
}

Segment* Document::MutableSegment(size_t index) noexcept {
	return index < segments_.size() ? &segments_[index] : nullptr;
}

std::vector<OverrideNode*> Document::FindTags(std::string_view name, bool recursive) {
	std::vector<OverrideNode*> result;
	for (auto& segment : segments_) {
		if (!segment.Block()) continue;
		auto found = segment.Block()->FindTags(name, recursive);
		result.insert(result.end(), found.begin(), found.end());
	}
	return result;
}

std::vector<OverrideNode const*> Document::FindTags(std::string_view name, bool recursive) const {
	std::vector<OverrideNode const*> result;
	for (auto const& segment : segments_) {
		if (!segment.Block()) continue;
		auto found = segment.Block()->FindTags(name, recursive);
		result.insert(result.end(), found.begin(), found.end());
	}
	return result;
}

bool Document::SetFirstArgument(std::string_view name, size_t argument, std::string value,
	                             bool recursive) {
	auto found = FindTags(name, recursive);
	if (found.empty()) return false;
	found.front()->SetArgument(argument, std::move(value));
	return true;
}

std::string Document::Serialize() const {
	if (!IsDirty()) return original_;
	std::string result;
	for (auto const& segment : segments_) result += segment.Serialize();
	return result;
}

} // namespace ass::ast

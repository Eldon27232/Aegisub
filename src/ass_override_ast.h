// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ass::ast {

/// The role of a top-level piece of an ASS dialogue's Text field.
enum class SegmentKind {
	Text,
	Comment,
	Override,
	Drawing
};

/// The role of an item inside an override block.
enum class OverrideNodeKind {
	Raw,
	Tag
};

class OverrideBlock;

/// A lossless node within an override block.
///
/// Unedited nodes serialize from Original(), so unusual whitespace, unknown
/// tags and even malformed input survive a parse/save cycle byte-for-byte.
/// Edited tags serialize from their structured fields instead.
class OverrideNode final {
public:
	OverrideNode();
	OverrideNode(OverrideNode const& other);
	OverrideNode(OverrideNode&& other) noexcept;
	OverrideNode& operator=(OverrideNode const& other);
	OverrideNode& operator=(OverrideNode&& other) noexcept;
	~OverrideNode();

	OverrideNodeKind Kind() const noexcept { return kind_; }
	std::string const& Original() const noexcept { return original_; }
	std::string const& Name() const noexcept { return name_; }
	std::vector<std::string> const& Arguments() const noexcept { return arguments_; }
	bool IsKnownTag() const noexcept { return known_tag_; }
	bool UsesParentheses() const noexcept { return parenthesized_; }
	bool IsDirty() const noexcept;

	/// A parsed nested transform body, or nullptr for non-transform tags.
	OverrideBlock const* Transform() const noexcept { return transform_.get(); }
	OverrideBlock* Transform() noexcept { return transform_.get(); }

	/// Change one argument, growing the argument list when necessary.
	void SetArgument(size_t index, std::string value);
	/// Replace all arguments and choose their serialized form.
	void SetArguments(std::vector<std::string> values, bool parenthesized);
	/// Rename a tag. The leading backslash is added when omitted.
	void SetName(std::string name);
	/// Replace an uninterpreted raw item.
	void SetRaw(std::string raw);

	std::string Serialize() const;
	OverrideNode Clone() const { return *this; }

private:
	friend class OverrideBlock;

	OverrideNodeKind kind_ = OverrideNodeKind::Raw;
	std::string original_;
	std::string name_;
	std::vector<std::string> arguments_;
	std::string trailing_;
	std::unique_ptr<OverrideBlock> transform_;
	bool known_tag_ = false;
	bool parenthesized_ = false;
	bool dirty_ = false;

	static OverrideNode ParseTag(std::string raw);
	static OverrideNode Raw(std::string raw);
	void ParseTransform();
};

/// A sequence of lossless raw and tag nodes. Top-level blocks include braces;
/// nested transform bodies do not.
class OverrideBlock final {
public:
	OverrideBlock();
	OverrideBlock(OverrideBlock const& other);
	OverrideBlock(OverrideBlock&& other) noexcept;
	OverrideBlock& operator=(OverrideBlock const& other);
	OverrideBlock& operator=(OverrideBlock&& other) noexcept;
	~OverrideBlock();

	static OverrideBlock Parse(std::string_view source, bool braced = true);

	std::string const& Original() const noexcept { return original_; }
	bool HasBraces() const noexcept { return braced_; }
	bool IsDirty() const noexcept;
	std::vector<OverrideNode> const& Nodes() const noexcept { return nodes_; }
	OverrideNode* MutableNode(size_t index) noexcept;

	/// Append a tag from standard ASS source such as "\\bord2".
	OverrideNode& AppendTag(std::string source);
	/// Insert a tag before node_index (or append when node_index is at the end).
	OverrideNode& InsertTag(size_t node_index, std::string source);
	void EraseNode(size_t node_index);

	std::vector<OverrideNode*> FindTags(std::string_view name, bool recursive = true);
	std::vector<OverrideNode const*> FindTags(std::string_view name, bool recursive = true) const;

	std::string Serialize() const;
	OverrideBlock Clone() const { return *this; }

private:
	std::string original_;
	std::vector<OverrideNode> nodes_;
	bool braced_ = true;
	bool structure_dirty_ = false;
};

/// A top-level dialogue text segment. Override segments own an OverrideBlock;
/// all other segment types expose their payload through Text().
class Segment final {
public:
	Segment();
	Segment(Segment const& other);
	Segment(Segment&& other) noexcept;
	Segment& operator=(Segment const& other);
	Segment& operator=(Segment&& other) noexcept;
	~Segment();

	SegmentKind Kind() const noexcept { return kind_; }
	std::string const& Original() const noexcept { return original_; }
	std::string const& Text() const noexcept { return text_; }
	int DrawingScale() const noexcept { return drawing_scale_; }
	bool IsDirty() const noexcept;

	OverrideBlock const* Block() const noexcept { return block_.get(); }
	OverrideBlock* Block() noexcept { return block_.get(); }

	void SetText(std::string text);
	std::string Serialize() const;
	Segment Clone() const { return *this; }

private:
	friend class Document;

	SegmentKind kind_ = SegmentKind::Text;
	std::string original_;
	std::string text_;
	std::unique_ptr<OverrideBlock> block_;
	int drawing_scale_ = 0;
	bool dirty_ = false;
};

/// Lossless, editable representation of an ASS dialogue Text field.
class Document final {
public:
	Document() = default;
	Document(Document const&) = default;
	Document(Document&&) noexcept = default;
	Document& operator=(Document const&) = default;
	Document& operator=(Document&&) noexcept = default;

	static Document Parse(std::string_view source);

	std::string const& Original() const noexcept { return original_; }
	std::vector<Segment> const& Segments() const noexcept { return segments_; }
	Segment* MutableSegment(size_t index) noexcept;
	bool IsDirty() const noexcept;

	std::vector<OverrideNode*> FindTags(std::string_view name, bool recursive = true);
	std::vector<OverrideNode const*> FindTags(std::string_view name, bool recursive = true) const;

	/// Set the first matching tag's argument. Returns false when absent.
	bool SetFirstArgument(std::string_view name, size_t argument, std::string value,
	                      bool recursive = true);

	std::string Serialize() const;
	Document Clone() const { return *this; }

private:
	std::string original_;
	std::vector<Segment> segments_;
};

} // namespace ass::ast

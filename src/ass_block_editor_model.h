// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libaegisub/color.h>

namespace ass::blocks {

/// Extradata key used to persist which portions of a dialogue line are owned
/// by the block editor. The dialogue Text itself always remains standard ASS.
inline constexpr std::string_view kOriginExtradataKey = "aegisub.ass-block-origins";

enum class Origin {
	Manual,
	Gui,
	Raw
};

enum class ItemKind {
	Tag,
	Text,
	Comment,
	Drawing,
	Raw
};

struct Item {
	ItemKind kind = ItemKind::Raw;
	Origin origin = Origin::Raw;
	std::string label;
	std::string category;
	std::string source;
};

struct Feature {
	std::string category;
	std::string name;
	std::string tag;
	std::string source;
};

/// Structured block view over an ASS dialogue Text field.
///
/// The model uses the lossless ASS AST to find block boundaries. Untouched
/// source is returned byte-for-byte; edits rebuild only the affected block.
class Model final {
public:
	/// Parse a direct edit of the complete ASS source. Recognized override
	/// blocks are explicitly taken over by the GUI; plain text stays manual.
	void SetSource(std::string source);
	/// Parse GUI-owned structure around one opaque manual text span. Invalid
	/// bounds fall back to treating the complete source as manual.
	void SetSourceWithManualSpan(std::string source, size_t offset, size_t length);
	/// Restore source plus persisted ownership. Missing, stale or malformed
	/// metadata is deliberately treated as one opaque manual text item.
	void SetStoredSource(std::string source, std::string_view origin_metadata);
	std::string Serialize() const;
	/// Versioned, source-bound ownership runs suitable for Extradata.
	std::string OriginMetadata() const;
	std::vector<Item> const& Items() const noexcept { return items_; }

	std::string Copy(std::vector<size_t> selection) const;
	std::string Cut(std::vector<size_t> selection);
	bool Delete(std::vector<size_t> selection);
	bool Paste(std::optional<size_t> after, std::string_view source);
	bool Replace(size_t item, std::string_view source);
	/// Replace a plain-text item without parsing ASS-looking input.
	bool ReplaceManual(size_t item, std::string_view source);
	bool Insert(std::optional<size_t> after, Feature const& feature);

	/// Colour tags use the same picker at top level and inside transforms.
	/// Each nested_path entry is an index in the current transform's Nodes().
	agi::Color GetColour(size_t item, std::vector<size_t> nested_path = {}) const;
	bool SetColour(size_t item, std::vector<size_t> nested_path, agi::Color colour);

	static std::vector<Feature> const& Features();
	static std::vector<Feature> SearchFeatures(std::string_view query);

private:
	enum class PartKind { Text, Comment, Override, Drawing, Raw };
	struct Node {
		std::string source;
		std::string name;
		bool known = false;
		bool tag = false;
	};
	struct Part {
		PartKind kind = PartKind::Text;
		Origin origin = Origin::Manual;
		std::string original;
		std::vector<Node> nodes;
		bool dirty = false;
	};
	struct Location {
		size_t part = 0;
		size_t node = 0;
		bool is_node = false;
	};

	std::string original_;
	std::vector<Part> parts_;
	std::vector<Item> items_;
	std::vector<Location> locations_;
	bool dirty_ = false;

	static std::vector<Part> ParseParts(std::string_view source);
	static std::string SerializePart(Part const& part);
	void RebuildItems();
};

} // namespace ass::blocks

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ass::templates {

enum class Scope { Global, Project };

inline constexpr std::string_view BodyPlaceholder = "{{正文}}";

struct Entry {
	std::string id;
	std::string name;
	std::string category;
	std::string text;
	Scope scope = Scope::Global;
};

struct Parameter {
	std::string id;
	std::string label;
	std::string value;
};

/// Encode templates as a single ASS-field-safe line.
std::string Encode(std::vector<Entry> const& entries, Scope scope);
/// Decode templates without discarding the rest when one record is malformed.
std::vector<Entry> Decode(std::string_view source, Scope scope);

/// Convert a dialogue Text field into reusable ASS structure with one body
/// placeholder. Existing templates which already contain the placeholder are
/// returned unchanged.
std::string MakeStructure(std::string_view source);
/// Read the visible body while discarding the line's existing ASS structure.
std::string ExtractBody(std::string_view source);
/// Fill a reusable structure with a new body. Legacy full-line templates are
/// converted to reusable structure before being applied.
std::string ApplyStructure(std::string_view structure, std::string_view body);

/// Find useful values which can be changed when applying a template.
std::vector<Parameter> ExtractParameters(std::string_view source);
/// Apply changed parameter values through the structured ASS AST.
std::string ApplyParameters(std::string_view source,
	std::unordered_map<std::string, std::string> const& values);

} // namespace ass::templates

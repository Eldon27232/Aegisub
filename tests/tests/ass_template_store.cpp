// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_template_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_map>

using ass::templates::Entry;
using ass::templates::Scope;

TEST(ass_template_store, global_and_project_records_roundtrip_without_touching_ass_text) {
	std::vector<Entry> entries = {
		{"global:1", "弹幕|气泡", "常用:动画", "{\\t(0,250,\\fscx100)}\\n中文#1", Scope::Global},
		{"project:1", "画面标题", "Project", "{\\pos(500,500)\\3c&HA87FFF&}正文", Scope::Project}
	};

	auto global = ass::templates::Decode(ass::templates::Encode(entries, Scope::Global), Scope::Global);
	ASSERT_EQ(1u, global.size());
	EXPECT_EQ(entries[0].id, global[0].id);
	EXPECT_EQ(entries[0].name, global[0].name);
	EXPECT_EQ(entries[0].category, global[0].category);
	EXPECT_EQ(entries[0].text, global[0].text);

	auto project = ass::templates::Decode(ass::templates::Encode(entries, Scope::Project), Scope::Project);
	ASSERT_EQ(1u, project.size());
	EXPECT_EQ(entries[1].text, project[0].text);
}

TEST(ass_template_store, malformed_record_does_not_hide_later_valid_templates) {
	auto entries = ass::templates::Decode("v1|broken|id:name:category:{\\an8}正文", Scope::Project);
	ASSERT_EQ(1u, entries.size());
	EXPECT_EQ("name", entries[0].name);
	EXPECT_EQ("{\\an8}正文", entries[0].text);
}

TEST(ass_template_store, extracts_and_applies_common_parameters_through_ast) {
	std::string source =
		"{\\an8\\fs50\\fscx0\\fscy0\\t(0,250,\\fscx100\\fscy100)"
		"\\3c&HA87FFF&\\pos(500,500)\\vendor(keep)}\\n中文";
	auto parameters = ass::templates::ExtractParameters(source);
	EXPECT_TRUE(std::any_of(parameters.begin(), parameters.end(), [](auto const& parameter) {
		return parameter.id == "tag:\\pos:0:0" && parameter.label == "Position X" && parameter.value == "500";
	}));
	EXPECT_TRUE(std::any_of(parameters.begin(), parameters.end(), [](auto const& parameter) {
		return parameter.id == "tag:\\3c:0:0" && parameter.label == "Outline color";
	}));
	EXPECT_TRUE(std::any_of(parameters.begin(), parameters.end(), [](auto const& parameter) {
		return parameter.id == "text:0" && parameter.value == "\\n中文";
	}));

	std::unordered_map<std::string, std::string> values = {
		{"tag:\\fs:0:0", "60"},
		{"tag:\\t:0:0", "10"},
		{"tag:\\t:0:1", "300"},
		{"tag:\\fscx:1:0", "120"},
		{"tag:\\3c:0:0", "&H112233&"},
		{"tag:\\pos:0:0", "640"},
		{"text:0", "\\n替换正文"}
	};
	auto applied = ass::templates::ApplyParameters(source, values);
	EXPECT_EQ(
		"{\\an8\\fs60\\fscx0\\fscy0\\t(10,300,\\fscx120\\fscy100)"
		"\\3c&H112233&\\pos(640,500)\\vendor(keep)}\\n替换正文",
		applied);
}

TEST(ass_template_store, saving_a_line_extracts_structure_and_replaces_its_body) {
	std::string source =
		"{\\an8\\fs50\\fscx0\\fscy0\\t(0,250,\\fscx100\\fscy100)"
		"\\3c&H0762ED&\\pos(500,500)}\\n辉夜酱太可爱了（4,000点）";

	auto structure = ass::templates::MakeStructure(source);
	EXPECT_EQ(
		"{\\an8\\fs50\\fscx0\\fscy0\\t(0,250,\\fscx100\\fscy100)"
		"\\3c&H0762ED&\\pos(500,500)}\\n{{正文}}",
		structure);
	EXPECT_EQ(structure, ass::templates::MakeStructure(structure));
	EXPECT_EQ(std::string::npos, structure.find("辉夜酱太可爱了"));

	auto parameters = ass::templates::ExtractParameters(structure);
	EXPECT_TRUE(std::any_of(parameters.begin(), parameters.end(), [](auto const& parameter) {
		return parameter.id == "tag:\\pos:0:0";
	}));
	EXPECT_TRUE(std::none_of(parameters.begin(), parameters.end(), [](auto const& parameter) {
		return parameter.id.rfind("text:", 0) == 0;
	}));
}

TEST(ass_template_store, applying_structure_keeps_the_target_line_body) {
	std::string source = "{\\an8\\fs50\\pos(500,500)}\\n辉夜酱太可爱了（4,000点）";
	std::string target = "{\\i1}让我们厮守终生吧！（4,000点）";
	auto body = ass::templates::ExtractBody(target);

	EXPECT_EQ("让我们厮守终生吧！（4,000点）", body);
	EXPECT_EQ(
		"{\\an8\\fs50\\pos(500,500)}\\n让我们厮守终生吧！（4,000点）",
		ass::templates::ApplyStructure(ass::templates::MakeStructure(source), body));
	// Templates saved by the previous implementation are migrated on use.
	EXPECT_EQ(
		"{\\an8\\fs50\\pos(500,500)}\\n让我们厮守终生吧！（4,000点）",
		ass::templates::ApplyStructure(source, body));
}

TEST(ass_template_store, applying_structure_to_a_new_line_does_not_copy_saved_body) {
	std::string legacy = "{\\an8\\pos(500,500)}\\n辉夜酱太可爱了（4,000点）";

	EXPECT_EQ("{\\an8\\pos(500,500)}\\n", ass::templates::ApplyStructure(legacy, {}));
}

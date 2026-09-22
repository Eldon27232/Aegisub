// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_block_editor_model.h"

#include <gtest/gtest.h>

#include <algorithm>

using ass::blocks::ItemKind;
using ass::blocks::Model;

TEST(ass_block_editor_model, untouched_source_is_byte_exact_and_unknown_content_is_raw) {
	std::string source =
		"lead{note}{\\an8\\t(0,250,\\frz45\\bord3)\\vendor(foo,(bar,baz))}"
		"{\\p1}m 0 0 l 10 10{\\p0}tail";
	Model model;
	model.SetSource(source);

	EXPECT_EQ(source, model.Serialize());
	auto const& items = model.Items();
	ASSERT_GE(items.size(), 9u);
	EXPECT_EQ(ItemKind::Comment, items[1].kind);
	EXPECT_EQ(ItemKind::Tag, items[2].kind);
	EXPECT_EQ("Anchor/Alignment", items[2].label);
	EXPECT_EQ(ItemKind::Raw, items[4].kind);
	EXPECT_EQ("\\vendor(foo,(bar,baz))", items[4].source);
	EXPECT_TRUE(std::any_of(items.begin(), items.end(), [](auto const& item) {
		return item.kind == ItemKind::Drawing;
	}));
}

TEST(ass_block_editor_model, feature_search_accepts_localized_names_and_ass_tags) {
	auto alignment = Model::SearchFeatures("an");
	ASSERT_FALSE(alignment.empty());
	EXPECT_TRUE(std::any_of(alignment.begin(), alignment.end(), [](auto const& feature) {
		return feature.tag == "an" && feature.name == "Anchor/Alignment";
	}));

	auto rotation = Model::SearchFeatures("Z rotation");
	ASSERT_FALSE(rotation.empty());
	EXPECT_TRUE(std::any_of(rotation.begin(), rotation.end(), [](auto const& feature) {
		return feature.tag == "frz";
	}));
}

TEST(ass_block_editor_model, creates_deletes_and_replaces_blocks_without_reordering_neighbors) {
	Model model;
	model.SetSource("{\\an8\\b1}text");
	auto const& features = Model::Features();
	auto rotation = std::find_if(features.begin(), features.end(), [](auto const& feature) {
		return feature.tag == "frz";
	});
	ASSERT_NE(features.end(), rotation);
	ASSERT_TRUE(model.Insert(0, *rotation));
	EXPECT_EQ("{\\an8\\frz0\\b1}text", model.Serialize());

	ASSERT_TRUE(model.Replace(1, "\\frz90"));
	EXPECT_EQ("{\\an8\\frz90\\b1}text", model.Serialize());
	ASSERT_TRUE(model.Delete({2}));
	EXPECT_EQ("{\\an8\\frz90}text", model.Serialize());
}

TEST(ass_block_editor_model, copy_cut_and_paste_preserve_standard_ass_fragments) {
	Model model;
	model.SetSource("{\\an8\\b1\\3c&HA87FFF&}直播{\\i1}中");

	auto copied = model.Copy({0, 1});
	EXPECT_EQ("{\\an8\\b1}", copied);
	ASSERT_TRUE(model.Paste(4, copied));
	EXPECT_EQ("{\\an8\\b1\\3c&HA87FFF&}直播{\\i1\\an8\\b1}中", model.Serialize());

	auto cut = model.Cut({2});
	EXPECT_EQ("{\\3c&HA87FFF&}", cut);
	EXPECT_EQ("{\\an8\\b1}直播{\\i1\\an8\\b1}中", model.Serialize());
}

TEST(ass_block_editor_model, reparses_direct_source_edits_into_blocks) {
	Model model;
	model.SetSource("plain");
	ASSERT_EQ(1u, model.Items().size());

	std::string edited = "{\\pos(844,306.67)\\unknown(x)}正文\\N下一行";
	model.SetSource(edited);
	EXPECT_EQ(edited, model.Serialize());
	ASSERT_GE(model.Items().size(), 3u);
	EXPECT_EQ("Position", model.Items()[0].label);
	EXPECT_EQ(ItemKind::Raw, model.Items()[1].kind);
}

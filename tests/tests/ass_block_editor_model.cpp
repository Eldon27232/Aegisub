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

TEST(ass_block_editor_model, live_colour_changes_reuse_channel_alpha) {
	Model model;
	model.SetSource("{\\1c&HFFFFFF&}text");
	EXPECT_EQ(agi::Color(255, 255, 255, 0), model.GetColour(0));
	ASSERT_TRUE(model.SetColour(0, {}, agi::Color(255, 127, 168, 128)));
	EXPECT_EQ("{\\1c&HA87FFF&\\1a&H80&}text", model.Serialize());
	ASSERT_TRUE(model.SetColour(0, {}, agi::Color(16, 32, 48, 64)));
	EXPECT_EQ("{\\1c&H302010&\\1a&H40&}text", model.Serialize());
	EXPECT_EQ(agi::Color(16, 32, 48, 64), model.GetColour(0));
	EXPECT_EQ(3U, model.Items().size());
}

TEST(ass_block_editor_model, channel_opacity_is_effective_after_later_global_alpha) {
	Model model;
	model.SetSource("{\\alpha&H40&\\3c&H010203&\\3a&H80&\\alpha&H20&\\2a&H90&}text");
	EXPECT_EQ(agi::Color(3, 2, 1, 0x20), model.GetColour(1));
	ASSERT_TRUE(model.SetColour(1, {}, agi::Color(255, 127, 168, 0xAA)));
	EXPECT_EQ("{\\alpha&H40&\\3c&HA87FFF&\\3a&H80&\\alpha&H20&\\2a&H90&\\3a&HAA&}text", model.Serialize());
	EXPECT_EQ(agi::Color(255, 127, 168, 0xAA), model.GetColour(1));
	ASSERT_TRUE(model.SetColour(1, {}, agi::Color(255, 127, 168, 0x88)));
	EXPECT_EQ("{\\alpha&H40&\\3c&HA87FFF&\\3a&H80&\\alpha&H20&\\2a&H90&\\3a&H88&}text", model.Serialize());
}

TEST(ass_block_editor_model, primary_alias_updates_existing_effective_alpha_only) {
	Model model;
	model.SetSource("{\\alpha&HFF&\\c&H102030&\\1a&H40&\\4a&H99&}text");
	EXPECT_EQ(agi::Color(0x30, 0x20, 0x10, 0x40), model.GetColour(1));
	ASSERT_TRUE(model.SetColour(1, {}, agi::Color(255, 0, 0, 0x55)));
	EXPECT_EQ("{\\alpha&HFF&\\c&H0000FF&\\1a&H55&\\4a&H99&}text", model.Serialize());
}

TEST(ass_block_editor_model, transform_colour_preserves_animation_neighbors_and_raw) {
	Model model;
	model.SetSource("head{\\alpha&H20&\\t(0,250,\\3c&HFFFFFF&\\bord3\\vendor(foo,(bar,baz))\\alpha&H70&\\4a&H22&)\\1c&H112233&}tail{note}");
	EXPECT_EQ(agi::Color(255, 255, 255, 0x70), model.GetColour(2, {0}));
	ASSERT_TRUE(model.SetColour(2, {0}, agi::Color(255, 127, 168, 0x80)));
	EXPECT_EQ("head{\\alpha&H20&\\t(0,250,\\3c&HA87FFF&\\bord3\\vendor(foo,(bar,baz))\\alpha&H70&\\4a&H22&\\3a&H80&)\\1c&H112233&}tail{note}", model.Serialize());
	EXPECT_EQ(agi::Color(255, 127, 168, 0x80), model.GetColour(2, {0}));
	EXPECT_EQ(agi::Color(0x33, 0x22, 0x11, 0x20), model.GetColour(3));
}

TEST(ass_block_editor_model, nested_transform_path_changes_only_its_own_colour_scope) {
	Model model;
	model.SetSource("{\\t(0,100,\\t(0,50,\\1c&HFF&\\1a&H20&)\\bord2)\\vendor(raw)}text");
	EXPECT_EQ(agi::Color(255, 0, 0, 0x20), model.GetColour(0, {0, 0}));
	ASSERT_TRUE(model.SetColour(0, {0, 0}, agi::Color(0, 255, 255, 0x10)));
	EXPECT_EQ("{\\t(0,100,\\t(0,50,\\1c&HFFFF00&\\1a&H10&)\\bord2)\\vendor(raw)}text", model.Serialize());
}

TEST(ass_block_editor_model, invalid_colour_targets_leave_the_source_untouched) {
	Model model;
	std::string source = "{\\pos(1,2)\\vendor(raw)}text";
	model.SetSource(source);
	EXPECT_EQ(agi::Color(255, 255, 255, 0), model.GetColour(0));
	EXPECT_FALSE(model.SetColour(0, {}, agi::Color(255, 0, 0)));
	EXPECT_FALSE(model.SetColour(0, {1}, agi::Color(255, 0, 0)));
	EXPECT_FALSE(model.SetColour(99, {}, agi::Color(255, 0, 0)));
	EXPECT_EQ(source, model.Serialize());
}

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_block_editor_model.h"
#include "ass_override_ast.h"

#include <gtest/gtest.h>

#include <algorithm>

using ass::blocks::ItemKind;
using ass::blocks::Model;
using ass::blocks::Origin;

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
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(Origin::Raw, model.Items()[1].origin);
	EXPECT_EQ(Origin::Manual, model.Items()[2].origin);
}

TEST(ass_block_editor_model, unmarked_existing_source_takes_over_recognized_ass_tags) {
	std::string source = "{\\pos(114,514)\\vendor(x)}正文\\n\\N";
	Model model;
	model.SetStoredSource(source, {});

	ASSERT_EQ(3u, model.Items().size());
	EXPECT_EQ(ItemKind::Tag, model.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ("\\pos(114,514)", model.Items()[0].source);
	EXPECT_EQ(ItemKind::Raw, model.Items()[1].kind);
	EXPECT_EQ(Origin::Raw, model.Items()[1].origin);
	EXPECT_EQ(ItemKind::Text, model.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[2].origin);
	EXPECT_EQ("正文\\n\\N", model.Items()[2].source);
	EXPECT_EQ(source, model.Serialize());

	Model reopened;
	reopened.SetStoredSource(source, model.OriginMetadata());
	EXPECT_EQ(source, reopened.Serialize());
	ASSERT_EQ(3u, reopened.Items().size());
	EXPECT_EQ(Origin::Gui, reopened.Items()[0].origin);
	EXPECT_EQ(Origin::Raw, reopened.Items()[1].origin);
	EXPECT_EQ(Origin::Manual, reopened.Items()[2].origin);
}

TEST(ass_block_editor_model, marked_manual_source_stays_opaque_after_reopening) {
	std::string source = "手写 \\n \\N {\\pos(114,514)} {invalid";
	Model authored;
	authored.SetStoredSource("", {});
	ASSERT_TRUE(authored.ReplaceManual(0, source));

	Model model;
	model.SetStoredSource(source, authored.OriginMetadata());
	ASSERT_EQ(1u, model.Items().size());
	EXPECT_EQ(ItemKind::Text, model.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[0].origin);
	EXPECT_EQ(source, model.Items()[0].source);
	EXPECT_EQ(source, model.Serialize());

	std::string edited = "\\N{\\pos(500,500)}\\n";
	ASSERT_TRUE(model.ReplaceManual(0, edited));
	ASSERT_EQ(1u, model.Items().size());
	EXPECT_EQ(ItemKind::Text, model.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[0].origin);
	EXPECT_EQ(edited, model.Serialize());
}

TEST(ass_block_editor_model, empty_stored_source_has_an_editable_manual_item) {
	Model model;
	model.SetStoredSource("", {});
	ASSERT_EQ(1u, model.Items().size());
	EXPECT_EQ(ItemKind::Text, model.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[0].origin);
	EXPECT_EQ("", model.Serialize());
	EXPECT_TRUE(model.ReplaceManual(0, "正文"));
	EXPECT_EQ("正文", model.Serialize());
}

TEST(ass_block_editor_model, default_gui_insert_precedes_and_preserves_empty_manual_body) {
	Model model;
	model.SetStoredSource("", {});
	auto const& features = Model::Features();
	auto bold = std::find_if(features.begin(), features.end(), [](auto const& feature) {
		return feature.tag == "b";
	});
	ASSERT_NE(features.end(), bold);
	ASSERT_TRUE(model.Insert(std::nullopt, *bold));
	EXPECT_EQ("{\\b1}", model.Serialize());
	ASSERT_EQ(2u, model.Items().size());
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(Origin::Manual, model.Items()[1].origin);
	EXPECT_EQ("", model.Items()[1].source);
	EXPECT_NE(std::string::npos, model.OriginMetadata().find(",M0"));

	Model restored;
	restored.SetStoredSource(model.Serialize(), model.OriginMetadata());
	ASSERT_EQ(2u, restored.Items().size());
	EXPECT_EQ(ItemKind::Tag, restored.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, restored.Items()[0].origin);
	EXPECT_EQ(ItemKind::Text, restored.Items()[1].kind);
	EXPECT_EQ(Origin::Manual, restored.Items()[1].origin);
	EXPECT_EQ("", restored.Items()[1].source);
	ASSERT_TRUE(restored.ReplaceManual(1, "正文"));
	EXPECT_EQ("{\\b1}正文", restored.Serialize());
}

TEST(ass_block_editor_model, direct_override_source_appends_persisted_manual_body_placeholder) {
	Model model;
	model.SetSource("{\\an8\\fs50}");
	EXPECT_EQ("{\\an8\\fs50}", model.Serialize());
	ASSERT_EQ(3u, model.Items().size());
	EXPECT_EQ(ItemKind::Tag, model.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(ItemKind::Tag, model.Items()[1].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[1].origin);
	EXPECT_EQ(ItemKind::Text, model.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[2].origin);
	EXPECT_EQ("", model.Items()[2].source);

	auto metadata = model.OriginMetadata();
	EXPECT_NE(std::string::npos, metadata.find(",M0"));
	Model restored;
	restored.SetStoredSource(model.Serialize(), metadata);
	ASSERT_EQ(3u, restored.Items().size());
	EXPECT_EQ(ItemKind::Text, restored.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, restored.Items()[2].origin);
	EXPECT_EQ("", restored.Items()[2].source);
}

TEST(ass_block_editor_model, structured_source_keeps_ass_looking_body_manual) {
	std::string prefix = "{\\an8}{note}";
	std::string body = "手写{\\pos(114,514)}\\N";
	std::string suffix = "{\\i1}";
	std::string source = prefix + body + suffix;
	Model model;
	model.SetSourceWithManualSpan(source, prefix.size(), body.size());

	EXPECT_EQ(source, model.Serialize());
	ASSERT_EQ(4u, model.Items().size());
	EXPECT_EQ(ItemKind::Tag, model.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(Origin::Raw, model.Items()[1].origin);
	EXPECT_EQ(ItemKind::Text, model.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[2].origin);
	EXPECT_EQ(body, model.Items()[2].source);
	EXPECT_EQ(ItemKind::Tag, model.Items()[3].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[3].origin);

	Model restored;
	restored.SetStoredSource(source, model.OriginMetadata());
	EXPECT_EQ(source, restored.Serialize());
	ASSERT_EQ(4u, restored.Items().size());
	EXPECT_EQ(Origin::Gui, restored.Items()[0].origin);
	EXPECT_EQ(Origin::Raw, restored.Items()[1].origin);
	EXPECT_EQ(ItemKind::Text, restored.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, restored.Items()[2].origin);
	EXPECT_EQ(body, restored.Items()[2].source);
	EXPECT_EQ(Origin::Gui, restored.Items()[3].origin);
}

TEST(ass_block_editor_model, structured_source_persists_empty_manual_span_in_place) {
	std::string prefix = "{\\b1}";
	std::string suffix = "{\\i1}";
	Model model;
	model.SetSourceWithManualSpan(prefix + suffix, prefix.size(), 0);

	ASSERT_EQ(3u, model.Items().size());
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(ItemKind::Text, model.Items()[1].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[1].origin);
	EXPECT_EQ("", model.Items()[1].source);
	EXPECT_EQ(Origin::Gui, model.Items()[2].origin);
	auto metadata = model.OriginMetadata();
	EXPECT_NE(std::string::npos, metadata.find(",M0,"));

	Model restored;
	restored.SetStoredSource(model.Serialize(), metadata);
	ASSERT_EQ(3u, restored.Items().size());
	EXPECT_EQ(Origin::Gui, restored.Items()[0].origin);
	EXPECT_EQ(Origin::Manual, restored.Items()[1].origin);
	EXPECT_EQ("", restored.Items()[1].source);
	EXPECT_EQ(Origin::Gui, restored.Items()[2].origin);
}

TEST(ass_block_editor_model, invalid_manual_span_defaults_complete_source_to_manual) {
	std::string source = "{\\pos(1,2)}正文";
	Model model;
	model.SetSourceWithManualSpan(source, source.size() + 1, 0);
	ASSERT_EQ(1u, model.Items().size());
	EXPECT_EQ(ItemKind::Text, model.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[0].origin);
	EXPECT_EQ(source, model.Items()[0].source);
	EXPECT_EQ(source, model.Serialize());
}

TEST(ass_block_editor_model, manual_and_gui_runs_survive_metadata_roundtrip) {
	Model model;
	model.SetStoredSource("目标正文", {});
	auto const& features = Model::Features();
	auto position = std::find_if(features.begin(), features.end(), [](auto const& feature) {
		return feature.tag == "pos";
	});
	ASSERT_NE(features.end(), position);
	ASSERT_TRUE(model.Insert(std::nullopt, *position));
	EXPECT_EQ("{\\pos(0,0)}目标正文", model.Serialize());
	ASSERT_EQ(2u, model.Items().size());
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(Origin::Manual, model.Items()[1].origin);

	ASSERT_TRUE(model.ReplaceManual(1, "手写{\\an8}\\N"));
	auto source = model.Serialize();
	auto metadata = model.OriginMetadata();
	EXPECT_EQ(std::string_view("aegisub.ass-block-origins"), ass::blocks::kOriginExtradataKey);
	EXPECT_EQ(0u, metadata.find("v1;"));

	Model restored;
	restored.SetStoredSource(source, metadata);
	EXPECT_EQ(source, restored.Serialize());
	ASSERT_EQ(2u, restored.Items().size());
	EXPECT_EQ(ItemKind::Tag, restored.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, restored.Items()[0].origin);
	EXPECT_EQ(ItemKind::Text, restored.Items()[1].kind);
	EXPECT_EQ("手写{\\an8}\\N", restored.Items()[1].source);
	EXPECT_EQ(Origin::Manual, restored.Items()[1].origin);
	EXPECT_EQ(metadata, restored.OriginMetadata());
}

TEST(ass_block_editor_model, stale_metadata_falls_back_to_manual_instead_of_taking_over) {
	Model original;
	original.SetSource("{\\pos(1,2)}old");
	auto metadata = original.OriginMetadata();

	Model restored;
	restored.SetStoredSource("{\\pos(9,9)}new", metadata);
	ASSERT_EQ(1u, restored.Items().size());
	EXPECT_EQ(ItemKind::Text, restored.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, restored.Items()[0].origin);
	EXPECT_EQ("{\\pos(9,9)}new", restored.Serialize());
}

TEST(ass_block_editor_model, visual_tool_changes_keep_gui_tags_and_manual_body) {
	std::string prefix = "{\\pos(1,2)\\fscx100\\frz0}";
	std::string body = "手写\\N{\\pos(114,514)}";
	Model model;
	model.SetSourceWithManualSpan(prefix + body, prefix.size(), body.size());

	std::string moved = "{\\fscx100\\frz0\\pos(9,8)}" + body;
	ASSERT_TRUE(model.SetGuiFirstOverride(moved));
	EXPECT_EQ(moved, model.Serialize());
	ASSERT_EQ(4u, model.Items().size());
	EXPECT_EQ("\\pos(9,8)", model.Items()[2].source);
	EXPECT_EQ(Origin::Gui, model.Items()[2].origin);
	EXPECT_EQ(body, model.Items()[3].source);
	EXPECT_EQ(Origin::Manual, model.Items()[3].origin);

	std::string transformed = "{\\pos(9,8)\\frz30\\fscx200}" + body;
	ASSERT_TRUE(model.SetGuiFirstOverride(transformed));
	Model reopened;
	reopened.SetStoredSource(transformed, model.OriginMetadata());
	EXPECT_EQ(transformed, reopened.Serialize());
	ASSERT_EQ(4u, reopened.Items().size());
	for (size_t i = 0; i < 3; ++i) {
		EXPECT_EQ(ItemKind::Tag, reopened.Items()[i].kind);
		EXPECT_EQ(Origin::Gui, reopened.Items()[i].origin);
	}
	EXPECT_EQ(body, reopened.Items()[3].source);
	EXPECT_EQ(Origin::Manual, reopened.Items()[3].origin);
}

TEST(ass_block_editor_model, visual_tool_takes_over_changed_manual_first_tag_only) {
	std::string body = "手写\\n{\\an8}";
	Model model;
	model.SetStoredSource("", {});
	ASSERT_TRUE(model.ReplaceManual(0, "{\\pos(1,2)}" + body));
	ASSERT_TRUE(model.SetGuiFirstOverride("{\\pos(3,4)}" + body));
	ASSERT_EQ(2u, model.Items().size());
	EXPECT_EQ("\\pos(3,4)", model.Items()[0].source);
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(body, model.Items()[1].source);
	EXPECT_EQ(Origin::Manual, model.Items()[1].origin);

	Model reopened;
	reopened.SetStoredSource(model.Serialize(), model.OriginMetadata());
	ASSERT_EQ(2u, reopened.Items().size());
	EXPECT_EQ(Origin::Gui, reopened.Items()[0].origin);
	EXPECT_EQ(body, reopened.Items()[1].source);
	EXPECT_EQ(Origin::Manual, reopened.Items()[1].origin);
}

TEST(ass_block_editor_model, visual_tool_preserves_untouched_manual_tags_in_same_and_later_blocks) {
	std::string original = "{\\pos(1,2)\\an 8}x{\\an 8}y";
	Model model;
	model.SetStoredSource("", {});
	ASSERT_TRUE(model.ReplaceManual(0, original));

	// The native visual tool edits only the first override block using the
	// lossless ASS AST; later hand-written source is left byte-for-byte intact.
	auto document = ass::ast::Document::Parse(original);
	auto *block = document.MutableSegment(0)->Block();
	ASSERT_NE(nullptr, block);
	block->EraseNode(0);
	block->AppendTag("\\pos(3,4)");
	std::string changed = document.Serialize();
	EXPECT_EQ("{\\an 8\\pos(3,4)}x{\\an 8}y", changed);
	ASSERT_TRUE(model.SetGuiFirstOverride(changed));
	ASSERT_EQ(3u, model.Items().size());
	EXPECT_EQ(ItemKind::Raw, model.Items()[0].kind);
	EXPECT_EQ(Origin::Manual, model.Items()[0].origin);
	EXPECT_EQ("\\an 8", model.Items()[0].source);
	EXPECT_EQ(ItemKind::Tag, model.Items()[1].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[1].origin);
	EXPECT_EQ("\\pos(3,4)", model.Items()[1].source);
	EXPECT_EQ("x{\\an 8}y", model.Items()[2].source);
	EXPECT_EQ(Origin::Manual, model.Items()[2].origin);

	Model reopened;
	reopened.SetStoredSource(changed, model.OriginMetadata());
	EXPECT_EQ(changed, reopened.Serialize());
	ASSERT_EQ(3u, reopened.Items().size());
	EXPECT_EQ(Origin::Manual, reopened.Items()[0].origin);
	EXPECT_EQ(Origin::Gui, reopened.Items()[1].origin);
	EXPECT_EQ(Origin::Manual, reopened.Items()[2].origin);
}

TEST(ass_block_editor_model, visual_tool_inserts_gui_override_before_manual_source) {
	std::string body = "正文\\N{\\pos(114,514)}";
	Model model;
	model.SetStoredSource("", {});
	ASSERT_TRUE(model.ReplaceManual(0, body));
	ASSERT_TRUE(model.SetGuiFirstOverride("{\\iclip(0,0,100,100)}" + body));
	ASSERT_EQ(2u, model.Items().size());
	EXPECT_EQ(ItemKind::Tag, model.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, model.Items()[0].origin);
	EXPECT_EQ(body, model.Items()[1].source);
	EXPECT_EQ(Origin::Manual, model.Items()[1].origin);
}

TEST(ass_block_editor_model, visual_tool_override_families_remain_structured) {
	std::pair<std::string, std::string> cases[] = {
		{"\\move(0,0,10,10)", "\\move(1,2,11,12)"},
		{"\\org(0,0)", "\\org(10,20)"},
		{"\\fscy100", "\\fscy125"},
		{"\\frx0", "\\frx15"},
		{"\\fry0", "\\fry20"},
		{"\\clip(0,0,10,10)", "\\clip(2,2,12,12)"},
		{"\\iclip(0,0,10,10)", "\\iclip(2,2,12,12)"}
	};
	for (auto const& [before, after] : cases) {
		std::string old_prefix = "{" + before + "}";
		std::string body = "正文\\N";
		Model model;
		model.SetSourceWithManualSpan(old_prefix + body, old_prefix.size(), body.size());
		ASSERT_TRUE(model.SetGuiFirstOverride("{" + after + "}" + body)) << after;
		ASSERT_EQ(2u, model.Items().size()) << after;
		EXPECT_EQ(ItemKind::Tag, model.Items()[0].kind) << after;
		EXPECT_EQ(Origin::Gui, model.Items()[0].origin) << after;
		EXPECT_EQ(after, model.Items()[0].source);
		EXPECT_EQ(Origin::Manual, model.Items()[1].origin);
		EXPECT_EQ(body, model.Items()[1].source);
	}
}

TEST(ass_block_editor_model, direct_source_takeover_persists_known_gui_and_unknown_raw_items) {
	std::string source = "{\\pos(844,306.67)\\vendor(x)}正文\\N";
	Model model;
	model.SetSource(source);
	auto metadata = model.OriginMetadata();

	Model restored;
	restored.SetStoredSource(source, metadata);
	EXPECT_EQ(source, restored.Serialize());
	ASSERT_GE(restored.Items().size(), 3u);
	EXPECT_EQ(ItemKind::Tag, restored.Items()[0].kind);
	EXPECT_EQ(Origin::Gui, restored.Items()[0].origin);
	EXPECT_EQ(ItemKind::Raw, restored.Items()[1].kind);
	EXPECT_EQ(Origin::Raw, restored.Items()[1].origin);
	EXPECT_EQ(ItemKind::Text, restored.Items()[2].kind);
	EXPECT_EQ(Origin::Manual, restored.Items()[2].origin);
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

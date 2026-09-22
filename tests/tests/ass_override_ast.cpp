// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#include "ass_override_ast.h"

#include <gtest/gtest.h>

using ass::ast::Document;
using ass::ast::OverrideNodeKind;
using ass::ast::SegmentKind;

TEST(ass_override_ast, untouched_complex_dialogue_is_byte_exact) {
	std::string source =
		"lead{note}{\\an8\\b1\\1a&HFF&\\3c&HA87FFF&\\bord23.00\\shad0"
		"\\t( 0, 250, 1.25, \\fscx100\\fscy100\\clip(1,m 0 0 l 10 10) )"
		"\\vendor(foo,(bar,baz))}直播\\N准备中{\\rAlt Style}。。。";
	auto document = Document::Parse(source);

	EXPECT_FALSE(document.IsDirty());
	EXPECT_EQ(source, document.Serialize());
	ASSERT_EQ(6u, document.Segments().size());
	EXPECT_EQ(SegmentKind::Comment, document.Segments()[1].Kind());
	EXPECT_EQ(SegmentKind::Override, document.Segments()[2].Kind());
	EXPECT_EQ(SegmentKind::Override, document.Segments()[4].Kind());

	auto unknown = document.FindTags("vendor");
	ASSERT_EQ(1u, unknown.size());
	EXPECT_FALSE(unknown.front()->IsKnownTag());
	EXPECT_EQ("\\vendor(foo,(bar,baz))", unknown.front()->Original());
}

TEST(ass_override_ast, parses_nested_transform_without_flattening_it) {
	std::string source = "{\\t(0,500,\\frz360\\t(100,200,\\bord4\\3c&H112233&))}x";
	auto document = Document::Parse(source);

	auto transforms = document.FindTags("t");
	ASSERT_EQ(2u, transforms.size());
	ASSERT_NE(nullptr, transforms[0]->Transform());
	ASSERT_NE(nullptr, transforms[1]->Transform());
	EXPECT_EQ(1u, document.FindTags("frz").size());
	EXPECT_EQ(1u, document.FindTags("3c").size());
	EXPECT_EQ(source, document.Serialize());
}

TEST(ass_override_ast, identifies_drawing_payload_and_returns_to_text) {
	std::string source = "{\\p1}m 0 0 l 100 0 100 100 0 100{shape note}{\\p0}caption";
	auto document = Document::Parse(source);

	ASSERT_EQ(5u, document.Segments().size());
	EXPECT_EQ(SegmentKind::Override, document.Segments()[0].Kind());
	EXPECT_EQ(SegmentKind::Drawing, document.Segments()[1].Kind());
	EXPECT_EQ(1, document.Segments()[1].DrawingScale());
	EXPECT_EQ(SegmentKind::Comment, document.Segments()[2].Kind());
	EXPECT_EQ(SegmentKind::Override, document.Segments()[3].Kind());
	EXPECT_EQ(SegmentKind::Text, document.Segments()[4].Kind());
	EXPECT_EQ(source, document.Serialize());
}

TEST(ass_override_ast, changing_one_property_preserves_every_other_node) {
	std::string source = "{ odd \\an8  \\bord23.00\\vendor  XYZ\\pos(844,306.67)}text{\\i1}tail";
	auto document = Document::Parse(source);
	ASSERT_TRUE(document.SetFirstArgument("bord", 0, "4.5"));

	EXPECT_EQ(
		"{ odd \\an8  \\bord4.5\\vendor  XYZ\\pos(844,306.67)}text{\\i1}tail",
		document.Serialize());
	EXPECT_EQ("\\an8  ", document.FindTags("an").front()->Original());
	EXPECT_EQ("\\vendor  XYZ", document.FindTags("vendor").front()->Original());
}

TEST(ass_override_ast, changing_nested_tag_only_rebuilds_its_transform) {
	std::string source = "a{\\b1\\t( 0, 250, \\fscx0\\fscy0 )\\unknown(raw)}b";
	auto document = Document::Parse(source);
	auto scales = document.FindTags("fscx");
	ASSERT_EQ(1u, scales.size());
	scales.front()->SetArgument(0, "100");

	EXPECT_EQ("a{\\b1\\t(0,250,\\fscx100\\fscy0)\\unknown(raw)}b", document.Serialize());
}

TEST(ass_override_ast, clones_have_independent_editable_storage) {
	std::string source = "{\\pos(10,20)\\t(\\bord2)}text";
	auto original = Document::Parse(source);
	auto clone = original.Clone();

	ASSERT_TRUE(clone.SetFirstArgument("pos", 0, "30"));
	ASSERT_TRUE(clone.SetFirstArgument("bord", 0, "5"));
	EXPECT_EQ(source, original.Serialize());
	EXPECT_EQ("{\\pos(30,20)\\t(\\bord5)}text", clone.Serialize());
}

TEST(ass_override_ast, preserves_malformed_and_empty_override_content) {
	for (std::string const source : {
		"{}plain",
		"{\\t(0,100,\\bord3}broken",
		"{\\unknown(foo,,bar) junk}x",
		"plain { without close"
	}) {
		EXPECT_EQ(source, Document::Parse(source).Serialize()) << source;
	}
}

TEST(ass_override_ast, supports_structural_tag_edits) {
	auto document = Document::Parse("{\\an8}text");
	auto* block = document.MutableSegment(0)->Block();
	ASSERT_NE(nullptr, block);
	auto& added = block->AppendTag("\\frz45");
	EXPECT_EQ(OverrideNodeKind::Tag, added.Kind());
	EXPECT_EQ("{\\an8\\frz45}text", document.Serialize());

	block->EraseNode(0);
	EXPECT_EQ("{\\frz45}text", document.Serialize());
}

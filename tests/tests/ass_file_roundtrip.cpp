// Copyright (c) 2026
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted.

#include <gtest/gtest.h>

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "subtitle_format_ass.h"

#include <libaegisub/vfr.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
class TempAssFiles {
	std::filesystem::path directory;

public:
	agi::fs::path input;
	agi::fs::path output;

	TempAssFiles() {
		auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
		directory = std::filesystem::temp_directory_path() / ("aegisub-ass-roundtrip-" + suffix);
		std::filesystem::create_directories(directory);
		input = agi::fs::path(directory / "input.ass");
		output = agi::fs::path(directory / "output.ass");
	}

	~TempAssFiles() {
		std::error_code error;
		std::filesystem::remove_all(directory, error);
	}
};

std::string ReadAll(agi::fs::path const& path) {
	std::ifstream stream(path, std::ios::binary);
	std::ostringstream data;
	data << stream.rdbuf();
	return data.str();
}
}

TEST(ass_file_roundtrip, preserves_unknown_content_while_writing_edited_known_fields) {
	TempAssFiles files;
	std::string const source =
		"[Script Info]\n"
		"; vendor comment with spacing  \n"
		"Title: Original title\n"
		"Vendor-Key:   retain this value\n"
		"an unstructured script-info line\n"
		"\n"
		"[Vendor Private Section]\n"
		"opaque:first\n"
		"; opaque comment\n"
		"\n"
		"[V4+ Styles]\n"
		"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
		"vendor style line\n"
		"Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n"
		"Style: malformed and must stay raw\n"
		"\n"
		"[Events]\n"
		"Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
		"event vendor line\n"
		"Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Original dialogue\n"
		"Dialogue: malformed and must stay raw\n"
		"\n"
		"[Aegisub Project Garbage]\n"
		"Video File: original-video.mkv\n"
		"Vendor Project Field: keep me\n";

	{
		std::ofstream stream(files.input, std::ios::binary);
		stream << source;
	}

	AssSubtitleFormat format;
	AssFile file;
	format.ReadFile(&file, files.input, agi::vfr::Framerate(), "utf-8");

	ASSERT_EQ(1U, file.Styles.size());
	ASSERT_EQ(1U, file.Events.size());
	EXPECT_EQ("retain this value", file.GetScriptInfo("Vendor-Key"));

	file.SetScriptInfo("Title", "Updated title");
	file.Properties.video_file = "updated-video.mkv";
	file.Styles.front().font = "Updated Font";
	file.Styles.front().UpdateData();
	file.Events.front().Text = "Updated dialogue";

	format.WriteFile(&file, files.output, agi::vfr::Framerate(), "utf-8");
	auto result = ReadAll(files.output);

	EXPECT_NE(std::string::npos, result.find("; vendor comment with spacing  "));
	EXPECT_NE(std::string::npos, result.find("Vendor-Key:   retain this value"));
	EXPECT_NE(std::string::npos, result.find("an unstructured script-info line"));
	EXPECT_NE(std::string::npos, result.find("[Vendor Private Section]"));
	EXPECT_NE(std::string::npos, result.find("opaque:first"));
	EXPECT_NE(std::string::npos, result.find("vendor style line"));
	EXPECT_NE(std::string::npos, result.find("Style: malformed and must stay raw"));
	EXPECT_NE(std::string::npos, result.find("event vendor line"));
	EXPECT_NE(std::string::npos, result.find("Dialogue: malformed and must stay raw"));
	EXPECT_NE(std::string::npos, result.find("Vendor Project Field: keep me"));

	EXPECT_NE(std::string::npos, result.find("Title: Updated title"));
	EXPECT_NE(std::string::npos, result.find("Video File: updated-video.mkv"));
	EXPECT_NE(std::string::npos, result.find("Style: Default,Updated Font,"));
	EXPECT_NE(std::string::npos, result.find("Updated dialogue"));
	EXPECT_EQ(std::string::npos, result.find("Title: Original title"));
	EXPECT_EQ(std::string::npos, result.find("Video File: original-video.mkv"));

	auto script_comment = result.find("; vendor comment with spacing  ");
	auto vendor_key = result.find("Vendor-Key:   retain this value");
	auto unstructured = result.find("an unstructured script-info line");
	auto private_section = result.find("[Vendor Private Section]");
	auto style_section = result.find("[V4+ Styles]");
	ASSERT_LT(script_comment, vendor_key);
	ASSERT_LT(vendor_key, unstructured);
	ASSERT_LT(unstructured, private_section);
	ASSERT_LT(private_section, style_section);

	AssFile reopened;
	format.ReadFile(&reopened, files.output, agi::vfr::Framerate(), "utf-8");
	EXPECT_EQ("Updated title", reopened.GetScriptInfo("Title"));
	EXPECT_EQ("retain this value", reopened.GetScriptInfo("Vendor-Key"));
	EXPECT_EQ("updated-video.mkv", reopened.Properties.video_file);
}

// Copyright (c) 2026, Aegisub Localization Edition contributors

#include "main.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "fold_controller.h"
#include "subtitle_format_ass.h"

#include <libaegisub/vfr.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <wx/string.h>

namespace {
class logical_group_view : public ::testing::Test {
protected:
	AssFile file;
	FoldController folds{&file};
	std::vector<AssDialogue *> lines;

	void SetUp() override {
		for (int i = 0; i < 5; ++i) {
			auto line = new AssDialogue;
			line->Start = i * 1000;
			line->End = (i + 1) * 1000;
			line->Text = "{\\pos(0," + std::to_string(i * 10) + ")}subtitle";
			file.Events.push_back(*line);
			lines.push_back(line);
		}
		file.Commit("", AssFile::COMMIT_NEW);
	}
};
}

TEST_F(logical_group_view, expanded_header_is_a_separate_non_event_row) {
	folds.AddFold(*lines[0], *lines[1], false);
	auto const& rows = folds.GetDisplayRows();
	ASSERT_EQ(6U, rows.size());
	EXPECT_TRUE(rows[0].group_header);
	EXPECT_EQ(lines[0], rows[0].line);
	EXPECT_FALSE(rows[1].group_header);
	EXPECT_EQ(lines[0], rows[1].line);
	EXPECT_FALSE(rows[2].group_header);
	EXPECT_EQ(lines[1], rows[2].line);
	EXPECT_EQ(1, lines[0]->Fold.getVisibleRow());
	EXPECT_EQ(2, lines[1]->Fold.getVisibleRow());
	EXPECT_EQ(5U, file.Events.size());
	EXPECT_EQ(1000, int(lines[0]->End));
	EXPECT_EQ(1000, int(lines[1]->Start));
	EXPECT_FALSE(lines[0]->Comment);
}

TEST_F(logical_group_view, collapse_and_release_change_only_the_view) {
	folds.AddFold(*lines[0], *lines[1], false);
	folds.CloseFoldsAt({lines[0]});
	ASSERT_EQ(4U, folds.GetDisplayRows().size());
	EXPECT_TRUE(folds.GetDisplayRows()[0].group_header);
	EXPECT_EQ(0, lines[0]->Fold.getVisibleRow());
	EXPECT_EQ(0, lines[1]->Fold.getVisibleRow());
	EXPECT_EQ(5U, file.Events.size());
	folds.OpenFoldsAt({lines[0]});
	ASSERT_EQ(6U, folds.GetDisplayRows().size());
	folds.ClearFoldsAt({lines[0], lines[1]});
	ASSERT_EQ(5U, folds.GetDisplayRows().size());
	for (size_t i = 0; i < lines.size(); ++i) {
		EXPECT_FALSE(folds.GetDisplayRows()[i].group_header);
		EXPECT_EQ(lines[i], folds.GetDisplayRows()[i].line);
		EXPECT_EQ(int(i), lines[i]->Fold.getVisibleRow());
	}
	EXPECT_EQ(5U, file.Events.size());
}

TEST_F(logical_group_view, releasing_one_child_preserves_the_real_lines) {
	folds.AddFold(*lines[0], *lines[4], false);
	folds.ReleaseLineFromFold(*lines[2]);
	EXPECT_EQ(5U, file.Events.size());
	ASSERT_EQ(7U, folds.GetDisplayRows().size());
	EXPECT_TRUE(folds.GetDisplayRows()[0].group_header);
	EXPECT_FALSE(folds.GetDisplayRows()[3].group_header);
	EXPECT_EQ(lines[2], folds.GetDisplayRows()[3].line);
	EXPECT_TRUE(folds.GetDisplayRows()[4].group_header);
	EXPECT_EQ(1U, folds.GetFoldLines(*lines[2]).size());
	EXPECT_EQ(2U, folds.GetFoldLines(*lines[0]).size());
	EXPECT_EQ(2U, folds.GetFoldLines(*lines[3]).size());
}

TEST_F(logical_group_view, merging_ordinary_lines_and_groups_keeps_one_flat_header) {
	folds.AddFold(*lines[0], *lines[1], false);
	folds.AddFold(*lines[3], *lines[4], false);
	folds.MergeIntoFlatGroup({lines[0], lines[2], lines[3]});
	ASSERT_EQ(1U, folds.GetDisplayRows().size());
	EXPECT_TRUE(folds.GetDisplayRows()[0].group_header);
	EXPECT_EQ(5U, folds.GetFoldLines(*lines[0]).size());
	folds.OpenAllFolds();
	ASSERT_EQ(6U, folds.GetDisplayRows().size());
	for (size_t i = 1; i < folds.GetDisplayRows().size(); ++i)
		EXPECT_FALSE(folds.GetDisplayRows()[i].group_header);
	EXPECT_EQ(5U, file.Events.size());
}

TEST_F(logical_group_view, export_contains_only_the_original_dialogues) {
	folds.AddFold(*lines[0], *lines[1], true);
	auto path = std::filesystem::temp_directory_path() /
		("aegisub-logical-group-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ass");
	struct Cleanup {
		std::filesystem::path path;
		~Cleanup() { std::error_code error; std::filesystem::remove(path, error); }
	} cleanup{path};
	AssSubtitleFormat{}.ExportFile(&file, agi::fs::path(path.string()), agi::vfr::Framerate(), "utf-8");
	std::ifstream stream(path);
	std::string row;
	int dialogue_count = 0;
	while (std::getline(stream, row)) {
		if (row.starts_with("Dialogue:")) ++dialogue_count;
		EXPECT_FALSE(row.starts_with("Comment:"));
		EXPECT_EQ(std::string::npos, row.find("Logical group"));
	}
	EXPECT_EQ(5, dialogue_count);
	EXPECT_EQ(5U, file.Events.size());
}

TEST_F(logical_group_view, default_line_added_after_precommit_is_still_visible) {
	file.Events.clear();
	file.Commit("", AssFile::COMMIT_NEW);
	// SubsController inserts one empty physical line after the pre-commit
	// phase when the user deletes all subtitles.
	auto line = new AssDialogue;
	line->Row = 0;
	file.Events.push_back(*line);
	ASSERT_EQ(1U, folds.GetDisplayRows().size());
	EXPECT_FALSE(folds.GetDisplayRows()[0].group_header);
	EXPECT_EQ(line, folds.GetDisplayRows()[0].line);
	EXPECT_EQ(0, line->Fold.getVisibleRow());
}

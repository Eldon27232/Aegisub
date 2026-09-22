// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

#include "fold_controller.h"

#include "ass_file.h"
#include "include/aegisub/context.h"
#include "format.h"

#include <algorithm>
#include <unordered_map>

#include <libaegisub/split.h>
#include <libaegisub/util.h>

const char *folds_key = "_aegi_folddata";

FoldController::FoldController(agi::Context *c)
: FoldController(c->ass.get())
{ }

FoldController::FoldController(AssFile *file)
: file(file)
, pre_commit_listener(file->AddPreCommitListener(&FoldController::FixFoldsPreCommit, this))
{ }


bool FoldController::CanAddFold(AssDialogue& start, AssDialogue& end) {
	if (start.Fold.valid || end.Fold.valid) {
		return false;
	}
	int folddepth = 0;
	for (auto it = std::next(file->Events.begin(), start.Row); it->Row < end.Row; it++) {
		if (it->Fold.valid) {
			folddepth += it->Fold.side ? -1 : 1;
		}
		if (folddepth < 0) {
			return false;
		}
	}
	return folddepth == 0;
}

void FoldController::RawAddFold(AssDialogue& start, AssDialogue& end, bool collapsed) {
	int id = ++max_fold_id;
	file->SetExtradataValue(start, folds_key, agi::format("0;%d;%d", int(collapsed), id));
	file->SetExtradataValue(end, folds_key, agi::format("1;%d;%d", int(collapsed), id));
}

void FoldController::UpdateLineExtradata(AssDialogue &line) {
	if (line.Fold.extraExists)
		file->SetExtradataValue(line, folds_key, agi::format("%d;%d;%d", int(line.Fold.side), int(line.Fold.collapsed), int(line.Fold.id)));
	else
		file->DeleteExtradataValue(line, folds_key);
}

void FoldController::InvalidateLineFold(AssDialogue &line) {
	line.Fold.valid = false;
	if (++line.Fold.invalidCount > 100) {
		line.Fold.extraExists = false;
		UpdateLineExtradata(line);
	}
}

void FoldController::AddFold(AssDialogue& start, AssDialogue& end, bool collapsed) {
	if (CanAddFold(start, end)) {
		RawAddFold(start, end, collapsed);
		file->Commit(_("add fold"), AssFile::COMMIT_FOLD);
	}
}

void FoldController::AddAutomaticFold(AssDialogue& start, AssDialogue& end, bool collapsed) {
	RawAddFold(start, end, collapsed);
}

std::vector<AssDialogue *> FoldController::GetFoldLines(AssDialogue& line) const {
	AssDialogue *opener = nullptr;
	if (line.Fold.valid && !line.Fold.side)
		opener = &line;
	else if (line.Fold.parent)
		opener = line.Fold.parent;
	else if (line.Fold.valid && line.Fold.side)
		opener = line.Fold.counterpart;

	if (!opener || !opener->Fold.counterpart)
		return {&line};

	std::vector<AssDialogue *> result;
	auto end = opener->Fold.counterpart;
	for (auto it = file->iterator_to(*opener); it != file->Events.end(); ++it) {
		result.push_back(&*it);
		if (&*it == end) break;
	}
	return result;
}

void FoldController::MergeIntoFlatGroup(std::vector<AssDialogue *> const& lines) {
	if (lines.size() < 2) return;

	int first = file->Events.size();
	int last = -1;
	for (auto *line : lines) {
		if (!line) continue;
		auto group = GetFoldLines(*line);
		first = std::min(first, group.front()->Row);
		last = std::max(last, group.back()->Row);
	}
	if (first < 0 || last <= first) return;

	// Include any group crossed by the selected range, then remove all old
	// delimiters in the final range so the result stays flat rather than nested.
	bool expanded;
	do {
		expanded = false;
		for (auto& line : file->Events) {
			if (line.Row < first || line.Row > last) continue;
			auto group = GetFoldLines(line);
			int next_first = std::min(first, group.front()->Row);
			int next_last = std::max(last, group.back()->Row);
			expanded |= next_first != first || next_last != last;
			first = next_first;
			last = next_last;
		}
	} while (expanded);

	AssDialogue *start = nullptr;
	AssDialogue *end = nullptr;
	for (auto& line : file->Events) {
		if (line.Row < first || line.Row > last) continue;
		if (!start) start = &line;
		end = &line;
		file->DeleteExtradataValue(line, folds_key);
	}
	if (!start || !end || start == end) return;
	RawAddFold(*start, *end, true);
	file->Commit(_("Merge logical subtitle group"), AssFile::COMMIT_FOLD);
}

void FoldController::ReleaseLineFromFold(AssDialogue& line) {
	auto group = GetFoldLines(line);
	if (group.size() < 2) return;
	auto found = std::find(group.begin(), group.end(), &line);
	if (found == group.end()) return;
	size_t released = static_cast<size_t>(found - group.begin());
	bool collapsed = group.front()->Fold.collapsed;

	for (auto *member : group)
		file->DeleteExtradataValue(*member, folds_key);

	if (released >= 2)
		RawAddFold(*group.front(), *group[released - 1], collapsed);
	if (group.size() - released - 1 >= 2)
		RawAddFold(*group[released + 1], *group.back(), collapsed);

	file->Commit(_("Release current line from logical subtitle group"), AssFile::COMMIT_FOLD);
}

void FoldController::DoForAllFolds(std::function<void(AssDialogue&)> action) {
	for (AssDialogue& line : file->Events) {
		if (line.Fold.valid) {
			action(line);
			UpdateLineExtradata(line);
		}
	}
}

void FoldController::FixFoldsPreCommit(int type, const AssDialogue *single_line) {
	if ((type & (AssFile::COMMIT_FOLD | AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_ORDER)) || type == AssFile::COMMIT_NEW) {
		UpdateFoldInfo();
	}
}

// For each line in lines, applies action() to the opening delimiter of the innermost fold containing this line.
//
// In general, this can leave the folds in an inconsistent state, so unless action() is read-only this should always
// be followed by a commit.
void FoldController::DoForFoldsAt(std::vector<AssDialogue *> const& lines, std::function<void(AssDialogue&)> action) {
	std::map<int, bool> visited;
	for (AssDialogue *line : lines) {
		if (line->Fold.parent != nullptr && !(line->Fold.valid && !line->Fold.side)) {
			line = line->Fold.parent;
		}
		if (visited.count(line->Row))
			continue;

		action(*line);
		UpdateLineExtradata(*line);
		visited[line->Row] = true;
	}
}

void FoldController::UpdateFoldInfo() {
	ReadFromExtradata();
	FixFolds();
	LinkFolds();
}

void FoldController::ReadFromExtradata() {
	max_fold_id = 0;

	for (auto line = file->Events.begin(); line != file->Events.end(); line++) {
		line->Fold.extraExists = false;

		for (auto const& extra : file->GetExtradata(line->ExtradataIds)) {
			if (extra.key == folds_key) {
				std::vector<std::string> fields;
				agi::Split(fields, extra.value, ';');
				if (fields.size() != 3)
					break;

				int side;
				int collapsed;
				if (!agi::util::try_parse(fields[0], &side)) break;
				if (!agi::util::try_parse(fields[1], &collapsed)) break;
				if (!agi::util::try_parse(fields[2], &line->Fold.id)) break;
				line->Fold.side = side;
				line->Fold.collapsed = collapsed;

				line->Fold.extraExists = true;
				max_fold_id = std::max(max_fold_id, line->Fold.id);
				break;
			}
		}
		line->Fold.valid = line->Fold.extraExists;
	}
}

void FoldController::FixFolds() {
	// Stack of which folds we've desended into so far
	std::vector<AssDialogue *> foldStack;

	// ID's for which we've found starters
	std::unordered_map<int, AssDialogue*> foldHeads;

	// ID's for which we've either found a valid starter and ender,
	// or determined that the respective fold is invalid. All further
	// fold data with this ID is skipped and deleted.
	std::unordered_map<int, bool> completedFolds;

	// Map iteratively applied to all id's.
	// Once some fold has been completely found, subsequent markers found with the same id will be mapped to this new id.
	std::unordered_map<int, int> idRemap;

	for (auto line = file->Events.begin(); line != file->Events.end(); line++) {
		if (line->Fold.extraExists) {
			bool needs_update = false;

			while (idRemap.count(line->Fold.id)) {
				line->Fold.id = idRemap[line->Fold.id];
				needs_update = true;
			}

			if (completedFolds.count(line->Fold.id)) { 	// Duplicate entry - try to start a new one
				idRemap[line->Fold.id] = ++max_fold_id;
				line->Fold.id = idRemap[line->Fold.id];
				needs_update = true;
			}

			if (!line->Fold.side) {
				if (foldHeads.count(line->Fold.id)) { 	// Duplicate entry
					InvalidateLineFold(*line);
				} else {
					foldHeads[line->Fold.id] = &*line;
					foldStack.push_back(&*line);
				}
			} else {
				if (!foldHeads.count(line->Fold.id)) { 	// Non-matching ender
					// Deactivate it. Because we can, also push it to completedFolds:
					// If its counterpart appears further below, we can delete it right away.
					completedFolds[line->Fold.id] = true;
					InvalidateLineFold(*line);
				} else {
					// We found a fold. Now we need to see if the stack matches.
					// We scan our stack for the counterpart of the fold.
					// If one exists, we assume all starters above it are invalid.
					// If none exists, we assume this ender is invalid.
					// If none of these assumptions are true, the folds are probably
					// broken beyond repair.

					completedFolds[line->Fold.id] = true;
					bool found = false;
					for (int i = foldStack.size() - 1; i >= 0; i--) {
						if (foldStack[i]->Fold.id == line->Fold.id) {
							// Erase all folds further inward
							for (int j = foldStack.size() - 1; j > i; j--) {
								completedFolds[foldStack[j]->Fold.id] = true;
								InvalidateLineFold(*foldStack[j]);
								foldStack.pop_back();
							}

							// Sync the found fold and pop the stack
							if (line->Fold.collapsed != foldStack[i]->Fold.collapsed) {
								line->Fold.collapsed = foldStack[i]->Fold.collapsed;
								needs_update = true;
							}
							foldStack.pop_back();

							found = true;
							break;
						}
					}
					if (!found) {
						completedFolds[line->Fold.id] = true;
						InvalidateLineFold(*line);
					}
				}
			}

			if (needs_update) {
				UpdateLineExtradata(*line);
			}
		}
	}

	// All remaining lines are invalid
	for (AssDialogue *line : foldStack) {
		line->Fold.valid = false;
		if (++line->Fold.invalidCount > 100) {
			line->Fold.extraExists = false;
			UpdateLineExtradata(*line);
		}
	}
}

void FoldController::LinkFolds() {
	std::vector<AssDialogue *> foldStack;
	maxdepth = 0;
	for (auto line = file->Events.begin(); line != file->Events.end(); line++) {
		line->Fold.parent = foldStack.empty() ? nullptr : foldStack.back();
		line->Fold.counterpart = nullptr;
		if (line->Fold.valid && !line->Fold.side) {
			foldStack.push_back(&*line);
			if ((int) foldStack.size() > maxdepth) {
				maxdepth = foldStack.size();
			}
		}
		if (line->Fold.valid && line->Fold.side) {
			line->Fold.counterpart = foldStack.back();
			(*foldStack.rbegin())->Fold.counterpart = &*line;

			foldStack.pop_back();
		}
	}

	GetDisplayRows();
}

std::vector<FoldDisplayRow> const& FoldController::GetDisplayRows() {
	// A view has a logical header followed by every real subtitle when open,
	// or only its header when closed. Headers never become ASS events.
	display_rows.clear();
	int hidden_through = -1;
	int hidden_header = -1;
	for (auto& line : file->Events) {
		if (line.Row <= hidden_through) {
			line.Fold.visibleRow = hidden_header;
			continue;
		}
		if (line.Fold.valid && !line.Fold.side) {
			int header = static_cast<int>(display_rows.size());
			display_rows.push_back({&line, true});
			if (line.Fold.collapsed) {
				hidden_through = line.Fold.counterpart->Row;
				hidden_header = header;
				line.Fold.visibleRow = header;
				continue;
			}
		}
		line.Fold.visibleRow = static_cast<int>(display_rows.size());
		display_rows.push_back({&line, false});
	}
	return display_rows;
}

int FoldController::GetMaxDepth() {
	return maxdepth;
}


void FoldController::ClearAllFolds() {
	DoForAllFolds([&](AssDialogue &line) {
		line.Fold.extraExists = false; line.Fold.valid = false;
	});
	file->Commit(_("clear all folds"), AssFile::COMMIT_FOLD);
}

void FoldController::OpenAllFolds() {
	DoForAllFolds([&](AssDialogue &line) {
		line.Fold.collapsed = false;
	});
	file->Commit(_("open all folds"), AssFile::COMMIT_FOLD);
}

void FoldController::CloseAllFolds() {
	DoForAllFolds([&](AssDialogue &line) {
		line.Fold.collapsed = true;
	});
	file->Commit(_("close all folds"), AssFile::COMMIT_FOLD);
}

bool FoldController::HasFolds() {
	bool hasfold = false;
	DoForAllFolds([&](AssDialogue &line) {
		hasfold = hasfold || line.Fold.valid;
	});
	return hasfold;
}

void FoldController::ClearFoldsAt(std::vector<AssDialogue *> const& lines) {
	DoForFoldsAt(lines, [&](AssDialogue &line) {
		line.Fold.extraExists = false; line.Fold.valid = false;
		if (line.Fold.counterpart) {
			line.Fold.counterpart->Fold.extraExists = false;
			line.Fold.counterpart->Fold.valid = false;
			UpdateLineExtradata(*line.Fold.counterpart);
		}
	});
	file->Commit(_("clear folds"), AssFile::COMMIT_FOLD);
}

void FoldController::OpenFoldsAt(std::vector<AssDialogue *> const& lines) {
	DoForFoldsAt(lines, [&](AssDialogue &line) {
		line.Fold.collapsed = false;
		if (line.Fold.counterpart)
			line.Fold.counterpart->Fold.collapsed = line.Fold.collapsed;
	});
	file->Commit(_("open folds"), AssFile::COMMIT_FOLD);
}

void FoldController::CloseFoldsAt(std::vector<AssDialogue *> const& lines) {
	DoForFoldsAt(lines, [&](AssDialogue &line) {
		line.Fold.collapsed = true;
		if (line.Fold.counterpart)
			line.Fold.counterpart->Fold.collapsed = line.Fold.collapsed;
	});
	file->Commit(_("close folds"), AssFile::COMMIT_FOLD);
}

void FoldController::ToggleFoldsAt(std::vector<AssDialogue *> const& lines) {
	DoForFoldsAt(lines, [&](AssDialogue &line) {
		line.Fold.collapsed = !line.Fold.collapsed;
		if (line.Fold.counterpart)
			line.Fold.counterpart->Fold.collapsed = line.Fold.collapsed;
	});
	file->Commit(_("toggle folds"), AssFile::COMMIT_FOLD);
}

bool FoldController::AreFoldsAt(std::vector<AssDialogue *> const& lines) {
	bool hasfold = false;
	DoForFoldsAt(lines, [&](AssDialogue &line) {
		hasfold = hasfold || line.Fold.valid;
	});
	return hasfold;
}

// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.

#pragma once

class wxWindow;
namespace agi { struct Context; }

enum class AssTemplateDialogResult {
	Closed,
	AppliedCurrent,
	AppliedNew
};

AssTemplateDialogResult ShowAssTemplateManager(wxWindow *parent, agi::Context *context);

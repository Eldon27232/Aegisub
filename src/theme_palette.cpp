// Copyright (c) 2026, Aegisub Localization Edition contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "theme_palette.h"

#include <cctype>
#include <string>

namespace theme {
namespace {

wxColour Colour(unsigned long rgb) {
	return wxColour(
		static_cast<unsigned char>((rgb >> 16) & 0xff),
		static_cast<unsigned char>((rgb >> 8) & 0xff),
		static_cast<unsigned char>(rgb & 0xff));
}

Palette const dark_plus {
	Colour(0x1e1e1e), Colour(0x252526), Colour(0xf1f1f1), Colour(0xa7a7a7),
	Colour(0x454545), Colour(0x4aa5f0), Colour(0x264f78), Colour(0xffffff),
	Colour(0x5a1d1d),

	Colour(0x1e1e1e), Colour(0x2d2d30), Colour(0x3f3f46), Colour(0x263a2b),
	Colour(0x23374b), Colour(0x415a46), Colour(0x24384b), Colour(0x3a303f),
	Colour(0x252526), Colour(0xf1f1f1), Colour(0xffffff), Colour(0xff8080),
	Colour(0x4aa5f0),

	Colour(0x171717), Colour(0x69b7ff), Colour(0x5f6b76), Colour(0x264f78),
	Colour(0xffcf66), Colour(0xff6b6b), Colour(0xc48dff),

	Colour(0x3399ff), Colour(0xb0b0b0)
};

Palette const deep_gray {
	Colour(0x292929), Colour(0x353535), Colour(0xf0f0f0), Colour(0xb1b1b1),
	Colour(0x505050), Colour(0xc4c7cb), Colour(0x55585e), Colour(0xffffff),
	Colour(0x653030),

	Colour(0x2b2b2b), Colour(0x383838), Colour(0x505050), Colour(0x354438),
	Colour(0x45474b), Colour(0x566359), Colour(0x42454a), Colour(0x555158),
	Colour(0x333333), Colour(0xf0f0f0), Colour(0xffffff), Colour(0xff8b8b),
	Colour(0xc4c7cb),

	Colour(0x242424), Colour(0xd0d3d8), Colour(0x78838c), Colour(0x55585e),
	Colour(0xffd37d), Colour(0xff8585), Colour(0xcda1ff),

	Colour(0xaeb4bd), Colour(0xc0c0c0)
};

Palette const oled_black {
	Colour(0x000000), Colour(0x0a0a0a), Colour(0xf5f5f5), Colour(0xaaaaaa),
	Colour(0x333333), Colour(0xedae95), Colour(0x683f33), Colour(0xffffff),
	Colour(0x500f0f),

	Colour(0x000000), Colour(0x101010), Colour(0x292929), Colour(0x102815),
	Colour(0x3b2823), Colour(0x29452f), Colour(0x452c24), Colour(0x493728),
	Colour(0x080808), Colour(0xf5f5f5), Colour(0xffffff), Colour(0xff7373),
	Colour(0xedae95),

	Colour(0x000000), Colour(0xefb59c), Colour(0x52616d), Colour(0x683f33),
	Colour(0xffca5f), Colour(0xff5959), Colour(0xdd9cbe),

	Colour(0xe5a084), Colour(0x8f8f8f)
};

Palette const light {
	Colour(0xf3f3f3), Colour(0xffffff), Colour(0x202020), Colour(0x666666),
	Colour(0xc5c5c5), Colour(0x087f68), Colour(0xbce6d7), Colour(0x101010),
	Colour(0xfde7e9),

	Colour(0xffffff), Colour(0xe9e9e9), Colour(0xd1d1d1), Colour(0xe9f5e8),
	Colour(0xe0f3ea), Colour(0xcfe4cf), Colour(0xcde9df), Colour(0xf2e2f2),
	Colour(0xf0f0f0), Colour(0x202020), Colour(0x101010), Colour(0xb42318),
	Colour(0x087f68),

	Colour(0xffffff), Colour(0x15876a), Colour(0x8a959d), Colour(0xc0e5d7),
	Colour(0xa15c00), Colour(0xc62828), Colour(0x7440a8),

	Colour(0x17876c), Colour(0x666666)
};

std::string Normalize(std::string_view name) {
	std::string normalized;
	normalized.reserve(name.size());
	for (unsigned char c : name) {
		if (std::isalnum(c))
			normalized.push_back(static_cast<char>(std::tolower(c)));
	}
	return normalized;
}

} // namespace

ThemeId ParseTheme(std::string_view name) {
	auto normalized = Normalize(name);
	if (normalized == "dark" || normalized == "darkplus")
		return ThemeId::DarkPlus;
	if (normalized == "deepgray" || normalized == "deepgrey")
		return ThemeId::DeepGray;
	if (normalized == "oled" || normalized == "oledblack")
		return ThemeId::OledBlack;
	if (normalized == "light")
		return ThemeId::Light;
	return ThemeId::Light;
}

std::string_view ThemeName(ThemeId id) {
	switch (id) {
		case ThemeId::DarkPlus: return "Dark+";
		case ThemeId::DeepGray: return "Deep Gray";
		case ThemeId::OledBlack: return "OLED Black";
		case ThemeId::Light: return "Light";
	}
	return "Light";
}

bool IsDark(ThemeId id) {
	return id != ThemeId::Light;
}

Palette const& GetPalette(ThemeId id) {
	switch (id) {
		case ThemeId::DarkPlus: return dark_plus;
		case ThemeId::DeepGray: return deep_gray;
		case ThemeId::OledBlack: return oled_black;
		case ThemeId::Light: return light;
	}
	return light;
}

} // namespace theme

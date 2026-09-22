// Minimal application globals and base-class methods needed by the ASS
// round-trip test. The exercised code always supplies an explicit encoding and
// does not load a style catalog.

#include "ass_file.h"
#include "ass_style.h"
#include "ass_style_storage.h"
#include "options.h"
#include "subtitle_format.h"

#include <iomanip>
#include <sstream>

namespace config {
	agi::Options *opt = nullptr;
}

SubtitleFormat::SubtitleFormat(std::string_view name) : name(name) { }
bool SubtitleFormat::CanReadFile(agi::fs::path const&, char const *) const { return false; }
bool SubtitleFormat::CanWriteFile(agi::fs::path const&) const { return false; }
bool SubtitleFormat::CanSave(AssFile const *) const { return false; }

AssStyleStorage::~AssStyleStorage() = default;
void AssStyleStorage::LoadCatalog(std::string_view) { }
bool AssStyleStorage::CatalogExists(std::string_view) { return false; }
void AssStyleStorage::ReplaceIntoFile(AssFile&) { }

std::string float_to_string(double value, int precision) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(precision) << value;
	auto result = stream.str();
	auto decimal = result.find('.');
	if (decimal != std::string::npos) {
		while (result.size() > decimal + 1 && result.back() == '0') result.pop_back();
		if (result.back() == '.') result.pop_back();
	}
	return result;
}

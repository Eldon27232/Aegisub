// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "ass_parser.h"

#include "ass_attachment.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_info.h"
#include "ass_style.h"
#include "subtitle_format.h"

#include <libaegisub/ass/string_codec.h>
#include <libaegisub/ass/uuencode.h>
#include <libaegisub/util.h>

#include <algorithm>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <cstring>
#include <exception>
#include <unordered_map>
#include <variant>

class AssParser::HeaderToProperty {
	using field = std::variant<
		std::string ProjectProperties::*,
		int ProjectProperties::*,
		double ProjectProperties::*
	>;
	std::unordered_map<std::string, field> fields;

public:
	HeaderToProperty()
	: fields({
		{"Automation Scripts", &ProjectProperties::automation_scripts},
		{"Export Filters", &ProjectProperties::export_filters},
		{"Export Encoding", &ProjectProperties::export_encoding},
		{"Last Style Storage", &ProjectProperties::style_storage},
		{"Audio URI", &ProjectProperties::audio_file},
		{"Audio File", &ProjectProperties::audio_file},
		{"Video File", &ProjectProperties::video_file},
		{"Timecodes File", &ProjectProperties::timecodes_file},
		{"Keyframes File", &ProjectProperties::keyframes_file},
		{"Video Zoom Percent", &ProjectProperties::video_zoom},
		{"Scroll Position", &ProjectProperties::scroll_position},
		{"Active Line", &ProjectProperties::active_row},
		{"Video Position", &ProjectProperties::video_position},
		{"Video AR Mode", &ProjectProperties::ar_mode},
		{"Video AR Value", &ProjectProperties::ar_value},
		{"Aegisub Video Zoom Percent", &ProjectProperties::video_zoom},
		{"Aegisub Scroll Position", &ProjectProperties::scroll_position},
		{"Aegisub Active Line", &ProjectProperties::active_row},
		{"Aegisub Video Position", &ProjectProperties::video_position}
	})
	{
	}

	bool ProcessProperty(AssFile *target, std::string const& key, std::string const& value) {
		auto it = fields.find(key);
		if (it != end(fields)) {
			using namespace agi::util;
			struct {
				using result_type = bool;
				ProjectProperties &obj;
				std::string const& value;
				bool operator()(std::string ProjectProperties::*f) const { obj.*f = value; return true; }
				bool operator()(int ProjectProperties::*f)         const { return try_parse(value, &(obj.*f)); }
				bool operator()(double ProjectProperties::*f)      const { return try_parse(value, &(obj.*f)); }
			} visitor {target->Properties, value};
			return std::visit(visitor, it->second);
		}

		if (key.starts_with("Automation Settings ")) {
			target->Properties.automation_settings[key.substr(strlen("Automation Settings "))] = value;
			return true;
		}

		return false;
	}

	std::string GetPropertyValue(AssFile const *target, std::string const& key) const {
		auto it = fields.find(key);
		if (it != end(fields)) {
			struct {
				using result_type = std::string;
				ProjectProperties const& obj;
				std::string operator()(std::string ProjectProperties::*f) const { return obj.*f; }
				std::string operator()(int ProjectProperties::*f) const { return std::to_string(obj.*f); }
				std::string operator()(double ProjectProperties::*f) const { return std::to_string(obj.*f); }
			} visitor {target->Properties};
			return std::visit(visitor, it->second);
		}

		if (key.starts_with("Automation Settings ")) {
			auto setting = target->Properties.automation_settings.find(key.substr(strlen("Automation Settings ")));
			if (setting != target->Properties.automation_settings.end())
				return setting->second;
		}

		return {};
	}
};

AssParser::AssParser(AssFile *target, int version)
: property_handler(std::make_unique<HeaderToProperty>())
, target(target)
, version(version)
, state(&AssParser::ParseScriptInfoLine)
, section(AssFileSection::SCRIPT_INFO)
{
}

AssParser::~AssParser() = default;

AssParser::ParsedLine AssParser::ParseAttachmentLine(std::string const& rawdata) {
	std::string data = rawdata;
	boost::trim(data);
	bool is_filename = data.starts_with("fontname: ") || data.starts_with("filename: ");

	bool valid_data = data.size() > 0 && data.size() <= 80;
	for (auto byte : data) {
		if (byte < 33 || byte >= 97) {
			valid_data = false;
			break;
		}
	}

	// Data is over, add attachment to the file
	if (!valid_data || is_filename) {
		target->Attachments.push_back(*attach.release());
		AddLine(rawdata);
	}
	else {
		attach->AddData(data);

		// Done building
		if (data.size() < 80)
			target->Attachments.push_back(*attach.release());
	}
	return ParsedLine::CONSUMED;
}

AssParser::ParsedLine AssParser::ParseScriptInfoLine(std::string const& data) {
	if (data.starts_with(";")) {
		return ParsedLine::RAW;
	}

	if (data.starts_with("ScriptType:")) {
		std::string version_str = data.substr(11);
		boost::trim(version_str);
		boost::to_lower(version_str);
		if (version_str == "v4.00")
			version = 0;
		else if (version_str == "v4.00+")
			version = 1;
		else
			return ParsedLine::RAW;
	}

	// Nothing actually supports the Collisions property and malformed values
	// crash VSFilter. Keep the original line as passthrough rather than adding
	// it to the structured Script Info model.
	if (data.starts_with("Collisions:"))
		return ParsedLine::RAW;

	size_t pos = data.find(':');
	if (pos == data.npos) return ParsedLine::RAW;

	auto key = data.substr(0, pos);
	auto value = data.substr(pos + 1);
	boost::trim_left(value);

	parsed_key = key;
	if (!property_handler->ProcessProperty(target, key, value)) {
		target->Info.push_back(*new AssInfo(std::move(key), std::move(value)));
		parsed_value = target->Info.back().GetEntryData();
		return ParsedLine::SCRIPT_INFO;
	}

	parsed_value = property_handler->GetPropertyValue(target, parsed_key);
	return ParsedLine::PROJECT_PROPERTY;
}

AssParser::ParsedLine AssParser::ParseMetadataLine(std::string const& rawdata) {
	std::string data = SanitizeLine(rawdata);

	size_t pos = data.find(':');
	if (pos == data.npos) return ParsedLine::RAW;

	auto key = data.substr(0, pos);
	auto value = data.substr(pos + 1);
	boost::trim_left(value);

	if (!property_handler->ProcessProperty(target, key, value))
		return ParsedLine::RAW;

	parsed_key = key;
	parsed_value = property_handler->GetPropertyValue(target, parsed_key);
	return ParsedLine::PROJECT_PROPERTY;
}

AssParser::ParsedLine AssParser::ParseEventLine(std::string const& data) {
	if (!data.starts_with("Dialogue:") && !data.starts_with("Comment:"))
		return ParsedLine::RAW;

	try {
		target->Events.push_back(*new AssDialogue(data));
		parsed_value = target->Events.back().GetEntryData();
		return ParsedLine::EVENT;
	}
	catch (SubtitleFormatParseError const&) {
		return ParsedLine::RAW;
	}
	catch (std::exception const&) {
		return ParsedLine::RAW;
	}
}

AssParser::ParsedLine AssParser::ParseStyleLine(std::string const& data) {
	if (!data.starts_with("Style:"))
		return ParsedLine::RAW;

	try {
		target->Styles.push_back(*new AssStyle(data, version));
		parsed_value = target->Styles.back().GetEntryData();
		return ParsedLine::STYLE;
	}
	catch (SubtitleFormatParseError const&) {
		return ParsedLine::RAW;
	}
	catch (std::exception const&) {
		return ParsedLine::RAW;
	}
}

AssParser::ParsedLine AssParser::ParseFontLine(std::string const& data) {
	if (data.starts_with("fontname: ")) {
		attach = std::make_unique<AssAttachment>(data, AssEntryGroup::FONT);
		return ParsedLine::ATTACHMENT;
	}
	return ParsedLine::RAW;
}

AssParser::ParsedLine AssParser::ParseGraphicsLine(std::string const& data) {
	if (data.starts_with("filename: ")) {
		attach = std::make_unique<AssAttachment>(data, AssEntryGroup::GRAPHIC);
		return ParsedLine::ATTACHMENT;
	}
	return ParsedLine::RAW;
}

AssParser::ParsedLine AssParser::ParseExtradataLine(std::string const &rawdata) {
	std::string data = SanitizeLine(rawdata);

	static const boost::regex matcher("Data:[[:space:]]*(\\d+),([^,]+),(.)(.*)");
	boost::match_results<std::string::const_iterator> mr;

	if (boost::regex_match(data, mr, matcher)) {
		auto id = boost::lexical_cast<uint32_t>(mr.str(1));
		auto key = agi::ass::inline_string_decode(mr.str(2));
		auto valuetype = mr.str(3);
		auto value = mr.str(4);
		if (valuetype == "e") {
			// escaped/inline_string encoded
			value = agi::ass::inline_string_decode(value);
		} else if (valuetype == "u") {
			// ass uuencoded
			auto valuedata = agi::ass::UUDecode(value.c_str(), value.c_str() + value.size());
			value = std::string(valuedata.begin(), valuedata.end());
		} else {
			// unknown, error?
			value = "";
		}

		// ensure next_extradata_id is always at least 1 more than the largest existing id
		target->next_extradata_id = std::max(id+1, target->next_extradata_id);
		target->Extradata.push_back(ExtradataEntry{id, EXTRADATA_EXPIRATION_LIMIT + 1, std::move(key), std::move(value)});
		auto const& entry = target->Extradata.back();
		parsed_key = std::to_string(entry.id);
		parsed_value = parsed_key + "\n" + entry.key + "\n" + entry.value;
		return ParsedLine::EXTRADATA;
	}

	return ParsedLine::RAW;
}

std::string AssParser::SanitizeLine(std::string const& data) {
	std::string result = data;
	boost::replace_all(result, std::string("\0", 1), "\uFFFD");		// Unicode replacement character
	return result;
}

void AssParser::AddLine(std::string const& data) {
	// Special-case for attachments since a line could theoretically be both a
	// valid attachment data line and a valid section header, and if an
	// attachment is in progress it needs to be treated as that
	if (attach.get()) {
		ParseAttachmentLine(data);
		return;
	}

	// TextFileReader does not trim lines for ASS files, so that passthrough can
	// retain their exact spelling. Parsing still uses the historically trimmed
	// representation.
	std::string parsed_data = data;
	boost::trim(parsed_data);

	// Section header
	if (parsed_data.size() >= 2 && parsed_data[0] == '[' && parsed_data.back() == ']') {
		// Ugly hacks to allow intermixed v4 and v4+ style sections
		const std::string low = boost::to_lower_copy(parsed_data);
		if (low == "[v4 styles]") {
			version = 0;
			state = &AssParser::ParseStyleLine;
			section = AssFileSection::STYLES;
		}
		else if (low == "[v4+ styles]") {
			version = 1;
			state = &AssParser::ParseStyleLine;
			section = AssFileSection::STYLES;
		}
		else if (low == "[events]") {
			state = &AssParser::ParseEventLine;
			section = AssFileSection::EVENTS;
		}
		else if (low == "[script info]") {
			state = &AssParser::ParseScriptInfoLine;
			section = AssFileSection::SCRIPT_INFO;
		}
		else if (low == "[aegisub project garbage]") {
			state = &AssParser::ParseMetadataLine;
			section = AssFileSection::PROJECT;
		}
		else if (low == "[aegisub extradata]") {
			state = &AssParser::ParseExtradataLine;
			section = AssFileSection::EXTRADATA;
		}
		else if (low == "[graphics]") {
			state = &AssParser::ParseGraphicsLine;
			section = AssFileSection::GRAPHICS;
		}
		else if (low == "[fonts]") {
			state = &AssParser::ParseFontLine;
			section = AssFileSection::FONTS;
		}
		else {
			state = &AssParser::UnknownLine;
			section = AssFileSection::UNKNOWN;
		}

		target->Passthrough.push_back({AssFilePassthroughType::SECTION, section, data, {}, {}});
		return;
	}

	parsed_key.clear();
	parsed_value.clear();
	auto result = (this->*state)(parsed_data);
	if (result == ParsedLine::CONSUMED)
		return;

	AssFilePassthroughType type = AssFilePassthroughType::RAW;
	switch (result) {
		case ParsedLine::SCRIPT_INFO: type = AssFilePassthroughType::SCRIPT_INFO; break;
		case ParsedLine::PROJECT_PROPERTY: type = AssFilePassthroughType::PROJECT_PROPERTY; break;
		case ParsedLine::STYLE: type = AssFilePassthroughType::STYLE; break;
		case ParsedLine::EVENT: type = AssFilePassthroughType::EVENT; break;
		case ParsedLine::ATTACHMENT: type = AssFilePassthroughType::ATTACHMENT; break;
		case ParsedLine::EXTRADATA: type = AssFilePassthroughType::EXTRADATA; break;
		case ParsedLine::RAW:
		case ParsedLine::CONSUMED: break;
	}
	target->Passthrough.push_back({type, section, data, parsed_key, parsed_value});
}

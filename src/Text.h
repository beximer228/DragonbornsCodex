#pragma once

namespace Text
{
	// Game strings come either as UTF-8 or as cp1251 (Russian localisation);
	// ImGui needs UTF-8.
	std::string ToUtf8(const char* a_str);

	// Lower-cases ASCII and Cyrillic letters in a UTF-8 string (for search).
	std::string ToLower(std::string_view a_utf8);

	bool Contains(std::string_view a_haystackLower, std::string_view a_needleLower);
}

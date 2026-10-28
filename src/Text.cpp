#include "Text.h"

namespace Text
{
	namespace
	{
		// cp1251 0x80..0xBF; 0xC0..0xFF map linearly to U+0410..U+044F
		constexpr char32_t kCp1251High[64] = {
			0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
			0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x003F, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
			0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
			0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457
		};

		void Append(std::string& a_out, char32_t a_cp)
		{
			if (a_cp < 0x80) {
				a_out += static_cast<char>(a_cp);
			} else if (a_cp < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_cp >> 6));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else if (a_cp < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_cp >> 12));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_cp >> 18));
				a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
		}

		// Decodes one code point; returns bytes consumed or 0 on invalid UTF-8.
		std::size_t Decode(std::string_view a_str, std::size_t a_pos, char32_t& a_cp)
		{
			const auto c = static_cast<unsigned char>(a_str[a_pos]);
			std::size_t len = 0;
			if (c < 0x80) {
				a_cp = c;
				return 1;
			} else if ((c & 0xE0) == 0xC0) {
				len = 2;
				a_cp = c & 0x1F;
			} else if ((c & 0xF0) == 0xE0) {
				len = 3;
				a_cp = c & 0x0F;
			} else if ((c & 0xF8) == 0xF0) {
				len = 4;
				a_cp = c & 0x07;
			} else {
				return 0;
			}
			if (a_pos + len > a_str.size()) {
				return 0;
			}
			for (std::size_t i = 1; i < len; ++i) {
				const auto cc = static_cast<unsigned char>(a_str[a_pos + i]);
				if ((cc & 0xC0) != 0x80) {
					return 0;
				}
				a_cp = (a_cp << 6) | (cc & 0x3F);
			}
			return len;
		}

		bool IsValidUtf8(std::string_view a_str)
		{
			char32_t cp;
			for (std::size_t i = 0; i < a_str.size();) {
				const auto n = Decode(a_str, i, cp);
				if (n == 0) {
					return false;
				}
				i += n;
			}
			return true;
		}
	}

	std::string ToUtf8(const char* a_str)
	{
		if (!a_str) {
			return {};
		}
		std::string_view str{ a_str };
		if (IsValidUtf8(str)) {
			return std::string{ str };
		}
		std::string out;
		out.reserve(str.size() * 2);
		for (const auto ch : str) {
			const auto c = static_cast<unsigned char>(ch);
			if (c < 0x80) {
				out += static_cast<char>(c);
			} else if (c < 0xC0) {
				Append(out, kCp1251High[c - 0x80]);
			} else {
				Append(out, 0x0410 + (c - 0xC0));
			}
		}
		return out;
	}

	std::string ToLower(std::string_view a_utf8)
	{
		std::string out;
		out.reserve(a_utf8.size());
		char32_t cp;
		for (std::size_t i = 0; i < a_utf8.size();) {
			auto n = Decode(a_utf8, i, cp);
			if (n == 0) {
				cp = static_cast<unsigned char>(a_utf8[i]);
				n = 1;
			}
			if (cp >= U'A' && cp <= U'Z') {
				cp += 0x20;
			} else if (cp >= 0x0410 && cp <= 0x042F) {
				cp += 0x20;
			} else if (cp >= 0x0400 && cp <= 0x040F) {
				cp += 0x50;
			}
			Append(out, cp);
			i += n;
		}
		return out;
	}

	bool Contains(std::string_view a_haystackLower, std::string_view a_needleLower)
	{
		return a_needleLower.empty() || a_haystackLower.find(a_needleLower) != std::string_view::npos;
	}
}

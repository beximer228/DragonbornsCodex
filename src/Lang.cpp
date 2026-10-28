#include "Lang.h"

#include "Settings.h"

namespace Lang
{
	namespace
	{
		const std::filesystem::path kDir{ "Data/SKSE/Plugins/DragonbornsCodex/lang" };

		using Table = std::unordered_map<std::string, std::string>;

		Table              g_tables[2];
		std::atomic<int>   g_current{ static_cast<int>(Id::kRussian) };
		std::mutex         g_lock;
		Table              g_suffixed[2];  // "key##id" -> "text##id", filled on demand, never erased

		Table ReadFile(const std::filesystem::path& a_path)
		{
			Table table;
			std::ifstream file{ a_path, std::ios::binary };
			if (!file) {
				logger::error("Lang: cannot read {}", a_path.string());
				return table;
			}
			for (std::string line; std::getline(file, line);) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.starts_with("\xEF\xBB\xBF")) {
					line.erase(0, 3);
				}
				const auto eq = line.find('=');
				if (line.empty() || line[0] == '#' || line[0] == ';' || eq == std::string::npos) {
					continue;
				}
				std::string value = line.substr(eq + 1);
				for (std::size_t i = 0; (i = value.find("\\n", i)) != std::string::npos;) {
					value.replace(i, 2, "\n");
					++i;
				}
				table.insert_or_assign(line.substr(0, eq), std::move(value));
			}
			return table;
		}

		// "section.name": lower-case latin, digits, '_' and at least one '.'.
		bool IsKey(std::string_view a_text)
		{
			if (a_text.empty() || a_text.front() == '.' || a_text.back() == '.') {
				return false;
			}
			bool dot = false;
			for (const char c : a_text) {
				if (c == '.') {
					dot = true;
				} else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
					return false;
				}
			}
			return dot;
		}

		const std::string* Find(int a_lang, std::string_view a_key)
		{
			const auto& table = g_tables[a_lang];
			const auto  it = table.find(std::string{ a_key });
			if (it != table.end()) {
				return &it->second;
			}
			const auto& english = g_tables[static_cast<int>(Id::kEnglish)];
			const auto  en = english.find(std::string{ a_key });
			return en != english.end() ? &en->second : nullptr;
		}
	}

	void Load()
	{
		g_tables[static_cast<int>(Id::kEnglish)] = ReadFile(kDir / "en.ini");
		g_tables[static_cast<int>(Id::kRussian)] = ReadFile(kDir / "ru.ini");

		auto lang = Settings::Get().language;
		if (lang.empty()) {
			const auto setting = RE::GetINISetting("sLanguage:General");
			lang = setting && setting->GetString() && std::string_view{ setting->GetString() } == "RUSSIAN" ? "ru" : "en";
		}
		g_current = static_cast<int>(lang == "ru" ? Id::kRussian : Id::kEnglish);
		logger::info("Lang: {} strings (en), {} (ru), using {}", g_tables[0].size(), g_tables[1].size(), lang);
	}

	Id Current() { return static_cast<Id>(g_current.load()); }

	void Set(Id a_lang)
	{
		g_current = static_cast<int>(a_lang);
		Settings::Get().language = a_lang == Id::kRussian ? "ru" : "en";
		Settings::Save();
	}

	const char* Tr(const char* a_key)
	{
		if (!a_key) {
			return "";
		}
		const std::string_view text{ a_key };
		const auto             hash = text.find("##");
		const auto             key = text.substr(0, hash);
		if (!IsKey(key)) {
			return a_key;
		}
		const int lang = g_current;
		if (hash == std::string_view::npos) {
			const auto* value = Find(lang, key);
			return value ? value->c_str() : a_key;
		}
		std::scoped_lock lock{ g_lock };
		auto&            cache = g_suffixed[lang];
		auto             it = cache.find(std::string{ text });
		if (it == cache.end()) {
			const auto* value = Find(lang, key);
			it = cache.emplace(std::string{ text }, (value ? *value : std::string{ key }) + std::string{ text.substr(hash) }).first;
		}
		return it->second.c_str();
	}

	std::string VFmt(const char* a_key, std::format_args a_args)
	{
		try {
			return std::vformat(Tr(a_key), a_args);
		} catch (const std::format_error&) {
			const auto* english = IsKey(a_key) ? Find(static_cast<int>(Id::kEnglish), a_key) : nullptr;
			try {
				return english ? std::vformat(*english, a_args) : std::string{ a_key };
			} catch (const std::format_error&) {
				return a_key;
			}
		}
	}
}

#pragma once

// Menu text in English and Russian: Data/SKSE/Plugins/CheatMenu/lang/en.ini and ru.ini, "key=text" lines with
// the same keys in both. Code refers to strings by key ("section.name"); anything that is not a key passes through.
namespace Lang
{
	enum class Id
	{
		kEnglish,
		kRussian
	};

	// Reads both files and picks the language from Settings (empty = the game's sLanguage).
	void Load();

	Id   Current();
	void Set(Id a_lang);  // also saves the choice to Settings

	// Text for a key in the current language; the input itself when it is not a key or the key is missing.
	// A "##id" / "###id" suffix is kept. The pointer stays valid for the whole session.
	const char* Tr(const char* a_key);

	// std::format with a translated format string; a broken translation falls back to the English one.
	std::string VFmt(const char* a_key, std::format_args a_args);

	template <class... Args>
	std::string Fmt(const char* a_key, const Args&... a_args)
	{
		return VFmt(a_key, std::make_format_args(a_args...));
	}
}

using Lang::Tr;

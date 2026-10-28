#pragma once

namespace Settings
{
	// A user-made console macro; several commands are separated by ';'.
	struct Command
	{
		std::string name;
		std::string command;
	};

	// A user item set: catalog keys with counts.
	struct Kit
	{
		std::string                              name;
		std::vector<std::pair<std::string, int>> items;
	};

	// ESP mark kinds, in Esp::Kind order: ids for the ini and the default look.
	inline constexpr std::size_t kEspKinds = 11;
	inline constexpr const char* kEspKindIds[kEspKinds] = { "enemy", "npc", "follower", "corpse", "container", "item", "flora", "quest", "interact", "trap", "puzzle" };
	inline constexpr std::uint32_t kEspDefaultColors[kEspKinds] = {  // ImGui ABGR
		0xFF3C48E8, 0xFFBEDEEC, 0xFF69CD6E, 0xDC969696, 0xFF52B2DE, 0xFFF0AA69, 0xFFBECD5F, 0xFF3CD2FF, 0xFFF58CB9, 0xFF2878FF, 0xFFFFDC3C
	};
	inline constexpr int kEspDefaultShapes[kEspKinds] = { 0, 0, 0, 0, 1, 2, 3, 4, 1, 3, 0 };  // 0 diamond, 1 square, 2 circle, 3 triangle, 4 star

	// How ESP marks look.
	struct EspLook
	{
		float iconScale{ 1.0f };
		float textScale{ 1.0f };
		float opacity{ 1.0f };
		float lineScale{ 1.0f };  // wallhack boxes, skeleton, tracers
		bool  names{ true };
		bool  distance{ true };
		std::array<std::uint32_t, kEspKinds> colors = std::to_array(kEspDefaultColors);
		std::array<int, kEspKinds>           shapes = std::to_array(kEspDefaultShapes);
	};

	// Per-user menu settings (not per save): look, keys, favourites. Kept in
	// Documents\My Games\Skyrim Special Edition\SKSE\DragonbornsCodex.ini (CheatMenu.ini before the rename).
	struct Data
	{
		bool                                 dark{ false };
		bool                                 compact{ false };  // tighter rows and cards
		float                                uiScale{ 1.0f };  // on top of the resolution-based scale
		std::array<float, 4>                 window{};          // x, y, w, h as parts of the screen, dragged by its edges; w 0: default
		std::uint32_t                        toggleKey{ 0xD2 };  // DIK_INSERT
		std::map<std::string, std::uint32_t> hotkeys;            // Hotkeys action id -> DIK code
		std::set<std::string>                favorites;          // Catalog keys, see Key()
		std::vector<std::string>             recent;             // newest first
		std::vector<Command>                 commands;
		std::vector<Kit>                     kits;
		bool                                 testBridge{ false };  // see TestBridge.h
		std::string                          language;  // "ru", "en" or empty = the game's language
		EspLook                              esp;

		// Fonts: a path relative to Data/SKSE/Plugins/CheatMenu or an absolute one; sizes in pixels at 1080p.
		std::string fontBody{ "fonts/Jost-Regular.ttf" };
		std::string fontHeading{ "fonts/Oswald-Regular.ttf" };
		std::string fontTitle{ "fonts/Oswald-Light.ttf" };
		float       fontBodySize{ 19.0f };
		float       fontHeadingSize{ 24.0f };
		float       fontTitleSize{ 38.0f };
	};

	Data& Get();
	void  Load();
	void  Save();

	// Load-order independent id of a form: "Plugin.esp:LocalID".
	std::string Key(std::string_view a_plugin, RE::FormID a_formID);

	void AddRecent(std::string a_key);
}

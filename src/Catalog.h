#pragma once

namespace Catalog
{
	enum class Category
	{
		kWeapon,
		kArmor,
		kAmmo,
		kPotion,
		kIngredient,
		kBook,
		kScroll,
		kSoulGem,
		kKey,
		kMisc,
		kSpell,
		kShout,
		kPerk,
		kNPC,

		kTotal
	};

	const char* CategoryName(Category a_cat);

	// Which menu page a category belongs to.
	enum class Group
	{
		kItems,
		kMagic,
		kActors
	};
	Group GroupOf(Category a_cat);

	struct Entry
	{
		RE::FormID  formID;
		Category    category;
		std::string name;
		std::string nameLower;
		std::string plugin;
	};

	struct Marker
	{
		RE::ObjectRefHandle ref;
		std::string         name;
		std::string         nameLower;
		std::string         world;
	};

	struct Weather
	{
		RE::FormID  formID;
		const char* kind;   // lang key: snow, rain...
		std::string label;  // "ID · plugin"
	};

	// Forms from every loaded plugin (Skyrim.esm, DLC, Creation Club and all mods).
	void BuildForms();
	const std::vector<Entry>&       Forms();
	const std::vector<std::string>& Plugins();

	// Map markers live in persistent cells, so they need a loaded save.
	void BuildMarkers();
	const std::vector<Marker>& Markers();

	const std::vector<Weather>& Weathers();

	// The shouts the player can really have (no dragon / NPC copies), one per word triple.
	std::vector<RE::TESShout*> PlayerShouts();
}

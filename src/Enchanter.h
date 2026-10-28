#pragma once

// The "Зачарование" page: put any enchantments, as strong as wanted, on any weapon or armour in the player's inventory.
namespace Enchanter
{
	enum class Kind
	{
		kWeapon,  // melee and bows: on-hit enchantments
		kStaff,   // staffs: aimed / self spells
		kArmor    // armour, clothes, jewellery: constant effects
	};

	// One magic effect of an enchantment, with the strength the menu will give it.
	struct EffectSpec
	{
		RE::FormID  source;  // the enchantment the effect is taken from (for its conditions)
		RE::FormID  effect;  // MGEF
		std::string name;
		float       magnitude{ 0.0f };
		float       maxMagnitude{ 100.0f };  // slider range
		float       duration{ 0.0f };
		bool        hasMagnitude{ true };
		bool        hasDuration{ false };
	};

	// The page's dropdown filters, in display order; index 0 is "all".
	const std::vector<const char*>& ItemCategories();
	const std::vector<const char*>& OptionCategories();

	// An enchantment to choose from: the game's base enchantments of every plugin, one per base.
	struct Option
	{
		RE::FormID              enchantment;
		Kind                    kind;
		std::string             name;
		std::string             nameLower;
		std::string             plugin;
		std::vector<EffectSpec> effects;  // with the strongest values any variant of it has in the game
		std::uint32_t           categories{ 0 };  // bit i: OptionCategories()[i]
	};
	const std::vector<Option>& Options();

	// One item of the player's inventory: every distinct instance (worn, tempered, enchanted) separately.
	struct Item
	{
		RE::FormID                object;
		const RE::ExtraDataList*  list;  // null = a plain one from the stack
		Kind                      kind;
		std::string               name;
		std::string               nameLower;
		int                       count;
		bool                      worn;
		std::vector<std::string>  enchantments;  // the effects it has now
		std::vector<EffectSpec>   current;       // the same, editable
		float                     charge;        // weapons: maximum charge
		bool                      ownEnchantment{ false };  // its extra data has an enchantment (table, this menu)
		std::size_t               category{ 0 };            // ItemCategories() index
		bool                      questItem{ false };       // a quest alias holds it: never swapped for its plain version
	};
	void              RefreshItems();  // a snapshot taken on the game thread; Items() has it a frame later
	std::vector<Item> Items();

	// Puts exactly these effects on the item (replacing its enchantment); empty name keeps the name. Returns a status line.
	std::string Apply(const Item& a_item, const std::vector<EffectSpec>& a_effects, const std::string& a_name, float a_charge);

	// Worn item: takes it off and puts it on again, so a changed enchantment's effects come on (game thread only).
	// a_charge: the weapon's charge left, set while it is off (worn, the hand keeps its own and writes it back on unequip).
	void Reequip(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, std::optional<float> a_charge = std::nullopt);

	// Puts the enchantment on the item; when its base form has one of its own (which the game would use instead), that is
	// replaced too and remembered. Returns the item's extra list (game thread only).
	RE::ExtraDataList* SetEnchantment(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, RE::EnchantmentItem* a_enchantment,
		std::uint16_t a_charge);

	// Puts the enchantment on the item, full charge: a common item with a built-in one is swapped for its plain version first
	// (tempering and name kept), a unique / quest one gets its base form's replaced (SetEnchantment); worn items are put on
	// again. Empty name keeps the name. Returns the enchanted item's extra list (game thread only).
	RE::ExtraDataList* Enchant(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, RE::EnchantmentItem* a_enchantment,
		std::uint16_t a_charge, const std::string& a_name = {});

	// Takes the enchantment off: a replaced base-form one gets its original back, an own one is removed, a base-form one
	// (Iron Sword of Fear) swaps the item for its plain version.
	std::string Remove(const Item& a_item);

	// The replaced base-form enchantments, in the co-save (record written by Cheats).
	void Save(SKSE::SerializationInterface* a_intfc);
	void Load(SKSE::SerializationInterface* a_intfc);
	void Revert();

	// A created enchantment with these effects (deep copies of each source effect's conditions); null if the game refused.
	RE::EnchantmentItem* Create(bool a_weapon, const std::vector<EffectSpec>& a_effects);
}

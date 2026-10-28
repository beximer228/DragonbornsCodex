#include "ItemIcons.h"

#include "Textures.h"

namespace ItemIcons
{
	namespace
	{
		// The atlas cells, in tools/make_item_icons.py order (the swf's frame labels).
		constexpr std::string_view kCells[] = { "default_weapon", "weapon_sword", "weapon_greatsword", "weapon_daedra", "weapon_dagger", "weapon_waraxe",
			"weapon_battleaxe", "weapon_mace", "weapon_hammer", "weapon_staff", "weapon_bow", "weapon_arrow", "weapon_pickaxe", "weapon_woodaxe",
			"weapon_crossbow", "weapon_bolt", "default_armor", "lightarmor_body", "lightarmor_head", "lightarmor_hands", "lightarmor_forearms",
			"lightarmor_feet", "lightarmor_calves", "lightarmor_shield", "lightarmor_mask", "armor_body", "armor_head", "armor_hands", "armor_forearms",
			"armor_feet", "armor_calves", "armor_shield", "armor_mask", "armor_bracer", "armor_daedra", "clothing_body", "clothing_robe", "clothing_head",
			"clothing_pants", "clothing_hands", "clothing_forearms", "clothing_feet", "clothing_calves", "clothing_shoes", "clothing_shield",
			"clothing_mask", "armor_amulet", "armor_ring", "armor_circlet", "default_scroll", "default_book", "default_book_read", "book_tome",
			"book_tome_read", "book_journal", "book_note", "book_map", "default_food", "food_wine", "food_beer", "default_ingredient", "default_key",
			"key_house", "default_potion", "potion_health", "potion_stam", "potion_magic", "potion_poison", "potion_frost", "potion_fire",
			"potion_shock", "default_misc", "misc_artifact", "misc_clutter", "misc_lockpick", "misc_soulgem", "soulgem_empty", "soulgem_partial",
			"soulgem_full", "soulgem_grandempty", "soulgem_grandpartial", "soulgem_grandfull", "soulgem_azura", "misc_gem", "misc_ore", "misc_ingot",
			"misc_hide", "misc_strips", "misc_leather", "misc_wood", "misc_remains", "misc_trollskull", "misc_torch", "misc_goldsack", "misc_gold",
			"misc_dragonclaw", "default_alteration", "default_illusion", "default_destruction", "default_conjuration", "default_restoration",
			"default_shout", "default_power", "default_effect", "magic_fire", "magic_frost", "magic_shock", "weapon_spear", "weapon_javelin",
			"weapon_pike", "weapon_halberd", "weapon_rapier", "weapon_quarterstaff", "weapon_claw", "weapon_whip", "weapon_katana", "weapon_scythe",
			"weapon_gun" };
		constexpr int kCols = 12;
		constexpr int kRows = (static_cast<int>(std::size(kCells)) + kCols - 1) / kCols;

		struct Glyph
		{
			int   cell{ -1 };
			ImU32 tint{ 0 };  // 0: the caller's
		};

		constexpr ImU32 Rgb(std::uint32_t a_rgb) { return IM_COL32((a_rgb >> 16) & 0xFF, (a_rgb >> 8) & 0xFF, a_rgb & 0xFF, 255); }
		// mid tones, readable on the dark panels and on the parchment
		constexpr ImU32 kRed = Rgb(0xC0443A), kBlue = Rgb(0x3F7FC8), kGreen = Rgb(0x4E9A4A), kPoison = Rgb(0x7C9A2E);
		constexpr ImU32 kFire = Rgb(0xD0702C), kFrost = Rgb(0x58A6C8), kShock = Rgb(0x9A72D0), kGold = Rgb(0xC9A23A);
		constexpr ImU32 kAlteration = Rgb(0x3E9A8E), kIllusion = Rgb(0xB05E9E), kConjuration = Rgb(0x8462B0), kRestoration = Rgb(0xC9A23A);

		int Cell(std::string_view a_name)
		{
			const auto it = std::ranges::find(kCells, a_name);
			return it == std::end(kCells) ? 0 : static_cast<int>(it - std::begin(kCells));
		}

		bool Has(const RE::BGSKeywordForm* a_form, RE::FormID a_keyword)
		{
			const auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(a_keyword);
			return a_form && keyword && a_form->HasKeyword(keyword);
		}

		Glyph Weapon(RE::TESObjectWEAP* a_weap)
		{
			using T = RE::WEAPON_TYPE;
			switch (a_weap->GetFormID()) {
			case 0x000E3C16: return { Cell("weapon_pickaxe") };   // Pickaxe
			case 0x0002F2F4: return { Cell("weapon_woodaxe") };   // Woodcutter's Axe
			default: break;
			}
			switch (a_weap->GetWeaponType()) {
			case T::kHandToHandMelee: return { Cell("weapon_claw") };
			case T::kOneHandSword: return { Cell("weapon_sword") };
			case T::kOneHandDagger: return { Cell("weapon_dagger") };
			case T::kOneHandAxe: return { Cell("weapon_waraxe") };
			case T::kOneHandMace: return { Cell("weapon_mace") };
			case T::kTwoHandSword: return { Cell("weapon_greatsword") };
			case T::kTwoHandAxe:
				return { Cell(Has(a_weap, 0x0006D930 /* WeapTypeWarhammer */) ? "weapon_hammer" : "weapon_battleaxe") };
			case T::kBow: return { Cell("weapon_bow") };
			case T::kStaff: return { Cell("weapon_staff") };
			case T::kCrossbow: return { Cell("weapon_crossbow") };
			default: return { Cell("default_weapon") };
			}
		}

		Glyph Armor(RE::TESObjectARMO* a_armor)
		{
			using S = RE::BGSBipedObjectForm::BipedObjectSlot;
			const auto  slots = a_armor->GetSlotMask();
			auto        has = [&](S a_slot) { return (slots & a_slot) != S::kNone; };
			const auto  type = a_armor->GetArmorType();
			const char* kind = type == RE::BIPED_MODEL::ArmorType::kLightArmor ? "lightarmor_" :
			                   type == RE::BIPED_MODEL::ArmorType::kHeavyArmor ? "armor_" :
			                                                                     "clothing_";
			auto piece = [&](const char* a_part) { return Glyph{ Cell(std::string{ kind } + a_part) }; };
			if (a_armor->IsShield() || has(S::kShield)) {
				return piece("shield");
			}
			if (has(S::kBody)) {
				return piece("body");
			}
			if (has(S::kHead) || has(S::kHair)) {
				return piece("head");
			}
			if (has(S::kHands)) {
				return piece("hands");
			}
			if (has(S::kForearms)) {
				return piece("forearms");
			}
			if (has(S::kFeet)) {
				return piece("feet");
			}
			if (has(S::kCalves)) {
				return piece("calves");
			}
			if (has(S::kAmulet)) {
				return { Cell("armor_amulet"), kGold };
			}
			if (has(S::kRing)) {
				return { Cell("armor_ring"), kGold };
			}
			if (has(S::kCirclet)) {
				return { Cell("armor_circlet"), kGold };
			}
			return { Cell("default_armor") };
		}

		Glyph Potion(RE::AlchemyItem* a_potion)
		{
			if (a_potion->IsFood()) {
				return { Cell("default_food") };
			}
			if (a_potion->IsPoison()) {
				return { Cell("potion_poison"), kPoison };
			}
			const auto effect = a_potion->GetCostliestEffectItem();
			const auto av = effect && effect->baseEffect ? effect->baseEffect->data.primaryAV : RE::ActorValue::kNone;
			switch (av) {
			case RE::ActorValue::kHealth: return { Cell("potion_health"), kRed };
			case RE::ActorValue::kMagicka: return { Cell("potion_magic"), kBlue };
			case RE::ActorValue::kStamina: return { Cell("potion_stam"), kGreen };
			case RE::ActorValue::kResistFire: return { Cell("potion_fire"), kFire };
			case RE::ActorValue::kResistFrost: return { Cell("potion_frost"), kFrost };
			case RE::ActorValue::kResistShock: return { Cell("potion_shock"), kShock };
			default: return { Cell("default_potion") };
			}
		}

		Glyph SoulGem(RE::TESSoulGem* a_gem)
		{
			if (a_gem->GetFormID() == 0x00063B27 || a_gem->GetFormID() == 0x00063B29) {  // Azura's Star, the Black Star
				return { Cell("soulgem_azura"), kConjuration };
			}
			const bool  grand = a_gem->soulCapacity >= RE::SOUL_LEVEL::kGrand;
			const auto  soul = a_gem->GetContainedSoul();
			const char* fill = soul == RE::SOUL_LEVEL::kNone ? "empty" : soul >= a_gem->soulCapacity ? "full" : "partial";
			return { Cell(std::string{ grand ? "soulgem_grand" : "soulgem_" } + fill), kConjuration };
		}

		Glyph Misc(RE::TESObjectMISC* a_misc)
		{
			switch (a_misc->GetFormID()) {
			case 0x0000000F: return { Cell("misc_gold"), kGold };
			case 0x0000000A: return { Cell("misc_lockpick") };
			default: break;
			}
			if (Has(a_misc, 0x000914ED)) {  // VendorItemGem
				return { Cell("misc_gem"), kGold };
			}
			if (Has(a_misc, 0x000914EC)) {  // VendorItemOreIngot
				return { Cell("misc_ingot") };
			}
			if (Has(a_misc, 0x000914EA)) {  // VendorItemAnimalHide
				return { Cell("misc_hide") };
			}
			if (Has(a_misc, 0x000914EB)) {  // VendorItemAnimalPart
				return { Cell("misc_remains") };
			}
			if (Has(a_misc, 0x000BECD7)) {  // VendorItemFirewood
				return { Cell("misc_wood") };
			}
			if (Has(a_misc, 0x000914E9)) {  // VendorItemClutter
				return { Cell("misc_clutter") };
			}
			return { Cell("default_misc") };
		}

		Glyph Spell(RE::SpellItem* a_spell)
		{
			using T = RE::MagicSystem::SpellType;
			switch (a_spell->GetSpellType()) {
			case T::kPower:
			case T::kLesserPower:
			case T::kVoicePower: return { Cell("default_power") };
			case T::kAbility: return { Cell("default_effect") };
			default: break;
			}
			const auto effect = a_spell->GetCostliestEffectItem();
			const auto base = effect ? effect->baseEffect : nullptr;
			if (!base) {
				return { Cell("default_effect") };
			}
			switch (base->GetMagickSkill()) {
			case RE::ActorValue::kAlteration: return { Cell("default_alteration"), kAlteration };
			case RE::ActorValue::kIllusion: return { Cell("default_illusion"), kIllusion };
			case RE::ActorValue::kConjuration: return { Cell("default_conjuration"), kConjuration };
			case RE::ActorValue::kRestoration: return { Cell("default_restoration"), kRestoration };
			case RE::ActorValue::kDestruction:
				switch (base->data.resistVariable) {
				case RE::ActorValue::kResistFire: return { Cell("magic_fire"), kFire };
				case RE::ActorValue::kResistFrost: return { Cell("magic_frost"), kFrost };
				case RE::ActorValue::kResistShock: return { Cell("magic_shock"), kShock };
				default: return { Cell("default_destruction"), kFire };
				}
			default: return { Cell("default_effect") };
			}
		}

		Glyph Pick(RE::TESForm* a_form)
		{
			if (!a_form) {
				return {};
			}
			switch (a_form->GetFormType()) {
			case RE::FormType::Weapon: return Weapon(a_form->As<RE::TESObjectWEAP>());
			case RE::FormType::Armor: return Armor(a_form->As<RE::TESObjectARMO>());
			case RE::FormType::Ammo: return { Cell(a_form->As<RE::TESAmmo>()->IsBolt() ? "weapon_bolt" : "weapon_arrow") };
			case RE::FormType::AlchemyItem: return Potion(a_form->As<RE::AlchemyItem>());
			case RE::FormType::Ingredient: return { Cell("default_ingredient") };
			case RE::FormType::Book:
				{
					auto book = a_form->As<RE::TESObjectBOOK>();
					return { Cell(book->TeachesSpell() ? "book_tome" : book->IsNoteScroll() ? "book_note" : "default_book") };
				}
			case RE::FormType::Scroll: return { Cell("default_scroll") };
			case RE::FormType::SoulGem: return SoulGem(a_form->As<RE::TESSoulGem>());
			case RE::FormType::KeyMaster: return { Cell("default_key") };
			case RE::FormType::Misc: return Misc(a_form->As<RE::TESObjectMISC>());
			case RE::FormType::Light: return { Cell("misc_torch") };
			case RE::FormType::Spell: return Spell(a_form->As<RE::SpellItem>());
			case RE::FormType::Shout: return { Cell("default_shout") };
			case RE::FormType::Perk: return { Cell("default_effect") };
			default: return {};
			}
		}

		std::unordered_map<RE::FormID, Glyph> g_cache;
	}

	bool Draw(ImDrawList* a_dl, RE::FormID a_form, ImVec2 a_min, float a_size, ImU32 a_tint)
	{
		const auto& atlas = Textures::Get("icons/item_atlas");
		if (!atlas) {
			return false;
		}
		auto it = g_cache.find(a_form);
		if (it == g_cache.end()) {
			it = g_cache.emplace(a_form, Pick(RE::TESForm::LookupByID(a_form))).first;
		}
		const auto& glyph = it->second;
		if (glyph.cell < 0) {
			return false;
		}
		const float  u = static_cast<float>(glyph.cell % kCols) / kCols;
		const float  v = static_cast<float>(glyph.cell / kCols) / kRows;
		ImU32        tint = glyph.tint ? glyph.tint : a_tint;
		const ImU32  alpha = (a_tint >> IM_COL32_A_SHIFT) & 0xFF;  // the caller's fade
		tint = (tint & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
		a_dl->AddImage(static_cast<ImTextureID>(atlas.id), a_min, { a_min.x + a_size, a_min.y + a_size }, { u, v },
			{ u + 1.0f / kCols, v + 1.0f / kRows }, tint);
		return true;
	}
}

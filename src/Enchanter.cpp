#include "Enchanter.h"

#include "Text.h"

// Enchanting from the menu, the way the enchanting table does it: BGSCreatedObjectManager makes a new enchantment
// from a list of effects, InventoryChanges::EnchantObject puts it on one item. An item whose base form has an
// enchantment of its own is the exception, the game reads that one first and ignores the item's: a common one (Iron Sword
// of Fear) becomes its plain version first; a unique or quest one (Nightingale Blade) gets its base form's enchantment
// swapped (every copy of it changes), put back on removal.
namespace Enchanter
{
	namespace
	{
		using Flag = RE::EffectSetting::EffectSettingData::Flag;

		std::mutex        g_itemsLock;
		std::vector<Item> g_items;

		// base forms whose enchantment the menu replaced: what they had before (kept in the co-save)
		struct BaseOriginal
		{
			RE::FormID    enchantment;
			std::uint16_t charge;
		};
		std::unordered_map<RE::FormID, BaseOriginal> g_baseOriginals;

		// The charge a hit (or a staff cast) takes grows steeply with magnitude: strong effects cost more than any charge
		// (Nightingale Blade at 100/100/150: 1.18 million) and the weapon never fires them. Capped like a strong vanilla one.
		// Only for created (dynamic) enchantments: the game's own keep their cost.
		void CapHitCost(RE::EnchantmentItem* a_enchantment)
		{
			constexpr float kMaxHitCost = 100.0f;
			if (a_enchantment && a_enchantment->IsDynamicForm() && a_enchantment->GetCastingType() != RE::MagicSystem::CastingType::kConstantEffect &&
				!a_enchantment->data.flags.any(RE::EnchantmentItem::EnchantmentFlag::kCostOverride) &&
				a_enchantment->CalculateMagickaCost(RE::PlayerCharacter::GetSingleton()) > kMaxHitCost) {
				a_enchantment->data.costOverride = static_cast<std::int32_t>(kMaxHitCost);
				a_enchantment->data.flags.set(RE::EnchantmentItem::EnchantmentFlag::kCostOverride);
			}
		}

		void SetBase(RE::TESEnchantableForm* a_form, RE::EnchantmentItem* a_enchantment, std::uint16_t a_charge)
		{
			a_form->formEnchanting = a_enchantment;
			a_form->amountofEnchantment = a_charge;
		}

		Kind KindOf(RE::EnchantmentItem* a_enchantment)
		{
			using CT = RE::MagicSystem::CastingType;
			using D = RE::MagicSystem::Delivery;
			if (a_enchantment->GetCastingType() == CT::kConstantEffect) {
				return Kind::kArmor;
			}
			return a_enchantment->GetDelivery() == D::kTouch ? Kind::kWeapon : Kind::kStaff;
		}

		Kind KindOf(RE::TESBoundObject* a_object)
		{
			if (auto weapon = a_object->As<RE::TESObjectWEAP>()) {
				return weapon->IsStaff() ? Kind::kStaff : Kind::kWeapon;
			}
			return Kind::kArmor;
		}

		// ItemCategories() index of a weapon or armour piece.
		std::size_t CategoryOf(RE::TESBoundObject* a_object)
		{
			using W = RE::WEAPON_TYPE;
			using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
			if (auto weapon = a_object->As<RE::TESObjectWEAP>()) {
				switch (weapon->GetWeaponType()) {
				case W::kOneHandSword:  return 1;
				case W::kOneHandDagger: return 2;
				case W::kTwoHandSword:
				case W::kTwoHandAxe:    return 4;
				case W::kBow:
				case W::kCrossbow:      return 5;
				case W::kStaff:         return 6;
				default:                return 3;
				}
			}
			auto armor = a_object->As<RE::TESObjectARMO>();
			if (!armor) {
				return 0;
			}
			const auto has = [&](Slot a_slot) { return armor->HasPartOf(a_slot); };
			if (has(Slot::kShield)) return 11;
			if (has(Slot::kBody)) return 8;
			if (has(Slot::kHead) || has(Slot::kHair) || has(Slot::kCirclet)) return 7;
			if (has(Slot::kHands) || has(Slot::kForearms)) return 9;
			if (has(Slot::kFeet) || has(Slot::kCalves)) return 10;
			if (has(Slot::kRing)) return 12;
			if (has(Slot::kAmulet)) return 13;
			return 14;
		}

		// OptionCategories() bits of one magic effect: the element it deals or resists, the value it changes, summons.
		std::uint32_t CategoryBits(const RE::EffectSetting* a_effect)
		{
			using AV = RE::ActorValue;
			using A = RE::EffectArchetypes::ArchetypeID;
			const auto    av = a_effect->data.primaryAV;
			const auto    resist = a_effect->data.resistVariable;
			std::uint32_t bits = 0;
			if (av == AV::kResistFire || resist == AV::kResistFire) bits |= 1u << 1;
			if (av == AV::kResistFrost || resist == AV::kResistFrost) bits |= 1u << 2;
			if (av == AV::kResistShock || resist == AV::kResistShock) bits |= 1u << 3;
			if (bits) {
				return bits;
			}
			switch (av) {
			case AV::kHealth: case AV::kHealRate: case AV::kHealRateMult:         return 1u << 4;
			case AV::kMagicka: case AV::kMagickaRate: case AV::kMagickaRateMult:  return 1u << 5;
			case AV::kStamina: case AV::kStaminaRate: case AV::kStaminaRateMult:  return 1u << 6;
			case AV::kResistMagic: case AV::kPoisonResist: case AV::kResistDisease:
			case AV::kDamageResist: case AV::kReflectDamage:                      return 1u << 8;
			default: break;
			}
			const auto in = [&](AV a_first, AV a_last) { return std::to_underlying(av) >= std::to_underlying(a_first) && std::to_underlying(av) <= std::to_underlying(a_last); };
			if (in(AV::kOneHanded, AV::kEnchanting) || in(AV::kOneHandedModifier, AV::kEnchantingModifier) || in(AV::kOneHandedPowerModifier, AV::kEnchantingPowerModifier)) {
				return 1u << 7;
			}
			const auto archetype = a_effect->GetArchetype();
			if (archetype == A::kSummonCreature || archetype == A::kReanimate) {
				return 1u << 9;
			}
			return 1u << 10;
		}

		bool Visible(const RE::Effect* a_effect)
		{
			return a_effect && a_effect->baseEffect && !a_effect->baseEffect->data.flags.any(Flag::kHideInUI);
		}

		EffectSpec SpecOf(RE::EnchantmentItem* a_source, const RE::Effect* a_effect)
		{
			const auto  base = a_effect->baseEffect;
			const char* name = base->GetName();
			EffectSpec  spec;
			spec.source = a_source->GetFormID();
			spec.effect = base->GetFormID();
			spec.name = Text::ToUtf8(name && *name ? name : a_source->GetName());
			spec.magnitude = a_effect->effectItem.magnitude;
			spec.duration = static_cast<float>(a_effect->effectItem.duration);
			spec.hasMagnitude = !base->data.flags.any(Flag::kNoMagnitude);
			spec.hasDuration = !base->data.flags.any(Flag::kNoDuration) && KindOf(a_source) != Kind::kArmor;
			return spec;
		}

		// The live extra list of the item the snapshot saw (lists can go away when the inventory changes).
		bool Find(RE::PlayerCharacter* a_player, const Item& a_item, RE::TESBoundObject*& a_object, RE::ExtraDataList*& a_list)
		{
			a_object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_item.object);
			a_list = nullptr;
			if (!a_object) {
				return false;
			}
			auto inventory = a_player->GetInventory([&](RE::TESBoundObject& a_obj) { return &a_obj == a_object; });
			const auto it = inventory.find(a_object);
			if (it == inventory.end()) {
				return false;
			}
			if (!a_item.list) {
				return true;
			}
			// the snapshot's list, or (snapshot older than a load) one that looks the same: worn or not, with or without its own enchantment
			RE::ExtraDataList* alike = nullptr;
			if (const auto& entry = it->second.second; entry && entry->extraLists) {
				for (auto list : *entry->extraLists) {
					if (list == a_item.list) {
						a_list = list;
						return true;
					}
					const bool worn = list && (list->HasType(RE::ExtraDataType::kWorn) || list->HasType(RE::ExtraDataType::kWornLeft));
					if (list && !alike && worn == a_item.worn && list->HasType(RE::ExtraDataType::kEnchantment) == a_item.ownEnchantment) {
						alike = list;
					}
				}
			}
			a_list = alike;
			return alike != nullptr;
		}
	}

	// Worn items: the enchantment's effects come on at equip, so take the item off and put it on again.
	void Reequip(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, std::optional<float> a_charge)
	{
		const auto setCharge = [&] {
			if (auto current = a_charge && a_list ? a_list->GetByType<RE::ExtraCharge>() : nullptr) {
				current->charge = *a_charge;
			}
		};
		auto manager = RE::ActorEquipManager::GetSingleton();
		if (!manager || !a_list || (!a_list->HasType(RE::ExtraDataType::kWorn) && !a_list->HasType(RE::ExtraDataType::kWornLeft))) {
			setCharge();
			return;
		}
		// taking it off writes the hand's charge (an actor value) back into the item: the new charge goes in after that
		manager->UnequipObject(a_player, a_object, a_list, 1, nullptr, false, false, false, true);
		setCharge();
		manager->EquipObject(a_player, a_object, a_list, 1, nullptr, false, false, false, true);
	}

	const std::vector<const char*>& ItemCategories()
	{
		static const std::vector<const char*> names{ "ench_cat.all_items", "ench_cat.swords", "ench_cat.daggers", "ench_cat.axes_and_maces", "ench_cat.two_handed", "ench_cat.bows_and_crossbows", "ench_cat.staves",
			"ench_cat.helmets_and_hoods", "cat.armor_and_clothing", "ench_cat.gauntlets", "ench_cat.boots", "ench_cat.shields", "ench_cat.rings", "ench_cat.amulets", "ench_cat.other" };
		return names;
	}

	const std::vector<const char*>& OptionCategories()
	{
		static const std::vector<const char*> names{ "ench_cat.all_enchantments", "ench_cat.fire", "ench_cat.frost", "ench_cat.shock", "common.health", "common.magicka", "common.stamina", "common.skills",
			"ench_cat.resistances", "ench_cat.conjuration", "ench_cat.other" };
		return names;
	}

	const std::vector<Option>& Options()
	{
		static const std::vector<Option> options = [] {
			// Every enchantment, grouped by its base (EnchWeaponFire01..06 -> EnchWeaponFireBase); per effect the strongest variant.
			std::map<RE::EnchantmentItem*, std::vector<RE::EnchantmentItem*>> groups;
			for (auto enchantment : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EnchantmentItem>()) {
				if (enchantment && !enchantment->effects.empty() && !enchantment->IsDynamicForm()) {
					groups[enchantment->data.baseEnchantment ? enchantment->data.baseEnchantment : enchantment].push_back(enchantment);
				}
			}
			// only enchantments some playable weapon or armour has (drops the bound weapons' FX, quest scripts and the like)
			std::unordered_set<RE::EnchantmentItem*> worn;
			const auto note = [&](RE::TESBoundObject* a_object, RE::EnchantmentItem* a_enchantment) {
				if (a_enchantment && a_object->GetPlayable() && a_object->GetName() && *a_object->GetName()) {
					worn.insert(a_enchantment->data.baseEnchantment ? a_enchantment->data.baseEnchantment : a_enchantment);
				}
			};
			for (auto weapon : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectWEAP>()) {
				if (weapon) {
					note(weapon, weapon->formEnchanting);
				}
			}
			for (auto armor : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectARMO>()) {
				if (armor) {
					note(armor, armor->formEnchanting);
				}
			}
			std::map<std::pair<std::string, Kind>, Option> byName;
			for (const auto& [base, members] : groups) {
				const char* name = base->GetName();
				if (!name || !*name || !worn.contains(base) || !std::ranges::any_of(base->effects, Visible)) {
					continue;
				}
				Option option{ base->GetFormID(), KindOf(base), Text::ToUtf8(name), {}, {}, {} };
				option.nameLower = Text::ToLower(option.name);
				if (Text::Contains(option.nameLower, "fx") || Text::Contains(option.nameLower, "null enchantment")) {
					continue;  // a unique weapon's visual-only enchantment (TrueflameFX, Zephyr Null Enchantment)
				}
				const auto file = base->GetFile(0);
				option.plugin = file ? file->GetFilename() : "";
				for (auto effect : base->effects) {
					if (!Visible(effect)) {
						continue;
					}
					auto spec = SpecOf(base, effect);
					for (auto member : members) {
						for (auto other : member->effects) {
							if (other && other->baseEffect == effect->baseEffect) {
								spec.magnitude = std::max(spec.magnitude, other->effectItem.magnitude);
								spec.duration = std::max(spec.duration, static_cast<float>(other->effectItem.duration));
							}
						}
					}
					spec.maxMagnitude = std::max(100.0f, std::ceil(spec.magnitude * 5.0f / 10.0f) * 10.0f);
					option.categories |= CategoryBits(effect->baseEffect);
					option.effects.push_back(std::move(spec));
				}
				// the same name twice (a mod's copy): keep the one with more variants in the game
				auto& kept = byName[{ option.name, option.kind }];
				if (kept.effects.empty() || members.size() > groups[RE::TESForm::LookupByID<RE::EnchantmentItem>(kept.enchantment)].size()) {
					kept = std::move(option);
				}
			}
			std::vector<Option> result;
			for (auto& [key, option] : byName) {
				result.push_back(std::move(option));
			}
			std::ranges::sort(result, [](const Option& a, const Option& b) { return a.nameLower < b.nameLower; });
			logger::info("Enchanter: {} enchantments to choose from", result.size());
			return result;
		}();
		return options;
	}

	void RefreshItems()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			std::vector<Item> items;
			for (auto& [object, data] : player->GetInventory()) {
				const auto& [count, entry] = data;
				if (!object || (!object->IsWeapon() && !object->IsArmor()) || !object->GetPlayable() || !object->GetName() || !*object->GetName()) {
					continue;
				}
				auto weapon = object->As<RE::TESObjectWEAP>();
				auto enchantable = object->As<RE::TESEnchantableForm>();
				auto add = [&](RE::ExtraDataList* a_list, int a_count) {
					Item item{ object->GetFormID(), a_list, KindOf(object), {}, {}, a_count, false, {}, {}, 0.0f };
					const char* name = a_list ? a_list->GetDisplayName(object) : object->GetName();
					item.name = Text::ToUtf8(name ? name : object->GetName());
					item.nameLower = Text::ToLower(item.name);
					item.worn = a_list && (a_list->HasType(RE::ExtraDataType::kWorn) || a_list->HasType(RE::ExtraDataType::kWornLeft));
					auto extra = a_list ? a_list->GetByType<RE::ExtraEnchantment>() : nullptr;
					item.ownEnchantment = extra != nullptr;
					item.questItem = a_list && a_list->HasType(RE::ExtraDataType::kAliasInstanceArray);
					item.category = CategoryOf(object);
					auto enchantment = extra && extra->enchantment ? extra->enchantment : enchantable ? enchantable->formEnchanting : nullptr;
					CapHitCost(enchantment);  // also ones made before the cap, loaded from a save
					if (enchantment) {
						for (auto effect : enchantment->effects) {
							if (Visible(effect)) {
								auto spec = SpecOf(enchantment, effect);
								spec.maxMagnitude = std::max(100.0f, std::ceil(spec.magnitude * 5.0f / 10.0f) * 10.0f);
								item.enchantments.push_back(spec.name);
								item.current.push_back(std::move(spec));
							}
						}
					}
					item.charge = extra ? extra->charge : weapon && enchantable ? enchantable->amountofEnchantment : 0.0f;
					items.push_back(std::move(item));
				};
				int listed = 0;
				if (entry && entry->extraLists) {
					for (auto list : *entry->extraLists) {
						if (list) {
							const int n = std::max(1, static_cast<int>(list->GetCount()));
							add(list, n);
							listed += n;
						}
					}
				}
				if (count > listed) {
					add(nullptr, count - listed);
				}
			}
			std::ranges::sort(items, [](const Item& a, const Item& b) { return a.worn != b.worn ? a.worn : a.nameLower < b.nameLower; });
			std::scoped_lock lock(g_itemsLock);
			g_items = std::move(items);
		});
	}

	std::vector<Item> Items()
	{
		std::scoped_lock lock(g_itemsLock);
		return g_items;
	}

	RE::EnchantmentItem* Create(bool a_weapon, const std::vector<EffectSpec>& a_effects)
	{
		RE::BSTArray<RE::Effect> effects;
		for (const auto& spec : a_effects) {
			auto base = RE::TESForm::LookupByID<RE::EffectSetting>(spec.effect);
			if (!base) {
				continue;
			}
			auto& effect = effects.emplace_back();
			effect.baseEffect = base;
			effect.effectItem.magnitude = spec.hasMagnitude ? std::max(0.0f, spec.magnitude) : 0.0f;
			effect.effectItem.duration = static_cast<std::uint32_t>(std::max(0.0f, spec.duration));
			effect.effectItem.area = 0;
			// conditions and area of the same effect in its own enchantment (Chillrend's paralysis chance and the like),
			// copied: the created form may be destroyed later, the original's list must stay intact
			auto       source = RE::TESForm::LookupByID<RE::EnchantmentItem>(spec.source);
			RE::Effect* same = nullptr;
			if (source) {
				const auto it = std::ranges::find_if(source->effects, [&](RE::Effect* a_e) { return a_e && a_e->baseEffect == base; });
				same = it != source->effects.end() ? *it : nullptr;
			}
			if (same) {
				effect.effectItem.area = same->effectItem.area;
				effect.cost = same->cost;
				RE::TESConditionItem** tail = &effect.conditions.head;
				for (auto item = same->conditions.head; item; item = item->next) {
					auto own = new RE::TESConditionItem();
					own->data = item->data;
					own->next = nullptr;
					*tail = own;
					tail = &own->next;
				}
			}
		}
		if (effects.empty()) {
			return nullptr;
		}
		auto manager = RE::BGSCreatedObjectManager::GetSingleton();
		auto created = manager ? (a_weapon ? manager->AddWeaponEnchantment(effects) : manager->AddArmorEnchantment(effects)) : nullptr;
		// if the created form took our condition lists as they are, they are its own now: don't free them with the temporary array
		for (auto& effect : effects) {
			const bool taken = created && effect.conditions.head && std::ranges::any_of(created->effects, [&](RE::Effect* a_e) {
				return a_e && a_e->conditions.head == effect.conditions.head;
			});
			if (taken) {
				effect.conditions.head = nullptr;
			}
		}
		if (created && a_weapon) {
			CapHitCost(created);
		}
		return created;
	}

	namespace
	{
		// The plain item an enchanted base form is made from (Iron Sword of Fear -> Iron Sword); null for uniques.
		RE::TESBoundObject* PlainOf(RE::TESBoundObject* a_object)
		{
			if (auto weapon = a_object->As<RE::TESObjectWEAP>()) {
				return weapon->templateWeapon;
			}
			if (auto armor = a_object->As<RE::TESObjectARMO>()) {
				return armor->templateArmor;
			}
			return nullptr;
		}
	}

	RE::ExtraDataList* SetEnchantment(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, RE::EnchantmentItem* a_enchantment,
		std::uint16_t a_charge)
	{
		// the item's own one also keeps the created enchantment referenced, so the game writes it into the save
		if (auto extra = a_list ? a_list->GetByType<RE::ExtraEnchantment>() : nullptr) {
			extra->enchantment = a_enchantment;
			extra->charge = a_charge;
		} else if (auto changes = a_player->GetInventoryChanges()) {
			a_list = changes->EnchantObject(a_object, a_list, a_enchantment, a_charge);
		}
		auto enchantable = a_object->As<RE::TESEnchantableForm>();
		if (const auto it = g_baseOriginals.find(a_object->GetFormID()); enchantable && it != g_baseOriginals.end() && it->second.enchantment == a_enchantment->GetFormID()) {
			SetBase(enchantable, a_enchantment, it->second.charge);  // back to its own one
			g_baseOriginals.erase(it);
		} else if (enchantable && enchantable->formEnchanting && enchantable->formEnchanting != a_enchantment) {
			// the original is remembered once: enchanting it again keeps the first one to put back
			g_baseOriginals.try_emplace(a_object->GetFormID(), BaseOriginal{ enchantable->formEnchanting->GetFormID(), enchantable->amountofEnchantment });
			SetBase(enchantable, a_enchantment, a_object->IsWeapon() ? a_charge : enchantable->amountofEnchantment);
			logger::info("Enchanter: base form {:08X} now has {:08X}", a_object->GetFormID(), a_enchantment->GetFormID());
		}
		return a_list;
	}

	RE::ExtraDataList* Enchant(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, RE::ExtraDataList* a_list, RE::EnchantmentItem* a_enchantment,
		std::uint16_t a_charge, const std::string& a_name)
	{
		const bool worn = a_list && (a_list->HasType(RE::ExtraDataType::kWorn) || a_list->HasType(RE::ExtraDataType::kWornLeft));
		const bool left = a_list && a_list->HasType(RE::ExtraDataType::kWornLeft);
		// built-in enchantment on a common item (Iron Sword of Fear): it becomes its plain version with the new enchantment,
		// keeping tempering and name, so the base form (every copy in the world) stays as it is. Uniques and quest items
		// (an alias holds that very item) can't be swapped: their base form's enchantment is replaced instead.
		auto enchantable = a_object->As<RE::TESEnchantableForm>();
		auto plain = enchantable && enchantable->formEnchanting && !g_baseOriginals.contains(a_object->GetFormID()) &&
		                     !(a_list && a_list->HasType(RE::ExtraDataType::kAliasInstanceArray)) ?
		                 PlainOf(a_object) :
		                 nullptr;
		float       health = 1.0f;
		std::string oldName;
		if (plain) {
			const auto xHealth = a_list ? a_list->GetByType<RE::ExtraHealth>() : nullptr;
			health = xHealth ? xHealth->health : 1.0f;
			const char* name = a_list ? a_list->GetDisplayName(a_object) : a_object->GetName();
			oldName = name ? name : "";
			a_player->RemoveItem(a_object, 1, RE::ITEM_REMOVE_REASON::kRemove, a_list, nullptr);
			a_player->AddObjectToContainer(plain, nullptr, 1, nullptr);
			logger::info("Enchanter: {:08X} swapped for plain {:08X} to be enchanted", a_object->GetFormID(), plain->GetFormID());
			a_object = plain;
			a_list = nullptr;
		}
		a_list = SetEnchantment(a_player, a_object, a_list, a_enchantment, a_charge);
		if (a_list && plain && health > 1.0f && !a_list->HasType(RE::ExtraDataType::kHealth)) {
			a_list->Add(new RE::ExtraHealth(health));
		}
		if (a_list && !a_name.empty()) {
			a_list->SetOverrideName(a_name.c_str());
		} else if (a_list && plain && !oldName.empty()) {
			a_list->SetOverrideName(oldName.c_str());
		}
		if (plain && worn) {
			// the left hand's equip slot (Skyrim.esm LeftHand); null = the item's default (right hand, or armour's own slots)
			const RE::BGSEquipSlot* slot = left && a_object->IsWeapon() ? RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x13F43) : nullptr;
			RE::ActorEquipManager::GetSingleton()->EquipObject(a_player, plain, a_list, 1, slot, false, false, false, true);
		} else {
			Reequip(a_player, a_object, a_list, a_object->IsWeapon() ? std::optional<float>{ static_cast<float>(a_charge) } : std::nullopt);
		}
		return a_list;
	}

	std::string Apply(const Item& a_item, const std::vector<EffectSpec>& a_effects, const std::string& a_name, float a_charge)
	{
		if (a_effects.empty()) {
			return Tr("status.add_at_least_one_enchantment");
		}
		SKSE::GetTaskInterface()->AddTask([a_item, a_effects, a_name, a_charge]() {
			auto                 player = RE::PlayerCharacter::GetSingleton();
			RE::TESBoundObject*  object = nullptr;
			RE::ExtraDataList*   list = nullptr;
			if (!player || !Find(player, a_item, object, list)) {
				logger::warn("Enchanter: {:08X} is no longer in the inventory", a_item.object);
				return;
			}
			auto enchantment = Create(a_item.kind != Kind::kArmor, a_effects);
			if (!enchantment) {
				logger::warn("Enchanter: the game made no enchantment for {:08X}", a_item.object);
				return;
			}
			const auto charge = static_cast<std::uint16_t>(std::clamp(a_charge, 0.0f, 65535.0f));
			Enchant(player, object, list, enchantment, charge, a_name);
			logger::info("Enchanter: {:08X} got {:08X} with {} effects", a_item.object, enchantment->GetFormID(), a_effects.size());
			RefreshItems();
		});
		return Lang::Fmt("status.enchanted", a_item.name);
	}

	std::string Remove(const Item& a_item)
	{
		if (a_item.enchantments.empty()) {
			return Tr("status.this_item_has_no_enchantments");
		}
		if (a_item.questItem && !a_item.ownEnchantment) {
			return Tr("status.quest_item_its_built_in");
		}
		SKSE::GetTaskInterface()->AddTask([a_item]() {
			auto                player = RE::PlayerCharacter::GetSingleton();
			RE::TESBoundObject* object = nullptr;
			RE::ExtraDataList*  list = nullptr;
			if (!player || !Find(player, a_item, object, list)) {
				logger::warn("Enchanter: {:08X} is no longer in the inventory", a_item.object);
				return;
			}
			// an enchantment of its own (table, this menu): just take it off the item
			if (list && list->HasType(RE::ExtraDataType::kEnchantment)) {
				list->RemoveByType(RE::ExtraDataType::kEnchantment);
				list->RemoveByType(RE::ExtraDataType::kCharge);
			}
			auto enchantable = object->As<RE::TESEnchantableForm>();
			// the base form's enchantment was replaced by this menu: the original comes back, the item stays
			if (const auto it = g_baseOriginals.find(object->GetFormID()); it != g_baseOriginals.end() && enchantable) {
				if (auto original = RE::TESForm::LookupByID<RE::EnchantmentItem>(it->second.enchantment)) {
					SetBase(enchantable, original, it->second.charge);
				}
				g_baseOriginals.erase(it);
				Reequip(player, object, list);
				logger::info("Enchanter: base form {:08X} has its own enchantment back", a_item.object);
				RefreshItems();
				return;
			}
			if (!enchantable || !enchantable->formEnchanting) {
				Reequip(player, object, list);
				logger::info("Enchanter: enchantment taken off {:08X}", a_item.object);
				RefreshItems();
				return;
			}
			// enchanted by its base form (Iron Sword of Fear): swap it for the plain item it is made from, keeping the tempering
			auto plain = PlainOf(object);
			if (!plain) {
				logger::warn("Enchanter: {:08X} has no plain version to swap for", a_item.object);
				return;
			}
			if (list && list->HasType(RE::ExtraDataType::kAliasInstanceArray)) {
				logger::warn("Enchanter: {:08X} is a quest item, not swapped", a_item.object);
				return;
			}
			const auto  xHealth = list ? list->GetByType<RE::ExtraHealth>() : nullptr;
			const float health = xHealth ? xHealth->health : 1.0f;
			const bool  worn = a_item.worn;
			player->RemoveItem(object, 1, RE::ITEM_REMOVE_REASON::kRemove, list, nullptr);
			player->AddObjectToContainer(plain, nullptr, 1, nullptr);
			if (worn) {
				RE::ActorEquipManager::GetSingleton()->EquipObject(player, plain, nullptr, 1, nullptr, false, false, false, true);
			}
			const auto plainID = plain->GetFormID();
			SKSE::GetTaskInterface()->AddTask([plainID, health]() {
				// worn, the new item has an extra list now: put the tempering back on it
				auto player = RE::PlayerCharacter::GetSingleton();
				auto plain = RE::TESForm::LookupByID<RE::TESBoundObject>(plainID);
				if (player && plain && health > 1.0f) {
					auto inventory = player->GetInventory([&](RE::TESBoundObject& a_obj) { return &a_obj == plain; });
					if (const auto it = inventory.find(plain); it != inventory.end() && it->second.second && it->second.second->extraLists) {
						for (auto list : *it->second.second->extraLists) {
							if (list && (list->HasType(RE::ExtraDataType::kWorn) || list->HasType(RE::ExtraDataType::kWornLeft)) && !list->HasType(RE::ExtraDataType::kHealth)) {
								list->Add(new RE::ExtraHealth(health));
								break;
							}
						}
					}
				}
				RefreshItems();
			});
			logger::info("Enchanter: {:08X} swapped for plain {:08X}", a_item.object, plainID);
		});
		return Lang::Fmt("status.enchantment_removed_from", a_item.name);
	}

	void Save(SKSE::SerializationInterface* a_intfc)
	{
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_baseOriginals.size()));
		for (const auto& [base, original] : g_baseOriginals) {
			auto enchantable = RE::TESForm::LookupByID<RE::TESEnchantableForm>(base);
			const RE::FormID now = enchantable && enchantable->formEnchanting ? enchantable->formEnchanting->GetFormID() : 0;
			a_intfc->WriteRecordData(base);
			a_intfc->WriteRecordData(original.enchantment);
			a_intfc->WriteRecordData(original.charge);
			a_intfc->WriteRecordData(now);
			a_intfc->WriteRecordData(enchantable ? enchantable->amountofEnchantment : std::uint16_t{ 0 });
		}
	}

	void Load(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t count = 0;
		a_intfc->ReadRecordData(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			RE::FormID    base = 0, original = 0, now = 0;
			std::uint16_t originalCharge = 0, nowCharge = 0;
			if (!a_intfc->ReadRecordData(base) || !a_intfc->ReadRecordData(original) || !a_intfc->ReadRecordData(originalCharge) ||
				!a_intfc->ReadRecordData(now) || !a_intfc->ReadRecordData(nowCharge)) {
				break;
			}
			auto enchantable = a_intfc->ResolveFormID(base, base) ? RE::TESForm::LookupByID<RE::TESEnchantableForm>(base) : nullptr;
			auto created = a_intfc->ResolveFormID(now, now) ? RE::TESForm::LookupByID<RE::EnchantmentItem>(now) : nullptr;
			if (!enchantable || !created || !a_intfc->ResolveFormID(original, original)) {
				continue;  // the plugin is gone, or the game dropped the created enchantment
			}
			g_baseOriginals[base] = { original, originalCharge };
			SetBase(enchantable, created, nowCharge);
		}
		logger::info("Enchanter: {} base forms with a replaced enchantment", g_baseOriginals.size());
	}

	void Revert()
	{
		// base forms keep runtime changes until the game quits: put the originals back before another save loads
		for (const auto& [base, original] : g_baseOriginals) {
			auto enchantable = RE::TESForm::LookupByID<RE::TESEnchantableForm>(base);
			auto enchantment = RE::TESForm::LookupByID<RE::EnchantmentItem>(original.enchantment);
			if (enchantable && enchantment) {
				SetBase(enchantable, enchantment, original.charge);
			}
		}
		g_baseOriginals.clear();
	}
}

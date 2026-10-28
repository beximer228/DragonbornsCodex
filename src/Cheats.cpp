#include "Cheats.h"
#include "Puzzles.h"

#include "Catalog.h"
#include "Console.h"
#include "Enchanter.h"
#include "Input.h"
#include "Lang.h"
#include "Menu.h"
#include "Esp.h"
#include "Killmoves.h"
#include "SkillCap.h"
#include "Text.h"

namespace Cheats
{
	namespace
	{
		State g_state;

		constexpr float kHugeCarryWeight = 100000.0f;
		constexpr float kNoFallDamageHeight = 100000.0f;
		constexpr int   kInfiniteAmmoMin = 50;
		constexpr int   kInfiniteLockpicksMin = 10;
		constexpr int   kInfinitePoisonMin = 5;
		constexpr float kLongEffectSeconds = 3600.0f;  // blessings, "rested" and other long buffs
		constexpr int   kTrainingUnlimited = 999;

		constexpr RE::FormID kLockpick = 0x0000000A;
		constexpr RE::FormID kNightEyeSpell = 0x000AA01D;   // PowerKhajiitNightEye
		constexpr RE::FormID kNightEyeEffect = 0x0006B10C;
		constexpr RE::FormID kApprenticeStone = 0x000E5F4E;
		constexpr RE::FormID kBeastForm = 0x00092C48;          // WerewolfChange
		constexpr RE::FormID kVampireLordLocal = 0x0000283B;   // DLC1VampireChange in Dawnguard.esm
		constexpr RE::FormID kStorageChest = 0x000F3923;       // PlayerHouseChest, never respawns
		constexpr RE::FormID kGold = 0x0000000F;
		constexpr int        kRichMerchantGold = 30000;
		constexpr float      kStorageDepth = 6000.0f;          // the storage chest hides under the ground
		constexpr RE::FormID kApprenticeWeakness = 0x0010E439;  // doomApprenticeNegativeAbility

		constexpr Stone kStones[] = {
			{ 0x000E5F4C, "stone.the_warrior_stone" },
			{ 0x000E5F47, "stone.the_mage_stone" },
			{ 0x000E5F45, "stone.the_thief_stone" },
			{ 0x000E5F54, "stone.the_lady_stone" },
			{ 0x000E5F58, "stone.the_lord_stone" },
			{ 0x000E5F5A, "stone.the_lover_stone" },
			{ kApprenticeStone, "stone.the_apprentice_stone" },
			{ 0x000E5F51, "stone.the_atronach_stone" },
			{ 0x000E7329, "stone.the_ritual_stone" },
			{ 0x000E5F61, "stone.the_serpent_stone" },
			{ 0x000E732A, "stone.the_shadow_stone" },
			{ 0x000E5F5E, "stone.the_steed_stone" },
			{ 0x000E7328, "stone.the_tower_stone" },
		};

		// Base actor values as they were before we overrode them. Base values are written
		// into the game save, so these go into the SKSE co-save next to it.
		struct Originals
		{
			std::optional<float> carryWeight;
			std::optional<float> speedMult;
			std::optional<float> shoutRecovery;
		};
		Originals g_originals;

		// Every other base actor value we override, original values first (also in the co-save).
		std::map<RE::ActorValue, float> g_avOriginals;
		bool                            g_immortalMode = false;  // global for the whole game session
		bool                            g_nightEyeTouched = false;
		RE::FormID                      g_storage = 0;  // portable storage chest reference
		RE::RefHandle                   g_lastLockpickTarget = 0;
		RE::RefHandle                   g_lastBarterTarget = 0;

		// Game settings, skill XP rates and item weights live only in memory (reset on every
		// game launch), so their originals are kept for the session only.
		std::unordered_map<std::string, float>    g_settingOriginals;
		std::unordered_map<RE::ActorValue, float> g_skillUseOriginals;
		std::unordered_map<RE::FormID, float>     g_weightOriginals;
		std::unordered_map<std::string, std::int32_t> g_intSettingOriginals;
		std::unordered_map<RE::FormID, float>         g_chargeTimeOriginals;
		std::unordered_map<RE::FormID, std::int32_t>  g_torchTimeOriginals;
		std::unordered_map<RE::FormID, RE::MagicSystem::SpellType> g_powerOriginals;

		struct RecipeOriginal
		{
			std::uint32_t          itemCount;
			RE::TESConditionItem*  conditions;
		};
		std::unordered_map<RE::FormID, RecipeOriginal> g_recipeOriginals;
		bool                                           g_recipesNoItems = false;
		bool                                           g_recipesNoConditions = false;
		bool                                      g_worldSpeedTouched = false;

		constexpr std::uint32_t kCosaveID = 'CHTM';
		constexpr std::uint32_t kRecordState = 'STAT';
		constexpr std::uint32_t kRecordVersion = 6;
		constexpr std::uint32_t kRecordStorage = 'STOR';
		constexpr std::uint32_t kRecordAvOriginals = 'AVOR';
		constexpr std::uint32_t kRecordPoints = 'TPNT';
		constexpr std::uint32_t kPointsVersion = 1;
		constexpr std::uint32_t kRecordLearned = 'LRND';
		constexpr std::uint32_t kRecordEssential = 'IMMF';
		constexpr std::uint32_t kRecordBoostedEnchantments = 'BENC';
		constexpr std::uint32_t kRecordBaseEnchantments = 'BORG';
		constexpr std::uint32_t kRecordStashes = 'STSH';
		constexpr RE::FormID    kXMarker = 0x0000003B;

		std::vector<SavedPoint> g_points;
		// Perks / spells / shouts the "learn all" buttons and auto perks gave, so they can be taken back.
		std::vector<RE::FormID> g_learned;
		std::vector<RE::FormID> g_stashes;  // containers the player put things into: the autoloot never empties them
		// NPC bases "immortal followers" made essential, so exactly those are made mortal again.
		std::vector<RE::FormID> g_madeEssential;
		// Enchantments "Легендарная экипировка" made stronger (created form -> the enchantment it was made from),
		// so pressing again starts from the original strength instead of compounding.
		std::unordered_map<RE::FormID, RE::FormID> g_boostedEnchantments;

		struct SaveRecord
		{
			State state;
			bool  hasCarryWeight, hasSpeedMult, hasShoutRecovery;
			float carryWeight, speedMult, shoutRecovery;
		};
		static_assert(std::is_trivially_copyable_v<SaveRecord>);

		// Version 5 layout: Originals first, then the State bytes (any length).
		struct OriginalsRecord
		{
			bool  hasCarryWeight, hasSpeedMult, hasShoutRecovery;
			float carryWeight, speedMult, shoutRecovery;
		};

		struct SaveRecordV4
		{
			StateV4 state;
			bool    hasCarryWeight, hasSpeedMult, hasShoutRecovery;
			float   carryWeight, speedMult, shoutRecovery;
		};

		struct SaveRecordV3
		{
			StateV3 state;
			bool    hasCarryWeight, hasSpeedMult, hasShoutRecovery;
			float   carryWeight, speedMult, shoutRecovery;
		};

		void RunOnPlayer(std::function<void(RE::PlayerCharacter*)> a_fn)
		{
			SKSE::GetTaskInterface()->AddTask([fn = std::move(a_fn)]() {
				if (auto player = RE::PlayerCharacter::GetSingleton(); player && player->Is3DLoaded()) {
					fn(player);
				}
			});
		}

		void RestoreFull(RE::PlayerCharacter* a_player, RE::ActorValue a_av)
		{
			const auto damage = a_player->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, a_av);
			if (damage < 0.0f) {
				a_player->AsActorValueOwner()->RestoreActorValue(a_av, -damage);
			}
		}

		// Speed changes only take effect once the movement controller re-reads actor values.
		void NudgeMovement(RE::PlayerCharacter* a_player)
		{
			auto avo = a_player->AsActorValueOwner();
			avo->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kCarryWeight, 0.1f);
			avo->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kCarryWeight, -0.1f);
		}

		// Sets a base actor value to a_target, remembering the original the first time;
		// with no target, puts the original back and forgets it.
		void Override(RE::ActorValueOwner* a_avo, RE::ActorValue a_av, std::optional<float>& a_original, std::optional<float> a_target)
		{
			if (a_target) {
				if (!a_original) {
					a_original = a_avo->GetBaseActorValue(a_av);
				}
				a_avo->SetBaseActorValue(a_av, *a_target);
			} else if (a_original) {
				a_avo->SetBaseActorValue(a_av, *a_original);
				a_original.reset();
			}
		}

		// Same idea for a float game setting. a_target receives the original value.
		void OverrideSetting(const char* a_name, bool a_enable, const std::function<float(float)>& a_target)
		{
			auto setting = RE::GameSettingCollection::GetSingleton()->GetSetting(a_name);
			if (!setting) {
				logger::warn("Game setting {} not found", a_name);
				return;
			}
			auto it = g_settingOriginals.find(a_name);
			if (a_enable) {
				if (it == g_settingOriginals.end()) {
					it = g_settingOriginals.emplace(a_name, setting->GetFloat()).first;
				}
				setting->SetFloat(a_target(it->second));
			} else if (it != g_settingOriginals.end()) {
				setting->SetFloat(it->second);
				g_settingOriginals.erase(it);
			}
		}

		void OverrideIntSetting(const char* a_name, bool a_enable, std::int32_t a_target)
		{
			auto setting = RE::GameSettingCollection::GetSingleton()->GetSetting(a_name);
			if (!setting) {
				logger::warn("Game setting {} not found", a_name);
				return;
			}
			auto it = g_intSettingOriginals.find(a_name);
			if (a_enable) {
				if (it == g_intSettingOriginals.end()) {
					g_intSettingOriginals.emplace(a_name, setting->GetInteger());
				}
				setting->SetInteger(a_target);
			} else if (it != g_intSettingOriginals.end()) {
				setting->SetInteger(it->second);
				g_intSettingOriginals.erase(it);
			}
		}

		float OriginalAV(RE::ActorValueOwner* a_avo, RE::ActorValue a_av)
		{
			const auto it = g_avOriginals.find(a_av);
			return it != g_avOriginals.end() ? it->second : a_avo->GetBaseActorValue(a_av);
		}

		// Like Override(), for any actor value; originals live in g_avOriginals.
		void OverrideAV(RE::ActorValueOwner* a_avo, RE::ActorValue a_av, std::optional<float> a_target)
		{
			auto it = g_avOriginals.find(a_av);
			if (a_target) {
				if (it == g_avOriginals.end()) {
					g_avOriginals.emplace(a_av, a_avo->GetBaseActorValue(a_av));
				}
				a_avo->SetBaseActorValue(a_av, *a_target);
			} else if (it != g_avOriginals.end()) {
				a_avo->SetBaseActorValue(a_av, it->second);
				g_avOriginals.erase(it);
			}
		}

		bool Changed(float a_value, float a_default) { return std::abs(a_value - a_default) > 0.001f; }

		// Fortify-style bonus on top of the original value.
		void Boost(RE::ActorValueOwner* a_avo, RE::ActorValue a_av, float a_bonus)
		{
			const float original = OriginalAV(a_avo, a_av);
			OverrideAV(a_avo, a_av, Changed(a_bonus, 0.0f) ? std::optional{ original + a_bonus } : std::nullopt);
		}

		void ApplyInstantCast(bool a_enable)
		{
			if (a_enable == !g_chargeTimeOriginals.empty()) {
				return;
			}
			for (auto spell : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>()) {
				if (!spell) {
					continue;
				}
				if (a_enable) {
					g_chargeTimeOriginals.try_emplace(spell->GetFormID(), spell->data.chargeTime);
					spell->data.chargeTime = 0.0f;
				} else if (auto it = g_chargeTimeOriginals.find(spell->GetFormID()); it != g_chargeTimeOriginals.end()) {
					spell->data.chargeTime = it->second;
				}
			}
			if (!a_enable) {
				g_chargeTimeOriginals.clear();
			}
		}

		void ApplyInfiniteTorch(bool a_enable)
		{
			if (a_enable == !g_torchTimeOriginals.empty()) {
				return;
			}
			for (auto light : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectLIGH>()) {
				if (!light || !light->CanBeCarried()) {
					continue;
				}
				if (a_enable) {
					g_torchTimeOriginals.try_emplace(light->GetFormID(), light->data.time);
					light->data.time = -1;  // -1 = never burns out
				} else if (auto it = g_torchTimeOriginals.find(light->GetFormID()); it != g_torchTimeOriginals.end()) {
					light->data.time = it->second;
				}
			}
			if (!a_enable) {
				g_torchTimeOriginals.clear();
			}
		}

		void ApplySkillXp(float a_mult)
		{
			const bool enable = Changed(a_mult, 1.0f);
			for (auto i = static_cast<int>(RE::ActorValue::kOneHanded); i <= static_cast<int>(RE::ActorValue::kEnchanting); ++i) {
				const auto av = static_cast<RE::ActorValue>(i);
				auto       info = RE::ActorValueList::GetActorValueInfo(av);
				if (!info || !info->skill) {
					continue;
				}
				auto it = g_skillUseOriginals.find(av);
				if (enable) {
					if (it == g_skillUseOriginals.end()) {
						it = g_skillUseOriginals.emplace(av, info->skill->useMult).first;
					}
					info->skill->useMult = it->second * a_mult;
				} else if (it != g_skillUseOriginals.end()) {
					info->skill->useMult = it->second;
					g_skillUseOriginals.erase(it);
				}
			}
		}

		enum class WeightGroup
		{
			kNone,
			kBooks,
			kPotions,
			kFood,
			kIngredients,
			kScrolls,
			kSoulGems,
			kCrafting,
			kGems,
			kMisc,
			kAmmo,
			kWeapons,
			kArmor,
			kTotal
		};

		bool HasKeyword(RE::BGSKeywordForm* a_form, std::initializer_list<RE::BGSKeyword*> a_keywords)
		{
			return a_form && std::ranges::any_of(a_keywords, [&](RE::BGSKeyword* a_kw) { return a_kw && a_form->HasKeyword(a_kw); });
		}

		// Which switch a form belongs to. Misc items are told apart by the vendor keywords the game uses for
		// shop categories, so items from mods sort themselves as long as they carry those keywords.
		WeightGroup GroupOf(RE::TESForm* a_form)
		{
			static auto kw = [](const char* a_editorID) { return RE::TESForm::LookupByEditorID<RE::BGSKeyword>(a_editorID); };
			static const auto ore = kw("VendorItemOreIngot"), hide = kw("VendorItemAnimalHide"), part = kw("VendorItemAnimalPart"),
							  firewood = kw("VendorItemFirewood"), gem = kw("VendorItemGem");
			switch (a_form->GetFormType()) {
			case RE::FormType::Book: return WeightGroup::kBooks;
			case RE::FormType::AlchemyItem: return a_form->As<RE::AlchemyItem>()->IsFood() ? WeightGroup::kFood : WeightGroup::kPotions;
			case RE::FormType::Ingredient: return WeightGroup::kIngredients;
			case RE::FormType::Scroll: return WeightGroup::kScrolls;
			case RE::FormType::SoulGem: return WeightGroup::kSoulGems;
			case RE::FormType::KeyMaster: return WeightGroup::kMisc;
			case RE::FormType::Ammo: return WeightGroup::kAmmo;
			case RE::FormType::Weapon: return WeightGroup::kWeapons;
			case RE::FormType::Armor: return WeightGroup::kArmor;
			case RE::FormType::Misc:
				{
					auto keywords = a_form->As<RE::BGSKeywordForm>();
					if (HasKeyword(keywords, { gem })) {
						return WeightGroup::kGems;
					}
					return HasKeyword(keywords, { ore, hide, part, firewood }) ? WeightGroup::kCrafting : WeightGroup::kMisc;
				}
			default: return WeightGroup::kNone;
			}
		}

		template <class T>
		void SetGroupWeights(const std::array<bool, static_cast<std::size_t>(WeightGroup::kTotal)>& a_on)
		{
			for (auto form : RE::TESDataHandler::GetSingleton()->GetFormArray<T>()) {
				auto weightForm = form ? form->template As<RE::TESWeightForm>() : nullptr;
				if (!weightForm) {
					continue;
				}
				if (a_on[static_cast<std::size_t>(GroupOf(form))]) {
					g_weightOriginals.try_emplace(form->GetFormID(), weightForm->weight);
					weightForm->weight = 0.0f;
				} else if (auto it = g_weightOriginals.find(form->GetFormID()); it != g_weightOriginals.end()) {
					weightForm->weight = it->second;
					g_weightOriginals.erase(it);
				}
			}
		}

		void ApplyWeightless(const State& a_state)
		{
			std::array<bool, static_cast<std::size_t>(WeightGroup::kTotal)> on{};
			auto set = [&](WeightGroup a_group, bool a_value) { on[static_cast<std::size_t>(a_group)] = a_value; };
			set(WeightGroup::kBooks, a_state.weightlessBooks);
			set(WeightGroup::kPotions, a_state.weightlessPotions);
			set(WeightGroup::kFood, a_state.weightlessFood);
			set(WeightGroup::kIngredients, a_state.weightlessIngredients);
			set(WeightGroup::kScrolls, a_state.weightlessScrolls);
			set(WeightGroup::kSoulGems, a_state.weightlessSoulGems);
			set(WeightGroup::kCrafting, a_state.weightlessCrafting);
			set(WeightGroup::kGems, a_state.weightlessGems);
			set(WeightGroup::kMisc, a_state.weightlessMiscItems);
			set(WeightGroup::kAmmo, a_state.weightlessAmmo);
			set(WeightGroup::kWeapons, a_state.weightlessWeapons);
			set(WeightGroup::kArmor, a_state.weightlessArmor);

			static std::optional<decltype(on)> applied;  // walking every form is not free: only when a switch changed
			if (applied == on) {
				return;
			}
			applied = on;
			SetGroupWeights<RE::TESObjectBOOK>(on);
			SetGroupWeights<RE::AlchemyItem>(on);
			SetGroupWeights<RE::IngredientItem>(on);
			SetGroupWeights<RE::ScrollItem>(on);
			SetGroupWeights<RE::TESSoulGem>(on);
			SetGroupWeights<RE::TESKey>(on);
			SetGroupWeights<RE::TESObjectMISC>(on);
			SetGroupWeights<RE::TESAmmo>(on);
			SetGroupWeights<RE::TESObjectWEAP>(on);
			SetGroupWeights<RE::TESObjectARMO>(on);
		}

		// The old single switch covered every consumable and misc item; saves made with it get the same categories.
		void MigrateLegacy(State& a_state)
		{
			if (!a_state.weightlessMisc) {
				return;
			}
			a_state.weightlessMisc = false;
			a_state.weightlessBooks = a_state.weightlessPotions = a_state.weightlessFood = a_state.weightlessIngredients = a_state.weightlessScrolls =
				a_state.weightlessSoulGems = a_state.weightlessCrafting = a_state.weightlessGems = a_state.weightlessMiscItems = true;
		}


		void KeepAmmo(RE::PlayerCharacter* a_player)
		{
			auto ammo = a_player->GetCurrentAmmo();
			if (!ammo) {
				return;
			}
			const auto counts = a_player->GetInventoryCounts([ammo](RE::TESBoundObject& a_obj) { return &a_obj == ammo; });
			const auto it = counts.find(ammo);
			const int  have = it != counts.end() ? it->second : 0;
			if (have < kInfiniteAmmoMin) {
				a_player->AddObjectToContainer(ammo, nullptr, kInfiniteAmmoMin - have, nullptr);
			}
		}

		void WriteString(SKSE::SerializationInterface* a_intfc, const std::string& a_str)
		{
			const auto size = static_cast<std::uint32_t>(a_str.size());
			a_intfc->WriteRecordData(size);
			a_intfc->WriteRecordData(a_str.data(), size);
		}

		bool ReadString(SKSE::SerializationInterface* a_intfc, std::string& a_str)
		{
			std::uint32_t size = 0;
			if (!a_intfc->ReadRecordData(size) || size > 4096) {
				return false;
			}
			a_str.resize(size);
			return a_intfc->ReadRecordData(a_str.data(), size) == size;
		}

		void ReadPoints(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t count = 0;
			if (!a_intfc->ReadRecordData(count)) {
				return;
			}
			for (std::uint32_t i = 0; i < count; ++i) {
				SavedPoint point{};
				if (!a_intfc->ReadRecordData(point.marker) || !ReadString(a_intfc, point.name) || !ReadString(a_intfc, point.place)) {
					logger::error("Co-save: teleport points are damaged");
					return;
				}
				if (a_intfc->ResolveFormID(point.marker, point.marker)) {
					g_points.push_back(std::move(point));
				}
			}
			logger::info("Co-save: {} teleport points", g_points.size());
		}

		// Greater powers (once a day) become lesser powers (no cooldown); beast form included.
		void ApplyPowersNoCooldown(bool a_enable)
		{
			if (a_enable == !g_powerOriginals.empty()) {
				return;
			}
			for (auto spell : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>()) {
				if (!spell) {
					continue;
				}
				if (a_enable) {
					if (spell->data.spellType == RE::MagicSystem::SpellType::kPower) {
						g_powerOriginals.emplace(spell->GetFormID(), spell->data.spellType);
						spell->data.spellType = RE::MagicSystem::SpellType::kLesserPower;
					}
				} else if (auto it = g_powerOriginals.find(spell->GetFormID()); it != g_powerOriginals.end()) {
					spell->data.spellType = it->second;
				}
			}
			if (!a_enable) {
				g_powerOriginals.clear();
			}
		}

		// Crafting recipes (forge, tanning rack, tempering, cooking, Hearthfire building...):
		// hide the required items and/or the conditions (perks), restore them later.
		void ApplyFreeCraft(bool a_noItems, bool a_noConditions)
		{
			if (a_noItems == g_recipesNoItems && a_noConditions == g_recipesNoConditions) {
				return;
			}
			for (auto recipe : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSConstructibleObject>()) {
				if (!recipe) {
					continue;
				}
				auto it = g_recipeOriginals.try_emplace(recipe->GetFormID(), RecipeOriginal{ recipe->requiredItems.numContainerObjects, recipe->conditions.head }).first;
				recipe->requiredItems.numContainerObjects = a_noItems ? 0 : it->second.itemCount;
				recipe->conditions.head = a_noConditions ? nullptr : it->second.conditions;
			}
			g_recipesNoItems = a_noItems;
			g_recipesNoConditions = a_noConditions;
			if (!a_noItems && !a_noConditions) {
				g_recipeOriginals.clear();
			}
		}

		// "MagicDisallowEnchanting" keeps artifacts and unique items off the enchanting table, both for
		// disenchanting and for enchanting. Taken off every form that has it, put back when switched off.
		std::vector<RE::BGSKeywordForm*> g_disallowStripped;

		void ApplyEnchantAnything(bool a_enable)
		{
			if (a_enable == !g_disallowStripped.empty()) {
				return;
			}
			auto keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDisallowEnchanting");
			if (!keyword) {
				return;
			}
			if (!a_enable) {
				for (auto form : g_disallowStripped) {
					form->AddKeyword(keyword);
				}
				logger::info("Enchant anything off: keyword back on {} forms", g_disallowStripped.size());
				g_disallowStripped.clear();
				return;
			}
			auto dh = RE::TESDataHandler::GetSingleton();
			auto strip = [&](auto& a_forms) {
				for (auto form : a_forms) {
					if (form && form->HasKeyword(keyword) && form->RemoveKeyword(keyword)) {
						g_disallowStripped.push_back(form);
					}
				}
			};
			strip(dh->GetFormArray<RE::TESObjectWEAP>());
			strip(dh->GetFormArray<RE::TESObjectARMO>());
			strip(dh->GetFormArray<RE::EnchantmentItem>());
			strip(dh->GetFormArray<RE::EffectSetting>());
			logger::info("Enchant anything on: keyword taken off {} forms", g_disallowStripped.size());
		}

		void OverrideBarter(bool a_enable)
		{
			// Buying costs the base price and selling pays it in full.
			for (const char* name : { "fBarterMin", "fBarterMax", "fBarterBuyMin", "fBarterSellMax" }) {
				OverrideSetting(name, a_enable, [](float) { return 1.0f; });
			}
		}

		void KeepAtLeastIn(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_obj, int a_min)
		{
			if (!a_ref || !a_obj) {
				return;
			}
			const auto counts = a_ref->GetInventoryCounts([a_obj](RE::TESBoundObject& a_item) { return &a_item == a_obj; });
			const auto it = counts.find(a_obj);
			const int  have = it != counts.end() ? it->second : 0;
			if (have < a_min) {
				a_ref->AddObjectToContainer(a_obj, nullptr, a_min - have, nullptr);
			}
		}

		void AutoUnlock()
		{
			auto target = RE::LockpickingMenu::GetTargetReference();
			if (!target || target->GetHandle().native_handle() == g_lastLockpickTarget) {
				return;
			}
			g_lastLockpickTarget = target->GetHandle().native_handle();
			Console::Run("unlock", target->GetHandle());
			RE::UIMessageQueue::GetSingleton()->AddMessage(RE::LockpickingMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
		}

		void FillMerchant()
		{
			const auto handle = RE::BarterMenu::GetTargetRefHandle();
			if (handle == g_lastBarterTarget) {
				return;
			}
			g_lastBarterTarget = handle;
			RE::NiPointer<RE::TESObjectREFR> ref;
			RE::LookupReferenceByHandle(handle, ref);
			auto actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return;
			}
			auto faction = actor->GetVendorFaction();
			auto chest = faction && faction->vendorData.merchantContainer ? faction->vendorData.merchantContainer : static_cast<RE::TESObjectREFR*>(actor);
			KeepAtLeastIn(chest, RE::TESForm::LookupByID<RE::TESBoundObject>(kGold), kRichMerchantGold);
		}

		// "Buys everything" for a vendor: its buy/sell list set to this one with "everything except the list". The list
		// holds only VendorNoSale (quest items and the like no merchant takes). No list at all doesn't work: items then
		// change hands for nothing (user: gave the Dremora merchant his things and got no gold).
		RE::BGSListForm* BuyAllList()
		{
			static RE::BGSListForm* list = [] {
				auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSListForm>();
				auto made = factory ? factory->Create() : nullptr;
				if (made) {
					if (auto noSale = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("VendorNoSale")) {
						made->AddForm(noSale);
					}
				}
				return made;
			}();
			return list;
		}

		// The summoned merchant is Dragonborn's Dremora merchant (the power DLC2ConjureDremoraMerchant 01EEC6: its effect script
		// brings the persistent DLC2DremoraMerchantRef over with the conjure effects and has him start talking; he trades
		// from DLC2DremoraMerchantFaction's (01EEC4) chest and goes back when the effect ends). Summoned from the menu he buys
		// anything and has plenty of gold, refilled whenever a trade with him opens. Kept under 32 767: the barter menu
		// handles the merchant's gold as a 16-bit count, and above that a sale pays nothing (100 000 broke trading with him).
		constexpr int  kSummonedMerchantGold = 30000;
		bool           g_merchantSummoned = false;
		RE::RefHandle  g_lastMerchantBarter = 0;

		// by local form ID: the game keeps no editor IDs for spells and factions
		RE::TESFaction* DremoraMerchantFaction() { return RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(0x01EEC4, "Dragonborn.esm"); }

		void FillDremoraMerchant()
		{
			auto faction = DremoraMerchantFaction();
			auto chest = faction ? faction->vendorData.merchantContainer : nullptr;
			auto gold = RE::TESForm::LookupByID<RE::TESBoundObject>(kGold);
			if (!chest || !gold) {
				return;
			}
			KeepAtLeastIn(chest, gold, kSummonedMerchantGold);
			// older versions filled him up to 100 000: take the extra back out
			const auto counts = chest->GetInventoryCounts([gold](RE::TESBoundObject& a_item) { return &a_item == gold; });
			const auto it = counts.find(gold);
			if (it != counts.end() && it->second > kSummonedMerchantGold) {
				chest->RemoveItem(gold, it->second - kSummonedMerchantGold, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}

		// A trade restocks a merchant's chest when its days are up (iDaysToRespawnVendor) — gold back to 2000 after the
		// menu has already read it. So the restock is done here at the summon, when due, and the day marked.
		void RestockDremoraMerchantIfDue()
		{
			auto faction = DremoraMerchantFaction();
			auto chest = faction ? faction->vendorData.merchantContainer : nullptr;
			auto calendar = RE::Calendar::GetSingleton();
			if (!chest || !calendar) {
				return;
			}
			const auto today = static_cast<std::uint32_t>(calendar->GetDaysPassed());
			auto       setting = RE::GameSettingCollection::GetSingleton()->GetSetting("iDaysToRespawnVendor");
			const auto every = static_cast<std::uint32_t>(setting ? std::max(1, setting->GetInteger()) : 2);
			if (faction->vendorData.lastDayReset == 0 || today >= faction->vendorData.lastDayReset + every) {
				chest->ResetInventory(false);
			}
			faction->vendorData.lastDayReset = std::max(today, 1u);
		}

		void MerchantStep(RE::PlayerCharacter*)
		{
			if (!RE::UI::GetSingleton()->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
				g_lastMerchantBarter = 0;
				return;
			}
			const auto handle = RE::BarterMenu::GetTargetRefHandle();
			if (handle == g_lastMerchantBarter) {
				return;
			}
			g_lastMerchantBarter = handle;
			RE::NiPointer<RE::TESObjectREFR> ref;
			RE::LookupReferenceByHandle(handle, ref);
			auto actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor && actor->GetVendorFaction() == DremoraMerchantFaction()) {
				FillDremoraMerchant();  // for the next trade: what he paid out this time comes back
			}
		}

		// Ore veins (MineOreScript, vanilla and USSEP) count down ResourceCountCurrent and are spent at 0;
		// -1 means "not mined yet" and the script compares it with nothing else. Holding the counter high
		// keeps a vein going; a spent one is revived the way its own onReset does it.
		constexpr std::int32_t kBottomlessOre = 9999;
		constexpr float        kOreRadius = 8000.0f;
		// After a jump to the map marker: put the player on the ground once the cell there has loaded.
		// The marker's height comes from the map's low-detail terrain and is often off by a lot.
		struct PendingLanding
		{
			RE::NiPoint3                          target;
			std::chrono::steady_clock::time_point until;
		};
		std::optional<PendingLanding> g_landing;

		// The highest surface the player could stand on below a_from: terrain, rocks, ruins, roofs, bridges.
		// A physics ray with the player's own collision filter, so it hits what the player collides with
		// (and not the player).
		std::optional<float> SurfaceBelow(RE::PlayerCharacter* a_player, RE::TESObjectCELL* a_cell, RE::NiPoint3 a_from, float a_toZ)
		{
			auto world = a_cell->GetbhkWorld();
			if (!world) {
				return std::nullopt;
			}
			const float     scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_toZ * scale, 0.0f);
			a_player->GetCollisionFilterInfo(pick.rayInput.filterInfo);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) {
				return std::nullopt;
			}
			return a_from.z + (a_toZ - a_from.z) * pick.rayOutput.hitFraction;
		}

		void Land(RE::PlayerCharacter* a_player)
		{
			if (!g_landing) {
				return;
			}
			if (std::chrono::steady_clock::now() > g_landing->until) {
				logger::warn("Map marker teleport: the cell did not load, height left as is");
				g_landing.reset();
				return;
			}
			auto       cell = a_player->GetParentCell();
			const auto pos = a_player->GetPosition();
			if (!cell || !cell->IsAttached() || !a_player->Is3DLoaded() || std::abs(pos.x - g_landing->target.x) > 512.0f ||
				std::abs(pos.y - g_landing->target.y) > 512.0f) {
				return;  // still on the way
			}
			float ground = 0.0f;
			RE::NiPoint3 probe{ g_landing->target.x, g_landing->target.y, std::max(pos.z, g_landing->target.z) + 10000.0f };
			if (!RE::TES::GetSingleton()->GetLandHeight(probe, ground)) {
				return;  // land not loaded yet
			}
			// Rocks, ruins and houses are not part of the land: cast down from well above both the ground and
			// the marker and take the first thing hit. Water: stand on its surface rather than the sea floor.
			const RE::NiPoint3 top{ g_landing->target.x, g_landing->target.y, std::max(ground, g_landing->target.z) + 5000.0f };
			const auto         solid = SurfaceBelow(a_player, cell, top, ground - 100.0f);
			const float        surface = std::max(solid.value_or(ground), cell->GetExteriorWaterHeight());  // -infinity when no water
			a_player->SetPosition({ g_landing->target.x, g_landing->target.y, surface + 20.0f }, true);
			logger::info("Map marker teleport: landed at {:.0f} (marker said {:.0f}, ground {:.0f}, first surface {})", surface + 20.0f, g_landing->target.z,
				ground, solid ? std::format("{:.0f}", *solid) : std::string{ "none" });
			g_landing.reset();
		}

		// Crafting stations anywhere: a real station of the wanted kind is put on the ground in front of the
		// player, facing them, and the player activates it — walks up, plays the station's animation and gets
		// the game's own crafting menu with every recipe. Once the player has left it and walked away, it is
		// deleted again.
		using Bench = RE::TESFurniture::WorkBenchData::BenchType;

		struct StationKind
		{
			Bench       type;
			const char* keyword;            // editor ID the furniture must have, or null
			const char* without{ nullptr };  // ...and must not have
		};

		constexpr StationKind kStationKinds[] = {
			{ Bench::kEnchanting, "WICraftingEnchanting" },     // Station::kEnchanting
			{ Bench::kAlchemy, "WICraftingAlchemy" },           // kAlchemy
			{ Bench::kCreateObject, "isBlacksmithForge", "CraftingSmithingSkyforge" },  // kForge (an ordinary one, not the huge Skyforge)
			{ Bench::kSmithingWeapon, nullptr },                // kGrindstone
			{ Bench::kSmithingArmor, nullptr },                 // kWorkbench
			{ Bench::kCreateObject, "isSmelter" },              // kSmelter
			{ Bench::kCreateObject, "CraftingTanningRack" },    // kTanningRack
			{ Bench::kCreateObject, "CraftingCookpot" },        // kCookpot
			{ Bench::kCreateObject, "BYOHCraftingOven" },       // kOven (Hearthfire)
			{ Bench::kCreateObject, "DLC2StaffEnchanter" },     // kStaffEnchanter (Dragonborn)
		};

		// Recipes of the special forges, offered at the summoned forge as well.
		constexpr const char* kExtraForgeKeywords[] = { "CraftingSmithingSkyforge", "DLC1CraftingDawnguard", "DLC2CraftingSmithingSkaalForge" };

		RE::TESFurniture* FindStation(Station a_station)
		{
			const auto& kind = kStationKinds[std::to_underlying(a_station)];
			auto keyword = kind.keyword ? RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kind.keyword) : nullptr;
			if (kind.keyword && !keyword) {
				return nullptr;  // e.g. no Hearthfire
			}
			auto without = kind.without ? RE::TESForm::LookupByEditorID<RE::BGSKeyword>(kind.without) : nullptr;
			RE::TESFurniture* best = nullptr;
			for (auto furn : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFurniture>()) {
				if (furn && furn->workBenchData.benchType == kind.type && (!keyword || furn->HasKeyword(keyword)) && !(without && furn->HasKeyword(without)) && furn->GetName() && *furn->GetName() &&
					(!best || furn->GetFormID() < best->GetFormID())) {
					best = furn;  // the lowest form ID: the vanilla station where there is one
				}
			}
			return best;
		}

		struct PendingStation
		{
			RE::ObjectRefHandle                   ref;
			RE::TESFurniture*                     base;
			std::vector<RE::BGSKeyword*>          addedKeywords;
			bool                                  activated{ false };
			std::chrono::steady_clock::time_point until;     // to load
			std::chrono::steady_clock::time_point lastUsed;  // sat at it / had its menu open
		};
		std::optional<PendingStation> g_station;

		void RemoveStation()
		{
			if (!g_station) {
				return;
			}
			for (auto keyword : g_station->addedKeywords) {
				g_station->base->RemoveKeyword(keyword);
			}
			if (auto ref = g_station->ref.get()) {
				ref->Disable();
				ref->SetDelete(true);
			}
			g_station.reset();
		}

		constexpr float kStationGone = 400.0f;  // walked this far from a station it left: it disappears

		void StationStep(RE::PlayerCharacter* a_player)
		{
			if (!g_station) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			auto       ref = g_station->ref.get();
			if (!ref) {
				g_station.reset();
				return;
			}
			if (!g_station->activated) {
				if (now > g_station->until) {
					logger::warn("Station: {:08X} did not load, removed", g_station->base->GetFormID());
					RemoveStation();
					return;
				}
				if (!ref->Get3D()) {
					return;
				}
				ref->ActivateRef(a_player, 0, nullptr, 1, false);
				g_station->activated = true;
				g_station->lastUsed = now;
				return;
			}
			// Out of reach counts as left even if the game still has the player "using" it (teleported off it).
			const bool away = a_player->GetPosition().GetDistance(ref->GetPosition()) > kStationGone || a_player->GetParentCell() != ref->GetParentCell() &&
			                                                                                                  !a_player->GetParentCell()->IsExteriorCell();
			const bool inUse = RE::UI::GetSingleton()->IsMenuOpen(RE::CraftingMenu::MENU_NAME) || (!away && a_player->GetOccupiedFurniture() == g_station->ref);
			if (inUse) {
				g_station->lastUsed = now;
				return;
			}
			// Left alone it stays a while, so walking back to it still works; gone once out of reach.
			if (away || now - g_station->lastUsed > 120s) {
				logger::info("Station: {:08X} left, removed", g_station->base->GetFormID());
				RemoveStation();
			}
		}

		// Immortal followers: every current unique follower (player teammate) and the player's horse is essential
		// — at worst they kneel and get up again — and gets its health topped up, so it rarely even gets there.
		// The game recomputes the reference's own essential flag all the time, so the flag is set on the NPC
		// base instead. The game may write that into the save with the rest of the NPC, so the changed bases
		// are kept in the co-save too: dismissed followers / the switch going off (also after loading) restore
		// exactly those.
		void SetEssential(RE::TESNPC* a_npc, bool a_essential)
		{
			auto& flags = a_npc->actorData.actorBaseFlags;
			if (a_essential) {
				flags.set(RE::ACTOR_BASE_DATA::Flag::kEssential);
			} else {
				flags.reset(RE::ACTOR_BASE_DATA::Flag::kEssential);
			}
		}

		void KeepFollowersAlive(RE::PlayerCharacter* a_player, bool a_enable)
		{
			std::vector<RE::Actor*> protect;
			if (a_enable) {
				RE::ProcessLists::GetSingleton()->ForEachHighActor([&](RE::Actor* a_actor) {
					// Summons and raised dead are teammates too; they stay as they are.
					if (a_actor && a_actor != a_player && a_actor->IsPlayerTeammate() && !a_actor->IsCommandedActor() && !a_actor->IsDead()) {
						protect.push_back(a_actor);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				if (auto horse = a_player->QLastRiddenMount().get(); horse && !horse->IsDead()) {
					protect.push_back(horse.get());
				}
			}
			std::erase_if(g_madeEssential, [&](RE::FormID a_id) {
				const bool keep = std::ranges::any_of(protect, [&](RE::Actor* a_actor) { return a_actor->GetActorBase() && a_actor->GetActorBase()->GetFormID() == a_id; });
				if (!keep) {
					if (auto npc = RE::TESForm::LookupByID<RE::TESNPC>(a_id)) {
						SetEssential(npc, false);
					}
				}
				return !keep;
			});
			for (auto actor : protect) {
				// Only unique NPCs: a generic base (guards, bandits, hired templates) is shared with every copy,
				// enemies included. Those followers just get their health topped up.
				if (auto npc = actor->GetActorBase(); npc && npc->IsUnique() && !npc->IsEssential()) {
					SetEssential(npc, true);
					g_madeEssential.push_back(npc->GetFormID());
					logger::info("Immortal followers: {:08X} \"{}\" made essential", npc->GetFormID(), npc->GetName());
				}
				const auto damage = actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth);
				if (damage < 0.0f) {
					actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, -damage);
				}
			}
		}

		// The Papyrus script object a_script attached to a form (a quest, a reference...).
		RE::BSTSmartPointer<RE::BSScript::Object> ScriptOn(RE::TESForm* a_form, const char* a_script)
		{
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (a_form && policy) {
				vm->FindBoundObject(policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_form->GetFormType()), a_form), a_script, object);
			}
			return object;
		}

		// Starts a quest the story manager would normally start with event data: the aliases that data would
		// fill are made optional just for the start (and must be forced in afterwards). False if it won't start.
		bool StartQuestRelaxed(RE::TESQuest* a_quest)
		{
			std::vector<RE::BGSBaseAlias*> relaxed;
			for (auto alias : a_quest->aliases) {
				if (alias && !alias->flags.any(RE::BGSBaseAlias::FLAGS::kOptional)) {
					alias->flags.set(RE::BGSBaseAlias::FLAGS::kOptional);
					relaxed.push_back(alias);
				}
			}
			bool started = false;
			a_quest->EnsureQuestStarted(started, true);
			for (auto alias : relaxed) {
				alias->flags.reset(RE::BGSBaseAlias::FLAGS::kOptional);
			}
			return started;
		}

		RE::BGSBaseAlias* AliasNamed(RE::TESQuest* a_quest, const char* a_name)
		{
			for (auto alias : a_quest->aliases) {
				if (alias && _stricmp(alias->aliasName.c_str(), a_name) == 0) {
					return alias;
				}
			}
			return nullptr;
		}

		// A form a quest's own script keeps in a property (forms without an editor ID in the game, e.g. factions).
		template <class T>
		T* QuestProperty(RE::TESQuest* a_quest, const char* a_property)
		{
			auto script = ScriptOn(a_quest, std::format("QF_{}_{:08X}", a_quest->GetFormEditorID(), a_quest->GetFormID()).c_str());
			auto var = script ? script->GetVariable(std::format("::{}_var", a_property)) : nullptr;
			return var && var->IsObject() ? var->Unpack<T*>() : nullptr;
		}

		// Thieves Guild city influence. TGRShellScript counts Delvin's / Vex's jobs per city and at 5 sets the
		// stage that offers the city's special job (TGTQ01-04). Riften has none.
		constexpr RE::FormID kTGRShell = 0x00028923;
		constexpr RE::FormID kTG01 = 0x0001F326;  // "Taking Care of Business": the player has joined
		struct GuildCity
		{
			const char*   counter;
			std::uint16_t stage;
		};
		constexpr GuildCity kGuildCities[] = {
			{ "::pTGRWhiterunCount_var", 160 }, { "::pTGRSolitudeCount_var", 170 }, { "::pTGRWindhelmCount_var", 180 }, { "::pTGRMarkarthCount_var", 190 },
		};

		bool JoinedThievesGuild()
		{
			auto tg01 = RE::TESForm::LookupByID<RE::TESQuest>(kTG01);
			return tg01 && tg01->IsCompleted();
		}

		// With the switch on a city's count is kept at 4 or more, so the next job there makes it 5 and the
		// guild's own script offers the special job.
		void KeepGuildCountersHigh()
		{
			if (!JoinedThievesGuild()) {
				return;
			}
			auto shell = ScriptOn(RE::TESForm::LookupByID(kTGRShell), "TGRShellScript");
			for (const auto& city : kGuildCities) {
				if (auto counter = shell ? shell->GetVariable(city.counter) : nullptr; counter && counter->IsInt() && counter->GetSInt() < 4) {
					counter->SetSInt(4);
				}
			}
		}

		// Moves everything in a_from to a_to, enchanted / tempered / named copies with their extra data.
		void MoveAllItems(RE::TESObjectREFR* a_from, RE::TESObjectREFR* a_to)
		{
			for (auto& [object, data] : a_from->GetInventory()) {
				if (!object || data.first <= 0 || object->Is(RE::FormType::LeveledItem)) {
					continue;
				}
				int left = data.first;
				std::vector<RE::ExtraDataList*> lists;
				if (data.second && data.second->extraLists) {
					for (auto list : *data.second->extraLists) {
						if (list) {
							lists.push_back(list);
						}
					}
				}
				for (auto list : lists) {
					const int count = std::min(left, static_cast<int>(list->GetCount()));
					a_from->RemoveItem(object, count, RE::ITEM_REMOVE_REASON::kStoreInContainer, list, a_to);
					left -= count;
				}
				if (left > 0) {
					a_from->RemoveItem(object, left, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_to);
				}
			}
		}

		// Jail: on arrest the game moves the player's things into the hold's "player inventory" chest (and
		// gives them back on release). With the switch on they come straight back.
		void TakeBackJailedItems(RE::PlayerCharacter* a_player)
		{
			for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
				auto chest = faction ? faction->crimeData.factionPlayerInventoryContainer : nullptr;
				if (chest && !chest->GetInventoryCounts().empty()) {
					logger::info("Jail: belongings back from {:08X}", chest->GetFormID());
					MoveAllItems(chest, a_player);
				}
			}
		}

		void ClearAllBounties()
		{
			for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
				if (faction && (faction->GetCrimeGold() > 0 || faction->GetCrimeGoldViolent() > 0)) {
					faction->SetCrimeGold(0);
					faction->SetCrimeGoldViolent(0);
				}
			}
		}

		// Flight: no gravity and no fall damage; WASD flies where the camera looks, Space up, X down. The game
		// recomputes the controller's velocity itself every update, so the player is moved here frame by frame
		// (and held in place when no key is down); a physics ray along each step keeps walls and ground solid,
		// unlike tcl.
		std::optional<float>        g_flightGravity;  // the controller's own value while flying
		std::optional<RE::NiPoint3> g_flightPos;      // where the flight holds the player
		bool                        g_softLanding = false;  // switched off in the air: no fall damage until landing
		std::optional<RE::NiPoint2> g_flightTestMove;     // test bridge: stands in for the movement keys

		bool StepBlocked(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			auto cell = a_player->GetParentCell();
			auto world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return false;
			}
			// Along the step and a body length further (64 units at chest height), so the player stops short.
			auto dir = a_to - a_from;
			const float length = dir.Length();
			if (length < 0.01f) {
				return false;
			}
			dir /= length;
			const RE::NiPoint3 chest{ 0.0f, 0.0f, 64.0f };
			const auto         from = a_from + chest;
			const auto         to = a_to + chest + dir * 48.0f;
			const float        scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData    pick{};
			pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0f);
			a_player->GetCollisionFilterInfo(pick.rayInput.filterInfo);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (pick.rayOutput.HasHit()) {
				return true;
			}
			// Going down: also the feet.
			if (a_to.z < a_from.z) {
				pick = {};
				pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, (a_from.z + 20.0f) * scale, 0.0f);
				pick.rayInput.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, (a_to.z - 10.0f) * scale, 0.0f);
				a_player->GetCollisionFilterInfo(pick.rayInput.filterInfo);
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
				return pick.rayOutput.HasHit();
			}
			return false;
		}

		void FlightStep()
		{
			static auto lastFrame = std::chrono::steady_clock::now();
			const auto  frameNow = std::chrono::steady_clock::now();
			const float dt = std::min(std::chrono::duration<float>(frameNow - lastFrame).count(), 0.1f);
			lastFrame = frameNow;

			auto player = RE::PlayerCharacter::GetSingleton();
			auto controller = player && player->Is3DLoaded() ? player->GetCharController() : nullptr;
			if (!controller) {
				g_flightPos.reset();
				return;
			}
			if (!g_state.flight) {
				if (g_flightGravity) {
					controller->gravity = *g_flightGravity;
					g_flightGravity.reset();
					g_softLanding = true;
				}
				if (g_softLanding) {
					controller->fallTime = 0.0f;
					controller->fallStartHeight = player->GetPosition().z;
					g_softLanding = controller->context.currentState != RE::hkpCharacterStateType::kOnGround;
				}
				g_flightPos.reset();
				return;
			}
			if (!g_flightGravity) {
				g_flightGravity = controller->gravity;
			}
			controller->gravity = 0.0f;
			controller->fallTime = 0.0f;
			controller->fallStartHeight = player->GetPosition().z;
			// A teleport, loading screen or the like moved the player: fly on from there.
			if (!g_flightPos || g_flightPos->GetDistance(player->GetPosition()) > 300.0f) {
				g_flightPos = player->GetPosition();
			}

			RE::NiPoint3 velocity;
			auto camera = RE::Main::WorldRootCamera();
			if (camera && !RE::UI::GetSingleton()->GameIsPaused() && !Input::IsMenuOpen()) {
				// NiCamera: column 0 of the rotation is the view direction, column 2 points to the right.
				const auto&        r = camera->world.rotate;
				const RE::NiPoint3 forward{ r.entry[0][0], r.entry[1][0], r.entry[2][0] };
				const RE::NiPoint3 right{ r.entry[0][2], r.entry[1][2], r.entry[2][2] };
				const auto         move = g_flightTestMove ? *g_flightTestMove : RE::PlayerControls::GetSingleton()->data.moveInputVec;
				const float        up = (Input::IsKeyHeld(RE::BSKeyboardDevice::Key::kSpacebar) ? 1.0f : 0.0f) -
				                 (Input::IsKeyHeld(RE::BSKeyboardDevice::Key::kX) ? 1.0f : 0.0f);
				velocity = (forward * move.y + right * move.x) * g_state.flightSpeed;
				velocity.z += up * g_state.flightSpeed * 0.6f;
			}
			const auto target = *g_flightPos + velocity * dt;
			if (velocity.Length() > 0.0f && !StepBlocked(player, *g_flightPos, target)) {
				g_flightPos = target;
			}
			// No drift: a jump's push (Space jumps as well) would otherwise carry on forever without gravity.
			controller->SetLinearVelocityImpl(RE::hkVector4(0.0f, 0.0f, 0.0f, 0.0f));
			player->SetPosition(*g_flightPos, true);
		}

		// Merchants buy anything: a vendor's buy/sell list says what it trades; "everything except BuyAllList()"
		// is everything a merchant may take. Restored when switched off.
		struct VendorOriginal
		{
			RE::BGSListForm* list;
			bool         notBuySell;
		};
		std::unordered_map<RE::TESFaction*, VendorOriginal> g_vendorOriginals;

		void ApplyMerchantsBuyAll(bool a_enable)
		{
			if (a_enable == !g_vendorOriginals.empty()) {
				return;
			}
			if (!a_enable) {
				for (auto& [faction, original] : g_vendorOriginals) {
					faction->vendorData.vendorSellBuyList = original.list;
					faction->vendorData.vendorValues.notBuySell = original.notBuySell;
				}
				g_vendorOriginals.clear();
				return;
			}
			for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
				if (faction && faction->vendorData.merchantContainer) {
					g_vendorOriginals.emplace(faction, VendorOriginal{ faction->vendorData.vendorSellBuyList, faction->vendorData.vendorValues.notBuySell });
					faction->vendorData.vendorSellBuyList = BuyAllList();
					faction->vendorData.vendorValues.notBuySell = true;
				}
			}
			logger::info("Merchants buy anything: {} vendors", g_vendorOriginals.size());
		}

		// No random dragon attacks: the main quest's own switch for them (MQ106TurnOffRandomDragons), held at 1.
		// Globals are saved, so the value it had before is kept in the co-save and put back when switched off.
		void ApplyNoDragonAttacks(bool a_enable)
		{
			auto global = RE::TESForm::LookupByEditorID<RE::TESGlobal>("MQ106TurnOffRandomDragons");
			if (!global) {
				return;
			}
			if (a_enable) {
				if (g_state.dragonsGlobalBefore < 0.0f) {
					g_state.dragonsGlobalBefore = global->value;
				}
				global->value = 1.0f;
			} else if (g_state.dragonsGlobalBefore >= 0.0f) {
				global->value = g_state.dragonsGlobalBefore;
				g_state.dragonsGlobalBefore = -1.0f;
			}
		}

		// Persuasion: the dialogue checks compare Speech with the SpeechVeryEasy..VeryHard globals, held at 0 while on (originals in
		// the co-save, globals are saved). Intimidation and bribes are engine condition functions, wrapped in the command table.
		constexpr const char* kSpeechGlobals[] = { "SpeechVeryEasy", "SpeechEasy", "SpeechAverage", "SpeechHard", "SpeechVeryHard" };

		void ApplyPersuadeAlways(bool a_enable)
		{
			for (std::size_t i = 0; i < std::size(kSpeechGlobals); ++i) {
				auto  global = RE::TESForm::LookupByEditorID<RE::TESGlobal>(kSpeechGlobals[i]);
				auto& before = g_state.speechBefore[i];
				if (!global) {
					continue;
				}
				if (a_enable) {
					if (before < 0.0f) {
						before = global->value;
					}
					global->value = 0.0f;
				} else if (before >= 0.0f) {
					global->value = before;
					before = -1.0f;
				}
			}
		}

		template <RE::FUNCTION_DATA::FunctionID ID>
		struct AlwaysPassHook
		{
			static bool Thunk(RE::TESObjectREFR* a_this, void* a_param1, void* a_param2, double& a_result)
			{
				if (g_state.persuadeAlways) {
					a_result = 1.0;
					return true;
				}
				return original(a_this, a_param1, a_param2, a_result);
			}
			static void Install(const char* a_name)
			{
				auto command = RE::SCRIPT_FUNCTION::LocateScriptCommand(a_name);
				if (!command || !command->conditionFunction) {
					logger::warn("Persuasion: no condition function {}", a_name);
					return;
				}
				original = command->conditionFunction;
				command->conditionFunction = &Thunk;
				logger::info("Persuasion: {} wrapped", a_name);
			}
			static inline RE::SCRIPT_FUNCTION::Condition_t* original = nullptr;
		};

		constexpr float kFastDrawSpeed = 3.0f;
		bool            g_bowBoosted = false;  // the weapon speed currently includes the fast-draw boost

		bool BowInHand(RE::PlayerCharacter* a_player)
		{
			auto weapon = a_player->GetEquippedObject(false);
			auto weap = weapon ? weapon->As<RE::TESObjectWEAP>() : nullptr;
			return weap && (weap->IsBow() || weap->IsCrossbow());
		}

		// Freeze time: the timescale held at 0, the old one put back when switched off.
		std::optional<float> g_frozenTimescale;

		void KeepTimeFrozen(bool a_enable)
		{
			auto calendar = RE::Calendar::GetSingleton();
			if (!calendar || !calendar->timeScale) {
				return;
			}
			if (a_enable) {
				if (!g_frozenTimescale) {
					g_frozenTimescale = calendar->timeScale->value;
				}
				calendar->timeScale->value = 0.0f;
			} else if (g_frozenTimescale) {
				calendar->timeScale->value = *g_frozenTimescale;
				g_frozenTimescale.reset();
			}
		}

		// Slow motion while a bow or crossbow is drawn, like an archery perk without the perk.
		bool g_aimSlowed = false;

		void AimSlowmo(RE::PlayerCharacter* a_player, bool a_enable)
		{
			const auto state = a_player->AsActorState()->GetAttackState();
			const bool aiming = a_enable && (state == RE::ATTACK_STATE_ENUM::kBowDraw || state == RE::ATTACK_STATE_ENUM::kBowAttached ||
			                                    state == RE::ATTACK_STATE_ENUM::kBowDrawn);
			if (aiming != g_aimSlowed) {
				g_aimSlowed = aiming;
				Console::Run(std::format("sgtm {:.2f}", aiming ? 0.3f : g_state.worldSpeed));
			}
		}

		// Calls a Papyrus function of the script a_script on the first quest that has it.
		bool CallQuestScript(const char* a_script, const char* a_function, RE::BSScript::IFunctionArguments* a_args)
		{
			auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) {
				delete a_args;
				return false;
			}
			for (auto quest : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESQuest>()) {
				if (!quest || !ScriptOn(quest, a_script)) {
					continue;
				}
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
				const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(quest->GetFormType()), quest);
				vm->DispatchMethodCall(handle, a_script, a_function, a_args, callback);
				logger::info("Called {}.{} on {}", a_script, a_function, quest->GetFormEditorID());
				return true;
			}
			delete a_args;
			return false;
		}

		// Race change keeps the skills: the race menu resets them to the new race's, so they are put back after.
		std::vector<std::pair<RE::ActorValue, float>> g_skillsBeforeRaceMenu;

		class RaceMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && !a_event->opening && a_event->menuName == RE::RaceSexMenu::MENU_NAME && !g_skillsBeforeRaceMenu.empty()) {
					auto skills = std::move(g_skillsBeforeRaceMenu);
					g_skillsBeforeRaceMenu.clear();
					SKSE::GetTaskInterface()->AddTask([skills]() {
						if (auto player = RE::PlayerCharacter::GetSingleton()) {
							for (const auto& [av, value] : skills) {
								player->AsActorValueOwner()->SetBaseActorValue(av, value);
							}
							logger::info("Race menu closed: {} skills put back", skills.size());
						}
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		std::set<RE::VMHandle> g_bottomlessVeins;  // veins changed this session, put back when the toggle is off

		RE::BSScript::Variable* VeinCounter(RE::BSTSmartPointer<RE::BSScript::Object>& a_script)
		{
			return a_script ? a_script->GetVariable("::ResourceCountCurrent_var") : nullptr;
		}

		RE::BSTSmartPointer<RE::BSScript::Object> FindVeinScript(RE::VMHandle a_handle)
		{
			RE::BSTSmartPointer<RE::BSScript::Object> script;
			if (auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
				vm->FindBoundObject(a_handle, "MineOreScript", script);
			}
			return script;
		}

		// Calls a_fn for every node of the 18 skill trees (mod perk overhauls edit these trees too).
		void ForEachPerkNode(const std::function<void(RE::BGSSkillPerkTreeNode*)>& a_fn)
		{
			std::unordered_set<RE::BGSSkillPerkTreeNode*> seen;
			std::vector<RE::BGSSkillPerkTreeNode*>        todo;
			for (auto av = RE::ActorValue::kOneHanded; av <= RE::ActorValue::kEnchanting; av = static_cast<RE::ActorValue>(std::to_underlying(av) + 1)) {
				if (auto info = RE::ActorValueList::GetActorValueInfo(av); info && info->perkTree) {
					todo.push_back(info->perkTree);
				}
			}
			while (!todo.empty()) {
				auto node = todo.back();
				todo.pop_back();
				if (!node || !seen.insert(node).second) {
					continue;
				}
				a_fn(node);
				for (auto child : node->children) {
					todo.push_back(child);
				}
			}
		}

		// The ranks of a perk: the perk itself and its "next perk" chain.
		std::vector<RE::BGSPerk*> PerkRanks(RE::BGSPerk* a_perk)
		{
			std::vector<RE::BGSPerk*> ranks;
			for (auto perk = a_perk; perk && ranks.size() < 16 && std::ranges::find(ranks, perk) == ranks.end(); perk = perk->nextPerk) {
				ranks.push_back(perk);
			}
			return ranks;
		}

		// Auto perks: what the perk screen would let the player take right now (a parent perk is
		// owned, the skill level and other conditions are met), one rank per node per pass.
		void AddEarnedPerks(RE::PlayerCharacter* a_player)
		{
			ForEachPerkNode([&](RE::BGSSkillPerkTreeNode* a_node) {
				if (!a_node->perk) {
					return;  // the tree root
				}
				const bool parentOwned = a_node->parents.empty() || std::ranges::any_of(a_node->parents, [&](RE::BGSSkillPerkTreeNode* a_parent) {
					return !a_parent || !a_parent->perk || a_player->HasPerk(a_parent->perk);
				});
				RE::BGSPerk* previous = nullptr;
				for (auto rank : PerkRanks(a_node->perk)) {
					if (a_player->HasPerk(rank)) {
						previous = rank;
						continue;
					}
					if ((previous || parentOwned) && rank->perkConditions.IsTrue(a_player, a_player)) {
						a_player->AddPerk(rank);
						g_learned.push_back(rank->GetFormID());
						logger::info("Auto perk: {}", rank->GetName());
					}
					return;
				}
			});
		}

		void KeepOreVeins(RE::PlayerCharacter* a_player)
		{
			auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) {
				return;
			}
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, kOreRadius, [&](RE::TESObjectREFR* a_ref) {
				auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
				if (!base || !base->Is(RE::FormType::Activator)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto handle = policy->GetHandleForObject(a_ref->GetFormType(), a_ref);
				auto       script = FindVeinScript(handle);
				auto       counter = VeinCounter(script);
				if (!counter || !counter->IsInt() || counter->GetSInt() == kBottomlessOre) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (counter->GetSInt() == 0) {
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(script, "ClearDestruction", RE::MakeFunctionArguments(), none);
					vm->DispatchMethodCall(script, "SetDestroyed", RE::MakeFunctionArguments(false), none);
				}
				counter->SetSInt(kBottomlessOre);
				g_bottomlessVeins.insert(handle);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		void RestoreOreVeins()
		{
			for (const auto handle : g_bottomlessVeins) {
				auto script = FindVeinScript(handle);
				auto counter = VeinCounter(script);
				auto total = script ? script->GetVariable("::ResourceCountTotal_var") : nullptr;
				if (counter && total && counter->GetSInt() > total->GetSInt()) {
					counter->SetSInt(total->GetSInt());
				}
			}
			g_bottomlessVeins.clear();
		}

		// Fast travel = the map is open, then a loading screen. The calendar is remembered while
		// the map is open and put back once the loading screen after it is gone.
		struct CalendarSnapshot
		{
			float year, month, day, hour, daysPassed;
		};

		void KeepTimeOnFastTravel()
		{
			static std::optional<CalendarSnapshot> snapshot;
			static bool                            loading = false;
			static int                             ticksWithoutLoading = 0;

			auto ui = RE::UI::GetSingleton();
			auto calendar = RE::Calendar::GetSingleton();
			if (!ui || !calendar || !calendar->gameDaysPassed) {
				return;
			}
			const bool mapOpen = ui->IsMenuOpen(RE::MapMenu::MENU_NAME);
			const bool loadingOpen = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
			if (mapOpen) {
				snapshot = CalendarSnapshot{ calendar->gameYear->value, calendar->gameMonth->value, calendar->gameDay->value, calendar->gameHour->value,
					calendar->gameDaysPassed->value };
				loading = false;
				ticksWithoutLoading = 0;
				return;
			}
			if (!snapshot) {
				return;
			}
			if (loadingOpen) {
				loading = true;
				return;
			}
			if (!loading) {
				if (++ticksWithoutLoading > 20) {  // the map was closed without travelling
					snapshot.reset();
				}
				return;
			}
			calendar->gameYear->value = snapshot->year;
			calendar->gameMonth->value = snapshot->month;
			calendar->gameDay->value = snapshot->day;
			calendar->gameHour->value = snapshot->hour;
			calendar->gameDaysPassed->value = snapshot->daysPassed;
			logger::info("Fast travel: game time restored");
			snapshot.reset();
			loading = false;
		}

		void KeepAtLeast(RE::PlayerCharacter* a_player, RE::TESBoundObject* a_obj, int a_min)
		{
			if (!a_obj) {
				return;
			}
			const auto counts = a_player->GetInventoryCounts([a_obj](RE::TESBoundObject& a_item) { return &a_item == a_obj; });
			const auto it = counts.find(a_obj);
			const int  have = it != counts.end() ? it->second : 0;
			if (have < a_min) {
				a_player->AddObjectToContainer(a_obj, nullptr, a_min - have, nullptr);
			}
		}

		void KeepPoison(RE::PlayerCharacter* a_player)
		{
			for (const bool left : { false, true }) {
				auto entry = a_player->GetEquippedEntryData(left);
				if (!entry || !entry->extraLists) {
					continue;
				}
				for (auto xList : *entry->extraLists) {
					if (auto poison = xList ? xList->GetByType<RE::ExtraPoison>() : nullptr; poison && poison->count < kInfinitePoisonMin) {
						poison->count = kInfinitePoisonMin;
					}
				}
			}
		}

		void CastOnSelf(RE::PlayerCharacter* a_player, RE::FormID a_spell)
		{
			auto spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
			auto caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (spell && caster) {
				caster->CastSpellImmediate(spell, false, a_player, 1.0f, false, 0.0f, a_player);
			}
		}

		// Paralysis immunity, endless night eye and long buffs all work on the active effect list.
		void TendEffects(RE::PlayerCharacter* a_player, const State& a_state, bool a_dropNightEye)
		{
			auto list = a_player->AsMagicTarget()->GetActiveEffectList();
			if (!list) {
				return;
			}
			std::vector<RE::ActiveEffect*> dispel;
			bool                           nightEye = false;
			for (auto effect : *list) {
				auto base = effect ? effect->GetBaseObject() : nullptr;
				if (!base) {
					continue;
				}
				if (base->GetFormID() == kNightEyeEffect) {
					nightEye = true;
					if (a_dropNightEye) {
						dispel.push_back(effect);
					} else if (a_state.nightEye) {
						effect->elapsedSeconds = 0.0f;
					}
				} else if (a_state.immunity && base->HasArchetype(RE::EffectSetting::Archetype::kParalysis)) {
					dispel.push_back(effect);
				} else if (a_state.eternalBuffs && effect->duration >= kLongEffectSeconds) {
					effect->elapsedSeconds = 0.0f;
				}
			}
			// Dispelling changes the list, so it happens after the walk.
			for (auto effect : dispel) {
				effect->Dispel(true);
			}
			if (a_state.nightEye && !nightEye && !a_dropNightEye) {
				CastOnSelf(a_player, kNightEyeSpell);
			}
		}

		// Soul gems. A pre-filled gem (SoulGemPettyFilled...) is its own form next to the empty one, with the
		// same model and capacity (only the black gem links its empty form to the filled one); a gem the
		// player filled is the empty form plus an ExtraSoul.
		// "Any soul in any gem": every gem takes grand and black (NPC) souls. Restored when switched off.
		struct GemOriginal
		{
			REX::EnumSet<RE::SOUL_LEVEL, std::uint8_t> capacity;
			bool                                       npcSouls;
		};
		std::unordered_map<RE::TESSoulGem*, GemOriginal> g_gemOriginals;

		// As the plugin made it, also while "any soul in any gem" has changed it.
		std::pair<RE::SOUL_LEVEL, bool> GemKind(RE::TESSoulGem* a_gem)
		{
			if (auto it = g_gemOriginals.find(a_gem); it != g_gemOriginals.end()) {
				return { it->second.capacity.get(), it->second.npcSouls };
			}
			return { a_gem->GetMaximumCapacity(), a_gem->CanHoldNPCSoul() };
		}

		bool SameGem(RE::TESSoulGem* a_a, RE::TESSoulGem* a_b)
		{
			return GemKind(a_a) == GemKind(a_b) && a_a->GetModel() &&
			       a_b->GetModel() && _stricmp(a_a->GetModel(), a_b->GetModel()) == 0;
		}

		RE::TESSoulGem* EmptyGem(RE::TESSoulGem* a_gem)
		{
			if (a_gem->GetContainedSoul() == RE::SOUL_LEVEL::kNone) {
				return a_gem;
			}
			RE::TESSoulGem* best = nullptr;
			for (auto gem : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESSoulGem>()) {
				if (gem && gem->GetContainedSoul() == RE::SOUL_LEVEL::kNone && (gem->linkedSoulGem == a_gem || SameGem(gem, a_gem)) &&
					(!best || gem->GetFormID() < best->GetFormID())) {
					best = gem;
				}
			}
			return best;
		}

		// The pre-filled form of an empty gem, full to its capacity.
		RE::TESSoulGem* FullGem(RE::TESSoulGem* a_empty)
		{
			if (a_empty->linkedSoulGem && a_empty->linkedSoulGem->GetContainedSoul() != RE::SOUL_LEVEL::kNone) {
				return a_empty->linkedSoulGem;
			}
			RE::TESSoulGem* best = nullptr;  // the lowest form ID: the vanilla one where mods add look-alikes
			for (auto gem : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESSoulGem>()) {
				if (gem && gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone && gem->GetContainedSoul() == GemKind(gem).first && SameGem(gem, a_empty) &&
					(!best || gem->GetFormID() < best->GetFormID())) {
					best = gem;
				}
			}
			return best;
		}

		std::unordered_map<RE::FormID, int> g_gemRemovalsToIgnore;  // the menu's own swaps (FillSoulGems)
		std::unordered_set<RE::FormID>      g_playerDropped;         // what the player dropped (form IDs): the autoloot leaves it be

		// Using a gem at the enchanting table or to recharge a weapon removes it from the player without
		// putting it anywhere (dropping makes a reference, selling / storing names the new container).
		class ContainerSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (a_event && a_event->oldContainer == 0x14 && a_event->newContainer == 0 && a_event->reference) {
					g_playerDropped.insert(a_event->reference.native_handle());  // holds the dropped reference's form ID, not a handle
				}
				if (a_event && a_event->oldContainer == 0x14 && a_event->newContainer != 0 && std::ranges::find(g_stashes, a_event->newContainer) == g_stashes.end()) {
					if (auto chest = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->newContainer); chest && !chest->Is(RE::FormType::ActorCharacter)) {
						g_stashes.push_back(a_event->newContainer);
					}
				}
				if (!a_event || a_event->oldContainer != 0x14 || a_event->newContainer != 0 || a_event->reference || a_event->itemCount <= 0) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (auto it = g_gemRemovalsToIgnore.find(a_event->baseObj); it != g_gemRemovalsToIgnore.end()) {
					it->second -= a_event->itemCount;
					if (it->second <= 0) {
						g_gemRemovalsToIgnore.erase(it);
					}
					return RE::BSEventNotifyControl::kContinue;
				}
				auto gem = RE::TESForm::LookupByID<RE::TESSoulGem>(a_event->baseObj);
				if (!gem || (!g_state.soulGemsKeep && !g_state.soulGemsFull)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto empty = EmptyGem(gem);
				const auto full = !g_state.soulGemsFull ? nullptr : gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone ? gem : empty ? FullGem(empty) : nullptr;
				const auto back = full ? full : empty;
				if (!back) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const int  count = a_event->itemCount;
				logger::info("Soul gem used: {:08X} -> {:08X} back x{}", gem->GetFormID(), back->GetFormID(), a_event->itemCount);
				SKSE::GetTaskInterface()->AddTask([back, count]() {
					if (auto player = RE::PlayerCharacter::GetSingleton()) {
						player->AddObjectToContainer(back, nullptr, count, nullptr);
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void ApplyAnySoul(bool a_enable)
		{
			if (a_enable == !g_gemOriginals.empty()) {
				return;
			}
			if (!a_enable) {
				for (auto& [gem, original] : g_gemOriginals) {
					gem->soulCapacity = original.capacity;
					if (!original.npcSouls) {
						gem->formFlags &= ~RE::TESSoulGem::RecordFlags::kCanHoldNPCSoul;
					}
				}
				g_gemOriginals.clear();
				return;
			}
			for (auto gem : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESSoulGem>()) {
				if (!gem || gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone) {
					continue;  // pre-filled ones keep what they hold
				}
				g_gemOriginals.emplace(gem, GemOriginal{ gem->soulCapacity, gem->CanHoldNPCSoul() });
				gem->soulCapacity = RE::SOUL_LEVEL::kGrand;
				gem->formFlags |= RE::TESSoulGem::RecordFlags::kCanHoldNPCSoul;
			}
			logger::info("Any soul in any gem: {} gems", g_gemOriginals.size());
		}

		class LevelSink final : public RE::BSTEventSink<RE::LevelIncrease::Event>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::LevelIncrease::Event*, RE::BSTEventSource<RE::LevelIncrease::Event>*) override
			{
				const int extra = static_cast<int>(std::round(g_state.perksPerLevel)) - 1;
				if (extra > 0) {
					AddPerkPoints(extra);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			const OriginalsRecord originals{ g_originals.carryWeight.has_value(), g_originals.speedMult.has_value(), g_originals.shoutRecovery.has_value(),
				g_originals.carryWeight.value_or(0.0f), g_originals.speedMult.value_or(0.0f), g_originals.shoutRecovery.value_or(0.0f) };
			// Version 6: originals, then "name=value" pairs for every State field.
			const auto fields = Fields(g_state);
			if (!a_intfc->OpenRecord(kRecordState, kRecordVersion) || !a_intfc->WriteRecordData(originals) ||
				!a_intfc->WriteRecordData(static_cast<std::uint32_t>(fields.size()))) {
				logger::error("Co-save: failed to write state");
			}
			for (const auto& f : fields) {
				WriteString(a_intfc, f.name);
				const float value = f.flag ? (*f.flag ? 1.0f : 0.0f) : *f.number;
				a_intfc->WriteRecordData(value);
			}
			if (g_storage && a_intfc->OpenRecord(kRecordStorage, 1)) {
				a_intfc->WriteRecordData(g_storage);
			}
			if (a_intfc->OpenRecord(kRecordAvOriginals, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_avOriginals.size()));
				for (const auto& [av, value] : g_avOriginals) {
					a_intfc->WriteRecordData(static_cast<std::uint32_t>(av));
					a_intfc->WriteRecordData(value);
				}
			}
			if (a_intfc->OpenRecord(kRecordLearned, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_learned.size()));
				for (auto formID : g_learned) {
					a_intfc->WriteRecordData(formID);
				}
			}
			if (a_intfc->OpenRecord(kRecordStashes, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_stashes.size()));
				for (auto formID : g_stashes) {
					a_intfc->WriteRecordData(formID);
				}
			}
			if (a_intfc->OpenRecord(kRecordEssential, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_madeEssential.size()));
				for (auto formID : g_madeEssential) {
					a_intfc->WriteRecordData(formID);
				}
			}
			if (a_intfc->OpenRecord(kRecordBoostedEnchantments, 1)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_boostedEnchantments.size()));
				for (const auto& [created, original] : g_boostedEnchantments) {
					a_intfc->WriteRecordData(created);
					a_intfc->WriteRecordData(original);
				}
			}
			if (a_intfc->OpenRecord(kRecordBaseEnchantments, 1)) {
				Enchanter::Save(a_intfc);
			}
			if (a_intfc->OpenRecord(kRecordPoints, kPointsVersion)) {
				a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_points.size()));
				for (const auto& point : g_points) {
					a_intfc->WriteRecordData(point.marker);
					WriteString(a_intfc, point.name);
					WriteString(a_intfc, point.place);
				}
			}
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type, version, length;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type == kRecordPoints && version == kPointsVersion) {
					ReadPoints(a_intfc);
					continue;
				}
				if ((type == kRecordLearned || type == kRecordEssential || type == kRecordStashes) && version == 1) {
					auto&         list = type == kRecordLearned ? g_learned : type == kRecordStashes ? g_stashes : g_madeEssential;
					std::uint32_t count = 0;
					a_intfc->ReadRecordData(count);
					for (std::uint32_t i = 0; i < count; ++i) {
						RE::FormID formID = 0;
						if (a_intfc->ReadRecordData(formID) && a_intfc->ResolveFormID(formID, formID)) {
							list.push_back(formID);
						}
					}
					continue;
				}
				if (type == kRecordBaseEnchantments && version == 1) {
					Enchanter::Load(a_intfc);
					continue;
				}
				if (type == kRecordBoostedEnchantments && version == 1) {
					std::uint32_t count = 0;
					a_intfc->ReadRecordData(count);
					for (std::uint32_t i = 0; i < count; ++i) {
						RE::FormID created = 0, original = 0;
						if (a_intfc->ReadRecordData(created) && a_intfc->ReadRecordData(original) && a_intfc->ResolveFormID(created, created) &&
							a_intfc->ResolveFormID(original, original)) {
							g_boostedEnchantments[created] = original;
						}
					}
					continue;
				}
				if (type == kRecordAvOriginals && version == 1) {
					std::uint32_t count = 0;
					a_intfc->ReadRecordData(count);
					for (std::uint32_t i = 0; i < count; ++i) {
						std::uint32_t av = 0;
						float         value = 0.0f;
						if (a_intfc->ReadRecordData(av) && a_intfc->ReadRecordData(value)) {
							g_avOriginals[static_cast<RE::ActorValue>(av)] = value;
						}
					}
					continue;
				}
				if (type == kRecordStorage && version == 1) {
					RE::FormID storage = 0;
					if (a_intfc->ReadRecordData(storage) && a_intfc->ResolveFormID(storage, storage)) {
						g_storage = storage;
					}
					continue;
				}
				if (type == kRecordState && (version == 5 || version == 6) && length >= sizeof(OriginalsRecord)) {
					OriginalsRecord originals{};
					a_intfc->ReadRecordData(originals);
					State state{};
					if (version == 6) {
						std::uint32_t count = 0;
						a_intfc->ReadRecordData(count);
						auto fields = Fields(state);
						for (std::uint32_t i = 0; i < count; ++i) {
							std::string name;
							float       value = 0.0f;
							if (!ReadString(a_intfc, name) || !a_intfc->ReadRecordData(value)) {
								break;
							}
							// unknown names (removed fields) are skipped; missing ones keep their defaults
							for (auto& f : fields) {
								if (name == f.name) {
									if (f.flag) {
										*f.flag = value != 0.0f;
									} else {
										*f.number = value;
									}
								}
							}
						}
					} else {
						// Raw bytes of whatever State was then. Its trailing padding was saved as zeros
						// and lands on newer bools, so those near the end get their defaults back.
						const auto size = std::min<std::uint32_t>(length - sizeof(OriginalsRecord), sizeof(State));
						a_intfc->ReadRecordData(&state, size);
						const State defaults{};
						const auto  base = reinterpret_cast<const std::byte*>(&state);
						auto        fields = Fields(state);
						auto        defaultFields = Fields(const_cast<State&>(defaults));
						for (std::size_t i = 0; i < fields.size(); ++i) {
							const auto offset = fields[i].flag ? reinterpret_cast<const std::byte*>(fields[i].flag) - base :
							                                     reinterpret_cast<const std::byte*>(fields[i].number) - base;
							if (static_cast<std::uint32_t>(offset) + 4 > size) {
								if (fields[i].flag) {
									*fields[i].flag = *defaultFields[i].flag;
								} else {
									*fields[i].number = *defaultFields[i].number;
								}
							}
						}
					}
					MigrateLegacy(state);
					g_state = state;
					if (originals.hasCarryWeight) {
						g_originals.carryWeight = originals.carryWeight;
					}
					if (originals.hasSpeedMult) {
						g_originals.speedMult = originals.speedMult;
					}
					if (originals.hasShoutRecovery) {
						g_originals.shoutRecovery = originals.shoutRecovery;
					}
					logger::info("Co-save: state restored");
					continue;
				}
				SaveRecord record{};
				if (type == kRecordState && version == 4 && length == sizeof(SaveRecordV4)) {
					SaveRecordV4 old{};
					if (!a_intfc->ReadRecordData(old)) {
						continue;
					}
					static_cast<StateV4&>(record.state) = old.state;
					record.hasCarryWeight = old.hasCarryWeight;
					record.hasSpeedMult = old.hasSpeedMult;
					record.hasShoutRecovery = old.hasShoutRecovery;
					record.carryWeight = old.carryWeight;
					record.speedMult = old.speedMult;
					record.shoutRecovery = old.shoutRecovery;
				} else if (type == kRecordState && version == 3 && length == sizeof(SaveRecordV3)) {
					// Older save: same toggles, the new ones stay at their defaults.
					SaveRecordV3 old{};
					if (!a_intfc->ReadRecordData(old)) {
						continue;
					}
					static_cast<StateV3&>(record.state) = old.state;
					record.hasCarryWeight = old.hasCarryWeight;
					record.hasSpeedMult = old.hasSpeedMult;
					record.hasShoutRecovery = old.hasShoutRecovery;
					record.carryWeight = old.carryWeight;
					record.speedMult = old.speedMult;
					record.shoutRecovery = old.shoutRecovery;
				} else {
					continue;
				}
				g_state = record.state;
				MigrateLegacy(g_state);
				if (record.hasCarryWeight) {
					g_originals.carryWeight = record.carryWeight;
				}
				if (record.hasSpeedMult) {
					g_originals.speedMult = record.speedMult;
				}
				if (record.hasShoutRecovery) {
					g_originals.shoutRecovery = record.shoutRecovery;
				}
				logger::info("Co-save: state restored");
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			g_state = {};
			g_originals = {};
			g_avOriginals.clear();
			g_points.clear();
			g_learned.clear();
			g_boostedEnchantments.clear();
			Enchanter::Revert();
			// Base forms keep runtime changes until the game quits: undo them before another save loads.
			for (auto formID : g_madeEssential) {
				if (auto npc = RE::TESForm::LookupByID<RE::TESNPC>(formID)) {
					SetEssential(npc, false);
				}
			}
			g_madeEssential.clear();
			g_station.reset();
			g_merchantSummoned = false;
			ApplyAnySoul(false);
			ApplyMerchantsBuyAll(false);
			KeepTimeFrozen(false);
			g_storage = 0;
			g_stashes.clear();
			g_bottomlessVeins.clear();
		}
	}

	State& Get() { return g_state; }

	std::vector<Field> Fields(State& s)
	{
#define B(n) Field{ #n, &s.n, nullptr }
#define F(n) Field{ #n, nullptr, &s.n }
		return {
			B(infiniteHealth), B(infiniteStamina), B(infiniteMagicka), B(noShoutCooldown), B(noCarryLimit), F(speedMult), F(jumpMult),
			B(noFallDamage), B(easyPickpocket), B(infiniteAmmo), B(infiniteCharge), B(weightlessMisc), F(damageDealtMult), F(damageTakenMult),
			F(skillXpMult), F(worldSpeed), B(lockpickHud),
			B(noSpellCost), B(instantCast), B(infinitePoison), B(immunity), B(essential), B(nightEye), B(waterWalking), B(infiniteTorch),
			B(eternalBuffs), B(infiniteLockpicks), B(unlimitedTraining), F(attackSpeed), F(smithingBoost), F(alchemyBoost), F(enchantingBoost),
			F(perksPerLevel),
			B(invisibility), B(powersNoCooldown), B(freeCraft), B(craftNoPerks), B(autoUnlock), B(fairTrade), B(richMerchants), B(esp), F(espRange),
			B(espEnemies), B(espNpcs), B(espFollowers), B(espCorpses), B(espContainers), B(espItems), B(espFlora), B(espInteract), B(espTraps), B(timelessTravel),
			B(playerNormalSpeed), B(whBoxes), B(whSkeleton), B(whHealth), B(whSnaplines), B(whInfo), B(whGlow),
			B(infiniteOre), B(autoLoot), F(lootRadius),
			B(autoPerks),
			B(fastTravelAnywhere),
			B(fastTravelIgnoreBans),
			F(enchantEffects), B(enchantAnything),
			B(immortalFollowers),
			B(soulGemsKeep), B(soulGemsFull), B(anySoulInAnyGem),
			B(thievesGuildQuick), B(noBounty), B(keepItemsInJail),
			B(flight), F(flightSpeed),
			B(waterBreathing), B(oneHitKill), B(freezeTime), B(aimSlowmo), B(merchantsBuyAll),
			B(noDragonAttacks), F(dragonsGlobalBefore), B(fastBowDraw), F(skillCap), B(aimbot), B(killmoves), F(killmoveChance), B(killmoveAnyEnemy), F(decapChance), B(decapNoPerk), B(noKillmovesOnPlayer),
			B(persuadeAlways), F(speechBefore[0]), F(speechBefore[1]), F(speechBefore[2]), F(speechBefore[3]), F(speechBefore[4]),
			F(alteration.power), F(alteration.duration), F(alteration.cost), F(conjuration.power), F(conjuration.duration), F(conjuration.cost), F(destruction.power), F(destruction.duration), F(destruction.cost),
			F(illusion.power), F(illusion.duration), F(illusion.cost), F(restoration.power), F(restoration.duration), F(restoration.cost),
			F(summonExtra), F(incomingMagic), F(incomingMagicDuration), F(dualCastPower), F(dualCastCost), F(magickaRegen), F(healthRegen), F(staminaRegen),
			B(weightlessBooks), B(weightlessPotions), B(weightlessFood), B(weightlessIngredients), B(weightlessScrolls), B(weightlessSoulGems),
			B(weightlessCrafting), B(weightlessGems), B(weightlessMiscItems), B(weightlessAmmo), B(weightlessWeapons), B(weightlessArmor),
		};
#undef B
#undef F
	}

	std::vector<Field> Fields() { return Fields(g_state); }

	void InstallEvents()
	{
		AlwaysPassHook<RE::FUNCTION_DATA::FunctionID::kGetIntimidateSuccess>::Install("GetIntimidateSuccess");
		AlwaysPassHook<RE::FUNCTION_DATA::FunctionID::kGetBribeSuccess>::Install("GetBribeSuccess");
		static LevelSink sink;
		if (auto source = RE::LevelIncrease::GetEventSource()) {
			source->AddEventSink(&sink);
		}
		static RaceMenuSink raceMenu;
		RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&raceMenu);
		static ContainerSink containers;
		RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESContainerChangedEvent>(&containers);
	}

	void InstallSerialization()
	{
		auto serialization = SKSE::GetSerializationInterface();
		serialization->SetUniqueID(kCosaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	namespace
	{
		void Loot(RE::PlayerCharacter* a_player, float a_radius, bool a_auto);  // below, with LootNearby
	}

	void Update()
	{
		if (g_state.flight || g_flightGravity || g_softLanding) {
			SKSE::GetTaskInterface()->AddTask(FlightStep);  // every frame
		}
		static auto last = std::chrono::steady_clock::now();
		const auto  now = std::chrono::steady_clock::now();
		if (now - last < 100ms) {
			return;
		}
		last = now;

		Puzzles::Update();
		if (g_landing) {
			RunOnPlayer(Land);
		}
		if (g_station) {
			RunOnPlayer(StationStep);
		}
		if (g_merchantSummoned) {
			RunOnPlayer(MerchantStep);
		}
		if (g_state.timelessTravel) {
			KeepTimeOnFastTravel();
		}
		static auto lastOre = now;
		if (now - lastOre > 500ms) {
			lastOre = now;
			if (g_state.infiniteOre) {
				RunOnPlayer(KeepOreVeins);
			} else if (!g_bottomlessVeins.empty()) {
				SKSE::GetTaskInterface()->AddTask(RestoreOreVeins);
			}
		}
		static auto lastFollowers = now;
		if ((g_state.immortalFollowers || !g_madeEssential.empty()) && now - lastFollowers > 100ms) {
			lastFollowers = now;
			RunOnPlayer([on = g_state.immortalFollowers](RE::PlayerCharacter* a_player) { KeepFollowersAlive(a_player, on); });
		}
		static auto lastGuild = now;
		if (g_state.thievesGuildQuick && now - lastGuild > 2s) {
			lastGuild = now;
			SKSE::GetTaskInterface()->AddTask(KeepGuildCountersHigh);
		}
		static auto lastCrime = now;
		if ((g_state.noBounty || g_state.keepItemsInJail) && now - lastCrime > 1s) {
			lastCrime = now;
			RunOnPlayer([state = g_state](RE::PlayerCharacter* a_player) {
				if (state.noBounty) {
					ClearAllBounties();
				}
				if (state.keepItemsInJail) {
					TakeBackJailedItems(a_player);
				}
			});
		}
		if (g_state.freezeTime || g_frozenTimescale) {
			SKSE::GetTaskInterface()->AddTask([on = g_state.freezeTime]() { KeepTimeFrozen(on); });
		}
		// The fast-draw boost follows what is in hand.
		if (g_state.fastBowDraw || g_bowBoosted) {
			RunOnPlayer([on = g_state.fastBowDraw](RE::PlayerCharacter* a_player) {
				if ((on && BowInHand(a_player)) != g_bowBoosted) {
					ApplyPersistent();
				}
			});
		}
		if (g_state.aimSlowmo || g_aimSlowed) {
			RunOnPlayer([on = g_state.aimSlowmo](RE::PlayerCharacter* a_player) { AimSlowmo(a_player, on); });
		}
		static auto lastPerks = now;
		if (g_state.autoPerks && now - lastPerks > 1s) {
			lastPerks = now;
			RunOnPlayer(AddEarnedPerks);
		}
		static auto lastLoot = now;
		if (g_state.autoLoot && now - lastLoot > 1s) {
			lastLoot = now;
			auto ui = RE::UI::GetSingleton();
			if (ui && !ui->GameIsPaused() && !Input::IsMenuOpen() && !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				RunOnPlayer([radius = g_state.lootRadius * 70.0f](RE::PlayerCharacter* a_player) { Loot(a_player, radius, true); });
			}
		}

		const auto& s = g_state;
		if (!s.infiniteHealth && !s.infiniteStamina && !s.infiniteMagicka && !s.infiniteAmmo && !s.infiniteCharge && !s.infinitePoison &&
			!s.infiniteLockpicks && !s.immunity && !s.nightEye && !s.eternalBuffs && !s.autoUnlock && !s.richMerchants) {
			return;
		}
		RunOnPlayer([state = g_state](RE::PlayerCharacter* a_player) {
			if (state.infiniteHealth) {
				RestoreFull(a_player, RE::ActorValue::kHealth);
			}
			if (state.infiniteStamina) {
				RestoreFull(a_player, RE::ActorValue::kStamina);
			}
			if (state.infiniteMagicka) {
				RestoreFull(a_player, RE::ActorValue::kMagicka);
			}
			if (state.infiniteCharge) {
				RestoreFull(a_player, RE::ActorValue::kRightItemCharge);
				RestoreFull(a_player, RE::ActorValue::kLeftItemCharge);
			}
			if (state.infiniteAmmo) {
				KeepAmmo(a_player);
			}
			if (state.infinitePoison) {
				KeepPoison(a_player);
			}
			if (state.infiniteLockpicks && RE::UI::GetSingleton()->IsMenuOpen(RE::LockpickingMenu::MENU_NAME)) {
				KeepAtLeast(a_player, RE::TESForm::LookupByID<RE::TESBoundObject>(kLockpick), kInfiniteLockpicksMin);
			}
			if (state.immunity || state.nightEye || state.eternalBuffs) {
				TendEffects(a_player, state, false);
			}
			auto ui = RE::UI::GetSingleton();
			if (!ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME)) {
				g_lastLockpickTarget = 0;
			} else if (state.autoUnlock) {
				AutoUnlock();
			}
			if (!ui->IsMenuOpen(RE::BarterMenu::MENU_NAME)) {
				g_lastBarterTarget = 0;
			} else if (state.richMerchants) {
				FillMerchant();
			}
		});
	}

	void ApplyPersistent()
	{
		RunOnPlayer([state = g_state](RE::PlayerCharacter* a_player) {
			auto avo = a_player->AsActorValueOwner();
			SkillCap::Apply();
			Killmoves::Apply();

			// Speed is a percentage of the original value, so bonuses from other mods survive.
			const float speedBase = g_originals.speedMult.value_or(avo->GetBaseActorValue(RE::ActorValue::kSpeedMult));
			// With the world slowed down, speed the player up by the same factor.
			const float compensation = state.playerNormalSpeed && state.worldSpeed < 0.999f ? 1.0f / std::max(state.worldSpeed, 0.1f) : 1.0f;
			Override(avo, RE::ActorValue::kSpeedMult, g_originals.speedMult,
				Changed(state.speedMult * compensation, 100.0f) ? std::optional{ speedBase * state.speedMult / 100.0f * compensation } : std::nullopt);
			Override(avo, RE::ActorValue::kShoutRecoveryMult, g_originals.shoutRecovery,
				state.noShoutCooldown ? std::optional{ 0.0f } : std::nullopt);
			Override(avo, RE::ActorValue::kCarryWeight, g_originals.carryWeight,
				state.noCarryLimit ? std::optional{ kHugeCarryWeight } : std::nullopt);
			NudgeMovement(a_player);

			OverrideSetting("fJumpHeightMin", Changed(state.jumpMult, 1.0f), [&](float o) { return o * state.jumpMult; });
			OverrideSetting("fJumpFallHeightMin", state.noFallDamage, [](float) { return kNoFallDamageHeight; });
			OverrideSetting("fPickPocketMinChance", state.easyPickpocket, [](float) { return 100.0f; });
			OverrideSetting("fPickPocketMaxChance", state.easyPickpocket, [](float) { return 100.0f; });
			OverrideSetting("fMagicDualCastingEffectivenessBase", Changed(state.dualCastPower, 1.0f), [&](float o) { return o * state.dualCastPower; });
			OverrideSetting("fMagicDualCastingCostMult", Changed(state.dualCastCost, 1.0f), [&](float o) { return o * state.dualCastCost; });

			// Damage multipliers exist per difficulty: VE, E, N, H, VH, L(egendary).
			for (const char* diff : { "VE", "E", "N", "H", "VH", "L" }) {
				// One-hit kill: the player's damage ×10 000 on top of the slider.
				OverrideSetting(std::format("fDiffMultHPByPC{}", diff).c_str(), Changed(state.damageDealtMult, 1.0f) || state.oneHitKill,
					[&](float o) { return o * state.damageDealtMult * (state.oneHitKill ? 10000.0f : 1.0f); });
				OverrideSetting(std::format("fDiffMultHPToPC{}", diff).c_str(), Changed(state.damageTakenMult, 1.0f),
					[&](float o) { return o * state.damageTakenMult; });
			}

			ApplySkillXp(state.skillXpMult);
			ApplyWeightless(state);
			ApplyInstantCast(state.instantCast);
			ApplyInfiniteTorch(state.infiniteTorch);
			OverrideIntSetting("iTrainingNumAllowedPerLevel", state.unlimitedTraining, kTrainingUnlimited);
			ApplyPowersNoCooldown(state.powersNoCooldown);
			ApplyFreeCraft(state.freeCraft, state.craftNoPerks);
			ApplyEnchantAnything(state.enchantAnything);
			ApplyAnySoul(state.anySoulInAnyGem);
			ApplyMerchantsBuyAll(state.merchantsBuyAll);
			// Fast draw itself (full draw power at once) is the player-only hook in Archery.cpp.
			ApplyNoDragonAttacks(state.noDragonAttacks);
			ApplyPersuadeAlways(state.persuadeAlways);
			OverrideBarter(state.fairTrade);
			OverrideAV(avo, RE::ActorValue::kInvisibility, state.invisibility ? std::optional{ 1.0f } : std::nullopt);

			// Fortify <school> = cost reduction in percent; 100 makes spells free.
			for (auto av : { RE::ActorValue::kAlterationModifier, RE::ActorValue::kConjurationModifier, RE::ActorValue::kDestructionModifier,
					 RE::ActorValue::kIllusionModifier, RE::ActorValue::kRestorationModifier }) {
				OverrideAV(avo, av, state.noSpellCost ? std::optional{ 100.0f } : std::nullopt);
			}
			// Fast bow draw also plays the draw animation 3x faster, only while a bow or crossbow is in hand.
			const float bowBoost = state.fastBowDraw && BowInHand(a_player) ? kFastDrawSpeed : 1.0f;
			g_bowBoosted = bowBoost > 1.0f;
			// The auto-aim of "Click-Shot 3", only while the player holds a bow. Its other settings are left out:
			// fArrowMinVelocity does nothing at full power, and fArrowMinBowVelocity / fArrowBowSlowMult go into a
			// bow-speed factor the game computes once per session (RVA 418820), so changing them at runtime gives
			// arrows a speed that depends on which values were set at the first shot.
			for (auto [name, value] : { std::pair{ "fAutoAimMaxDegrees", 10.0f }, { "fAutoAimMaxDegrees3rdPerson", 20.0f },
					 { "fAutoAimMaxDistance", 18000.0f }, { "fAutoAimScreenPercentage", 0.0f } }) {
				OverrideSetting(name, g_bowBoosted, [v = value](float) { return v; });
			}
			const float weaponSpeed = state.attackSpeed * compensation * bowBoost;
			const auto  attackSpeed = Changed(weaponSpeed, 1.0f) ? std::optional{ weaponSpeed } : std::nullopt;
			OverrideAV(avo, RE::ActorValue::kWeaponSpeedMult, attackSpeed);
			OverrideAV(avo, RE::ActorValue::kLeftWeaponSpeedMultiply, attackSpeed);
			OverrideAV(avo, RE::ActorValue::kResistDisease, state.immunity ? std::optional{ 100.0f } : std::nullopt);
			OverrideAV(avo, RE::ActorValue::kWaterWalking, state.waterWalking ? std::optional{ 1.0f } : std::nullopt);
			OverrideAV(avo, RE::ActorValue::kWaterBreathing, state.waterBreathing ? std::optional{ 1.0f } : std::nullopt);
			for (const auto [av, mult] : { std::pair{ RE::ActorValue::kMagickaRateMult, state.magickaRegen }, { RE::ActorValue::kHealRateMult, state.healthRegen },
					 { RE::ActorValue::kStaminaRateMult, state.staminaRegen } }) {
				const float base = OriginalAV(avo, av);
				OverrideAV(avo, av, Changed(mult, 1.0f) ? std::optional{ base * mult } : std::nullopt);
			}
			Boost(avo, RE::ActorValue::kSmithingModifier, state.smithingBoost);
			Boost(avo, RE::ActorValue::kAlchemyModifier, state.alchemyBoost);
			Boost(avo, RE::ActorValue::kEnchantingModifier, state.enchantingBoost);

			if (state.nightEye) {
				g_nightEyeTouched = true;
			} else if (g_nightEyeTouched) {
				TendEffects(a_player, state, true);
				g_nightEyeTouched = false;
			}
		});

		if (Changed(g_state.worldSpeed, 1.0f) || g_worldSpeedTouched) {
			Console::Run(std::format("sgtm {:.2f}", g_state.worldSpeed));
			g_worldSpeedTouched = Changed(g_state.worldSpeed, 1.0f);
		}

		// The essential flag is ignored for the player; the game's own immortal mode
		// (tim: damage goes through, death does not) is a toggle, so track what we set.
		if (g_state.essential != g_immortalMode) {
			Console::Run("tim");
			g_immortalMode = g_state.essential;
		}
	}

	namespace
	{
		// PlayerCharacter::IsGodMode() reads the wrong address on 1.6.1170 (always false), so the
		// menu keeps its own flag and drives the console's tgm, checking what it reports.
		std::atomic_bool g_godMode{ false };
	}

	bool IsGodMode() { return g_godMode; }

	void SetGodMode(bool a_enable)
	{
		g_godMode = a_enable;
		Console::RunCaptured("tgm", {}, [a_enable](std::string a_output) {
			const bool nowOn = a_output.find("enabled") != std::string::npos;
			const bool nowOff = a_output.find("disabled") != std::string::npos;
			if ((nowOn && !a_enable) || (nowOff && a_enable)) {
				Console::Run("tgm");  // it was already the other way round: toggle back
			}
		});
	}

	void AddGold(int a_count) { Grant(0x0000000F, a_count); }

	void AddLevels(int a_count)
	{
		RunOnPlayer([a_count](RE::PlayerCharacter* a_player) {
			Console::Run(std::format("player.setlevel {}", a_player->GetLevel() + a_count));
		});
	}

	void AddPerkPoints(int a_count)
	{
		RunOnPlayer([a_count](RE::PlayerCharacter* a_player) {
			auto& points = a_player->GetPlayerRuntimeData().perkCount;
			points = static_cast<std::uint8_t>(std::min(255, points + a_count));
		});
	}

	void AddDragonSouls(int a_count) { Console::Run(std::format("player.modav dragonsouls {}", a_count)); }

	void MaxAllSkills()
	{
		static constexpr std::array kSkills{
			"OneHanded", "TwoHanded", "Marksman", "Block", "Smithing", "HeavyArmor",
			"LightArmor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speechcraft",
			"Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"
		};
		for (auto skill : kSkills) {
			Console::Run(std::format("player.setav {} 100", skill));
		}
	}

	void HealAll()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			RestoreFull(a_player, RE::ActorValue::kHealth);
			RestoreFull(a_player, RE::ActorValue::kStamina);
			RestoreFull(a_player, RE::ActorValue::kMagicka);
		});
	}

	void Grant(RE::FormID a_formID, int a_count)
	{
		RunOnPlayer([a_formID, a_count](RE::PlayerCharacter* a_player) {
			auto form = RE::TESForm::LookupByID(a_formID);
			if (!form) {
				return;
			}
			if (auto spell = form->As<RE::SpellItem>()) {
				a_player->AddSpell(spell);
			} else if (auto shout = form->As<RE::TESShout>()) {
				a_player->AddShout(shout);
				for (auto& variation : shout->variations) {
					if (variation.word) {
						Console::Run(std::format("player.teachword {:08X}", variation.word->GetFormID()));
						Console::Run(std::format("unlockword {:08X}", variation.word->GetFormID()));
					}
				}
			} else if (auto perk = form->As<RE::BGSPerk>()) {
				a_player->AddPerk(perk);
			} else if (form->Is(RE::FormType::NPC)) {
				Console::Run(std::format("player.placeatme {:08X} {}", a_formID, std::clamp(a_count, 1, 20)));
			} else if (auto object = form->As<RE::TESBoundObject>()) {
				a_player->AddObjectToContainer(object, nullptr, a_count, nullptr);
			}
		});
	}

	void GiveKit(Kit a_kit, int a_count)
	{
		RunOnPlayer([a_kit, a_count](RE::PlayerCharacter* a_player) {
			auto dh = RE::TESDataHandler::GetSingleton();
			auto give = [&](RE::TESBoundObject* a_obj) {
				if (a_obj && a_obj->GetName() && *a_obj->GetName()) {
					a_player->AddObjectToContainer(a_obj, nullptr, a_count, nullptr);
				}
			};
			int before = 0;
			switch (a_kit) {
			case Kit::kSmithing:
				for (auto misc : dh->GetFormArray<RE::TESObjectMISC>()) {
					if (misc && (misc->HasKeywordString("VendorItemOreIngot") || misc->HasKeywordString("VendorItemAnimalHide") ||
									misc->HasKeywordString("VendorItemAnimalPart"))) {
						give(misc);
						++before;
					}
				}
				break;
			case Kit::kIngredients:
				for (auto ingredient : dh->GetFormArray<RE::IngredientItem>()) {
					give(ingredient);
					++before;
				}
				break;
			case Kit::kSoulGems:
				for (auto gem : dh->GetFormArray<RE::TESSoulGem>()) {
					if (gem && gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone) {
						give(gem);
						++before;
					}
				}
				break;
			case Kit::kLockpicks:
				give(RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000A));
				++before;
				break;
			}
			logger::info("Kit {}: {} kinds x{}", static_cast<int>(a_kit), before, a_count);
		});
	}

	void TeleportTo(RE::ObjectRefHandle a_marker)
	{
		RunOnPlayer([a_marker](RE::PlayerCharacter* a_player) {
			if (auto ref = a_marker.get()) {
				a_player->MoveTo(ref.get());
			}
		});
	}

	std::string BecomeThaneEverywhere()
	{
		// Each hold's thane quest (Favor250-258) is started by the story manager when the player meets the
		// jarl, and stage 25 makes the player thane: housecarl, jarl's weapon, "get out of jail" card. Here the
		// quest is started directly — its event-filled aliases made optional for the start — the jarl (the
		// actor in the quest's FavorNNNQuestGiverFaction) put into the Questgiver alias, the weapon (a leveled
		// list the alias would have created) given straight away, and stage 25 set. The Rift has its own quest
		// whose stage 200 does the same. Same stages in USSEP.
		struct Hold
		{
			const char* quest;
			RE::FormID  weapon;  // leveled list of the jarl's gift
		};
		constexpr Hold kHolds[] = {
			{ "Favor250", 0x0D069E }, { "Favor252", 0x0494B0 }, { "Favor253", 0x0494B0 }, { "Favor254", 0x0D069E }, { "Favor255", 0x0494B0 },
			{ "Favor256", 0x0494B0 }, { "Favor257", 0x0494B0 }, { "Favor258", 0x0494B0 },
		};
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return Tr("status.load_a_game_first");
		}

		// Jarls: every actor reference in one of the quests' quest-giver factions.
		std::unordered_map<RE::TESFaction*, RE::Actor*> jarls;
		std::vector<std::pair<RE::TESQuest*, RE::TESFaction*>> quests;
		for (const auto& hold : kHolds) {
			auto quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(hold.quest);
			if (!quest || quest->GetCurrentStageID() >= 25) {
				continue;  // missing, or the player is thane there already
			}
			auto script = ScriptOn(quest, std::format("QF_{}_{:08X}", hold.quest, quest->GetFormID()).c_str());
			auto var = script ? script->GetVariable(std::format("::{}QuestGiverFaction_var", hold.quest)) : nullptr;
			auto faction = var && var->IsObject() ? var->Unpack<RE::TESFaction*>() : nullptr;
			quests.emplace_back(quest, faction);
			if (faction) {
				jarls.emplace(faction, nullptr);
			}
		}
		{
			const auto [forms, lock] = RE::TESForm::GetAllForms();
			RE::BSReadLockGuard guard(lock);
			for (const auto& [id, form] : *forms) {
				auto actor = form ? form->As<RE::Actor>() : nullptr;
				if (!actor || actor->IsDead() || actor->IsDisabled()) {
					continue;
				}
				for (auto& [faction, jarl] : jarls) {
					if (!jarl && actor->IsInFaction(faction)) {
						jarl = actor;
					}
				}
			}
		}

		int made = 0;
		for (std::size_t i = 0; i < quests.size(); ++i) {
			auto [quest, faction] = quests[i];
			const auto& hold = *std::ranges::find_if(kHolds, [&](const Hold& a_hold) { return quest->GetFormEditorID() == std::string_view{ a_hold.quest }; });
			// IsRunning() says true for these before they ever ran; starting a running quest changes nothing.
			if (!StartQuestRelaxed(quest)) {
				logger::warn("Thane: {} did not start", hold.quest);
				continue;
			}
			for (auto alias : quest->aliases) {
				if (alias && _stricmp(alias->aliasName.c_str(), "Questgiver") == 0 && !quest->GetAliasedRef(alias->aliasID)) {
					if (auto jarl = faction ? jarls[faction] : nullptr) {
						quest->ForceRefIntoAlias(alias->aliasID, jarl);
					}
				}
			}
			if (auto list = RE::TESForm::LookupByID<RE::TESLevItem>(hold.weapon)) {
				RE::BSScrapArray<RE::CALCED_OBJECT> weapons;
				list->CalculateCurrentFormList(static_cast<std::uint16_t>(player->GetLevel()), 1, weapons, 0, true);
				for (const auto& weapon : weapons) {
					if (auto object = weapon.form ? weapon.form->As<RE::TESBoundObject>() : nullptr) {
						player->AddObjectToContainer(object, nullptr, weapon.count, nullptr);
					}
				}
			}
			Console::Run(std::format("setstage {} 25", hold.quest));
			++made;
		}
		if (auto rift = RE::TESForm::LookupByEditorID<RE::TESQuest>("FreeformRiftenThane"); rift && rift->GetCurrentStageID() < 200) {
			Console::Run("setstage FreeformRiftenThane 200");
			++made;
		}
		logger::info("Thane: {} holds", made);
		return made ? Lang::Fmt("status.thane_of_holds_housecarls_are", made) : Tr("status.already_thane_in_every_hold");
	}

	RE::Actor* Spouse()
	{
		auto fin = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriageFIN");
		auto alias = fin ? AliasNamed(fin, "LoveInterest") : nullptr;
		auto ref = alias ? fin->GetAliasedRef(alias->aliasID).get() : nullptr;
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	std::string MarryTarget(RE::ObjectRefHandle a_target)
	{
		// The wedding quest's stage 100 is the end of the ceremony: rings, both in the "married" faction, the
		// spouse in the player's faction, and the story event that starts married life (RelationshipMarriageFIN).
		auto actor = a_target.get() ? a_target.get()->As<RE::Actor>() : nullptr;
		if (!actor || actor->IsDead() || actor->IsPlayerRef()) {
			return Tr("status.aim_at_a_living_npc");
		}
		if (Spouse()) {
			return Tr("status.already_married_divorce_first");
		}
		auto wedding = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriageWedding");
		auto courting = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriage");
		if (!wedding || !courting) {
			return Tr("status.marriage_quests_not_found");
		}
		Console::Run("resetquest RelationshipMarriageWedding");
		if (!StartQuestRelaxed(wedding)) {
			return Tr("status.the_wedding_did_not_start");
		}
		for (auto quest : { wedding, courting }) {
			if (auto alias = AliasNamed(quest, "LoveInterest")) {
				quest->ForceRefIntoAlias(alias->aliasID, actor);
			}
		}
		Console::Run("setstage RelationshipMarriageWedding 100");
		// The story event only starts married life for NPCs with a home of their own; others are put in directly.
		if (!Spouse()) {
			if (auto fin = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriageFIN"); fin && StartQuestRelaxed(fin)) {
				if (auto alias = AliasNamed(fin, "LoveInterest")) {
					fin->ForceRefIntoAlias(alias->aliasID, actor);
				}
			}
		}
		logger::info("Married {:08X} \"{}\"", actor->GetFormID(), actor->GetName());
		return Lang::Fmt("status.married", actor->GetName());
	}

	std::string Divorce()
	{
		auto spouse = Spouse();
		auto fin = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriageFIN");
		if (!spouse || !fin) {
			return Tr("status.not_married");
		}
		auto married = QuestProperty<RE::TESFaction>(fin, "PlayerMarriedFaction");
		auto wedding = RE::TESForm::LookupByEditorID<RE::TESQuest>("RelationshipMarriageWedding");
		auto playerFaction = wedding ? QuestProperty<RE::TESFaction>(wedding, "PlayerFaction") : nullptr;
		const std::string name = spouse->GetName();
		// Stage 200 of married life is "the spouse is gone": the player leaves the married faction, the quest stops.
		Console::Run("setstage RelationshipMarriageFIN 200");
		for (auto faction : { married, playerFaction }) {
			if (faction) {
				Console::Run(std::format("removefac {:08X}", faction->GetFormID()), spouse->GetHandle());
			}
		}
		Console::Run("setrelationshiprank player 1", spouse->GetHandle());
		Console::Run("stopquest RelationshipMarriageFIN");
		// Courting and wedding from the start again, so the Amulet of Mara works once more.
		Console::Run("resetquest RelationshipMarriageWedding");
		Console::Run("resetquest RelationshipMarriage");
		Console::Run("startquest RelationshipMarriage");
		spouse->EvaluatePackage();
		logger::info("Divorced {:08X} \"{}\"", spouse->GetFormID(), name);
		return Lang::Fmt("status.divorced_from_the_amulet_of", name);
	}

	// ------------------------------------------------------------ character profiles (like Proteus)
	// A profile is a text file in SKSE\CheatMenuProfiles: one "key=value" per line; forms as
	// "<plugin>|<local form ID>", so a profile still fits after the load order changed. The appearance is
	// the player NPC's own data (head parts, hair colour, face texture, weight, skin, face sliders) plus the
	// player's tint layers; the game's spf / lpf commands write nothing on this version.
	namespace
	{
		std::optional<std::filesystem::path> ProfileDir()
		{
			auto dir = SKSE::log::log_directory();
			if (!dir) {
				return std::nullopt;
			}
			auto path = *dir / "DragonbornsCodexProfiles";
			std::error_code ec;
			if (const auto old = *dir / "CheatMenuProfiles"; !std::filesystem::exists(path, ec) && std::filesystem::exists(old, ec)) {
				std::filesystem::rename(old, path, ec);  // the menu's name before the rename
			}
			std::filesystem::create_directories(path, ec);
			return path;
		}

		std::filesystem::path Utf8Path(const std::string& a_text) { return std::filesystem::path(reinterpret_cast<const char8_t*>(a_text.c_str())); }

		std::string FormRef(const RE::TESForm* a_form)
		{
			auto file = a_form ? a_form->GetFile(0) : nullptr;
			return file ? std::format("{}|{:X}", file->GetFilename(), a_form->GetLocalFormID()) : std::string{};
		}

		RE::TESForm* ResolveRef(std::string_view a_ref)
		{
			const auto bar = a_ref.find('|');
			if (bar == std::string_view::npos) {
				return nullptr;
			}
			const auto local = static_cast<RE::FormID>(std::stoul(std::string{ a_ref.substr(bar + 1) }, nullptr, 16));
			const auto id = RE::TESDataHandler::GetSingleton()->LookupFormID(local, a_ref.substr(0, bar));
			return id ? RE::TESForm::LookupByID(id) : nullptr;
		}

		std::vector<float> Numbers(const std::string& a_text)
		{
			std::vector<float> numbers;
			std::stringstream  in{ a_text };
			for (std::string item; std::getline(in, item, ',');) {
				if (!item.empty()) {
					numbers.push_back(std::stof(item));
				}
			}
			return numbers;
		}

		void ApplyLook(RE::PlayerCharacter* a_player, const std::vector<std::pair<std::string, std::string>>& a_look)
		{
			auto npc = a_player->GetActorBase();
			if (!npc || a_look.empty()) {
				return;
			}
			auto& tints = a_player->GetPlayerRuntimeData().tintMasks;
			for (const auto& [key, value] : a_look) {
				try {
					if (key == "weight") {
						npc->weight = std::stof(value);
					} else if (key == "skin") {
						const auto rgb = Numbers(value);
						if (rgb.size() == 3) {
							npc->bodyTintColor = RE::Color(static_cast<std::uint8_t>(rgb[0]), static_cast<std::uint8_t>(rgb[1]), static_cast<std::uint8_t>(rgb[2]), 0);
						}
					} else if (key == "headpart") {
						if (auto part = ResolveRef(value) ? ResolveRef(value)->As<RE::BGSHeadPart>() : nullptr) {
							npc->ChangeHeadPart(part);
						}
					} else if (key == "haircolor") {
						if (auto color = ResolveRef(value) ? ResolveRef(value)->As<RE::BGSColorForm>() : nullptr) {
							npc->SetHairColor(color);
						}
					} else if (key == "facetexture") {
						if (auto texture = ResolveRef(value) ? ResolveRef(value)->As<RE::BGSTextureSet>() : nullptr) {
							npc->SetFaceTexture(texture);
						}
					} else if (key == "morphs" && npc->faceData) {
						const auto morphs = Numbers(value);
						for (std::size_t i = 0; i < morphs.size() && i < std::size(npc->faceData->morphs); ++i) {
							npc->faceData->morphs[i] = morphs[i];
						}
					} else if (key == "parts" && npc->faceData) {
						const auto parts = Numbers(value);
						for (std::size_t i = 0; i < parts.size() && i < std::size(npc->faceData->parts); ++i) {
							npc->faceData->parts[i] = static_cast<std::int32_t>(parts[i]);
						}
					} else if (key == "tint") {
						// type|r|g|b|alpha|texture: the player's layer with the same texture takes the colour.
						std::vector<std::string> f;
						std::stringstream        in{ value };
						for (std::string item; std::getline(in, item, '|');) {
							f.push_back(item);
						}
						if (f.size() < 6) {
							continue;
						}
						for (auto tint : tints) {
							if (tint && tint->texture && _stricmp(tint->texture->textureName.c_str(), f[5].c_str()) == 0) {
								tint->color = RE::Color(static_cast<std::uint8_t>(std::stoi(f[1])), static_cast<std::uint8_t>(std::stoi(f[2])),
									static_cast<std::uint8_t>(std::stoi(f[3])), tint->color.alpha);
								tint->alpha = std::stof(f[4]);
							}
						}
					}
				} catch (const std::exception&) {
				}
			}
			a_player->UpdateSkinColor();
			a_player->UpdateHairColor();
			a_player->DoReset3D(true);  // rebuilds the head from the data above
		}
	}

	std::vector<std::string> ProfileNames()
	{
		std::vector<std::string> names;
		auto                     dir = ProfileDir();
		std::error_code          ec;
		if (!dir) {
			return names;
		}
		for (const auto& entry : std::filesystem::directory_iterator(*dir, ec)) {
			if (entry.path().extension() == ".txt") {
				const auto stem = entry.path().stem().u8string();
				names.emplace_back(reinterpret_cast<const char*>(stem.c_str()));
			}
		}
		std::ranges::sort(names);
		return names;
	}

	std::string SaveProfile(const std::string& a_name)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto dir = ProfileDir();
		if (!player || !player->Is3DLoaded() || !dir || a_name.empty()) {
			return Tr("status.failed_a_loaded_game_and");
		}
		std::ostringstream out;
		out << "name=" << player->GetName() << "\n";
		out << "race=" << FormRef(player->GetRace()) << "\n";
		out << "female=" << (player->GetActorBase() && player->GetActorBase()->IsFemale() ? 1 : 0) << "\n";
		out << "level=" << player->GetLevel() << "\n";
		auto avo = player->AsActorValueOwner();
		for (auto av = RE::ActorValue::kOneHanded; av <= RE::ActorValue::kEnchanting; av = static_cast<RE::ActorValue>(std::to_underlying(av) + 1)) {
			out << "av." << std::to_underlying(av) << "=" << avo->GetBaseActorValue(av) << "\n";
		}
		for (auto av : { RE::ActorValue::kHealth, RE::ActorValue::kMagicka, RE::ActorValue::kStamina }) {
			out << "av." << std::to_underlying(av) << "=" << avo->GetBaseActorValue(av) << "\n";
		}
		int perks = 0, spells = 0, shouts = 0, items = 0;
		ForEachPerkNode([&](RE::BGSSkillPerkTreeNode* a_node) {
			for (auto rank : PerkRanks(a_node->perk)) {
				if (player->HasPerk(rank)) {
					out << "perk=" << FormRef(rank) << "\n";
					++perks;
				}
			}
		});
		for (auto spell : player->GetActorRuntimeData().addedSpells) {
			const auto type = spell ? spell->GetSpellType() : RE::MagicSystem::SpellType::kAbility;
			if (type == RE::MagicSystem::SpellType::kSpell || type == RE::MagicSystem::SpellType::kPower || type == RE::MagicSystem::SpellType::kLesserPower) {
				out << "spell=" << FormRef(spell) << "\n";
				++spells;
			}
		}
		for (auto shout : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESShout>()) {
			if (shout && player->HasShout(shout)) {
				out << "shout=" << FormRef(shout) << "\n";
				++shouts;
			}
		}
		for (const auto& [object, data] : player->GetInventory()) {
			if (object && data.first > 0 && object->GetPlayable() && !(data.second && data.second->IsQuestObject()) && !object->Is(RE::FormType::LeveledItem)) {
				out << "item=" << FormRef(object) << "|" << data.first << "\n";
				++items;
			}
		}
		if (auto npc = player->GetActorBase()) {
			out << "weight=" << npc->weight << "\n";
			out << "skin=" << +npc->bodyTintColor.red << "," << +npc->bodyTintColor.green << "," << +npc->bodyTintColor.blue << "\n";
			for (std::int8_t i = 0; npc->headParts && i < npc->numHeadParts; ++i) {
				if (npc->headParts[i]) {
					out << "headpart=" << FormRef(npc->headParts[i]) << "\n";
				}
			}
			if (npc->headRelatedData && npc->headRelatedData->hairColor) {
				out << "haircolor=" << FormRef(npc->headRelatedData->hairColor) << "\n";
			}
			if (npc->headRelatedData && npc->headRelatedData->faceDetails) {
				out << "facetexture=" << FormRef(npc->headRelatedData->faceDetails) << "\n";
			}
			if (npc->faceData) {
				out << "morphs=";
				for (auto morph : npc->faceData->morphs) {
					out << morph << ",";
				}
				out << "\nparts=";
				for (auto part : npc->faceData->parts) {
					out << part << ",";
				}
				out << "\n";
			}
		}
		for (auto tint : player->GetPlayerRuntimeData().tintMasks) {
			if (tint && tint->texture) {
				out << "tint=" << tint->type.underlying() << "|" << +tint->color.red << "|" << +tint->color.green << "|" << +tint->color.blue << "|"
					<< tint->alpha << "|" << tint->texture->textureName.c_str() << "\n";
			}
		}
		std::ofstream file{ *dir / Utf8Path(a_name + ".txt"), std::ios::trunc };
		file << out.str();
		if (!file) {
			return Tr("status.could_not_write_the_profile");
		}
		logger::info("Profile \"{}\" saved: {} perks, {} spells, {} shouts, {} item stacks", a_name, perks, spells, shouts, items);
		return Lang::Fmt("status.profile_saved_perks_spells_shouts", a_name, perks, spells, shouts, items);
	}

	std::string LoadProfile(const std::string& a_name, bool a_withItems)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto dir = ProfileDir();
		if (!player || !player->Is3DLoaded() || !dir) {
			return Tr("status.load_a_game_first");
		}
		std::ifstream file{ *dir / Utf8Path(a_name + ".txt") };
		if (!file) {
			return Tr("status.profile_not_found");
		}
		std::string                                      line;
		int                                              level = 0, missing = 0, perks = 0, spells = 0, shouts = 0;
		std::optional<bool>                              female;
		RE::TESRace*                                     race = nullptr;
		std::vector<std::pair<RE::ActorValue, float>>    values;
		std::vector<RE::TESForm*>                        grants;
		std::vector<std::pair<RE::TESBoundObject*, int>> inventory;
		std::vector<std::pair<std::string, std::string>> look;
		while (std::getline(file, line)) {
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = line.substr(0, eq);
			const auto value = line.substr(eq + 1);
			try {
				if (key == "race") {
					auto form = ResolveRef(value);
					race = form ? form->As<RE::TESRace>() : nullptr;
				} else if (key == "female") {
					female = value == "1";
				} else if (key == "level") {
					level = std::stoi(value);
				} else if (key.starts_with("av.")) {
					values.emplace_back(static_cast<RE::ActorValue>(std::stoi(key.substr(3))), std::stof(value));
				} else if (key == "perk" || key == "spell" || key == "shout") {
					if (auto form = ResolveRef(value)) {
						grants.push_back(form);
						++(key == "perk" ? perks : key == "spell" ? spells : shouts);
					} else {
						++missing;
					}
				} else if (key == "item") {
					const auto last = value.rfind('|');
					auto       form = ResolveRef(value.substr(0, last));
					if (auto object = form ? form->As<RE::TESBoundObject>() : nullptr) {
						inventory.emplace_back(object, std::stoi(value.substr(last + 1)));
					} else {
						++missing;
					}
				} else {
					look.emplace_back(key, value);  // appearance, applied after race and sex
				}
			} catch (const std::exception&) {
				++missing;
			}
		}

		// Race and sex first (they reset the face), then the face, stats, everything learned, the items.
		if (race && race != player->GetRace() && race->GetFormEditorID() && *race->GetFormEditorID()) {
			Console::Run(std::format("player.setrace {}", race->GetFormEditorID()));
		}
		if (female && player->GetActorBase() && player->GetActorBase()->IsFemale() != *female) {
			Console::Run(Tr("player.sexchange"));
		}
		if (level > 0) {
			SetLevel(level);
		}
		RunOnPlayer([values, grants, inventory, look, a_withItems](RE::PlayerCharacter* a_player) {
			ApplyLook(a_player, look);
			for (const auto& [av, value] : values) {
				a_player->AsActorValueOwner()->SetBaseActorValue(av, value);
			}
			for (auto form : grants) {
				if (auto perk = form->As<RE::BGSPerk>()) {
					if (!a_player->HasPerk(perk)) {
						a_player->AddPerk(perk);
					}
				} else if (auto spell = form->As<RE::SpellItem>()) {
					a_player->AddSpell(spell);
				} else if (form->Is(RE::FormType::Shout)) {
					Grant(form->GetFormID(), 1);
				}
			}
			if (a_withItems) {
				for (const auto& [object, count] : inventory) {
					a_player->AddObjectToContainer(object, nullptr, count, nullptr);
				}
			}
		});
		logger::info("Profile \"{}\" loaded: {} perks, {} spells, {} shouts, {} item stacks{}, {} missing", a_name, perks, spells, shouts, inventory.size(),
			a_withItems ? "" : " (not given)", missing);
		return Lang::Fmt("status.profile_applied", a_name, missing ? Lang::Fmt("status.records_not_found_missing_mod", missing) : std::string{});
	}

	bool DeleteProfile(const std::string& a_name)
	{
		auto            dir = ProfileDir();
		std::error_code ec;
		return dir && std::filesystem::remove(*dir / Utf8Path(a_name + ".txt"), ec);
	}

	std::pair<int, int> ThaneProgress()
	{
		int done = 0, total = 0;
		for (auto quest : { "Favor250", "Favor252", "Favor253", "Favor254", "Favor255", "Favor256", "Favor257", "Favor258" }) {
			if (auto q = RE::TESForm::LookupByEditorID<RE::TESQuest>(quest)) {
				++total;
				done += q->GetCurrentStageID() >= 25;
			}
		}
		if (auto rift = RE::TESForm::LookupByEditorID<RE::TESQuest>("FreeformRiftenThane")) {
			++total;
			done += rift->GetCurrentStageID() >= 200;
		}
		return { done, total };
	}

	bool InThievesGuild() { return JoinedThievesGuild(); }

	int StolenCount()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto self = player ? player->GetActorBase() : nullptr;
		int  stolen = 0;
		if (!self) {
			return 0;
		}
		for (const auto& [object, data] : player->GetInventory()) {
			if (data.second && data.second->extraLists) {
				for (auto list : *data.second->extraLists) {
					auto ownership = list ? list->GetByType<RE::ExtraOwnership>() : nullptr;
					stolen += ownership && ownership->owner && ownership->owner != self ? list->GetCount() : 0;
				}
			}
		}
		return stolen;
	}

	int EmptySoulGemCount()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		int  empty = 0;
		if (!player) {
			return 0;
		}
		for (const auto& [object, data] : player->GetInventory()) {
			auto gem = object ? object->As<RE::TESSoulGem>() : nullptr;
			if (!gem || gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone || data.first <= 0) {
				continue;
			}
			int withSoul = 0;
			if (data.second && data.second->extraLists) {
				for (auto list : *data.second->extraLists) {
					withSoul += list && list->HasType(RE::ExtraDataType::kSoul) ? list->GetCount() : 0;
				}
			}
			empty += std::max(0, data.first - withSoul);
		}
		return empty;
	}

	std::vector<ProfileInfo> ProfileInfos()
	{
		std::vector<ProfileInfo> infos;
		auto                     dir = ProfileDir();
		if (!dir) {
			return infos;
		}
		for (const auto& name : ProfileNames()) {
			ProfileInfo info{ name };
			const auto  path = *dir / Utf8Path(name + ".txt");
			std::ifstream file{ path };
			for (std::string line; std::getline(file, line);) {
				if (line.starts_with("name=")) {
					info.character = line.substr(5);
				} else if (line.starts_with("level=")) {
					info.level = std::atoi(line.c_str() + 6);
				} else if (line.starts_with("race=")) {
					if (auto race = ResolveRef(line.substr(5))) {
						info.race = race->GetName() ? race->GetName() : "";
					}
				} else if (line.starts_with("perk=")) {
					++info.perks;
				}
			}
			std::error_code ec;
			const auto      written = std::filesystem::last_write_time(path, ec);
			if (!ec) {
				const auto time = std::chrono::clock_cast<std::chrono::system_clock>(written);
				const auto local = std::chrono::zoned_time{ std::chrono::current_zone(), std::chrono::floor<std::chrono::minutes>(time) };
				info.saved = std::format("{:%d.%m.%Y %H:%M}", local);
			}
			infos.push_back(std::move(info));
		}
		return infos;
	}

	bool CureVampirism()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		return player && CallQuestScript("PlayerVampireQuestScript", "VampireCure", RE::MakeFunctionArguments(static_cast<RE::Actor*>(player)));
	}

	bool BecomeVampire()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		return player && CallQuestScript("PlayerVampireQuestScript", "VampireChange", RE::MakeFunctionArguments(static_cast<RE::Actor*>(player)));
	}

	bool CureLycanthropy() { return CallQuestScript("CompanionsHousekeepingScript", "CurePlayer", RE::MakeFunctionArguments()); }

	std::string SummonMerchant()
	{
		auto spell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x01EEC6, "Dragonborn.esm");  // DLC2ConjureDremoraMerchant
		auto faction = DremoraMerchantFaction();
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!spell || !faction) {
			return Tr("status.the_dremora_merchant_requires_the");
		}
		if (!player || !player->Is3DLoaded()) {
			return Tr("status.load_a_game_first");
		}
		RunOnPlayer([spell, faction](RE::PlayerCharacter* a_player) {
			// buys every kind of item, clutter too (his own exclusion list skips it): runtime only, set again on every summon
			faction->vendorData.vendorSellBuyList = BuyAllList();
			faction->vendorData.vendorValues.notBuySell = true;
			RestockDremoraMerchantIfDue();
			FillDremoraMerchant();
			g_merchantSummoned = true;
			g_lastMerchantBarter = 0;
			if (auto caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
				caster->CastSpellImmediate(spell, false, a_player, 1.0f, false, 0.0f, a_player);
			}
			logger::info("Merchant: Dremora merchant summoned");
		});
		return Tr("status.the_dremora_merchant_is_on");
	}

	int RestockMerchants()
	{
		int restocked = 0;
		for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
			if (auto chest = faction ? faction->vendorData.merchantContainer : nullptr) {
				chest->ResetInventory(false);
				faction->vendorData.lastDayReset = 0;
				++restocked;
			}
		}
		logger::info("Restocked {} merchant chests", restocked);
		return restocked;
	}

	void OpenWaitMenu()
	{
		RE::UIMessageQueue::GetSingleton()->AddMessage(RE::SleepWaitMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
	}

	std::string ResetCurrentDungeon()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell || !cell->IsInteriorCell() || !cell->GetFormEditorID() || !*cell->GetFormEditorID()) {
			return Tr("status.works_only_indoors_and_in");
		}
		Console::Run(std::format("resetinterior {}", cell->GetFormEditorID()));
		return Lang::Fmt("status.resets_after_leaving_and_coming", cell->GetName());
	}

	std::string SpawnDragon()
	{
		// The dragons random attacks use (LCharDragonAny levels), but the scripted bases: the NoScript ones in that list
		// leave no soul to absorb. Placed high in the air ahead, a dragon keeps flying and comes for the player by itself.
		struct Tier
		{
			std::uint16_t level;
			RE::FormID    fire, frost;
		};
		constexpr Tier kTiers[] = {
			{ 1, 0x01CA03, 0x0F80FA },   // EncDragon01Fire / Frost
			{ 18, 0x0F80FD, 0x0F77F8 },  // 02 blood
			{ 27, 0x0351C3, 0x0351C3 },  // 03 frost only
			{ 36, 0x0F811B, 0x0F811A },  // 04 elder
			{ 45, 0x0F811C, 0x0F811E },  // 05 ancient
		};
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cell = player ? player->GetParentCell() : nullptr;
		if (!cell) {
			return Tr("status.load_a_game_first");
		}
		if (cell->IsInteriorCell()) {
			return Tr("status.dragons_only_arrive_under_the");
		}
		const auto  level = player->GetLevel();
		const Tier* tier = &kTiers[0];
		for (const auto& t : kTiers) {
			if (level >= t.level) {
				tier = &t;
			}
		}
		const bool frost = std::chrono::steady_clock::now().time_since_epoch().count() & 1;
		auto       base = RE::TESForm::LookupByID<RE::TESNPC>(frost ? tier->frost : tier->fire);
		if (!base) {
			return Tr("status.dragon_not_found_in_skyrim");
		}
		RunOnPlayer([base](RE::PlayerCharacter* a_player) {
			auto ref = a_player->PlaceObjectAtMe(base, false);
			if (!ref) {
				return;
			}
			const float heading = a_player->GetAngleZ();
			auto        pos = a_player->GetPosition();
			pos.x += std::sin(heading) * 3000.0f;
			pos.y += std::cos(heading) * 3000.0f;
			pos.z += 1800.0f;
			ref->SetPosition(pos);
			ref->SetAngle({ 0.0f, 0.0f, std::fmod(heading + RE::NI_PI, RE::NI_TWO_PI) });
			logger::info("Dragon: {:08X} \"{}\" placed at {:.0f} {:.0f} {:.0f}", base->GetFormID(), base->GetName(), pos.x, pos.y, pos.z);
		});
		return Lang::Fmt("status.is_flying_toward_the_dragonborn", base->GetName());
	}

	std::string TemperEquipped()
	{
		// Tempering is the item's ExtraHealth: 1.0 = plain, 1.6 = Legendary (the top tier the grindstone and workbench give).
		// The "Кузнечное дело, +%" slider works as Fortify Smithing does at the bench: the improvement over 1.0 grows by (1 + boost/100);
		// the name still says Legendary, the stats keep growing. Set exactly, so lowering the slider lowers it again.
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			return Tr("status.load_a_game_first");
		}
		const float boost = std::clamp(Get().smithingBoost, 0.0f, 1000.0f);
		const float health = 1.0f + 0.6f * (1.0f + boost / 100.0f);
		// The "Зачарование, +%" slider makes the item's enchantment that much stronger, as Fortify Enchanting does at the table:
		// a created copy with every magnitude ×(1 + boost/100) goes on this item only (Enchanter::Enchant: a common item with a
		// built-in one becomes its plain version first). Always made from the original, so pressing again doesn't compound.
		const float enchantMult = 1.0f + std::clamp(Get().enchantingBoost, 0.0f, 1000.0f) / 100.0f;
		RunOnPlayer([health, enchantMult](RE::PlayerCharacter* a_player) {
			// done after the inventory walk: a common item with a built-in enchantment is swapped for its plain version on the way
			struct Work
			{
				RE::TESBoundObject*  object;
				RE::ExtraDataList*   list;
				RE::EnchantmentItem* enchantment;
				std::uint16_t        charge;
			};
			std::vector<Work> work;
			auto strengthen = [&](RE::TESBoundObject* a_item, RE::ExtraDataList* a_list) {
				auto extra = a_list->GetByType<RE::ExtraEnchantment>();
				auto enchantable = a_item->As<RE::TESEnchantableForm>();
				auto current = extra && extra->enchantment ? extra->enchantment : enchantable ? enchantable->formEnchanting : nullptr;
				if (!current) {
					return false;
				}
				auto original = current;
				if (const auto it = g_boostedEnchantments.find(current->GetFormID()); it != g_boostedEnchantments.end()) {
					if (auto found = RE::TESForm::LookupByID<RE::EnchantmentItem>(it->second)) {
						original = found;
					}
				}
				RE::EnchantmentItem* wanted = original;
				if (enchantMult > 1.0f) {
					std::vector<Enchanter::EffectSpec> specs;  // every effect, hidden ones too, each that much stronger
					for (auto effect : original->effects) {
						if (effect && effect->baseEffect) {
							Enchanter::EffectSpec spec;
							spec.source = original->GetFormID();
							spec.effect = effect->baseEffect->GetFormID();
							spec.magnitude = effect->effectItem.magnitude * enchantMult;
							spec.duration = static_cast<float>(effect->effectItem.duration);
							specs.push_back(std::move(spec));
						}
					}
					wanted = Enchanter::Create(a_item->IsWeapon(), specs);
					if (!wanted) {
						return false;
					}
					g_boostedEnchantments[wanted->GetFormID()] = original->GetFormID();
				}
				if (wanted == current) {
					return true;
				}
				auto weapon = a_item->As<RE::TESObjectWEAP>();
				const auto charge = extra ? extra->charge : weapon ? weapon->amountofEnchantment : std::uint16_t{ 0 };
				work.push_back({ a_item, a_list, wanted, charge });
				return true;
			};

			int tempered = 0, enchanted = 0;
			for (auto& [item, data] : a_player->GetInventory()) {
				const auto& [count, entry] = data;
				if (!item || (!item->IsWeapon() && !item->IsArmor()) || !entry || !entry->extraLists) {
					continue;
				}
				for (auto list : *entry->extraLists) {
					if (!list || (!list->HasType(RE::ExtraDataType::kWorn) && !list->HasType(RE::ExtraDataType::kWornLeft))) {
						continue;
					}
					if (auto xHealth = list->GetByType<RE::ExtraHealth>()) {
						xHealth->health = health;
					} else {
						list->Add(new RE::ExtraHealth(health));
					}
					++tempered;
					enchanted += strengthen(item, list) ? 1 : 0;
				}
			}
			for (const auto& w : work) {
				Enchanter::Enchant(a_player, w.object, w.list, w.enchantment, w.charge);
			}
			logger::info("Tempered {} worn items to {:.2f}, {} enchantments ×{:.2f}", tempered, health, enchanted, enchantMult);
		});
		std::string status = Tr("status.equipped_gear_improved_to_legendary");
		if (boost > 0.0f) {
			status += Lang::Fmt("status.smithing", boost);
		}
		if (enchantMult > 1.0f) {
			status += Lang::Fmt("status.enchantments_stronger", (enchantMult - 1.0f) * 100.0f);
		}
		return status;
	}

	float PlayerScale()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		return player ? player->GetScale() : 1.0f;
	}

	void SetPlayerScale(float a_scale)
	{
		// The reference scale is saved with the game, so it stays without our co-save.
		RunOnPlayer([a_scale](RE::PlayerCharacter* a_player) { a_player->SetScale(std::clamp(a_scale, 0.1f, 10.0f)); });
	}

	void OpenRaceMenuKeepingSkills()
	{
		g_skillsBeforeRaceMenu.clear();
		if (auto player = RE::PlayerCharacter::GetSingleton()) {
			for (auto av = RE::ActorValue::kOneHanded; av <= RE::ActorValue::kEnchanting; av = static_cast<RE::ActorValue>(std::to_underlying(av) + 1)) {
				g_skillsBeforeRaceMenu.emplace_back(av, player->AsActorValueOwner()->GetBaseActorValue(av));
			}
		}
		Console::Run("showracemenu");
	}

	void SetFlightTestMove(std::optional<RE::NiPoint2> a_move) { g_flightTestMove = a_move; }

	std::string OpenGuildCityJobs()
	{
		if (!JoinedThievesGuild()) {
			return Tr("status.join_the_thieves_guild_first");
		}
		auto quest = RE::TESForm::LookupByID<RE::TESQuest>(kTGRShell);
		auto shell = ScriptOn(quest, "TGRShellScript");
		if (!quest || !shell) {
			return Tr("status.guild_script_not_found");
		}
		// A stage already done is not run again (the quest does not allow repeating stages).
		for (const auto& city : kGuildCities) {
			if (auto counter = shell->GetVariable(city.counter); counter && counter->IsInt() && counter->GetSInt() < 5) {
				counter->SetSInt(5);
			}
			Console::Run(std::format("setstage TGRShell {}", city.stage));
		}
		logger::info("Thieves Guild: city jobs opened");
		return Tr("status.special_jobs_are_open_in");
	}

	void FillSoulGems()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			int filled = 0;
			for (const auto& [object, data] : a_player->GetInventory()) {
				auto gem = object ? object->As<RE::TESSoulGem>() : nullptr;
				if (!gem || gem->GetContainedSoul() != RE::SOUL_LEVEL::kNone || data.first <= 0) {
					continue;
				}
				// Gems filled by the player carry their soul in an ExtraSoul; only the empty ones are swapped.
				int withSoul = 0;
				if (data.second && data.second->extraLists) {
					for (auto list : *data.second->extraLists) {
						if (list && list->HasType(RE::ExtraDataType::kSoul)) {
							withSoul += list->GetCount();
						}
					}
				}
				const int empty = data.first - withSoul;
				auto      full = FullGem(gem);
				if (empty <= 0 || !full) {
					continue;
				}
				g_gemRemovalsToIgnore[gem->GetFormID()] += empty;
				a_player->RemoveItem(gem, empty, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				a_player->AddObjectToContainer(full, nullptr, empty, nullptr);
				filled += empty;
			}
			logger::info("Filled {} soul gems", filled);
		});
	}

	bool HasStation(Station a_station) { return FindStation(a_station) != nullptr; }

	bool OpenStation(Station a_station)
	{
		auto base = FindStation(a_station);
		auto player = RE::PlayerCharacter::GetSingleton();
		auto cell = player ? player->GetParentCell() : nullptr;
		if (RE::UI::GetSingleton()->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
			return false;
		}
		RemoveStation();  // the previous one, if still standing
		auto occupied = player->GetOccupiedFurniture().get();  // can be left set to a station already removed
		if (!base || !cell || (occupied && !occupied->IsDisabled() && !occupied->IsMarkedForDeletion())) {
			return false;
		}

		// In front of the player, a step further than the station is deep, on whatever is there to stand on.
		const auto& bounds = base->boundData;
		const float reach = 60.0f + 0.5f * std::max(bounds.boundMax.x - bounds.boundMin.x, bounds.boundMax.y - bounds.boundMin.y);
		const float heading = player->GetAngleZ();
		RE::NiPoint3 pos = player->GetPosition();
		pos.x += std::sin(heading) * reach;
		pos.y += std::cos(heading) * reach;
		if (const auto floor = SurfaceBelow(player, cell, { pos.x, pos.y, pos.z + 150.0f }, pos.z - 300.0f)) {
			pos.z = *floor;
		}

		auto ref = player->PlaceObjectAtMe(base, false);
		if (!ref) {
			return false;
		}
		ref->SetPosition(pos);
		ref->SetAngle({ 0.0f, 0.0f, std::fmod(heading + RE::NI_PI, RE::NI_TWO_PI) });  // facing the player
		g_station = PendingStation{ ref->GetHandle(), base, {}, false, std::chrono::steady_clock::now() + 10s, {} };
		if (a_station == Station::kForge) {
			for (auto name : kExtraForgeKeywords) {
				auto keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(name);
				if (keyword && !base->HasKeyword(keyword) && base->AddKeyword(keyword)) {
					g_station->addedKeywords.push_back(keyword);
				}
			}
		}
		logger::info("Station: {:08X} \"{}\" placed {:.0f} in front of the player", base->GetFormID(), base->GetName(), reach);
		return true;
	}

	bool HasMapMarker()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto marker = player ? player->GetInfoRuntimeData().playerMapMarker.get() : nullptr;
		return marker && !marker->IsDisabled();
	}

	void TeleportToMapMarker()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			auto marker = a_player->GetInfoRuntimeData().playerMapMarker.get();
			if (!marker || marker->IsDisabled()) {
				return;
			}
			g_landing = PendingLanding{ marker->GetPosition(), std::chrono::steady_clock::now() + 20s };
			a_player->MoveTo(marker.get());
		});
	}

	const std::vector<SavedPoint>& SavedPoints() { return g_points; }

	void SavePoint(std::string a_name)
	{
		RunOnPlayer([name = std::move(a_name)](RE::PlayerCharacter* a_player) mutable {
			auto xmarker = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
			auto marker = xmarker ? a_player->PlaceObjectAtMe(xmarker, true) : nullptr;
			if (!marker) {
				logger::error("Teleport point: could not place a marker");
				return;
			}
			std::string place;
			auto        cell = a_player->GetParentCell();
			if (cell && cell->IsInteriorCell()) {
				place = Text::ToUtf8(cell->GetFullName());
			} else if (auto location = a_player->GetCurrentLocation()) {
				place = Text::ToUtf8(location->GetFullName());
			} else if (auto world = a_player->GetWorldspace()) {
				place = Text::ToUtf8(world->GetFullName());
			}
			if (name.empty()) {
				name = place.empty() ? Lang::Fmt("status.point", g_points.size() + 1) : place;
			}
			g_points.push_back({ marker->GetFormID(), std::move(name), std::move(place) });
		});
	}

	void TeleportToPoint(std::size_t a_index)
	{
		if (a_index >= g_points.size()) {
			return;
		}
		RunOnPlayer([formID = g_points[a_index].marker](RE::PlayerCharacter* a_player) {
			if (auto marker = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID)) {
				a_player->MoveTo(marker);
			}
		});
	}

	void DeletePoint(std::size_t a_index)
	{
		if (a_index >= g_points.size()) {
			return;
		}
		const auto formID = g_points[a_index].marker;
		g_points.erase(g_points.begin() + static_cast<std::ptrdiff_t>(a_index));
		SKSE::GetTaskInterface()->AddTask([formID]() {
			if (auto marker = RE::TESForm::LookupByID<RE::TESObjectREFR>(formID)) {
				marker->Disable();
				marker->SetDelete(true);
			}
		});
	}

	namespace
	{
		bool g_noClip = false;
		bool g_aiBlind = false;
	}

	bool IsNoClip() { return g_noClip; }

	void SetNoClip(bool a_on)
	{
		if (a_on != g_noClip) {
			Console::Run("tcl");
			g_noClip = a_on;
		}
	}

	bool IsFreeCamera()
	{
		auto camera = RE::PlayerCamera::GetSingleton();
		return camera && camera->IsInFreeCameraMode();
	}

	void SetFreeCamera(bool a_on)
	{
		if (a_on != IsFreeCamera()) {
			Console::Run("tfc");
		}
	}

	bool IsAiBlind() { return g_aiBlind; }

	void SetAiBlind(bool a_on)
	{
		if (a_on != g_aiBlind) {
			Console::Run("tdetect");
			g_aiBlind = a_on;
		}
	}

	bool IsHudHidden()
	{
		auto ui = RE::UI::GetSingleton();
		return ui && !ui->IsShowingMenus();
	}

	void SetHudHidden(bool a_on)
	{
		if (a_on != IsHudHidden()) {
			Console::Run("tm");
		}
	}

	void KillHostiles(float a_radius)
	{
		RunOnPlayer([a_radius](RE::PlayerCharacter* a_player) {
			std::vector<RE::ObjectRefHandle> targets;
			RE::ProcessLists::GetSingleton()->ForEachHighActor([&](RE::Actor* a_actor) {
				if (a_actor && a_actor != a_player && !a_actor->IsDead() && !a_actor->IsPlayerTeammate() && a_actor->IsHostileToActor(a_player) &&
					a_actor->GetPosition().GetDistance(a_player->GetPosition()) <= a_radius) {
					targets.push_back(a_actor->GetHandle());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (const auto& target : targets) {
				Console::Run("kill", target);
			}
			logger::info("Killed {} hostiles", targets.size());
		});
	}

	namespace
	{
		std::unordered_map<RE::RefHandle, std::chrono::steady_clock::time_point> g_deadSince;  // first seen dead

		bool InPlayerHome(RE::PlayerCharacter* a_player)
		{
			// by the keyword's form: HasKeywordString read garbage here and crashed
			static const auto house = RE::TESForm::LookupByID<RE::BGSKeyword>(0x000FC1A3);  // LocTypePlayerHouse
			auto              location = a_player->GetCurrentLocation();
			return house && location && location->HasKeyword(house);
		}

		// Moves a_count of a_object (all its enchanted / tempered copies first) from a_from to the player.
		void Take(RE::TESObjectREFR* a_from, RE::PlayerCharacter* a_player, RE::TESBoundObject* a_object, int a_count, RE::InventoryEntryData* a_entry)
		{
			int rest = a_count;
			if (a_entry && a_entry->extraLists) {
				std::vector<RE::ExtraDataList*> lists{ a_entry->extraLists->begin(), a_entry->extraLists->end() };
				for (auto xList : lists) {
					if (xList && rest > 0) {
						const int n = std::min(rest, static_cast<int>(xList->GetCount()));
						a_from->RemoveItem(a_object, n, RE::ITEM_REMOVE_REASON::kStoreInContainer, xList, a_player);
						rest -= n;
					}
				}
			}
			if (rest > 0) {
				a_from->RemoveItem(a_object, rest, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, a_player);
			}
		}

		// One sweep (game thread), as the auto-loot mods do it (SmartHarvest SE's rules, simplified):
		// - loose items and plants: all but clutter (Esp::IsClutter); never what the player dropped;
		// - dead bodies: everything, followers' never; in the background only 2 s after the death (killmoves, scenes);
		// - containers: unlocked ones, all but clutter; never the portable storage or a chest the player put things into;
		// - in a player home only bodies (shelves, chests and the garden are the player's own);
		// - never anything owned (no theft), activation-blocked (scripted, quest-held) or too heavy to carry.
		// a_auto: the background autoloot, which also says what it took.
		void Loot(RE::PlayerCharacter* a_player, float a_radius, bool a_auto)
		{
			// the player's own drops (not PlayerCharacter::droppedRefList: its offset is wrong for 1.6.1170, reading it crashed)
			std::erase_if(g_playerDropped, [](RE::FormID a_id) { return !RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id); });
			const auto& dropped = g_playerDropped;
			const auto now = std::chrono::steady_clock::now();
			const bool home = InPlayerHome(a_player);
			auto       avo = a_player->AsActorValueOwner();
			float      room = avo->GetActorValue(RE::ActorValue::kCarryWeight) - avo->GetActorValue(RE::ActorValue::kInventoryWeight);
			std::map<std::string, int> taken;  // name -> count
			auto fits = [&](RE::TESBoundObject* a_object, int a_count) {
				const float weight = a_object->GetWeight() * static_cast<float>(a_count);
				if (weight > 0.0f && weight > room) {
					return false;
				}
				room -= std::max(weight, 0.0f);
				return true;
			};
			auto note = [&](RE::TESBoundObject* a_object, int a_count) {
				const auto name = Text::ToUtf8(a_object->GetName());
				if (!name.empty()) {
					taken[name] += a_count;
				}
			};
			auto takeAll = [&](RE::TESObjectREFR* a_from, bool a_skipClutter) {
				for (auto& [object, data] : a_from->GetInventory()) {
					auto& [count, entry] = data;
					if (!object || count <= 0 || !object->GetPlayable() || (a_skipClutter && Esp::IsClutter(object)) || !fits(object, count)) {
						continue;
					}
					Take(a_from, a_player, object, count, entry.get());
					note(object, count);
				}
			};

			std::vector<RE::TESObjectREFR*> corpses, containers, loose, plants;
			std::unordered_set<RE::RefHandle> stillDead;
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, a_radius, [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref == a_player || a_ref->IsDisabled() || a_ref->IsMarkedForDeletion() || !a_ref->Is3DLoaded() || a_ref->IsActivationBlocked()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (auto actor = a_ref->As<RE::Actor>()) {
					if (actor->IsDead() && !actor->IsPlayerTeammate() && !a_ref->IsCrimeToActivate()) {
						const auto handle = a_ref->GetHandle().native_handle();
						stillDead.insert(handle);
						const auto since = g_deadSince.try_emplace(handle, now).first->second;
						if (!a_auto || now - since >= 2s) {
							corpses.push_back(a_ref);
						}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto base = a_ref->GetBaseObject();
				if (!base || home || a_ref->IsCrimeToActivate()) {  // never steal
					return RE::BSContainer::ForEachResult::kContinue;
				}
				switch (base->GetFormType()) {
				case RE::FormType::Container:
					if (!a_ref->IsLocked() && a_ref->GetFormID() != g_storage && std::ranges::find(g_stashes, a_ref->GetFormID()) == g_stashes.end()) {
						containers.push_back(a_ref);
					}
					break;
				case RE::FormType::Flora:
				case RE::FormType::Tree:
					if ((a_ref->formFlags & RE::TESObjectREFR::RecordFlags::kHarvested) == 0 &&
						(base->Is(RE::FormType::Flora) || base->As<RE::TESObjectTREE>()->produceItem)) {
						plants.push_back(a_ref);
					}
					break;
				case RE::FormType::Weapon:
				case RE::FormType::Armor:
				case RE::FormType::Ammo:
				case RE::FormType::AlchemyItem:
				case RE::FormType::Ingredient:
				case RE::FormType::Book:
				case RE::FormType::Scroll:
				case RE::FormType::SoulGem:
				case RE::FormType::KeyMaster:
				case RE::FormType::Misc:
					if (!dropped.contains(a_ref->GetFormID()) && !Esp::IsClutter(base)) {
						loose.push_back(a_ref);
					}
					break;
				default:
					break;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::erase_if(g_deadSince, [&](const auto& a_entry) { return !stillDead.contains(a_entry.first); });

			for (auto corpse : corpses) {
				takeAll(corpse, false);
			}
			for (auto container : containers) {
				takeAll(container, true);
			}
			for (auto ref : loose) {
				auto       base = ref->GetBaseObject();
				const auto count = static_cast<std::int32_t>(ref->extraList.GetCount());
				if (fits(base, count)) {
					note(base, count);
					a_player->PickUpObject(ref, count, false, false);
				}
			}
			for (auto plant : plants) {
				plant->ActivateRef(a_player, 0, nullptr, 1, false);  // the game harvests it: ingredient, perks, the flag
			}
			if (!taken.empty() || !plants.empty()) {
				logger::info("Loot: {} bodies, {} containers, {} loose, {} plants, {} kinds taken", corpses.size(), containers.size(), loose.size(), plants.size(), taken.size());
			}
			if (a_auto && !taken.empty()) {
				std::string list;
				int         shown = 0;
				for (const auto& [name, count] : taken) {
					if (shown++ == 3) {
						break;
					}
					list += (list.empty() ? "" : ", ") + (count > 1 ? std::format("{} ×{}", name, count) : name);
				}
				if (taken.size() > 3) {
					list += Lang::Fmt("status.autoloot_more", taken.size() - 3);
				}
				Menu::Toast(Lang::Fmt("status.autoloot_took", list));
			}
		}
	}

	void LootNearby(float a_radius)
	{
		RunOnPlayer([a_radius](RE::PlayerCharacter* a_player) { Loot(a_player, a_radius, false); });
	}

	void GiveBeastForm()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(kBeastForm)) {
				a_player->AddSpell(spell);
			}
		});
	}

	bool GiveVampireLord()
	{
		const auto formID = RE::TESDataHandler::GetSingleton()->LookupFormID(kVampireLordLocal, "Dawnguard.esm");
		if (!formID) {
			return false;
		}
		RunOnPlayer([formID](RE::PlayerCharacter* a_player) {
			if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
				a_player->AddSpell(spell);
			}
		});
		return true;
	}

	void OpenStorage()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			auto chest = g_storage ? RE::TESForm::LookupByID<RE::TESObjectREFR>(g_storage) : nullptr;
			if (!chest) {
				// First use: a persistent chest is left deep under the player's feet.
				auto base = RE::TESForm::LookupByID<RE::TESBoundObject>(kStorageChest);
				auto placed = base ? a_player->PlaceObjectAtMe(base, true) : nullptr;
				if (!placed) {
					logger::error("Storage: could not place the chest");
					return;
				}
				auto pos = placed->GetPosition();
				pos.z -= kStorageDepth;
				placed->SetPosition(pos);
				g_storage = placed->GetFormID();
				chest = placed.get();
				logger::info("Storage: chest {:08X} created", g_storage);
			}
			chest->ActivateRef(a_player, 0, nullptr, 0, false);
		});
	}

	bool MoveToNpc(RE::FormID a_npc, bool a_bringToPlayer)
	{
		auto npc = RE::TESForm::LookupByID<RE::TESNPC>(a_npc);
		if (!npc) {
			return false;
		}
		RE::ObjectRefHandle found;
		{
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard guard{ lock };
			for (const auto& [id, form] : *map) {
				auto actor = form && form->Is(RE::FormType::ActorCharacter) ? form->As<RE::Actor>() : nullptr;
				if (actor && !actor->IsMarkedForDeletion() && !actor->IsDisabled() && actor->GetActorBase() == npc) {
					found = actor->GetHandle();
					if (!actor->IsDead()) {
						break;  // prefer a living one
					}
				}
			}
		}
		if (!found) {
			return false;
		}
		RunOnPlayer([found, a_bringToPlayer](RE::PlayerCharacter* a_player) {
			if (auto ref = found.get()) {
				if (a_bringToPlayer) {
					ref->MoveTo(a_player);
				} else {
					a_player->MoveTo(ref.get());
				}
			}
		});
		return true;
	}

	std::vector<QuestInfo> RunningQuests()
	{
		using QT = RE::QUEST_DATA::Type;
		std::vector<QuestInfo> result;
		for (auto quest : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESQuest>()) {
			if (!quest || !quest->IsRunning() || quest->IsCompleted()) {
				continue;
			}
			auto name = Text::ToUtf8(quest->GetFullName());
			if (name.empty()) {
				continue;
			}
			// Like the journal: only quests with an objective currently shown to the player.
			const bool shown = std::ranges::any_of(quest->objectives, [](const RE::BGSQuestObjective* a_objective) {
				return a_objective && a_objective->state.get() == RE::QUEST_OBJECTIVE_STATE::kDisplayed;
			});
			if (!shown) {
				continue;
			}
			const char* type = "";
			switch (quest->GetType()) {
			case QT::kMainQuest: type = "quest_type.main"; break;
			case QT::kMagesGuild: type = "quest_type.college_of_winterhold"; break;
			case QT::kThievesGuild: type = "quest_type.thieves_guild"; break;
			case QT::kDarkBrotherhood: type = "quest_type.dark_brotherhood"; break;
			case QT::kCompanionsQuest: type = "quest_type.companions"; break;
			case QT::kMiscellaneous: type = "cat.misc"; break;
			case QT::kDaedric: type = "quest_type.daedric"; break;
			case QT::kSideQuest: type = "quest_type.side"; break;
			case QT::kCivilWar: type = "quest_type.civil_war"; break;
			case QT::kDLC01_Vampire: type = "Dawnguard"; break;
			case QT::kDLC02_Dragonborn: type = "Dragonborn"; break;
			default: continue;  // untyped quests are internal / radiant machinery
			}
			result.push_back({ quest->GetFormID(), std::move(name), type, quest->GetCurrentStageID() });
		}
		std::ranges::sort(result, [](const QuestInfo& a, const QuestInfo& b) { return a.type != b.type ? a.type < b.type : a.name < b.name; });
		return result;
	}

	bool NextQuestStage(RE::FormID a_quest)
	{
		auto quest = RE::TESForm::LookupByID<RE::TESQuest>(a_quest);
		if (!quest || !quest->waitingStages) {
			return false;
		}
		const auto current = quest->GetCurrentStageID();
		std::optional<std::uint16_t> next;
		for (auto stage : *quest->waitingStages) {
			if (stage && stage->data.index > current && (!next || stage->data.index < *next)) {
				next = stage->data.index;
			}
		}
		if (!next) {
			return false;
		}
		Console::Run(std::format("setstage {:08X} {}", a_quest, *next));
		return true;
	}

	void CompleteQuest(RE::FormID a_quest) { Console::Run(std::format("completequest {:08X}", a_quest)); }

	void DismissSummons()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			auto process = a_player->GetActorRuntimeData().currentProcess;
			if (!process || !process->middleHigh) {
				return;
			}
			// Ending the summoning effect unsummons the creature / releases the thrall properly.
			std::vector<RE::ActiveEffect*> effects;
			for (auto& commanded : process->middleHigh->commandedActors) {
				if (commanded.activeEffect) {
					effects.push_back(commanded.activeEffect);
				}
			}
			for (auto effect : effects) {
				effect->Dispel(true);
			}
		});
	}

	void SummonFollowers()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			std::vector<RE::Actor*> followers;
			RE::ProcessLists::GetSingleton()->ForAllActors([&](RE::Actor* a_actor) {
				if (a_actor && a_actor != a_player && a_actor->IsPlayerTeammate() && !a_actor->IsDead()) {
					followers.push_back(a_actor);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			for (auto follower : followers) {
				follower->MoveTo(a_player);
			}
			logger::info("Summoned {} followers", followers.size());
		});
	}

	void ClearBounties()
	{
		SKSE::GetTaskInterface()->AddTask(ClearAllBounties);
	}

	int UnmarkStolen()
	{
		// "Stolen" = owned by someone else. The owner becomes the player (removing the ownership instead can
		// leave an empty extra-data list behind, which crashed the game on the next load).
		auto player = RE::PlayerCharacter::GetSingleton();
		auto self = player ? player->GetActorBase() : nullptr;
		if (!self) {
			return 0;
		}
		int items = 0;
		for (auto& [object, data] : player->GetInventory()) {
			if (!data.second || !data.second->extraLists) {
				continue;
			}
			for (auto list : *data.second->extraLists) {
				auto ownership = list ? list->GetByType<RE::ExtraOwnership>() : nullptr;
				if (ownership && ownership->owner && ownership->owner != self) {
					items += list->GetCount();
					ownership->owner = self;
				}
			}
		}
		logger::info("Unmarked {} stolen items", items);
		return items;
	}

	void FlingTarget(RE::ObjectRefHandle a_target, float a_force)
	{
		if (auto ref = a_target.get()) {
			Console::Run(std::format("player.pushactoraway {:08X} {:.0f}", ref->GetFormID(), a_force));
		}
	}

	void LearnAllEnchantments()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			// Disenchanting makes the player "know" the enchantment's base (the item's leveled variant points
			// to it) and its magic effects. The ones learned here are the ones disenchanting could give: those
			// of playable weapons and armour, artifacts only with "enchant anything" on. Every other enchantment
			// (bound weapon visuals, "Not User Facing" ones...) is never offered at the table, so any of those an
			// older version of this button marked known are cleaned up again. Through the game's own function,
			// so the change is written into the save.
			auto disallow = Get().enchantAnything ? nullptr : RE::TESForm::LookupByEditorID<RE::BGSKeyword>("MagicDisallowEnchanting");
			std::unordered_set<RE::EnchantmentItem*> learnable;
			auto collect = [&](auto& a_items) {
				for (auto item : a_items) {
					auto enchantment = item ? item->formEnchanting : nullptr;
					if (!enchantment || !item->GetPlayable() || !item->GetName() || !*item->GetName() || (disallow && item->HasKeyword(disallow))) {
						continue;
					}
					auto base = enchantment->data.baseEnchantment ? enchantment->data.baseEnchantment : enchantment;
					if (!(disallow && base->HasKeyword(disallow)) && base->GetName() && *base->GetName()) {
						learnable.insert(base);
					}
				}
			};
			auto dh = RE::TESDataHandler::GetSingleton();
			collect(dh->GetFormArray<RE::TESObjectWEAP>());
			collect(dh->GetFormArray<RE::TESObjectARMO>());

			int enchantments = 0, effects = 0, cleaned = 0;
			for (auto enchantment : dh->GetFormArray<RE::EnchantmentItem>()) {
				if (!enchantment) {
					continue;
				}
				if (!learnable.contains(enchantment)) {
					if (enchantment->GetKnown()) {
						enchantment->SetPlayerKnows(false);
						++cleaned;
					}
					continue;
				}
				if (!enchantment->GetKnown()) {
					enchantment->SetPlayerKnows(true);
					++enchantments;
				}
				for (auto effect : enchantment->effects) {
					if (auto base = effect ? effect->baseEffect : nullptr; base && !base->GetKnown()) {
						base->SetPlayerKnows(true);
						++effects;
					}
				}
			}
			logger::info("Learned {} enchantments ({} learnable), {} magic effects; {} never-offered ones unmarked", enchantments, learnable.size(), effects, cleaned);
		});
	}

	void LearnAllIngredients()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			int learned = 0, total = 0;
			for (auto ingredient : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::IngredientItem>()) {
				if (!ingredient) {
					continue;
				}
				++total;
				if ((ingredient->gamedata.knownEffectFlags & 0xF) != 0xF) {
					ingredient->LearnAllEffects();
					++learned;
				}
			}
			logger::info("Ingredients: learned all effects of {} (of {})", learned, total);
		});
	}

	void LearnAllSpells()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			int learned = 0;
			for (auto book : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectBOOK>()) {
				auto spell = book && book->TeachesSpell() ? book->GetSpell() : nullptr;
				if (spell && !a_player->HasSpell(spell)) {
					a_player->AddSpell(spell);
					g_learned.push_back(spell->GetFormID());
					++learned;
				}
			}
			logger::info("Learned {} spells from spell tomes", learned);
		});
	}

	void LearnAllShouts()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			const auto shouts = Catalog::PlayerShouts();  // only the player's own shouts, no dragon / NPC copies
			int        learned = 0;
			for (auto shout : shouts) {
				if (!a_player->HasShout(shout)) {
					g_learned.push_back(shout->GetFormID());
					Grant(shout->GetFormID(), 1);  // adds the shout, then teaches and unlocks each word
					++learned;
				}
			}
			logger::info("Learned {} shouts (of {} the player can have)", learned, shouts.size());
		});
	}

	void LearnAllPerks()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			int learned = 0;
			ForEachPerkNode([&](RE::BGSSkillPerkTreeNode* a_node) {
				for (auto rank : PerkRanks(a_node->perk)) {
					if (!a_player->HasPerk(rank)) {
						a_player->AddPerk(rank);
						g_learned.push_back(rank->GetFormID());
						++learned;
					}
				}
			});
			logger::info("Learned {} perks", learned);
		});
	}

	std::size_t LearnedCount() { return g_learned.size(); }

	void ForgetLearned()
	{
		RunOnPlayer([](RE::PlayerCharacter* a_player) {
			for (auto formID : g_learned) {
				auto form = RE::TESForm::LookupByID(formID);
				if (auto perk = form ? form->As<RE::BGSPerk>() : nullptr) {
					a_player->RemovePerk(perk);
				} else if (auto spell = form ? form->As<RE::SpellItem>() : nullptr) {
					a_player->RemoveSpell(spell);
				} else if (auto shout = form ? form->As<RE::TESShout>() : nullptr) {
					// AddShout puts the shout into the base NPC's spell list; Papyrus RemoveShout takes it from there too.
					if (auto spells = a_player->GetActorBase() ? a_player->GetActorBase()->GetSpellList() : nullptr) {
						spells->RemoveShout(shout);
					}
				}
			}
			logger::info("Forgot {} learned perks / spells / shouts", g_learned.size());
			g_learned.clear();
		});
	}

	std::span<const Stone> StandingStones() { return kStones; }

	int CurrentStone()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		for (int i = 0; player && i < static_cast<int>(std::size(kStones)); ++i) {
			if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(kStones[i].spell); spell && player->HasSpell(spell)) {
				return i;
			}
		}
		return -1;
	}

	void SetStone(int a_index)
	{
		RunOnPlayer([a_index](RE::PlayerCharacter* a_player) {
			auto remove = [&](RE::FormID a_id) {
				if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(a_id); spell && a_player->HasSpell(spell)) {
					a_player->RemoveSpell(spell);
				}
			};
			for (const auto& stone : kStones) {
				remove(stone.spell);
			}
			remove(kApprenticeWeakness);
			if (a_index < 0 || a_index >= static_cast<int>(std::size(kStones))) {
				return;
			}
			if (auto spell = RE::TESForm::LookupByID<RE::SpellItem>(kStones[a_index].spell)) {
				a_player->AddSpell(spell);
			}
			if (kStones[a_index].spell == kApprenticeStone) {  // the Apprentice also brings its weakness
				if (auto weakness = RE::TESForm::LookupByID<RE::SpellItem>(kApprenticeWeakness)) {
					a_player->AddSpell(weakness);
				}
			}
		});
	}

	void SetBaseValue(RE::ActorValue a_av, float a_value)
	{
		RunOnPlayer([a_av, a_value](RE::PlayerCharacter* a_player) {
			a_player->AsActorValueOwner()->SetBaseActorValue(a_av, a_value);
		});
	}

	void SetLevel(int a_level) { Console::Run(std::format("player.setlevel {}", std::clamp(a_level, 1, 65535))); }

	void SetHour(float a_hour)
	{
		SKSE::GetTaskInterface()->AddTask([a_hour]() {
			if (auto calendar = RE::Calendar::GetSingleton(); calendar && calendar->gameHour) {
				calendar->gameHour->value = a_hour;
			}
		});
	}

	void SetTimescale(float a_scale)
	{
		SKSE::GetTaskInterface()->AddTask([a_scale]() {
			if (auto calendar = RE::Calendar::GetSingleton(); calendar && calendar->timeScale) {
				calendar->timeScale->value = a_scale;
			}
		});
	}

	void ForceWeather(RE::FormID a_weather)
	{
		SKSE::GetTaskInterface()->AddTask([a_weather]() {
			auto weather = RE::TESForm::LookupByID<RE::TESWeather>(a_weather);
			if (auto sky = RE::Sky::GetSingleton(); sky && weather) {
				sky->ForceWeather(weather, true);
			}
		});
	}

	void ReleaseWeather()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			if (auto sky = RE::Sky::GetSingleton()) {
				sky->ReleaseWeatherOverride();
			}
		});
	}
}

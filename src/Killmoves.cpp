#include "Killmoves.h"

#include "Cheats.h"

// Killmoves for the player, from the game's own killmove idles (no plugin, no new animations).
//
// A melee attack plays a killmove when one of the killmove roots under the attack idles passes its conditions
// (KillMoveFrontSideRoot/BackSideRoot, with "00" variants for normal attacks, the dragon roots, ...). They all
// need ShouldAttackKill (the hit would kill). There is no extra random roll in the engine: when the conditions
// pass, the killmove plays. What limits the player is a pair at the end of the front/back roots:
//     GetIsID(Player) == 0  OR  target IsLastHostileActor == 1
// i.e. NPCs killmove anyone, the player only the last enemy (the dragon roots require the last enemy from everyone).
// Decapitations are leaves of the killmove trees with HasPerk(<Savage Strike / Devastating Blow>) and
// GetRandomPercent <= DecapitationChance.
//
// Our own condition items are put into these lists once, the first time killmoves are switched on, and are
// never removed (the game may be checking the lists from another thread); afterwards only their values change,
// and switched off every one of them is neutral, so the game behaves as without them.
namespace Killmoves
{
	namespace
	{
		using F = RE::FUNCTION_DATA::FunctionID;
		using Op = RE::CONDITION_ITEM_DATA::OpCode;

		constexpr RE::FormID kPlayerBase = 0x7;

		RE::TESConditionItem* MakeItem(F a_function, Op a_op, float a_value, RE::CONDITIONITEMOBJECT a_on, void* a_param = nullptr)
		{
			auto item = new RE::TESConditionItem();
			item->next = nullptr;
			item->data.comparisonValue.f = a_value;
			item->data.functionData.function = a_function;
			item->data.functionData.params[0] = a_param;
			item->data.functionData.params[1] = nullptr;
			item->data.flags.isOR = false;
			item->data.flags.global = false;
			item->data.flags.opCode = a_op;
			item->data.object = a_on;
			return item;
		}

		// a_item OR a_new, the rest of the list unchanged.
		void InsertOr(RE::TESConditionItem* a_item, RE::TESConditionItem* a_new)
		{
			a_new->data.flags.isOR = a_item->data.flags.isOR;
			a_new->next = a_item->next;
			a_item->next = a_new;
			a_item->data.flags.isOR = true;
		}

		void Append(RE::TESCondition& a_conditions, std::initializer_list<RE::TESConditionItem*> a_items)
		{
			auto last = a_conditions.head;
			while (last && last->next) {
				last = last->next;
			}
			for (auto item : a_items) {
				(last ? last->next : a_conditions.head) = item;
				last = item;
			}
		}

		RE::TESConditionItem* Find(const RE::TESCondition& a_conditions, F a_function)
		{
			for (auto item = a_conditions.head; item; item = item->next) {
				if (item->data.functionData.function == a_function) {
					return item;
				}
			}
			return nullptr;
		}

		struct Items
		{
			std::vector<RE::TESConditionItem*> anyEnemy;    // ... OR GetIsID(Player) == 1 (2 = off)
			std::vector<RE::TESConditionItem*> chance;      // GetIsID(Player) == 0 OR GetRandomPercent < chance (100 = off)
			std::vector<RE::TESConditionItem*> notPlayer;   // target GetIsID(Player) != 1 (!= 2 = off)
			std::vector<RE::TESConditionItem*> free;        // target GetRestrained != 1 (!= 2 = off); a killmove on a restrained actor crashed the game
			std::vector<RE::TESConditionItem*> decapOwn;    // GetIsID(Player) == 1 OR <the game's roll> (2 = off)
			std::vector<RE::TESConditionItem*> decapRoll;   // GetIsID(Player) == 0 OR GetRandomPercent <= chance (100 = off)
			std::vector<RE::TESConditionItem*> decapNoPerk; // HasPerk OR GetIsID(Player) == 1 (2 = off)
		};
		std::optional<Items> g_items;

		bool IsKillmove(RE::TESIdleForm* a_idle)
		{
			const char* edid = a_idle->GetFormEditorID();
			return edid && std::string_view{ edid }.find("KillMove") != std::string_view::npos;
		}

		void Install()
		{
			auto player = RE::TESForm::LookupByID(kPlayerBase);
			if (!player) {
				return;
			}
			Items items;
			auto  isPlayer = [&](float a_value) { return MakeItem(F::kGetIsID, Op::kEqualTo, a_value, RE::CONDITIONITEMOBJECT::kSelf, player); };
			int   roots = 0, decaps = 0;

			const auto [forms, lock] = RE::TESForm::GetAllForms();
			RE::BSReadLockGuard guard(lock);
			for (const auto& [id, form] : *forms) {
				auto idle = form ? form->As<RE::TESIdleForm>() : nullptr;
				if (!idle || !IsKillmove(idle)) {
					continue;
				}
				auto& conditions = idle->conditions;
				if (Find(conditions, F::kShouldAttackKill)) {
					++roots;
					if (auto last = Find(conditions, F::kIsLastHostileActor)) {
						auto item = isPlayer(2.0f);
						InsertOr(last, item);
						items.anyEnemy.push_back(item);
					}
					auto roll = MakeItem(F::kGetRandomPercent, Op::kLessThan, 100.0f, RE::CONDITIONITEMOBJECT::kSelf);
					auto notMe = isPlayer(0.0f);
					notMe->data.flags.isOR = true;
					auto victim = MakeItem(F::kGetIsID, Op::kNotEqualTo, 2.0f, RE::CONDITIONITEMOBJECT::kTarget, player);
					auto unbound = MakeItem(F::kGetRestrained, Op::kNotEqualTo, 2.0f, RE::CONDITIONITEMOBJECT::kTarget);
					Append(conditions, { notMe, roll, victim, unbound });
					items.chance.push_back(roll);
					items.notPlayer.push_back(victim);
					items.free.push_back(unbound);
				}
				auto gameRoll = Find(conditions, F::kGetRandomPercent);
				if (gameRoll && gameRoll->data.flags.global && gameRoll->data.comparisonValue.g &&
					std::string_view{ gameRoll->data.comparisonValue.g->GetFormEditorID() } == "DecapitationChance") {
					++decaps;
					// Put "GetIsID(Player) == 1 OR" in front of the game's roll: swap the roll's contents into a new item
					// that follows, and turn the existing item into the player check (so the list keeps its head).
					auto rollCopy = new RE::TESConditionItem();
					rollCopy->data = gameRoll->data;
					rollCopy->next = gameRoll->next;
					gameRoll->data.flags.global = false;
					gameRoll->data.comparisonValue.f = 2.0f;
					gameRoll->data.functionData.function = F::kGetIsID;
					gameRoll->data.functionData.params[0] = player;
					gameRoll->data.functionData.params[1] = nullptr;
					gameRoll->data.flags.opCode = Op::kEqualTo;
					gameRoll->data.object = RE::CONDITIONITEMOBJECT::kSelf;
					gameRoll->data.flags.isOR = true;
					gameRoll->next = rollCopy;
					items.decapOwn.push_back(gameRoll);

					auto notMe = isPlayer(0.0f);
					notMe->data.flags.isOR = true;
					auto roll = MakeItem(F::kGetRandomPercent, Op::kLessThanOrEqualTo, 100.0f, RE::CONDITIONITEMOBJECT::kSelf);
					Append(conditions, { notMe, roll });
					items.decapRoll.push_back(roll);

					if (auto perk = Find(conditions, F::kHasPerk)) {
						auto item = isPlayer(2.0f);
						InsertOr(perk, item);
						items.decapNoPerk.push_back(item);
					}
				}
			}
			logger::info("Killmoves: {} killmove roots, {} decapitations", roots, decaps);
			g_items = std::move(items);
		}

		void Set(const std::vector<RE::TESConditionItem*>& a_items, float a_value)
		{
			for (auto item : a_items) {
				item->data.comparisonValue.f = a_value;
			}
		}
	}

	void Apply()
	{
		const auto& st = Cheats::Get();
		if (!g_items) {
			if (!st.killmoves) {
				return;  // never switched on: the game's lists stay untouched
			}
			Install();
			if (!g_items) {
				return;
			}
		}
		const bool on = st.killmoves;
		Set(g_items->anyEnemy, on && st.killmoveAnyEnemy ? 1.0f : 2.0f);
		Set(g_items->chance, on ? std::clamp(std::round(st.killmoveChance), 0.0f, 100.0f) : 100.0f);
		Set(g_items->notPlayer, on && st.noKillmovesOnPlayer ? 1.0f : 2.0f);
		Set(g_items->free, on ? 1.0f : 2.0f);
		Set(g_items->decapOwn, on ? 1.0f : 2.0f);
		Set(g_items->decapRoll, on ? std::clamp(std::round(st.decapChance), 0.0f, 100.0f) : 100.0f);
		Set(g_items->decapNoPerk, on && st.decapNoPerk ? 1.0f : 2.0f);
	}
}

#include "Catalog.h"

#include "Text.h"

namespace Catalog
{
	namespace
	{
		std::vector<Entry>       g_forms;
		std::vector<std::string> g_plugins;
		std::vector<Marker>      g_markers;
		std::vector<Weather>     g_weathers;

		std::string PluginOf(const RE::TESForm* a_form)
		{
			const auto file = a_form->GetFile(0);
			return file ? std::string{ file->GetFilename() } : std::string{ Tr("common.dynamic") };
		}

		template <class T, class Filter>
		void Collect(Category a_cat, Filter&& a_filter)
		{
			auto dh = RE::TESDataHandler::GetSingleton();
			for (auto form : dh->GetFormArray<T>()) {
				if (!form || form->IsDeleted() || !a_filter(form)) {
					continue;
				}
				const char* name = form->GetName();
				if (!name || !*name) {
					continue;
				}
				auto utf8 = Text::ToUtf8(name);
				g_forms.push_back({ form->GetFormID(), a_cat, utf8, Text::ToLower(utf8), PluginOf(form) });
			}
		}

		constexpr auto kAny = [](auto*) { return true; };

		// Perks of the skill trees (and the werewolf / Vampire Lord trees), every rank, with "rank k/n" for the multi-rank ones.
		// Other playable perks are NPC bonuses, set bonuses and quest helpers that do nothing useful on the player.
		std::unordered_map<RE::BGSPerk*, std::pair<int, int>> TreePerks()
		{
			std::unordered_map<RE::BGSPerk*, std::pair<int, int>> perks;
			std::unordered_set<RE::BGSSkillPerkTreeNode*>          seen;
			std::vector<RE::BGSSkillPerkTreeNode*>                 todo;
			auto addTree = [&](RE::ActorValue a_av) {
				if (auto info = RE::ActorValueList::GetActorValueInfo(a_av); info && info->perkTree) {
					todo.push_back(info->perkTree);
				}
			};
			for (auto av = RE::ActorValue::kOneHanded; av <= RE::ActorValue::kEnchanting; av = static_cast<RE::ActorValue>(std::to_underlying(av) + 1)) {
				addTree(av);
			}
			addTree(RE::ActorValue::kWerewolfPerks);
			addTree(RE::ActorValue::kVampirePerks);
			while (!todo.empty()) {
				auto node = todo.back();
				todo.pop_back();
				if (!node || !seen.insert(node).second) {
					continue;
				}
				std::vector<RE::BGSPerk*> ranks;
				for (auto perk = node->perk; perk && ranks.size() < 16 && std::ranges::find(ranks, perk) == ranks.end(); perk = perk->nextPerk) {
					ranks.push_back(perk);
				}
				for (std::size_t i = 0; i < ranks.size(); ++i) {
					perks.emplace(ranks[i], std::pair{ static_cast<int>(i) + 1, static_cast<int>(ranks.size()) });
				}
				for (auto child : node->children) {
					todo.push_back(child);
				}
			}
			return perks;
		}

		// Spells for the catalog: those a spell tome teaches, and powers / lesser powers - minus the ones beast forms
		// (Vampire Lord, werewolf, creatures) carry in their race spell list, and powers whose every effect is hidden.
		// The rest are NPC copies, staff / trap / quest spells and visual effects that break or do nothing on the player.
		std::unordered_set<RE::SpellItem*> CatalogSpells()
		{
			auto                               dh = RE::TESDataHandler::GetSingleton();
			std::unordered_set<RE::SpellItem*> spells, beast, playable;
			for (auto race : dh->GetFormArray<RE::TESRace>()) {
				if (!race || !race->actorEffects || !race->actorEffects->spells) {
					continue;
				}
				for (std::uint32_t i = 0; i < race->actorEffects->numSpells; ++i) {
					(race->GetPlayable() ? playable : beast).insert(race->actorEffects->spells[i]);
				}
			}
			// the vampire variants of the playable races are not flagged playable but carry the same racial powers
			std::erase_if(beast, [&](RE::SpellItem* a_spell) { return playable.contains(a_spell); });
			for (auto book : dh->GetFormArray<RE::TESObjectBOOK>()) {
				if (book && book->TeachesSpell() && book->GetSpell()) {
					spells.insert(book->GetSpell());
				}
			}
			// Beast-form powers the transformation scripts hand out (not in a race list); cast in human form they break the form's state.
			constexpr std::pair<RE::FormID, std::string_view> kFormOnly[] = {
				{ 0x011996, "Dawnguard.esm" }, { 0x00E653, "Dawnguard.esm" }, { 0x0068B1, "Dawnguard.esm" }, { 0x0038B9, "Dawnguard.esm" },  // Летучие мыши
				{ 0x00D650, "Dawnguard.esm" }, { 0x0038BA, "Dawnguard.esm" },                                                                // Туманная форма
				{ 0x00CD5C, "Dawnguard.esm" },                                                                                               // Восстановление облика
				{ 0x0038BC, "Dawnguard.esm" },                                                                                               // Сверхъестественные рефлексы
				{ 0x0038B8, "Dawnguard.esm" },                                                                                               // Обнаружение всех существ
				{ 0x0029AC, "Dawnguard.esm" },                                                                                               // Принесение в жертву
				{ 0x035B1B, "Dragonborn.esm" },                                                                                              // Инстинкт Морозной Луны (werewolf)
			};
			for (const auto& [id, plugin] : kFormOnly) {
				if (auto spell = dh->LookupForm<RE::SpellItem>(id, plugin)) {
					beast.insert(spell);
				}
			}
			using ST = RE::MagicSystem::SpellType;
			for (auto spell : dh->GetFormArray<RE::SpellItem>()) {
				const auto type = spell ? spell->GetSpellType() : ST::kSpell;
				if (!spell || (type != ST::kPower && type != ST::kLesserPower) || beast.contains(spell)) {
					continue;
				}
				const bool visible = std::ranges::any_of(spell->effects, [](RE::Effect* a_effect) {
					return a_effect && a_effect->baseEffect && !a_effect->baseEffect->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHideInUI);
				});
				if (visible) {
					spells.insert(spell);
				}
			}
			return spells;
		}
	}

	std::vector<RE::TESShout*> PlayerShouts()
	{
		// Dragons and NPCs have copies that lack a description, use the placeholder menu icon, or reuse the same
		// three words (the Greybeards' Unrelenting Force) - of those the lowest form ID is the player's.
		constexpr RE::FormID kPlaceholderIcon = 0x000435A5;
		std::map<std::array<RE::TESWordOfPower*, 3>, RE::TESShout*> byWords;
		for (auto shout : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESShout>()) {
			if (!shout || !shout->GetName() || !*shout->GetName() || !shout->menuDispObject || shout->menuDispObject->GetFormID() == kPlaceholderIcon) {
				continue;
			}
			std::array<RE::TESWordOfPower*, 3> words{};
			bool                               named = true;
			for (std::size_t i = 0; i < words.size(); ++i) {
				words[i] = shout->variations[i].word;
				named = named && words[i] && words[i]->GetName() && *words[i]->GetName();
			}
			RE::BSString description;
			shout->GetDescription(description, shout);
			if (!named || description.empty()) {
				continue;
			}
			if (auto& kept = byWords[words]; !kept || shout->GetFormID() < kept->GetFormID()) {
				kept = shout;
			}
		}
		std::vector<RE::TESShout*> shouts;
		for (const auto& [words, shout] : byWords) {
			shouts.push_back(shout);
		}
		return shouts;
	}

	const char* CategoryName(Category a_cat)
	{
		switch (a_cat) {
		case Category::kWeapon:
			return Tr("cat.weapons");
		case Category::kArmor:
			return Tr("cat.armor_and_clothing");
		case Category::kAmmo:
			return Tr("cat.arrows_and_bolts");
		case Category::kPotion:
			return Tr("cat.potions_and_food");
		case Category::kIngredient:
			return Tr("cat.ingredients");
		case Category::kBook:
			return Tr("cat.books");
		case Category::kScroll:
			return Tr("cat.scrolls");
		case Category::kSoulGem:
			return Tr("cat.soul_gems");
		case Category::kKey:
			return Tr("cat.keys");
		case Category::kMisc:
			return Tr("cat.misc");
		case Category::kSpell:
			return Tr("cat.spells");
		case Category::kShout:
			return Tr("cat.shouts");
		case Category::kPerk:
			return Tr("cat.perks");
		case Category::kNPC:
			return Tr("cat.creatures_and_npcs");
		default:
			return "?";
		}
	}

	Group GroupOf(Category a_cat)
	{
		switch (a_cat) {
		case Category::kSpell:
		case Category::kShout:
		case Category::kPerk:
			return Group::kMagic;
		case Category::kNPC:
			return Group::kActors;
		default:
			return Group::kItems;
		}
	}

	void BuildForms()
	{
		g_forms.clear();
		g_plugins.clear();
		g_weathers.clear();

		Collect<RE::TESObjectWEAP>(Category::kWeapon, kAny);
		Collect<RE::TESObjectARMO>(Category::kArmor, kAny);
		Collect<RE::TESAmmo>(Category::kAmmo, kAny);
		Collect<RE::AlchemyItem>(Category::kPotion, kAny);
		Collect<RE::IngredientItem>(Category::kIngredient, kAny);
		Collect<RE::TESObjectBOOK>(Category::kBook, kAny);
		Collect<RE::ScrollItem>(Category::kScroll, kAny);
		Collect<RE::TESSoulGem>(Category::kSoulGem, kAny);
		Collect<RE::TESKey>(Category::kKey, kAny);
		Collect<RE::TESObjectMISC>(Category::kMisc, kAny);
		const auto spells = CatalogSpells();
		const auto firstSpell = g_forms.size();
		Collect<RE::SpellItem>(Category::kSpell, [&](RE::SpellItem* a_spell) { return spells.contains(a_spell); });
		// one entry per name: the leveled copies of a power (Слуга вампира ×4, Вызов Карстага ×3) - the lowest form ID
		std::unordered_map<std::string, std::size_t> spellByName;
		for (auto i = firstSpell; i < g_forms.size();) {
			auto [it, added] = spellByName.try_emplace(g_forms[i].name, i);
			if (added) {
				++i;
				continue;
			}
			if (g_forms[i].formID < g_forms[it->second].formID) {
				g_forms[it->second] = g_forms[i];
			}
			g_forms.erase(g_forms.begin() + static_cast<std::ptrdiff_t>(i));
		}
		const auto shouts = PlayerShouts();
		Collect<RE::TESShout>(Category::kShout, [&](RE::TESShout* a_shout) { return std::ranges::find(shouts, a_shout) != shouts.end(); });
		const auto perks = TreePerks();
		const auto firstPerk = g_forms.size();
		Collect<RE::BGSPerk>(Category::kPerk, [&](RE::BGSPerk* a_perk) { return perks.contains(a_perk); });
		for (auto i = firstPerk; i < g_forms.size(); ++i) {
			const auto [rank, ranks] = perks.at(RE::TESForm::LookupByID<RE::BGSPerk>(g_forms[i].formID));
			if (ranks > 1) {
				g_forms[i].name += std::format(" ({}/{})", rank, ranks);
				g_forms[i].nameLower = Text::ToLower(g_forms[i].name);
			}
		}
		Collect<RE::TESNPC>(Category::kNPC, kAny);

		std::ranges::sort(g_forms, [](const Entry& a, const Entry& b) {
			return a.category != b.category ? a.category < b.category : a.nameLower < b.nameLower;
		});

		for (const auto& entry : g_forms) {
			if (std::ranges::find(g_plugins, entry.plugin) == g_plugins.end()) {
				g_plugins.push_back(entry.plugin);
			}
		}
		std::ranges::sort(g_plugins);

		using WF = RE::TESWeather::WeatherDataFlag;
		for (auto weather : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWeather>()) {
			if (!weather) {
				continue;
			}
			const auto& flags = weather->data.flags;
			const char* kind = flags.any(WF::kSnow)     ? "weather.snow" :
			                   flags.any(WF::kRainy)    ? "weather.rain" :
			                   flags.any(WF::kCloudy)   ? "weather.cloudy" :
			                   flags.any(WF::kPleasant) ? "weather.clear" :
			                                              "weather.special";
			g_weathers.push_back({ weather->GetFormID(), kind, std::format("{:08X} · {}", weather->GetFormID(), PluginOf(weather)) });
		}

		logger::info("Catalog: {} forms from {} plugins, {} weathers", g_forms.size(), g_plugins.size(), g_weathers.size());
	}

	const std::vector<Entry>&       Forms() { return g_forms; }
	const std::vector<std::string>& Plugins() { return g_plugins; }

	void BuildMarkers()
	{
		g_markers.clear();
		for (auto world : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESWorldSpace>()) {
			if (!world || !world->persistentCell) {
				continue;
			}
			const auto worldName = Text::ToUtf8(world->GetName());
			world->persistentCell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
				auto marker = a_ref ? a_ref->extraList.GetByType<RE::ExtraMapMarker>() : nullptr;
				if (marker && marker->mapData) {
					auto name = Text::ToUtf8(marker->mapData->locationName.GetFullName());
					if (!name.empty()) {
						g_markers.push_back({ a_ref->GetHandle(), name, Text::ToLower(name), worldName });
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		std::ranges::sort(g_markers, [](const Marker& a, const Marker& b) {
			return a.world != b.world ? a.world < b.world : a.nameLower < b.nameLower;
		});
		logger::info("Catalog: {} map markers", g_markers.size());
	}

	const std::vector<Marker>&  Markers() { return g_markers; }
	const std::vector<Weather>& Weathers() { return g_weathers; }
}

#include "Hotkeys.h"

#include "Cheats.h"
#include "Puzzles.h"
#include "Console.h"
#include "Menu.h"
#include "Settings.h"

namespace Hotkeys
{
	namespace
	{
		constexpr std::uint32_t kEscape = 0x01;
		constexpr std::uint32_t kBackspace = 0x0E;
		constexpr std::uint32_t kDelete = 0xD3;
		constexpr float         kSlowWorld = 0.3f;
		constexpr float         kKillRadius = 5000.0f;

		constexpr Action kActions[] = {
			{ "godMode", "feat.god_mode" },
			{ "infiniteHealth", "feat.infinite_health" },
			{ "infiniteStamina", "feat.infinite_stamina" },
			{ "infiniteMagicka", "feat.infinite_magicka" },
			{ "noCarryLimit", "feat.no_weight_limit" },
			{ "noFallDamage", "feat.no_fall_damage" },
			{ "infiniteAmmo", "feat.infinite_arrows" },
			{ "infiniteCharge", "feat.infinite_weapon_charge" },
			{ "noShoutCooldown", "feat.no_shout_cooldown" },
			{ "weightlessMisc", "feat.weightless_consumables" },
			{ "easyPickpocket", "feat.100_pickpocket" },
			{ "lockpickHud", "feat.lockpicking_helper" },
			{ "noSpellCost", "feat.free_spells" },
			{ "instantCast", "feat.instant_casting" },
			{ "essential", "feat.can_t_die" },
			{ "immunity", "feat.disease_and_paralysis_immunity" },
			{ "nightEye", "feat.night_eye" },
			{ "waterWalking", "feat.water_walking" },
			{ "infinitePoison", "feat.infinite_poison" },
			{ "infiniteLockpicks", "feat.infinite_lockpicks" },
			{ "infiniteTorch", "feat.infinite_torches" },
			{ "eternalBuffs", "feat.permanent_blessings" },
			{ "dismissSummons", "feat.dismiss_summons" },
			{ "followers", "feat.summon_followers" },
			{ "clearBounty", "feat.clear_bounties" },
			{ "invisibility", "feat.invisibility" },
			{ "powersNoCooldown", "feat.no_power_cooldown" },
			{ "freeCraft", "feat.free_crafting" },
			{ "infiniteOre", "feat.infinite_ore_veins" },
			{ "enchantAnything", "feat.enchant_anything" },
			{ "immortalFollowers", "feat.immortal_followers" },
			{ "soulGemsKeep", "feat.soul_gems_not_consumed" },
			{ "soulGemsFull", "feat.soul_gems_always_full" },
			{ "anySoulInAnyGem", "feat.any_soul_in_any_gem" },
			{ "fillSoulGems", "feat.fill_all_soul_gems" },
			{ "thievesGuildQuick", "feat.thieves_guild_one_job_instead" },
			{ "noBounty", "feat.no_bounty" },
			{ "keepItemsInJail", "feat.keep_items_in_jail" },
			{ "unmarkStolen", "feat.clear_stolen_flag" },
			{ "divorce", "feat.divorce" },
			{ "flight", "feat.flight" },
			{ "waterBreathing", "feat.waterbreathing" },
			{ "oneHitKill", "feat.one_hit_kill" },
			{ "freezeTime", "feat.stop_time" },
			{ "aimSlowmo", "feat.slow_time_while_aiming" },
			{ "merchantsBuyAll", "feat.merchants_buy_everything" },
			{ "waitHere", "feat.wait_here" },
			{ "noDragonAttacks", "feat.no_dragon_attacks" },
			{ "spawnDragon", "feat.summon_dragon" },
			{ "temperEquipped", "feat.legendary_gear" },
			{ "persuadeAlways", "feat.speech_always_succeeds" },
			{ "fastBowDraw", "feat.fast_bow_draw" },
			{ "aimbot", "feat.aimbot" },
			{ "killmoves", "feat.kill_moves_beta" },
			{ "restockMerchants", "feat.restock_merchants" },
			{ "summonMerchant", "feat.summon_merchant" },
			{ "solvePuzzle", "feat.solve_puzzle" },
			{ "autoPerks", "feat.auto_perks" },
			{ "fastTravelAnywhere", "feat.fast_travel_in_combat_and" },
			{ "fastTravelIgnoreBans", "feat.fast_travel_anywhere" },
			{ "autoUnlock", "feat.auto_unlock" },
			{ "esp", "feat.highlights_esp" },
			{ "killHostiles", "feat.kill_nearby_enemies" },
			{ "loot", "feat.loot_nearby" },
			{ "autoLoot", "feat.auto_loot" },
			{ "storage", "feat.open_storage" },
			{ "heal", "feat.full_heal" },
			{ "slowWorld", "feat.slow_time" },
			{ "tcl", "feat.no_clip" },
			{ "tfc", "feat.free_camera" },
			{ "tdetect", "feat.undetectable_by_ai" },
			{ "tm", "feat.hide_hud" },
			{ "savePoint", "feat.save_point_here" },
			{ "lastPoint", "feat.to_last_point" },
			{ "mapMarker", "feat.to_map_marker" },
			{ "stationEnchanting", "station.enchanting_table" },
			{ "stationAlchemy", "station.alchemy_lab" },
			{ "stationForge", "station.forge" },
			{ "stationGrindstone", "station.grindstone" },
			{ "stationWorkbench", "station.workbench" },
			{ "stationSmelter", "station.smelter" },
			{ "stationTanningRack", "station.tanning_rack" },
			{ "stationCookpot", "station.cooking_pot" },
			{ "stationOven", "station.oven" },
			{ "stationStaffEnchanter", "station.staff_enchanter" },
		};

		std::mutex                 g_mutex;  // guards everything below
		std::vector<std::uint32_t> g_pressed;
		std::string                g_captureTarget;
		std::optional<std::uint32_t> g_captured;
		std::atomic_bool           g_capturing{ false };

		std::string OnOff(const char* a_name, bool a_on) { return std::format("{} — {}", a_name, a_on ? Tr("common.on") : Tr("common.off")); }

		std::string Flip(bool& a_value, const char* a_name, bool a_persistent)
		{
			a_value = !a_value;
			if (a_persistent) {
				Cheats::ApplyPersistent();
			}
			return OnOff(a_name, a_value);
		}

		std::string Run(const Action& a_action)
		{
			auto&            st = Cheats::Get();
			const std::string_view id = a_action.id;
			const char*      name = Tr(a_action.name);

			if (id == "godMode") {
				const bool on = !Cheats::IsGodMode();
				Cheats::SetGodMode(on);
				return OnOff(name, on);
			}
			if (id == "infiniteHealth") return Flip(st.infiniteHealth, name, false);
			if (id == "infiniteStamina") return Flip(st.infiniteStamina, name, false);
			if (id == "infiniteMagicka") return Flip(st.infiniteMagicka, name, false);
			if (id == "noCarryLimit") return Flip(st.noCarryLimit, name, true);
			if (id == "noFallDamage") return Flip(st.noFallDamage, name, true);
			if (id == "infiniteAmmo") return Flip(st.infiniteAmmo, name, false);
			if (id == "infiniteCharge") return Flip(st.infiniteCharge, name, false);
			if (id == "noShoutCooldown") return Flip(st.noShoutCooldown, name, true);
			if (id == "weightlessMisc") {
				// The old single switch, now all consumable and misc categories of the "Невесомость" card at once.
				bool* groups[] = { &st.weightlessBooks, &st.weightlessPotions, &st.weightlessFood, &st.weightlessIngredients, &st.weightlessScrolls,
					&st.weightlessSoulGems, &st.weightlessCrafting, &st.weightlessGems, &st.weightlessMiscItems };
				const bool on = !std::ranges::all_of(groups, [](bool* a_on) { return *a_on; });
				for (auto group : groups) {
					*group = on;
				}
				Cheats::ApplyPersistent();
				return OnOff(name, on);
			}
			if (id == "easyPickpocket") return Flip(st.easyPickpocket, name, true);
			if (id == "lockpickHud") return Flip(st.lockpickHud, name, false);
			if (id == "noSpellCost") return Flip(st.noSpellCost, name, true);
			if (id == "instantCast") return Flip(st.instantCast, name, true);
			if (id == "essential") return Flip(st.essential, name, true);
			if (id == "immunity") return Flip(st.immunity, name, true);
			if (id == "nightEye") return Flip(st.nightEye, name, true);
			if (id == "waterWalking") return Flip(st.waterWalking, name, true);
			if (id == "infinitePoison") return Flip(st.infinitePoison, name, false);
			if (id == "infiniteLockpicks") return Flip(st.infiniteLockpicks, name, false);
			if (id == "infiniteTorch") return Flip(st.infiniteTorch, name, true);
			if (id == "eternalBuffs") return Flip(st.eternalBuffs, name, false);
			if (id == "invisibility") return Flip(st.invisibility, name, true);
			if (id == "powersNoCooldown") return Flip(st.powersNoCooldown, name, true);
			if (id == "freeCraft") return Flip(st.freeCraft, name, true);
			if (id == "infiniteOre") return Flip(st.infiniteOre, name, false);
			if (id == "enchantAnything") return Flip(st.enchantAnything, name, true);
			if (id == "immortalFollowers") return Flip(st.immortalFollowers, name, false);
			if (id == "soulGemsKeep") return Flip(st.soulGemsKeep, name, false);
			if (id == "soulGemsFull") return Flip(st.soulGemsFull, name, false);
			if (id == "anySoulInAnyGem") return Flip(st.anySoulInAnyGem, name, true);
			if (id == "thievesGuildQuick") return Flip(st.thievesGuildQuick, name, false);
			if (id == "noBounty") return Flip(st.noBounty, name, false);
			if (id == "keepItemsInJail") return Flip(st.keepItemsInJail, name, false);
			if (id == "divorce") return Cheats::Divorce();
			if (id == "flight") return Flip(st.flight, name, false);
			if (id == "waterBreathing") return Flip(st.waterBreathing, name, true);
			if (id == "oneHitKill") return Flip(st.oneHitKill, name, true);
			if (id == "freezeTime") return Flip(st.freezeTime, name, false);
			if (id == "aimSlowmo") return Flip(st.aimSlowmo, name, false);
			if (id == "merchantsBuyAll") return Flip(st.merchantsBuyAll, name, true);
			if (id == "noDragonAttacks") return Flip(st.noDragonAttacks, name, true);
			if (id == "spawnDragon") return Cheats::SpawnDragon();
			if (id == "temperEquipped") return Cheats::TemperEquipped();
			if (id == "persuadeAlways") return Flip(st.persuadeAlways, name, true);
			if (id == "fastBowDraw") return Flip(st.fastBowDraw, name, true);
			if (id == "aimbot") return Flip(st.aimbot, name, false);
			if (id == "killmoves") return Flip(st.killmoves, name, true);
			if (id == "waitHere") {
				Cheats::OpenWaitMenu();
				return name;
			}
			if (id == "restockMerchants") return Lang::Fmt("status.restocked_merchants", Cheats::RestockMerchants());
			if (id == "summonMerchant") return Cheats::SummonMerchant();
			if (id == "solvePuzzle") return Puzzles::SolveNearby();
			if (id == "unmarkStolen") return Lang::Fmt("status.stolen_flag_cleared", Cheats::UnmarkStolen());
			if (id == "fillSoulGems") {
				Cheats::FillSoulGems();
				return Tr("status.soul_gems_filled");
			}
			if (id == "autoPerks") return Flip(st.autoPerks, name, false);
			if (id == "fastTravelAnywhere") return Flip(st.fastTravelAnywhere, name, false);
			if (id == "fastTravelIgnoreBans") return Flip(st.fastTravelIgnoreBans, name, false);
			if (id == "autoUnlock") return Flip(st.autoUnlock, name, false);
			if (id == "autoLoot") return Flip(st.autoLoot, name, false);
			if (id == "esp") return Flip(st.esp, name, false);
			if (id == "killHostiles") {
				Cheats::KillHostiles(kKillRadius);
				return Tr("status.nearby_enemies_defeated");
			}
			if (id == "loot") {
				Cheats::LootNearby(st.lootRadius * 70.0f);
				return Tr("status.nearby_loot_collected");
			}
			if (id == "storage") {
				Cheats::OpenStorage();
				return {};
			}
			if (id == "dismissSummons") {
				Cheats::DismissSummons();
				return Tr("status.summoned_creatures_dismissed");
			}
			if (id == "followers") {
				Cheats::SummonFollowers();
				return Tr("status.followers_moved_to_the_dragonborn");
			}
			if (id == "clearBounty") {
				Cheats::ClearBounties();
				return Tr("status.bounties_cleared_in_every_hold");
			}
			if (id == "heal") {
				Cheats::HealAll();
				return Tr("status.health_magicka_and_stamina_restored");
			}
			if (id == "slowWorld") {
				const bool slow = std::abs(st.worldSpeed - 1.0f) < 0.001f;
				st.worldSpeed = slow ? kSlowWorld : 1.0f;
				Cheats::ApplyPersistent();
				return OnOff(name, slow);
			}
			if (id == "tcl") {
				Cheats::SetNoClip(!Cheats::IsNoClip());
				return OnOff(name, Cheats::IsNoClip());
			}
			if (id == "tfc") {
				const bool on = !Cheats::IsFreeCamera();
				Cheats::SetFreeCamera(on);
				return OnOff(name, on);
			}
			if (id == "tdetect") {
				Cheats::SetAiBlind(!Cheats::IsAiBlind());
				return OnOff(name, Cheats::IsAiBlind());
			}
			if (id == "tm") {
				const bool on = !Cheats::IsHudHidden();
				Cheats::SetHudHidden(on);
				return OnOff(name, on);
			}
			if (id == "savePoint") {
				Cheats::SavePoint({});
				return Tr("status.point_saved_see_teleport");
			}
			if (id.starts_with("station")) {
				constexpr std::pair<std::string_view, Cheats::Station> kIds[] = {
					{ "stationEnchanting", Cheats::Station::kEnchanting }, { "stationAlchemy", Cheats::Station::kAlchemy },
					{ "stationForge", Cheats::Station::kForge }, { "stationGrindstone", Cheats::Station::kGrindstone },
					{ "stationWorkbench", Cheats::Station::kWorkbench }, { "stationSmelter", Cheats::Station::kSmelter },
					{ "stationTanningRack", Cheats::Station::kTanningRack }, { "stationCookpot", Cheats::Station::kCookpot },
					{ "stationOven", Cheats::Station::kOven }, { "stationStaffEnchanter", Cheats::Station::kStaffEnchanter },
				};
				for (const auto& [key, station] : kIds) {
					if (id == key) {
						return Cheats::OpenStation(station) ? std::string{ name } : std::string{ Tr("status.the_station_cannot_be_opened") };
					}
				}
			}
			if (id == "mapMarker") {
				if (!Cheats::HasMapMarker()) {
					return Tr("status.place_a_custom_marker_on");
				}
				Cheats::TeleportToMapMarker();
				return Tr("status.traveling_to_the_map_marker");
			}
			if (id == "lastPoint") {
				const auto& points = Cheats::SavedPoints();
				if (points.empty()) {
					return Tr("status.no_saved_points_yet");
				}
				Cheats::TeleportToPoint(points.size() - 1);
				return Lang::Fmt("status.traveling", points.back().name);
			}
			return {};
		}

		bool CanTrigger()
		{
			auto ui = RE::UI::GetSingleton();
			auto player = RE::PlayerCharacter::GetSingleton();
			return ui && player && player->Is3DLoaded() && !ui->GameIsPaused() &&
			       !ui->IsMenuOpen(RE::Console::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) &&
			       !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		}
	}

	std::span<const Action> Actions() { return kActions; }

	std::string Trigger(std::string_view a_id)
	{
		for (const auto& action : kActions) {
			if (a_id == action.id) {
				return Run(action);
			}
		}
		throw std::runtime_error(std::format("no action {}", a_id));
	}

	std::string CommandId(std::string_view a_name) { return std::format("cmd:{}", a_name); }

	void RunCommand(const std::string& a_command)
	{
		std::size_t start = 0;
		while (start <= a_command.size()) {
			auto end = a_command.find(';', start);
			if (end == std::string::npos) {
				end = a_command.size();
			}
			auto part = a_command.substr(start, end - start);
			part.erase(0, part.find_first_not_of(' '));
			part.erase(part.find_last_not_of(' ') + 1);
			if (!part.empty()) {
				Console::Run(part);
			}
			start = end + 1;
		}
	}

	const Action* FindByName(std::string_view a_name)
	{
		for (const auto& a : kActions) {
			if (a_name == a.name) {
				return &a;
			}
		}
		return nullptr;
	}

	std::string KeyName(std::uint32_t a_dik)
	{
		if (a_dik == 0) {
			return "—";
		}
		// DirectInput codes are keyboard scan codes; codes >= 0x80 are the extended (E0) keys.
		const LONG lParam = static_cast<LONG>(((a_dik & 0x7F) << 16) | ((a_dik & 0x80) ? (1 << 24) : 0));
		wchar_t    name[64]{};
		const int  len = GetKeyNameTextW(lParam, name, static_cast<int>(std::size(name)));
		if (len <= 0) {
			return std::format("0x{:02X}", a_dik);
		}
		const int size = WideCharToMultiByte(CP_UTF8, 0, name, len, nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, name, len, result.data(), size, nullptr, nullptr);
		return result;
	}

	std::uint32_t KeyOf(std::string_view a_id)
	{
		if (a_id == kMenuKey) {
			return Settings::Get().toggleKey;
		}
		const auto& map = Settings::Get().hotkeys;
		const auto  it = map.find(std::string{ a_id });
		return it != map.end() ? it->second : 0;
	}

	void Clear(std::string_view a_id)
	{
		Settings::Get().hotkeys.erase(std::string{ a_id });
		Settings::Save();
	}

	void BeginCapture(std::string a_id)
	{
		std::scoped_lock lock{ g_mutex };
		g_captureTarget = std::move(a_id);
		g_captured.reset();
		g_capturing = true;
	}

	bool IsCapturing() { return g_capturing; }

	std::string CaptureTarget()
	{
		std::scoped_lock lock{ g_mutex };
		return g_captureTarget;
	}

	bool OnKeyDown(std::uint32_t a_dik, bool a_menuOpen)
	{
		std::scoped_lock lock{ g_mutex };
		if (g_capturing) {
			g_captured = a_dik;
			g_capturing = false;
			return true;
		}
		if (!a_menuOpen) {
			g_pressed.push_back(a_dik);
		}
		return false;
	}

	void Process()
	{
		std::vector<std::uint32_t> pressed;
		std::optional<std::uint32_t> captured;
		std::string                target;
		{
			std::scoped_lock lock{ g_mutex };
			pressed.swap(g_pressed);
			captured.swap(g_captured);
			target = g_captureTarget;
		}

		if (captured) {
			auto& s = Settings::Get();
			const auto dik = *captured;
			if (dik == kEscape) {
				Menu::SetStatus(Tr("status.hotkey_assignment_canceled"));
			} else if (dik == kBackspace || dik == kDelete) {
				if (target != kMenuKey) {
					Clear(target);
					Menu::SetStatus(Tr("status.hotkey_removed"));
				}
			} else if (target == kMenuKey) {
				s.toggleKey = dik;
				Settings::Save();
				Menu::SetStatus(Lang::Fmt("status.the_menu_now_opens_with", KeyName(dik)));
			} else if (dik == s.toggleKey) {
				Menu::SetStatus(Lang::Fmt("status.already_opens_the_menu_choose", KeyName(dik)));
			} else {
				// One key — one action.
				std::erase_if(s.hotkeys, [dik](const auto& a_pair) { return a_pair.second == dik; });
				s.hotkeys[target] = dik;
				Settings::Save();
				Menu::SetStatus(Lang::Fmt("status.assigned", KeyName(dik)));
			}
		}

		if (pressed.empty() || !CanTrigger()) {
			return;
		}
		for (const auto dik : pressed) {
			for (const auto& action : kActions) {
				if (KeyOf(action.id) == dik) {
					if (auto text = Run(action); !text.empty()) {
						Menu::Toast(std::move(text));
					}
				}
			}
			for (const auto& command : Settings::Get().commands) {
				if (KeyOf(CommandId(command.name)) == dik) {
					RunCommand(command.command);
					Menu::Toast(Lang::Fmt("status.command_executed", command.name));
				}
			}
		}
	}
}

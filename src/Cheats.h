#pragma once

namespace Cheats
{
	// Fields of save format version 3. New fields go into State below, so an old
	// record can be copied straight into the front of the new one.
	struct StateV3
	{
		bool  infiniteHealth{ false };
		bool  infiniteStamina{ false };
		bool  infiniteMagicka{ false };
		bool  noShoutCooldown{ false };
		bool  noCarryLimit{ false };
		float speedMult{ 100.0f };
		float jumpMult{ 1.0f };

		bool  noFallDamage{ false };
		bool  easyPickpocket{ false };
		bool  infiniteAmmo{ false };
		bool  infiniteCharge{ false };
		bool  weightlessMisc{ false };
		float damageDealtMult{ 1.0f };
		float damageTakenMult{ 1.0f };
		float skillXpMult{ 1.0f };
		float worldSpeed{ 1.0f };
		bool  lockpickHud{ true };
	};

	// Save format version 4.
	struct StateV4 : StateV3
	{
		bool  noSpellCost{ false };
		bool  instantCast{ false };
		bool  infinitePoison{ false };
		bool  immunity{ false };  // diseases and paralysis
		bool  essential{ false };
		bool  nightEye{ false };
		bool  waterWalking{ false };
		bool  infiniteTorch{ false };
		bool  eternalBuffs{ false };
		bool  infiniteLockpicks{ false };
		bool  unlimitedTraining{ false };
		float attackSpeed{ 1.0f };
		float smithingBoost{ 0.0f };  // percent, like Fortify Smithing
		float alchemyBoost{ 0.0f };
		float enchantingBoost{ 0.0f };
		float perksPerLevel{ 1.0f };
	};

	// Multipliers for the player's own spells of one school (1 = as in the game).
	struct School
	{
		float power{ 1.0f };  // magnitude: damage, healing, ward / armour points, highest level affected...
		float duration{ 1.0f };
		float cost{ 1.0f };
	};

	// From version 5 on the state is stored as raw bytes and read up to the stored length,
	// so fields appended here simply keep their defaults on older saves.
	struct State : StateV4
	{
		bool  invisibility{ false };
		bool  powersNoCooldown{ false };
		bool  freeCraft{ false };     // no materials
		bool  craftNoPerks{ false };  // no perk / other conditions
		bool  autoUnlock{ false };
		bool  fairTrade{ false };
		bool  richMerchants{ false };
		bool  esp{ false };
		float espRange{ 60.0f };  // meters
		bool  espEnemies{ true };
		bool  espNpcs{ true };
		bool  espFollowers{ true };
		bool  espCorpses{ true };
		bool  espContainers{ true };
		bool  espItems{ false };
		bool  espFlora{ false };
		bool  espInteract{ true };  // locked / load doors, levers and the like, crafting stations
		bool  espTraps{ true };
		bool  timelessTravel{ false };    // fast travel takes no game time
		bool  playerNormalSpeed{ false };  // the player is not slowed by "world speed"
		bool  whBoxes{ true };             // wallhack drawing for creatures (needs esp)
		bool  whSkeleton{ false };
		bool  whHealth{ true };
		bool  whSnaplines{ false };
		bool  whInfo{ true };
		bool  whGlow{ false };  // the game's Detect Life shader on every creature
		bool  infiniteOre{ false };  // ore veins never run out
		bool  autoLoot{ false };     // the loot below is gathered by itself every second
		float lootRadius{ 25.0f };   // meters, for the loot button, its hotkey and the autoloot
		School alteration, conjuration, destruction, illusion, restoration;
		float  summonExtra{ 0.0f };            // more summons / raised dead at once
		float  incomingMagic{ 1.0f };          // hostile spells on the player: magnitude
		float  incomingMagicDuration{ 1.0f };  // ...and duration
		float  dualCastPower{ 1.0f };          // on top of the game's dual-cast bonus
		float  dualCastCost{ 1.0f };
		float  magickaRegen{ 1.0f };
		float  healthRegen{ 1.0f };
		float  staminaRegen{ 1.0f };
		// Weightless item categories (weightlessMisc above is the old all-consumables switch, migrated on load).
		bool  weightlessBooks{ false };
		bool  weightlessPotions{ false };
		bool  weightlessFood{ false };
		bool  weightlessIngredients{ false };
		bool  weightlessScrolls{ false };
		bool  weightlessSoulGems{ false };
		bool  weightlessCrafting{ false };  // ingots, ore, hides, leather, firewood, animal parts
		bool  weightlessGems{ false };
		bool  weightlessMiscItems{ false };  // other "misc" clutter and keys
		bool  weightlessAmmo{ false };
		bool  weightlessWeapons{ false };
		bool  weightlessArmor{ false };  // armour, clothes and jewellery
		bool  fastTravelAnywhere{ false };    // in combat, overencumbered, taking damage, falling
		bool  fastTravelIgnoreBans{ false };  // also where places / quests forbid it
		bool  autoPerks{ false };        // skill-tree perks are added for free once their requirements are met
		float enchantEffects{ 1.0f };    // effects per enchanted item (the game's count is kept when higher)
		bool  enchantAnything{ false };
		bool  immortalFollowers{ false };  // followers and the player's horse can't die and heal at once
		bool  soulGemsKeep{ false };     // a used soul gem comes back empty
		bool  soulGemsFull{ false };     // ...or comes back filled
		bool  anySoulInAnyGem{ false };  // every gem holds grand and black souls
		bool  thievesGuildQuick{ false };  // one job per city instead of five for the guild's influence
		bool  noBounty{ false };           // bounties are cleared as soon as they appear
		bool  keepItemsInJail{ false };    // what the guards take on arrest comes straight back
		bool  flight{ false };             // no gravity; WASD where the camera looks, Space up, X down
		float flightSpeed{ 800.0f };       // units per second
		bool  waterBreathing{ false };
		bool  oneHitKill{ false };       // the player's damage ×10 000
		bool  freezeTime{ false };       // the clock stands still
		bool  aimSlowmo{ false };        // slow motion while a bow is drawn
		bool  merchantsBuyAll{ false };  // every merchant buys every kind of item
		bool  noDragonAttacks{ false };    // no random dragons in the wild
		float dragonsGlobalBefore{ -1.0f };  // MQ106TurnOffRandomDragons before that (-1 = not changed)
		bool  fastBowDraw{ false };        // any release of a bow is a full-power, full-speed shot
		float skillCap{ 100.0f };          // skills keep leveling up to this (100 = as in the game)
		bool  aimbot{ false };             // the player's arrows and bolts fly at the actor nearest the crosshair
		bool  killmoves{ false };          // the killmove settings below are in effect
		float killmoveChance{ 100.0f };    // percent of the player's killing melee blows that become killmoves
		bool  killmoveAnyEnemy{ true };    // not only on the last enemy
		float decapChance{ 40.0f };        // percent of the player's killmoves that decapitate (the game's DecapitationChance is 40)
		bool  decapNoPerk{ false };        // decapitations without Savage Strike / Devastating Blow
		bool  noKillmovesOnPlayer{ false };  // enemies cannot killmove the player
		bool  persuadeAlways{ false };       // persuade / intimidate / bribe checks in dialogue always pass
		float speechBefore[5]{ -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };  // SpeechVeryEasy..VeryHard before that (-1 = not changed)
	};

	State& Get();

	// Every State field by name. Used by the co-save (saved as name/value pairs, so fields
	// can be added, reordered or removed without breaking saves) and by the test bridge.
	struct Field
	{
		const char* name;
		bool*       flag;
		float*      number;
	};
	std::vector<Field> Fields(State& a_state);
	std::vector<Field> Fields();  // of the live state

	// Keeps toggles and the original base values in the SKSE co-save of each save game.
	void InstallSerialization();

	// Game event sinks (level-ups); after kDataLoaded.
	void InstallEvents();

	// Called once per frame; keeps the "infinite" toggles applied.
	void Update();

	// Re-applies every persistent setting (after a toggle or slider change, or after loading).
	void ApplyPersistent();

	bool IsGodMode();
	void SetGodMode(bool a_enable);

	void AddGold(int a_count);
	void AddLevels(int a_count);
	void AddPerkPoints(int a_count);
	void AddDragonSouls(int a_count);
	void MaxAllSkills();
	void HealAll();

	// Gives an item / teaches a spell or shout / adds a perk / spawns an NPC, by catalog form ID.
	void Grant(RE::FormID a_formID, int a_count);

	enum class Kit
	{
		kSmithing,     // ingots, ores, hides, leather, dragon parts
		kIngredients,  // every alchemy ingredient
		kSoulGems,     // filled soul gems
		kLockpicks
	};
	void GiveKit(Kit a_kit, int a_count);

	void TeleportTo(RE::ObjectRefHandle a_marker);

	// The player's own marker on the world map: false when none is placed. The jump lands on the ground there.
	// Crafting stations the menu opens anywhere (order matches kStationKinds in Cheats.cpp).
	enum class Station
	{
		kEnchanting,
		kAlchemy,
		kForge,
		kGrindstone,
		kWorkbench,
		kSmelter,
		kTanningRack,
		kCookpot,
		kOven,
		kStaffEnchanter,
	};
	bool HasStation(Station a_station);
	void FillSoulGems();
	std::string OpenGuildCityJobs();
	std::string BecomeThaneEverywhere();
	RE::Actor*  Spouse();  // null when not married
	std::string MarryTarget(RE::ObjectRefHandle a_target);
	std::string Divorce();
	// Character profiles: appearance, race, level, skills, perks, spells, shouts and items to a file and back.
	struct ProfileInfo
	{
		std::string name;
		std::string character;  // in the game's encoding, like GetName()
		std::string race;       // same
		int         level{ 0 };
		int         perks{ 0 };
		std::string saved;      // "23.10.2026 12:19"
	};
	std::vector<ProfileInfo> ProfileInfos();
	std::vector<std::string> ProfileNames();
	std::string              SaveProfile(const std::string& a_name);
	std::string              LoadProfile(const std::string& a_name, bool a_withItems);
	bool                     DeleteProfile(const std::string& a_name);

	// Current state for the menu rows.
	std::pair<int, int> ThaneProgress();  // holds where the player is thane, holds there are
	bool                InThievesGuild();
	int                 StolenCount();
	int                 EmptySoulGemCount();

	bool        CureVampirism();
	bool        BecomeVampire();
	bool        CureLycanthropy();
	int         RestockMerchants();  // merchant chests refilled; returns how many
	std::string SummonMerchant();    // Dragonborn's Dremora merchant comes over: buys anything, 30 000 gold; returns a status line
	void        OpenWaitMenu();
	std::string ResetCurrentDungeon();
	std::string SpawnDragon();  // a hostile dragon of the player's level flies in; returns a status line
	std::string TemperEquipped();  // worn weapons and armour become Legendary; returns a status line
	float       PlayerScale();
	void        SetPlayerScale(float a_scale);
	void        OpenRaceMenuKeepingSkills();  // showracemenu, skills put back when it closes

	void        SetFlightTestMove(std::optional<RE::NiPoint2> a_move);  // test bridge: fake WASD input for flight  // Thieves Guild special jobs in all four cities; returns a status line  // the player's empty gems become full ones  // false when no loaded plugin has one (Hearthfire oven, Dragonborn staff table)
	bool OpenStation(Station a_station);

	bool HasMapMarker();
	void TeleportToMapMarker();

	// Player-made teleport points: an invisible XMarker left at the spot, so the game
	// itself keeps it in the save and handles cell loading on the way back.
	struct SavedPoint
	{
		RE::FormID  marker;
		std::string name;
		std::string place;  // cell / location name at the time it was saved
	};
	const std::vector<SavedPoint>& SavedPoints();
	void                           SavePoint(std::string a_name);
	void                           TeleportToPoint(std::size_t a_index);
	void                           DeletePoint(std::size_t a_index);

	// Console toggles shown as switches. Free camera and hidden HUD are read from the game;
	// collision and AI detection have no readable state, so what the menu set is remembered.
	bool IsNoClip();
	void SetNoClip(bool a_on);
	bool IsFreeCamera();
	void SetFreeCamera(bool a_on);
	bool IsAiBlind();
	void SetAiBlind(bool a_on);
	bool IsHudHidden();
	void SetHudHidden(bool a_on);

	void KillHostiles(float a_radius);
	// Takes what is lying around within a_radius: loose items, plants, dead bodies, unlocked containers nobody owns
	// (see Loot in Cheats.cpp for what is left alone). Returns at once, the looting runs on the game thread.
	void LootNearby(float a_radius);
	void GiveBeastForm();
	bool GiveVampireLord();  // false without Dawnguard
	void OpenStorage();

	// Finds a loaded reference of an NPC base and moves the player to it or it to the player.
	bool MoveToNpc(RE::FormID a_npc, bool a_bringToPlayer);

	struct QuestInfo
	{
		RE::FormID    formID;
		std::string   name;
		std::string   type;
		std::uint16_t stage;
	};
	std::vector<QuestInfo> RunningQuests();
	bool                   NextQuestStage(RE::FormID a_quest);  // false when no later stage
	void                   CompleteQuest(RE::FormID a_quest);

	void DismissSummons();
	void SummonFollowers();
	void ClearBounties();
	int  UnmarkStolen();  // the player's stolen items become their own; returns how many
	void FlingTarget(RE::ObjectRefHandle a_target, float a_force);
	void LearnAllEnchantments();
	void LearnAllIngredients();
	void LearnAllSpells();  // every spell that has a spell tome, mods included
	void LearnAllShouts();  // every named shout with all of its words, unlocked
	void LearnAllPerks();   // every perk (all ranks) in the 18 skill trees
	// Takes back what the three above and auto perks gave (remembered per save).
	std::size_t LearnedCount();
	void        ForgetLearned();

	// Standing stones: index into StandingStones(), the current one is -1 if none.
	struct Stone
	{
		RE::FormID  spell;
		const char* name;
	};
	std::span<const Stone> StandingStones();
	int                    CurrentStone();
	void                   SetStone(int a_index);

	// Character editor.
	void SetBaseValue(RE::ActorValue a_av, float a_value);
	void SetLevel(int a_level);

	void SetHour(float a_hour);
	void SetTimescale(float a_scale);
	void ForceWeather(RE::FormID a_weather);
	void ReleaseWeather();
}

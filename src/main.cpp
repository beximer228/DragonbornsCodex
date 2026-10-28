#include "Catalog.h"
#include "Cheats.h"
#include "Archery.h"
#include "Enchanting.h"
#include "FastTravel.h"
#include "Input.h"
#include "Magic.h"
#include "Overlay.h"
#include "Settings.h"
#include "SkillCap.h"
#include "Enchanter.h"
#include "Health.h"

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			Lang::Load();
			Catalog::BuildForms();
			Overlay::Install();
			Input::Install();
			Cheats::InstallEvents();
			Magic::InstallPlayerHooks();
			Health::InstallPlayerHooks();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Input::SetMenuOpen(false);
			Catalog::BuildMarkers();
			Cheats::ApplyPersistent();
			Enchanter::RefreshItems();  // the old snapshot points into the previous game's inventory
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .logName = "DragonbornsCodex", .trampoline = true, .trampolineSize = 2048 });
	logger::info("Dragonborn's Codex {} loading", SKSE::GetPluginVersion().string());

	Settings::Load();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	Cheats::InstallSerialization();
	Magic::Install();
	FastTravel::Install();
	Enchanting::Install();
	Archery::Install();
	SkillCap::Install();
	return true;
}

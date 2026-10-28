#pragma once

namespace Magic
{
	// Hooks the perk entry points behind spell magnitude, duration and cost (call once at plugin load).
	void Install();
	// Hooks what needs the player object (call at kDataLoaded).
	void InstallPlayerHooks();
	bool IsInstalled();
}

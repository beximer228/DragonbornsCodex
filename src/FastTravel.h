#pragma once

namespace FastTravel
{
	// Hooks the game's "can the player fast travel now" checks (call once at plugin load).
	void Install();
	bool IsInstalled();

	// The game's own check, as the map asks it; a_quiet = no message box.
	bool CanFastTravel(bool a_quiet);
}

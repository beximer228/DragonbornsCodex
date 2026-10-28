#pragma once

namespace LockpickHud
{
	// True while the lockpicking minigame is open and the hint is enabled.
	bool IsActive();

	// Draws the hint arc over the lock: where the sweet spot is and where the pick points now.
	void Draw();
}

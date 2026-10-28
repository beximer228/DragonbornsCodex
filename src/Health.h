#pragma once

namespace Health
{
	// Infinite health also stops damage itself, so a blow, trap or fall that would kill in one go can't (call after the data has loaded).
	void InstallPlayerHooks();
}

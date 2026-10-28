#pragma once

namespace Archery
{
	// Hooks the player's bow draw power (call once at plugin load).
	void Install();
	bool IsInstalled();

	// Every frame: keeps the player's arrows in flight on a straight line (fast draw) or steers them at a target (aimbot).
	void Update();

	// Test hook: JSON with the actor's position, height, aim point, model bounds and head-like node names.
	std::string DebugAim(RE::Actor* a_actor);
}

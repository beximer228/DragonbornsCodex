#pragma once

namespace SkillCap
{
	// Points the skill-experience code at our own cap instead of the built-in 100 (call once at plugin load).
	void Install();
	bool IsInstalled();

	// Takes the cap from the menu (100 = as in the game) and gives skills stuck at the old cap a next-level threshold.
	void Apply();
}

#pragma once

namespace Enchanting
{
	// Hooks the enchanting table's "how many effects per item" entry point (call once at plugin load).
	void Install();
	bool IsInstalled();
}

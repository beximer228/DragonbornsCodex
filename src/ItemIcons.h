#pragma once

#include <imgui.h>

// Item / spell / shout icons for the menu's lists: SkyUI's item icons (psychosteve), packed by tools/make_item_icons.py
// into one white atlas, icons/item_atlas.dds, and picked per form the way SkyUI's inventory picks them (weapon type,
// armor slot and weight class, a potion's main effect, a spell's school or element...). Render thread only.
// The atlas stays out of the public release (SkyUI's icons need their authors' leave): without it, no icons.
namespace ItemIcons
{
	// Draws a_form's icon in the square at a_min (a_size wide), tinted a_tint unless the icon has its own colour
	// (potions, spell schools and elements, gold). False, and nothing drawn, for forms without one (NPCs) or without
	// the atlas.
	bool Draw(ImDrawList* a_dl, RE::FormID a_form, ImVec2 a_min, float a_size, ImU32 a_tint);
}

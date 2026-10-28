#pragma once

struct ImDrawList;
struct ImVec2;

namespace Esp
{
	// True when creature highlighting is on and the world is visible (no paused menu).
	bool IsActive();

	// Marks every living creature nearby with its name and distance, through walls.
	void Draw();

	// One mark shape (0 diamond, 1 square, 2 circle, 3 triangle, 4 star) of radius a_r; a_warn = red outline.
	// The menu uses it for the ESP look previews.
	void DrawShape(ImDrawList* a_dl, ImVec2 a_p, float a_r, unsigned int a_color, int a_shape, bool a_warn = false);

	// Things nobody goes looking for (plates, cups, food, worthless items): neither marked nor looted.
	bool IsClutter(RE::TESBoundObject* a_base);

	// Test hook: current marks as "formID|kind|label".
	std::vector<std::string> DebugMarks();
}

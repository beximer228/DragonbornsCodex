#pragma once

namespace Menu
{
	void ApplyStyle();
	void OnOpen();
	void Draw();

	// Test hooks: switch page by id (home, dovahkiin, combat, magic, progress, world,
	// teleport, quests, target, console, items, actors, service) or type into the search.
	bool OpenPage(std::string_view a_id);

	// The next / previous sidebar page (gamepad shoulder buttons).
	void CyclePage(int a_step);
	void SetSearch(std::string a_text);
	void ScrollTo(float a_y);

	// Test hook: screen rect of the control drawn last frame for a row / button label.
	std::optional<std::array<float, 4>> FindControl(std::string_view a_label);
	std::vector<std::string>            VisibleControls();  // content scroll of the current page; negative = bottom

	// Short message in the footer (inside the menu).
	void SetStatus(std::string a_text);

	// Pop-up notice drawn even with the menu closed (hotkey feedback).
	void Toast(std::string a_text);
	bool HasToast();
	void DrawToast();
}

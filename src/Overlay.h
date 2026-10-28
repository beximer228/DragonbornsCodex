#pragma once

struct ImFont;

namespace Overlay
{
	struct Fonts
	{
		ImFont* body{ nullptr };
		ImFont* caption{ nullptr };  // hints, sub-headers
		ImFont* heading{ nullptr };  // section titles
		ImFont* title{ nullptr };    // page title / logo
	};

	// Hooks IDXGISwapChain::Present of the game's swap chain and sets up ImGui.
	// Must run after the renderer exists (kDataLoaded is fine).
	void Install();

	bool         IsReady();

	// A white texture with the given 8-bit alpha (tint it when drawing), as an ImGui texture id; 0 if it failed.
	unsigned long long CreateAlphaTexture(const unsigned char* a_alpha, int a_width, int a_height);
	const Fonts& GetFonts();
	float        Scale();      // resolution scale x the user's "menu size" setting
	float        UserScale();  // just the user's setting

	// ImGui IO is fed from the input thread and consumed on the render thread.
	[[nodiscard]] std::unique_lock<std::recursive_mutex> Lock();

	// True once text has arrived through WM_CHAR; then the game's CharEvents are ignored
	// so characters are not typed twice.
	bool HasNativeText();

	// Active keyboard layout of the game window, e.g. "RU" or "EN".
	std::string KeyboardLanguage();

	// Switches to the next keyboard layout (Alt+Shift / Ctrl+Shift inside the menu, or a click
	// on the indicator). The game swallows the Windows hotkey, so the menu does it itself.
	void RequestNextKeyboardLayout();

	void MoveCursor(float a_dx, float a_dy);
	void SetCursor(float a_x, float a_y);  // test hook, screen pixels
	void OnMenuOpened();
	void OnMenuClosed();
}

#pragma once

namespace Hotkeys
{
	// Something a key can be bound to. `name` matches the row label in the menu, so a
	// right click on that label can bind it.
	struct Action
	{
		const char* id;
		const char* name;
	};

	std::span<const Action> Actions();
	const Action*           FindByName(std::string_view a_name);

	// Runs an action by id as if its hotkey was pressed; returns the notice text.
	std::string Trigger(std::string_view a_id);

	// Hotkey id of a user command (Settings::Command).
	std::string CommandId(std::string_view a_name);

	// Runs a user command: each ';'-separated part as its own console line.
	void RunCommand(const std::string& a_command);

	// Special capture target: the key that opens the menu.
	inline constexpr std::string_view kMenuKey = "menu";

	// Human-readable key name for a DirectInput scan code ("F5", "Num 1", "Insert").
	std::string KeyName(std::uint32_t a_dik);

	// Bound key of an action, or 0.
	std::uint32_t KeyOf(std::string_view a_id);
	void          Clear(std::string_view a_id);

	// Waits for the next key press and binds it to a_id (Esc cancels, Backspace unbinds).
	void        BeginCapture(std::string a_id);
	bool        IsCapturing();
	std::string CaptureTarget();

	// Input thread: a keyboard key went down. Returns true if it was eaten by a capture.
	bool OnKeyDown(std::uint32_t a_dik, bool a_menuOpen);

	// Render thread, every frame: binds captured keys and runs triggered actions.
	void Process();
}

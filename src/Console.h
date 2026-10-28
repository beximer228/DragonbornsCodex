#pragma once

namespace Console
{
	// Runs a console command on the game thread, optionally against a reference
	// (same as selecting it in the console first).
	void Run(std::string a_command, RE::ObjectRefHandle a_target = {});

	// Like Run, then hands what the command printed into the console to a_done
	// (on the game thread).
	void RunCaptured(std::string a_command, RE::ObjectRefHandle a_target, std::function<void(std::string)> a_done);

	// Prints a line into the in-game console log.
	void Print(std::string_view a_text);
}

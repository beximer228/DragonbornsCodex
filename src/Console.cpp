#include "Console.h"

namespace Console
{
	void Run(std::string a_command, RE::ObjectRefHandle a_target)
	{
		SKSE::GetTaskInterface()->AddTask([command = std::move(a_command), a_target]() {
			auto factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			auto script = factory ? factory->Create() : nullptr;
			if (!script) {
				logger::error("Failed to create console script for '{}'", command);
				return;
			}
			auto target = a_target.get();
			script->SetCommand(command);
			script->CompileAndRun(target.get());
			delete script;
			logger::info("console: {}", command);
		});
	}

	void RunCaptured(std::string a_command, RE::ObjectRefHandle a_target, std::function<void(std::string)> a_done)
	{
		SKSE::GetTaskInterface()->AddTask([command = std::move(a_command), a_target, done = std::move(a_done)]() {
			// What the command printed = what was added to the log.
			auto log = RE::ConsoleLog::GetSingleton();
			if (log && std::string_view{ log->buffer.c_str() }.size() > 48000) {
				log->buffer = "";  // a full log (64 KB) takes no more lines at all
			}
			const std::string before = log ? std::string{ log->buffer.c_str() } : std::string{};
			auto        factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			auto        script = factory ? factory->Create() : nullptr;
			std::string output;
			if (script) {
				auto target = a_target.get();
				script->SetCommand(command);
				script->CompileAndRun(target.get());
				delete script;
				logger::info("console: {}", command);
			}
			if (log) {
				const std::string_view all{ log->buffer.c_str() };
				output = all.starts_with(before) ? std::string{ all.substr(before.size()) } : std::string{ all };
			}
			done(std::move(output));
		});
	}

	void Print(std::string_view a_text)
	{
		if (auto log = RE::ConsoleLog::GetSingleton()) {
			log->Print("%.*s", static_cast<int>(a_text.size()), a_text.data());
		}
	}
}

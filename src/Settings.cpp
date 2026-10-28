#include "Settings.h"

namespace Settings
{
	namespace
	{
		constexpr std::size_t kRecentLimit = 40;

		Data g_data;

		std::optional<std::filesystem::path> FilePath()
		{
			auto dir = SKSE::log::log_directory();
			if (!dir) {
				return std::nullopt;
			}
			// the menu was called Cheat Menu before: its settings come along once
			const auto path = *dir / "DragonbornsCodex.ini";
			std::error_code ec;
			if (const auto old = *dir / "CheatMenu.ini"; !std::filesystem::exists(path, ec) && std::filesystem::exists(old, ec)) {
				std::filesystem::copy_file(old, path, ec);
			}
			return path;
		}
	}

	Data& Get() { return g_data; }

	void Load()
	{
		const auto path = FilePath();
		if (!path) {
			return;
		}
		std::ifstream file{ *path };
		if (!file) {
			logger::info("Settings: no {}, using defaults", path->string());
			return;
		}
		std::string line;
		while (std::getline(file, line)) {
			const auto eq = line.find('=');
			if (eq == std::string::npos || line.starts_with('#')) {
				continue;
			}
			const auto key = std::string_view{ line }.substr(0, eq);
			const auto value = line.substr(eq + 1);
			try {
				if (key == "dark") {
					g_data.dark = value == "1";
				} else if (key == "compact") {
					g_data.compact = value == "1";
				} else if (key == "scale") {
					g_data.uiScale = std::clamp(std::stof(value), 0.6f, 1.6f);
				} else if (key == "window") {
					float x, y, w, h;
					if (std::sscanf(value.c_str(), "%f,%f,%f,%f", &x, &y, &w, &h) == 4) {
						g_data.window = { x, y, w, h };
					}
				} else if (key == "toggleKey") {
					g_data.toggleKey = static_cast<std::uint32_t>(std::stoul(value));
				} else if (key.starts_with("hotkey.")) {
					g_data.hotkeys[std::string{ key.substr(7) }] = static_cast<std::uint32_t>(std::stoul(value));
				} else if (key == "fontBody") {
					g_data.fontBody = value;
				} else if (key == "fontHeading") {
					g_data.fontHeading = value;
				} else if (key == "fontTitle") {
					g_data.fontTitle = value;
				} else if (key == "fontBodySize") {
					g_data.fontBodySize = std::clamp(std::stof(value), 10.0f, 40.0f);
				} else if (key == "fontHeadingSize") {
					g_data.fontHeadingSize = std::clamp(std::stof(value), 10.0f, 60.0f);
				} else if (key == "fontTitleSize") {
					g_data.fontTitleSize = std::clamp(std::stof(value), 10.0f, 80.0f);
				} else if (key == "language") {
					g_data.language = value;
				} else if (key == "espIconScale") {
					g_data.esp.iconScale = std::clamp(std::stof(value), 0.5f, 3.0f);
				} else if (key == "espTextScale") {
					g_data.esp.textScale = std::clamp(std::stof(value), 0.5f, 2.5f);
				} else if (key == "espOpacity") {
					g_data.esp.opacity = std::clamp(std::stof(value), 0.2f, 1.0f);
				} else if (key == "espLineScale") {
					g_data.esp.lineScale = std::clamp(std::stof(value), 0.5f, 3.0f);
				} else if (key == "espNames") {
					g_data.esp.names = value == "1";
				} else if (key == "espDistance") {
					g_data.esp.distance = value == "1";
				} else if (key.starts_with("espColor.") || key.starts_with("espShape.")) {
					const auto id = key.substr(9);
					for (std::size_t i = 0; i < kEspKinds; ++i) {
						if (id == kEspKindIds[i]) {
							if (key[3] == 'C') {
								g_data.esp.colors[i] = static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
							} else {
								g_data.esp.shapes[i] = std::clamp(std::stoi(value), 0, 4);
							}
						}
					}
				} else if (key == "testBridge") {
					g_data.testBridge = value == "1";
				} else if (key == "favorite") {
					g_data.favorites.insert(value);
				} else if (key == "command") {
					const auto bar = value.find('|');
					if (bar != std::string::npos) {
						g_data.commands.push_back({ value.substr(0, bar), value.substr(bar + 1) });
					}
				} else if (key == "kit") {
					// kit=Name|Key*count;Key*count
					const auto bar = value.find('|');
					if (bar == std::string::npos) {
						continue;
					}
					Kit kit{ value.substr(0, bar), {} };
					std::stringstream items{ value.substr(bar + 1) };
					for (std::string item; std::getline(items, item, ';');) {
						if (const auto star = item.rfind('*'); star != std::string::npos) {
							kit.items.emplace_back(item.substr(0, star), std::stoi(item.substr(star + 1)));
						}
					}
					g_data.kits.push_back(std::move(kit));
				} else if (key == "recent" && g_data.recent.size() < kRecentLimit) {
					g_data.recent.push_back(value);
				}
			} catch (const std::exception&) {
				logger::warn("Settings: bad line '{}'", line);
			}
		}
		logger::info("Settings: loaded ({} hotkeys, {} favorites)", g_data.hotkeys.size(), g_data.favorites.size());
	}

	void Save()
	{
		const auto path = FilePath();
		if (!path) {
			return;
		}
		std::ofstream file{ *path, std::ios::trunc };
		if (!file) {
			logger::error("Settings: cannot write {}", path->string());
			return;
		}
		file << "# Dragonborn's Codex settings (written by the menu)\n";
		file << "dark=" << (g_data.dark ? 1 : 0) << '\n';
		file << "compact=" << (g_data.compact ? 1 : 0) << '\n';
		file << std::format("scale={:.2f}\n", g_data.uiScale);
		if (g_data.window[2] > 0.0f) {
			file << std::format("window={:.4f},{:.4f},{:.4f},{:.4f}\n", g_data.window[0], g_data.window[1], g_data.window[2], g_data.window[3]);
		}
		file << "toggleKey=" << g_data.toggleKey << '\n';
		if (!g_data.language.empty()) {
			file << "language=" << g_data.language << '\n';
		}
		file << "# fonts: a path relative to Data/SKSE/Plugins/CheatMenu or an absolute one, size in pixels at 1080p; restart the game after a change\n";
		file << "fontBody=" << g_data.fontBody << '\n';
		file << "fontHeading=" << g_data.fontHeading << '\n';
		file << "fontTitle=" << g_data.fontTitle << '\n';
		file << std::format("fontBodySize={:.0f}\nfontHeadingSize={:.0f}\nfontTitleSize={:.0f}\n", g_data.fontBodySize, g_data.fontHeadingSize, g_data.fontTitleSize);
		if (g_data.testBridge) {
			file << "testBridge=1\n";
		}
		const auto& esp = g_data.esp;
		file << std::format("espIconScale={:.2f}\nespTextScale={:.2f}\nespOpacity={:.2f}\nespLineScale={:.2f}\nespNames={}\nespDistance={}\n", esp.iconScale,
			esp.textScale, esp.opacity, esp.lineScale, esp.names ? 1 : 0, esp.distance ? 1 : 0);
		file << "# ESP colours as ImGui AABBGGRR hex, shapes 0 diamond, 1 square, 2 circle, 3 triangle, 4 star\n";
		for (std::size_t i = 0; i < kEspKinds; ++i) {
			file << std::format("espColor.{}={:08X}\nespShape.{}={}\n", kEspKindIds[i], esp.colors[i], kEspKindIds[i], esp.shapes[i]);
		}
		for (const auto& [id, dik] : g_data.hotkeys) {
			file << "hotkey." << id << '=' << dik << '\n';
		}
		for (const auto& fav : g_data.favorites) {
			file << "favorite=" << fav << '\n';
		}
		for (const auto& c : g_data.commands) {
			file << "command=" << c.name << '|' << c.command << '\n';
		}
		for (const auto& kit : g_data.kits) {
			file << "kit=" << kit.name << '|';
			for (std::size_t i = 0; i < kit.items.size(); ++i) {
				file << (i ? ";" : "") << kit.items[i].first << '*' << kit.items[i].second;
			}
			file << '\n';
		}
		for (const auto& r : g_data.recent) {
			file << "recent=" << r << '\n';
		}
	}

	std::string Key(std::string_view a_plugin, RE::FormID a_formID)
	{
		const bool light = (a_formID >> 24) == 0xFE;
		return std::format("{}:{:X}", a_plugin, a_formID & (light ? 0xFFF : 0xFFFFFF));
	}

	void AddRecent(std::string a_key)
	{
		std::erase(g_data.recent, a_key);
		g_data.recent.insert(g_data.recent.begin(), std::move(a_key));
		if (g_data.recent.size() > kRecentLimit) {
			g_data.recent.resize(kRecentLimit);
		}
		Save();
	}
}

#include "Menu.h"

#include "Catalog.h"
#include "Cheats.h"
#include "Puzzles.h"
#include "Console.h"
#include "Enchanter.h"
#include "Esp.h"
#include "Hotkeys.h"
#include "Input.h"
#include "Magic.h"
#include "Overlay.h"
#include "Settings.h"
#include "LogoData.h"
#include "Text.h"
#include "ItemIcons.h"
#include "Textures.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

namespace Menu
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// ---------------------------------------------------------------- theme

		struct Theme
		{
			ImU32 side, sideText, sideActive, gold;
			ImU32 bg, card, text, muted, title;
			ImU32 accent, onAccent, line, seg, off, ok, hover;
		};
		static_assert(sizeof(Theme) == 16 * sizeof(ImU32));

		constexpr ImU32 Hex(std::uint32_t a_rgb, std::uint8_t a_alpha = 255)
		{
			return IM_COL32((a_rgb >> 16) & 0xFF, (a_rgb >> 8) & 0xFF, a_rgb & 0xFF, a_alpha);
		}

		// Dark: the vanilla menus (inventory, journal, skills) — near-black translucent panels over the game,
		// bone-white text and thin white rules. Light: parchment, like the world map, books and notes,
		// with brown ink and a wax-red accent.
		constexpr Theme kLight{
			Hex(0xD6C39B, 250), Hex(0x4A3520), Hex(0x5A3A1A, 34), Hex(0x6B4A26),
			Hex(0xE6D7B3, 250), Hex(0xEFE3C6, 150), Hex(0x2E2116), Hex(0x6A5640), Hex(0x22170E),
			Hex(0x7A2418), Hex(0xF6EBD3), Hex(0x5A4128, 80), Hex(0x5A4128, 26), Hex(0xB9A684), Hex(0x3F6B34), Hex(0x5A4128, 24)
		};
		constexpr Theme kDark{
			Hex(0x000000, 214), Hex(0xB9B6AE), Hex(0xFFFFFF, 20), Hex(0xE4DFD1),
			Hex(0x060606, 196), Hex(0x000000, 80), Hex(0xE8E6E1), Hex(0x8F8C85), Hex(0xFFFFFF),
			Hex(0xDCD6C6), Hex(0x111111), Hex(0xFFFFFF, 46), Hex(0xFFFFFF, 16), Hex(0x4A4946), Hex(0x9BC48E), Hex(0xFFFFFF, 18)
		};

		constexpr ImU32 kHealthCol = Hex(0xB8352B);
		constexpr ImU32 kMagickaCol = Hex(0x2F6DB5);
		constexpr ImU32 kStaminaCol = Hex(0x3E8E41);

		// Colors of the current frame: the theme with the fade-in alpha already applied,
		// because most of the menu is drawn straight into draw lists and ignores style.Alpha.
		Theme g_cur = kLight;
		float g_alpha = 1.0f;

		const Theme& Base() { return Settings::Get().dark ? kDark : kLight; }
		const Theme& T() { return g_cur; }
		float        S(float a_px) { return a_px * Overlay::Scale(); }
		bool         Compact() { return Settings::Get().compact; }
		float        D(float a_normal, float a_compact) { return S(Compact() ? a_compact : a_normal); }  // spacing by density
		ImVec4       V(ImU32 a_col) { return ImGui::ColorConvertU32ToFloat4(a_col); }

		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const auto a = static_cast<float>((a_col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a_alpha, 0.0f, 1.0f);
			return (a_col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
		}

		ImU32 Mix(ImU32 a_from, ImU32 a_to, float a_t)
		{
			return ImGui::ColorConvertFloat4ToU32(ImLerp(V(a_from), V(a_to), std::clamp(a_t, 0.0f, 1.0f)));
		}

		// WCAG contrast of two colors (alpha ignored); the themes are checked against 4.5 when applied.
		float Contrast(ImU32 a_a, ImU32 a_b)
		{
			auto lum = [](ImU32 c) {
				auto ch = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
				const auto f = ImGui::ColorConvertU32ToFloat4(c);
				return 0.2126f * ch(f.x) + 0.7152f * ch(f.y) + 0.0722f * ch(f.z);
			};
			const float a = lum(a_a), b = lum(a_b);
			return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
		}

		// Headers are upper case, as in the game's menus. UTF-8: ASCII and Russian letters.
		std::string Upper(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const auto c = static_cast<unsigned char>(a_text[i]);
				if (c >= 'a' && c <= 'z') {
					out += static_cast<char>(c - 32);
				} else if ((c == 0xD0 || c == 0xD1) && i + 1 < a_text.size()) {
					const auto n = static_cast<unsigned char>(a_text[++i]);
					if (c == 0xD0 && n >= 0xB0 && n <= 0xBF) {  // а..п
						out += static_cast<char>(0xD0);
						out += static_cast<char>(n - 0x20);
					} else if (c == 0xD1 && n >= 0x80 && n <= 0x8F) {  // р..я
						out += static_cast<char>(0xD0);
						out += static_cast<char>(n + 0x20);
					} else if (c == 0xD1 && n == 0x91) {  // ё
						out += "Ё";
					} else {
						out += static_cast<char>(c);
						out += static_cast<char>(n);
					}
				} else {
					out += static_cast<char>(c);
				}
			}
			return out;
		}

		void SetAlpha(float a_alpha)
		{
			g_alpha = std::clamp(a_alpha, 0.0f, 1.0f);
			g_cur = Base();
			auto colors = reinterpret_cast<ImU32*>(&g_cur);
			for (std::size_t i = 0; i < sizeof(Theme) / sizeof(ImU32); ++i) {
				colors[i] = Fade(colors[i], g_alpha);
			}
			ImGui::GetStyle().Alpha = std::max(g_alpha, 0.01f);
		}

		float EaseOut(float a_t)
		{
			const float t = 1.0f - std::clamp(a_t, 0.0f, 1.0f);
			return 1.0f - t * t * t;
		}

		float Since(Clock::time_point a_time) { return std::chrono::duration<float>(Clock::now() - a_time).count(); }

		// Smoothly follows a_target; the value lives in the current window's state storage.
		float Anim(ImGuiID a_id, float a_target, float a_speed = 14.0f)
		{
			float* v = ImGui::GetStateStorage()->GetFloatRef(a_id + 0x5A17, a_target);
			*v += (a_target - *v) * std::min(1.0f, ImGui::GetIO().DeltaTime * a_speed);
			return *v;
		}

		void PushFont(ImFont* a_font) { ImGui::PushFont(a_font, a_font->LegacySize); }

		// Test hook: where each labelled control was drawn, refreshed every frame.
		std::unordered_map<std::string, ImRect> g_controls;
		std::unordered_map<std::string, ImRect> g_controlsLastFrame;
		std::string                             g_rowLabel;

		void RecordControl(std::string_view a_label)
		{
			const auto key = std::string{ a_label.substr(0, a_label.find("##")) };
			const std::string label = Tr(key.c_str());
			g_controls[label] = ImRect{ ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
		}

		// ---------------------------------------------------------------- icons

		enum class Icon
		{
			kHome,
			kUser,
			kSword,
			kSparkles,
			kChart,
			kGlobe,
			kPin,
			kTarget,
			kTerminal,
			kGift,
			kGear,
			kSearch,
			kClose,
			kInfo,
			kDragon,
			kUsers,
			kCoin,
			kKey,
			kScroll,
			kFlame,
			// Skyrim-themed
			kHall,
			kHelmet,
			kAxeSword,
			kShout,
			kRuneCircle,
			kConstellation,
			kMoons,
			kMap,
			kEye,
			kTilde,
			kChest,
			kDragonHead,
			kDwemerGear,
			// crafting stations
			kAnvil,
			kFlask,
			kSoulGem,
			kWheel,
			kHammer,
			kCrucible,
			kHide,
			kPot,
			kOven,
			kStaff,
			// states
			kCheck,
			kWarn
		};

		// The game's map markers (exported from map.swf into Data/SKSE/Plugins/DragonbornsCodex/icons) for the icons that
		// have one; white with alpha, so they take the tint. The rest, and all of them if the files are missing,
		// are line icons drawn below.
		const char* IconTexture(Icon a_icon)
		{
			switch (a_icon) {
			case Icon::kHall: return "icons/home";
			case Icon::kHelmet: return "icons/dovahkiin";
			case Icon::kAxeSword: return "icons/combat";
			case Icon::kShout: return "icons/magic";
			case Icon::kRuneCircle: return "icons/schools";
			case Icon::kStaff: return "icons/enchant";
			case Icon::kConstellation: return "icons/progress";
			case Icon::kMoons: return "icons/world";
			case Icon::kMap: return "icons/teleport";
			case Icon::kScroll: return "icons/quests";
			case Icon::kEye: return "icons/target";
			case Icon::kTilde: return "icons/console";
			case Icon::kChest: return "icons/items";
			case Icon::kDragonHead: return "icons/actors";
			case Icon::kDwemerGear: return "icons/service";
			case Icon::kHammer: return "icons/mine";
			case Icon::kUsers: return "icons/town";
			default: return nullptr;
			}
		}

		// Line icons drawn in a 24x24 design grid, scaled to a_size.
		void DrawIcon(ImDrawList* a_dl, Icon a_icon, ImVec2 a_center, float a_size, ImU32 a_col)
		{
			if (const auto name = IconTexture(a_icon)) {
				if (const auto& image = Textures::Get(name)) {
					const float h = a_size * 1.15f;  // markers are drawn a bit larger: they have no stroke padding
					const float w = h * image.width / image.height;
					a_dl->AddImage(static_cast<ImTextureID>(image.id), { a_center.x - w * 0.5f, a_center.y - h * 0.5f }, { a_center.x + w * 0.5f, a_center.y + h * 0.5f },
						{ 0, 0 }, { 1, 1 }, a_col);
					return;
				}
			}
			const float k = a_size / 24.0f;
			const float w = std::max(1.0f, 1.8f * k);
			auto        P = [&](float x, float y) { return ImVec2{ a_center.x + (x - 12.0f) * k, a_center.y + (y - 12.0f) * k }; };
			auto        line = [&](float x1, float y1, float x2, float y2) { a_dl->AddLine(P(x1, y1), P(x2, y2), a_col, w); };
			auto        poly = [&](std::initializer_list<ImVec2> pts, bool closed) {
                std::vector<ImVec2> v;
                for (auto p : pts) {
                    v.push_back(P(p.x, p.y));
                }
                a_dl->AddPolyline(v.data(), static_cast<int>(v.size()), a_col, closed ? ImDrawFlags_Closed : ImDrawFlags_None, w);
			};
			auto circle = [&](float x, float y, float r) { a_dl->AddCircle(P(x, y), r * k, a_col, 0, w); };
			auto arc = [&](float x, float y, float r, float a0, float a1) {
				a_dl->PathArcTo(P(x, y), r * k, a0, a1);
				a_dl->PathStroke(a_col, ImDrawFlags_None, w);
			};
			constexpr float pi = 3.14159265f;

			switch (a_icon) {
			case Icon::kHome:
				poly({ { 3, 10 }, { 12, 3 }, { 21, 10 }, { 21, 20 }, { 15, 20 }, { 15, 14 }, { 9, 14 }, { 9, 20 }, { 3, 20 } }, true);
				break;
			case Icon::kUser:
				circle(12, 8, 4);
				arc(12, 21, 8, pi, 2 * pi);
				break;
			case Icon::kSword:
				line(5, 19, 18, 6);
				poly({ { 15, 4 }, { 20, 4 }, { 20, 9 } }, false);
				line(5, 14, 10, 19);
				line(3, 21, 6, 18);
				break;
			case Icon::kSparkles:
				poly({ { 11, 3 }, { 13, 9 }, { 19, 11 }, { 13, 13 }, { 11, 19 }, { 9, 13 }, { 3, 11 }, { 9, 9 } }, true);
				poly({ { 19, 16 }, { 19.8f, 18.2f }, { 22, 19 }, { 19.8f, 19.8f }, { 19, 22 }, { 18.2f, 19.8f }, { 16, 19 }, { 18.2f, 18.2f } }, true);
				break;
			case Icon::kChart:
				line(3, 20, 21, 20);
				poly({ { 6, 16 }, { 10, 11 }, { 13, 14 }, { 18, 7 } }, false);
				break;
			case Icon::kGlobe:
				circle(12, 12, 9);
				line(3, 12, 21, 12);
				a_dl->AddEllipse(P(12, 12), ImVec2{ 4.0f * k, 9.0f * k }, a_col, 0.0f, 0, w);
				break;
			case Icon::kPin:
				arc(12, 10, 7, pi * 0.85f, pi * 2.15f);
				poly({ { 5.6f, 12.8f }, { 12, 21 }, { 18.4f, 12.8f } }, false);
				circle(12, 10, 2.5f);
				break;
			case Icon::kTarget:
				circle(12, 12, 8);
				line(12, 2, 12, 6);
				line(12, 18, 12, 22);
				line(2, 12, 6, 12);
				line(18, 12, 22, 12);
				break;
			case Icon::kTerminal:
				poly({ { 4, 6 }, { 10, 12 }, { 4, 18 } }, false);
				line(12, 18, 20, 18);
				break;
			case Icon::kGift:
				a_dl->AddRect(P(3, 8), P(21, 21), a_col, 1.0f * k, 0, w);
				line(12, 8, 12, 21);
				line(3, 12, 21, 12);
				arc(9.5f, 6.5f, 2.5f, 0.0f, 2 * pi);
				arc(14.5f, 6.5f, 2.5f, 0.0f, 2 * pi);
				break;
			case Icon::kGear:
				circle(12, 12, 3.2f);
				for (int i = 0; i < 8; ++i) {
					const float a = i * pi / 4;
					line(12 + std::cos(a) * 6, 12 + std::sin(a) * 6, 12 + std::cos(a) * 9.5f, 12 + std::sin(a) * 9.5f);
				}
				break;
			case Icon::kSearch:
				circle(11, 11, 7);
				line(16, 16, 20.5f, 20.5f);
				break;
			case Icon::kClose:
				line(6, 6, 18, 18);
				line(18, 6, 6, 18);
				break;
			case Icon::kInfo:
				circle(12, 12, 9);
				line(12, 11, 12, 16.5f);
				a_dl->AddCircleFilled(P(12, 7.8f), 1.1f * k, a_col);
				break;
			case Icon::kUsers:
				circle(9, 8, 3.5f);
				arc(9, 20, 6.5f, pi, 2 * pi);
				arc(16, 8, 3, -pi * 0.5f, pi * 0.5f);
				arc(16, 20, 5.5f, pi * 1.45f, 2 * pi);
				break;
			case Icon::kDragon:
				poly({ { 4, 9 }, { 7, 4 }, { 10, 8 }, { 14, 8 }, { 17, 4 }, { 20, 9 }, { 18, 13 }, { 19, 17 }, { 15, 20 }, { 9, 20 }, { 5, 17 }, { 6, 13 } }, true);
				a_dl->AddCircleFilled(P(9.5f, 13), 1.0f * k, a_col);
				a_dl->AddCircleFilled(P(14.5f, 13), 1.0f * k, a_col);
				break;
			case Icon::kCoin:
				circle(12, 12, 8.5f);
				circle(12, 12, 5);
				break;
			case Icon::kFlame:
				poly({ { 12, 3 }, { 17, 9 }, { 19, 14 }, { 17, 19 }, { 12, 21 }, { 7, 19 }, { 5, 14 }, { 8, 9 }, { 10, 12 } }, true);
				poly({ { 12, 13 }, { 14.5f, 16.5f }, { 12, 19.5f }, { 9.5f, 16.5f } }, true);
				break;
			case Icon::kScroll:
				a_dl->AddRect(P(6, 4), P(18, 20), a_col, 1.5f * k, 0, w);
				line(9, 9, 15, 9);
				line(9, 12.5f, 15, 12.5f);
				line(9, 16, 13, 16);
				break;
			case Icon::kKey:
				circle(8, 12, 4.5f);
				line(12.5f, 12, 21, 12);
				line(18, 12, 18, 16);
				line(21, 12, 21, 15);
				break;
			case Icon::kHall:  // Nordic longhouse with crossed gable horns
				poly({ { 2, 12 }, { 12, 4.5f }, { 22, 12 } }, false);
				poly({ { 5, 10 }, { 5, 20 }, { 19, 20 }, { 19, 10 } }, false);
				poly({ { 10, 20 }, { 10, 15 }, { 14, 15 }, { 14, 20 } }, false);
				line(10, 6, 7, 2.5f);
				line(14, 6, 17, 2.5f);
				break;
			case Icon::kHelmet:  // horned Nord helmet
				arc(12, 13, 7, pi, 2 * pi);
				line(4, 13, 20, 13);
				line(12, 13, 12, 19);
				line(6.5f, 13, 7.5f, 18);
				line(17.5f, 13, 16.5f, 18);
				poly({ { 5.5f, 9.5f }, { 2.5f, 7 }, { 2.5f, 2.5f } }, false);
				poly({ { 18.5f, 9.5f }, { 21.5f, 7 }, { 21.5f, 2.5f } }, false);
				break;
			case Icon::kAxeSword:  // crossed sword and axe
				line(4, 20, 17, 7);
				poly({ { 15.5f, 5 }, { 20, 4 }, { 19, 8.5f } }, false);
				line(6, 14.5f, 9.5f, 18);
				line(20, 20, 6, 6);
				poly({ { 9, 4.5f }, { 4, 3.5f }, { 3, 8.5f }, { 4.5f, 9 }, { 7.5f, 7.5f } }, true);
				break;
			case Icon::kShout:  // the Thu'um: a voice in three waves
				a_dl->AddCircleFilled(P(5, 12), 1.8f * k, a_col);
				arc(5, 12, 5.5f, -0.7f, 0.7f);
				arc(5, 12, 10, -0.6f, 0.6f);
				arc(5, 12, 14.5f, -0.5f, 0.5f);
				break;
			case Icon::kRuneCircle:  // magic circle
				circle(12, 12, 9);
				poly({ { 12, 4.5f }, { 18.5f, 15.8f }, { 5.5f, 15.8f } }, true);
				poly({ { 12, 19.5f }, { 5.5f, 8.2f }, { 18.5f, 8.2f } }, true);
				break;
			case Icon::kConstellation:  // skill trees are constellations in Skyrim
				poly({ { 4, 19 }, { 8, 12 }, { 13, 14 }, { 17, 5 } }, false);
				line(13, 14, 20, 17);
				for (auto [x, y] : { std::pair{ 4.0f, 19.0f }, { 8.0f, 12.0f }, { 13.0f, 14.0f }, { 17.0f, 5.0f }, { 20.0f, 17.0f } }) {
					a_dl->AddCircleFilled(P(x, y), 1.9f * k, a_col);
				}
				break;
			case Icon::kMoons:  // Masser and Secunda
				circle(9.5f, 13.5f, 7);
				arc(12.5f, 11, 6, pi * 0.62f, pi * 1.38f);
				circle(19, 5.5f, 2.6f);
				break;
			case Icon::kMap:  // folded map
				poly({ { 3, 6 }, { 9, 4 }, { 15, 6 }, { 21, 4 }, { 21, 18 }, { 15, 20 }, { 9, 18 }, { 3, 20 } }, true);
				line(9, 4, 9, 18);
				line(15, 6, 15, 20);
				break;
			case Icon::kEye:
				arc(12, 21, 12, pi * 1.2f, pi * 1.8f);
				arc(12, 3, 12, pi * 0.2f, pi * 0.8f);
				circle(12, 12, 3);
				break;
			case Icon::kTilde:  // the console key
				a_dl->AddRect(P(3, 5), P(21, 19), a_col, 2.0f * k, 0, w);
				poly({ { 7, 13.5f }, { 9, 10.5f }, { 12, 12 }, { 15, 13.5f }, { 17, 10.5f } }, false);
				break;
			case Icon::kChest:  // treasure chest
				poly({ { 3, 11 }, { 3, 8 }, { 5, 5.5f }, { 19, 5.5f }, { 21, 8 }, { 21, 11 } }, false);
				a_dl->AddRect(P(3, 11), P(21, 20), a_col, 0.0f, 0, w);
				a_dl->AddRect(P(10.5f, 9.5f), P(13.5f, 13.5f), a_col, 0.0f, 0, w);
				break;
			case Icon::kDragonHead:  // dragon in profile
				poly({ { 2, 15 }, { 7, 11 }, { 11, 10 }, { 13, 5 }, { 14.5f, 9.5f }, { 18, 10 }, { 22, 13 }, { 17.5f, 14 }, { 19.5f, 17.5f }, { 13, 16 }, { 7, 19 } }, true);
				a_dl->AddCircleFilled(P(15, 12), 1.0f * k, a_col);
				break;
			case Icon::kDwemerGear:  // Dwemer cog
				circle(12, 12, 6);
				circle(12, 12, 2.2f);
				for (int i = 0; i < 6; ++i) {
					const float a = i * pi / 3;
					const float c0 = std::cos(a - 0.28f), s0 = std::sin(a - 0.28f), c1 = std::cos(a + 0.28f), s1 = std::sin(a + 0.28f);
					poly({ { 12 + c0 * 6, 12 + s0 * 6 }, { 12 + c0 * 9.5f, 12 + s0 * 9.5f }, { 12 + c1 * 9.5f, 12 + s1 * 9.5f }, { 12 + c1 * 6, 12 + s1 * 6 } }, false);
				}
				break;
			case Icon::kAnvil:
				poly({ { 2, 7 }, { 16, 7 }, { 21.5f, 9.5f }, { 16, 11.5f }, { 14.5f, 11.5f }, { 14.5f, 14.5f }, { 18, 18 }, { 6, 18 }, { 9.5f, 14.5f }, { 9.5f, 11.5f }, { 6, 11.5f } }, true);
				line(4, 21, 20, 21);
				break;
			case Icon::kFlask:
				poly({ { 10, 3 }, { 10, 9 }, { 4.5f, 19 }, { 5.5f, 21 }, { 18.5f, 21 }, { 19.5f, 19 }, { 14, 9 }, { 14, 3 } }, false);
				line(9, 3, 15, 3);
				line(6.8f, 15.5f, 17.2f, 15.5f);
				break;
			case Icon::kSoulGem:
				poly({ { 12, 2.5f }, { 19.5f, 9.5f }, { 12, 21.5f }, { 4.5f, 9.5f } }, true);
				line(4.5f, 9.5f, 19.5f, 9.5f);
				poly({ { 9, 9.5f }, { 12, 2.5f }, { 15, 9.5f }, { 12, 21.5f }, { 9, 9.5f } }, false);
				break;
			case Icon::kWheel:  // grindstone
				circle(11, 10, 7);
				a_dl->AddCircleFilled(P(11, 10), 1.6f * k, a_col);
				line(6, 21, 11, 10);
				line(16, 21, 11, 10);
				line(3, 21, 19, 21);
				poly({ { 11, 10 }, { 20.5f, 7 }, { 21.5f, 10 } }, false);
				break;
			case Icon::kHammer:  // workbench hammer
				a_dl->AddRect(P(4, 4), P(16, 9), a_col, 1.0f * k, 0, w);
				line(10, 9, 10, 21);
				line(16, 6.5f, 20, 6.5f);
				break;
			case Icon::kCrucible:  // smelter
				poly({ { 5, 7 }, { 19, 7 }, { 17, 16 }, { 7, 16 } }, true);
				line(19, 7, 21.5f, 4);
				poly({ { 7, 21.5f }, { 9.5f, 18.5f }, { 12, 21.5f }, { 14.5f, 18.5f }, { 17, 21.5f } }, false);
				break;
			case Icon::kHide:  // tanning rack
				a_dl->AddRect(P(2.5f, 2.5f), P(21.5f, 21.5f), a_col, 0.0f, 0, w);
				poly({ { 8, 6 }, { 16, 6 }, { 18, 9 }, { 16, 12 }, { 18, 18 }, { 12, 16 }, { 6, 18 }, { 8, 12 }, { 6, 9 } }, true);
				line(2.5f, 2.5f, 8, 6);
				line(21.5f, 2.5f, 16, 6);
				line(2.5f, 21.5f, 6, 18);
				line(21.5f, 21.5f, 18, 18);
				break;
			case Icon::kPot:  // cooking pot over a fire
				arc(12, 9, 8, pi, 2 * pi);
				poly({ { 4, 10 }, { 20, 10 }, { 18, 17 }, { 6, 17 } }, true);
				poly({ { 8, 22 }, { 10, 19.5f }, { 12, 22 }, { 14, 19.5f }, { 16, 22 } }, false);
				break;
			case Icon::kOven:
				arc(12, 18, 9, pi, 2 * pi);
				line(2, 18, 22, 18);
				arc(12, 18, 4, pi, 2 * pi);
				a_dl->AddRect(P(15, 4), P(18, 9), a_col, 0.0f, 0, w);
				break;
			case Icon::kStaff:
				line(5, 22, 15, 8);
				circle(17, 5.5f, 2.8f);
				arc(17, 5.5f, 5.5f, pi * 0.55f, pi * 1.2f);
				arc(17, 5.5f, 5.5f, pi * 1.8f, pi * 2.45f);
				break;
			case Icon::kCheck:
				poly({ { 4.5f, 12.5f }, { 9.5f, 17.5f }, { 19.5f, 6.5f } }, false);
				break;
			case Icon::kWarn:
				poly({ { 12, 3 }, { 22, 20 }, { 2, 20 } }, true);
				line(12, 9, 12, 14);
				a_dl->AddCircleFilled(P(12, 17), 1.1f * k, a_col);
				break;
			}
		}

		// Five-pointed star (favourites); a fan from the centre fills it correctly.
		void Star(ImDrawList* a_dl, ImVec2 a_c, float a_r, ImU32 a_col, bool a_filled)
		{
			ImVec2 pts[10];
			for (int i = 0; i < 10; ++i) {
				const float a = -IM_PI * 0.5f + i * IM_PI / 5.0f;
				const float r = i % 2 == 0 ? a_r : a_r * 0.45f;
				pts[i] = { a_c.x + std::cos(a) * r, a_c.y + std::sin(a) * r };
			}
			if (a_filled) {
				for (int i = 0; i < 10; ++i) {
					a_dl->AddTriangleFilled(a_c, pts[i], pts[(i + 1) % 10], a_col);
				}
			} else {
				a_dl->AddPolyline(pts, 10, a_col, ImDrawFlags_Closed, std::max(1.0f, S(1.4f)));
			}
		}

		// ---------------------------------------------------------------- decoration

		void Corners(ImDrawList* a_dl, ImVec2 a_min, ImVec2 a_max, float a_len, float a_inset, ImU32 a_col, float a_thick)
		{
			const float l = a_len, i = a_inset;
			a_dl->AddLine({ a_min.x + i, a_min.y + i }, { a_min.x + i + l, a_min.y + i }, a_col, a_thick);
			a_dl->AddLine({ a_min.x + i, a_min.y + i }, { a_min.x + i, a_min.y + i + l }, a_col, a_thick);
			a_dl->AddLine({ a_max.x - i, a_min.y + i }, { a_max.x - i - l, a_min.y + i }, a_col, a_thick);
			a_dl->AddLine({ a_max.x - i, a_min.y + i }, { a_max.x - i, a_min.y + i + l }, a_col, a_thick);
			a_dl->AddLine({ a_min.x + i, a_max.y - i }, { a_min.x + i + l, a_max.y - i }, a_col, a_thick);
			a_dl->AddLine({ a_min.x + i, a_max.y - i }, { a_min.x + i, a_max.y - i - l }, a_col, a_thick);
			a_dl->AddLine({ a_max.x - i, a_max.y - i }, { a_max.x - i - l, a_max.y - i }, a_col, a_thick);
			a_dl->AddLine({ a_max.x - i, a_max.y - i }, { a_max.x - i, a_max.y - i - l }, a_col, a_thick);
		}

		void Diamond(ImDrawList* a_dl, ImVec2 a_c, float a_r, ImU32 a_fill, ImU32 a_border, float a_thick)
		{
			const ImVec2 pts[4] = { { a_c.x, a_c.y - a_r }, { a_c.x + a_r, a_c.y }, { a_c.x, a_c.y + a_r }, { a_c.x - a_r, a_c.y } };
			a_dl->AddConvexPolyFilled(pts, 4, a_fill);
			a_dl->AddPolyline(pts, 4, a_border, ImDrawFlags_Closed, a_thick);
		}

		// The game's divider: a hairline that fades out towards both ends. a_gap leaves room in the middle.
		void Rule(ImDrawList* a_dl, float a_x0, float a_x1, float a_y, ImU32 a_col, float a_gap = 0.0f)
		{
			const float mid = (a_x0 + a_x1) * 0.5f;
			const float t = std::max(1.0f, S(1));
			const ImU32 clear = Fade(a_col, 0.0f);
			a_dl->AddRectFilledMultiColor({ a_x0, a_y }, { mid - a_gap, a_y + t }, clear, a_col, a_col, clear);
			a_dl->AddRectFilledMultiColor({ mid + a_gap, a_y }, { a_x1, a_y + t }, a_col, clear, clear, a_col);
		}

		// Divider with a small diamond in the middle, as between the sidebar groups.
		void Ornament(float a_alpha = 0.45f)
		{
			auto       dl = ImGui::GetWindowDrawList();
			const auto pos = ImGui::GetCursorScreenPos();
			const auto w = ImGui::GetContentRegionAvail().x;
			const auto y = pos.y + S(8);
			const auto col = Fade(T().gold, a_alpha);
			const auto mid = pos.x + w * 0.5f;
			Rule(dl, pos.x, pos.x + w, y, col, S(8));
			Diamond(dl, { mid, y + S(0.5f) }, S(3.5f), Fade(T().gold, 0.0f), col, 1.0f);
			ImGui::Dummy({ w, S(16) });
		}

		// ---------------------------------------------------------------- state

		enum class Page
		{
			kHome,
			kDovahkiin,
			kCombat,
			kMagic,
			kSchools,
			kEnchant,
			kProgress,
			kWorld,
			kTeleport,
			kTarget,
			kConsole,
			kItems,
			kActors,
			kService,
			kQuests
		};

		struct PageInfo
		{
			Page        page;
			Icon        icon;
			const char* name;
		};

		constexpr PageInfo kTopPages[] = {
			{ Page::kHome, Icon::kHall, "page.home" },
		};
		constexpr PageInfo kMainPages[] = {
			{ Page::kDovahkiin, Icon::kHelmet, "page.dragonborn" },
			{ Page::kCombat, Icon::kAxeSword, "page.combat_and_movement" },
			{ Page::kMagic, Icon::kShout, "page.magic_shouts_perks" },
			{ Page::kSchools, Icon::kRuneCircle, "page.schools_of_magic" },
			{ Page::kEnchant, Icon::kStaff, "page.enchanting" },
			{ Page::kProgress, Icon::kConstellation, "page.experience_and_skills" },
			{ Page::kWorld, Icon::kMoons, "page.world_and_time" },
			{ Page::kTeleport, Icon::kMap, "page.teleport" },
			{ Page::kQuests, Icon::kScroll, "page.quests" },
			{ Page::kTarget, Icon::kEye, "page.target" },
			{ Page::kConsole, Icon::kTilde, "page.console" },
		};
		constexpr PageInfo kBottomPages[] = {
			{ Page::kItems, Icon::kChest, "page.add_items" },
			{ Page::kActors, Icon::kDragonHead, "cat.creatures_and_npcs" },
			{ Page::kService, Icon::kDwemerGear, "page.service" },
		};

		Page                 g_page = Page::kHome;
		std::optional<float> g_scrollRequest;  // test hook; negative = to the bottom
		std::string g_search;
		std::string g_help;
		std::string g_status;  // result of the last action, shown in the footer
		bool        g_focusSearch = false;

		Clock::time_point g_openTime;
		Clock::time_point g_pageTime;
		float             g_barY = -1.0f;  // animated sidebar marker, relative to the sidebar window

		std::string       g_toast;
		Clock::time_point g_toastTime;
		constexpr float   kToastLife = 2.2f;

		// What was done lately (menu actions and hotkeys), newest first; the home page lists it.
		struct HistoryEntry
		{
			Clock::time_point time;
			std::string       text;
		};
		std::vector<HistoryEntry> g_history;

		// The Skyrim emblem as a texture, made on first use.
		unsigned long long LogoTexture()
		{
			static unsigned long long texture = 0;
			static bool               tried = false;
			if (!tried) {
				tried = true;
				texture = Overlay::CreateAlphaTexture(LogoData::kAlpha, LogoData::kWidth, LogoData::kHeight);
			}
			return texture;
		}

		// The result of an action: in the footer, and as a notice on top (also seen when the action closed the menu).
		void Status(std::string a_text)
		{
			g_status = Tr(a_text.c_str());
			Toast(g_status);
		}

		RE::ObjectRefHandle g_target;
		std::string         g_targetName;

		std::string              g_consoleInput;
		std::vector<std::string> g_consoleHistory;

		float g_hour = 12.0f;
		float g_timescale = 20.0f;
		float g_playerScale = 1.0f;
		float g_scalePct = 100.0f;

		// Character editor values, read from the game when the menu opens.
		struct SkillInfo
		{
			RE::ActorValue av;
			const char*    name;
		};
		constexpr SkillInfo kSkills[] = {
			{ RE::ActorValue::kOneHanded, "skill.one_handed" },
			{ RE::ActorValue::kTwoHanded, "skill.two_handed" },
			{ RE::ActorValue::kArchery, "skill.archery" },
			{ RE::ActorValue::kBlock, "skill.block" },
			{ RE::ActorValue::kSmithing, "skill.smithing" },
			{ RE::ActorValue::kHeavyArmor, "skill.heavy_armor" },
			{ RE::ActorValue::kLightArmor, "skill.light_armor" },
			{ RE::ActorValue::kPickpocket, "skill.pickpocket" },
			{ RE::ActorValue::kLockpicking, "skill.lockpicking" },
			{ RE::ActorValue::kSneak, "skill.sneak" },
			{ RE::ActorValue::kAlchemy, "skill.alchemy" },
			{ RE::ActorValue::kSpeech, "skill.speech" },
			{ RE::ActorValue::kAlteration, "skill.alteration" },
			{ RE::ActorValue::kConjuration, "skill.conjuration" },
			{ RE::ActorValue::kDestruction, "skill.destruction" },
			{ RE::ActorValue::kIllusion, "skill.illusion" },
			{ RE::ActorValue::kRestoration, "skill.restoration" },
			{ RE::ActorValue::kEnchanting, "page.enchanting" },
		};

		struct StationRow
		{
			Cheats::Station station;
			Icon            icon;
			const char*     label;
			const char*     help;
		};
		constexpr StationRow kStations[] = {
			{ Cheats::Station::kEnchanting, Icon::kSoulGem, "station.enchanting_table", "station.enchant_and_disenchant_items_just" },
			{ Cheats::Station::kAlchemy, Icon::kFlask, "station.alchemy_lab", "station.brew_potions_and_poisons" },
			{ Cheats::Station::kForge, Icon::kAnvil, "station.forge", "station.forge_weapons_and_armor_including" },
			{ Cheats::Station::kGrindstone, Icon::kWheel, "station.grindstone", "station.improve_weapons" },
			{ Cheats::Station::kWorkbench, Icon::kHammer, "station.workbench", "station.improve_armor" },
			{ Cheats::Station::kSmelter, Icon::kCrucible, "station.smelter", "station.smelt_ore_into_ingots" },
			{ Cheats::Station::kTanningRack, Icon::kHide, "station.tanning_rack", "station.turn_hides_into_leather_and" },
			{ Cheats::Station::kCookpot, Icon::kPot, "station.cooking_pot", "station.cook_food" },
			{ Cheats::Station::kOven, Icon::kOven, "station.oven", "station.bake_hearthfire_recipes" },
			{ Cheats::Station::kStaffEnchanter, Icon::kStaff, "station.staff_enchanter", "station.craft_dragonborn_staves" },
		};
		constexpr SkillInfo kAttributes[] = {
			{ RE::ActorValue::kHealth, "progress.health_base" },
			{ RE::ActorValue::kMagicka, "progress.magicka_base" },
			{ RE::ActorValue::kStamina, "progress.stamina_base" },
		};
		std::array<float, std::size(kSkills)>     g_skillValues{};
		std::array<float, std::size(kAttributes)> g_attrValues{};
		int                                       g_levelInput = 1;

		void RefreshStats()
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			auto avo = player->AsActorValueOwner();
			for (std::size_t i = 0; i < std::size(kSkills); ++i) {
				g_skillValues[i] = avo->GetBaseActorValue(kSkills[i].av);
			}
			for (std::size_t i = 0; i < std::size(kAttributes); ++i) {
				g_attrValues[i] = avo->GetBaseActorValue(kAttributes[i].av);
			}
			g_levelInput = player->GetLevel();
		}

		const char* PageName(Page a_page)
		{
			for (std::span<const PageInfo> list : { std::span<const PageInfo>{ kTopPages }, std::span<const PageInfo>{ kMainPages }, std::span<const PageInfo>{ kBottomPages } }) {
				for (const auto& p : list) {
					if (p.page == a_page) {
						return p.name;
					}
				}
			}
			return "";
		}

		std::string ToggleKeyName() { return Hotkeys::KeyName(Settings::Get().toggleKey); }

		bool Matches(const char* a_label)
		{
			if (g_search.empty()) {
				return true;
			}
			return Text::Contains(Text::ToLower(Tr(a_label)), Text::ToLower(g_search));
		}

		// ---------------------------------------------------------------- catalog keys

		// Load-order independent keys of Catalog::Forms(), for favourites and recents.
		std::vector<std::string>             g_keys;
		std::unordered_map<std::string, int> g_keyIndex;

		void EnsureKeys()
		{
			const auto& forms = Catalog::Forms();
			if (g_keys.size() == forms.size()) {
				return;
			}
			g_keys.clear();
			g_keyIndex.clear();
			g_keys.reserve(forms.size());
			for (int i = 0; i < static_cast<int>(forms.size()); ++i) {
				g_keys.push_back(Settings::Key(forms[i].plugin, forms[i].formID));
				g_keyIndex.emplace(g_keys.back(), i);
			}
		}

		const Catalog::Entry* FindByKey(const std::string& a_key)
		{
			EnsureKeys();
			const auto it = g_keyIndex.find(a_key);
			return it != g_keyIndex.end() ? &Catalog::Forms()[it->second] : nullptr;
		}

		// ---------------------------------------------------------------- global search

		// While searching, pages are drawn as one flat list: cards are not drawn, and the
		// page / card titles are printed only above rows that actually match.
		bool        g_searching = false;
		Page        g_searchPage = Page::kHome;
		bool        g_searchPageShown = false;
		const char* g_searchCard = nullptr;
		bool        g_searchCardShown = false;
		int         g_searchHits = 0;

		void SubHeaderImpl(const char* a_text)
		{
			ImGui::Spacing();
			auto       dl = ImGui::GetWindowDrawList();
			const auto p = ImGui::GetCursorScreenPos();
			const auto h = ImGui::GetTextLineHeight();
			const auto right = p.x + ImGui::GetContentRegionAvail().x;
			Diamond(dl, { p.x + S(4), p.y + h * 0.5f }, S(3), T().accent, T().accent, 1.0f);
			ImGui::SetCursorScreenPos({ p.x + S(14), p.y });
			PushFont(Overlay::GetFonts().caption);
			ImGui::PushStyleColor(ImGuiCol_Text, T().accent);
			ImGui::TextUnformatted(Upper(Tr(a_text)).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			const auto  end = ImGui::GetItemRectMax();
			const ImU32 clear = Fade(T().line, 0.0f);
			dl->AddRectFilledMultiColor({ end.x + S(10), p.y + h * 0.5f }, { right, p.y + h * 0.5f + std::max(1.0f, S(1)) }, T().line, clear, clear, T().line);
		}

		void SubHeader(const char* a_text)
		{
			if (!g_searching) {
				SubHeaderImpl(a_text);
			}
		}

		void EmitSearchHeaders()
		{
			if (!g_searchPageShown) {
				g_searchPageShown = true;
				if (g_searchHits > 0) {
					ImGui::Dummy({ 0, S(10) });
				}
				PushFont(Overlay::GetFonts().heading);
				ImGui::PushStyleColor(ImGuiCol_Text, T().title);
				ImGui::TextUnformatted(Tr(PageName(g_searchPage)));
				ImGui::PopStyleColor();
				ImGui::PopFont();
				if (ImGui::IsItemHovered()) {
					g_help = "search.click_to_open_this_section";
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				}
				if (ImGui::IsItemClicked()) {
					g_page = g_searchPage;
					g_search.clear();
				}
			}
			if (!g_searchCardShown && g_searchCard) {
				g_searchCardShown = true;
				SubHeaderImpl(g_searchCard);
			}
			++g_searchHits;
		}

		// ---------------------------------------------------------------- widgets

		// Toggle switch, right-aligned in the current row.
		bool Switch(const char* a_id, bool* a_value)
		{
			const ImVec2 size{ S(44), S(24) };
			ImGui::PushID(a_id);
			const bool clicked = ImGui::InvisibleButton("##sw", size);
			ImGui::PopID();
			if (clicked) {
				*a_value = !*a_value;
			}
			const float t = Anim(ImGui::GetItemID(), *a_value ? 1.0f : 0.0f, 16.0f);
			const float hover = Anim(ImGui::GetItemID() + 1, ImGui::IsItemHovered() ? 1.0f : 0.0f);
			const auto  min = ImGui::GetItemRectMin();
			const auto  max = ImGui::GetItemRectMax();
			auto        dl = ImGui::GetWindowDrawList();
			// Square track and knob: the game's menus have no round shapes.
			const ImVec2 tmin{ min.x, min.y + S(3) }, tmax{ max.x, max.y - S(3) };
			dl->AddRectFilled(tmin, tmax, Fade(Mix(T().off, T().accent, t), 0.35f + 0.65f * t), S(1));
			dl->AddRect(tmin, tmax, Mix(T().line, T().gold, std::max(hover, t * 0.6f)), S(1), 0, std::max(1.0f, S(1)));
			const float k = (tmax.y - tmin.y) - S(6);
			const float x = ImLerp(tmin.x + S(3), tmax.x - S(3) - k, t);
			dl->AddRectFilled({ x, tmin.y + S(3) }, { x + k, tmin.y + S(3) + k }, Mix(T().muted, T().onAccent, t), S(1));
			return clicked;
		}

		// Slider with a thin track and a diamond thumb. Returns true when the value should be
		// applied: on release, and (if a_live) a few times per second while dragging.
		bool Slider(const char* a_id, float* a_value, float a_min, float a_max, const char* a_fmt, float a_width, bool a_live = true)
		{
			const ImVec2 size{ a_width, S(24) };
			ImGui::PushID(a_id);
			ImGui::InvisibleButton("##sl", size);
			ImGui::PopID();
			const auto  min = ImGui::GetItemRectMin();
			const auto  max = ImGui::GetItemRectMax();
			const bool  active = ImGui::IsItemActive();
			const bool  released = ImGui::IsItemDeactivated();  // must be queried before any other item is submitted
			const float grow = Anim(ImGui::GetItemID(), active || ImGui::IsItemHovered() ? 1.0f : 0.0f);
			const float pad = S(8);
			if (active) {
				const float t = std::clamp((ImGui::GetIO().MousePos.x - min.x - pad) / (size.x - 2 * pad), 0.0f, 1.0f);
				*a_value = a_min + t * (a_max - a_min);
			}
			const float t = std::clamp((*a_value - a_min) / (a_max - a_min), 0.0f, 1.0f);
			const float y = (min.y + max.y) * 0.5f;
			const float x = min.x + pad + t * (size.x - 2 * pad);
			auto        dl = ImGui::GetWindowDrawList();
			dl->AddLine({ min.x + pad, y }, { max.x - pad, y }, T().line, S(2));
			dl->AddLine({ min.x + pad, y }, { x, y }, T().accent, S(2));
			Diamond(dl, { x, y }, S(6) + S(2) * grow, Mix(T().bg, T().gold, 0.25f + grow * 0.5f), T().accent, S(1.5f));

			char buf[32];
			std::snprintf(buf, sizeof(buf), a_fmt, *a_value);
			ImGui::SameLine(0, S(10));
			ImGui::PushStyleColor(ImGuiCol_Text, T().accent);
			ImGui::TextUnformatted(buf);
			ImGui::PopStyleColor();

			static auto lastLiveApply = Clock::now();
			const auto  now = Clock::now();
			if (a_live && active && now - lastLiveApply > 200ms) {
				lastLiveApply = now;
				return true;
			}
			return released;
		}

		// Pill-shaped segmented control.
		bool Segmented(const char* a_id, int* a_value, std::initializer_list<const char*> a_keys)
		{
			std::vector<const char*> a_labels;
			for (auto k : a_keys) {
				a_labels.push_back(Tr(k));
			}
			bool changed = false;
			auto dl = ImGui::GetWindowDrawList();
			ImGui::PushID(a_id);
			const auto start = ImGui::GetCursorScreenPos();
			float      totalW = 0;
			for (auto l : a_labels) {
				totalW += ImGui::CalcTextSize(l).x + S(20);
			}
			const float h = ImGui::GetFrameHeight();
			dl->AddRectFilled(start, { start.x + totalW + S(4), start.y + h }, T().seg, S(2));
			dl->AddRect(start, { start.x + totalW + S(4), start.y + h }, T().line, S(2));

			// The selection pill slides between segments.
			float selX = 0, selW = 0, x = start.x + S(2);
			int   i = 0;
			for (auto l : a_labels) {
				const float w = ImGui::CalcTextSize(l).x + S(20);
				if (*a_value == i) {
					selX = x - start.x;
					selW = w;
				}
				x += w;
				++i;
			}
			const ImGuiID id = ImGui::GetID("##pill");
			const float   ax = Anim(id, selX, 18.0f);
			const float   aw = Anim(id + 7, selW, 18.0f);
			dl->AddRectFilled({ start.x + ax, start.y + S(2) }, { start.x + ax + aw, start.y + h - S(2) }, T().accent, S(1));

			ImGui::SetCursorScreenPos({ start.x + S(2), start.y });
			i = 0;
			for (auto l : a_labels) {
				const float w = ImGui::CalcTextSize(l).x + S(20);
				const auto  p = ImGui::GetCursorScreenPos();
				ImGui::PushID(i);
				if (ImGui::InvisibleButton("##seg", { w, h })) {
					*a_value = i;
					changed = true;
				}
				RecordControl(l);
				ImGui::PopID();
				const bool sel = *a_value == i;
				dl->AddText({ p.x + S(10), p.y + (h - ImGui::GetFontSize()) * 0.5f }, sel ? T().onAccent : T().text, l);
				ImGui::SameLine(0, 0);
				++i;
			}
			ImGui::NewLine();
			ImGui::PopID();
			return changed;
		}

		bool Button(const char* a_label, bool a_primary = false, float a_width = 0.0f)
		{
			a_label = Tr(a_label);
			ImGui::PushStyleColor(ImGuiCol_Button, a_primary ? T().accent : T().seg);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a_primary ? Mix(T().accent, T().gold, 0.35f) : T().line);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, T().gold);
			ImGui::PushStyleColor(ImGuiCol_Text, a_primary ? T().onAccent : T().text);
			const bool clicked = ImGui::Button(a_label, { a_width, D(36, 30) });
			ImGui::PopStyleColor(4);
			if (g_rowLabel.empty()) {
				RecordControl(a_label);  // free-standing button; inside a row the row label wins
			}
			return clicked;
		}

		// Small framed key name, e.g. [F5]. Draw-only. a_faint: an empty slot ("+ Key"), drawn lighter.
		void KeyChip(const std::string& a_key, bool a_highlight, bool a_faint = false)
		{
			const std::string a_text = Tr(a_key.c_str());
			PushFont(Overlay::GetFonts().caption);
			const auto  ts = ImGui::CalcTextSize(a_text.c_str());
			const float h = ImGui::GetFrameHeight();
			const auto  p = ImGui::GetCursorScreenPos();
			const float w = ts.x + S(14);
			auto        dl = ImGui::GetWindowDrawList();
			const float y0 = p.y + (h - ts.y - S(6)) * 0.5f;
			const bool  hovered = ImGui::IsMouseHoveringRect({ p.x, y0 }, { p.x + w, y0 + ts.y + S(6) });
			if (!a_faint || hovered) {
				dl->AddRectFilled({ p.x, y0 }, { p.x + w, y0 + ts.y + S(6) }, a_highlight ? Fade(T().accent, 0.15f) : T().seg, S(2));
			}
			dl->AddRect({ p.x, y0 }, { p.x + w, y0 + ts.y + S(6) }, a_highlight || (a_faint && hovered) ? T().accent : a_faint ? Fade(T().line, 0.7f) : T().line, S(2));
			dl->AddText({ p.x + S(7), y0 + S(3) }, a_highlight || (a_faint && hovered) ? T().accent : a_faint ? Fade(T().muted, 0.8f) : T().muted, a_text.c_str());
			ImGui::Dummy({ w, h });
			ImGui::PopFont();
		}

		// Pill with a label and a small cross; flows onto the next line when it does not fit.
		bool Chip(const char* a_label, bool& a_first)
		{
			a_label = Tr(a_label);
			const auto  ts = ImGui::CalcTextSize(a_label);
			const float h = S(30);
			const float w = ts.x + S(42);
			if (!a_first) {
				ImGui::SameLine(0, S(8));
				if (w > ImGui::GetContentRegionAvail().x) {
					ImGui::NewLine();
				}
			}
			a_first = false;
			const auto p = ImGui::GetCursorScreenPos();
			const bool clicked = ImGui::InvisibleButton(a_label, { w, h });
			const float hover = Anim(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f);
			auto        dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p, { p.x + w, p.y + h }, Mix(T().seg, T().hover, hover), S(2));
			dl->AddRect(p, { p.x + w, p.y + h }, Mix(T().line, T().accent, hover), S(2));
			dl->AddCircleFilled({ p.x + S(14), p.y + h * 0.5f }, S(3.5f), T().ok);
			dl->AddText({ p.x + S(24), p.y + (h - ts.y) * 0.5f }, T().text, a_label);
			DrawIcon(dl, Icon::kClose, { p.x + w - S(12), p.y + h * 0.5f }, S(11), Mix(T().muted, T().accent, hover));
			return clicked;
		}

		// Small gold icon before a card title, for the cards that have one.
		std::optional<Icon> CardIcon(std::string_view a_title)
		{
			static const std::unordered_map<std::string_view, Icon> icons = {
				{ "card.survival", Icon::kHelmet }, { "card.stamina_and_magicka", Icon::kFlask }, { "card.weightless_items", Icon::kChest },
				{ "card.transformations", Icon::kDragonHead }, { "card.abilities", Icon::kEye }, { "card.character_profiles", Icon::kScroll },
				{ "card.movement", Icon::kMap }, { "card.damage", Icon::kAxeSword }, { "skill.sneak", Icon::kEye }, { "feat.highlights_esp", Icon::kEye },
				{ "card.level_and_perks", Icon::kConstellation }, { "card.attributes", Icon::kHelmet }, { "common.skills", Icon::kConstellation },
				{ "card.learn_everything", Icon::kShout }, { "cat.soul_gems", Icon::kSoulGem }, { "card.crafting_stations_anywhere", Icon::kAnvil }, { "card.crafting", Icon::kHammer },
				{ "card.standing_stone", Icon::kRuneCircle }, { "card.resources", Icon::kCoin }, { "card.time", Icon::kMoons }, { "card.weather", Icon::kMoons },
				{ "card.guilds_and_titles", Icon::kKey }, { "card.followers_and_law", Icon::kUsers }, { "card.trade_and_loot", Icon::kCoin },
				{ "card.my_points", Icon::kMap }, { "card.all_magic", Icon::kRuneCircle }, { "skill.destruction", Icon::kFlame }, { "skill.alteration", Icon::kRuneCircle },
				{ "skill.restoration", Icon::kSparkles }, { "skill.conjuration", Icon::kRuneCircle }, { "skill.illusion", Icon::kEye },
				{ "card.quick_toggles", Icon::kSparkles }, { "card.active_now", Icon::kCheck }, { "card.recently_added", Icon::kChest },
				{ "card.quick_actions", Icon::kKey }, { "card.recent_actions", Icon::kScroll }, { "card.appearance", Icon::kDwemerGear },
				{ "card.controls", Icon::kKey }, { "card.about", Icon::kInfo }, { "card.knowledge_and_restrictions", Icon::kScroll }, { "card.custom_commands", Icon::kTilde },
				{ "card.hotkeys", Icon::kKey }, { "card.console_command", Icon::kTilde }, { "card.target_under_crosshair", Icon::kEye },
			};
			const auto it = icons.find(a_title);
			return it != icons.end() ? std::optional{ it->second } : std::nullopt;
		}

		// A card with ornamental corners; content goes between BeginCard/EndCard.
		void BeginCard(const char* a_title)
		{
			if (g_searching) {
				g_searchCard = a_title;
				g_searchCardShown = false;
				return;
			}
			ImGui::PushStyleColor(ImGuiCol_ChildBg, T().card);
			ImGui::PushStyleColor(ImGuiCol_Border, T().line);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { S(16), D(12, 8) });
			ImGui::BeginChild(a_title, { -FLT_MIN, 0 }, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
			const auto min = ImGui::GetWindowPos();
			const auto max = ImVec2{ min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight() };
			Corners(ImGui::GetWindowDrawList(), min, max, S(7), S(3), Fade(T().gold, 0.7f), 1.0f);

			if (const auto icon = CardIcon(a_title)) {
				const auto p = ImGui::GetCursorScreenPos();
				PushFont(Overlay::GetFonts().heading);
				const float h = ImGui::GetTextLineHeight();
				ImGui::PopFont();
				DrawIcon(ImGui::GetWindowDrawList(), *icon, { p.x + S(11), p.y + h * 0.55f }, S(20), T().gold);
				ImGui::SetCursorScreenPos({ p.x + S(30), p.y });
			}
			PushFont(Overlay::GetFonts().heading);
			ImGui::PushStyleColor(ImGuiCol_Text, T().title);
			ImGui::TextUnformatted(Upper(Tr(a_title)).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			ImGui::Spacing();
		}

		void EndCard()
		{
			if (g_searching) {
				return;
			}
			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			ImGui::Spacing();
		}

		void Muted(const char* a_text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
			ImGui::TextWrapped("%s", Tr(a_text));
			ImGui::PopStyleColor();
		}

		bool g_noBadges = false;  // the hotkey list shows keys itself
		// A second, small line under the next row's label: "навсегда" for actions that can't be undone, and the
		// current state ("3 из 9", "в браке с Лидией"...).
		struct RowNote
		{
			bool        permanent{ false };
			std::string state;
			bool        done{ false };  // a finished state: green, with a tick
		};
		RowNote g_rowNote;
		float   g_rowTextBottom = 0;  // below the label (and its note); the row ends no higher

		// Row highlight under the mouse: rows are drawn top to bottom, so last frame's rect is used.
		std::unordered_map<ImGuiID, ImRect> g_rowRects;
		ImGuiID                             g_rowId = 0;
		ImVec2                              g_rowMin;
		float                               g_rowRight = 0;

		// The note line under a row label.
		void NoteLine(const RowNote& a_note)
		{
			PushFont(Overlay::GetFonts().caption);
			auto        dl = ImGui::GetWindowDrawList();
			const auto  start = ImGui::GetCursorScreenPos();
			const float h = ImGui::GetTextLineHeight();
			float       x = start.x;
			if (a_note.permanent) {
				DrawIcon(dl, Icon::kWarn, { x + S(7), start.y + h * 0.5f }, S(13), T().accent);
				dl->AddText({ x + S(17), start.y }, T().accent, Tr("row.permanent"));
				x += S(17) + ImGui::CalcTextSize(Tr("row.permanent")).x + S(10);
			}
			if (!a_note.state.empty()) {
				if (a_note.done) {
					DrawIcon(dl, Icon::kCheck, { x + S(7), start.y + h * 0.5f }, S(13), T().ok);
					x += S(17);
				}
				const char* state = Tr(a_note.state.c_str());
				dl->AddText({ x, start.y }, a_note.done ? T().ok : T().muted, state);
				x += ImGui::CalcTextSize(state).x;
			}
			ImGui::Dummy({ std::max(1.0f, x - start.x), h });
			if (a_note.permanent && ImGui::IsItemHovered()) {
				g_help = "row.permanent_this_cannot_be_undone";
			}
			ImGui::PopFont();
		}

		// Row: label on the left, control on the right. Returns false when filtered out by search.
		bool BeginRow(const char* a_label, const char* a_help, float a_controlWidth)
		{
			if (!Matches(a_label)) {
				return false;
			}
			if (g_searching) {
				EmitSearchHeaders();
			}
			a_help = a_help ? Tr(a_help) : nullptr;
			g_rowLabel = a_label;
			const auto  rowStart = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x;
			const float rowRight = rowStart.x + width;  // right edge of this column, in screen coordinates
			g_rowId = ImGui::GetID(std::format("{}##row", a_label).c_str());
			g_rowMin = rowStart;
			g_rowRight = rowRight;
			if (const auto it = g_rowRects.find(g_rowId); it != g_rowRects.end()) {
				const bool  hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(it->second.Min, it->second.Max, false);
				const float t = Anim(g_rowId, hovered ? 1.0f : 0.0f, 18.0f);
				if (t > 0.01f) {
					// The game's list selection: a band that fades out to the right.
					const ImU32 col = Fade(T().hover, std::min(1.0f, t * 1.6f));
					ImGui::GetWindowDrawList()->AddRectFilledMultiColor({ it->second.Min.x - S(6), it->second.Min.y + S(1) }, { it->second.Max.x + S(6), it->second.Max.y },
						col, Fade(col, 0.2f), Fade(col, 0.2f), col);
				}
			}
			ImGui::GetWindowDrawList()->AddLine({ rowStart.x, rowStart.y }, { rowStart.x + width, rowStart.y }, T().line);
			ImGui::Dummy({ 0, D(6, 2) });
			ImGui::AlignTextToFramePadding();
			// Anything that has a hotkey action gets a key chip after its label: its key, or "+ Key" to bind one
			// (a click on the chip, or a right click on the name, waits for the key).
			const auto action = g_noBadges ? nullptr : Hotkeys::FindByName(a_label);
			const bool chip = action != nullptr;
			const auto note = std::exchange(g_rowNote, {});
			const float labelTop = ImGui::GetCursorScreenPos().y;
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - a_controlWidth - (chip ? S(140) : S(16)));
			ImGui::TextUnformatted(Tr(a_label));
			ImGui::PopTextWrapPos();
			if (ImGui::IsItemHovered()) {
				if (action) {
					g_help = Lang::Fmt("row.right_click_the_name_to", a_help ? a_help : "", a_help ? " · " : "");
				} else if (a_help) {
					g_help = a_help;
				}
				if (action && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
					Hotkeys::BeginCapture(action->id);
				}
			}
			if (action) {
				const bool capturing = Hotkeys::IsCapturing() && Hotkeys::CaptureTarget() == action->id;
				const auto key = Hotkeys::KeyOf(action->id);
				ImGui::SameLine(0, S(10));
				KeyChip(capturing ? "row.press_a_key" : key ? Hotkeys::KeyName(key) : "row.assign_key", capturing, !key && !capturing);
				if (ImGui::IsItemHovered()) {
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
					g_help = key ? "row.change_key_help" : "row.assign_key_help";
					if (!capturing && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
						Hotkeys::BeginCapture(action->id);
					}
				}
			}
			if (note.permanent || !note.state.empty()) {
				NoteLine(note);
			}
			g_rowTextBottom = ImGui::GetCursorScreenPos().y;
			// Screen coordinates: SameLine(x) inside a table column is relative to the column and doubles its offset.
			ImGui::SameLine();
			ImGui::SetCursorScreenPos({ rowRight - a_controlWidth, note.permanent || !note.state.empty() ? labelTop : ImGui::GetCursorScreenPos().y });
			return true;
		}

		void EndRow(const char* a_help)
		{
			if (!g_rowLabel.empty()) {
				RecordControl(g_rowLabel);
				g_rowLabel.clear();
			}
			if (a_help && ImGui::IsItemHovered()) {
				g_help = Tr(a_help);
			}
			if (ImGui::GetCursorScreenPos().y < g_rowTextBottom) {
				ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x, g_rowTextBottom });
			}
			g_rowTextBottom = 0;
			ImGui::Dummy({ 0, D(4, 1) });
			if (g_rowId) {
				g_rowRects[g_rowId] = ImRect{ g_rowMin, { g_rowRight, ImGui::GetCursorScreenPos().y } };
				g_rowId = 0;
			}
		}

		void SwitchRow(const char* a_label, bool* a_value, const char* a_help, const std::function<void(bool)>& a_onChange)
		{
			if (!BeginRow(a_label, a_help, S(44))) {
				return;
			}
			if (Switch(a_label, a_value) && a_onChange) {
				a_onChange(*a_value);
			}
			EndRow(a_help);
		}

		void SliderRow(const char* a_label, float* a_value, float a_min, float a_max, const char* a_fmt, const char* a_help, const std::function<void()>& a_onRelease,
			bool a_live = true)
		{
			if (!BeginRow(a_label, a_help, S(205))) {
				return;
			}
			if (Slider(a_label, a_value, a_min, a_max, a_fmt, S(145), a_live) && a_onRelease) {
				a_onRelease();
			}
			EndRow(a_help);
		}

		struct RowOptions
		{
			std::string state;           // shown before the button: "3 из 9", "в браке с Лидией"...
			bool        enabled{ true };
			bool        permanent{ false };  // can't be undone: badge + a second click to confirm
			bool        done{ false };       // the state is a finished one (green, with a tick)
		};

		constexpr const char* kConfirm = "row.sure_click_again";
		std::unordered_map<ImGuiID, Clock::time_point> g_armed;  // permanent buttons waiting for the second click

		// a_button may be null: then the row only shows its state.
		void ActionRowEx(const char* a_label, const char* a_button, const char* a_help, const std::function<void()>& a_action, const RowOptions& a_opt)
		{
			const float pad = ImGui::GetStyle().FramePadding.x * 2;
			a_button = a_button ? Tr(a_button) : nullptr;
			const float buttonW = !a_button ? 0.0f :
			                      a_opt.permanent ? std::max(ImGui::CalcTextSize(a_button).x, ImGui::CalcTextSize(Tr(kConfirm)).x) + pad :
			                                        ImGui::CalcTextSize(a_button).x + pad;
			g_rowNote = { a_opt.permanent, a_opt.state, a_opt.done };
			if (!BeginRow(a_label, a_help, buttonW)) {
				g_rowNote = {};
				return;
			}
			if (a_button) {
				ImGui::PushID(a_label);
				const ImGuiID id = ImGui::GetID("##armed");
				auto          armed = g_armed.find(id);
				if (armed != g_armed.end() && Since(armed->second) > 4.0f) {
					g_armed.erase(armed);
					armed = g_armed.end();
				}
				const bool isArmed = armed != g_armed.end();
				if (!a_opt.enabled) {
					ImGui::BeginDisabled();
				}
				if (isArmed) {
					ImGui::PushStyleColor(ImGuiCol_Button, Mix(T().accent, IM_COL32(200, 40, 30, 255), 0.5f));
				}
				const bool clicked = Button(isArmed ? kConfirm : a_button, true, a_opt.permanent ? buttonW : 0.0f);
				if (isArmed) {
					ImGui::PopStyleColor();
				}
				if (!a_opt.enabled) {
					ImGui::EndDisabled();
				}
				if (clicked) {
					if (!a_opt.permanent || isArmed) {
						g_armed.erase(id);
						a_action();
					} else {
						g_armed[id] = Clock::now();
					}
				}
				ImGui::PopID();
			} else {
				ImGui::Dummy({ 0, ImGui::GetFrameHeight() });
			}
			EndRow(a_help);
		}

		void ActionRow(const char* a_label, const char* a_button, const char* a_help, const std::function<void()>& a_action)
		{
			ActionRowEx(a_label, a_button, a_help, a_action, {});
		}

		// Two clicks, with the "навсегда" badge.
		void PermanentRow(const char* a_label, const char* a_button, const char* a_help, const std::function<void()>& a_action, RowOptions a_opt = {})
		{
			a_opt.permanent = true;
			ActionRowEx(a_label, a_button, a_help, a_action, a_opt);
		}

		// Sub-pages of a long page, as a segmented control under the title. While searching every tab is drawn (-1).
		std::unordered_map<std::string, int> g_tabs;

		int Tabs(const char* a_id, std::initializer_list<const char*> a_labels)
		{
			if (g_searching) {
				return -1;
			}
			int&      tab = g_tabs[a_id];
			const int before = tab;
			Segmented(a_id, &tab, a_labels);
			if (tab != before) {
				g_pageTime = Clock::now();
			}
			ImGui::Dummy({ 0, D(8, 4) });
			return tab;
		}

		bool Tab(int a_current, int a_index) { return a_current < 0 || a_current == a_index; }

		// Game state shown in rows, read at most once a second.
		struct Snapshot
		{
			std::pair<int, int> thane;
			bool                thievesGuild{ false };
			std::string         spouse;
			int                 stolen{ 0 };
			int                 emptyGems{ 0 };
			std::size_t         learned{ 0 };
			Clock::time_point   at{};
		};

		const Snapshot& Snap()
		{
			static Snapshot snap;
			if (Since(snap.at) > 1.0f && RE::PlayerCharacter::GetSingleton()) {
				snap.thane = Cheats::ThaneProgress();
				snap.thievesGuild = Cheats::InThievesGuild();
				auto spouse = Cheats::Spouse();
				snap.spouse = spouse ? Text::ToUtf8(spouse->GetName()) : std::string{};
				snap.stolen = Cheats::StolenCount();
				snap.emptyGems = Cheats::EmptySoulGemCount();
				snap.learned = Cheats::LearnedCount();
				snap.at = Clock::now();
			}
			return snap;
		}

		void RefreshSnapshot() { const_cast<Snapshot&>(Snap()).at = {}; }

		// Centred text broken into lines of at most a_width.
		void CenteredText(ImDrawList* a_dl, const char* a_text, float a_centerX, float a_y, float a_width, ImU32 a_col)
		{
			std::vector<std::string> lines;
			std::string              line;
			std::stringstream        words{ Tr(a_text) };
			for (std::string word; words >> word;) {
				const auto candidate = line.empty() ? word : line + " " + word;
				if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > a_width) {
					lines.push_back(line);
					line = word;
				} else {
					line = candidate;
				}
			}
			if (!line.empty()) {
				lines.push_back(line);
			}
			float y = a_y;
			for (const auto& l : lines) {
				const auto w = ImGui::CalcTextSize(l.c_str()).x;
				a_dl->AddText({ a_centerX - w * 0.5f, y }, a_col, l.c_str());
				y += ImGui::GetTextLineHeight();
			}
		}

		// Two-column card layout (a single column while searching).
		template <class L, class R>
		void Columns(L&& a_left, R&& a_right)
		{
			if (g_searching) {
				a_left();
				a_right();
				return;
			}
			if (ImGui::BeginTable("##cols", 2, ImGuiTableFlags_SizingStretchSame)) {
				ImGui::TableNextColumn();
				a_left();
				ImGui::TableNextColumn();
				a_right();
				ImGui::EndTable();
			}
		}

		// Horizontal bar with a caption and "current / max" above it.
		void Bar(const char* a_label, float a_cur, float a_max, ImU32 a_col)
		{
			const float w = ImGui::GetContentRegionAvail().x;
			PushFont(Overlay::GetFonts().caption);
			ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
			ImGui::TextUnformatted(Tr(a_label));
			const auto value = std::format("{:.0f} / {:.0f}", a_cur, a_max);
			ImGui::SameLine(ImGui::GetCursorPosX() + w - ImGui::CalcTextSize(value.c_str()).x);
			ImGui::TextUnformatted(value.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();

			const auto  p = ImGui::GetCursorScreenPos();
			const float h = S(8);
			const float t = a_max > 0 ? std::clamp(a_cur / a_max, 0.0f, 1.0f) : 0.0f;
			const float shown = Anim(ImGui::GetID(a_label), t, 8.0f);
			auto        dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p, { p.x + w, p.y + h }, T().seg, h * 0.5f);
			if (shown > 0.005f) {
				dl->AddRectFilled(p, { p.x + std::max(h, w * shown), p.y + h }, Fade(a_col, g_alpha), h * 0.5f);
			}
			ImGui::Dummy({ w, h + S(8) });
		}

		// ---------------------------------------------------------------- form browser

		enum class View
		{
			kAll,
			kFavorites,
			kRecent
		};

		struct Browser
		{
			std::string      search;
			int              category{ -1 };  // -1 = all allowed
			int              plugin{ -1 };    // -1 = all plugins
			int              count{ 1 };
			int              view{ 0 };       // View
			std::vector<int> filtered;
			bool             dirty{ true };
		};

		Browser g_items;
		Browser g_magic;
		Browser g_actors;

		constexpr int kBulkLimit = 3000;

		const char* GrantVerb(Catalog::Group a_group)
		{
			return a_group == Catalog::Group::kMagic ? "items.learned" : a_group == Catalog::Group::kActors ? "items.summoned" : "items.received";
		}

		void GrantOne(const Catalog::Entry& a_entry, int a_count)
		{
			const auto group = Catalog::GroupOf(a_entry.category);
			Cheats::Grant(a_entry.formID, a_count);
			Settings::AddRecent(Settings::Key(a_entry.plugin, a_entry.formID));
			Status(std::format("{}: {}{}", Tr(GrantVerb(group)), a_entry.name, group == Catalog::Group::kMagic ? "" : std::format(" ×{}", a_count)));
		}

		// User item sets, made from the current favourites.
		void MyKits(const Browser& a_b)
		{
			static std::string name;
			static bool        naming = false;
			auto&              kits = Settings::Get().kits;

			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr("items.my_kits"));
			ImGui::SameLine();
			for (int i = 0; i < static_cast<int>(kits.size()); ++i) {
				ImGui::PushID(i);
				if (Button(kits[i].name.c_str(), true)) {
					int given = 0;
					for (const auto& [key, count] : kits[i].items) {
						if (auto entry = FindByKey(key)) {
							Cheats::Grant(entry->formID, count);
							++given;
						}
					}
					Status(Lang::Fmt("items.kit_items_added", kits[i].name, given));
				}
				if (ImGui::IsItemHovered()) {
					g_help = Lang::Fmt("items.items_right_click_to_delete", kits[i].items.size());
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
						kits.erase(kits.begin() + i);
						Settings::Save();
						ImGui::PopID();
						break;
					}
				}
				ImGui::PopID();
				ImGui::SameLine();
			}
			if (!naming) {
				if (Button("items.from_favorites")) {
					naming = true;
				}
				if (ImGui::IsItemHovered()) {
					g_help = "items.saves_every_item_in_favorites";
				}
			} else {
				ImGui::SetNextItemWidth(S(220));
				const bool enter = ImGui::InputTextWithHint("##kitname", Tr("items.kit_name"), &name, ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if ((Button("common.save", true) || enter) && !name.empty()) {
					Settings::Kit kit{ name, {} };
					EnsureKeys();
					for (const auto& key : Settings::Get().favorites) {
						if (auto entry = FindByKey(key); entry && Catalog::GroupOf(entry->category) == Catalog::Group::kItems) {
							kit.items.emplace_back(key, a_b.count);
						}
					}
					if (kit.items.empty()) {
						Status("items.no_favorite_items_mark_them");
					} else {
						Status(Lang::Fmt("items.kit_saved_items", name, kit.items.size()));
						kits.push_back(std::move(kit));
						Settings::Save();
					}
					name.clear();
					naming = false;
				}
				ImGui::SameLine();
				if (Button("common.cancel")) {
					naming = false;
				}
			}
			ImGui::NewLine();
		}

		void DrawBrowser(Browser& a_b, Catalog::Group a_group)
		{
			const auto& forms = Catalog::Forms();
			const auto& plugins = Catalog::Plugins();
			const bool  magic = a_group == Catalog::Group::kMagic;
			const bool  actors = a_group == Catalog::Group::kActors;
			auto&       favorites = Settings::Get().favorites;
			EnsureKeys();

			if (Segmented("view", &a_b.view, { "items.all", "items.favorites", "items.recent" })) {
				a_b.dirty = true;
			}
			ImGui::SameLine(0, S(12));
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
			a_b.dirty |= ImGui::InputTextWithHint("##q", Tr("items.name"), &a_b.search);

			const float filtersW = ImGui::GetContentRegionAvail().x - (magic ? 0.0f : S(170)) - ImGui::GetStyle().ItemSpacing.x * 2;
			ImGui::SetNextItemWidth(filtersW * 0.4f);
			const char* catPreview = a_b.category < 0 ? Tr("items.all_categories") : Catalog::CategoryName(static_cast<Catalog::Category>(a_b.category));
			if (ImGui::BeginCombo("##cat", catPreview)) {
				if (ImGui::Selectable(Tr("items.all_categories"), a_b.category < 0)) {
					a_b.category = -1;
					a_b.dirty = true;
				}
				for (int i = 0; i < static_cast<int>(Catalog::Category::kTotal); ++i) {
					if (Catalog::GroupOf(static_cast<Catalog::Category>(i)) != a_group) {
						continue;
					}
					if (ImGui::Selectable(Catalog::CategoryName(static_cast<Catalog::Category>(i)), a_b.category == i)) {
						a_b.category = i;
						a_b.dirty = true;
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(filtersW * 0.6f);
			const char* modPreview = a_b.plugin < 0 ? Tr("items.all_mods") : plugins[a_b.plugin].c_str();
			if (ImGui::BeginCombo("##mod", modPreview, ImGuiComboFlags_HeightLarge)) {
				if (ImGui::Selectable(Tr("items.all_mods"), a_b.plugin < 0)) {
					a_b.plugin = -1;
					a_b.dirty = true;
				}
				for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
					if (ImGui::Selectable(plugins[i].c_str(), a_b.plugin == i)) {
						a_b.plugin = i;
						a_b.dirty = true;
					}
				}
				ImGui::EndCombo();
			}

			if (!magic) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(S(130));
				ImGui::InputInt("##count", &a_b.count);
				a_b.count = std::clamp(a_b.count, 1, actors ? 20 : 100000);
				ImGui::SameLine();
				ImGui::TextUnformatted(Tr("items.qty"));
			}

			if (a_group == Catalog::Group::kItems) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(Tr("items.kits"));
				ImGui::SameLine();
				const std::pair<const char*, Cheats::Kit> kits[] = {
					{ "items.smithing_materials", Cheats::Kit::kSmithing },
					{ "items.all_ingredients", Cheats::Kit::kIngredients },
					{ "items.filled_soul_gems", Cheats::Kit::kSoulGems },
					{ "items.lockpicks", Cheats::Kit::kLockpicks },
				};
				for (const auto& [label, kit] : kits) {
					if (Button(label)) {
						Cheats::GiveKit(kit, a_b.count);
						Status(Lang::Fmt("items.kit_added_to_inventory_from", Tr(label), a_b.count));
					}
					if (ImGui::IsItemHovered()) {
						g_help = "items.adds_every_item_of_the";
					}
					ImGui::SameLine();
				}
				ImGui::NewLine();
				MyKits(a_b);
			}

			if (a_b.dirty) {
				a_b.dirty = false;
				a_b.filtered.clear();
				const auto needle = Text::ToLower(a_b.search);
				auto       accept = [&](int i) {
                    const auto& e = forms[i];
                    return Catalog::GroupOf(e.category) == a_group &&
                           (a_b.category < 0 || static_cast<int>(e.category) == a_b.category) &&
                           (a_b.plugin < 0 || e.plugin == plugins[a_b.plugin]) &&
                           Text::Contains(e.nameLower, needle);
				};
				if (a_b.view == static_cast<int>(View::kRecent)) {
					for (const auto& key : Settings::Get().recent) {
						if (const auto it = g_keyIndex.find(key); it != g_keyIndex.end() && accept(it->second)) {
							a_b.filtered.push_back(it->second);
						}
					}
				} else {
					const bool favOnly = a_b.view == static_cast<int>(View::kFavorites);
					for (int i = 0; i < static_cast<int>(forms.size()); ++i) {
						if ((!favOnly || favorites.contains(g_keys[i])) && accept(i)) {
							a_b.filtered.push_back(i);
						}
					}
				}
			}

			ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(Tr("items.found"), static_cast<int>(a_b.filtered.size()));
			ImGui::PopStyleColor();

			// Bulk grant (e.g. "all shouts", "every item of this mod"). Asks for a second click.
			if (!actors && !a_b.filtered.empty()) {
				static Browser* armed = nullptr;
				const int       n = static_cast<int>(a_b.filtered.size());
				ImGui::SameLine();
				const auto label = armed == &a_b ? Lang::Fmt("items.sure_click_again", n) :
				                                   Lang::Fmt("items.all_found", Tr(magic ? "common.learn" : "items.add"), n);
				if (n > kBulkLimit) {
					ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
					ImGui::Text(Tr("items.narrow_the_search_to_to"), kBulkLimit);
					ImGui::PopStyleColor();
				} else if (Button(label.c_str(), armed == &a_b)) {
					if (armed == &a_b) {
						for (int idx : a_b.filtered) {
							Cheats::Grant(forms[idx].formID, a_b.count);
						}
						Status(Lang::Fmt("items.different_forms", Tr(magic ? "items.learned" : "items.added"), n));
						armed = nullptr;
					} else {
						armed = &a_b;
					}
				}
			}

			if (a_b.filtered.empty() && a_b.view != static_cast<int>(View::kAll)) {
				Muted(a_b.view == static_cast<int>(View::kFavorites) ?
				          "items.favorites_is_empty_click_the" :
				          "items.items_you_recently_added_or");
			}

			constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##forms", 5, flags, { -FLT_MIN, -FLT_MIN })) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(30));
				ImGui::TableSetupColumn(Tr("common.name"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn(Tr("items.category"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
				ImGui::TableSetupColumn(Tr("items.mod"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, actors ? S(330) : S(110));
				ImGui::TableHeadersRow();

				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(a_b.filtered.size()));
				while (clipper.Step()) {
					for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
						const int   idx = a_b.filtered[row];
						const auto& e = forms[idx];
						const auto& key = g_keys[idx];
						ImGui::PushID(row);
						ImGui::TableNextRow();

						ImGui::TableNextColumn();
						{
							const bool  fav = favorites.contains(key);
							const float h = ImGui::GetFrameHeight();
							const auto  p = ImGui::GetCursorScreenPos();
							if (ImGui::InvisibleButton("##fav", { S(26), h })) {
								if (fav) {
									favorites.erase(key);
								} else {
									favorites.insert(key);
								}
								Settings::Save();
								a_b.dirty |= a_b.view == static_cast<int>(View::kFavorites);
							}
							const bool hovered = ImGui::IsItemHovered();
							if (hovered) {
								g_help = fav ? "items.remove_from_favorites" : "items.add_to_favorites_find_it";
							}
							Star(ImGui::GetWindowDrawList(), { p.x + S(13), p.y + h * 0.5f }, S(8), fav || hovered ? T().gold : T().line, fav);
						}

						ImGui::TableNextColumn();
						ImGui::AlignTextToFramePadding();
						if (const float h = ImGui::GetFrameHeight(), size = S(20); !actors) {
							const auto p = ImGui::GetCursorScreenPos();
							if (ItemIcons::Draw(ImGui::GetWindowDrawList(), e.formID, { p.x, p.y + (h - size) * 0.5f }, size, T().text)) {
								ImGui::SetCursorScreenPos({ p.x + size + S(8), p.y });
							}
						}
						ImGui::TextUnformatted(e.name.c_str());
						ImGui::TableNextColumn();
						ImGui::TextUnformatted(Catalog::CategoryName(e.category));
						ImGui::TableNextColumn();
						ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
						ImGui::TextUnformatted(e.plugin.c_str());
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered()) {
							g_help = std::format("{} · ID {:08X}", e.plugin, e.formID);
						}
						ImGui::TableNextColumn();
						if (Button(magic ? "common.learn" : actors ? "common.summon" : "items.add", true)) {
							GrantOne(e, a_b.count);
							a_b.dirty |= a_b.view == static_cast<int>(View::kRecent);
						}
						if (actors) {
							for (const bool bring : { false, true }) {
								ImGui::SameLine(0, S(6));
								if (Button(bring ? "items.bring" : "items.go_to")) {
									if (!Cheats::MoveToNpc(e.formID, bring)) {
										Status(Lang::Fmt("items.is_not_in_the_world", e.name));
									} else if (!bring) {
										Input::SetMenuOpen(false);
									} else {
										Status(Lang::Fmt("items.moved_to_the_dragonborn", e.name));
									}
								}
								if (ImGui::IsItemHovered()) {
									g_help = bring ? "items.moves_this_npc_not_a" : "items.moves_the_dragonborn_to_this";
								}
							}
						}
						ImGui::PopID();
					}
				}
				ImGui::EndTable();
			}
		}

		// ---------------------------------------------------------------- pages

		void TransformCard()
		{
			BeginCard("card.transformations");
			ActionRow("dovahkiin.beast_form_werewolf", "dovahkiin.give_power", "dovahkiin.grants_the_beast_form_power", [] {
				Cheats::GiveBeastForm();
				Status("dovahkiin.beast_form_power_added_magic");
			});
			ActionRow("dovahkiin.cure_vampirism", "common.cure", "dovahkiin.removes_vampirism_just_like_falion", [] {
				Status(Cheats::CureVampirism() ? "dovahkiin.vampirism_cured" : "dovahkiin.failed_vampirism_quest_not_found");
			});
			PermanentRow("dovahkiin.become_a_vampire", "dovahkiin.turn", "dovahkiin.the_dragonborn_becomes_a_vampire", [] {
				Status(Cheats::BecomeVampire() ? "dovahkiin.the_dragonborn_is_now_a" : "dovahkiin.failed_vampirism_quest_not_found");
			});
			ActionRow("dovahkiin.cure_lycanthropy", "common.cure", "dovahkiin.removes_the_beast_blood_like", [] {
				Status(Cheats::CureLycanthropy() ? "dovahkiin.lycanthropy_cured" : "dovahkiin.failed_companions_quest_not_found");
			});
			ActionRow("dovahkiin.vampire_lord", "dovahkiin.give_power", "dovahkiin.grants_the_vampire_lord_power", [] {
				Status(Cheats::GiveVampireLord() ? "dovahkiin.vampire_lord_power_added_magic" : "dovahkiin.dawnguard_esm_is_missing");
			});
			EndCard();
		}

		// Character profiles (like Proteus): the current character to a file, a file onto the current character.
		void ProfilesCard()
		{
			static std::string name;
			static bool        withItems = true;
			static auto        profiles = Cheats::ProfileInfos();
			static auto        loaded = Clock::now();
			if (Since(loaded) > 2.0f) {  // profiles saved elsewhere (another save, the bridge) show up
				profiles = Cheats::ProfileInfos();
				loaded = Clock::now();
			}
			BeginCard("card.character_profiles");
			if (!g_searching) {
				ImGui::SetNextItemWidth(S(320));
				const bool enter = ImGui::InputTextWithHint("##profile", Tr("profiles.profile_name"), &name, ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if (Button("profiles.save_character", true) || enter) {
					Status(name.empty() ? "profiles.enter_a_name_first" : Cheats::SaveProfile(name));
					profiles = Cheats::ProfileInfos();
					name.clear();
				}
				if (ImGui::IsItemHovered()) {
					g_help = "profiles.saves_appearance_race_level_skills";
				}
			}
			SwitchRow("profiles.transfer_items", &withItems, "profiles.when_applying_the_profile_also", nullptr);
			if (g_searching) {
				for (std::size_t i = 0; i < profiles.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					PermanentRow(profiles[i].name.c_str(), "common.apply", "profiles.makes_the_current_character_the",
						[i] { Status(Cheats::LoadProfile(profiles[i].name, withItems)); });
					ImGui::PopID();
				}
				EndCard();
				return;
			}
			if (profiles.empty()) {
				Muted("profiles.no_profiles_yet_enter_a");
				EndCard();
				return;
			}

			// Two tiles per line: name, who it is, when saved, Apply / Delete.
			ImGui::Dummy({ 0, S(4) });
			const float gap = S(12);
			const float w = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
			const float h = D(134, 118);
			const auto  origin = ImGui::GetCursorScreenPos();
			auto        dl = ImGui::GetWindowDrawList();
			std::optional<std::size_t> remove;
			for (std::size_t i = 0; i < profiles.size(); ++i) {
				const auto& info = profiles[i];
				const ImVec2 p{ origin.x + (i % 2) * (w + gap), origin.y + (i / 2) * (h + gap) };
				const ImVec2 max{ p.x + w, p.y + h };
				const bool   hovered = ImGui::IsMouseHoveringRect(p, max) && ImGui::IsWindowHovered();
				ImGui::PushID(static_cast<int>(i));
				const float hover = Anim(ImGui::GetID("##tile"), hovered ? 1.0f : 0.0f);
				dl->AddRectFilled(p, max, Mix(T().seg, T().hover, hover), S(4));
				dl->AddRect(p, max, Mix(T().line, T().gold, hover), S(4));
				DrawIcon(dl, Icon::kHelmet, { p.x + S(26), p.y + S(28) }, S(26), T().gold);
				PushFont(Overlay::GetFonts().heading);
				dl->AddText({ p.x + S(50), p.y + S(12) }, T().title, info.name.c_str());
				ImGui::PopFont();
				PushFont(Overlay::GetFonts().caption);
				const auto who = Lang::Fmt("profiles.lvl_perks", Text::ToUtf8(info.character.c_str()), info.race.empty() ? "" : " · ",
					Text::ToUtf8(info.race.c_str()), info.level, info.perks);
				dl->AddText({ p.x + S(50), p.y + S(44) }, T().muted, who.c_str());
				if (!info.saved.empty()) {
					dl->AddText({ p.x + S(50), p.y + S(62) }, T().muted, Lang::Fmt("profiles.saved", info.saved).c_str());
				}
				ImGui::PopFont();

				// Buttons along the bottom of the tile.
				ImGui::SetCursorScreenPos({ p.x + S(14), max.y - D(36, 30) - S(12) });
				const ImGuiID applyId = ImGui::GetID("##apply");
				const bool    armed = g_armed.contains(applyId) && Since(g_armed[applyId]) < 4.0f;
				if (Button(armed ? kConfirm : "common.apply", true)) {
					if (armed) {
						g_armed.erase(applyId);
						Status(Cheats::LoadProfile(info.name, withItems));
					} else {
						g_armed[applyId] = Clock::now();
					}
				}
				if (ImGui::IsItemHovered()) {
					g_help = "profiles.makes_the_current_character_the_2";
				}
				ImGui::SameLine(0, S(8));
				const ImGuiID deleteId = ImGui::GetID("##delete");
				const bool    deleteArmed = g_armed.contains(deleteId) && Since(g_armed[deleteId]) < 4.0f;
				if (Button(deleteArmed ? "common.delete" : "common.delete_2")) {
					if (deleteArmed) {
						g_armed.erase(deleteId);
						remove = i;
					} else {
						g_armed[deleteId] = Clock::now();
					}
				}
				if (ImGui::IsItemHovered()) {
					g_help = "profiles.deletes_the_profile_file_press";
				}
				ImGui::PopID();
			}
			const auto rows = (profiles.size() + 1) / 2;
			ImGui::SetCursorScreenPos({ origin.x, origin.y + rows * (h + gap) - gap });
			ImGui::Dummy({ 0, S(4) });
			if (remove) {
				Cheats::DeleteProfile(profiles[*remove].name);
				Status(Lang::Fmt("profiles.profile_deleted", profiles[*remove].name));
				profiles = Cheats::ProfileInfos();
			}
			EndCard();
		}

		void PageDovahkiin()
		{
			auto&     st = Cheats::Get();
			const int tab = Tabs("dovahkiin", { "dovahkiin.general", "card.weightless_items", "card.character_profiles" });
			if (Tab(tab, 0)) {
				Columns(
					[&] {
						BeginCard("card.survival");
						bool god = Cheats::IsGodMode();
						SwitchRow("feat.god_mode", &god, "dovahkiin.no_damage_is_taken_stamina",
							[](bool v) { Cheats::SetGodMode(v); });
						SwitchRow("feat.infinite_health", &st.infiniteHealth, "dovahkiin.health_damage_never_gets_through", nullptr);
						SwitchRow("feat.no_fall_damage", &st.noFallDamage, "dovahkiin.jump_from_any_height_like",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.can_t_die", &st.essential,
							"dovahkiin.damage_is_taken_as_usual",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.disease_and_paralysis_immunity", &st.immunity, "dovahkiin.diseases_don_t_stick_and",
							[](bool) { Cheats::ApplyPersistent(); });
						ActionRow("feat.full_heal", "common.cure", "dovahkiin.restores_health_magicka_and_stamina", [] {
							Cheats::HealAll();
							Status("dovahkiin.the_dragonborn_is_fully_healed");
						});
						ActionRow("dovahkiin.change_appearance_and_race", "common.open", "dovahkiin.character_editor_showracemenu_face_name", [] {
							Input::SetMenuOpen(false);
							Cheats::OpenRaceMenuKeepingSkills();
						});
						EndCard();
					},
					[&] {
						BeginCard("card.stamina_and_magicka");
						SwitchRow("feat.infinite_stamina", &st.infiniteStamina, "dovahkiin.sprinting_power_attacks_and_blocking", nullptr);
						SwitchRow("feat.infinite_magicka", &st.infiniteMagicka, "dovahkiin.magicka_always_refills_to_the", nullptr);
						SliderRow("dovahkiin.health_regen", &st.healthRegen, 0.5f, 50.0f, "%.1f", "dovahkiin.how_fast_the_dragonborn_s",
							[] { Cheats::ApplyPersistent(); });
						SliderRow("dovahkiin.stamina_regen", &st.staminaRegen, 0.5f, 50.0f, "%.1f", "dovahkiin.how_fast_stamina_regenerates_1",
							[] { Cheats::ApplyPersistent(); });
						SwitchRow("feat.no_weight_limit", &st.noCarryLimit, "dovahkiin.carry_weight_becomes_100_000",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.free_spells", &st.noSpellCost, "dovahkiin.spells_of_every_school_cost",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.instant_casting", &st.instantCast, "dovahkiin.spells_need_no_charging_and",
							[](bool) { Cheats::ApplyPersistent(); });
						ActionRow("feat.dismiss_summons", "dovahkiin.dismiss", "dovahkiin.removes_all_summoned_creatures_and", [] {
							Cheats::DismissSummons();
							Status("dovahkiin.summons_dismissed");
						});
						EndCard();
					});

				TransformCard();

				BeginCard("card.abilities");
				Columns(
					[&] {
						SwitchRow("feat.night_eye", &st.nightEye, "dovahkiin.permanent_night_eye_like_the",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.water_walking", &st.waterWalking, "dovahkiin.the_dragonborn_walks_on_water", [](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.waterbreathing", &st.waterBreathing, "dovahkiin.air_never_runs_out_underwater", [](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.invisibility", &st.invisibility, "dovahkiin.the_dragonborn_is_invisible_while",
							[](bool) { Cheats::ApplyPersistent(); });
					},
					[&] {
						SwitchRow("feat.infinite_torches", &st.infiniteTorch, "dovahkiin.torches_never_burn_out_a",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.permanent_blessings", &st.eternalBuffs, "dovahkiin.shrine_blessings_rested_and_other",
							nullptr);
						SwitchRow("feat.no_power_cooldown", &st.powersNoCooldown,
							"dovahkiin.greater_powers_once_a_day",
							[](bool) { Cheats::ApplyPersistent(); });
					});
				EndCard();
			}
			if (Tab(tab, 1)) {
				BeginCard("card.weightless_items");
				const auto apply = [](bool) { Cheats::ApplyPersistent(); };
				Columns(
					[&] {
						SwitchRow("feat.weightless_books", &st.weightlessBooks, "dovahkiin.books_notes_letters_spell_tomes", apply);
						SwitchRow("feat.weightless_potions_and_poisons", &st.weightlessPotions, "dovahkiin.all_potions_and_poisons_including", apply);
						SwitchRow("feat.weightless_food", &st.weightlessFood, "dovahkiin.food_and_drinks", apply);
						SwitchRow("feat.weightless_ingredients", &st.weightlessIngredients, "dovahkiin.alchemy_ingredients", apply);
						SwitchRow("feat.weightless_scrolls", &st.weightlessScrolls, "dovahkiin.spell_scrolls", apply);
						SwitchRow("feat.weightless_soul_gems", &st.weightlessSoulGems, "dovahkiin.empty_and_filled_soul_gems", apply);
					},
					[&] {
						SwitchRow("feat.weightless_resources", &st.weightlessCrafting, "dovahkiin.ingots_ore_hides_leather_firewood", apply);
						SwitchRow("feat.weightless_gems", &st.weightlessGems, "dovahkiin.precious_gems", apply);
						SwitchRow("feat.weightless_clutter", &st.weightlessMiscItems, "dovahkiin.other_misc_items_dishes_tools", apply);
						SwitchRow("feat.weightless_arrows", &st.weightlessAmmo, "dovahkiin.arrows_and_bolts_they_have", apply);
						SwitchRow("feat.weightless_weapons", &st.weightlessWeapons, "dovahkiin.all_weapons_including_staves", apply);
						SwitchRow("feat.weightless_armor", &st.weightlessArmor, "dovahkiin.armor_clothing_rings_amulets_circlets", apply);
					});
				EndCard();
			}
			if (Tab(tab, 2)) {
				ProfilesCard();
			}
		}

		void EspCard()
		{
			auto& st = Cheats::Get();
			BeginCard("feat.highlights_esp");
			SwitchRow("feat.highlights_esp", &st.esp, "esp.labels_with_name_and_distance", nullptr);
			SliderRow("esp.highlight_range_m", &st.espRange, 10.0f, 300.0f, "%.0f", "esp.how_far_to_search", nullptr);
			Columns(
				[&] {
					SwitchRow("esp.enemies", &st.espEnemies, "esp.red_diamonds", nullptr);
					SwitchRow("esp.neutral_npcs_and_animals", &st.espNpcs, "esp.light_diamonds", nullptr);
					SwitchRow("esp.followers", &st.espFollowers, "esp.green_diamonds", nullptr);
					SwitchRow("esp.corpses", &st.espCorpses, "esp.grey_diamonds_something_to_search", nullptr);
				},
				[&] {
					SwitchRow("esp.chests_and_containers", &st.espContainers,
						"esp.gold_squares_non_empty_only", nullptr);
					SwitchRow("esp.items_on_the_ground", &st.espItems, "esp.blue_circles_red_frame_owned", nullptr);
					SwitchRow("esp.plants_and_ore", &st.espFlora, "esp.teal_triangles_unharvested_plants_and", nullptr);
					SwitchRow("esp.doors_and_switches", &st.espInteract, "esp.purple_squares_doors_switches", nullptr);
					SwitchRow("esp.traps", &st.espTraps, "esp.orange_triangles_traps", nullptr);
				});
			SubHeader("esp.creature_wallhack");
			Columns(
				[&] {
					SwitchRow("esp.boxes", &st.whBoxes, "esp.a_box_around_each_creature", nullptr);
					SwitchRow("esp.skeleton", &st.whSkeleton, "esp.lines_along_the_model_s", nullptr);
					SwitchRow("esp.health_bar", &st.whHealth, "esp.a_vertical_bar_left_of", nullptr);
				},
				[&] {
					SwitchRow("esp.tracer_lines", &st.whSnaplines, "esp.a_thin_line_from_the", nullptr);
					SwitchRow("esp.level_and_behind_wall", &st.whInfo, "esp.below_the_box_level_distance", nullptr);
					SwitchRow("esp.glow_through_walls", &st.whGlow,
						"esp.the_models_themselves_glow_through", nullptr);
				});
			EndCard();
		}

		// ESP look (per user, in DragonbornsCodex.ini): sizes, opacity, what the labels say, colour and shape per kind.
		constexpr const char* kEspLookLabels[Settings::kEspKinds] = { "esp.look_enemy", "esp.look_npc", "esp.look_follower", "esp.look_corpse",
			"esp.look_container", "esp.look_item", "esp.look_flora", "esp.look_quest", "esp.look_interact", "esp.look_trap", "esp.look_puzzle" };
		constexpr ImU32 kEspPalette[] = { IM_COL32(232, 72, 60, 255), IM_COL32(255, 120, 40, 255), IM_COL32(255, 210, 60, 255), IM_COL32(222, 178, 82, 255),
			IM_COL32(110, 205, 105, 255), IM_COL32(95, 205, 190, 255), IM_COL32(105, 170, 240, 255), IM_COL32(70, 100, 235, 255),
			IM_COL32(185, 140, 245, 255), IM_COL32(240, 110, 200, 255), IM_COL32(236, 222, 190, 255), IM_COL32(150, 150, 150, 220) };
		bool  g_espLookDirty = false;  // saved once the mouse is let go, not on every frame of a drag
		float g_espOpacityPct = -1.0f;

		void EspKindRow(std::size_t a_kind)
		{
			auto& look = Settings::Get().esp;
			const float box = ImGui::GetFrameHeight();
			if (!BeginRow(kEspLookLabels[a_kind], "esp.look_kind_help", box * 1.5f + S(10) + box * 5)) {
				return;
			}
			ImGui::PushID(static_cast<int>(a_kind));
			auto dl = ImGui::GetWindowDrawList();
			ImU32& color = look.colors[a_kind];

			// colour swatch, opens the picker
			if (ImGui::InvisibleButton("##colour", { box * 1.5f, box })) {
				ImGui::OpenPopup("##picker");
			}
			const auto min = ImGui::GetItemRectMin();
			const auto max = ImGui::GetItemRectMax();
			dl->AddRectFilled({ min.x + S(3), min.y + S(3) }, { max.x - S(3), max.y - S(3) }, color, S(3));
			dl->AddRect({ min.x + S(3), min.y + S(3) }, { max.x - S(3), max.y - S(3) }, ImGui::IsItemHovered() ? T().accent : T().line, S(3), 0, S(1.5f));
			if (ImGui::BeginPopup("##picker")) {
				for (std::size_t i = 0; i < std::size(kEspPalette); ++i) {
					ImGui::PushID(static_cast<int>(i));
					if (i % 6) {
						ImGui::SameLine(0, S(4));
					}
					if (ImGui::InvisibleButton("##preset", { box, box })) {
						color = kEspPalette[i];
						g_espLookDirty = true;
					}
					const auto pmin = ImGui::GetItemRectMin();
					const auto pmax = ImGui::GetItemRectMax();
					ImGui::GetWindowDrawList()->AddRectFilled(pmin, pmax, kEspPalette[i], S(3));
					if (ImGui::IsItemHovered()) {
						ImGui::GetWindowDrawList()->AddRect(pmin, pmax, T().accent, S(3), 0, S(2));
					}
					ImGui::PopID();
				}
				ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
				ImGui::SetNextItemWidth(S(240));
				if (ImGui::ColorPicker4("##custom", &c.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs)) {
					color = ImGui::ColorConvertFloat4ToU32(c);
					g_espLookDirty = true;
				}
				if (Button("esp.look_default_colour")) {
					color = Settings::kEspDefaultColors[a_kind];
					g_espLookDirty = true;
				}
				ImGui::EndPopup();
			}

			// shapes, drawn by the ESP itself
			ImGui::SameLine(0, S(10));
			for (int shape = 0; shape < 5; ++shape) {
				ImGui::PushID(shape);
				if (shape) {
					ImGui::SameLine(0, 0);
				}
				if (ImGui::InvisibleButton("##shape", { box, box })) {
					look.shapes[a_kind] = shape;
					g_espLookDirty = true;
				}
				const auto smin = ImGui::GetItemRectMin();
				const auto smax = ImGui::GetItemRectMax();
				const bool chosen = look.shapes[a_kind] == shape;
				if (chosen || ImGui::IsItemHovered()) {
					dl->AddRect({ smin.x + S(2), smin.y + S(2) }, { smax.x - S(2), smax.y - S(2) }, chosen ? T().accent : T().line, S(3), 0, S(1.5f));
				}
				Esp::DrawShape(dl, { (smin.x + smax.x) * 0.5f, (smin.y + smax.y) * 0.5f }, box * 0.24f, chosen ? color : Fade(color, 0.45f), shape);
				ImGui::PopID();
			}
			ImGui::PopID();
			EndRow("esp.look_kind_help");
		}

		void EspLookCard()
		{
			auto& look = Settings::Get().esp;
			if (g_espOpacityPct < 0) {
				g_espOpacityPct = look.opacity * 100.0f;
			}
			auto dirty = [] { g_espLookDirty = true; };
			BeginCard("esp.look");
			Columns(
				[&] {
					SliderRow("esp.look_icon_size", &look.iconScale, 0.5f, 3.0f, "×%.1f", "esp.look_icon_size_help", dirty);
					SliderRow("esp.look_text_size", &look.textScale, 0.5f, 2.5f, "×%.1f", "esp.look_text_size_help", dirty);
					SliderRow("esp.look_line_width", &look.lineScale, 0.5f, 3.0f, "×%.1f", "esp.look_line_width_help", dirty);
				},
				[&] {
					SliderRow("esp.look_opacity", &g_espOpacityPct, 20.0f, 100.0f, "%.0f%%", "esp.look_opacity_help", [&] {
						look.opacity = g_espOpacityPct / 100.0f;
						g_espLookDirty = true;
					});
					SwitchRow("esp.look_names", &look.names, "esp.look_names_help", [](bool) { g_espLookDirty = true; });
					SwitchRow("esp.look_distance", &look.distance, "esp.look_distance_help", [](bool) { g_espLookDirty = true; });
				});
			SubHeader("esp.look_colours_and_icons");
			Columns(
				[&] {
					for (std::size_t i = 0; i < Settings::kEspKinds; i += 2) {
						EspKindRow(i);
					}
				},
				[&] {
					for (std::size_t i = 1; i < Settings::kEspKinds; i += 2) {
						EspKindRow(i);
					}
				});
			ActionRow("esp.look_reset", "esp.look_reset_button", "esp.look_reset_help", [&] {
				look = {};
				g_espOpacityPct = 100.0f;
				g_espLookDirty = true;
			});
			EndCard();
			if (g_espLookDirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				g_espLookDirty = false;
				Settings::Save();
			}
		}

		void PageCombat()
		{
			auto&     st = Cheats::Get();
			const int tab = Tabs("combat", { "page.combat_and_movement", "feat.highlights_esp" });
			if (Tab(tab, 1)) {
				EspCard();
				EspLookCard();
			}
			if (!Tab(tab, 0)) {
				return;
			}
			Columns(
				[&] {
					BeginCard("card.movement");
					SliderRow("combat.run_speed", &st.speedMult, 50.0f, 500.0f, "%.0f", "combat.movement_speed_multiplier_100_default",
						[] { Cheats::ApplyPersistent(); });
					SliderRow("combat.jump_height", &st.jumpMult, 1.0f, 10.0f, "%.1f", "combat.how_many_times_higher_the", [] { Cheats::ApplyPersistent(); });
					SliderRow("combat.dragonborn_size", &g_playerScale, 0.25f, 3.0f, "%.2f", "combat.size_of_the_dragonborn_below",
						[] { Cheats::SetPlayerScale(g_playerScale); });
					bool noClip = Cheats::IsNoClip();
					SwitchRow("feat.flight", &st.flight, "combat.the_dragonborn_flies_wasd_goes", nullptr);
					SliderRow("combat.flight_speed", &st.flightSpeed, 200.0f, 5000.0f, "%.0f", "combat.units_per_second_running_is", nullptr);
					SwitchRow("feat.no_clip", &noClip, "combat.the_dragonborn_passes_through_walls", [](bool v) { Cheats::SetNoClip(v); });
					bool freeCamera = Cheats::IsFreeCamera();
					SwitchRow("feat.free_camera", &freeCamera, "combat.the_camera_detaches_from_the", [](bool v) { Cheats::SetFreeCamera(v); });
					EndCard();

					BeginCard("skill.sneak");
					SwitchRow("feat.lockpicking_helper", &st.lockpickHud,
						"combat.while_picking_a_lock_an", nullptr);
					SwitchRow("feat.infinite_lockpicks", &st.infiniteLockpicks, "combat.while_lockpicking_there_are_always", nullptr);
					SwitchRow("feat.auto_unlock", &st.autoUnlock, "combat.the_lock_opens_as_soon", nullptr);

					SwitchRow("feat.100_pickpocket", &st.easyPickpocket, "combat.every_pickpocket_attempt_succeeds_like",
						[](bool) { Cheats::ApplyPersistent(); });
					bool aiBlind = Cheats::IsAiBlind();
					SwitchRow("feat.undetectable_by_ai", &aiBlind, "combat.npcs_stop_noticing_the_player", [](bool v) { Cheats::SetAiBlind(v); });
					bool hudHidden = Cheats::IsHudHidden();
					SwitchRow("feat.hide_hud", &hudHidden, "combat.hides_the_hud_for_screenshots", [](bool v) { Cheats::SetHudHidden(v); });
					EndCard();
				},
				[&] {
					BeginCard("card.damage");
					SliderRow("combat.damage_dealt", &st.damageDealtMult, 0.1f, 20.0f, "%.1f", "combat.dragonborn_damage_multiplier_on_every",
						[] { Cheats::ApplyPersistent(); });
					SliderRow("combat.damage_taken", &st.damageTakenMult, 0.0f, 3.0f, "%.2f", "combat.damage_multiplier_against_the_dragonborn",
						[] { Cheats::ApplyPersistent(); });
					SliderRow("combat.attack_speed", &st.attackSpeed, 0.5f, 3.0f, "%.2f", "combat.how_much_faster_the_dragonborn",
						[] { Cheats::ApplyPersistent(); });
					SwitchRow("feat.one_hit_kill", &st.oneHitKill, "combat.any_blow_arrow_or_spell",
						[](bool) { Cheats::ApplyPersistent(); });
					SwitchRow("feat.kill_moves_beta", &st.killmoves, "combat.custom_kill_move_rules_for",
						[](bool) { Cheats::ApplyPersistent(); });
					if (st.killmoves) {
						SliderRow("combat.kill_move_chance", &st.killmoveChance, 0.0f, 100.0f, "%.0f", "combat.share_of_the_dragonborn_s",
							[] { Cheats::ApplyPersistent(); });
						SwitchRow("combat.not_only_the_last_enemy", &st.killmoveAnyEnemy, "combat.in_the_game_the_dragonborn",
							[](bool) { Cheats::ApplyPersistent(); });
						SliderRow("combat.decapitation_chance", &st.decapChance, 0.0f, 100.0f, "%.0f", "combat.how_often_a_kill_move",
							[] { Cheats::ApplyPersistent(); });
						SwitchRow("combat.decapitation_without_perk", &st.decapNoPerk, "combat.decapitate_even_without_the_savage",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("combat.enemies_can_t_kill_move", &st.noKillmovesOnPlayer, "combat.enemies_no_longer_kill_the",
							[](bool) { Cheats::ApplyPersistent(); });
					}
					SwitchRow("feat.fast_bow_draw", &st.fastBowDraw, "combat.bows_and_crossbows_draw_three",
						[](bool) { Cheats::ApplyPersistent(); });
					SwitchRow("feat.aimbot", &st.aimbot, "combat.the_dragonborn_s_arrows_and", nullptr);
					SwitchRow("feat.slow_time_while_aiming", &st.aimSlowmo, "combat.while_a_bow_or_crossbow", nullptr);
					SwitchRow("feat.infinite_arrows", &st.infiniteAmmo, "combat.equipped_arrows_or_bolts_never", nullptr);
					SwitchRow("feat.infinite_weapon_charge", &st.infiniteCharge, "combat.enchanted_weapons_and_staves_never", nullptr);
					SwitchRow("feat.infinite_poison", &st.infinitePoison, "combat.poison_applied_to_a_weapon", nullptr);
					ActionRow("feat.kill_nearby_enemies", "common.kill", "combat.all_hostile_creatures_within_70", [] {
						Cheats::KillHostiles(5000.0f);
						Status("status.nearby_enemies_defeated");
					});
					EndCard();
				});
		}

		// One card per school; the same row labels repeat between cards, so each card gets its own id scope.
		void BeginSchool(const char* a_title, Cheats::School& a_school, const char* a_power, const char* a_powerHelp, const char* a_durationHelp)
		{
			ImGui::PushID(a_title);
			BeginCard(a_title);
			SliderRow(a_power, &a_school.power, 0.1f, 20.0f, "%.1f", a_powerHelp, nullptr);
			SliderRow("schools.duration", &a_school.duration, 0.1f, 20.0f, "%.1f", a_durationHelp, nullptr);
			SliderRow("schools.cost", &a_school.cost, 0.0f, 3.0f, "%.2f", "schools.how_much_magicka_a_spell", nullptr);
		}

		void EndSchool()
		{
			EndCard();
			ImGui::PopID();
		}

		void PageSchools()
		{
			auto& st = Cheats::Get();
			if (!Magic::IsInstalled() && !g_searching) {
				ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
				ImGui::TextWrapped(Tr("schools.school_settings_are_unavailable_in"));
				ImGui::PopStyleColor();
			}
			Columns(
				[&] {
					BeginSchool("skill.destruction", st.destruction, "schools.damage", "schools.fire_frost_and_shock_damage",
						"schools.how_long_burning_slowing_and");
					EndSchool();
					BeginSchool("skill.alteration", st.alteration, "schools.magnitude", "schools.armor_from_oakflesh_and_similar",
						"schools.how_long_armor_light_waterbreathing");
					EndSchool();
					BeginSchool("skill.restoration", st.restoration, "schools.magnitude", "schools.how_much_spells_heal_ward",
						"schools.duration_of_healing_over_time");
					EndSchool();
				},
				[&] {
					BeginSchool("skill.conjuration", st.conjuration, "schools.magnitude", "schools.bound_weapon_strength_and_the",
						"schools.how_long_summoned_creatures_and");
					SliderRow("schools.extra_summons", &st.summonExtra, 0.0f, 10.0f, "%.0f",
						"schools.how_many_creatures_and_dead", nullptr);
					EndSchool();
					BeginSchool("skill.illusion", st.illusion, "schools.magnitude", "schools.up_to_which_level_fear",
						"schools.how_long_fear_calm_frenzy");
					EndSchool();

					BeginCard("card.all_magic");
					SliderRow("schools.enemy_magic_on_dragonborn", &st.incomingMagic, 0.0f, 3.0f, "%.2f",
						"schools.strength_of_hostile_spells_that", nullptr);
					SliderRow("schools.enemy_effect_duration", &st.incomingMagicDuration, 0.0f, 3.0f, "%.2f",
						"schools.how_long_paralysis_burning_fear", nullptr);
					SliderRow("schools.dual_casting_power", &st.dualCastPower, 0.5f, 5.0f, "%.2f",
						"schools.how_much_stronger_a_dual", [] { Cheats::ApplyPersistent(); });
					SliderRow("schools.dual_casting_cost", &st.dualCastCost, 0.0f, 3.0f, "%.2f",
						"schools.how_much_magicka_a_dual", [] { Cheats::ApplyPersistent(); });
					SliderRow("schools.magicka_regen", &st.magickaRegen, 0.5f, 20.0f, "%.1f", "schools.how_fast_the_dragonborn_s",
						[] { Cheats::ApplyPersistent(); });
					ActionRow("schools.reset_school_settings", "common.reset", "schools.every_multiplier_on_this_page", [&] {
						for (auto school : { &st.alteration, &st.conjuration, &st.destruction, &st.illusion, &st.restoration }) {
							*school = {};
						}
						st.summonExtra = 0.0f;
						st.incomingMagic = st.incomingMagicDuration = st.dualCastPower = st.dualCastCost = st.magickaRegen = 1.0f;
						Cheats::ApplyPersistent();
						Status("schools.schools_of_magic_reset");
					});
					EndCard();
				});
		}


		// Enchanting from the menu: pick a weapon or armour from the inventory, put on any enchantments, as strong as wanted.
		struct EnchantUi
		{
			RE::FormID                          object{ 0 };
			const RE::ExtraDataList*            list{ nullptr };
			bool                                selected{ false };
			std::vector<Enchanter::EffectSpec>  effects;
			std::string                         name;
			float                               charge{ 3000.0f };
			Clock::time_point                   refreshed{};
			std::size_t                         itemCategory{ 0 }, optionCategory{ 0 };  // the dropdown filters
			RE::FormID                          confirmRemove{ 0 };  // built-in enchantment: the first click of «Снять» warns
			Clock::time_point                   confirmAt{};
		} g_enchant;

		constexpr std::size_t kMaxEnchantEffects = 10;

		// A row of the Зачарование page's lists in a frame of its own (a pill when a_rounding is large); true when clicked.
		// a_icon: a form whose item icon goes before the text (ItemIcons), 0 for none.
		bool FramedRow(const char* a_id, const std::string& a_text, bool a_selected, float a_rounding, bool a_center = false, bool a_muted = false,
			bool a_clickable = true, RE::FormID a_icon = 0)
		{
			const float  h = ImGui::GetTextLineHeight() + D(12, 8);
			const ImVec2 size{ ImGui::GetContentRegionAvail().x, h };
			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max{ min.x + size.x, min.y + size.y };
			bool         clicked = false;
			bool         hovered = false;
			if (a_clickable) {
				clicked = ImGui::InvisibleButton(a_id, size);
				hovered = ImGui::IsItemHovered();
			} else {
				ImGui::Dummy(size);
			}
			auto        dl = ImGui::GetWindowDrawList();
			const float r = std::min(a_rounding, h * 0.5f);
			if (a_selected || hovered) {
				dl->AddRectFilled(min, max, a_selected ? Fade(T().accent, 0.18f) : T().hover, r);
			}
			dl->AddRect(min, max, a_selected ? T().accent : T().line, r, 0, S(1));
			const float tw = ImGui::CalcTextSize(a_text.c_str()).x;
			const float pad = std::max(S(10), r * 0.6f);
			float       x = a_center ? std::max(min.x + pad, min.x + (size.x - tw) * 0.5f) : min.x + pad;
			if (const float icon = S(20); a_icon && ItemIcons::Draw(dl, a_icon, { x, min.y + (h - icon) * 0.5f }, icon, a_muted ? T().muted : T().text)) {
				x += icon + S(8);
			}
			dl->PushClipRect({ min.x + S(2), min.y }, { max.x - S(2), max.y }, true);
			dl->AddText({ x, min.y + (h - ImGui::GetTextLineHeight()) * 0.5f }, a_muted ? T().muted : T().text, a_text.c_str());
			dl->PopClipRect();
			return clicked;
		}

		// "Фильтр: <category> v" dropdown over the whole width.
		void CategoryCombo(const char* a_id, const std::vector<const char*>& a_names, std::size_t* a_current)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			const auto preview = Lang::Fmt("enchant.filter", Tr(a_names[*a_current]));
			if (ImGui::BeginCombo(a_id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
				for (std::size_t i = 0; i < a_names.size(); ++i) {
					if (ImGui::Selectable(Tr(a_names[i]), *a_current == i)) {
						*a_current = i;
					}
				}
				ImGui::EndCombo();
			}
		}

		void PageEnchant()
		{
			if (g_searching) {
				return;
			}
			auto& ui = g_enchant;
			if (Since(ui.refreshed) > 2.0f) {
				ui.refreshed = Clock::now();
				Enchanter::RefreshItems();
			}
			const auto items = Enchanter::Items();
			const Enchanter::Item* item = nullptr;
			for (const auto& it : items) {
				if (ui.selected && it.object == ui.object && it.list == ui.list) {
					item = &it;
				}
			}
			const auto muted = [](const std::string& a_text) {
				ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
				ImGui::TextWrapped("%s", Tr(a_text.c_str()));
				ImGui::PopStyleColor();
			};
			Columns(
				[&] {
					BeginCard("enchant.item");
					CategoryCombo("##enchitemcat", Enchanter::ItemCategories(), &ui.itemCategory);
					if (ImGui::BeginChild("##enchitemlist", { -FLT_MIN, S(460) })) {
						bool any = false;
						for (std::size_t i = 0; i < items.size(); ++i) {
							const auto& it = items[i];
							if (ui.itemCategory != 0 && it.category != ui.itemCategory) {
								continue;
							}
							any = true;
							ImGui::PushID(static_cast<int>(i));
							std::string label = it.name;
							if (it.count > 1) {
								label += std::format(" ×{}", it.count);
							}
							if (it.worn) {
								label += Tr("enchant.equipped");
							}
							const bool picked = FramedRow("##item", label, item == &it, S(4), false, false, true, it.object);
							RecordControl(it.name);
							if (picked) {
								ui.selected = true;
								ui.object = it.object;
								ui.list = it.list;
								ui.effects = it.current;
								ui.charge = it.charge > 0.0f ? it.charge : 3000.0f;
								ui.name.clear();
							}
							if (!it.enchantments.empty()) {
								// its enchantments: a smaller frame right under it, indented
								std::string text;
								for (const auto& e : it.enchantments) {
									text += (text.empty() ? "" : ", ") + e;
								}
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);
								ImGui::Indent(S(18));
								FramedRow("##ench", text, false, S(4), false, true, false);
								ImGui::Unindent(S(18));
							}
							ImGui::Dummy({ 0, S(2) });
							ImGui::PopID();
						}
						if (!any) {
							muted(items.empty() ? "enchant.no_weapons_or_armor_in" : "enchant.nothing_in_this_category");
						}
					}
					ImGui::EndChild();
					EndCard();
				},
				[&] {
					BeginCard("enchant.enchantments");
					if (!item) {
						muted("enchant.pick_an_item_on_the");
						EndCard();
						return;
					}
					ImGui::TextUnformatted(item->name.c_str());
					if (ui.effects.empty()) {
						muted("enchant.no_enchantments_yet_add_them");
					}
					for (std::size_t i = 0; i < ui.effects.size();) {
						auto& e = ui.effects[i];
						ImGui::PushID(static_cast<int>(i) + 1000);
						ImGui::Separator();
						ImGui::TextUnformatted(e.name.c_str());
						ImGui::SameLine();
						const bool remove = ImGui::SmallButton(Tr("common.remove"));
						if (e.hasMagnitude) {
							SliderRow("enchant.magnitude", &e.magnitude, 0.0f, e.maxMagnitude, "%.0f", "enchant.effect_magnitude_damage_percent_or", nullptr);
						}
						if (e.hasDuration) {
							SliderRow("enchant.duration_s", &e.duration, 0.0f, 600.0f, "%.0f", "enchant.how_many_seconds_the_effect", nullptr);
						}
						ImGui::PopID();
						if (remove) {
							ui.effects.erase(ui.effects.begin() + static_cast<std::ptrdiff_t>(i));
						} else {
							++i;
						}
					}
					ImGui::Separator();
					if (item->kind != Enchanter::Kind::kArmor) {
						SliderRow("enchant.charge", &ui.charge, 100.0f, 20000.0f, "%.0f", "enchant.weapon_charge_a_hit_spends", nullptr);
					}
					ImGui::SetNextItemWidth(-FLT_MIN);
					ImGui::InputTextWithHint("##enchname", Tr("enchant.custom_name_optional"), &ui.name);
					if (Button("enchant.enchant", true)) {
						Status(Enchanter::Apply(*item, ui.effects, ui.name, ui.charge));
						ui.refreshed = {};
					}
					ImGui::SameLine();
					// built into the item (Iron Sword of Fear): it is swapped for its plain version, so the first click only warns
					const bool builtIn = !item->ownEnchantment && !item->enchantments.empty();
					const bool confirming = builtIn && ui.confirmRemove == item->object && Clock::now() - ui.confirmAt < std::chrono::seconds(4);
					if (Button(confirming ? "enchant.remove_for_sure" : "enchant.remove_enchantment")) {
						if (builtIn && !confirming) {
							ui.confirmRemove = item->object;
							ui.confirmAt = Clock::now();
							Status("enchant.the_enchantment_is_built_in");
						} else {
							ui.confirmRemove = 0;
							Status(Enchanter::Remove(*item));
							ui.effects.clear();
							ui.refreshed = {};
						}
					}
					ImGui::SameLine();
					if (Button("enchant.current")) {
						ui.effects = item->current;
					}
					EndCard();

					BeginCard("enchant.add_enchantment");
					CategoryCombo("##enchoptcat", Enchanter::OptionCategories(), &ui.optionCategory);
					if (ui.effects.size() >= kMaxEnchantEffects) {
						muted(Lang::Fmt("enchant.no_more_than_effects_can", kMaxEnchantEffects));
					}
					if (ImGui::BeginChild("##enchoptlist", { -FLT_MIN, S(300) })) {
						for (const auto& option : Enchanter::Options()) {
							if (option.kind != item->kind || (ui.optionCategory != 0 && !(option.categories & (1u << ui.optionCategory)))) {
								continue;
							}
							ImGui::PushID(static_cast<int>(option.enchantment));
							const bool picked = FramedRow("##opt", option.name, false, S(100), true);
							RecordControl(option.name);
							if (picked && ui.effects.size() < kMaxEnchantEffects) {
								for (const auto& e : option.effects) {
									if (ui.effects.size() < kMaxEnchantEffects) {
										ui.effects.push_back(e);
									}
								}
							}
							if (ImGui::IsItemHovered() && !option.plugin.empty()) {
								ImGui::SetTooltip("%s", option.plugin.c_str());
							}
							ImGui::Dummy({ 0, S(2) });
							ImGui::PopID();
						}
					}
					ImGui::EndChild();
					EndCard();
				});
		}

		// Crafting stations as a grid of tiles with the station's icon; rows while searching.
		void StationTiles()
		{
			BeginCard("card.crafting_stations_anywhere");
			const auto open = [](Cheats::Station a_station) {
				if (Cheats::OpenStation(a_station)) {
					Input::SetMenuOpen(false);
				} else {
					Status("enchant.not_possible_now_leave_the");
				}
			};
			if (g_searching) {
				for (const auto& row : kStations) {
					if (Cheats::HasStation(row.station)) {
						ActionRow(row.label, "common.open", row.help, [&] { open(row.station); });
					}
				}
				EndCard();
				return;
			}
			ImGui::Dummy({ 0, S(2) });
			constexpr int cols = 5;
			const float   gap = S(10);
			const float   w = (ImGui::GetContentRegionAvail().x - gap * (cols - 1)) / cols;
			const float   h = D(108, 92);
			auto          dl = ImGui::GetWindowDrawList();
			int           i = 0;
			for (const auto& row : kStations) {
				if (!Cheats::HasStation(row.station)) {
					continue;  // its DLC / mod is not loaded
				}
				if (i % cols) {
					ImGui::SameLine(0, gap);
				}
				const auto p = ImGui::GetCursorScreenPos();
				ImGui::PushID(row.label);
				const bool clicked = ImGui::InvisibleButton("##station", { w, h });
				RecordControl(row.label);
				const bool  hovered = ImGui::IsItemHovered();
				const float hover = Anim(ImGui::GetItemID(), hovered ? 1.0f : 0.0f);
				const ImVec2 max{ p.x + w, p.y + h };
				dl->AddRectFilled(p, max, Mix(T().seg, T().hover, hover), S(4));
				dl->AddRect(p, max, Mix(T().line, T().gold, hover), S(4));
				if (hover > 0.01f) {
					Corners(dl, p, max, S(7), S(3), Fade(T().gold, hover), 1.0f);
				}
				DrawIcon(dl, row.icon, { p.x + w * 0.5f, p.y + h * 0.36f - S(2) * hover }, S(34), Mix(T().accent, T().gold, hover));
				PushFont(Overlay::GetFonts().caption);
				CenteredText(dl, row.label, p.x + w * 0.5f, p.y + h * 0.64f, w - S(12), T().text);
				ImGui::PopFont();
				if (hovered) {
					g_help = Lang::Fmt("enchant.opens_next_to_the_dragonborn", Tr(row.help));
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				}
				if (clicked) {
					open(row.station);
				}
				ImGui::PopID();
				++i;
			}
			ImGui::Dummy({ 0, S(4) });
			EndCard();
		}

		void PageProgress()
		{
			auto&       st = Cheats::Get();
			const auto& snap = Snap();
			const int   tab = Tabs("progress", { "progress.level_and_skills", "progress.training_and_soul_gems", "progress.crafting_and_stations" });
			if (Tab(tab, 0)) {
				Columns(
					[&] {
						BeginCard("card.level_and_perks");
						if (BeginRow("progress.character_level", "progress.sets_an_exact_level_player", S(250))) {
							ImGui::SetNextItemWidth(S(120));
							ImGui::InputInt("##level", &g_levelInput);
							g_levelInput = std::clamp(g_levelInput, 1, 65535);
							ImGui::SameLine();
							if (Button("progress.set", true)) {
								Cheats::SetLevel(g_levelInput);
								Status(Lang::Fmt("progress.level", g_levelInput));
							}
							EndRow(nullptr);
						}
						ActionRow("progress.level_up", "+1", "progress.raises_the_character_level_by", [] {
							Cheats::AddLevels(1);
							++g_levelInput;
							Status("progress.level_1");
						});
						ActionRow("progress.level_up_10", "+10", "progress.raises_the_character_level_by_2", [] {
							Cheats::AddLevels(10);
							g_levelInput += 10;
							Status("progress.level_10");
						});
						ActionRow("progress.perk_points", "+1", "progress.adds_one_perk_point", [] {
							Cheats::AddPerkPoints(1);
							Status("progress.perk_point_1");
						});
						ActionRow("progress.perk_points_10", "+10", "progress.adds_ten_perk_points", [] {
							Cheats::AddPerkPoints(10);
							Status("progress.perk_points_10_2");
						});
						ActionRow("progress.all_skills_to_100", "common.apply", "progress.sets_all_18_skills_to", [] {
							Cheats::MaxAllSkills();
							g_skillValues.fill(100.0f);
							Status("progress.all_skills_at_100");
						});
						SliderRow("progress.skill_cap", &st.skillCap, 100.0f, 250.0f, "%.0f", "progress.the_level_up_to_which",
							[] { Cheats::Get().skillCap = std::round(Cheats::Get().skillCap); Cheats::ApplyPersistent(); });
						SliderRow("progress.leveling_speed", &st.skillXpMult, 1.0f, 30.0f, "%.0f", "progress.how_many_times_faster_skills",
							[] { Cheats::ApplyPersistent(); });
						SwitchRow("feat.auto_perks", &st.autoPerks, "progress.perks_are_taken_automatically_and", nullptr);
						SliderRow("progress.perks_per_level", &st.perksPerLevel, 1.0f, 10.0f, "%.0f", "progress.how_many_perk_points_each",
							[] { Cheats::Get().perksPerLevel = std::round(Cheats::Get().perksPerLevel); });
						SwitchRow("feat.unlimited_training", &st.unlimitedTraining, "progress.trainers_teach_any_number_of",
							[](bool) { Cheats::ApplyPersistent(); });
						EndCard();
					},
					[&] {
						BeginCard("card.attributes");
						for (std::size_t i = 0; i < std::size(kAttributes); ++i) {
							SliderRow(kAttributes[i].name, &g_attrValues[i], 10.0f, 2000.0f, "%.0f",
								"progress.base_value_without_enchantments_or",
								[i] { Cheats::SetBaseValue(kAttributes[i].av, std::round(g_attrValues[i])); });
						}
						EndCard();

						BeginCard("card.standing_stone");
						if (BeginRow("card.standing_stone", "progress.changes_the_standing_stone_without", S(260))) {
							const auto stones = Cheats::StandingStones();
							const int  current = Cheats::CurrentStone();
							ImGui::SetNextItemWidth(S(260));
							if (ImGui::BeginCombo("##stone", Tr(current >= 0 ? stones[current].name : "progress.none"), ImGuiComboFlags_HeightLarge)) {
								for (int i = 0; i < static_cast<int>(stones.size()); ++i) {
									if (ImGui::Selectable(Tr(stones[i].name), i == current)) {
										Cheats::SetStone(i);
										Status(Lang::Fmt("progress.stone", Tr(stones[i].name)));
									}
								}
								ImGui::EndCombo();
							}
							EndRow(nullptr);
						}
						EndCard();

						BeginCard("card.resources");
						ActionRow("feat.gold", "+10 000", "progress.puts_10_000_septims_into", [] {
							Cheats::AddGold(10000);
							Status("progress.gold_10_000");
						});
						ActionRow("feat.dragon_souls", "+5", "progress.adds_5_souls_for_unlocking", [] {
							Cheats::AddDragonSouls(5);
							Status("progress.dragon_souls_5");
						});
						SwitchRow("feat.no_shout_cooldown", &st.noShoutCooldown, "progress.shouts_can_be_used_again", [](bool) { Cheats::ApplyPersistent(); });
						EndCard();
					});

				BeginCard("common.skills");
				auto skills = [](std::size_t a_from, std::size_t a_to) {
					return [=] {
						for (std::size_t i = a_from; i < a_to; ++i) {
							SliderRow(kSkills[i].name, &g_skillValues[i], 0.0f, 250.0f, "%.0f", "progress.skill_level_base_value_can",
								[i] { Cheats::SetBaseValue(kSkills[i].av, std::round(g_skillValues[i])); });
						}
					};
				};
				Columns(skills(0, std::size(kSkills) / 2), skills(std::size(kSkills) / 2, std::size(kSkills)));
				EndCard();
			}
			if (Tab(tab, 1)) {
				Columns(
					[&] {
						BeginCard("card.learn_everything");
						ActionRow("progress.all_spells", "progress.learn", "progress.every_spell_that_has_a", [] {
							Cheats::LearnAllSpells();
							Status("progress.all_spells_learned");
							RefreshSnapshot();
						});
						ActionRow("progress.all_shouts", "progress.learn", "progress.all_dragonborn_shouts_including_dlc", [] {
							Cheats::LearnAllShouts();
							Status("progress.all_shouts_learned");
							RefreshSnapshot();
						});
						ActionRow("progress.all_perks", "progress.learn", "progress.every_perk_of_all_18", [] {
							Cheats::LearnAllPerks();
							Status("progress.all_perks_learned");
							RefreshSnapshot();
						});
						ActionRowEx("progress.undo_learned", "progress.forget", "progress.removes_perks_spells_and_shouts", [] {
							Status(Lang::Fmt("progress.forgotten", Cheats::LearnedCount()));
							Cheats::ForgetLearned();
							RefreshSnapshot();
						}, { snap.learned ? Lang::Fmt("progress.given", snap.learned) : std::string{ "progress.nothing" }, snap.learned > 0 });
						EndCard();
					},
					[&] {
						BeginCard("cat.soul_gems");
						SwitchRow("feat.soul_gems_not_consumed", &st.soulGemsKeep, "progress.after_enchanting_or_recharging_a", nullptr);
						SwitchRow("feat.soul_gems_always_full", &st.soulGemsFull, "progress.a_used_soul_gem_comes", nullptr);
						SwitchRow("feat.any_soul_in_any_gem", &st.anySoulInAnyGem,
							"progress.any_empty_soul_gem_accepts", [](bool) { Cheats::ApplyPersistent(); });
						ActionRowEx("feat.fill_all_soul_gems", "progress.fill", "progress.all_empty_soul_gems_in", [] {
							Cheats::FillSoulGems();
							Status("status.soul_gems_filled");
							RefreshSnapshot();
						}, { snap.emptyGems ? Lang::Fmt("progress.empty", snap.emptyGems) : std::string{ "progress.none_empty" }, snap.emptyGems > 0 });
						EndCard();
					});
			}
			if (Tab(tab, 2)) {
				StationTiles();
				Columns(
					[&] {
						BeginCard("card.crafting");
						SliderRow("progress.smithing", &st.smithingBoost, 0.0f, 1000.0f, "%.0f", "progress.like_a_fortify_smithing_potion",
							[] { Cheats::ApplyPersistent(); });
						SliderRow("progress.alchemy", &st.alchemyBoost, 0.0f, 1000.0f, "%.0f", "progress.like_fortify_alchemy_how_much",
							[] { Cheats::ApplyPersistent(); });
						SliderRow("progress.enchanting", &st.enchantingBoost, 0.0f, 1000.0f, "%.0f", "progress.like_fortify_enchanting_how_much",
							[] { Cheats::ApplyPersistent(); });
						SliderRow("progress.effects_per_item", &st.enchantEffects, 1.0f, 10.0f, "%.0f",
							"progress.how_many_enchantments_one_item", nullptr);
						SwitchRow("feat.free_crafting", &st.freeCraft, "progress.forge_improvement_cooking_house_building",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.crafting_without_perks", &st.craftNoPerks, "progress.recipes_don_t_require_perks",
							[](bool) { Cheats::ApplyPersistent(); });
						ActionRow("feat.legendary_gear", "progress.improve", "progress.all_equipped_weapons_and_armor",
							[] { Status(Cheats::TemperEquipped()); });
						SwitchRow("feat.infinite_ore_veins", &st.infiniteOre, "progress.nearby_ore_veins_don_t", nullptr);
						EndCard();
					},
					[&] {
						BeginCard("card.knowledge_and_restrictions");
						SwitchRow("feat.enchant_anything", &st.enchantAnything,
							"progress.artifacts_and_unique_items_that",
							[](bool) { Cheats::ApplyPersistent(); });
						PermanentRow("progress.know_all_enchantments", "progress.learn", "progress.all_enchantments_are_available_at", [] {
							Cheats::LearnAllEnchantments();
							Status("progress.all_enchantments_learned");
						});
						PermanentRow("progress.know_all_ingredient_effects", "progress.learn", "progress.all_4_effects_of_every", [] {
							Cheats::LearnAllIngredients();
							Status("progress.all_ingredient_effects_learned");
						});
						EndCard();
					});
			}
		}

		void PageWorld()
		{
			auto&       st = Cheats::Get();
			const auto& snap = Snap();
			const int   tab = Tabs("world", { "world.time_and_weather", "world.guilds_and_law", "world.trade" });
			if (Tab(tab, 0)) {
				Columns(
					[&] {
						BeginCard("card.time");
						SliderRow("world.time_of_day_h", &g_hour, 0.0f, 23.99f, "%.1f", "world.sets_the_in_game_clock", [] { Cheats::SetHour(g_hour); });
						SliderRow("world.timescale", &g_timescale, 1.0f, 100.0f, "%.0f", "world.how_many_game_minutes_pass",
							[] { Cheats::SetTimescale(g_timescale); });
						SliderRow("world.world_speed", &Cheats::Get().worldSpeed, 0.1f, 2.0f, "%.2f",
							"world.slows_down_or_speeds_up",
							[] { Cheats::ApplyPersistent(); });
						SwitchRow("feat.no_dragon_attacks", &st.noDragonAttacks, "world.random_dragons_no_longer_appear",
							[](bool) { Cheats::ApplyPersistent(); });
						ActionRow("feat.summon_dragon", "common.summon",
							"world.a_hostile_dragon_matching_the",
							[] { Status(Cheats::SpawnDragon()); });
						SwitchRow("feat.stop_time", &st.freezeTime, "world.the_clock_stops_day_doesn", nullptr);
						ActionRow("feat.wait_here", "world.wait", "world.the_wait_window_right_here", [] {
							Input::SetMenuOpen(false);
							Cheats::OpenWaitMenu();
						});
						ActionRow("world.reset_this_dungeon", "world.reset", "world.enemies_and_loot_in_the", [] {
							Status(Cheats::ResetCurrentDungeon());
						});
						ActionRow("feat.solve_puzzle", "world.solve", "world.claw_doors_and_pillar_puzzles", [] {
							Status(Puzzles::SolveNearby());
						});
						SwitchRow("feat.dragonborn_not_slowed", &st.playerNormalSpeed,
							"world.when_the_world_is_slowed",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.travel_without_time_passing", &st.timelessTravel,
							"world.fast_travel_on_the_map", nullptr);
						SwitchRow("feat.fast_travel_in_combat_and", &st.fastTravelAnywhere,
							"world.fast_travel_even_with_enemies", nullptr);
						SwitchRow("feat.fast_travel_anywhere", &st.fastTravelIgnoreBans,
							"world.also_from_places_and_moments",
							nullptr);
						EndCard();
					},
					[&] {
						BeginCard("card.weather");
						if (!g_searching) {
							static std::string filter;
							ImGui::SetNextItemWidth(-FLT_MIN);
							ImGui::InputTextWithHint("##wf", Tr("world.filter_snow_rain_mod"), &filter);
							if (Button("world.restore_normal_weather")) {
								Cheats::ReleaseWeather();
								Status("world.weather_back_to_normal");
							}
							const auto needle = Text::ToLower(filter);
							if (ImGui::BeginChild("##weathers", { -FLT_MIN, S(300) })) {
								for (const auto& w : Catalog::Weathers()) {
									const auto label = std::format("{} · {}", Tr(w.kind), w.label);
									if (!Text::Contains(Text::ToLower(label), needle)) {
										continue;
									}
									if (ImGui::Selectable(label.c_str())) {
										Cheats::ForceWeather(w.formID);
										Status(Lang::Fmt("world.weather", label));
									}
								}
							}
							ImGui::EndChild();
						}
						EndCard();
					});
			}
			if (Tab(tab, 1)) {
				Columns(
					[&] {
						BeginCard("card.guilds_and_titles");
						const auto [thane, holds] = snap.thane;
						if (holds > 0 && thane == holds) {
							ActionRowEx("world.thane_in_all_holds", nullptr, "world.thane_title_received_in_every", [] {}, { Lang::Fmt("world.all", holds), true, false, true });
						} else {
							PermanentRow("world.thane_in_all_holds", "world.become_thane",
								"world.instantly_thane_in_all_9", [] {
									Status(Cheats::BecomeThaneEverywhere());
									RefreshSnapshot();
								},
								{ Lang::Fmt("world.of", thane, holds) });
						}
						SwitchRow("feat.thieves_guild_one_job_instead", &st.thievesGuildQuick,
							"world.for_a_city_s_special", nullptr);
						PermanentRow("world.thieves_guild_special_jobs_everywhere", "common.open",
							"world.unlocks_the_special_jobs_in", [] { Status(Cheats::OpenGuildCityJobs()); },
							{ snap.thievesGuild ? std::string{} : std::string{ "world.must_join_first" }, snap.thievesGuild });
						EndCard();
					},
					[&] {
						BeginCard("card.followers_and_law");
						SwitchRow("feat.immortal_followers", &st.immortalFollowers,
							"world.followers_and_the_dragonborn_s", nullptr);
						ActionRow("feat.summon_followers", "world.call", "world.all_followers_and_a_follower", [] {
							Cheats::SummonFollowers();
							Status("status.followers_moved_to_the_dragonborn");
						});
						PermanentRow("feat.divorce", "world.divorce", "world.ends_the_marriage_the_spouse", [] {
							Status(Cheats::Divorce());
							RefreshSnapshot();
						},
							{ snap.spouse.empty() ? std::string{ "world.not_married" } : snap.spouse, !snap.spouse.empty() });
						SubHeader("world.law");
						ActionRow("feat.clear_bounties", "common.clear", "world.clears_the_bounty_in_every", [] {
							Cheats::ClearBounties();
							Status("world.bounties_cleared");
						});
						SwitchRow("feat.no_bounty", &st.noBounty, "world.the_bounty_resets_at_once", nullptr);
						SwitchRow("feat.keep_items_in_jail", &st.keepItemsInJail, "world.on_arrest_everything_the_guards", nullptr);
						ActionRowEx("feat.clear_stolen_flag", "common.clear", "world.all_stolen_items_in_the", [] {
							Status(Lang::Fmt("status.stolen_flag_cleared", Cheats::UnmarkStolen()));
							RefreshSnapshot();
						},
							{ snap.stolen ? Lang::Fmt("world.stolen", snap.stolen) : std::string{ "world.nothing_stolen" }, snap.stolen > 0 });
						EndCard();
					});
			}
			if (Tab(tab, 2)) {
				BeginCard("card.trade_and_loot");
				Columns(
					[&] {
						SwitchRow("feat.trade_at_base_price", &st.fairTrade, "world.buy_at_base_price_sell",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.speech_always_succeeds", &st.persuadeAlways,
							"world.persuade_intimidate_and_bribe_always",
							[](bool) { Cheats::ApplyPersistent(); });
						SwitchRow("feat.rich_merchants", &st.richMerchants, "world.a_merchant_always_has_at", nullptr);
						SwitchRow("feat.merchants_buy_everything", &st.merchantsBuyAll, "world.any_merchant_takes_any_item",
							[](bool) { Cheats::ApplyPersistent(); });
						ActionRow("feat.summon_merchant", "common.summon",
							"world.a_dremora_merchant_appears_next",
							[] {
								Input::SetMenuOpen(false);
								Status(Cheats::SummonMerchant());
							});
						ActionRow("feat.restock_merchants", "world.reset", "world.all_merchants_restock_goods_and", [] {
							Status(Lang::Fmt("status.restocked_merchants", Cheats::RestockMerchants()));
						});
						ActionRow("feat.open_storage", "common.open", "world.a_portable_chest_opens_from", [] {
							Input::SetMenuOpen(false);
							Cheats::OpenStorage();
						});
					},
					[&] {
						SwitchRow("feat.auto_loot", &st.autoLoot, "world.auto_loot_help", nullptr);
						SliderRow("world.loot_radius_m", &st.lootRadius, 5.0f, 100.0f, "%.0f", "world.how_far_loot_nearby_searches", nullptr);
						ActionRow("feat.loot_nearby", "world.loot", "world.takes_everything_from_corpses_and", [&st] {
							Cheats::LootNearby(st.lootRadius * 70.0f);
							Status("status.nearby_loot_collected");
						});
					});
				EndCard();
			}
		}

		void SavedPointsCard()
		{
			static std::string name;
			static int         armedDelete = -1;

			BeginCard("card.my_points");
			ImGui::SetNextItemWidth(S(320));
			const bool enter = ImGui::InputTextWithHint("##pname", Tr("teleport.name_optional"), &name, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if (Button("feat.save_point_here", true) || enter) {
				Cheats::SavePoint(name);
				Status(name.empty() ? "teleport.point_saved" : Lang::Fmt("teleport.point_saved_2", name));
				name.clear();
			}
			if (ImGui::IsItemHovered()) {
				g_help = "teleport.remembers_where_the_dragonborn_stands";
			}
			ImGui::SameLine();
			if (Button("feat.to_map_marker")) {
				if (Cheats::HasMapMarker()) {
					Cheats::TeleportToMapMarker();
					Input::SetMenuOpen(false);
				} else {
					Status("status.place_a_custom_marker_on");
				}
			}
			if (ImGui::IsItemHovered()) {
				g_help = "teleport.moves_to_the_marker_placed";
			}

			const auto& points = Cheats::SavedPoints();
			if (points.empty()) {
				Muted("teleport.empty_for_now_stand_at");
			} else {
				constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH;
				const float    rows = static_cast<float>(std::min<std::size_t>(points.size(), 4));
				if (ImGui::BeginTable("##points", 3, flags, { -FLT_MIN, (rows + 0.2f) * (S(36) + S(10)) })) {
					ImGui::TableSetupColumn(Tr("teleport.point"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
					ImGui::TableSetupColumn(Tr("teleport.where"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(300));
					// Newest on top.
					for (int i = static_cast<int>(points.size()) - 1; i >= 0; --i) {
						const auto& p = points[i];
						ImGui::PushID(i);
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::AlignTextToFramePadding();
						ImGui::TextUnformatted(p.name.c_str());
						ImGui::TableNextColumn();
						ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
						ImGui::TextUnformatted(p.place.c_str());
						ImGui::PopStyleColor();
						ImGui::TableNextColumn();
						if (Button("teleport.travel", true)) {
							Cheats::TeleportToPoint(static_cast<std::size_t>(i));
							Input::SetMenuOpen(false);
						}
						ImGui::SameLine();
						if (Button(armedDelete == i ? "teleport.delete_for_sure" : "common.delete_2", armedDelete == i)) {
							if (armedDelete == i) {
								Cheats::DeletePoint(static_cast<std::size_t>(i));
								armedDelete = -1;
								ImGui::PopID();
								break;
							}
							armedDelete = i;
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
			EndCard();
		}

		void PageTeleport()
		{
			static std::string filter;
			static std::string coc;
			if (Catalog::Markers().empty()) {
				Catalog::BuildMarkers();
			}
			SavedPointsCard();

			ImGui::SetNextItemWidth(S(360));
			ImGui::InputTextWithHint("##mf", Tr("teleport.search_places_whiterun_cave_mod"), &filter);
			ImGui::SameLine();
			if (Button("teleport.reveal_all_map_markers")) {
				Console::Run("tmm 1");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(S(220));
			ImGui::InputTextWithHint("##coc", Tr("teleport.cell_id_coc"), &coc);
			ImGui::SameLine();
			if (Button("coc", true) && !coc.empty()) {
				Console::Run("coc " + coc);
				Input::SetMenuOpen(false);
			}

			const auto needle = Text::ToLower(filter);
			constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH;
			if (ImGui::BeginTable("##markers", 3, flags, { -FLT_MIN, std::max(S(200), ImGui::GetContentRegionAvail().y) })) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn(Tr("teleport.location"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn(Tr("teleport.worldspace"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(130));
				ImGui::TableHeadersRow();
				int id = 0;
				for (const auto& m : Catalog::Markers()) {
					if (!Text::Contains(m.nameLower, needle) && !Text::Contains(Text::ToLower(m.world), needle)) {
						continue;
					}
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(m.name.c_str());
					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
					ImGui::TextUnformatted(m.world.c_str());
					ImGui::PopStyleColor();
					ImGui::TableNextColumn();
					ImGui::PushID(id++);
					if (Button("teleport.travel", true)) {
						Cheats::TeleportTo(m.ref);
						Input::SetMenuOpen(false);
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		void PageTarget()
		{
			if (g_searching && !g_target) {
				return;
			}
			BeginCard("card.target_under_crosshair");
			if (!g_target) {
				Muted("target.no_target_close_the_menu");
				EndCard();
				return;
			}
			if (!g_searching) {
				ImGui::Text(Tr("target.selected"), g_targetName.c_str());
			}
			auto run = [](const char* a_cmd) { Console::Run(a_cmd, g_target); };
			ActionRow("common.kill", "common.kill", "target.kills_the_target_instantly_kill", [&] { run("kill"); });
			ActionRow("target.resurrect", "target.resurrect", "target.brings_a_dead_target_back", [&] { run("resurrect"); });
			ActionRow("target.unlock", "target.unlock_2", "target.opens_a_locked_door_or", [&] { run("unlock"); });
			ActionRow("target.make_owned", "target.claim", "target.a_bed_workbench_chest_or",
				[&] { run("setownership"); });
			if (auto ref = g_target.get(); ref && ref->GetBaseObject() && ref->GetBaseObject()->Is(RE::FormType::NPC)) {
				const auto base = ref->GetBaseObject()->GetFormID();
				ActionRow("target.npc_essential", "common.enable", "target.the_npc_drops_to_one",
					[base] { Console::Run(std::format("setessential {:08X} 1", base)); });
				ActionRow("target.remove_npc_essential", "common.clear", "target.a_normal_npc_that_can",
					[base] { Console::Run(std::format("setessential {:08X} 0", base)); });
				static float force = 10.0f;
				SliderRow("target.push_force", &force, 1.0f, 50.0f, "%.0f", "target.how_hard_push_away_launches", nullptr);
				ActionRow("target.push_away", "target.push_away", "target.the_target_flies_away_from",
					[] { Cheats::FlingTarget(g_target, force); });
			}
			ActionRow("target.calm", "target.calm", "target.the_target_stops_being_aggressive", [&] {
				run("stopcombat");
				run("setav aggression 0");
			});
			PermanentRow("target.marry", "target.marry_2", "target.a_wedding_with_the_target", [] {
				Status(Cheats::MarryTarget(g_target));
				RefreshSnapshot();
			});
			ActionRow("target.make_follower", "target.recruit", "target.adds_to_follower_factions_and", [&] {
				run("setrelationshiprank player 3");
				run("addfac 0005C84D 0");
			});
			ActionRow("target.open_inventory", "common.open", "target.shows_the_target_s_inventory", [&] {
				run("openactorcontainer 1");
				Input::SetMenuOpen(false);
			});
			ActionRow("target.remove_from_world", "target.disable", "target.makes_the_object_invisible_and", [&] { run("disable"); });
			EndCard();
		}

		void PageConsole()
		{
			BeginCard("card.console_command");
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(140));
			const bool enter = ImGui::InputTextWithHint("##cmd", Tr("console.e_g_player_additem_f"), &g_consoleInput, ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((Button("console.run", true) || enter) && !g_consoleInput.empty()) {
				Console::Run(g_consoleInput, g_target);
				g_consoleHistory.insert(g_consoleHistory.begin(), g_consoleInput);
				if (g_consoleHistory.size() > 30) {
					g_consoleHistory.pop_back();
				}
				g_consoleInput.clear();
			}
			Muted("console.commands_run_on_the_selected");
			for (int i = 0; i < static_cast<int>(g_consoleHistory.size()); ++i) {
				ImGui::PushID(i);
				if (ImGui::Selectable(g_consoleHistory[i].c_str())) {
					Console::Run(g_consoleHistory[i], g_target);
				}
				ImGui::PopID();
			}
			EndCard();
		}

		void PageQuests()
		{
			static std::vector<Cheats::QuestInfo> quests;
			static std::string                    filter;
			static RE::FormID                     armed = 0;
			if (quests.empty() || Button("quests.refresh_list")) {
				quests = Cheats::RunningQuests();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(S(360));
			ImGui::InputTextWithHint("##qf", Tr("quests.search_quests"), &filter);
			Muted("quests.active_quests_next_stage_advances");

			const auto     needle = Text::ToLower(filter);
			constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH;
			if (ImGui::BeginTable("##quests", 4, flags, { -FLT_MIN, -FLT_MIN })) {
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn(Tr("quests.quest"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
				ImGui::TableSetupColumn(Tr("quests.type"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
				ImGui::TableSetupColumn(Tr("quests.stage"), ImGuiTableColumnFlags_WidthFixed, S(60));
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, S(330));
				ImGui::TableHeadersRow();
				for (auto& q : quests) {
					if (!Text::Contains(Text::ToLower(q.name), needle)) {
						continue;
					}
					ImGui::PushID(static_cast<int>(q.formID));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(q.name.c_str());
					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
					ImGui::TextUnformatted(Tr(q.type.c_str()));
					ImGui::PopStyleColor();
					ImGui::TableNextColumn();
					ImGui::Text("%u", q.stage);
					ImGui::TableNextColumn();
					if (Button("quests.next_stage", true)) {
						Status(Cheats::NextQuestStage(q.formID) ? Lang::Fmt("quests.next_stage_2", q.name) : "quests.the_quest_has_no_next");
						quests.clear();
					}
					ImGui::SameLine();
					if (Button(armed == q.formID ? "common.sure" : "quests.complete", armed == q.formID)) {
						if (armed == q.formID) {
							Cheats::CompleteQuest(q.formID);
							Status(Lang::Fmt("quests.quest_completed", q.name));
							armed = 0;
							quests.clear();
						} else {
							armed = q.formID;
						}
					}
					ImGui::PopID();
					if (quests.empty()) {
						break;
					}
				}
				ImGui::EndTable();
			}
		}

		// User console macros, each runnable from here or from its own hotkey.
		void CommandsCard()
		{
			static std::string name;
			static std::string command;
			auto&              commands = Settings::Get().commands;

			BeginCard("card.custom_commands");
			if (!g_searching) {
				Muted("commands.any_console_command_or_several");
				ImGui::SetNextItemWidth(S(170));
				ImGui::InputTextWithHint("##cname", Tr("common.name"), &name);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(120));
				const bool enter = ImGui::InputTextWithHint("##ccmd", Tr("commands.commands"), &command, ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if ((Button("common.add", true) || enter) && !name.empty() && !command.empty()) {
					if (std::ranges::any_of(commands, [&](const auto& c) { return c.name == name; })) {
						Status(Lang::Fmt("commands.command_already_exists_choose_another", name));
					} else {
						commands.push_back({ name, command });
						Settings::Save();
						name.clear();
						command.clear();
					}
				}
			}

			g_noBadges = true;
			for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
				const auto& c = commands[i];
				const auto  id = Hotkeys::CommandId(c.name);
				ImGui::PushID(i);
				if (BeginRow(c.name.c_str(), c.command.c_str(), S(370))) {
					const bool capturing = Hotkeys::IsCapturing() && Hotkeys::CaptureTarget() == id;
					KeyChip(capturing ? "row.press_a_key" : Hotkeys::KeyName(Hotkeys::KeyOf(id)), capturing);
					ImGui::SameLine(0, S(10));
					if (Button("console.run", true)) {
						Hotkeys::RunCommand(c.command);
						Status(Lang::Fmt("commands.done", c.command));
					}
					ImGui::SameLine(0, S(6));
					if (Button("common.key")) {
						Hotkeys::BeginCapture(id);
					}
					ImGui::SameLine(0, S(6));
					if (Button("×")) {
						Hotkeys::Clear(id);
						commands.erase(commands.begin() + i);
						Settings::Save();
						EndRow(nullptr);
						ImGui::PopID();
						break;
					}
					EndRow(nullptr);
				}
				ImGui::PopID();
			}
			g_noBadges = false;
			if (commands.empty() && !g_searching) {
				Muted("commands.no_commands_yet");
			}
			EndCard();
		}

		// One action in the hotkey list: key chip, assign, clear.
		void HotkeyRow(const Hotkeys::Action& a_action)
		{
			const bool capturing = Hotkeys::IsCapturing() && Hotkeys::CaptureTarget() == a_action.id;
			const auto key = Hotkeys::KeyOf(a_action.id);
			const float controls = S(key ? 250.0f : 215.0f);
			if (!BeginRow(a_action.name, nullptr, controls)) {
				return;
			}
			KeyChip(capturing ? "hotkeys.press" : Hotkeys::KeyName(key), capturing);
			ImGui::SameLine(0, S(8));
			if (Button("hotkeys.assign")) {
				Hotkeys::BeginCapture(a_action.id);
			}
			if (key) {
				ImGui::SameLine(0, S(6));
				if (Button("×")) {
					Hotkeys::Clear(a_action.id);
				}
			}
			EndRow(nullptr);
		}

		void PageService()
		{
			auto& settings = Settings::Get();
			Columns(
				[&] {
					BeginCard("card.appearance");
					if (BeginRow("service.language", "service.menu_text_language", S(220))) {
						int lang = Lang::Current() == Lang::Id::kRussian ? 0 : 1;
						if (Segmented("language", &lang, { "Русский", "English" })) {
							Lang::Set(lang == 0 ? Lang::Id::kRussian : Lang::Id::kEnglish);
						}
						EndRow(nullptr);
					}
					if (BeginRow("service.theme", "service.light_parchment_or_dark_gold", S(200))) {
						int theme = settings.dark ? 1 : 0;
						if (Segmented("theme", &theme, { "service.light", "service.dark" })) {
							settings.dark = theme == 1;
							Settings::Save();
							ApplyStyle();
						}
						EndRow(nullptr);
					}
					if (BeginRow("service.density", "service.compact_less_padding_more_rows", S(250))) {
						int density = settings.compact ? 1 : 0;
						if (Segmented("density", &density, { "service.normal", "service.compact" })) {
							settings.compact = density == 1;
							Settings::Save();
							ApplyStyle();
						}
						EndRow(nullptr);
					}
					SliderRow("service.menu_size", &g_scalePct, 60.0f, 160.0f, "%.0f", "service.scale_of_the_whole_menu", [] {
						Settings::Get().uiScale = std::round(g_scalePct / 5.0f) * 0.05f;
						g_scalePct = Settings::Get().uiScale * 100.0f;
						Settings::Save();
						ApplyStyle();
					},
						false);
					EndCard();

					BeginCard("card.controls");
					if (BeginRow("service.menu_hotkey", "service.which_key_opens_and_closes", S(250))) {
						const bool capturing = Hotkeys::IsCapturing() && Hotkeys::CaptureTarget() == Hotkeys::kMenuKey;
						KeyChip(capturing ? "row.press_a_key" : ToggleKeyName(), capturing);
						ImGui::SameLine(0, S(10));
						if (Button("service.change")) {
							Hotkeys::BeginCapture(std::string{ Hotkeys::kMenuKey });
						}
						EndRow(nullptr);
					}
					EndCard();

					BeginCard("card.about");
					if (!g_searching) {
						ImGui::TextWrapped(Tr("service.open_close_esc_close_items"),
							ToggleKeyName().c_str(), static_cast<int>(Catalog::Forms().size()), static_cast<int>(Catalog::Plugins().size()));
						if (Button("service.reload_lists")) {
							Catalog::BuildForms();
							Catalog::BuildMarkers();
							g_keys.clear();
							g_items.dirty = g_magic.dirty = g_actors.dirty = true;
						}
					}
					EndCard();
				},
				[&] { CommandsCard(); });

			BeginCard("card.hotkeys");
			if (!g_searching) {
				Muted("service.work_in_game_while_the");
			}
			g_noBadges = true;
			const auto actions = Hotkeys::Actions();
			const auto half = (actions.size() + 1) / 2;
			auto       column = [&](std::size_t a_from, std::size_t a_to) {
				return [=] {
					for (std::size_t i = a_from; i < a_to; ++i) {
						ImGui::PushID(static_cast<int>(i));
						HotkeyRow(actions[i]);
						ImGui::PopID();
					}
				};
			};
			Columns(column(0, half), column(half, actions.size()));
			g_noBadges = false;
			EndCard();
		}

		void PageHome()
		{
			auto  player = RE::PlayerCharacter::GetSingleton();
			auto& st = Cheats::Get();
			if (!player) {
				return;
			}
			auto avo = player->AsActorValueOwner();

			// ---- hero card: who, level, vitals
			{
				const auto name = Text::ToUtf8(player->GetName());
				BeginCard(name.empty() ? "page.dragonborn" : name.c_str());
				if (const auto logo = LogoTexture()) {
					// The emblem, faint, in the corner of the hero card.
					const auto  win = ImGui::GetWindowPos();
					const float h = S(120);
					const float w = h * LogoData::kWidth / LogoData::kHeight;
					const float x = win.x + ImGui::GetWindowWidth() - w - S(20);
					ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(logo), { x, win.y + S(8) }, { x + w, win.y + S(8) + h }, { 0, 0 }, { 1, 1 },
						Fade(T().gold, 0.10f));
				}
				const auto race = player->GetRace() ? Text::ToUtf8(player->GetRace()->GetFullName()) : std::string{};
				if (ImGui::BeginTable("##hero", 2, ImGuiTableFlags_SizingStretchProp)) {
					ImGui::TableSetupColumn("##who", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("##vitals", ImGuiTableColumnFlags_WidthStretch, 1.6f);
					ImGui::TableNextColumn();
					ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
					ImGui::Text(Tr("home.level"), race.c_str(), race.empty() ? "" : " · ", player->GetLevel());
					ImGui::PopStyleColor();
					if (auto skills = player->GetPlayerRuntimeData().skills; skills && skills->data) {
						Bar("home.level_progress", skills->data->xp, skills->data->levelThreshold, T().gold);
					}
					std::string place;
					if (auto location = player->GetCurrentLocation()) {
						place = Text::ToUtf8(location->GetFullName());
					}
					if (place.empty() && player->GetParentCell()) {
						place = Text::ToUtf8(player->GetParentCell()->GetFullName());
					}
					DrawIcon(ImGui::GetWindowDrawList(), Icon::kPin, { ImGui::GetCursorScreenPos().x + S(8), ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f },
						S(15), T().accent);
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + S(22));
					ImGui::TextUnformatted(place.empty() ? Tr("home.somewhere_in_skyrim") : place.c_str());

					ImGui::TableNextColumn();
					Bar("common.health", avo->GetActorValue(RE::ActorValue::kHealth), player->GetActorValueMax(RE::ActorValue::kHealth), kHealthCol);
					Bar("common.magicka", avo->GetActorValue(RE::ActorValue::kMagicka), player->GetActorValueMax(RE::ActorValue::kMagicka), kMagickaCol);
					Bar("home.stamina", avo->GetActorValue(RE::ActorValue::kStamina), player->GetActorValueMax(RE::ActorValue::kStamina), kStaminaCol);
					ImGui::EndTable();
				}
				EndCard();
			}

			// ---- stat tiles
			{
				struct Tile
				{
					Icon        icon;
					std::string value;
					const char* label;
				};
				const Tile tiles[] = {
					{ Icon::kCoin, std::format("{}", player->GetGoldAmount()), "feat.gold" },
					{ Icon::kSparkles, std::format("{}", player->GetPlayerRuntimeData().perkCount), "progress.perk_points" },
					{ Icon::kDragon, std::format("{:.0f}", avo->GetActorValue(RE::ActorValue::kDragonSouls)), "feat.dragon_souls" },
					{ Icon::kPin, std::format("{}", Cheats::SavedPoints().size()), "card.my_points" },
				};
				if (ImGui::BeginTable("##tiles", static_cast<int>(std::size(tiles)), ImGuiTableFlags_None)) {
					for (const auto& tile : tiles) {
						ImGui::TableNextColumn();
						const auto  p = ImGui::GetCursorScreenPos();
						const float w = ImGui::GetContentRegionAvail().x - S(4);
						const float h = S(70);
						auto        dl = ImGui::GetWindowDrawList();
						dl->AddRectFilled(p, { p.x + w, p.y + h }, T().card);
						dl->AddRect(p, { p.x + w, p.y + h }, T().line);
						Corners(dl, p, { p.x + w, p.y + h }, S(7), S(3), Fade(T().gold, 0.7f), 1.0f);
						DrawIcon(dl, tile.icon, { p.x + S(30), p.y + h * 0.5f }, S(24), T().gold);
						PushFont(Overlay::GetFonts().heading);
						dl->AddText({ p.x + S(56), p.y + S(12) }, T().title, tile.value.c_str());
						ImGui::PopFont();
						PushFont(Overlay::GetFonts().caption);
						dl->AddText({ p.x + S(56), p.y + S(42) }, T().muted, Tr(tile.label));
						ImGui::PopFont();
						ImGui::Dummy({ w, h });
					}
					ImGui::EndTable();
				}
				ImGui::Spacing();
			}

			Columns(
				[&] {
					BeginCard("card.quick_toggles");
					bool god = Cheats::IsGodMode();
					SwitchRow("feat.god_mode", &god, "home.no_damage_is_taken_tgm", [](bool v) { Cheats::SetGodMode(v); });
					SwitchRow("feat.infinite_health", &st.infiniteHealth, "dovahkiin.health_damage_never_gets_through", nullptr);
					SwitchRow("feat.infinite_stamina", &st.infiniteStamina, "dovahkiin.sprinting_power_attacks_and_blocking", nullptr);
					SwitchRow("feat.infinite_magicka", &st.infiniteMagicka, "dovahkiin.magicka_always_refills_to_the", nullptr);
					SliderRow("dovahkiin.health_regen", &st.healthRegen, 0.5f, 50.0f, "%.1f", "dovahkiin.how_fast_the_dragonborn_s",
						[] { Cheats::ApplyPersistent(); });
					SliderRow("dovahkiin.stamina_regen", &st.staminaRegen, 0.5f, 50.0f, "%.1f", "dovahkiin.how_fast_stamina_regenerates_1",
						[] { Cheats::ApplyPersistent(); });
					SwitchRow("feat.no_weight_limit", &st.noCarryLimit, "home.carry_weight_100_000", [](bool) { Cheats::ApplyPersistent(); });
					EndCard();

					BeginCard("card.quick_actions");
					{
						bool any = false;
						for (const auto& action : Hotkeys::Actions()) {
							const auto key = Hotkeys::KeyOf(action.id);
							if (!key) {
								continue;
							}
							const auto label = std::format("{}  [{}]##qa{}", Tr(action.name), Hotkeys::KeyName(key), action.id);
							if (any) {
								ImGui::SameLine(0, S(8));
								if (ImGui::CalcTextSize(label.c_str(), nullptr, true).x + S(24) > ImGui::GetContentRegionAvail().x) {
									ImGui::NewLine();
								}
							}
							any = true;
							if (Button(label.c_str())) {
								Status(Hotkeys::Trigger(action.id));
							}
							if (ImGui::IsItemHovered()) {
								g_help = "home.same_as_the_hotkey_all";
							}
						}
						if (!any) {
							Muted("home.assign_a_hotkey_to_any");
						}
					}
					EndCard();
				},
				[&] {
					BeginCard("card.active_now");
					struct ActiveCheat
					{
						std::string           label;
						std::function<void()> off;
					};
					std::vector<ActiveCheat> active;
					auto flag = [&](bool& a_value, const char* a_label, bool a_persistent) {
						if (a_value) {
							active.push_back({ a_label, [&a_value, a_persistent] {
												  a_value = false;
												  if (a_persistent) {
													  Cheats::ApplyPersistent();
												  }
											  } });
						}
					};
					auto slider = [&](float& a_value, float a_default, std::string a_label) {
						if (std::abs(a_value - a_default) > 0.001f) {
							active.push_back({ std::move(a_label), [&a_value, a_default] {
												  a_value = a_default;
												  Cheats::ApplyPersistent();
											  } });
						}
					};
					if (Cheats::IsGodMode()) {
						active.push_back({ "feat.god_mode", [] { Cheats::SetGodMode(false); } });
					}
					flag(st.infiniteHealth, "feat.infinite_health", false);
					flag(st.infiniteStamina, "feat.infinite_stamina", false);
					flag(st.infiniteMagicka, "feat.infinite_magicka", false);
					flag(st.noCarryLimit, "feat.no_weight_limit", true);
					flag(st.noFallDamage, "feat.no_fall_damage", true);
					flag(st.weightlessBooks, "feat.weightless_books", true);
					flag(st.weightlessPotions, "flag.weightless_potions", true);
					flag(st.weightlessFood, "feat.weightless_food", true);
					flag(st.weightlessIngredients, "feat.weightless_ingredients", true);
					flag(st.weightlessScrolls, "feat.weightless_scrolls", true);
					flag(st.weightlessSoulGems, "feat.weightless_soul_gems", true);
					flag(st.weightlessCrafting, "feat.weightless_resources", true);
					flag(st.weightlessGems, "feat.weightless_gems", true);
					flag(st.weightlessMiscItems, "feat.weightless_clutter", true);
					flag(st.weightlessAmmo, "feat.weightless_arrows", true);
					flag(st.weightlessWeapons, "feat.weightless_weapons", true);
					flag(st.weightlessArmor, "feat.weightless_armor", true);
					flag(st.infiniteAmmo, "feat.infinite_arrows", false);
					flag(st.infiniteCharge, "flag.infinite_charges", false);
					flag(st.noShoutCooldown, "feat.no_shout_cooldown", true);
					flag(st.easyPickpocket, "flag.100_pickpocket", true);
					flag(st.essential, "feat.can_t_die", true);
					flag(st.immunity, "flag.immunity", true);
					flag(st.noSpellCost, "feat.free_spells", true);
					flag(st.instantCast, "feat.instant_casting", true);
					flag(st.nightEye, "feat.night_eye", true);
					flag(st.waterWalking, "feat.water_walking", true);
					flag(st.infiniteTorch, "feat.infinite_torches", true);
					flag(st.eternalBuffs, "feat.permanent_blessings", false);
					flag(st.infinitePoison, "feat.infinite_poison", false);
					flag(st.infiniteLockpicks, "feat.infinite_lockpicks", false);
					flag(st.unlimitedTraining, "feat.unlimited_training", true);
					flag(st.invisibility, "feat.invisibility", true);
					flag(st.powersNoCooldown, "feat.no_power_cooldown", true);
					flag(st.freeCraft, "feat.free_crafting", true);
					flag(st.craftNoPerks, "feat.crafting_without_perks", true);
					flag(st.infiniteOre, "flag.infinite_ore", false);
					flag(st.enchantAnything, "feat.enchant_anything", true);
					flag(st.immortalFollowers, "feat.immortal_followers", false);
					flag(st.thievesGuildQuick, "flag.thieves_guild_one_job", false);
					flag(st.noBounty, "feat.no_bounty", false);
					flag(st.flight, "feat.flight", false);
					flag(st.waterBreathing, "feat.waterbreathing", true);
					flag(st.oneHitKill, "feat.one_hit_kill", true);
					flag(st.freezeTime, "flag.time_stopped", false);
					flag(st.aimSlowmo, "feat.slow_time_while_aiming", false);
					flag(st.merchantsBuyAll, "feat.merchants_buy_everything", true);
				flag(st.persuadeAlways, "feat.speech_always_succeeds", true);
					flag(st.noDragonAttacks, "feat.no_dragon_attacks", true);
					flag(st.fastBowDraw, "feat.fast_bow_draw", true);
					flag(st.aimbot, "feat.aimbot", false);
					flag(st.killmoves, "feat.kill_moves_beta", true);
					flag(st.keepItemsInJail, "feat.keep_items_in_jail", false);
					flag(st.soulGemsKeep, "feat.soul_gems_not_consumed", false);
					flag(st.soulGemsFull, "feat.soul_gems_always_full", false);
					flag(st.anySoulInAnyGem, "feat.any_soul_in_any_gem", true);
					flag(st.autoPerks, "feat.auto_perks", false);
					flag(st.fastTravelAnywhere, "flag.travel_in_combat", false);
					flag(st.fastTravelIgnoreBans, "feat.fast_travel_anywhere", false);
					flag(st.autoUnlock, "flag.auto_unlock", false);
					flag(st.autoLoot, "feat.auto_loot", false);
					flag(st.fairTrade, "flag.base_price_trade", true);
					flag(st.richMerchants, "feat.rich_merchants", false);
					flag(st.esp, "flag.highlight", false);
					flag(st.timelessTravel, "flag.timeless_travel", false);
					flag(st.playerNormalSpeed, "feat.dragonborn_not_slowed", true);
					slider(st.attackSpeed, 1.0f, Lang::Fmt("flag.attack", st.attackSpeed));
					for (auto [school, name] : { std::pair{ &st.destruction, "skill.destruction" }, { &st.conjuration, "skill.conjuration" }, { &st.alteration, "skill.alteration" },
							 { &st.illusion, "skill.illusion" }, { &st.restoration, "skill.restoration" } }) {
						slider(school->power, 1.0f, Lang::Fmt("flag.magnitude", Tr(name), school->power));
						slider(school->duration, 1.0f, Lang::Fmt("flag.duration", Tr(name), school->duration));
						slider(school->cost, 1.0f, Lang::Fmt("flag.cost", Tr(name), school->cost));
					}
					slider(st.summonExtra, 0.0f, Lang::Fmt("flag.summons", st.summonExtra));
					slider(st.incomingMagic, 1.0f, Lang::Fmt("flag.enemy_magic", st.incomingMagic));
					slider(st.incomingMagicDuration, 1.0f, Lang::Fmt("flag.enemy_effects", st.incomingMagicDuration));
					slider(st.dualCastPower, 1.0f, Lang::Fmt("flag.dual_cast", st.dualCastPower));
					slider(st.dualCastCost, 1.0f, Lang::Fmt("flag.dual_cast_cost", st.dualCastCost));
					slider(st.magickaRegen, 1.0f, Lang::Fmt("flag.magicka_regen", st.magickaRegen));
					slider(st.healthRegen, 1.0f, Lang::Fmt("flag.health_regen", st.healthRegen));
					slider(st.staminaRegen, 1.0f, Lang::Fmt("flag.stamina_regen", st.staminaRegen));
					slider(st.smithingBoost, 0.0f, Lang::Fmt("flag.smithing", st.smithingBoost));
					slider(st.alchemyBoost, 0.0f, Lang::Fmt("flag.alchemy", st.alchemyBoost));
					slider(st.enchantingBoost, 0.0f, Lang::Fmt("flag.enchanting", st.enchantingBoost));
					slider(st.enchantEffects, 1.0f, Lang::Fmt("flag.effects_per_item", st.enchantEffects));
					slider(st.perksPerLevel, 1.0f, Lang::Fmt("flag.perks_per_level", st.perksPerLevel));
					slider(st.speedMult, 100.0f, Lang::Fmt("flag.speed", st.speedMult));
					slider(st.jumpMult, 1.0f, Lang::Fmt("flag.jump", st.jumpMult));
					slider(st.damageDealtMult, 1.0f, Lang::Fmt("flag.damage", st.damageDealtMult));
					slider(st.damageTakenMult, 1.0f, Lang::Fmt("flag.damage_taken", st.damageTakenMult));
					slider(st.skillXpMult, 1.0f, Lang::Fmt("flag.leveling", st.skillXpMult));
					slider(st.skillCap, 100.0f, Lang::Fmt("flag.skill_cap", st.skillCap));
					slider(st.worldSpeed, 1.0f, Lang::Fmt("flag.world_speed", st.worldSpeed));

					if (active.empty()) {
						Muted("home.nothing_is_on_the_game");
					} else {
						bool first = true;
						for (const auto& cheat : active) {
							if (Chip(cheat.label.c_str(), first)) {
								cheat.off();
								Status(Lang::Fmt("home.turned_off", cheat.label));
							}
							if (ImGui::IsItemHovered()) {
								g_help = "home.click_to_turn_off";
							}
						}
					}
					EndCard();

					BeginCard("card.recent_actions");
					if (g_history.empty()) {
						Muted("home.actions_from_the_menu_and");
					}
					for (std::size_t i = 0; i < g_history.size() && i < 6; ++i) {
						const auto& entry = g_history[i];
						const int   ago = static_cast<int>(Since(entry.time));
						const auto  when = ago < 60 ? std::string{ Tr("home.just_now") } : ago < 3600 ? Lang::Fmt("home.min_ago", ago / 60) : Lang::Fmt("home.h_ago", ago / 3600);
						const auto  p = ImGui::GetCursorScreenPos();
						Diamond(ImGui::GetWindowDrawList(), { p.x + S(5), p.y + ImGui::GetTextLineHeight() * 0.5f }, S(3), T().gold, T().gold, 1.0f);
						ImGui::SetCursorScreenPos({ p.x + S(16), p.y });
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - S(110));
						ImGui::TextUnformatted(entry.text.c_str());
						ImGui::PopTextWrapPos();
						ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(when.c_str()).x);
						ImGui::PushStyleColor(ImGuiCol_Text, T().muted);
						ImGui::TextUnformatted(when.c_str());
						ImGui::PopStyleColor();
					}
					EndCard();

					BeginCard("card.recently_added");
					const auto& recent = Settings::Get().recent;
					int         shown = 0;
					for (const auto& key : recent) {
						if (shown == 5) {
							break;
						}
						const auto entry = FindByKey(key);
						if (!entry) {
							continue;
						}
						ImGui::PushID(shown++);
						const auto group = Catalog::GroupOf(entry->category);
						const char* button = group == Catalog::Group::kMagic ? "common.learn" : group == Catalog::Group::kActors ? "common.summon" : "home.more";
						if (BeginRow(entry->name.c_str(), Catalog::CategoryName(entry->category), ImGui::CalcTextSize(button).x + S(20))) {
							if (Button(button, true)) {
								GrantOne(*entry, 1);
							}
							EndRow(nullptr);
						}
						ImGui::PopID();
					}
					if (shown == 0) {
						Muted("home.items_spells_and_npcs_given");
					}
					EndCard();
				});
		}

		void DrawPage()
		{
			switch (g_page) {
			case Page::kHome: PageHome(); break;
			case Page::kDovahkiin: PageDovahkiin(); break;
			case Page::kCombat: PageCombat(); break;
			case Page::kMagic: DrawBrowser(g_magic, Catalog::Group::kMagic); break;
			case Page::kSchools: PageSchools(); break;
			case Page::kEnchant: PageEnchant(); break;
			case Page::kProgress: PageProgress(); break;
			case Page::kWorld: PageWorld(); break;
			case Page::kTeleport: PageTeleport(); break;
			case Page::kTarget: PageTarget(); break;
			case Page::kConsole: PageConsole(); break;
			case Page::kItems: DrawBrowser(g_items, Catalog::Group::kItems); break;
			case Page::kActors: DrawBrowser(g_actors, Catalog::Group::kActors); break;
			case Page::kService: PageService(); break;
			case Page::kQuests: PageQuests(); break;
			}
		}

		// Results of the header search: every settings page, filtered, as one list,
		// plus a shortcut into the item / spell / NPC lists.
		void PageSearch()
		{
			struct Searchable
			{
				Page page;
				void (*draw)();
			};
			constexpr Searchable pages[] = {
				{ Page::kDovahkiin, PageDovahkiin },
				{ Page::kCombat, PageCombat },
				{ Page::kSchools, PageSchools },
				{ Page::kProgress, PageProgress },
				{ Page::kWorld, PageWorld },
				{ Page::kTarget, PageTarget },
				{ Page::kService, PageService },
			};
			g_searching = true;
			g_searchHits = 0;
			for (const auto& p : pages) {
				g_searchPage = p.page;
				g_searchPageShown = false;
				g_searchCard = nullptr;
				ImGui::PushID(static_cast<int>(p.page));
				p.draw();
				ImGui::PopID();
			}
			g_searching = false;

			if (g_searchHits == 0) {
				Muted("search.nothing_found_among_the_settings");
			}

			// How many forms of each list match, recounted only when the query changes.
			static std::string counted;
			static int         counts[3]{};
			if (counted != g_search) {
				counted = g_search;
				std::ranges::fill(counts, 0);
				const auto needle = Text::ToLower(g_search);
				for (const auto& e : Catalog::Forms()) {
					if (Text::Contains(e.nameLower, needle)) {
						++counts[static_cast<int>(Catalog::GroupOf(e.category))];
					}
				}
			}
			const std::tuple<Catalog::Group, Page, Browser*, const char*> lists[] = {
				{ Catalog::Group::kItems, Page::kItems, &g_items, "search.items" },
				{ Catalog::Group::kMagic, Page::kMagic, &g_magic, "page.magic_shouts_perks" },
				{ Catalog::Group::kActors, Page::kActors, &g_actors, "cat.creatures_and_npcs" },
			};
			if (counts[0] + counts[1] + counts[2] > 0) {
				ImGui::Dummy({ 0, S(10) });
				SubHeaderImpl("search.also_in_lists");
				for (const auto& [group, page, browser, label] : lists) {
					const int n = counts[static_cast<int>(group)];
					if (n == 0) {
						continue;
					}
					if (Button(std::format("{}: {} →##{}", Tr(label), n, label).c_str())) {
						browser->search = g_search;
						browser->view = static_cast<int>(View::kAll);
						browser->dirty = true;
						g_page = page;
						g_search.clear();
					}
					ImGui::SameLine();
				}
				ImGui::NewLine();
			}
		}

		// ---------------------------------------------------------------- frame

		float g_activeY = 0;  // active sidebar item, relative to the sidebar window
		float g_activeH = 0;
		float g_sideW = 270;  // sidebar width this frame, in pixels


		void SidebarItem(const PageInfo& a_info)
		{
			const float w = ImGui::GetContentRegionAvail().x;
			const float h = D(38, 32);
			const auto  p = ImGui::GetCursorScreenPos();
			const bool clickedPage = ImGui::InvisibleButton(a_info.name, { w, h });
			RecordControl(a_info.name);
			if (clickedPage) {
				g_page = a_info.page;
				g_search.clear();
			}
			const bool active = g_page == a_info.page;
			const bool hovered = ImGui::IsItemHovered();
			const float glow = Anim(ImGui::GetItemID(), active || hovered ? 1.0f : 0.0f);
			const float sel = Anim(ImGui::GetItemID() + 1, active ? 1.0f : 0.0f);
			auto        dl = ImGui::GetWindowDrawList();
			if (glow > 0.01f) {
				// The game's list selection: a band that fades out to the right.
				const ImU32 band = Fade(T().sideActive, glow * (0.6f + 0.4f * sel));
				dl->AddRectFilledMultiColor(p, { p.x + w, p.y + h }, band, Fade(band, 0.0f), Fade(band, 0.0f), band);
			}
			if (active) {
				g_activeY = p.y - ImGui::GetWindowPos().y;
				g_activeH = h;
			}
			const auto col = Mix(T().sideText, T().title, std::max(sel, glow * 0.5f));
			DrawIcon(dl, a_info.icon, { p.x + S(20) + S(3) * sel, p.y + h * 0.5f }, S(19), Mix(col, T().gold, sel));
			const auto font = Overlay::GetFonts().heading;
			const auto name = Upper(Tr(a_info.name));
			const float size = Overlay::GetFonts().body->LegacySize * ImGui::GetStyle().FontScaleMain;
			dl->AddText(font, size, { p.x + S(42) + S(3) * sel, p.y + (h - size) * 0.5f - S(1) }, col, name.c_str());
		}

		void DrawSidebar(float a_height)
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, T().side);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { S(14), S(20) });
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, S(2) });
			ImGui::BeginChild("##side", { g_sideW, a_height }, ImGuiChildFlags_AlwaysUseWindowPadding);

			auto       dl = ImGui::GetWindowDrawList();
			const auto p = ImGui::GetCursorScreenPos();
			{
				// The Skyrim dragon emblem, from the game's own main-menu logo mesh.
				const float h = S(54);
				const float w = h * LogoData::kWidth / LogoData::kHeight;
				if (const auto logo = LogoTexture()) {
					dl->AddImage(static_cast<ImTextureID>(logo), { p.x + S(28) - w * 0.5f, p.y + S(23) - h * 0.5f }, { p.x + S(28) + w * 0.5f, p.y + S(23) + h * 0.5f },
						{ 0, 0 }, { 1, 1 }, T().gold);
				} else {
					DrawIcon(dl, Icon::kDragon, { p.x + S(28), p.y + S(23) }, S(26), T().gold);
				}
			}
			PushFont(Overlay::GetFonts().heading);
			dl->AddText({ p.x + S(62), p.y + S(2) }, T().title, "DRAGONBORN'S CODEX");
			ImGui::PopFont();
			PushFont(Overlay::GetFonts().caption);
			dl->AddText({ p.x + S(62), p.y + S(28) }, T().sideText, Lang::Fmt("sidebar.0_2_0_open_close", ToggleKeyName()).c_str());
			ImGui::PopFont();
			ImGui::Dummy({ 0, S(52) });
			Ornament();

			for (const auto& page : kTopPages) {
				SidebarItem(page);
			}
			Ornament(0.35f);
			for (const auto& page : kMainPages) {
				SidebarItem(page);
			}

			const float bottomH = std::size(kBottomPages) * (D(38, 32) + S(2)) + S(18);
			ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - S(20) - bottomH));
			Ornament(0.35f);
			for (const auto& page : kBottomPages) {
				SidebarItem(page);
			}

			// The gold marker slides to the active page.
			if (g_barY < 0) {
				g_barY = g_activeY;
			}
			g_barY += (g_activeY - g_barY) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
			const auto  win = ImGui::GetWindowPos();
			const float x = win.x + S(14);
			const float y = win.y + g_barY;
			const ImU32 clear = Fade(T().gold, 0.0f);
			dl->AddRectFilledMultiColor({ x - S(1), y }, { x + S(1), y + g_activeH * 0.5f }, clear, clear, T().gold, T().gold);
			dl->AddRectFilledMultiColor({ x - S(1), y + g_activeH * 0.5f }, { x + S(1), y + g_activeH }, T().gold, T().gold, clear, clear);
			Diamond(dl, { x, y + g_activeH * 0.5f }, S(3.5f), T().gold, T().gold, 1.0f);

			ImGui::EndChild();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();
		}

		// The game's key glyph for a DirectInput code ("keys/insert"), or null when there is none.
		const char* KeyGlyph(std::uint32_t a_dik)
		{
			static const std::unordered_map<std::uint32_t, std::string> names = [] {
				std::unordered_map<std::uint32_t, std::string> m{
					{ 0x01, "keys/esc" }, { 0x1C, "keys/enter" }, { 0xD2, "keys/insert" }, { 0xC7, "keys/home" }, { 0xCF, "keys/end" },
					{ 0xC9, "keys/pgup" }, { 0xD1, "keys/pgdn" }, { 0xD3, "keys/delete" }, { 0x0F, "keys/tab" }, { 0x39, "keys/space" },
					{ 0x0E, "keys/backspace" }, { 0x29, "keys/tilde" }, { 0x1D, "keys/l-ctrl" }, { 0x9D, "keys/r-ctrl" }, { 0x2A, "keys/l-shift" },
					{ 0x38, "keys/l-alt" }, { 0x57, "keys/f11" }, { 0x58, "keys/f12" }, { 0xC8, "keys/up" }, { 0xD0, "keys/down" },
				};
				for (int i = 0; i < 10; ++i) {
					m[0x3B + i] = std::format("keys/f{}", i + 1);
					m[0x02 + i] = std::format("keys/{}", (i + 1) % 10);
				}
				const std::pair<std::uint32_t, const char*> rows[] = { { 0x10, "qwertyuiop" }, { 0x1E, "asdfghjkl" }, { 0x2C, "zxcvbnm" } };
				for (const auto& [first, letters] : rows) {
					for (std::size_t i = 0; letters[i]; ++i) {
						m[first + static_cast<std::uint32_t>(i)] = std::format("keys/{}", letters[i]);
					}
				}
				return m;
			}();
			const auto it = names.find(a_dik);
			return it != names.end() ? it->second.c_str() : nullptr;
		}

		// Draws one hint ("[glyph] [glyph] label") ending at a_right; returns its width. A missing glyph is drawn as
		// a framed key name.
		float KeyHint(ImDrawList* a_dl, float a_right, float a_y, const std::vector<std::string>& a_glyphs, const char* a_label)
		{
			a_label = Tr(a_label);
			const float h = ImGui::GetFontSize() * 1.45f;
			const float labelW = ImGui::CalcTextSize(a_label).x;
			float       w = labelW + S(6);
			std::vector<std::pair<Textures::Image, std::string>> parts;
			for (const auto& g : a_glyphs) {
				const auto image = g.starts_with("keys/") ? Textures::Get(g) : Textures::Image{};
				parts.emplace_back(image, g);
				w += (image ? h * image.width / image.height : ImGui::CalcTextSize(g.c_str()).x + S(12)) + S(2);
			}
			float x = a_right - w;
			for (const auto& [image, name] : parts) {
				if (image) {
					const float gw = h * image.width / image.height;
					a_dl->AddImage(static_cast<ImTextureID>(image.id), { x, a_y - h * 0.5f }, { x + gw, a_y + h * 0.5f }, { 0, 0 }, { 1, 1 }, Fade(IM_COL32_WHITE, g_alpha));
					x += gw + S(2);
				} else {
					const float tw = ImGui::CalcTextSize(name.c_str()).x + S(12);
					a_dl->AddRect({ x, a_y - h * 0.4f }, { x + tw, a_y + h * 0.4f }, T().muted, S(2));
					a_dl->AddText({ x + S(6), a_y - ImGui::GetFontSize() * 0.5f }, T().text, name.c_str());
					x += tw + S(2);
				}
			}
			a_dl->AddText({ x + S(4), a_y - ImGui::GetFontSize() * 0.5f }, T().muted, a_label);
			return w;
		}

		// The bottom-right button bar, as in the game's menus: keyboard keys, or the gamepad's when it is in use.
		float KeyHints(ImDrawList* a_dl, ImVec2 a_rightCenter)
		{
			PushFont(Overlay::GetFonts().caption);
			const auto toggle = KeyGlyph(Settings::Get().toggleKey);
			std::vector<std::pair<std::vector<std::string>, const char*>> hints;
			if (GImGui->NavInputSource == ImGuiInputSource_Gamepad) {
				hints = { { { "keys/360_a" }, "keys.select" }, { { "keys/360_b" }, "keys.close" }, { { "keys/360_lb", "keys/360_rb" }, "keys.sections" },
					{ { "keys/360_ls", "keys/360_rs" }, "keys.menu" } };
			} else {
				hints = { { { "keys/esc" }, "keys.close" }, { { toggle ? toggle : ToggleKeyName() }, "keys.menu" }, { { "keys/l-ctrl", "keys/f" }, "keys.search" },
					{ { "keys/mouse2" }, "common.key" } };
			}
			float x = a_rightCenter.x;
			for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
				x -= KeyHint(a_dl, x, a_rightCenter.y, it->first, it->second) + S(18);
			}
			ImGui::PopFont();
			return a_rightCenter.x - x;
		}

		void DrawMain(float a_height)
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, T().bg);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { S(28), S(16) });
			ImGui::BeginChild("##main", { 0, a_height }, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

			// header: status, search, close
			ImGui::PushStyleColor(ImGuiCol_Text, T().ok);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Tr(Catalog::Forms().empty() ? "main.loading_lists" : "main.game_connected"));
			ImGui::PopStyleColor();

			const float searchW = S(380);
			const float langW = S(44);
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - searchW - langW - S(54));
			{
				// Active keyboard layout, so it is obvious which language the search will type in.
				const auto lang = Overlay::KeyboardLanguage();
				const auto p = ImGui::GetCursorScreenPos();
				const float h = ImGui::GetFrameHeight();
				auto        dl = ImGui::GetWindowDrawList();
				dl->AddRect(p, { p.x + langW, p.y + h }, T().line, S(2));
				const auto ts = ImGui::CalcTextSize(lang.c_str());
				dl->AddText({ p.x + (langW - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f }, T().accent, lang.c_str());
				if (ImGui::InvisibleButton("##lang", { langW, h })) {
					Overlay::RequestNextKeyboardLayout();
				}
				if (ImGui::IsItemHovered()) {
					g_help = "main.current_keyboard_layout_click_alt";
				}
				ImGui::SameLine(0, S(10));
			}
			{
				const auto p = ImGui::GetCursorScreenPos();
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(2));
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { S(32), S(6) });
				ImGui::SetNextItemWidth(searchW);
				if (g_focusSearch) {
					ImGui::SetKeyboardFocusHere();
					g_focusSearch = false;
				}
				ImGui::InputTextWithHint("##search", Tr("main.search_the_whole_menu_ctrl"), &g_search);
				ImGui::PopStyleVar(2);
				DrawIcon(ImGui::GetWindowDrawList(), Icon::kSearch, { p.x + S(16), p.y + ImGui::GetFrameHeight() * 0.5f + S(1) }, S(15), T().muted);
			}
			ImGui::SameLine();
			{
				const auto p = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##close", { S(36), S(36) })) {
					Input::SetMenuOpen(false);
				}
				DrawIcon(ImGui::GetWindowDrawList(), Icon::kClose, { p.x + S(18), p.y + S(18) }, S(18), ImGui::IsItemHovered() ? T().accent : T().text);
			}

			// Page changes restart the content fade-in.
			static Page lastPage = g_page;
			static bool lastSearching = false;
			const bool  searching = !g_search.empty();
			if (lastPage != g_page || lastSearching != searching) {
				if (g_page == Page::kProgress && lastPage != g_page) {
					RefreshStats();
				}
				lastPage = g_page;
				lastSearching = searching;
				g_pageTime = Clock::now();
			}

			// title
			PushFont(Overlay::GetFonts().title);
			ImGui::PushStyleColor(ImGuiCol_Text, T().title);
			ImGui::TextUnformatted(searching ? Lang::Fmt("main.search", g_search).c_str() : Upper(Tr(PageName(g_page))).c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			{
				const auto  p = ImGui::GetCursorScreenPos();
				const float w = ImGui::GetContentRegionAvail().x;
				auto        dl = ImGui::GetWindowDrawList();
				const ImU32 clear = Fade(T().gold, 0.0f);
				dl->AddRectFilledMultiColor({ p.x + S(8), p.y }, { p.x + w * 0.75f, p.y + std::max(1.0f, S(1)) }, Fade(T().gold, 0.6f), clear, clear, Fade(T().gold, 0.6f));
				Diamond(dl, { p.x + S(3), p.y + S(0.5f) }, S(3), T().gold, T().gold, 1.0f);
				ImGui::Dummy({ 0, S(8) });
			}

			// page content
			const float footerH = S(46);
			const float openAlpha = g_alpha;
			const float pageT = EaseOut(Since(g_pageTime) / 0.2f);
			const float slide = S(10) * (1.0f - pageT);
			SetAlpha(openAlpha * pageT);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slide);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
			// Every page keeps its own scroll position (one shared child made pages open half-scrolled).
			const auto contentId = searching ? std::string{ "##content_search" } : std::format("##content{}", static_cast<int>(g_page));
			ImGui::BeginChild(contentId.c_str(), { 0, ImGui::GetContentRegionAvail().y - footerH });
			if (g_scrollRequest) {
				ImGui::SetScrollY(*g_scrollRequest < 0 ? ImGui::GetScrollMaxY() : *g_scrollRequest);
				g_scrollRequest.reset();
			}
			if (searching) {
				PageSearch();
			} else {
				DrawPage();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			SetAlpha(openAlpha);

			// footer hint
			{
				const auto p = ImGui::GetCursorScreenPos();
				const float w = ImGui::GetContentRegionAvail().x;
				auto        dl = ImGui::GetWindowDrawList();
				Rule(dl, p.x - S(28), p.x + w + S(28), p.y, T().line, S(8));
				Diamond(dl, { p.x + w * 0.5f, p.y + S(0.5f) }, S(3.5f), Fade(T().gold, 0.0f), T().gold, 1.0f);
				const float glyphsW = KeyHints(dl, { p.x + w, p.y + footerH * 0.5f });
				DrawIcon(dl, Icon::kInfo, { p.x + S(9), p.y + footerH * 0.5f }, S(17), T().accent);
				std::string hint;
				ImU32       col = T().muted;
				if (Hotkeys::IsCapturing()) {
					const auto target = Hotkeys::CaptureTarget();
					const auto actions = Hotkeys::Actions();
					const auto action = std::ranges::find_if(actions, [&](const auto& a) { return target == a.id; });
					hint = Lang::Fmt("main.press_a_key_for_esc",
						target == Hotkeys::kMenuKey ? Tr("main.open_menu") : action != actions.end() ? Tr(action->name) : target.c_str());
					col = T().accent;
				} else if (!g_help.empty()) {
					hint = Tr(g_help.c_str());
				} else if (!g_status.empty()) {
					hint = Tr(g_status.c_str());
				} else {
					hint = Tr("main.changes_apply_at_once_right");
				}
				PushFont(Overlay::GetFonts().caption);
				dl->PushClipRect({ p.x, p.y }, { p.x + w - glyphsW - S(16), p.y + footerH }, true);
				dl->AddText({ p.x + S(28), p.y + (footerH - ImGui::GetFontSize()) * 0.5f }, col, hint.c_str());
				dl->PopClipRect();
				ImGui::PopFont();
			}

			ImGui::EndChild();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}

	namespace
	{
		bool g_conversationAtOpen = false;

		bool InConversation()
		{
			auto ui = RE::UI::GetSingleton();
			return ui && (ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME) || ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME));
		}
	}

	void ApplyStyle()
	{
		const auto& t = Base();
		auto&       style = ImGui::GetStyle();
		style = ImGuiStyle{};
		style.FontScaleMain = Overlay::Scale();  // fonts are loaded at their 1080p size
		style.WindowRounding = 0;
		style.ChildRounding = 0;
		style.FrameRounding = S(2);
		style.PopupRounding = S(2);
		style.GrabRounding = S(1);
		style.ScrollbarRounding = 0;
		style.TabRounding = S(2);
		style.WindowBorderSize = 0;
		style.ChildBorderSize = 1;
		style.FramePadding = { S(10), S(6) };
		style.ItemSpacing = { S(10), Settings::Get().compact ? S(5) : S(8) };
		style.ScrollbarSize = S(10);
		style.CellPadding = { S(8), S(5) };

		auto& c = style.Colors;
		c[ImGuiCol_Text] = V(t.text);
		c[ImGuiCol_TextDisabled] = V(t.muted);
		c[ImGuiCol_WindowBg] = V(t.bg);
		c[ImGuiCol_ChildBg] = V(t.bg);
		c[ImGuiCol_PopupBg] = V(t.card);
		c[ImGuiCol_Border] = V(t.line);
		c[ImGuiCol_FrameBg] = V(t.card);
		c[ImGuiCol_FrameBgHovered] = V(t.hover);
		c[ImGuiCol_FrameBgActive] = V(t.hover);
		c[ImGuiCol_Header] = V(t.seg);
		c[ImGuiCol_HeaderHovered] = V(t.hover);
		c[ImGuiCol_HeaderActive] = V(t.line);
		c[ImGuiCol_TableHeaderBg] = V(t.seg);
		c[ImGuiCol_TableRowBg] = V(t.card);
		c[ImGuiCol_TableRowBgAlt] = V(t.bg);
		c[ImGuiCol_TableBorderLight] = V(t.line);
		c[ImGuiCol_ScrollbarBg] = V(t.bg);
		c[ImGuiCol_ScrollbarGrab] = V(t.line);
		c[ImGuiCol_ScrollbarGrabHovered] = V(t.gold);
		c[ImGuiCol_ScrollbarGrabActive] = V(t.accent);
		c[ImGuiCol_CheckMark] = V(t.accent);
		c[ImGuiCol_SliderGrab] = V(t.accent);
		c[ImGuiCol_Button] = V(t.seg);
		c[ImGuiCol_ButtonHovered] = V(t.line);
		c[ImGuiCol_ButtonActive] = V(t.gold);
		c[ImGuiCol_TextSelectedBg] = V((t.gold & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 90));
		c[ImGuiCol_NavCursor] = V(t.gold);
		c[ImGuiCol_PopupBg] = V((t.bg & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 245));
		g_cur = t;

		// Readability: text against the page background (composited over black, as the game behind is dimmed).
		const auto opaque = [](ImU32 a_col) { return Mix(IM_COL32(0, 0, 0, 255), a_col | IM_COL32_A_MASK, ((a_col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f); };
		const std::pair<const char*, float> checks[] = { { "text", Contrast(t.text, opaque(t.bg)) }, { "muted", Contrast(t.muted, opaque(t.bg)) },
			{ "accent", Contrast(t.accent, opaque(t.bg)) }, { "onAccent", Contrast(t.onAccent, t.accent) }, { "sideText", Contrast(t.sideText, opaque(t.side)) } };
		for (const auto& [name, ratio] : checks) {
			if (ratio < 4.5f) {
				logger::warn("Theme {}: {} contrast {:.1f} is below 4.5", Settings::Get().dark ? "dark" : "light", name, ratio);
			}
		}
	}

	void CyclePage(int a_step)
	{
		std::vector<Page> order;
		for (const auto& list : { std::span<const PageInfo>{ kTopPages }, std::span<const PageInfo>{ kMainPages }, std::span<const PageInfo>{ kBottomPages } }) {
			for (const auto& info : list) {
				order.push_back(info.page);
			}
		}
		const auto it = std::ranges::find(order, g_page);
		const auto i = it == order.end() ? 0 : static_cast<int>(it - order.begin());
		const auto n = static_cast<int>(order.size());
		g_page = order[((i + a_step) % n + n) % n];
		g_search.clear();
	}

	void OnOpen()
	{
		if (Catalog::Forms().empty()) {
			Catalog::BuildForms();
		}

		g_target = RE::CrosshairPickData::GetSingleton()->GetActiveTarget();
		if (!g_target) {
			g_target = RE::Console::GetSelectedRefHandle();
		}
		g_targetName.clear();
		if (auto ref = g_target.get()) {
			g_targetName = std::format("{} ({:08X})", Text::ToUtf8(ref->GetDisplayFullName()), ref->GetFormID());
		}

		if (auto calendar = RE::Calendar::GetSingleton()) {
			g_hour = calendar->GetHour();
			g_timescale = calendar->GetTimescale();
		}
		g_playerScale = Cheats::PlayerScale();
		RefreshStats();
		g_conversationAtOpen = InConversation();
		g_scalePct = Settings::Get().uiScale * 100.0f;
		g_status.clear();
		g_openTime = Clock::now();
		g_pageTime = g_openTime;
	}

	namespace
	{
		enum : int
		{
			kEdgeLeft = 1,
			kEdgeRight = 2,
			kEdgeTop = 4,
			kEdgeBottom = 8,
			kMove = 16  // dragged by an empty spot
		};
		int    g_resizeHover = 0;  // the edges under the mouse, or being dragged
		int    g_resizeDrag = 0;
		ImVec2 g_resizeFrom{};  // the mouse at the press
		ImVec2 g_resizePos{};
		ImVec2 g_resizeSize{};

		// Sized from the screen and centered: wide screens (21:9) get a wider window, never more than they have.
		std::pair<ImVec2, ImVec2> DefaultRect(ImVec2 a_screen)
		{
			const ImVec2 size{ std::min(std::clamp(a_screen.x * 0.66f, S(1040), S(1420)), a_screen.x - S(40)),
				std::min(std::clamp(a_screen.y * 0.70f, S(620), S(800)), a_screen.y - S(40)) };
			return { { (a_screen.x - size.x) * 0.5f, (a_screen.y - size.y) * 0.5f }, size };
		}

		// The window's place and size: the default, or where the user dragged it (by any empty spot) and what they
		// dragged its edges and corners to, kept in the settings as parts of the screen. A double click on an edge
		// brings the default back.
		std::pair<ImVec2, ImVec2> WindowRect()
		{
			const auto& io = ImGui::GetIO();
			const auto  screen = io.DisplaySize;
			auto&       saved = Settings::Get().window;
			auto [pos, size] = DefaultRect(screen);
			if (saved[2] > 0.0f) {
				pos = { saved[0] * screen.x, saved[1] * screen.y };
				size = { saved[2] * screen.x, saved[3] * screen.y };
			}
			const ImVec2 minSize{ std::min(S(860), screen.x), std::min(S(520), screen.y) };
			size = { std::clamp(size.x, minSize.x, screen.x), std::clamp(size.y, minSize.y, screen.y) };
			pos = { std::clamp(pos.x, 0.0f, screen.x - size.x), std::clamp(pos.y, 0.0f, screen.y - size.y) };

			// the grab band: a little inside the frame, more outside it (nothing else there to click)
			const auto  m = io.MousePos;
			const float in = S(4), out = S(10);
			const bool  inX = m.x >= pos.x - out && m.x <= pos.x + size.x + out;
			const bool  inY = m.y >= pos.y - out && m.y <= pos.y + size.y + out;
			int         edges = 0;
			if (inY && m.x >= pos.x - out && m.x <= pos.x + in) {
				edges |= kEdgeLeft;
			}
			if (inY && m.x >= pos.x + size.x - in && m.x <= pos.x + size.x + out) {
				edges |= kEdgeRight;
			}
			if (inX && m.y >= pos.y - out && m.y <= pos.y + in) {
				edges |= kEdgeTop;
			}
			if (inX && m.y >= pos.y + size.y - in && m.y <= pos.y + size.y + out) {
				edges |= kEdgeBottom;
			}
			// an empty spot of the menu: not on a control (last frame's hover), nothing being typed or held
			const auto& g = *ImGui::GetCurrentContext();
			const bool  empty = !edges && g.HoveredWindow && g.HoveredWindow->RootWindow && g.HoveredWindow->RootWindow->Name &&
			                   std::string_view{ g.HoveredWindow->RootWindow->Name }.starts_with("##CheatMenu") && g.HoveredIdPreviousFrame == 0 && g.ActiveId == 0;
			const bool  popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
			if (popup) {
				edges = 0;  // a combo's list may hang over the frame
			}

			if (g_resizeDrag) {
				if (io.MouseDown[0]) {
					const ImVec2 d{ m.x - g_resizeFrom.x, m.y - g_resizeFrom.y };
					if (g_resizeDrag == kMove) {
						pos.x = std::clamp(g_resizePos.x + d.x, 0.0f, screen.x - size.x);
						pos.y = std::clamp(g_resizePos.y + d.y, 0.0f, screen.y - size.y);
					}
					if (g_resizeDrag & kEdgeRight) {
						size.x = std::clamp(g_resizeSize.x + d.x, minSize.x, screen.x - pos.x);
					}
					if (g_resizeDrag & kEdgeBottom) {
						size.y = std::clamp(g_resizeSize.y + d.y, minSize.y, screen.y - pos.y);
					}
					if (g_resizeDrag & kEdgeLeft) {
						const float right = g_resizePos.x + g_resizeSize.x;
						pos.x = std::clamp(g_resizePos.x + d.x, 0.0f, right - minSize.x);
						size.x = right - pos.x;
					}
					if (g_resizeDrag & kEdgeTop) {
						const float bottom = g_resizePos.y + g_resizeSize.y;
						pos.y = std::clamp(g_resizePos.y + d.y, 0.0f, bottom - minSize.y);
						size.y = bottom - pos.y;
					}
					saved = { pos.x / screen.x, pos.y / screen.y, size.x / screen.x, size.y / screen.y };
				} else {
					g_resizeDrag = 0;
					Settings::Save();
				}
			} else if (edges && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				saved = {};
				Settings::Save();
				std::tie(pos, size) = DefaultRect(screen);
			} else if ((edges || (empty && !popup)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				g_resizeDrag = edges ? edges : kMove;
				g_resizeFrom = m;
				g_resizePos = pos;
				g_resizeSize = size;
			}

			g_resizeHover = g_resizeDrag ? g_resizeDrag & ~kMove : edges;
			if (g_resizeDrag == kMove) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
			} else if (g_resizeHover) {
				const bool h = g_resizeHover & (kEdgeLeft | kEdgeRight);
				const bool v = g_resizeHover & (kEdgeTop | kEdgeBottom);
				const bool nwse = (g_resizeHover & kEdgeLeft && g_resizeHover & kEdgeTop) || (g_resizeHover & kEdgeRight && g_resizeHover & kEdgeBottom);
				ImGui::SetMouseCursor(h && v ? (nwse ? ImGuiMouseCursor_ResizeNWSE : ImGuiMouseCursor_ResizeNESW) :
				                      h      ? ImGuiMouseCursor_ResizeEW :
				                               ImGuiMouseCursor_ResizeNS);
			}
			return { pos, size };
		}
	}

	void Draw()
	{
		// Someone started talking (an NPC walked up) or the game asks something: step aside so it can be answered.
		if (!g_conversationAtOpen && InConversation()) {
			Input::SetMenuOpen(false);
			Toast("main.menu_closed_someone_started_talking");
			return;
		}
		g_controlsLastFrame.swap(g_controls);
		g_controls.clear();
		const auto& io = ImGui::GetIO();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
			g_focusSearch = true;
		}
		g_help.clear();

		// Fade in and rise a little on open.
		const float openT = EaseOut(Since(g_openTime) / 0.22f);
		SetAlpha(openT);

		const auto [pos, size] = WindowRect();
		g_sideW = std::clamp(size.x * 0.22f, S(240), S(300));
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGui::SetNextWindowPos({ pos.x, pos.y + S(24) * (1.0f - openT) }, ImGuiCond_Always);

		// dim the game behind the menu
		ImGui::GetBackgroundDrawList()->AddRectFilled({ 0, 0 }, io.DisplaySize, IM_COL32(0, 0, 0, static_cast<int>((Settings::Get().dark ? 120 : 100) * openT)));

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 0 });
		ImGui::Begin("##CheatMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar(2);

		const float h = ImGui::GetContentRegionAvail().y;
		DrawSidebar(h);
		ImGui::SameLine(0, 0);
		DrawMain(h);

		const auto min = ImGui::GetWindowPos();
		const auto max = ImVec2{ min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight() };
		auto       dl = ImGui::GetForegroundDrawList();
		if (!Settings::Get().dark) {
			// Parchment darkens towards its edges.
			const float v = S(70);
			const ImU32 edge = Fade(Hex(0x5A3A1A, 45), g_alpha), clear = Fade(edge, 0.0f);
			dl->AddRectFilledMultiColor(min, { max.x, min.y + v }, edge, edge, clear, clear);
			dl->AddRectFilledMultiColor({ min.x, max.y - v }, max, clear, clear, edge, edge);
			dl->AddRectFilledMultiColor(min, { min.x + v, max.y }, edge, clear, clear, edge);
			dl->AddRectFilledMultiColor({ max.x - v, min.y }, max, clear, edge, edge, clear);
		}
		dl->AddRect(min, max, Fade(T().gold, 0.55f), 0, 0, 1.0f);
		if (g_resizeHover) {  // the edges under the mouse light up
			const float w = std::max(2.0f, S(2.5f));
			const ImU32 c = T().gold;
			if (g_resizeHover & kEdgeLeft) {
				dl->AddLine(min, { min.x, max.y }, c, w);
			}
			if (g_resizeHover & kEdgeRight) {
				dl->AddLine({ max.x, min.y }, max, c, w);
			}
			if (g_resizeHover & kEdgeTop) {
				dl->AddLine(min, { max.x, min.y }, c, w);
			}
			if (g_resizeHover & kEdgeBottom) {
				dl->AddLine({ min.x, max.y }, max, c, w);
			}
		}
		Corners(dl, min, max, S(14), S(4), T().gold, std::max(1.0f, S(1.5f)));
		{
			// The sidebar border fades out at the top and bottom.
			const float sx = min.x + g_sideW, my = (min.y + max.y) * 0.5f;
			const ImU32 c = Fade(T().gold, 0.5f), cl = Fade(c, 0.0f);
			dl->AddRectFilledMultiColor({ sx, min.y }, { sx + std::max(1.0f, S(1)), my }, cl, cl, c, c);
			dl->AddRectFilledMultiColor({ sx, my }, { sx + std::max(1.0f, S(1)), max.y }, c, c, cl, cl);
		}

		ImGui::End();
		SetAlpha(1.0f);
	}

	void SetStatus(std::string a_text) { g_status = std::move(a_text); }

	bool OpenPage(std::string_view a_id)
	{
		constexpr std::pair<std::string_view, Page> kIds[] = {
			{ "home", Page::kHome }, { "dovahkiin", Page::kDovahkiin }, { "combat", Page::kCombat }, { "magic", Page::kMagic }, { "schools", Page::kSchools }, { "enchant", Page::kEnchant },
			{ "progress", Page::kProgress }, { "world", Page::kWorld }, { "teleport", Page::kTeleport }, { "quests", Page::kQuests },
			{ "target", Page::kTarget }, { "console", Page::kConsole }, { "items", Page::kItems }, { "actors", Page::kActors },
			{ "service", Page::kService },
		};
		for (const auto& [id, page] : kIds) {
			if (id == a_id) {
				g_page = page;
				g_search.clear();
				return true;
			}
		}
		return false;
	}

	void SetSearch(std::string a_text) { g_search = std::move(a_text); }
	void ScrollTo(float a_y) { g_scrollRequest = a_y; }

	std::optional<std::array<float, 4>> FindControl(std::string_view a_label)
	{
		const auto it = g_controlsLastFrame.find(std::string{ a_label });
		if (it == g_controlsLastFrame.end()) {
			return std::nullopt;
		}
		return std::array{ it->second.Min.x, it->second.Min.y, it->second.Max.x, it->second.Max.y };
	}

	std::vector<std::string> VisibleControls()
	{
		std::vector<std::string> out;
		for (const auto& [label, rect] : g_controlsLastFrame) {
			out.push_back(label);
		}
		std::ranges::sort(out);
		return out;
	}

	void Toast(std::string a_text)
	{
		g_history.insert(g_history.begin(), { Clock::now(), a_text });
		if (g_history.size() > 12) {
			g_history.pop_back();
		}
		g_toast = Tr(a_text.c_str());
		g_toastTime = Clock::now();
	}

	bool HasToast() { return !g_toast.empty() && Since(g_toastTime) < kToastLife; }

	void DrawToast()
	{
		const float age = Since(g_toastTime);
		const float alpha = std::min(EaseOut(age / 0.15f), std::clamp((kToastLife - age) / 0.4f, 0.0f, 1.0f));
		SetAlpha(alpha);

		const auto& io = ImGui::GetIO();
		auto        dl = ImGui::GetForegroundDrawList();
		PushFont(Overlay::GetFonts().body);
		const auto  ts = ImGui::CalcTextSize(g_toast.c_str());
		const float w = ts.x + S(64);
		const float h = ts.y + S(24);
		const ImVec2 min{ (io.DisplaySize.x - w) * 0.5f, S(70) - S(10) * (1.0f - EaseOut(age / 0.2f)) };
		const ImVec2 max{ min.x + w, min.y + h };
		dl->AddRectFilled(min, max, Fade(T().side, 0.92f * alpha));
		dl->AddRect(min, max, T().gold);
		Corners(dl, min, max, S(8), S(3), T().gold, S(1.5f));
		Diamond(dl, { min.x + S(24), (min.y + max.y) * 0.5f }, S(4), T().gold, T().gold, 1.0f);
		dl->AddText({ min.x + S(40), min.y + S(12) }, T().sideText, g_toast.c_str());
		ImGui::PopFont();
		SetAlpha(1.0f);
	}
}

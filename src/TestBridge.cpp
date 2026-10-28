#include "TestBridge.h"

#include "Catalog.h"
#include "Archery.h"
#include "Cheats.h"
#include "Puzzles.h"
#include "Console.h"
#include "Enchanter.h"
#include "Esp.h"
#include "FastTravel.h"
#include "Hotkeys.h"
#include "Input.h"
#include "Menu.h"
#include "Overlay.h"
#include "Settings.h"
#include "Overlay.h"
#include "Text.h"

#include <imgui.h>

#include <d3d11.h>
#include <dxgi.h>

namespace TestBridge
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Screenshot
		{
			std::string           id;
			std::filesystem::path path;
			int                   framesLeft;
		};

		std::optional<Screenshot> g_screenshot;

		// Multi-frame actions (key press then release, mouse click...): one step per frame;
		// a step returning false runs again next frame.
		std::deque<std::function<bool()>> g_steps;
		Clock::time_point         g_lastPoll;

		std::optional<std::filesystem::path> Dir()
		{
			auto dir = SKSE::log::log_directory();
			if (!dir) {
				return std::nullopt;
			}
			return *dir / "CheatMenuBridge";
		}

		// ------------------------------------------------------------ json

		std::string Quote(std::string_view a_text)
		{
			std::string out = "\"";
			for (const char c : a_text) {
				switch (c) {
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						out += std::format("\\u{:04x}", c);
					} else {
						out += c;
					}
				}
			}
			return out + "\"";
		}

		std::string Num(float a_value) { return std::isfinite(a_value) ? std::format("{:.4g}", a_value) : "null"; }

		void Reply(const std::string& a_id, bool a_ok, const std::string& a_data)
		{
			const auto dir = Dir();
			if (!dir) {
				return;
			}
			std::ofstream out{ *dir / "out.jsonl", std::ios::app };
			out << "{\"id\":" << Quote(a_id) << ",\"ok\":" << (a_ok ? "true" : "false") << ",\"data\":" << a_data << "}\n";
		}

		// ------------------------------------------------------------ parsing

		std::vector<std::string> Split(const std::string& a_line)
		{
			std::vector<std::string> parts;
			std::stringstream        stream{ a_line };
			for (std::string word; stream >> word;) {
				parts.push_back(word);
			}
			return parts;
		}

		// Everything after the first a_skip words, as typed.
		std::string Rest(const std::string& a_line, std::size_t a_skip)
		{
			std::size_t pos = 0;
			for (std::size_t i = 0; i < a_skip; ++i) {
				pos = a_line.find_first_not_of(' ', pos);
				pos = a_line.find(' ', pos);
				if (pos == std::string::npos) {
					return {};
				}
			}
			const auto start = a_line.find_first_not_of(' ', pos);
			return start == std::string::npos ? std::string{} : a_line.substr(start);
		}

		RE::FormID g_lastSpawned = 0;  // "call spawn <base>"; "last" in place of a form ID

		// "0001A2B3", "Skyrim.esm:1A2B3" or "last".
		RE::FormID ParseForm(const std::string& a_text)
		{
			if (a_text == "last") {
				return g_lastSpawned;
			}
			if (a_text == "mortalnpc") {
				// A named, unique, ordinary person from Skyrim.esm that is neither essential nor protected.
				auto person = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
				for (auto npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
					if (npc && npc->GetFormID() > 0x7 && npc->GetFormID() < 0x01000000 && npc->IsUnique() && !npc->IsEssential() && !npc->IsProtected() && !npc->UsesTemplate() &&
						npc->GetName() && *npc->GetName() && npc->race && person && npc->race->HasKeyword(person)) {
						return npc->GetFormID();
					}
				}
				return 0;
			}
			if (const auto colon = a_text.rfind(':'); colon != std::string::npos) {
				const auto local = static_cast<RE::FormID>(std::stoul(a_text.substr(colon + 1), nullptr, 16));
				return RE::TESDataHandler::GetSingleton()->LookupFormID(local, a_text.substr(0, colon));
			}
			return static_cast<RE::FormID>(std::stoul(a_text, nullptr, 16));
		}

		// "F5", "Insert", "A", "0x3F" or a decimal DirectInput code.
		std::uint32_t ParseKey(const std::string& a_text)
		{
			static const std::unordered_map<std::string, std::uint32_t> names = {
				{ "esc", 0x01 }, { "escape", 0x01 }, { "1", 0x02 }, { "2", 0x03 }, { "3", 0x04 }, { "4", 0x05 }, { "5", 0x06 }, { "6", 0x07 },
				{ "7", 0x08 }, { "8", 0x09 }, { "9", 0x0A }, { "0", 0x0B }, { "backspace", 0x0E }, { "tab", 0x0F }, { "q", 0x10 }, { "w", 0x11 },
				{ "e", 0x12 }, { "r", 0x13 }, { "t", 0x14 }, { "y", 0x15 }, { "u", 0x16 }, { "i", 0x17 }, { "o", 0x18 }, { "p", 0x19 },
				{ "enter", 0x1C }, { "lctrl", 0x1D }, { "ctrl", 0x1D }, { "a", 0x1E }, { "s", 0x1F }, { "d", 0x20 }, { "f", 0x21 }, { "g", 0x22 },
				{ "h", 0x23 }, { "j", 0x24 }, { "k", 0x25 }, { "l", 0x26 }, { "lshift", 0x2A }, { "shift", 0x2A }, { "z", 0x2C }, { "x", 0x2D },
				{ "c", 0x2E }, { "v", 0x2F }, { "b", 0x30 }, { "n", 0x31 }, { "m", 0x32 }, { "rshift", 0x36 }, { "lalt", 0x38 }, { "alt", 0x38 },
				{ "space", 0x39 }, { "f1", 0x3B }, { "f2", 0x3C }, { "f3", 0x3D }, { "f4", 0x3E }, { "f5", 0x3F }, { "f6", 0x40 }, { "f7", 0x41 },
				{ "f8", 0x42 }, { "f9", 0x43 }, { "f10", 0x44 }, { "f11", 0x57 }, { "f12", 0x58 }, { "rctrl", 0x9D }, { "ralt", 0xB8 },
				{ "home", 0xC7 }, { "up", 0xC8 }, { "pgup", 0xC9 }, { "left", 0xCB }, { "right", 0xCD }, { "end", 0xCF }, { "down", 0xD0 },
				{ "pgdn", 0xD1 }, { "insert", 0xD2 }, { "delete", 0xD3 },
			};
			auto lower = a_text;
			std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (const auto it = names.find(lower); it != names.end()) {
				return it->second;
			}
			return static_cast<std::uint32_t>(std::stoul(a_text, nullptr, 0));
		}

		std::string RefsJson(float a_meters, const std::string& a_filter)
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return "[]";
			}
			const auto origin = player->GetPosition();
			const auto needle = Text::ToLower(a_filter);
			std::vector<std::pair<float, std::string>> rows;
			RE::TES::GetSingleton()->ForEachReferenceInRange(player, a_meters * 70.0f, [&](RE::TESObjectREFR* a_ref) {
				auto base = a_ref ? a_ref->GetBaseObject() : nullptr;
				if (!base || a_ref == player) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				const auto name = Text::ToUtf8(a_ref->GetDisplayFullName());
				const auto type = std::string{ RE::FormTypeToString(base->GetFormType()) };
				if (!needle.empty() && !Text::Contains(Text::ToLower(name), needle) && !Text::Contains(Text::ToLower(type), needle)) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto        actor = a_ref->As<RE::Actor>();
				const float distance = a_ref->GetPosition().GetDistance(origin) / 70.0f;
				int         items = 0;
				if (base->Is(RE::FormType::Container) || actor) {
					for (const auto& [obj, count] : a_ref->GetInventoryCounts()) {
						items += count > 0 && !obj->Is(RE::FormType::LeveledItem) ? count : 0;
					}
				}
				rows.emplace_back(distance,
					std::format("{{\"ref\":\"{:08X}\",\"base\":\"{:08X}\",\"type\":{},\"name\":{},\"m\":{:.1f},\"disabled\":{},\"locked\":{},"
								"\"owned\":{},\"dead\":{},\"hostile\":{},\"items\":{},\"essential\":{},\"teammate\":{}}}",
						a_ref->GetFormID(), base->GetFormID(), Quote(type), Quote(name), distance, a_ref->IsDisabled() ? "true" : "false",
						a_ref->IsLocked() ? "true" : "false", a_ref->IsCrimeToActivate() ? "true" : "false", actor && actor->IsDead() ? "true" : "false",
						actor && actor->IsHostileToActor(player) ? "true" : "false", items, actor && actor->IsEssential() ? "true" : "false",
						actor && actor->IsPlayerTeammate() ? "true" : "false"));
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::ranges::sort(rows, {}, &std::pair<float, std::string>::first);
			std::string out = "[";
			for (std::size_t i = 0; i < rows.size() && i < 200; ++i) {
				out += (i ? "," : "") + rows[i].second;
			}
			return out + "]";
		}

		std::string LogTail(std::size_t a_lines)
		{
			auto dir = SKSE::log::log_directory();
			std::ifstream file{ *dir / "DragonbornsCodex.log" };
			std::deque<std::string> tail;
			for (std::string line; std::getline(file, line);) {
				tail.push_back(line);
				if (tail.size() > a_lines) {
					tail.pop_front();
				}
			}
			std::string out = "[";
			for (const auto& line : tail) {
				out += (out.size() > 1 ? "," : "") + Quote(line);
			}
			return out + "]";
		}

		// Moves the cursor onto a point and clicks it over the next frames, then replies.
		void QueueClick(const std::string& a_id, float a_x, float a_y, int a_button)
		{
			Overlay::SetCursor(a_x, a_y);
			g_steps.push_back([] { return true; });  // let ImGui see the hover first
			g_steps.push_back([a_button] {
				auto lock = Overlay::Lock();
				ImGui::GetIO().AddMouseButtonEvent(a_button, true);
				return true;
			});
			g_steps.push_back([a_id, a_x, a_y, a_button] {
				{
					auto lock = Overlay::Lock();
					ImGui::GetIO().AddMouseButtonEvent(a_button, false);
				}
				Reply(a_id, true, std::format("[{:.0f},{:.0f}]", a_x, a_y));
				return true;
			});
		}

		// Presses the left button at one point, moves to another over a few frames and lets go there, then replies.
		void QueueDrag(const std::string& a_id, float a_x1, float a_y1, float a_x2, float a_y2)
		{
			Overlay::SetCursor(a_x1, a_y1);
			g_steps.push_back([] { return true; });
			g_steps.push_back([] {
				auto lock = Overlay::Lock();
				ImGui::GetIO().AddMouseButtonEvent(0, true);
				return true;
			});
			constexpr int kSteps = 8;
			for (int i = 1; i <= kSteps; ++i) {
				const float t = static_cast<float>(i) / kSteps;
				g_steps.push_back([=] {
					Overlay::SetCursor(a_x1 + (a_x2 - a_x1) * t, a_y1 + (a_y2 - a_y1) * t);
					return true;
				});
			}
			g_steps.push_back([] { return true; });
			g_steps.push_back([a_id, a_x2, a_y2] {
				{
					auto lock = Overlay::Lock();
					ImGui::GetIO().AddMouseButtonEvent(0, false);
				}
				Reply(a_id, true, std::format("[{:.0f},{:.0f}]", a_x2, a_y2));
				return true;
			});
		}

		// ------------------------------------------------------------ state fields

		using Field = Cheats::Field;

		std::vector<Field> Fields() { return Cheats::Fields(); }

		std::string FieldJson(const Field& a_field)
		{
			return a_field.flag ? (*a_field.flag ? "true" : "false") : Num(*a_field.number);
		}

		// ------------------------------------------------------------ queries

		std::string PlayerJson()
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				return "{\"loaded\":false}";
			}
			auto        avo = player->AsActorValueOwner();
			const auto  pos = player->GetPosition();
			auto        cell = player->GetParentCell();
			auto        location = player->GetCurrentLocation();
			auto        vital = [&](RE::ActorValue a_av) {
                return std::format("{{\"cur\":{},\"max\":{}}}", Num(avo->GetActorValue(a_av)), Num(player->GetActorValueMax(a_av)));
			};
			const int stone = Cheats::CurrentStone();
			return std::format(
				"{{\"loaded\":true,\"name\":{},\"level\":{},\"cell\":{},\"location\":{},\"pos\":[{},{},{}],\"gold\":{},\"perkPoints\":{},"
				"\"godMode\":{},\"stone\":{},\"health\":{},\"magicka\":{},\"stamina\":{},\"paused\":{},\"menuOpen\":{}}}",
				Quote(Text::ToUtf8(player->GetName())), player->GetLevel(), Quote(cell ? Text::ToUtf8(cell->GetFullName()) : ""),
				Quote(location ? Text::ToUtf8(location->GetFullName()) : ""), Num(pos.x), Num(pos.y), Num(pos.z), player->GetGoldAmount(),
				player->GetPlayerRuntimeData().perkCount, Cheats::IsGodMode() ? "true" : "false",
				stone >= 0 ? Quote(Cheats::StandingStones()[stone].name) : "null", vital(RE::ActorValue::kHealth), vital(RE::ActorValue::kMagicka),
				vital(RE::ActorValue::kStamina), RE::UI::GetSingleton()->GameIsPaused() ? "true" : "false", Input::IsMenuOpen() ? "true" : "false");
		}

		std::string ActorValueJson(const std::string& a_name)
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			const auto av = RE::ActorValueList::LookupActorValueByName(a_name.c_str());
			if (!player || av == RE::ActorValue::kNone) {
				throw std::runtime_error("unknown actor value " + a_name);
			}
			auto avo = player->AsActorValueOwner();
			return std::format("{{\"value\":{},\"base\":{},\"permanent\":{},\"max\":{}}}", Num(avo->GetActorValue(av)), Num(avo->GetBaseActorValue(av)),
				Num(avo->GetPermanentActorValue(av)), Num(player->GetActorValueMax(av)));
		}

		std::string GameSettingJson(const std::string& a_name)
		{
			auto setting = RE::GameSettingCollection::GetSingleton()->GetSetting(a_name.c_str());
			if (!setting) {
				throw std::runtime_error("no game setting " + a_name);
			}
			switch (setting->GetType()) {
			case RE::Setting::Type::kFloat: return Num(setting->GetFloat());
			case RE::Setting::Type::kInteger:
			case RE::Setting::Type::kUnsignedInteger: return std::to_string(setting->GetInteger());
			case RE::Setting::Type::kBool: return setting->GetBool() ? "true" : "false";
			default: return Quote(setting->GetString() ? setting->GetString() : "");
			}
		}

		std::string FormJson(RE::FormID a_id)
		{
			auto form = RE::TESForm::LookupByID(a_id);
			if (!form) {
				return "null";
			}
			std::string extra;
			if (auto ingredient = form->As<RE::IngredientItem>()) {
				extra += std::format(",\"knownEffects\":{}", ingredient->gamedata.knownEffectFlags & 0xF);
			}
			if (auto spell = form->As<RE::SpellItem>()) {
				extra += std::format(",\"spellType\":{},\"chargeTime\":{}", static_cast<int>(spell->data.spellType), Num(spell->data.chargeTime));
			}
			if (auto weight = form->As<RE::TESWeightForm>()) {
				extra += std::format(",\"weight\":{}", Num(weight->weight));
			}
			if (auto light = form->As<RE::TESObjectLIGH>()) {
				extra += std::format(",\"time\":{}", light->data.time);
			}
			if (auto recipe = form->As<RE::BGSConstructibleObject>()) {
				extra += std::format(",\"requiredItems\":{},\"hasConditions\":{}", recipe->requiredItems.numContainerObjects,
					recipe->conditions.head ? "true" : "false");
			}
			if (auto gem = form->As<RE::TESSoulGem>()) {
				extra += std::format(",\"soul\":{},\"capacity\":{},\"npcSouls\":{},\"linked\":\"{:08X}\"", gem->currentSoul.underlying(), gem->soulCapacity.underlying(),
					gem->CanHoldNPCSoul() ? "true" : "false", gem->linkedSoulGem ? gem->linkedSoulGem->GetFormID() : 0);
				extra += std::format(",\"model\":{}", Quote(gem->GetModel() ? gem->GetModel() : ""));
			}
			if (auto actor = form->As<RE::Actor>()) {
				extra += std::format(",\"dead\":{},\"essential\":{},\"bleedout\":{},\"teammate\":{},\"health\":{}", actor->IsDead() ? "true" : "false",
					actor->IsEssential() || (actor->GetActorBase() && actor->GetActorBase()->IsEssential()) ? "true" : "false", actor->AsActorState()->IsBleedingOut() ? "true" : "false",
					actor->IsPlayerTeammate() ? "true" : "false", Num(actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth)));
			}
			if (auto keywords = form->As<RE::BGSKeywordForm>()) {
				std::string list;
				keywords->ForEachKeyword([&](RE::BGSKeyword* a_kw) {
					list += std::format("{}{}", list.empty() ? "" : " ", a_kw ? a_kw->GetFormEditorID() : "?");
					return RE::BSContainer::ForEachResult::kContinue;
				});
				extra += std::format(",\"keywords\":{}", Quote(list));
			}
			if (auto enchantable = form->As<RE::TESEnchantableForm>(); enchantable && enchantable->formEnchanting) {
				extra += std::format(",\"enchantment\":\"{:08X}\",\"charge\":{}", enchantable->formEnchanting->GetFormID(), enchantable->amountofEnchantment);
			}
			if (auto enchantment = form->As<RE::EnchantmentItem>()) {
				extra += std::format(",\"baseEnchantment\":\"{:08X}\",\"chargeOverride\":{},\"costOverride\":{},\"flags\":{}",
					enchantment->data.baseEnchantment ? enchantment->data.baseEnchantment->GetFormID() : 0, enchantment->data.chargeOverride, enchantment->data.costOverride,
					enchantment->data.flags.underlying());
			}
			auto player = RE::PlayerCharacter::GetSingleton();
			if (auto spell = form->As<RE::SpellItem>(); spell && player) {
				extra += std::format(",\"playerHas\":{}", player->HasSpell(spell) ? "true" : "false");
			}
			if (auto perk = form->As<RE::BGSPerk>(); perk && player) {
				extra += std::format(",\"playerHas\":{}", player->HasPerk(perk) ? "true" : "false");
			}
			if (auto shout = form->As<RE::TESShout>(); shout && player) {
				extra += std::format(",\"playerHas\":{}", player->HasShout(shout) ? "true" : "false");
			}
			if (auto object = form->As<RE::TESBoundObject>(); object && player) {
				const auto counts = player->GetInventoryCounts([object](RE::TESBoundObject& a_item) { return &a_item == object; });
				const auto it = counts.find(object);
				extra += std::format(",\"playerCount\":{}", it != counts.end() ? it->second : 0);
			}
			return std::format("{{\"formID\":\"{:08X}\",\"type\":{},\"name\":{},\"known\":{}{}}}", form->GetFormID(), static_cast<int>(form->GetFormType()),
				Quote(Text::ToUtf8(form->GetName())), form->GetKnown() ? "true" : "false", extra);
		}

		// Enchanting / alchemy knowledge in numbers, to see whether "learn all" did anything.
		std::string KnowledgeJson()
		{
			auto dh = RE::TESDataHandler::GetSingleton();
			int  enchKnown = 0, enchTotal = 0, effKnown = 0, effTotal = 0, ingFull = 0, ingTotal = 0;
			for (auto enchantment : dh->GetFormArray<RE::EnchantmentItem>()) {
				if (!enchantment) {
					continue;
				}
				++enchTotal;
				enchKnown += enchantment->GetKnown();
				for (auto effect : enchantment->effects) {
					if (effect && effect->baseEffect) {
						++effTotal;
						effKnown += effect->baseEffect->GetKnown();
					}
				}
			}
			for (auto ingredient : dh->GetFormArray<RE::IngredientItem>()) {
				if (ingredient) {
					++ingTotal;
					ingFull += (ingredient->gamedata.knownEffectFlags & 0xF) == 0xF;
				}
			}
			return std::format("{{\"enchantments\":[{},{}],\"enchantmentEffects\":[{},{}],\"ingredientsFullyKnown\":[{},{}]}}", enchKnown, enchTotal,
				effKnown, effTotal, ingFull, ingTotal);
		}

		std::string QuestsJson()
		{
			std::string out = "[";
			for (const auto& q : Cheats::RunningQuests()) {
				out += std::format("{}{{\"formID\":\"{:08X}\",\"name\":{},\"type\":{},\"stage\":{}}}", out.size() > 1 ? "," : "", q.formID, Quote(q.name),
					Quote(q.type), q.stage);
			}
			return out + "]";
		}

		std::string PointsJson()
		{
			std::string out = "[";
			for (const auto& p : Cheats::SavedPoints()) {
				out += std::format("{}{{\"marker\":\"{:08X}\",\"name\":{},\"place\":{}}}", out.size() > 1 ? "," : "", p.marker, Quote(p.name), Quote(p.place));
			}
			return out + "]";
		}

		// ------------------------------------------------------------ commands

		const char* kHelp =
			"ping | state | get <field> | set <field> <value> | apply | player | av <name> | gs <setting> | form <id> | inv <ref> | effects [ref] | spellcost <spell> | refs [meters] [filter]"
			" | knowledge | quests | points | esp | log [lines] | console [@ref] <command> (replies with console output) | action <hotkey id>"
			" | call <function> [args] | menu open|close | page <name> | search <text> | scroll <y|-1> | ui [label] | click <label>|<x y>"
			" | rclick ... | drag <x1 y1 x2 y2> | key <name|code> [down|up] | type <text> | wheel <n> | layout [next] | menus | uiclose <menu>"
			" | screenshot <file.bmp> | save <name> | load <name> | continue";

		// Returns the JSON data of the reply; throws on bad input.
		std::string Run(const std::string& a_id, const std::string& a_line, bool& a_deferred)
		{
			const auto args = Split(a_line);
			if (args.size() < 2) {
				throw std::runtime_error("expected '<id> <verb> ...'");
			}
			const auto& verb = args[1];
			auto        arg = [&](std::size_t i) -> const std::string& {
                if (args.size() <= i + 1) {
                    throw std::runtime_error("missing argument for " + verb);
                }
                return args[i + 1];
			};

			if (verb == "help") {
				return Quote(kHelp);
			}
			if (verb == "ping") {
				auto player = RE::PlayerCharacter::GetSingleton();
				return std::format("{{\"version\":{},\"inGame\":{},\"forms\":{}}}", Quote(SKSE::GetPluginVersion().string()),
					player && player->Is3DLoaded() ? "true" : "false", Catalog::Forms().size());
			}
			if (verb == "state") {
				std::string out = "{";
				for (const auto& f : Fields()) {
					out += std::format("{}\"{}\":{}", out.size() > 1 ? "," : "", f.name, FieldJson(f));
				}
				return out + "}";
			}
			if (verb == "get" || verb == "set") {
				const auto& name = arg(1);
				for (const auto& f : Fields()) {
					if (name != f.name) {
						continue;
					}
					if (verb == "set") {
						const auto& value = arg(2);
						if (f.flag) {
							*f.flag = value == "1" || value == "true" || value == "on";
						} else {
							*f.number = std::stof(value);
						}
						Cheats::ApplyPersistent();
					}
					return FieldJson(f);
				}
				throw std::runtime_error("no state field " + name);
			}
			if (verb == "apply") {
				Cheats::ApplyPersistent();
				return "null";
			}
			if (verb == "player") {
				return PlayerJson();
			}
			if (verb == "av") {
				return ActorValueJson(arg(1));
			}
			if (verb == "gs") {
				return GameSettingJson(arg(1));
			}
			if (verb == "lockpick") {
				// The open lockpicking menu as CommonLib reads it, plus raw floats of its runtime data (offset:value).
				// "lockpick set <angle>" puts the pick at that angle.
				auto ui = RE::UI::GetSingleton();
				auto menu = ui && ui->IsMenuOpen(RE::LockpickingMenu::MENU_NAME) ? ui->GetMenu<RE::LockpickingMenu>().get() : nullptr;
				if (!menu) {
					throw std::runtime_error("lockpicking menu is not open");
				}
				auto& data = menu->GetRuntimeData();
				if (args.size() > 3 && arg(1) == "set") {
					data.pickAngle = std::stof(arg(2));
				}
				std::string raw;
				const auto  base = reinterpret_cast<const std::uint8_t*>(&data);
				for (std::size_t off = 0x80; off < 0xC0; off += 4) {
					raw += std::format("{}\"{:X}\":{}", raw.empty() ? "" : ",", off, Num(*reinterpret_cast<const float*>(base + off)));
				}
				return std::format("{{\"pick\":{},\"lock\":{},\"damage\":{},\"sweet\":{},\"partial\":{},\"broken\":{},\"dataOffset\":\"{:X}\",\"raw\":{{{}}}}}",
					Num(data.pickAngle), Num(data.lockAngle), Num(data.damagePickAngle), Num(data.sweetSpotAngle), Num(data.partialPickAngle), data.numBrokenPicks,
					reinterpret_cast<std::uintptr_t>(&data) - reinterpret_cast<std::uintptr_t>(menu), raw);
			}
			if (verb == "skillxp") {
				// A skill's experience as the game keeps it (PlayerSkills), plus the character's level experience.
				auto player = RE::PlayerCharacter::GetSingleton();
				const auto av = RE::ActorValueList::LookupActorValueByName(arg(1).c_str());
				auto skills = player ? player->GetInfoRuntimeData().skills : nullptr;
				const auto i = std::to_underlying(av) - std::to_underlying(RE::ActorValue::kOneHanded);
				if (!skills || !skills->data || i < 0 || i >= 18) {
					throw std::runtime_error("not a skill: " + arg(1));
				}
				const auto& skill = skills->data->skills[i];
				return std::format("{{\"base\":{},\"xp\":{},\"threshold\":{},\"levelXp\":{},\"levelThreshold\":{}}}",
					Num(player->AsActorValueOwner()->GetBaseActorValue(av)), Num(skill.xp), Num(skill.levelThreshold), Num(skills->data->xp), Num(skills->data->levelThreshold));
			}
			if (verb == "gsaddr") {
				// Where a game setting object lives (RVA in SkyrimSE.exe), for reverse engineering.
				auto setting = RE::GameSettingCollection::GetSingleton()->GetSetting(arg(1).c_str());
				if (!setting) {
					throw std::runtime_error("no setting " + arg(1));
				}
				const auto addr = reinterpret_cast<std::uintptr_t>(setting);
				const auto base = REL::Module::get().base();
				return std::format("{{\"rva\":\"{:X}\",\"nameRva\":\"{:X}\"}}", addr - base, reinterpret_cast<std::uintptr_t>(setting->GetName()) - base);
			}
			if (verb == "form") {
				return FormJson(ParseForm(arg(1)));
			}
			if (verb == "shouts") {
				// Every shout: name, named words, whether it has a description (the magic menu shows it), player has it.
				auto        player = RE::PlayerCharacter::GetSingleton();
				std::string out = "[";
				for (auto shout : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESShout>()) {
					if (!shout) {
						continue;
					}
					int words = 0;
					for (const auto& variation : shout->variations) {
						words += variation.word && variation.word->GetName() && *variation.word->GetName();
					}
					RE::BSString description;
					shout->GetDescription(description, shout);
					out += std::format("{}{{\"formID\":\"{:08X}\",\"name\":{},\"words\":{},\"description\":{},\"menuObject\":{},\"playerHas\":{}}}", out.size() > 1 ? "," : "",
						shout->GetFormID(), Quote(Text::ToUtf8(shout->GetName())), words, !description.empty() ? "true" : "false",
						shout->menuDispObject ? std::format("\"{:08X}\"", shout->menuDispObject->GetFormID()) : std::string{ "null" }, player && player->HasShout(shout) ? "true" : "false");
				}
				return out + "]";
			}
			if (verb == "mapmarker") {
				// The custom marker the player put on the map.
				auto player = RE::PlayerCharacter::GetSingleton();
				auto marker = player ? player->GetInfoRuntimeData().playerMapMarker.get() : nullptr;
				if (!marker) {
					return "null";
				}
				const auto pos = marker->GetPosition();
				auto       world = marker->GetWorldspace();
				auto       cell = marker->GetParentCell();
				return std::format("{{\"ref\":\"{:08X}\",\"base\":\"{:08X}\",\"pos\":[{},{},{}],\"disabled\":{},\"world\":{},\"cell\":\"{:08X}\"}}",
					marker->GetFormID(), marker->GetBaseObject() ? marker->GetBaseObject()->GetFormID() : 0, Num(pos.x), Num(pos.y), Num(pos.z),
					marker->IsDisabled() ? "true" : "false", Quote(world ? Text::ToUtf8(world->GetFullName()) : ""), cell ? cell->GetFormID() : 0);
			}
			if (verb == "dumpids") {
				// "dumpids": every Address Library (id, RVA) of this game version into CheatMenuBridge\ids.txt, to name
				// code found in a memory dump. Offset2ID walks the table; looking ids up one by one would end the game
				// with a message box for any id this version doesn't have.
				const REL::Offset2ID table;
				std::ofstream        file{ *Dir() / "ids.txt", std::ios::trunc };
				std::size_t          count = 0;
				for (const auto& mapping : table) {
					file << mapping.id << ' ' << std::hex << mapping.offset << std::dec << '\n';
					++count;
				}
				return std::to_string(count);
			}
			if (verb == "callers") {
				// "callers <rva>": the first bytes of that code and every direct call/jump to it in the loaded game
				// (the exe on disk is encrypted by the Steam stub, so this is the only place to scan).
				const auto target = std::stoull(arg(1), nullptr, 16);
				const auto text = REL::Module::get().segment(REL::Segment::textx);
				const auto base = REL::Module::get().base();
				const auto code = reinterpret_cast<const std::uint8_t*>(text.address());
				std::string bytes, out;
				for (int i = 0; i < 48; ++i) {
					bytes += std::format("{:02X}", reinterpret_cast<const std::uint8_t*>(base + target)[i]);
				}
				for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
					if ((code[i] == 0xE8 || code[i] == 0xE9) &&
						text.offset() + i + 5 + *reinterpret_cast<const std::int32_t*>(code + i + 1) == target) {
						out += std::format("{}\"{}{:X}\"", out.empty() ? "" : ",", code[i] == 0xE8 ? "" : "jmp ", text.offset() + i);
					}
				}
				return std::format("{{\"bytes\":\"{}\",\"callers\":[{}]}}", bytes, out);
			}
			if (verb == "projectiles") {
				// Projectiles near the player: speed (units/s) and the shot's power (0..1).
				std::string out = "[";
				auto        player = RE::PlayerCharacter::GetSingleton();
				RE::TES::GetSingleton()->ForEachReferenceInRange(player, 20000.0f, [&](RE::TESObjectREFR* a_ref) {
					if (auto projectile = a_ref ? a_ref->As<RE::Projectile>() : nullptr) {
						const auto& data = projectile->GetProjectileRuntimeData();
						if (args.size() > 2 && arg(1) == "new" && data.livingTime > 2.0f) {
							return RE::BSContainer::ForEachResult::kContinue;  // "projectiles new": fired in the last 2 s
						}
						out += std::format("{}{{\"ref\":\"{:08X}\",\"speed\":{:.0f},\"power\":{:.2f},\"age\":{:.2f},\"z\":{:.0f},\"vz\":{:.0f},\"m\":{:.1f}}}", out.size() > 1 ? "," : "", a_ref->GetFormID(),
							data.linearVelocity.Length(), data.power, data.livingTime, a_ref->GetPosition().z, data.linearVelocity.z, a_ref->GetPosition().GetDistance(player->GetPosition()) / 70.0f);
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
				return out + "]";
			}
			if (verb == "consolelog") {
				auto log = RE::ConsoleLog::GetSingleton();
				const std::string all = log ? log->buffer.c_str() : "";
				return std::format("{{\"size\":{},\"last\":{},\"tail\":{}}}", all.size(), Quote(log ? log->lastMessage : ""),
					Quote(all.substr(all.size() > 120 ? all.size() - 120 : 0)));
			}
			if (verb == "controls") {
				// The game's enabled control groups (UEFlag bits; 0x7FF... = all): "stuck" shows as 0.
				std::uint32_t enabled = 0, stored = 0;
				RE::ControlMap::GetSingleton()->GetControlsState(enabled, stored);
				return std::format("{{\"enabled\":{},\"stored\":{}}}", enabled, stored);
			}
			if (verb == "quest" || verb == "globals") {
				// "quest <editorID part>": quests with that in their editor ID; "globals <part>": global variables.
				const auto  needle = Text::ToLower(Rest(a_line, 2));
				std::string out = "[";
				auto        dh = RE::TESDataHandler::GetSingleton();
				if (verb == "quest") {
					for (auto quest : dh->GetFormArray<RE::TESQuest>()) {
						const char* edid = quest ? quest->GetFormEditorID() : nullptr;
						if (!edid || !Text::Contains(Text::ToLower(edid), needle)) {
							continue;
						}
						out += std::format("{}{{\"formID\":\"{:08X}\",\"edid\":{},\"name\":{},\"running\":{},\"completed\":{},\"stage\":{}}}", out.size() > 1 ? "," : "",
							quest->GetFormID(), Quote(edid), Quote(Text::ToUtf8(quest->GetName())), quest->IsRunning() ? "true" : "false",
							quest->IsCompleted() ? "true" : "false", quest->GetCurrentStageID());
					}
				} else {
					for (auto global : dh->GetFormArray<RE::TESGlobal>()) {
						const char* edid = global ? global->GetFormEditorID() : nullptr;
						if (!edid || !Text::Contains(Text::ToLower(edid), needle)) {
							continue;
						}
						out += std::format("{}{{\"formID\":\"{:08X}\",\"edid\":{},\"value\":{}}}", out.size() > 1 ? "," : "", global->GetFormID(), Quote(edid), Num(global->value));
					}
				}
				return out + "]";
			}
			if (verb == "enchopts") {
				// "enchopts <name part>": the Зачарование page's enchantments to choose from (kind 0 weapon, 1 staff, 2 armour).
				const auto  needle = Text::ToLower(Rest(a_line, 2));
				std::string out = "[";
				for (const auto& o : Enchanter::Options()) {
					if (!Text::Contains(o.nameLower, needle)) {
						continue;
					}
					std::string effects;
					for (const auto& e : o.effects) {
						effects += std::format("{}[{},{},{}]", effects.empty() ? "" : ",", Quote(e.name), Num(e.magnitude), Num(e.duration));
					}
					out += std::format("{}{{\"formID\":\"{:08X}\",\"name\":{},\"kind\":{},\"cat\":{},\"plugin\":{},\"effects\":[{}]}}", out.size() > 1 ? "," : "", o.enchantment,
						Quote(o.name), static_cast<int>(o.kind), o.categories, Quote(o.plugin), effects);
				}
				return out + "]";
			}
			if (verb == "catalog") {
				// "catalog <spell|shout|perk>": the menu's catalog entries of that kind with editor ID and plugin; spells also with
				// type, casting, delivery, whether a tome teaches them, and their effects (name, editor ID, archetype, hidden in UI).
				const auto kind = arg(1);
				const auto cat = kind == "spell" ? Catalog::Category::kSpell : kind == "shout" ? Catalog::Category::kShout : Catalog::Category::kPerk;
				std::unordered_set<RE::SpellItem*> tomes;
				for (auto book : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESObjectBOOK>()) {
					if (book && book->TeachesSpell() && book->GetSpell()) {
						tomes.insert(book->GetSpell());
					}
				}
				auto edidOf = [](RE::TESForm* a_form) { return Quote(a_form && a_form->GetFormEditorID() ? a_form->GetFormEditorID() : ""); };
				std::string out = "[";
				for (const auto& e : Catalog::Forms()) {
					if (e.category != cat) {
						continue;
					}
					auto        form = RE::TESForm::LookupByID(e.formID);
					std::string extra;
					if (auto spell = form ? form->As<RE::SpellItem>() : nullptr) {
						std::string effects;
						for (auto effect : spell->effects) {
							auto base = effect ? effect->baseEffect : nullptr;
							if (!base) {
								continue;
							}
							effects += std::format("{}[{},{},{},{},{}]", effects.empty() ? "" : ",", Quote(Text::ToUtf8(base->GetName())), edidOf(base),
								std::to_underlying(base->GetArchetype()), base->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHideInUI) ? 1 : 0,
								Num(effect->effectItem.magnitude));
						}
						extra = std::format(",\"type\":{},\"cast\":{},\"delivery\":{},\"tome\":{},\"effects\":[{}]", std::to_underlying(spell->GetSpellType()),
							std::to_underlying(spell->GetCastingType()), std::to_underlying(spell->GetDelivery()), tomes.contains(spell) ? "true" : "false", effects);
					}
					out += std::format("{}{{\"formID\":\"{:08X}\",\"name\":{},\"edid\":{},\"plugin\":{}{}}}", out.size() > 1 ? "," : "", e.formID, Quote(e.name), edidOf(form),
						Quote(e.plugin), extra);
				}
				return out + "]";
			}
			if (verb == "idles") {
				// "idles <editorID part>": idle animations with their conditions (function id, "or", op, value or global, parameters).
				const auto  needle = Text::ToLower(Rest(a_line, 2));
				std::string out = "[";
				const auto [forms, lock] = RE::TESForm::GetAllForms();
				RE::BSReadLockGuard guard(lock);
				for (const auto& [id, form] : *forms) {
					auto        idle = form ? form->As<RE::TESIdleForm>() : nullptr;
					const char* edid = idle ? idle->GetFormEditorID() : nullptr;
					if (!edid || !Text::Contains(Text::ToLower(edid), needle)) {
						continue;
					}
					std::string conds;
					for (auto item = idle->conditions.head; item; item = item->next) {
						const auto& d = item->data;
						// raw values: a parameter is a form pointer, an actor value index or other data depending on the function
						auto param = [&](void* a_param) {
							using F = RE::FUNCTION_DATA::FunctionID;
							const auto fn = d.functionData.function.get();
							if (fn == F::kGetGlobalValue && a_param) {
								return Quote(static_cast<RE::TESGlobal*>(a_param)->GetFormEditorID());
							}
							if ((fn == F::kGetIsID || fn == F::kGetIsRace || fn == F::kHasKeyword || fn == F::kHasPerk) && a_param) {
								auto form = static_cast<RE::TESForm*>(a_param);
								return std::format("\"{:08X} {}\"", form->GetFormID(), form->GetFormEditorID());
							}
							return std::format("\"{:X}\"", reinterpret_cast<std::uintptr_t>(a_param));
						};
						conds += std::format("{}[{},{},{},{},{},{},\"on{}{}\"]", conds.empty() ? "" : ",", std::to_underlying(d.functionData.function.get()), d.flags.isOR ? 1 : 0,
							static_cast<int>(d.flags.opCode), d.flags.global && d.comparisonValue.g ? Quote(d.comparisonValue.g->GetFormEditorID()) : Num(d.comparisonValue.f),
							param(d.functionData.params[0]), param(d.functionData.params[1]), d.object.underlying(), d.flags.swapTarget ? " swap" : "");
					}
					out += std::format("{}{{\"formID\":\"{:08X}\",\"edid\":{},\"parent\":{},\"event\":{},\"conditions\":[{}]}}", out.size() > 1 ? "," : "", idle->GetFormID(), Quote(edid),
						Quote(idle->parentIdle && idle->parentIdle->GetFormEditorID() ? idle->parentIdle->GetFormEditorID() : ""), Quote(idle->animEventName.c_str()), conds);
				}
				return out + "]";
			}
			if (verb == "benches") {
				// Every crafting furniture: bench type, skill and keywords (to tell forges, cookpots, tanning racks apart).
				std::string out = "[";
				for (auto furn : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFurniture>()) {
					if (!furn || furn->workBenchData.benchType == RE::TESFurniture::WorkBenchData::BenchType::kNone) {
						continue;
					}
					std::string keywords;
					furn->ForEachKeyword([&](RE::BGSKeyword* a_kw) {
						keywords += std::format("{}{}", keywords.empty() ? "" : " ", a_kw ? a_kw->GetFormEditorID() : "?");
						return RE::BSContainer::ForEachResult::kContinue;
					});
					out += std::format("{}{{\"formID\":\"{:08X}\",\"name\":{},\"type\":{},\"skill\":{},\"keywords\":{}}}", out.size() > 1 ? "," : "", furn->GetFormID(),
						Quote(Text::ToUtf8(furn->GetName())), furn->workBenchData.benchType.underlying(), furn->workBenchData.usesSkill.underlying(), Quote(keywords));
				}
				return out + "]";
			}
			if (verb == "enchmenu") {
				// The open enchanting table: effects allowed per item and every list entry (filter bits, name).
				auto menu = RE::UI::GetSingleton()->GetMenu<RE::CraftingMenu>();
				auto ench = menu ? skyrim_cast<RE::CraftingSubMenus::EnchantConstructMenu*>(menu->GetCraftingSubMenu()) : nullptr;
				if (!ench) {
					return "null";
				}
				std::string out = std::format("{{\"allowed\":{},\"entries\":[", ench->selected.numEnchantmentsAllowed);
				bool first = true;
				for (auto& entry : ench->listEntries) {
					if (!entry) {
						continue;
					}
					out += std::format("{}[{},{}]", first ? "" : ",", entry->filterFlag.underlying(), Quote(Text::ToUtf8(entry->GetName() ? entry->GetName() : "")));
					first = false;
				}
				return out + "]}";
			}
			if (verb == "knowledge") {
				return KnowledgeJson();
			}
			if (verb == "vendors") {
				// Vendor factions (with a merchant chest): editor ID, chest, buy/sell list, "everything except", a member NPC.
				std::unordered_map<RE::TESFaction*, RE::TESNPC*> member;
				for (auto npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
					if (npc && npc->GetName() && *npc->GetName()) {
						for (const auto& rank : npc->factions) {
							if (rank.faction && !member.contains(rank.faction)) {
								member[rank.faction] = npc;
							}
						}
					}
				}
				std::string out = "[";
				for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
					if (!faction || !faction->vendorData.merchantContainer) {
						continue;
					}
					auto npc = member.contains(faction) ? member[faction] : nullptr;
					const auto& v = faction->vendorData;
					out += std::format("{}{{\"faction\":\"{:08X}\",\"edid\":{},\"chest\":\"{:08X}\",\"list\":\"{:08X}\",\"notBuySell\":{},\"npc\":\"{:08X}\",\"name\":{},\"hours\":\"{}-{}\"}}",
						out.size() > 1 ? "," : "", faction->GetFormID(), Quote(faction->GetFormEditorID()), v.merchantContainer->GetFormID(),
						v.vendorSellBuyList ? v.vendorSellBuyList->GetFormID() : 0, v.vendorValues.notBuySell ? "true" : "false", npc ? npc->GetFormID() : 0,
						Quote(npc ? Text::ToUtf8(npc->GetName()) : ""), v.vendorValues.startHour, v.vendorValues.endHour);
				}
				return out + "]";
			}
			if (verb == "quests") {
				return QuestsJson();
			}
			if (verb == "points") {
				return PointsJson();
			}
			if (verb == "console") {
				// "console [@<ref>] <command>": runs on the reference if given, replies with what was printed.
				auto                command = Rest(a_line, 2);
				RE::ObjectRefHandle target;
				if (command.starts_with('@')) {
					const auto space = command.find(' ');
					auto       ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(command.substr(1, space - 1)));
					if (!ref) {
						throw std::runtime_error("no reference " + command.substr(1, space - 1));
					}
					target = ref->GetHandle();
					command = space == std::string::npos ? std::string{} : command.substr(space + 1);
				}
				Console::RunCaptured(command, target, [a_id](std::string a_output) { Reply(a_id, true, Quote(a_output)); });
				a_deferred = true;
				return {};
			}
			if (verb == "refs") {
				return RefsJson(args.size() > 2 ? std::stof(args[2]) : 20.0f, args.size() > 3 ? Rest(a_line, 3) : std::string{});
			}
			if (verb == "log") {
				return LogTail(args.size() > 2 ? std::stoul(args[2]) : 20);
			}
			if (verb == "key") {
				// "key <name> [down|up]": a full press by default (down now, up next frame).
				const auto dik = ParseKey(arg(1));
				const auto mode = args.size() > 3 ? args[3] : std::string{ "press" };
				if (mode != "up") {
					Input::SimulateKey(dik, true);
				}
				if (mode == "press") {
					g_steps.push_back([dik] {
						Input::SimulateKey(dik, false);
						return true;
					});
				} else if (mode == "up") {
					Input::SimulateKey(dik, false);
				}
				return std::format("{}", dik);
			}
			if (verb == "type") {
				const auto text = Rest(a_line, 2);
				auto       lock = Overlay::Lock();
				ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
				return "null";
			}
			if (verb == "wheel") {
				auto lock = Overlay::Lock();
				ImGui::GetIO().AddMouseWheelEvent(0.0f, std::stof(arg(1)));
				return "null";
			}
			if (verb == "ui") {
				if (args.size() > 2) {
					const auto rect = Menu::FindControl(Rest(a_line, 2));
					if (!rect) {
						throw std::runtime_error("not on screen: " + Rest(a_line, 2));
					}
					return std::format("[{:.0f},{:.0f},{:.0f},{:.0f}]", (*rect)[0], (*rect)[1], (*rect)[2], (*rect)[3]);
				}
				std::string out = "[";
				for (const auto& label : Menu::VisibleControls()) {
					out += (out.size() > 1 ? "," : "") + Quote(label);
				}
				return out + "]";
			}
			if (verb == "click" || verb == "rclick") {
				// "click <control label>" or "click <x> <y>" in screen pixels.
				float x = 0, y = 0;
				if (args.size() == 4 && std::isdigit(static_cast<unsigned char>(args[2][0])) && std::isdigit(static_cast<unsigned char>(args[3][0]))) {
					x = std::stof(args[2]);
					y = std::stof(args[3]);
				} else {
					const auto rect = Menu::FindControl(Rest(a_line, 2));
					if (!rect) {
						throw std::runtime_error("not on screen: " + Rest(a_line, 2));
					}
					x = ((*rect)[0] + (*rect)[2]) * 0.5f;
					y = ((*rect)[1] + (*rect)[3]) * 0.5f;
				}
				QueueClick(a_id, x, y, verb == "click" ? 0 : 1);
				a_deferred = true;
				return {};
			}
			if (verb == "drag" && args.size() == 6) {
				// "drag <x1> <y1> <x2> <y2>" in screen pixels, left button
				QueueDrag(a_id, std::stof(args[2]), std::stof(args[3]), std::stof(args[4]), std::stof(args[5]));
				a_deferred = true;
				return {};
			}
			if (verb == "action") {
				return Quote(Hotkeys::Trigger(arg(1)));
			}
			if (verb == "layout") {
				if (args.size() > 2 && args[2] == "next") {
					Overlay::RequestNextKeyboardLayout();
				}
				return Quote(Overlay::KeyboardLanguage());
			}
			if (verb == "inv") {
				// Raw inventory of a reference, as the plugin sees it.
				auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(1)));
				if (!ref) {
					throw std::runtime_error("no reference " + arg(1));
				}
				std::string out = "[";
				for (auto& [object, data] : ref->GetInventory()) {
					// Soul levels of filled gems, one per stack ("souls":[5] = one grand soul).
					std::string souls;
					float       health = 1.0f;  // the best tempering among the stacks (1.6 = Legendary)
					std::string charges;        // weapons: the item's own maximum charge, "left N" = charge it has now
					// effect magnitudes of the enchantment in effect: an item's own (ExtraEnchantment), else the base form's
					auto enchantable = object->As<RE::TESEnchantableForm>();
					auto enchantment = enchantable ? enchantable->formEnchanting : nullptr;
					if (data.second && data.second->extraLists) {
						for (auto list : *data.second->extraLists) {
							if (auto soul = list && object->Is(RE::FormType::SoulGem) ? list->GetByType<RE::ExtraSoul>() : nullptr) {
								souls += std::format("{}{}", souls.empty() ? "" : ",", soul->soul.underlying());
							}
							if (auto xHealth = list ? list->GetByType<RE::ExtraHealth>() : nullptr) {
								health = std::max(health, xHealth->health);
							}
							if (auto xEnchantment = list ? list->GetByType<RE::ExtraEnchantment>() : nullptr; xEnchantment && xEnchantment->enchantment) {
								enchantment = xEnchantment->enchantment;
								charges += std::format("{}{}", charges.empty() ? "" : ",", xEnchantment->charge);
							}
							if (auto xCharge = list ? list->GetByType<RE::ExtraCharge>() : nullptr) {
								charges += std::format("{}left {:.0f}", charges.empty() ? "" : ",", xCharge->charge);
							}
						}
					}
					std::string magnitudes;
					if (enchantment) {
						for (auto effect : enchantment->effects) {
							magnitudes += std::format("{}{}", magnitudes.empty() ? "" : ",", effect ? Num(effect->effectItem.magnitude) : "0");
						}
					}
					// the enchantment the game itself reports for the stack (what its UI and hits use)
					auto gameEnchantment = data.second ? data.second->GetEnchantment() : nullptr;
					out += std::format("{}{{\"name\":{},\"formID\":\"{:08X}\",\"type\":{},\"count\":{},\"playable\":{},\"souls\":[{}],\"health\":{},\"ench\":[{}],\"game\":\"{:08X}\",\"charges\":\"{}\"}}",
						out.size() > 1 ? "," : "", Quote(Text::ToUtf8(object->GetName())), object->GetFormID(), static_cast<int>(object->GetFormType()), data.first,
						object->GetPlayable() ? "true" : "false", souls, Num(health), magnitudes, gameEnchantment ? gameEnchantment->GetFormID() : 0, charges);
				}
				return out + "]";
			}
			if (verb == "effects") {
				// Active magic effects on a reference (the player by default): magnitude, duration, elapsed.
				RE::Actor* actor = RE::PlayerCharacter::GetSingleton();
				if (args.size() > 2) {
					actor = RE::TESForm::LookupByID<RE::Actor>(ParseForm(arg(1)));
				}
				auto list = actor ? actor->AsMagicTarget()->GetActiveEffectList() : nullptr;
				std::string out = "[";
				if (list) {
					for (auto effect : *list) {
						if (!effect || !effect->effect || !effect->effect->baseEffect) {
							continue;
						}
						out += std::format("{}{{\"effect\":{},\"spell\":\"{:08X}\",\"magnitude\":{:.2f},\"duration\":{:.1f},\"elapsed\":{:.1f}}}",
							out.size() > 1 ? "," : "", Quote(Text::ToUtf8(effect->effect->baseEffect->GetName())), effect->spell ? effect->spell->GetFormID() : 0,
							effect->magnitude, effect->duration, effect->elapsedSeconds);
					}
				}
				return out + "]";
			}
			if (verb == "spellcost") {
				auto spell = RE::TESForm::LookupByID<RE::MagicItem>(ParseForm(arg(1)));
				if (!spell) {
					throw std::runtime_error("no spell " + arg(1));
				}
				return std::format("{:.2f}", spell->CalculateMagickaCost(RE::PlayerCharacter::GetSingleton()));
			}
			if (verb == "esp") {
				std::string out = "[";
				for (const auto& mark : Esp::DebugMarks()) {
					out += (out.size() > 1 ? "," : "") + Quote(mark);
				}
				return out + "]";
			}
			if (verb == "uiclose") {
				// Closes a game menu by name, e.g. "uiclose ContainerMenu".
				RE::UIMessageQueue::GetSingleton()->AddMessage(Rest(a_line, 2), RE::UI_MESSAGE_TYPE::kHide, nullptr);
				return "null";
			}
			if (verb == "gfx") {
				// "gfx <menu> get <path> | set <path> <value> | call <path> [args]": a game menu's Flash movie, on the UI thread.
				const auto menuName = arg(1);
				const auto op = arg(2);
				const auto path = arg(3);
				std::vector<std::string> rest(args.size() > 5 ? args.begin() + 5 : args.end(), args.end());
				SKSE::GetTaskInterface()->AddUITask([a_id, menuName, op, path, rest]() {
					auto menu = RE::UI::GetSingleton()->GetMenu(menuName);
					auto movie = menu ? menu->uiMovie : nullptr;
					if (!movie) {
						Reply(a_id, false, Quote("menu not open"));
						return;
					}
					auto value = [](const std::string& a_text) {
						if (a_text == "true" || a_text == "false") {
							return RE::GFxValue(a_text == "true");
						}
						char*        end = nullptr;
						const double d = std::strtod(a_text.c_str(), &end);
						return end && *end == 0 && !a_text.empty() ? RE::GFxValue(d) : RE::GFxValue(a_text.c_str());
					};
					auto json = [](const RE::GFxValue& a_v) -> std::string {
						if (a_v.IsNumber()) return Num(static_cast<float>(a_v.GetNumber()));
						if (a_v.IsBool()) return a_v.GetBool() ? "true" : "false";
						if (a_v.IsString()) return Quote(a_v.GetString());
						if (a_v.IsUndefined()) return Quote("undefined");
						if (a_v.IsNull()) return "null";
						return Quote("object");
					};
					RE::GFxValue result;
					bool         ok = true;
					if (op == "get") {
						ok = movie->GetVariable(&result, path.c_str());
					} else if (op == "set") {
						ok = movie->SetVariable(path.c_str(), value(rest.empty() ? "" : rest[0]));
					} else {
						std::vector<RE::GFxValue> values;
						for (const auto& a : rest) {
							values.push_back(value(a));
						}
						ok = movie->Invoke(path.c_str(), &result, values.data(), static_cast<std::uint32_t>(values.size()));
					}
					Reply(a_id, ok, ok ? json(result) : Quote(op + " failed: " + path));
				});
				a_deferred = true;
				return {};
			}
			if (verb == "menus") {
				std::string out = "[";
				for (const char* name : { "ContainerMenu", "InventoryMenu", "MapMenu", "Loading Menu", "Lockpicking Menu", "BarterMenu", "Console",
						 "Crafting Menu", "Dialogue Menu", "MagicMenu", "Journal Menu", "StatsMenu", "Sleep/Wait Menu", "MessageBoxMenu", "RaceSex Menu",
						 "Book Menu", "GiftMenu", "Training Menu" }) {
					if (RE::UI::GetSingleton()->IsMenuOpen(name)) {
						out += std::format("{}{}", out.size() > 1 ? "," : "", Quote(name));
					}
				}
				return out + "]";
			}
			if (verb == "menu") {
				Input::SetMenuOpen(arg(1) == "open");
				return "null";
			}
			if (verb == "page") {
				if (!Menu::OpenPage(arg(1))) {
					throw std::runtime_error("no page " + arg(1));
				}
				return "null";
			}
			if (verb == "scroll") {
				Menu::ScrollTo(std::stof(arg(1)));
				return "null";
			}
			if (verb == "search") {
				Menu::SetSearch(Rest(a_line, 2));
				return "null";
			}
			if (verb == "screenshot") {
				const auto dir = Dir();
				g_screenshot = Screenshot{ a_id, *dir / arg(1), 2 };
				a_deferred = true;
				return {};
			}
			if (verb == "save" || verb == "load") {
				const auto name = arg(1);
				SKSE::GetTaskInterface()->AddTask([save = verb == "save", name]() {
					auto manager = RE::BGSSaveLoadManager::GetSingleton();
					if (save) {
						manager->Save(name.c_str());
					} else {
						manager->Load(name.c_str(), false);
					}
				});
				return "null";
			}
			if (verb == "continue") {
				// "Continue" from the main menu: the save list has to be built first there.
				SKSE::GetTaskInterface()->AddTask([a_id]() {
					auto       manager = RE::BGSSaveLoadManager::GetSingleton();
					const bool listed = manager->PopulateSaveList();
					const bool loading = manager->LoadMostRecentSaveGame();
					logger::info("Bridge continue: save list {}, load {}", listed, loading);
					Reply(a_id, loading, loading ? "true" : Quote("LoadMostRecentSaveGame failed"));
				});
				a_deferred = true;
				return {};
			}
			if (verb == "call") {
				const auto& fn = arg(1);
				auto        f = [&](std::size_t i, float a_default) { return args.size() > i + 1 ? std::stof(args[i + 1]) : a_default; };
				if (fn == "heal") Cheats::HealAll();
				else if (fn == "learnEnchantments") Cheats::LearnAllEnchantments();
				else if (fn == "learnIngredients") Cheats::LearnAllIngredients();
				else if (fn == "learnSpells") Cheats::LearnAllSpells();
				else if (fn == "learnShouts") Cheats::LearnAllShouts();
				else if (fn == "learnPerks") Cheats::LearnAllPerks();
				else if (fn == "forgetLearned") Cheats::ForgetLearned();
				else if (fn == "mapMarker") Cheats::TeleportToMapMarker();
				else if (fn == "spawn") {
					// "call spawn <base>": places one at the player; "last" names it in other commands.
					auto base = RE::TESForm::LookupByID<RE::TESBoundObject>(ParseForm(arg(2)));
					auto ref = base ? RE::PlayerCharacter::GetSingleton()->PlaceObjectAtMe(base, false) : nullptr;
					if (!ref) {
						return Quote("not spawned");
					}
					g_lastSpawned = ref->GetFormID();
					return std::format("\"{:08X}\"", g_lastSpawned);
				}
				else if (fn == "fillSoulGems") Cheats::FillSoulGems();
				else if (fn == "bounty") {
					// "call bounty [faction gold]": sets one faction's bounty if given; returns the total of all.
					if (args.size() > 4) {
						if (auto faction = RE::TESForm::LookupByID<RE::TESFaction>(ParseForm(arg(2)))) {
							faction->SetCrimeGold(static_cast<std::uint32_t>(f(3, 0.0f)));
						}
					}
					std::uint64_t total = 0;
					for (auto faction : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESFaction>()) {
						total += faction ? faction->GetCrimeGold() + faction->GetCrimeGoldViolent() : 0;
					}
					return std::to_string(total);
				}
				else if (fn == "confiscate") {
					// "call confiscate <faction> <item> <count>": as an arrest would, into the faction's player-inventory chest.
					auto faction = RE::TESForm::LookupByID<RE::TESFaction>(ParseForm(arg(2)));
					auto item = RE::TESForm::LookupByID<RE::TESBoundObject>(ParseForm(arg(3)));
					auto chest = faction ? faction->crimeData.factionPlayerInventoryContainer : nullptr;
					if (!chest || !item) {
						return Quote("no chest");
					}
					RE::PlayerCharacter::GetSingleton()->RemoveItem(item, static_cast<std::int32_t>(f(4, 1.0f)), RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, chest);
					return std::format("\"{:08X}\"", chest->GetFormID());
				}
				else if (fn == "stolenCount") {
					int stolen = 0;
					for (auto& [object, data] : RE::PlayerCharacter::GetSingleton()->GetInventory()) {
						if (data.second && data.second->extraLists) {
							for (auto list : *data.second->extraLists) {
								auto ownership = list ? list->GetByType<RE::ExtraOwnership>() : nullptr;
								stolen += ownership && ownership->owner != RE::PlayerCharacter::GetSingleton()->GetActorBase() ? list->GetCount() : 0;
							}
						}
					}
					return std::to_string(stolen);
				}
				else if (fn == "unmarkStolen") return std::to_string(Cheats::UnmarkStolen());
				else if (fn == "forceQuest") {
					// Experiment: start a quest with every alias made optional for the start.
					auto quest = RE::TESForm::LookupByEditorID<RE::TESQuest>(arg(2));
					if (!quest) {
						return Quote("no quest");
					}
					std::vector<RE::BGSBaseAlias*> changed;
					for (auto alias : quest->aliases) {
						if (alias && !alias->flags.any(RE::BGSBaseAlias::FLAGS::kOptional)) {
							alias->flags.set(RE::BGSBaseAlias::FLAGS::kOptional);
							changed.push_back(alias);
						}
					}
					bool result = false;
					quest->EnsureQuestStarted(result, true);
					for (auto alias : changed) {
						alias->flags.reset(RE::BGSBaseAlias::FLAGS::kOptional);
					}
					std::string filled;
					for (auto alias : quest->aliases) {
						if (alias && alias->GetVMTypeID() == RE::BGSRefAlias::VMTYPEID) {
							auto ref = quest->GetAliasedRef(alias->aliasID).get();
							filled += std::format("{}={} ", alias->aliasName.c_str(), ref ? std::format("{:08X}", ref->GetFormID()) : "-");
						}
					}
					return Quote(std::format("started {} running {} {}", result, quest->IsRunning(), filled));
				}
				else if (fn == "marry") {
					auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(2)));
					return Quote(Cheats::MarryTarget(ref ? ref->GetHandle() : RE::ObjectRefHandle{}));
				}
				else if (fn == "cameraAxes") {
					auto        camera = RE::Main::WorldRootCamera();
					const auto& r = camera->world.rotate;
					return std::format("{{\"col0\":[{:.2f},{:.2f},{:.2f}],\"col1\":[{:.2f},{:.2f},{:.2f}],\"col2\":[{:.2f},{:.2f},{:.2f}],\"heading\":{:.1f},\"firstPerson\":{}}}",
						r.entry[0][0], r.entry[1][0], r.entry[2][0], r.entry[0][1], r.entry[1][1], r.entry[2][1], r.entry[0][2], r.entry[1][2], r.entry[2][2],
						RE::PlayerCharacter::GetSingleton()->GetAngleZ() * 57.2958f, RE::PlayerCamera::GetSingleton()->IsInFirstPerson() ? "true" : "false");
				}
				else if (fn == "flightMove") {
					// "call flightMove <x> <y>" (strafe, forward; -1..1) or "call flightMove" to stop.
					Cheats::SetFlightTestMove(args.size() > 4 ? std::optional{ RE::NiPoint2{ f(2, 0.0f), f(3, 0.0f) } } : std::nullopt);
				}
				else if (fn == "look") {
					// The player's appearance data, to see a profile put it back.
					auto npc = RE::PlayerCharacter::GetSingleton()->GetActorBase();
					return std::format("{{\"weight\":{},\"morph0\":{},\"part0\":{},\"headParts\":{},\"skin\":[{},{},{}],\"tints\":{}}}", Num(npc->weight),
						npc->faceData ? Num(npc->faceData->morphs[0]) : std::string{ "null" }, npc->faceData ? npc->faceData->parts[0] : -1, npc->numHeadParts,
						+npc->bodyTintColor.red, +npc->bodyTintColor.green, +npc->bodyTintColor.blue,
						RE::PlayerCharacter::GetSingleton()->GetPlayerRuntimeData().tintMasks.size());
				}
				else if (fn == "setLook") {
					// Test only: "call setLook <weight> <morph0>".
					auto npc = RE::PlayerCharacter::GetSingleton()->GetActorBase();
					npc->weight = f(2, 50.0f);
					if (npc->faceData) {
						npc->faceData->morphs[0] = f(3, 0.0f);
					}
					RE::PlayerCharacter::GetSingleton()->DoReset3D(true);
				}
				else if (fn == "saveProfile") return Quote(Cheats::SaveProfile(Rest(a_line, 3)));
				else if (fn == "loadProfile") return Quote(Cheats::LoadProfile(Rest(a_line, 3), true));
				else if (fn == "deleteProfile") return Cheats::DeleteProfile(Rest(a_line, 3)) ? "true" : "false";
				else if (fn == "cureVampirism") return Cheats::CureVampirism() ? "true" : "false";
				else if (fn == "becomeVampire") return Cheats::BecomeVampire() ? "true" : "false";
				else if (fn == "cureLycanthropy") return Cheats::CureLycanthropy() ? "true" : "false";
				else if (fn == "restock") return std::to_string(Cheats::RestockMerchants());
				else if (fn == "waitMenu") Cheats::OpenWaitMenu();
				else if (fn == "resetDungeon") return Quote(Cheats::ResetCurrentDungeon());
				else if (fn == "divorce") return Quote(Cheats::Divorce());
				else if (fn == "spouse") {
					auto spouse = Cheats::Spouse();
					return spouse ? std::format("\"{:08X}\"", spouse->GetFormID()) : std::string{ "null" };
				}
				else if (fn == "thane") return Quote(Cheats::BecomeThaneEverywhere());
				else if (fn == "guildJobs") return Quote(Cheats::OpenGuildCityJobs());
				else if (fn == "castOn") {
					// "call castOn <spell> <ref>": the player casts it straight at the reference (no aiming).
					auto spell = RE::TESForm::LookupByID<RE::MagicItem>(ParseForm(arg(2)));
					auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(3)));
					auto player = RE::PlayerCharacter::GetSingleton();
					auto caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
					if (!spell || !target || !caster) {
						return Quote("cannot cast");
					}
					caster->CastSpellImmediate(spell, false, target, 1.0f, false, 0.0f, player);
				}
				else if (fn == "findScript") {
					// "call findScript <script name part>": references with such a script bound right now (form ids, cell).
					auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
					const auto part = Text::ToLower(arg(2));
					std::string out;
					RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
					for (const auto& [handle, scripts] : vm->attachedScripts) {
						for (const auto& script : scripts) {
							auto type = script ? script->GetTypeInfo() : nullptr;
							if (!type || Text::ToLower(type->GetName()).find(part) == std::string::npos) {
								continue;
							}
							auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(static_cast<RE::FormID>(handle & 0xFFFFFFFF));
							auto cell = ref ? ref->GetParentCell() : nullptr;
							out += std::format("{}{{\"ref\":\"{:08X}\",\"script\":{},\"cell\":{},\"world\":{}}}", out.empty() ? "" : ",", static_cast<RE::FormID>(handle & 0xFFFFFFFF),
								Quote(type->GetName()), Quote(cell ? cell->GetFormEditorID() : ""), Quote(ref && ref->GetWorldspace() ? ref->GetWorldspace()->GetFormEditorID() : ""));
						}
					}
					return "[" + out + "]";
				}
				else if (fn == "puzzles") {
					// "call puzzles": unsolved claw doors / pillar puzzles nearby and their pieces; "call solvePuzzle": the menu button.
					return Puzzles::Debug();
				}
				else if (fn == "solvePuzzle") {
					return Quote(Puzzles::SolveNearby());
				}
				else if (fn == "activate") {
					// "call activate <ref>": the player activates it, as with the E key (console "activate" misses some scripts).
					auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(2)));
					if (!ref) {
						return Quote("no such reference");
					}
					ref->ActivateRef(RE::PlayerCharacter::GetSingleton(), 0, nullptr, 1, false);
				}
				else if (fn == "playerName") {
					// "call playerName <name>": renames the Dragonborn for this session (screenshots); don't save after it.
					if (auto base = RE::PlayerCharacter::GetSingleton()->GetActorBase()) {
						base->fullName = Rest(a_line, 3);
					}
				}
				else if (fn == "castForward") {
					// "call castForward <spell> [right|left]": the player casts it where they look, as an aimed shot (the aimbot test);
					// with a hand, from that hand's caster (a staff's enchantment: what the staff itself does).
					auto spell = RE::TESForm::LookupByID<RE::MagicItem>(ParseForm(arg(2)));
					auto player = RE::PlayerCharacter::GetSingleton();
					const auto hand = args.size() > 4 ? arg(3) : std::string{};
					auto caster = player->GetMagicCaster(hand == "right" ? RE::MagicSystem::CastingSource::kRightHand :
					                                     hand == "left"  ? RE::MagicSystem::CastingSource::kLeftHand :
					                                                       RE::MagicSystem::CastingSource::kInstant);
					if (!spell || !caster) {
						return Quote("cannot cast");
					}
					caster->CastSpellImmediate(spell, false, nullptr, 1.0f, false, 0.0f, player);
				}
				else if (fn == "stepAway") {
					// "call stepAway <units>": moves the player that far along x from where they are.
					auto player = RE::PlayerCharacter::GetSingleton();
					auto pos = player->GetPosition();
					pos.x += f(2, 800.0f);
					player->SetPosition(pos, true);
				}
				else if (fn == "attack") {
					// "call attack <ref> [power|draw]": the player attacks (or draws the weapon) as when the button is pressed: the action, not just an animation event.
					auto player = RE::PlayerCharacter::GetSingleton();
					auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(2)));
					const auto kind = args.size() > 4 ? arg(3) : std::string{};
					const auto which = kind == "power" ? RE::DEFAULT_OBJECT::kActionRightPowerAttack :
					                   kind == "draw"  ? RE::DEFAULT_OBJECT::kActionDraw :
					                                       RE::DEFAULT_OBJECT::kActionRightAttack;
					auto       form = RE::BGSDefaultObjectManager::GetSingleton()->objects[std::to_underlying(which)];
					auto       action = form ? form->As<RE::BGSAction>() : nullptr;
					auto data = RE::TESActionData::Create();
					if (!data || !action) {
						return Quote("no action");
					}
					data->source = RE::NiPointer<RE::TESObjectREFR>(player);
					data->target = RE::NiPointer<RE::TESObjectREFR>(target);
					data->action = action;
					const bool done = data->Process();
					data->~TESActionData();
					RE::free(data);
					return done ? "true" : "false";
				}
				else if (fn == "enchantItem" || fn == "unenchant") {
					// "call enchantItem <object> <enchantment option> <magnitude>": the first such item of the player gets exactly that enchantment
					// (as the Зачарование page's button does); "call unenchant <object>": the page's "Снять зачарование" on it.
					const auto object = ParseForm(arg(2));
					const auto items = Enchanter::Items();
					const auto item = std::ranges::find_if(items, [&](const Enchanter::Item& a_item) { return a_item.object == object; });
					if (item == items.end()) {
						Enchanter::RefreshItems();
						return Quote("item not in the snapshot yet, try again");
					}
					if (fn == "unenchant") {
						return Quote(Enchanter::Remove(*item));
					}
					const auto  optionID = ParseForm(arg(3));
					const auto& options = Enchanter::Options();
					const auto  option = std::ranges::find_if(options, [&](const Enchanter::Option& a_option) { return a_option.enchantment == optionID; });
					if (option == options.end()) {
						return Quote("no such enchantment option");
					}
					auto effects = option->effects;
					for (auto& e : effects) {
						e.magnitude = f(4, e.magnitude);
					}
					// optional: own name ("-" = none), charge
					const auto name = args.size() > 6 && arg(5) != "-" ? arg(5) : std::string{};
					return Quote(Enchanter::Apply(*item, effects, name, args.size() > 7 ? f(6, 3000.0f) : 3000.0f));
				}
				else if (fn == "condfn") {
					// "call condfn <function name> <ref>": a one-item condition "<function> == 1" run on the reference with the player as target,
					// evaluated the way dialogue and idles evaluate theirs (through the command table).
					auto command = RE::SCRIPT_FUNCTION::LocateScriptCommand(arg(2));
					auto ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(3)));
					if (!command || !ref) {
						return Quote("no function or reference");
					}
					RE::TESConditionItem item;
					item.next = nullptr;
					item.data.comparisonValue.f = 1.0f;
					item.data.functionData.function = static_cast<RE::FUNCTION_DATA::FunctionID>(command - RE::SCRIPT_FUNCTION::GetFirstScriptCommand());
					item.data.functionData.params[0] = nullptr;
					item.data.functionData.params[1] = nullptr;
					item.data.flags.isOR = false;
					item.data.flags.global = false;
					item.data.flags.opCode = RE::CONDITION_ITEM_DATA::OpCode::kEqualTo;
					item.data.object = RE::CONDITIONITEMOBJECT::kSelf;
					RE::ConditionCheckParams params(ref, RE::PlayerCharacter::GetSingleton());
					return item.IsTrue(params) ? "true" : "false";
				}
				else if (fn == "conds") {
					// "call conds <idle editorID> <target>": each condition of the idle for the player against the target (1/0), then the whole list.
					auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(3)));
					auto player = RE::PlayerCharacter::GetSingleton();
					auto idle = RE::TESForm::LookupByEditorID<RE::TESIdleForm>(arg(2));
					if (!idle || !target) {
						return Quote("no idle or target");
					}
					std::string out;
					for (auto item = idle->conditions.head; item; item = item->next) {
						RE::ConditionCheckParams params(player, target);
						out += std::format("{}:{}{} ", std::to_underlying(item->data.functionData.function.get()), item->IsTrue(params) ? 1 : 0, item->data.flags.isOR ? "|" : "");
					}
					return Quote(out + "=> " + (idle->conditions.IsTrue(player, target) ? "true" : "false"));
				}
				else if (fn == "killmove") {
					// "call killmove": whether the player is in a killmove right now.
					return RE::PlayerCharacter::GetSingleton()->IsInKillMove() ? "true" : "false";
				}
				else if (fn == "standFrom") {
					// "call standFrom <ref> <compass degrees> <units>": puts the player that far from the reference, in that direction.
					auto player = RE::PlayerCharacter::GetSingleton();
					auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(2)));
					if (!target) {
						return Quote("no reference");
					}
					const float heading = f(3, 0.0f) * 0.01745329f, distance = f(4, 1000.0f);
					auto        pos = target->GetPosition();
					pos.x += distance * std::sin(heading);
					pos.y += distance * std::cos(heading);
					player->SetPosition(pos, true);
				}
				else if (fn == "faceOff") {
					// "call faceOff <ref> <degrees>": the player looks level, that many degrees to the left of the reference.
					auto player = RE::PlayerCharacter::GetSingleton();
					auto target = RE::TESForm::LookupByID<RE::TESObjectREFR>(ParseForm(arg(2)));
					if (!target) {
						return Quote("no reference");
					}
					const auto d = target->GetPosition() - player->GetPosition();
					player->SetAngle({ 0.0f, 0.0f, std::atan2(d.x, d.y) - f(3, 0.0f) * 0.01745329f });
					// the third-person camera keeps its own yaw offset, and an attack turns the player to the camera: put it behind
					auto camera = RE::PlayerCamera::GetSingleton();
					if (camera && camera->currentState && camera->currentState->id == RE::CameraState::kThirdPerson) {
						auto third = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
						third->freeRotation.x = 0.0f;
					}
				}
				else if (fn == "findNpc") {
					// "call findNpc <name part>": NPC bases whose name contains it (first 12), for spawning test creatures.
					const auto needle = Text::ToLower(Rest(a_line, 3));
					std::string out = "[";
					int         n = 0;
					for (auto npc : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>()) {
						const auto name = npc && npc->GetName() ? Text::ToUtf8(npc->GetName()) : std::string{};
						if (!name.empty() && Text::Contains(Text::ToLower(name), needle) && n++ < 12) {
							out += std::format("{}{{\"base\":\"{:08X}\",\"name\":{},\"level\":{}}}", out.size() > 1 ? "," : "", npc->GetFormID(), Quote(name), npc->GetLevel());
						}
					}
					return out + "]";
				}
				else if (fn == "aimInfo") {
					// "call aimInfo <ref>": where the aimbot aims at that actor, its model bounds and head-like nodes.
					auto actor = RE::TESForm::LookupByID<RE::Actor>(ParseForm(arg(2)));
					return actor ? Archery::DebugAim(actor) : Quote("no actor");
				}
				else if (fn == "standUp") RE::PlayerCharacter::GetSingleton()->InitiateGetUpPackage();  // leave furniture, as Tab would
				else if (fn == "station") {
					// "call station <index>": index into Cheats::Station
					if (!Cheats::OpenStation(static_cast<Cheats::Station>(static_cast<int>(f(2, 0.0f))))) {
						return Quote("station not opened");
					}
				}
				else if (fn == "fakeMapMarker") {
					// Test only: puts the player's map marker at x y z (in the current worldspace), as the map would.
					auto player = RE::PlayerCharacter::GetSingleton();
					auto marker = player->GetInfoRuntimeData().playerMapMarker.get();
					if (!marker) {
						auto xmarker = RE::TESForm::LookupByID<RE::TESBoundObject>(0x3B);
						marker = player->PlaceObjectAtMe(xmarker, true);
						player->GetInfoRuntimeData().playerMapMarker = marker->GetHandle();
					}
					marker->SetPosition({ f(2, 0.0f), f(3, 0.0f), f(4, 0.0f) });
				}
				else if (fn == "canFastTravel") return FastTravel::CanFastTravel(f(2, 1.0f) != 0.0f) ? "true" : "false";
				else if (fn == "killHostiles") Cheats::KillHostiles(f(2, 5000.0f));
				else if (fn == "loot") Cheats::LootNearby(f(2, 1500.0f));
				else if (fn == "storage") Cheats::OpenStorage();
				else if (fn == "stone") Cheats::SetStone(static_cast<int>(f(2, -1.0f)));
				else if (fn == "beastForm") Cheats::GiveBeastForm();
				else if (fn == "vampireLord") return Cheats::GiveVampireLord() ? "true" : "false";
				else if (fn == "followers") Cheats::SummonFollowers();
				else if (fn == "clearBounties") Cheats::ClearBounties();
				else if (fn == "dismissSummons") Cheats::DismissSummons();
				else if (fn == "savePoint") Cheats::SavePoint(Rest(a_line, 3));
				else if (fn == "gotoPoint") Cheats::TeleportToPoint(static_cast<std::size_t>(f(2, 0.0f)));
				else if (fn == "deletePoint") Cheats::DeletePoint(static_cast<std::size_t>(f(2, 0.0f)));
				else if (fn == "level") Cheats::SetLevel(static_cast<int>(f(2, 1.0f)));
				else if (fn == "addLevels") Cheats::AddLevels(static_cast<int>(f(2, 1.0f)));
				else if (fn == "perks") Cheats::AddPerkPoints(static_cast<int>(f(2, 1.0f)));
				else if (fn == "gold") Cheats::AddGold(static_cast<int>(f(2, 1000.0f)));
				else if (fn == "maxSkills") Cheats::MaxAllSkills();
				else if (fn == "setav") Cheats::SetBaseValue(RE::ActorValueList::LookupActorValueByName(arg(2).c_str()), f(3, 0.0f));
				else if (fn == "grant") Cheats::Grant(ParseForm(arg(2)), static_cast<int>(f(3, 1.0f)));
				else if (fn == "nextStage") return Cheats::NextQuestStage(ParseForm(arg(2))) ? "true" : "false";
				else if (fn == "completeQuest") Cheats::CompleteQuest(ParseForm(arg(2)));
				else if (fn == "moveToNpc") return Cheats::MoveToNpc(ParseForm(arg(2)), f(3, 0.0f) != 0.0f) ? "true" : "false";
				else throw std::runtime_error("no function " + fn);
				return "null";
			}
			throw std::runtime_error("unknown verb " + verb + "; try help");
		}

		void WriteBmp(const std::filesystem::path& a_path, const std::uint8_t* a_pixels, UINT a_pitch, UINT a_width, UINT a_height, bool a_bgra)
		{
			std::ofstream file{ a_path, std::ios::binary };
			const std::uint32_t rowSize = a_width * 4;
			const std::uint32_t imageSize = rowSize * a_height;
#pragma pack(push, 1)
			struct
			{
				std::uint16_t type{ 0x4D42 };
				std::uint32_t size;
				std::uint32_t reserved{ 0 };
				std::uint32_t offset{ 54 };
				std::uint32_t headerSize{ 40 };
				std::int32_t  width;
				std::int32_t  height;
				std::uint16_t planes{ 1 };
				std::uint16_t bits{ 32 };
				std::uint32_t compression{ 0 };
				std::uint32_t imageSize;
				std::int32_t  xppm{ 2835 }, yppm{ 2835 };
				std::uint32_t colors{ 0 }, important{ 0 };
			} header;
#pragma pack(pop)
			header.size = 54 + imageSize;
			header.width = static_cast<std::int32_t>(a_width);
			header.height = -static_cast<std::int32_t>(a_height);  // top-down
			header.imageSize = imageSize;
			file.write(reinterpret_cast<const char*>(&header), sizeof(header));
			std::vector<std::uint8_t> row(rowSize);
			for (UINT y = 0; y < a_height; ++y) {
				const auto src = a_pixels + static_cast<std::size_t>(y) * a_pitch;
				for (UINT x = 0; x < a_width; ++x) {
					const auto p = src + x * 4;
					row[x * 4 + 0] = a_bgra ? p[0] : p[2];
					row[x * 4 + 1] = p[1];
					row[x * 4 + 2] = a_bgra ? p[2] : p[0];
					row[x * 4 + 3] = 255;
				}
				file.write(reinterpret_cast<const char*>(row.data()), rowSize);
			}
		}
	}

	void Poll()
	{
		if (!Settings::Get().testBridge) {
			return;
		}
		if (g_screenshot && g_screenshot->framesLeft > 0) {
			--g_screenshot->framesLeft;
		}
		if (!g_steps.empty() && g_steps.front()()) {
			g_steps.pop_front();
		}
		const auto now = Clock::now();
		if (now - g_lastPoll < 100ms) {
			return;
		}
		g_lastPoll = now;

		const auto dir = Dir();
		if (!dir) {
			return;
		}
		std::error_code ec;
		std::filesystem::create_directories(*dir, ec);
		const auto in = *dir / "in.txt";
		const auto taken = *dir / "in.processing";
		if (!std::filesystem::exists(in, ec)) {
			return;
		}
		std::filesystem::rename(in, taken, ec);
		if (ec) {
			return;  // still being written
		}
		std::ifstream file{ taken };
		std::string   line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty() || line.starts_with('#')) {
				continue;
			}
			const auto id = line.substr(0, line.find(' '));
			try {
				bool deferred = false;
				auto data = Run(id, line, deferred);
				if (!deferred) {
					Reply(id, true, data);
				}
			} catch (const std::exception& e) {
				Reply(id, false, Quote(e.what()));
			}
		}
		file.close();
		std::filesystem::remove(taken, ec);
	}

	bool NeedsFrame() { return g_screenshot.has_value() || !g_steps.empty(); }

	void AfterRender(IDXGISwapChain* a_swapChain, ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		if (!g_screenshot || g_screenshot->framesLeft > 0) {
			return;
		}
		const auto shot = std::move(*g_screenshot);
		g_screenshot.reset();

		ID3D11Texture2D* backBuffer = nullptr;
		if (FAILED(a_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
			Reply(shot.id, false, Quote("no back buffer"));
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		backBuffer->GetDesc(&desc);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		ID3D11Texture2D* staging = nullptr;
		if (desc.SampleDesc.Count == 1 && SUCCEEDED(a_device->CreateTexture2D(&desc, nullptr, &staging))) {
			a_context->CopyResource(staging, backBuffer);
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(a_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
				const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
				WriteBmp(shot.path, static_cast<const std::uint8_t*>(mapped.pData), mapped.RowPitch, desc.Width, desc.Height, bgra);
				a_context->Unmap(staging, 0);
				Reply(shot.id, true, Quote(shot.path.string()));
			} else {
				Reply(shot.id, false, Quote("map failed"));
			}
			staging->Release();
		} else {
			Reply(shot.id, false, Quote(std::format("cannot copy back buffer (format {}, samples {})", static_cast<int>(desc.Format), desc.SampleDesc.Count)));
		}
		backBuffer->Release();
	}
}

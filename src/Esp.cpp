#include "Esp.h"

#include "Cheats.h"
#include "Input.h"
#include "Overlay.h"
#include "Puzzles.h"
#include "Settings.h"
#include "Text.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Esp
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float kUnitsPerMeter = 70.0f;
		constexpr auto  kRescanEvery = 300ms;  // the world is scanned a few times a second, drawn every frame
		constexpr int   kMaxMarks = 120;       // nearest first

		enum class Kind
		{
			kEnemy,
			kNpc,
			kFollower,
			kCorpse,
			kContainer,
			kItem,
			kFlora,
			kQuest,     // quest item: never cut by the mark limit
			kInteract,  // locked / load doors, levers and the like, crafting stations
			kTrap,
			kPuzzle  // an unsolved puzzle's keyhole, lever or button (with espInteract): never cut by the mark limit
		};

		struct Style
		{
			ImU32 color;
			int   shape;  // 0 diamond, 1 square, 2 circle, 3 triangle, 4 star
		};

		// Alpha multiplied, not replaced: the user's colour may already be see-through.
		ImU32 Faded(ImU32 a_col, float a_k)
		{
			const float alpha = ((a_col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a_k, 0.0f, 1.0f);
			return (a_col & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
		}

		// Colour and shape of a kind as the user set them (Settings: ESP look), with the overall opacity.
		Style StyleOf(Kind a_kind)
		{
			const auto& look = Settings::Get().esp;
			const auto  i = static_cast<std::size_t>(a_kind);
			return { Faded(look.colors[i], look.opacity), look.shapes[i] };
		}

		struct Mark
		{
			RE::ObjectRefHandle ref;
			Kind                kind;
			std::string         label;  // without the distance
			bool                warn;   // locked / owned: drawn with an outline
			float               lift;   // marker height above the reference origin

			// creatures only, refreshed with every scan
			bool                                       actor{ false };
			bool                                       visible{ true };  // direct line of sight from the player
			std::uint16_t                              level{ 0 };
			std::vector<RE::NiPointer<RE::NiAVObject>> bones;  // kBones order, empty if not humanoid
		};

		// Humanoid skeleton nodes and the lines drawn between them.
		constexpr const char* kBoneNames[] = {
			"NPC Head [Head]", "NPC Neck [Neck]", "NPC Spine2 [Spn2]", "NPC Spine1 [Spn1]", "NPC Pelvis [Pelv]",              // 0-4
			"NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]",                                           // 5-7
			"NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]",                                           // 8-10
			"NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]", "NPC R Thigh [RThg]", "NPC R Calf [RClf]",       // 11-15
			"NPC R Foot [Rft ]",                                                                                            // 16
		};
		constexpr std::pair<int, int> kBoneLines[] = {
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 1, 5 }, { 5, 6 }, { 6, 7 }, { 1, 8 }, { 8, 9 }, { 9, 10 },
			{ 4, 11 }, { 11, 12 }, { 12, 13 }, { 4, 14 }, { 14, 15 }, { 15, 16 },
		};

		constexpr RE::FormID kGlowFriend = 0x00000146;  // LifeDetected
		constexpr RE::FormID kGlowEnemy = 0x000DC209;   // LifeDetectedEnemy
		constexpr float      kGlowSeconds = 3.0f;       // short and refreshed, so it never sticks in a save
		constexpr auto       kGlowRefresh = 2s;
		std::unordered_map<RE::RefHandle, Clock::time_point> g_glowApplied;

		void FillActorInfo(Mark& a_mark, RE::Actor* a_actor, RE::PlayerCharacter* a_player, const Cheats::State& a_st)
		{
			a_mark.actor = true;
			bool unused = false;
			a_mark.visible = a_player->HasLineOfSight(a_actor, unused);
			a_mark.level = a_actor->GetLevel();
			if (a_st.whSkeleton) {
				static const std::vector<RE::BSFixedString> names{ std::begin(kBoneNames), std::end(kBoneNames) };
				if (auto root = a_actor->Get3D()) {
					for (const auto& name : names) {
						auto node = root->GetObjectByName(name);
						if (!node) {
							a_mark.bones.clear();  // not a humanoid skeleton
							break;
						}
						a_mark.bones.emplace_back(node);
					}
				}
			}
			if (a_st.whGlow && !a_actor->IsDead()) {
				const auto handle = a_actor->GetHandle().native_handle();
				const auto now = Clock::now();
				if (auto it = g_glowApplied.find(handle); it == g_glowApplied.end() || now - it->second > kGlowRefresh) {
					g_glowApplied[handle] = now;
					const auto id = a_actor->IsHostileToActor(a_player) ? kGlowEnemy : kGlowFriend;
					if (auto shader = RE::TESForm::LookupByID<RE::TESEffectShader>(id)) {
						a_actor->ApplyEffectShader(shader, kGlowSeconds);
					}
				}
			}
		}

		std::vector<Mark> g_marks;
		Clock::time_point g_lastScan;

		const char* LockName(RE::LOCK_LEVEL a_level)
		{
			switch (a_level) {
			case RE::LOCK_LEVEL::kVeryEasy: return Tr("lock.novice");
			case RE::LOCK_LEVEL::kEasy: return Tr("lock.apprentice");
			case RE::LOCK_LEVEL::kAverage: return Tr("lock.adept");
			case RE::LOCK_LEVEL::kHard: return Tr("lock.expert");
			case RE::LOCK_LEVEL::kVeryHard: return Tr("lock.master");
			case RE::LOCK_LEVEL::kRequiresKey: return Tr("lock.requires_key");
			default: return Tr("lock.locked");
			}
		}

		bool IsLooseItem(RE::FormType a_type)
		{
			switch (a_type) {
			case RE::FormType::Weapon:
			case RE::FormType::Armor:
			case RE::FormType::Ammo:
			case RE::FormType::AlchemyItem:
			case RE::FormType::Ingredient:
			case RE::FormType::Book:
			case RE::FormType::Scroll:
			case RE::FormType::SoulGem:
			case RE::FormType::KeyMaster:
			case RE::FormType::Misc:
				return true;
			default:
				return false;
			}
		}

		// Whether a container still has something to take. Its inventory also lists leftovers:
		// entries whose count dropped to 0 and the leveled lists the loot was rolled from.
		bool HasLoot(RE::TESObjectREFR* a_ref)
		{
			const auto items = a_ref->GetInventoryCounts([](RE::TESBoundObject& a_obj) { return !a_obj.Is(RE::FormType::LeveledItem); });
			return std::ranges::any_of(items, [](const auto& a_item) { return a_item.second > 0 && a_item.first->GetPlayable(); });
		}

		bool IsOreVein(RE::TESBoundObject* a_base)
		{
			if (!a_base->Is(RE::FormType::Activator)) {
				return false;
			}
			const auto name = Text::ToLower(Text::ToUtf8(a_base->GetName()));
			return Text::Contains(name, "жила") || Text::Contains(name, "vein");
		}

		// Things nobody goes looking for: plates, cups and other clutter, food, items worth nothing (often editor
		// leftovers like "Do Not Delete"). A misc item stays when it is crafting stuff (ore, ingots, hides, animal
		// parts, gems), gold or lockpicks, or worth 100+ (claws, Dwemer parts). Keys, notes and books always stay.
		bool IsJunk(RE::TESBoundObject* a_base)
		{
			static const std::array<RE::BGSKeyword*, 4> useful = [] {
				auto kw = [](const char* a_id) { return RE::TESForm::LookupByEditorID<RE::BGSKeyword>(a_id); };
				return std::array{ kw("VendorItemOreIngot"), kw("VendorItemAnimalHide"), kw("VendorItemAnimalPart"), kw("VendorItemGem") };
			}();
			if (!a_base->GetPlayable()) {
				return true;
			}
			switch (a_base->GetFormType()) {
			case RE::FormType::Misc: {
				if (a_base->GetFormID() == 0xF || a_base->GetFormID() == 0xA) {  // gold, lockpick
					return false;
				}
				auto keywords = a_base->As<RE::BGSKeywordForm>();
				const bool craft = std::ranges::any_of(useful, [&](RE::BGSKeyword* a_kw) { return a_kw && keywords->HasKeyword(a_kw); });
				return !craft && a_base->GetGoldValue() < 100;
			}
			case RE::FormType::Weapon:
			case RE::FormType::Armor:
			case RE::FormType::Ammo:
				return a_base->GetGoldValue() <= 0;
			case RE::FormType::AlchemyItem:
				return a_base->As<RE::AlchemyItem>()->IsFood();
			default:
				return false;
			}
		}

		// Traps carry a script named Trap... (TrapBase, TrapTriggerBase, TrapBear, trapTripwire and their children).
		// Looked up once per base object.
		bool IsTrap(RE::TESObjectREFR* a_ref, RE::TESBoundObject* a_base)
		{
			static std::unordered_map<RE::FormID, bool> cache;
			if (auto it = cache.find(a_base->GetFormID()); it != cache.end()) {
				return it->second;
			}
			auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy) {
				return false;
			}
			const auto handle = policy->GetHandleForObject(a_ref->GetFormType(), a_ref);
			bool       trap = false;
			{
				RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
				if (auto it = vm->attachedScripts.find(handle); it != vm->attachedScripts.end()) {
					for (const auto& script : it->second) {
						for (auto type = script ? script->GetTypeInfo() : nullptr; type && !trap; type = type->GetParent()) {
							trap = Text::ToLower(type->GetName()).starts_with("trap");
						}
					}
				}
			}
			cache[a_base->GetFormID()] = trap;
			return trap;
		}

		// "Door → Bleak Falls Barrow": where a load door leads.
		std::string DoorTarget(RE::TESObjectREFR* a_door)
		{
			auto teleport = a_door->extraList.GetByType<RE::ExtraTeleport>();
			auto target = teleport && teleport->teleportData ? teleport->teleportData->linkedDoor.get() : nullptr;
			if (!target) {
				return {};
			}
			auto cell = target->GetParentCell();
			if (cell && cell->IsInteriorCell() && cell->GetFullName() && *cell->GetFullName()) {
				return Text::ToUtf8(cell->GetFullName());
			}
			if (auto location = target->GetCurrentLocation(); location && location->GetFullName()) {
				return Text::ToUtf8(location->GetFullName());
			}
			auto world = target->GetWorldspace();
			return world && world->GetFullName() ? Text::ToUtf8(world->GetFullName()) : std::string{};
		}

		std::optional<Mark> Classify(RE::TESObjectREFR* a_ref, RE::PlayerCharacter* a_player, const Cheats::State& a_st)
		{
			if (auto actor = a_ref->As<RE::Actor>()) {
				const auto name = Text::ToUtf8(actor->GetDisplayFullName());
				const float lift = actor->GetHeight() + 12.0f;
				if (actor->IsDead()) {
					return a_st.espCorpses ? std::optional{ Mark{ a_ref->GetHandle(), Kind::kCorpse, name + Tr("esp.corpse"), false, 30.0f } } : std::nullopt;
				}
				if (actor->IsPlayerTeammate()) {
					return a_st.espFollowers ? std::optional{ Mark{ a_ref->GetHandle(), Kind::kFollower, name, false, lift } } : std::nullopt;
				}
				if (actor->IsHostileToActor(a_player)) {
					return a_st.espEnemies ? std::optional{ Mark{ a_ref->GetHandle(), Kind::kEnemy, name, false, lift } } : std::nullopt;
				}
				return a_st.espNpcs ? std::optional{ Mark{ a_ref->GetHandle(), Kind::kNpc, name, false, lift } } : std::nullopt;
			}

			auto base = a_ref->GetBaseObject();
			if (!base) {
				return std::nullopt;
			}
			const auto type = base->GetFormType();

			if (type == RE::FormType::Container && a_st.espContainers) {
				if (!HasLoot(a_ref)) {
					return std::nullopt;  // nothing (left) to take
				}
				auto        label = Text::ToUtf8(a_ref->GetDisplayFullName());
				const bool  locked = a_ref->IsLocked();
				const bool  owned = a_ref->IsCrimeToActivate();
				if (locked) {
					label += std::format(" · {}", LockName(a_ref->GetLockLevel()));
				}
				if (owned) {
					label += Tr("esp.owned");
				}
				return Mark{ a_ref->GetHandle(), Kind::kContainer, label, locked || owned, 40.0f };
			}

			if (IsLooseItem(type) && a_st.espItems) {
				if (a_ref->HasQuestObject()) {
					return Mark{ a_ref->GetHandle(), Kind::kQuest, "★ " + Text::ToUtf8(a_ref->GetDisplayFullName()), false, 15.0f };
				}
				auto label = Text::ToUtf8(a_ref->GetDisplayFullName());
				if (label.empty() || label == "Do Not Delete" || IsJunk(base)) {  // that one is a vanilla editor helper
					return std::nullopt;
				}
				const auto count = a_ref->extraList.GetCount();
				if (count > 1) {
					label += std::format(" ×{}", count);
				}
				const bool owned = a_ref->IsCrimeToActivate();
				if (owned) {
					label += Tr("esp.owned_2");
				}
				return Mark{ a_ref->GetHandle(), Kind::kItem, label, owned, 15.0f };
			}

			if (a_st.espFlora) {
				const bool harvested = (a_ref->formFlags & RE::TESObjectREFR::RecordFlags::kHarvested) != 0;
				const bool plant = (type == RE::FormType::Flora || (type == RE::FormType::Tree && base->As<RE::TESObjectTREE>()->produceItem)) && !harvested;
				if (plant || IsOreVein(base)) {
					return Mark{ a_ref->GetHandle(), Kind::kFlora, Text::ToUtf8(a_ref->GetDisplayFullName()), false, 30.0f };
				}
			}

			if (a_ref->extraList.HasType(RE::ExtraDataType::kPrimitive)) {
				return std::nullopt;  // an invisible trigger volume, nothing to see or use there
			}

			if (a_st.espTraps && (type == RE::FormType::Activator || type == RE::FormType::Hazard) && IsTrap(a_ref, base)) {
				auto name = Text::ToUtf8(a_ref->GetDisplayFullName());
				return Mark{ a_ref->GetHandle(), Kind::kTrap, name.empty() ? Tr("esp.trap") : name, true, 20.0f };
			}

			if (a_st.espInteract) {
				auto name = Text::ToUtf8(a_ref->GetDisplayFullName());
				if (name.empty()) {
					return std::nullopt;
				}
				if (type == RE::FormType::Door) {
					const bool locked = a_ref->IsLocked();
					const auto target = DoorTarget(a_ref);
					if (!locked && target.empty()) {
						return std::nullopt;  // an ordinary inner door
					}
					if (!target.empty()) {
						name += " → " + target;
					}
					if (locked) {
						name += std::format(" · {}", LockName(a_ref->GetLockLevel()));
					}
					return Mark{ a_ref->GetHandle(), Kind::kInteract, name, locked, 110.0f };
				}
				if (type == RE::FormType::Activator && !IsOreVein(base) && !IsTrap(a_ref, base)) {
					return Mark{ a_ref->GetHandle(), Kind::kInteract, name, false, 40.0f };
				}
				if (type == RE::FormType::Furniture && base->As<RE::TESFurniture>()->workBenchData.benchType != RE::TESFurniture::WorkBenchData::BenchType::kNone) {
					return Mark{ a_ref->GetHandle(), Kind::kInteract, name, false, 60.0f };
				}
			}
			return std::nullopt;
		}

		void Rescan(RE::PlayerCharacter* a_player)
		{
			const auto& st = Cheats::Get();
			const float range = st.espRange * kUnitsPerMeter;
			const auto  origin = a_player->GetPosition();

			std::vector<std::pair<float, Mark>> found;
			std::unordered_set<RE::RefHandle>   puzzles;  // their keyhole or lever is not marked twice
			if (st.espInteract) {
				for (auto& puzzle : Puzzles::Marks(range)) {
					if (auto ref = puzzle.ref.get()) {
						puzzles.insert(puzzle.ref.native_handle());
						found.emplace_back(ref->GetPosition().GetDistance(origin), Mark{ puzzle.ref, Kind::kPuzzle, std::move(puzzle.label), false, 70.0f });
					}
				}
			}
			RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, range, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref != a_player && !a_ref->IsDisabled() && !a_ref->IsMarkedForDeletion() && a_ref->Is3DLoaded() &&
					!puzzles.contains(a_ref->GetHandle().native_handle())) {
					if (auto mark = Classify(a_ref, a_player, st); mark && !mark->label.empty()) {
						if (auto actor = a_ref->As<RE::Actor>()) {
							FillActorInfo(*mark, actor, a_player, st);
						}
						found.emplace_back(a_ref->GetPosition().GetDistance(origin), std::move(*mark));
					}
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			// creatures, quest items and puzzles always; everything else nearest first up to the limit
			std::ranges::sort(found, {}, &std::pair<float, Mark>::first);
			g_marks.clear();
			int others = 0;
			for (auto& [distance, mark] : found) {
				if (mark.actor || mark.kind == Kind::kQuest || mark.kind == Kind::kPuzzle || others++ < kMaxMarks) {
					g_marks.push_back(std::move(mark));
				}
			}
		}
	}

	bool IsClutter(RE::TESBoundObject* a_base)
	{
		return IsJunk(a_base);
	}

	void DrawShape(ImDrawList* a_dl, ImVec2 a_p, float a_r, ImU32 a_color, int a_shape, bool a_warn)
	{
		const float alpha = ((a_color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
		const ImU32 outline = Faded(a_warn ? IM_COL32(240, 70, 50, 255) : IM_COL32(0, 0, 0, 200), alpha);
		const float thick = (a_warn ? 2.0f : 1.0f) * std::max(1.0f, a_r / 5.0f);
		switch (a_shape) {
		case 1:
			a_dl->AddRectFilled({ a_p.x - a_r, a_p.y - a_r }, { a_p.x + a_r, a_p.y + a_r }, a_color);
			a_dl->AddRect({ a_p.x - a_r, a_p.y - a_r }, { a_p.x + a_r, a_p.y + a_r }, outline, 0, 0, thick);
			break;
		case 2:
			a_dl->AddCircleFilled(a_p, a_r, a_color);
			a_dl->AddCircle(a_p, a_r, outline, 0, thick);
			break;
		case 4: {
			ImVec2 pts[10];
			for (int i = 0; i < 10; ++i) {
				const float angle = -IM_PI / 2 + i * IM_PI / 5;
				const float rr = (i % 2 ? 0.45f : 1.25f) * a_r;
				pts[i] = { a_p.x + std::cos(angle) * rr, a_p.y + std::sin(angle) * rr };
			}
			for (int i = 0; i < 10; i += 2) {
				a_dl->AddTriangleFilled(a_p, pts[i], pts[(i + 1) % 10], a_color);
				a_dl->AddTriangleFilled(a_p, pts[(i + 9) % 10], pts[i], a_color);
			}
			a_dl->AddPolyline(pts, 10, outline, ImDrawFlags_Closed, thick);
			break;
		}
		case 3:
			a_dl->AddTriangleFilled({ a_p.x, a_p.y - a_r }, { a_p.x + a_r, a_p.y + a_r }, { a_p.x - a_r, a_p.y + a_r }, a_color);
			a_dl->AddTriangle({ a_p.x, a_p.y - a_r }, { a_p.x + a_r, a_p.y + a_r }, { a_p.x - a_r, a_p.y + a_r }, outline, thick);
			break;
		default: {
			const ImVec2 pts[4] = { { a_p.x, a_p.y - a_r }, { a_p.x + a_r, a_p.y }, { a_p.x, a_p.y + a_r }, { a_p.x - a_r, a_p.y } };
			a_dl->AddConvexPolyFilled(pts, 4, a_color);
			a_dl->AddPolyline(pts, 4, outline, ImDrawFlags_Closed, thick);
		}
		}
	}

	std::vector<std::string> DebugMarks()
	{
		std::vector<std::string> out;
		for (const auto& mark : g_marks) {
			if (auto ref = mark.ref.get()) {
				out.push_back(std::format("{:08X}|{}|{}", ref->GetFormID(), static_cast<int>(mark.kind), mark.label));
			}
		}
		return out;
	}

	namespace
	{
		// "lvl 5 · 12 m · behind wall" without the parts that are switched off.
		std::string JoinInfo(std::initializer_list<std::string> a_parts)
		{
			constexpr std::string_view dot = "·";
			std::string                out;
			for (auto part : a_parts) {
				while (part.starts_with(' ') || part.starts_with(dot)) {
					part.erase(0, part.starts_with(' ') ? 1 : dot.size());
				}
				while (part.ends_with(' ') || part.ends_with(dot)) {
					part.erase(part.size() - (part.ends_with(' ') ? 1 : dot.size()));
				}
				if (!part.empty()) {
					out += (out.empty() ? "" : " · ") + part;
				}
			}
			return out;
		}

		bool Project(RE::NiCamera* a_camera, const RE::NiPoint3& a_point, ImVec2& a_out)
		{
			float x = 0, y = 0, z = 0;
			if (!a_camera->WorldPtToScreenPt3(a_point, x, y, z, 1e-5f) || z < 0.0f) {
				return false;
			}
			const auto& io = ImGui::GetIO();
			a_out = { x * io.DisplaySize.x, (1.0f - y) * io.DisplaySize.y };
			return true;
		}

		void Text(ImDrawList* a_dl, ImFont* a_font, float a_size, ImVec2 a_pos, ImU32 a_col, const std::string& a_text)
		{
			a_dl->AddText(a_font, a_size, { a_pos.x + 1, a_pos.y + 1 }, IM_COL32(0, 0, 0, 220), a_text.c_str());
			a_dl->AddText(a_font, a_size, a_pos, a_col, a_text.c_str());
		}

		// Wallhack look for a creature: box, health, skeleton, snapline, name and info.
		// Returns false when it cannot be drawn this way (then a plain marker is used).
		bool DrawActor(ImDrawList* a_dl, RE::NiCamera* a_camera, RE::Actor* a_actor, const Mark& a_mark, float a_distance, const Style& a_style)
		{
			const auto& st = Cheats::Get();
			auto        feetPos = a_actor->GetPosition();
			auto        headPos = feetPos;
			headPos.z += a_actor->GetHeight();
			ImVec2 feet, head;
			if (!Project(a_camera, feetPos, feet) || !Project(a_camera, headPos, head)) {
				return false;
			}
			const float s = Overlay::Scale();
			const float h = std::max(feet.y - head.y, 6.0f * s);
			const float w = h * 0.45f;
			const ImVec2 min{ head.x - w * 0.5f, head.y };
			const ImVec2 max{ head.x + w * 0.5f, head.y + h };
			const bool  dead = a_actor->IsDead();
			const auto& look = Settings::Get().esp;
			const ImU32 col = Faded(a_style.color, a_mark.visible ? 1.0f : 0.55f);
			const float thick = std::max(1.0f, 1.6f * s * look.lineScale);

			if (st.whSnaplines && !dead) {
				const auto& io = ImGui::GetIO();
				a_dl->AddLine({ io.DisplaySize.x * 0.5f, io.DisplaySize.y }, { feet.x, max.y }, Faded(a_style.color, 0.35f), std::max(1.0f, look.lineScale));
			}
			if (st.whBoxes) {
				// faint frame plus solid corners
				a_dl->AddRect(min, max, Faded(a_style.color, 0.18f), 0, 0, std::max(1.0f, look.lineScale));
				const float c = std::min(w, h) * 0.28f;
				for (const auto& [corner, dx, dy] : { std::tuple{ min, 1.0f, 1.0f }, std::tuple{ ImVec2{ max.x, min.y }, -1.0f, 1.0f },
						 std::tuple{ ImVec2{ min.x, max.y }, 1.0f, -1.0f }, std::tuple{ max, -1.0f, -1.0f } }) {
					a_dl->AddLine({ corner.x + 1, corner.y + 1 }, { corner.x + dx * c + 1, corner.y + 1 }, IM_COL32(0, 0, 0, 160), thick);
					a_dl->AddLine({ corner.x + 1, corner.y + 1 }, { corner.x + 1, corner.y + dy * c + 1 }, IM_COL32(0, 0, 0, 160), thick);
					a_dl->AddLine(corner, { corner.x + dx * c, corner.y }, col, thick);
					a_dl->AddLine(corner, { corner.x, corner.y + dy * c }, col, thick);
				}
			}
			if (st.whSkeleton && a_mark.bones.size() == std::size(kBoneNames)) {
				ImVec2 points[std::size(kBoneNames)];
				bool   ok[std::size(kBoneNames)];
				for (std::size_t i = 0; i < std::size(kBoneNames); ++i) {
					ok[i] = Project(a_camera, a_mark.bones[i]->world.translate, points[i]);
				}
				for (const auto& [a, b] : kBoneLines) {
					if (ok[a] && ok[b]) {
						a_dl->AddLine(points[a], points[b], col, thick);
					}
				}
			}
			if (st.whHealth && !dead) {
				auto        avo = a_actor->AsActorValueOwner();
				const float maxHp = a_actor->GetActorValueMax(RE::ActorValue::kHealth);
				const float pct = maxHp > 0 ? std::clamp(avo->GetActorValue(RE::ActorValue::kHealth) / maxHp, 0.0f, 1.0f) : 0.0f;
				const float bw = std::max(2.5f * s, 2.0f);
				const ImVec2 bmin{ min.x - bw - 4.0f * s, min.y };
				a_dl->AddRectFilled({ bmin.x - 1, bmin.y - 1 }, { bmin.x + bw + 1, max.y + 1 }, IM_COL32(0, 0, 0, 170));
				const ImU32 hp = pct > 0.6f ? IM_COL32(95, 205, 95, 255) : pct > 0.3f ? IM_COL32(235, 195, 70, 255) : IM_COL32(230, 70, 55, 255);
				a_dl->AddRectFilled({ bmin.x, max.y - h * pct }, { bmin.x + bw, max.y }, hp);
			}

			auto        font = Overlay::GetFonts().caption;
			const float fontSize = font->LegacySize * Overlay::Scale() * look.textScale;
			if (look.names) {
				const auto& name = a_mark.label;
				const auto  nameSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name.c_str());
				Text(a_dl, font, fontSize, { head.x - nameSize.x * 0.5f, min.y - nameSize.y - 2.0f * s }, col, name);
			}
			const auto distance = look.distance ? Lang::Fmt("esp.m_2", a_distance / kUnitsPerMeter) : std::string{};
			const auto info = st.whInfo ? JoinInfo({ dead ? "" : Lang::Fmt("esp.lvl", a_mark.level), distance, a_mark.visible ? "" : Tr("esp.behind_wall") }) :
			                              distance;
			if (!info.empty()) {
				const auto infoSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, info.c_str());
				Text(a_dl, font, fontSize, { head.x - infoSize.x * 0.5f, max.y + 2.0f * s }, Faded(col, 0.85f), info);
			}
			return true;
		}
	}

	bool IsActive()
	{
		if (!Cheats::Get().esp || Input::IsMenuOpen()) {
			return false;
		}
		auto ui = RE::UI::GetSingleton();
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!ui || !player || !player->Is3DLoaded() || ui->GameIsPaused()) {
			return false;
		}
		// Menus that cover the screen without pausing the game.
		for (const auto& name : { RE::CraftingMenu::MENU_NAME, RE::DialogueMenu::MENU_NAME, RE::LockpickingMenu::MENU_NAME, RE::BookMenu::MENU_NAME }) {
			if (ui->IsMenuOpen(name)) {
				return false;
			}
		}
		return true;
	}

	void Draw()
	{
		auto camera = RE::Main::WorldRootCamera();
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!camera || !player) {
			return;
		}
		if (const auto now = Clock::now(); now - g_lastScan > kRescanEvery) {
			g_lastScan = now;
			Rescan(player);
		}

		const auto& io = ImGui::GetIO();
		const float s = Overlay::Scale();
		const auto  origin = player->GetPosition();
		auto        dl = ImGui::GetBackgroundDrawList();
		auto        font = Overlay::GetFonts().caption;
		const auto& look = Settings::Get().esp;
		const float fontSize = font->LegacySize * Overlay::Scale() * look.textScale;

		struct Label
		{
			ImVec2      anchor;  // centre-bottom of the text
			float       distance;
			ImU32       color;
			std::string text;
		};
		std::vector<Label> labels;

		for (const auto& mark : g_marks) {
			auto ref = mark.ref.get();
			if (!ref || ref->IsDisabled()) {
				continue;
			}
			auto point = ref->GetPosition();
			const float distance = point.GetDistance(origin);
			const auto  style = StyleOf(mark.kind);
			if (auto actor = mark.actor ? ref->As<RE::Actor>() : nullptr) {
				const auto& st = Cheats::Get();
				if ((st.whBoxes || st.whSkeleton || st.whHealth || st.whSnaplines) && DrawActor(dl, camera, actor, mark, distance, style)) {
					continue;
				}
			}
			point.z += mark.lift;
			float x = 0, y = 0, z = 0;
			if (!camera->WorldPtToScreenPt3(point, x, y, z, 1e-5f) || z < 0.0f) {
				continue;  // behind the camera
			}
			const ImVec2 p{ x * io.DisplaySize.x, (1.0f - y) * io.DisplaySize.y };
			const float  r = 5.0f * s * look.iconScale;
			DrawShape(dl, p, r, style.color, style.shape, mark.warn);

			auto text = look.names && look.distance ? Lang::Fmt("esp.m_3", mark.label, distance / kUnitsPerMeter) :
			            look.names                  ? mark.label :
			            look.distance               ? Lang::Fmt("esp.m_2", distance / kUnitsPerMeter) :
			                                          std::string{};
			if (!text.empty()) {
				labels.push_back({ { p.x, p.y - r - 3.0f * s }, distance, style.color, std::move(text) });
			}
		}

		// Nearer labels claim their spot first; a farther one that would overlap moves up a line or two,
		// and when there is still no room only its marker stays.
		std::ranges::sort(labels, {}, &Label::distance);
		std::vector<ImRect> placed;
		placed.reserve(labels.size());
		for (const auto& label : labels) {
			const auto size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.text.c_str());
			for (int step = 0; step < 3; ++step) {
				const ImVec2 t{ label.anchor.x - size.x * 0.5f, label.anchor.y - size.y * (step + 1) };
				const ImRect rect{ t, { t.x + size.x, t.y + size.y } };
				if (std::ranges::any_of(placed, [&](const ImRect& a_other) { return a_other.Overlaps(rect); })) {
					continue;
				}
				placed.push_back(rect);
				dl->AddText(font, fontSize, { t.x + 1, t.y + 1 }, IM_COL32(0, 0, 0, 220), label.text.c_str());
				dl->AddText(font, fontSize, t, label.color, label.text.c_str());
				break;
			}
		}
	}
}

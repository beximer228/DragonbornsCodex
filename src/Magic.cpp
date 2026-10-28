#include "Magic.h"

#include "Cheats.h"

// Per-school spell tuning for the player. The game asks BGSEntryPoint::HandleEntryPoint for every perk-adjusted
// value ("Augmented Flames" and the like go through it): the base value is passed by pointer and perks change it.
// The call sites for the values below are hooked, the game's perks run first and the menu's multiplier is applied
// on top, so only the player's spells change and NPCs casting the same spells are left alone.
//
// Argument layout of those calls (checked in the 1.6.1170 disassembly):
//   ModSpellMagnitude / ModSpellDuration     (ep, caster, MagicItem*, target, float*)   — target is null in the magic menu
//   ModSpellCost / ModCommandedActorLimit    (ep, actor,  MagicItem*, float*)
//   ModIncomingSpellMagnitude / Duration     (ep, target, MagicItem*, float*)   — the magnitude one is a tail jump
namespace Magic
{
	namespace
	{
		using EP = RE::BGSEntryPoint::ENTRY_POINT;

		constexpr std::uint8_t kSpellMagnitude = static_cast<std::uint8_t>(EP::kModSpellMagnitude);
		constexpr std::uint8_t kSpellDuration = static_cast<std::uint8_t>(EP::kModSpellDuration);
		constexpr std::uint8_t kSpellCost = static_cast<std::uint8_t>(EP::kModSpellCost);
		constexpr std::uint8_t kIncomingMagnitude = static_cast<std::uint8_t>(EP::kModIncomingSpellMagnitude);
		constexpr std::uint8_t kIncomingDuration = static_cast<std::uint8_t>(EP::kModIncomingSpellDuration);
		constexpr std::uint8_t kCommandedLimit = static_cast<std::uint8_t>(EP::kModCommandedActorLimit);

		// How many call sites of each entry point the hooked game version has (1.6.1170). Anything else means
		// a different build: then nothing is patched rather than patching the wrong thing.
		constexpr std::pair<std::uint8_t, int> kExpectedSites[] = {
			{ kSpellMagnitude, 4 }, { kSpellDuration, 3 }, { kSpellCost, 1 }, { kIncomingMagnitude, 1 }, { kIncomingDuration, 1 }, { kCommandedLimit, 1 },
		};

		using Handler = void (*)(EP, RE::Actor*, ...);
		Handler g_original = nullptr;
		bool    g_installed = false;
		thread_local bool g_scaledOnAdd = false;  // set while AddTargetHook already scaled the effect being added

		bool IsPlayer(RE::Actor* a_actor) { return a_actor && a_actor->IsPlayerRef(); }

		// Cast spells and scrolls only: abilities, diseases, potions, enchantments and shouts keep their values.
		const Cheats::School* SchoolOf(RE::MagicItem* a_item)
		{
			if (!a_item) {
				return nullptr;
			}
			const auto type = a_item->GetSpellType();
			if (type != RE::MagicSystem::SpellType::kSpell && type != RE::MagicSystem::SpellType::kScroll) {
				return nullptr;
			}
			auto& st = Cheats::Get();
			switch (a_item->GetAssociatedSkill()) {
			case RE::ActorValue::kAlteration: return &st.alteration;
			case RE::ActorValue::kConjuration: return &st.conjuration;
			case RE::ActorValue::kDestruction: return &st.destruction;
			case RE::ActorValue::kIllusion: return &st.illusion;
			case RE::ActorValue::kRestoration: return &st.restoration;
			default: return nullptr;
			}
		}

		void CasterValue(EP a_ep, RE::Actor* a_caster, RE::MagicItem* a_item, void* a_target, float* a_value)
		{
			g_original(a_ep, a_caster, a_item, a_target, a_value);
			if (!a_value || !IsPlayer(a_caster)) {
				return;
			}
			if (auto school = SchoolOf(a_item)) {
				*a_value *= a_ep == EP::kModSpellDuration ? school->duration : school->power;
			}
		}

		void OwnerValue(EP a_ep, RE::Actor* a_actor, RE::MagicItem* a_item, float* a_value)
		{
			g_original(a_ep, a_actor, a_item, a_value);
			if (!a_value || !IsPlayer(a_actor)) {
				return;
			}
			const auto& st = Cheats::Get();
			switch (a_ep) {
			case EP::kModSpellCost:
				if (auto school = SchoolOf(a_item)) {
					*a_value *= school->cost;
				}
				break;
			case EP::kModCommandedActorLimit:
				*a_value += std::round(st.summonExtra);
				break;
			case EP::kModIncomingSpellMagnitude:
			case EP::kModIncomingSpellDuration:
				// Only what hurts: the player's own heals and buffs are not "incoming magic".
				if (a_item && a_item->IsHostile() && !(g_scaledOnAdd && a_ep == EP::kModIncomingSpellMagnitude)) {
					*a_value *= a_ep == EP::kModIncomingSpellMagnitude ? st.incomingMagic : st.incomingMagicDuration;
				}
				break;
			default:
				break;
			}
		}

		// Cloaks, hazards and the like put their effect on the player with the magnitude already decided
		// (AddTargetData::magnitude, 0 = "work it out"), and then the incoming-magnitude entry point is not asked.
		// Those are scaled here; while that happens the entry point hooks leave the same effect alone.
		struct AddTargetHook
		{
			static bool Thunk(RE::MagicTarget* a_this, RE::MagicTarget::AddTargetData& a_data)
			{
				const float mult = Cheats::Get().incomingMagic;
				const bool  scale = a_data.magnitude > 0.0f && std::abs(mult - 1.0f) > 0.001f && a_data.magicItem && a_data.magicItem->IsHostile() &&
				                   !(a_data.caster && a_data.caster->IsPlayerRef());
				if (!scale) {
					return original(a_this, a_data);
				}
				a_data.magnitude = std::max(a_data.magnitude * mult, 0.0001f);  // 0 would mean "no override"
				g_scaledOnAdd = true;
				const bool result = original(a_this, a_data);
				g_scaledOnAdd = false;
				return result;
			}
			static inline bool (*original)(RE::MagicTarget*, RE::MagicTarget::AddTargetData&) = nullptr;
		};

		struct Site
		{
			std::uintptr_t address;
			std::uint8_t   entryPoint;
			bool           jump;  // tail call
		};

		// The entry point id is a constant loaded into ecx shortly before the call: `mov ecx, imm32` (B9 xx 00 00 00),
		// or, where the target argument is null, `xor r9d, r9d` ... `lea ecx, [r9 + imm8]` (45 33 C9 ... 41 8D 49 xx)
		// right before the call — those are the magic menu's descriptions.
		std::optional<std::uint8_t> EntryPointBefore(const std::uint8_t* a_call, const std::uint8_t* a_begin)
		{
			constexpr std::ptrdiff_t kWindow = 40;
			constexpr std::ptrdiff_t kZeroWindow = 24;
			if (a_call - kZeroWindow >= a_begin && a_call[-4] == 0x41 && a_call[-3] == 0x8D && a_call[-2] == 0x49) {
				for (auto p = a_call - 7; p >= a_call - kZeroWindow; --p) {
					if (p[0] == 0x45 && p[1] == 0x33 && p[2] == 0xC9) {
						return a_call[-1];
					}
				}
				return std::nullopt;
			}
			for (auto p = a_call - 5; p >= a_begin && p >= a_call - kWindow; --p) {
				if (p[0] == 0xB9 && p[2] == 0 && p[3] == 0 && p[4] == 0) {
					return p[1];
				}
			}
			return std::nullopt;
		}

		std::vector<Site> FindSites()
		{
			const auto target = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(23073, 23526) }.address();
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			const auto bytes = text.pointer<std::uint8_t>();
			std::vector<Site> sites;
			for (std::size_t i = 0; i + 5 <= text.size(); ++i) {
				if (bytes[i] != 0xE8 && bytes[i] != 0xE9) {
					continue;
				}
				std::int32_t rel;
				std::memcpy(&rel, bytes + i + 1, 4);
				const auto at = reinterpret_cast<std::uintptr_t>(bytes + i);
				if (at + 5 + rel != target) {
					continue;
				}
				const auto ep = EntryPointBefore(bytes + i, bytes);
				if (ep && std::ranges::any_of(kExpectedSites, [&](const auto& a_e) { return a_e.first == *ep; })) {
					sites.push_back({ at, *ep, bytes[i] == 0xE9 });
				}
			}
			return sites;
		}
	}

	void Install()
	{
		const auto sites = FindSites();
		for (const auto& [ep, expected] : kExpectedSites) {
			const auto found = std::ranges::count(sites, ep, &Site::entryPoint);
			if (found != expected) {
				logger::warn("Magic tuning is off: entry point {} has {} call sites, expected {}", ep, found, expected);
				return;
			}
		}
		auto& trampoline = SKSE::GetTrampoline();
		const auto base = REL::Module::get().base();
		for (const auto& site : sites) {
			const bool fiveArgs = site.entryPoint == kSpellMagnitude || site.entryPoint == kSpellDuration;
			const auto hook = fiveArgs ? reinterpret_cast<std::uintptr_t>(&CasterValue) : reinterpret_cast<std::uintptr_t>(&OwnerValue);
			const auto original = site.jump ? trampoline.write_branch<5>(site.address, hook) : trampoline.write_call<5>(site.address, hook);
			g_original = reinterpret_cast<Handler>(original);
			logger::info("Magic tuning: entry point {} hooked at {:X}", site.entryPoint, site.address - base);
		}
		g_installed = true;
	}

	void InstallPlayerHooks()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		// AsMagicTarget, not static_cast: the MagicTarget base sits at a different offset on SE and AE.
		auto vtable = *reinterpret_cast<std::uintptr_t**>(player->AsMagicTarget());
		const auto& image = REL::Module::get();
		const auto address = reinterpret_cast<std::uintptr_t>(vtable);
		if (address < image.base() || address >= image.base() + image.segment(REL::Segment::data).offset() + image.segment(REL::Segment::data).size()) {
			logger::warn("Magic tuning: player MagicTarget vtable {:X} is outside the game, not hooked", address);
			return;
		}
		DWORD oldProtect;
		VirtualProtect(&vtable[1], sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
		AddTargetHook::original = reinterpret_cast<decltype(AddTargetHook::original)>(vtable[1]);
		vtable[1] = reinterpret_cast<std::uintptr_t>(&AddTargetHook::Thunk);
		VirtualProtect(&vtable[1], sizeof(std::uintptr_t), oldProtect, &oldProtect);
		logger::info("Magic tuning: player AddTarget hooked");
	}

	bool IsInstalled() { return g_installed; }
}

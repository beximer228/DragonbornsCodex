#include "Health.h"

#include "Cheats.h"

// "Бесконечное здоровье" used to only top health up every tick, and a hit, trap or fall bigger than the whole bar
// killed the player before the next top-up. Damage that goes through the player's ActorValueOwner::ModActorValue
// (slot 6: console damageav, some scripts) with the damage modifier and a negative value is dropped; weapon hits,
// damage spells and falls go through Actor::DoDamage instead (enemies killed the player with one blow even with the
// switch on), so its eight callers on 1.6.1170 are hooked too and skip it for the player.
namespace Health
{
	namespace
	{
		struct ModActorValueHook
		{
			static void Thunk(RE::ActorValueOwner* a_this, RE::ACTOR_VALUE_MODIFIER a_modifier, RE::ActorValue a_value, float a_amount)
			{
				if (a_value == RE::ActorValue::kHealth && a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && a_amount < 0.0f && Cheats::Get().infiniteHealth) {
					return;
				}
				original(a_this, a_modifier, a_value, a_amount);
			}
			static inline void (*original)(RE::ActorValueOwner*, RE::ACTOR_VALUE_MODIFIER, RE::ActorValue, float) = nullptr;
		};

		// Actor::DoDamage(health damage, source, don't adjust for difficulty) and its callers on 1.6.1170 (call, or a tail jump
		// at 666F43): ValueModifierEffect (damage spells), BSTaskPool hit task, PlayerCharacter, Actor::Update (2),
		// bhkCharacterMoveFinishEvent (falls), TESObjectREFR, Character.
		constexpr std::uintptr_t kDoDamage = 0x666D20;
		constexpr std::uintptr_t kDoDamageCallers[]{ 0x5D8CA2, 0x657475, 0x666F43, 0x66862E, 0x668DF7, 0x69F6C7, 0x6B8067, 0x6BAD34 };

		using DoDamage = bool (*)(RE::Actor*, float, RE::Actor*, bool);
		DoDamage g_doDamage = nullptr;

		bool DoDamageThunk(RE::Actor* a_this, float a_damage, RE::Actor* a_source, bool a_dontAdjust)
		{
			if (a_damage > 0.0f && a_this && a_this->IsPlayerRef() && Cheats::Get().infiniteHealth) {
				return false;
			}
			return g_doDamage(a_this, a_damage, a_source, a_dontAdjust);
		}

		bool CallsDoDamage(std::uintptr_t a_rva)
		{
			const auto addr = REL::Offset(a_rva).address();
			const auto op = *reinterpret_cast<const std::uint8_t*>(addr);
			return (op == 0xE8 || op == 0xE9) && addr + 5 + *reinterpret_cast<const std::int32_t*>(addr + 1) == REL::Offset(kDoDamage).address();
		}

		void InstallDoDamageHooks()
		{
			if (!std::ranges::all_of(kDoDamageCallers, CallsDoDamage)) {
				logger::warn("Health: the DoDamage calls are not where expected (another game version?), hits can still kill");
				return;
			}
			g_doDamage = reinterpret_cast<DoDamage>(REL::Offset(kDoDamage).address());
			auto& trampoline = SKSE::GetTrampoline();
			for (const auto rva : kDoDamageCallers) {
				const auto addr = REL::Offset(rva).address();
				if (*reinterpret_cast<const std::uint8_t*>(addr) == 0xE9) {
					trampoline.write_branch<5>(addr, DoDamageThunk);
				} else {
					trampoline.write_call<5>(addr, DoDamageThunk);
				}
			}
			logger::info("Health: DoDamage hooked at {} calls", std::size(kDoDamageCallers));
		}
	}

	void InstallPlayerHooks()
	{
		InstallDoDamageHooks();
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		// AsActorValueOwner, not static_cast: base offsets differ between SE and AE.
		auto        vtable = *reinterpret_cast<std::uintptr_t**>(player->AsActorValueOwner());
		const auto& image = REL::Module::get();
		const auto  address = reinterpret_cast<std::uintptr_t>(vtable);
		if (address < image.base() || address >= image.base() + image.segment(REL::Segment::data).offset() + image.segment(REL::Segment::data).size()) {
			logger::warn("Health: player ActorValueOwner vtable {:X} is outside the game, not hooked", address);
			return;
		}
		DWORD oldProtect;
		VirtualProtect(&vtable[6], sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
		ModActorValueHook::original = reinterpret_cast<decltype(ModActorValueHook::original)>(vtable[6]);
		vtable[6] = reinterpret_cast<std::uintptr_t>(&ModActorValueHook::Thunk);
		VirtualProtect(&vtable[6], sizeof(std::uintptr_t), oldProtect, &oldProtect);
		logger::info("Health: player ModActorValue hooked");
	}
}

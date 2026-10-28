#include "Enchanting.h"

#include "Cheats.h"

// More effects per enchanted item. The enchanting menu asks BGSEntryPoint::HandleEntryPoint for
// ModNumberAppliedEnchantmentsAllowed (80) once when it opens, with the player and a float that starts at 1
// ("Extra Effect" raises it to 2), and keeps the result in Selections::numEnchantmentsAllowed. That one call
// (RVA 90519D on 1.6.1170, `mov ecx, 50h` right before it) is hooked to raise the result to the menu's value.
namespace Enchanting
{
	namespace
	{
		constexpr std::uintptr_t kCall = 0x90519D;
		constexpr std::uintptr_t kHandleEntryPoint = 0x385F30;

		using Handler = void (*)(RE::BGSEntryPoint::ENTRY_POINT, RE::Actor*, float*);
		Handler g_original = nullptr;
		bool    g_installed = false;

		void EffectsAllowed(RE::BGSEntryPoint::ENTRY_POINT a_ep, RE::Actor* a_actor, float* a_value)
		{
			g_original(a_ep, a_actor, a_value);
			const float want = std::round(Cheats::Get().enchantEffects);
			if (a_value && a_actor && a_actor->IsPlayerRef() && want > *a_value) {
				*a_value = want;
			}
		}

		bool SiteMatches()
		{
			const auto addr = REL::Offset(kCall).address();
			const auto code = reinterpret_cast<const std::uint8_t*>(addr);
			if (code[0] != 0xE8 || code[-5] != 0xB9 || code[-4] != 0x50 || code[-3] != 0 || code[-2] != 0 || code[-1] != 0) {
				return false;
			}
			const auto rel = *reinterpret_cast<const std::int32_t*>(addr + 1);
			return addr + 5 + rel == REL::Offset(kHandleEntryPoint).address();
		}
	}

	void Install()
	{
		if (!SiteMatches()) {
			logger::warn("Enchanting: the effects-per-item call does not look as expected (another game version?), not hooked");
			return;
		}
		g_original = reinterpret_cast<Handler>(SKSE::GetTrampoline().write_call<5>(REL::Offset(kCall).address(), EffectsAllowed));
		g_installed = true;
		logger::info("Enchanting: effects-per-item hooked");
	}

	bool IsInstalled() { return g_installed; }
}

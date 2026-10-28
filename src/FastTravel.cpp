#include "FastTravel.h"

#include "Cheats.h"

// Fast travel from anywhere. Everything that blocks fast travel is checked in one function of the game,
// CanFastTravel(PlayerCharacter*, bool a_quiet) (RVA 731780 on 1.6.1170, found through the message settings it
// shows). Each check is a call whose result picks the message; those calls are hooked to answer "not blocked"
// while the matching switch is on:
//   +0A6 enemies nearby               +0E1 guards pursuing            +114 overencumbered
//   +141 taking damage                +19A jumping / falling
//   +221 cell is "can't travel from"  +232 worldspace is "can't travel from"
// The script ban (Game.EnableFastTravel(false), flag 0x10 at player+0xBE1) is a plain byte test, its jne at +174
// is turned into a jmp. The flying-mount check and the "default" refusals (in a scene, on a dragon...) stay.
namespace FastTravel
{
	namespace
	{
		constexpr std::uintptr_t kFunction = 0x731780;

		enum class Kind
		{
			kHurry,  // combat, weight, falling: safe to skip
			kBan     // places and quests that forbid it
		};

		struct Site
		{
			std::uint32_t offset;  // of the call in the function
			std::uint32_t target;  // RVA the original call goes to
			Kind          kind;
		};

		constexpr Site kSites[] = {
			{ 0x0A6, 0x772E10, Kind::kHurry },
			{ 0x0E1, 0x76BC70, Kind::kHurry },
			{ 0x114, 0x673500, Kind::kHurry },
			{ 0x141, 0x5C2D60, Kind::kHurry },
			{ 0x19A, 0x661DD0, Kind::kHurry },
			{ 0x221, 0x2B9B30, Kind::kBan },
			{ 0x232, 0x307510, Kind::kBan },
		};

		constexpr std::uint32_t kScriptBanJump = 0x174;  // 75 21: jne -> "allowed by scripts"

		bool g_installed = false;

		bool Skip(Kind a_kind)
		{
			const auto& st = Cheats::Get();
			return a_kind == Kind::kHurry ? st.fastTravelAnywhere : st.fastTravelIgnoreBans;
		}

		void SyncScriptBan()
		{
			const auto    addr = REL::Offset(kFunction + kScriptBanJump).address();
			const uint8_t want = Cheats::Get().fastTravelIgnoreBans ? 0xEB : 0x75;
			if (*reinterpret_cast<const std::uint8_t*>(addr) != want) {
				REL::safe_write(addr, want);
			}
		}

		// All hooked checks take integer / pointer arguments and return bool, so one forwarding shape fits them.
		using Check = bool (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);

		template <std::size_t I>
		struct Hook
		{
			static bool thunk(std::uintptr_t a_1, std::uintptr_t a_2, std::uintptr_t a_3, std::uintptr_t a_4)
			{
				if constexpr (I == 0) {
					SyncScriptBan();  // the first check runs before the script ban test
				}
				return Skip(kSites[I].kind) ? false : original(a_1, a_2, a_3, a_4);
			}
			static inline Check original = nullptr;
		};

		bool SiteMatches(const Site& a_site)
		{
			const auto addr = REL::Offset(kFunction + a_site.offset).address();
			if (*reinterpret_cast<const std::uint8_t*>(addr) != 0xE8) {
				return false;
			}
			const auto rel = *reinterpret_cast<const std::int32_t*>(addr + 1);
			return addr + 5 + rel == REL::Offset(a_site.target).address();
		}

		template <std::size_t... I>
		void Patch(std::index_sequence<I...>)
		{
			auto& trampoline = SKSE::GetTrampoline();
			((Hook<I>::original = reinterpret_cast<Check>(
				  trampoline.write_call<5>(REL::Offset(kFunction + kSites[I].offset).address(), Hook<I>::thunk))),
				...);
		}
	}

	void Install()
	{
		const auto jump = REL::Offset(kFunction + kScriptBanJump).address();
		const bool jumpOk = reinterpret_cast<const std::uint8_t*>(jump)[0] == 0x75 && reinterpret_cast<const std::uint8_t*>(jump)[1] == 0x21;
		if (!jumpOk || !std::ranges::all_of(kSites, SiteMatches)) {
			logger::warn("Fast travel: CanFastTravel does not look as expected (another game version?), not hooked");
			return;
		}
		Patch(std::make_index_sequence<std::size(kSites)>{});
		g_installed = true;
		logger::info("Fast travel: {} checks hooked", std::size(kSites));
	}

	bool IsInstalled() { return g_installed; }

	bool CanFastTravel(bool a_quiet)
	{
		using Fn = bool (*)(RE::PlayerCharacter*, bool);
		auto player = RE::PlayerCharacter::GetSingleton();
		return player && reinterpret_cast<Fn>(REL::Offset(kFunction).address())(player, a_quiet);
	}
}

#include "SkillCap.h"

#include "Cheats.h"

// Skill levels above 100 with normal leveling. Every way a skill gains experience (use, books, trainers,
// Game.AdvanceSkill/IncrementSkill) ends in PlayerSkills::AddExperience (RVA 77AE60 on 1.6.1170). It loads the cap
// once, `movss xmm10, [rip+100.0f]` at +76, and uses it twice: no experience at all when the base skill is at the
// cap, and the level-up loop stops there (the next threshold is set to 0). The 100.0f constant is shared with
// other code, so instead of changing it the instruction's displacement is pointed at our own float, which sits
// in trampoline memory near the game module.
namespace SkillCap
{
	namespace
	{
		constexpr std::uintptr_t kLoad = 0x77AED6;  // movss xmm10, dword ptr [rip+disp32]
		constexpr std::uintptr_t kCapConstant = 0x1AD2954;
		constexpr float          kGameCap = 100.0f;

		float* g_cap = nullptr;
		bool   g_installed = false;

		bool SiteMatches()
		{
			const auto addr = REL::Offset(kLoad).address();
			const auto code = reinterpret_cast<const std::uint8_t*>(addr);
			if (code[0] != 0xF3 || code[1] != 0x44 || code[2] != 0x0F || code[3] != 0x10 || code[4] != 0x15) {
				return false;
			}
			const auto disp = *reinterpret_cast<const std::int32_t*>(addr + 5);
			const auto target = addr + 9 + disp;
			return target == REL::Offset(kCapConstant).address() && *reinterpret_cast<const float*>(target) == kGameCap;
		}

		// A skill that reached the old cap was left with threshold 0, which would hand out a free level on the
		// next experience. Give it the threshold the game uses for that level.
		void FixThresholds(RE::PlayerCharacter* a_player, float a_cap)
		{
			auto skills = a_player->GetInfoRuntimeData().skills;
			auto curve = RE::GameSettingCollection::GetSingleton()->GetSetting("fSkillUseCurve");
			if (!skills || !skills->data || !curve) {
				return;
			}
			auto avo = a_player->AsActorValueOwner();
			for (std::uint32_t i = 0; i < RE::PlayerCharacter::PlayerSkills::Data::Skill::kTotal; ++i) {
				const auto av = static_cast<RE::ActorValue>(std::to_underlying(RE::ActorValue::kOneHanded) + i);
				auto&      skill = skills->data->skills[i];
				auto       info = RE::ActorValueList::GetActorValueInfo(av);
				const float level = avo->GetBaseActorValue(av);
				if (skill.levelThreshold <= 0.0f && level >= kGameCap && level < a_cap && info && info->skill) {
					skill.levelThreshold = std::pow(level, curve->GetFloat()) * info->skill->improveMult + info->skill->improveOffset;
				}
			}
		}
	}

	void Install()
	{
		if (!SiteMatches()) {
			logger::warn("SkillCap: the skill cap load does not look as expected (another game version?), not patched");
			return;
		}
		const auto addr = REL::Offset(kLoad).address();
		g_cap = static_cast<float*>(SKSE::GetTrampoline().allocate(sizeof(float)));
		const auto disp = reinterpret_cast<std::intptr_t>(g_cap) - static_cast<std::intptr_t>(addr + 9);
		if (disp < INT32_MIN || disp > INT32_MAX) {
			logger::warn("SkillCap: trampoline memory is too far from the game code, not patched");
			return;
		}
		*g_cap = kGameCap;
		REL::safe_write(addr + 5, static_cast<std::int32_t>(disp));
		g_installed = true;
		logger::info("SkillCap: skill cap patched");
	}

	bool IsInstalled() { return g_installed; }

	void Apply()
	{
		if (!g_installed) {
			return;
		}
		const float cap = std::max(kGameCap, std::round(Cheats::Get().skillCap));
		*g_cap = cap;
		if (cap > kGameCap) {
			if (auto player = RE::PlayerCharacter::GetSingleton()) {
				FixThresholds(player, cap);
			}
		}
	}
}

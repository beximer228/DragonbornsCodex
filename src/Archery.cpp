#include "Archery.h"

#include "Cheats.h"

// Fast bow draw for the player only. The draw power of a shot (0..1: damage and arrow speed) comes from one
// function, power(drawTime, drawSpeed) = minPower + (t - fArrowBowMinTime) / (fBowDrawTime - fArrowBowMinTime) x
// (1 - minPower) (RVA 418770 on 1.6.1170). It has two callers and both pass the player's own draw timer: the
// aiming check (RVA 763352) and the arrow launch, which stores the result as the projectile's power (RVA 7E4C96).
// Those calls answer 1 while the switch is on; the game settings, shared with NPC archers, stay untouched.
//
// The player's arrows also fly dead straight: gravity comes from the projectile's base form (shared with NPC
// arrows), so instead each of the player's arrows in flight gets its launch velocity back every frame until it
// hits something.
//
// Aimbot: every projectile the player launches goes through Projectile::Launch (RVA 7E46C0), called for bows and
// crossbows from TESObjectWEAP::Fire (RVA 287AF0) and for spells, staves and shouts from ActorMagicCaster (RVA
// 5C00B4). At those calls a living actor near the line of the shot is picked (in sight of the player, not a
// follower or summon): an enemy or a wild creature within kAimCone of the nearest edge of its body, or, with none
// there, a peaceful person, horse, farm animal or pet only within the narrow kAimConePeaceful, so a shot at a wolf
// does not swerve into the guard or the cow next to it. The shot is launched straight at it (a person's head, a
// creature's body centre): that alone serves beams (lightning) and flames. Spells count only when one of their
// effects is marked hostile (user: heals, calm, summons are not shots). Arrows, bolts and missile spells (firebolt,
// ice spike, fireball) are then also steered every frame until they hit, so moving targets are hit too.
namespace Archery
{
	namespace
	{
		constexpr std::uintptr_t kPowerFunction = 0x418770;
		constexpr std::uintptr_t kCallers[] = { 0x763352, 0x7E4C96 };

		using Power = float (*)(float, float);
		Power g_original = nullptr;
		bool  g_installed = false;

		constexpr std::uintptr_t kLaunchFunction = 0x7E46C0;
		constexpr std::uintptr_t kLaunchCallers[] = { 0x287AF0, 0x5C00B4 };

		using Launch = RE::ProjectileHandle* (*)(RE::ProjectileHandle*, RE::Projectile::LaunchData&);
		Launch g_launch = nullptr;
		bool   g_launchHooked = false;

		std::atomic<std::chrono::steady_clock::rep>     g_lastShot{};  // projectiles are looked for only shortly after
		std::unordered_map<RE::FormID, RE::NiPoint3>    g_launchVelocity;
		std::unordered_map<RE::FormID, RE::ActorHandle> g_targets;  // aimbot: projectile -> actor it flies at

		// Launches may come from animation threads; the frame task takes them over from here.
		using UsesTarget = bool (*)(RE::Projectile*);
		UsesTarget g_flameUsesTarget = nullptr;

		std::mutex                                                    g_pendingLock;
		std::vector<std::pair<RE::ProjectileHandle, RE::ActorHandle>> g_pending;

		constexpr float kAimCone = 0.906f;          // cos 25°: how far off the crosshair an enemy may be
		constexpr float kAimConePeaceful = 0.9945f;  // cos 6°: guards, townsfolk, livestock, pets that are not attacking
		constexpr float kAimRange = 15000.0f;   // ~210 m

		float DrawPower(float a_drawTime, float a_drawSpeed)
		{
			const auto& st = Cheats::Get();
			if (st.fastBowDraw || st.aimbot) {
				g_lastShot = std::chrono::steady_clock::now().time_since_epoch().count();
			}
			return st.fastBowDraw ? 1.0f : g_original(a_drawTime, a_drawSpeed);
		}

		bool IsPerson(RE::Actor* a_actor)
		{
			static const auto npc = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
			return npc && a_actor->HasKeyword(npc);
		}

		// People, horses, livestock and pets: they get only the narrow cone while they are not attacking. Cows,
		// chickens and dogs are told by race; goats are not (Gleda in Riverwood is GoatRace like a mountain goat),
		// so any animal inside a town, village or farm (a location with LocTypeHabitation, or one inside such)
		// counts as livestock, as does one in a crime faction (killing it is a crime).
		bool IsTame(RE::Actor* a_actor)
		{
			static const auto npc = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeNPC");
			static const auto horse = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeHorse");
			if ((npc && a_actor->HasKeyword(npc)) || (horse && a_actor->HasKeyword(horse)) || a_actor->GetCrimeFaction()) {
				return true;
			}
			const auto race = a_actor->GetRace();
			const std::string_view id = race ? race->GetFormEditorID() : "";
			if (std::ranges::any_of(std::array{ "Cow", "Chicken", "Dog", "Husky" }, [&](std::string_view a_part) { return id.find(a_part) != std::string_view::npos; })) {
				return true;
			}
			static const auto habitation = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeHabitation");
			for (auto loc = a_actor->GetCurrentLocation(); habitation && loc; loc = loc->parentLoc) {
				if (loc->HasKeyword(habitation)) {
					return true;
				}
			}
			return false;
		}

		// People are shot in the head. Creatures in the middle of their body: their skeletons have an
		// "NPC Head" node as well, but on a giant frostbite spider it sits at floor level in front of the body,
		// and arrows sent there went into the ground (user: missed a big wounded spider in Bleak Falls Barrow).
		// Creature skeletons name their spine differently ("Mammoth Spine 3", "Horker_Spine2", "NPC Spine2 [Spn2]"):
		// the middle one of the nodes with "spine" in the name, found once per race; none (spiders) = "".
		std::string TorsoNode(RE::Actor* a_actor, RE::NiAVObject* a_root)
		{
			static std::mutex                                     lock;
			static std::unordered_map<RE::TESRace*, std::string> cache;
			const auto                                            race = a_actor->GetRace();
			std::scoped_lock                                      guard{ lock };
			if (auto found = cache.find(race); found != cache.end()) {
				return found->second;
			}
			std::vector<std::string> spines;
			RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_obj) {
				std::string name = a_obj->name.empty() ? "" : a_obj->name.c_str();
				std::string lower = name;
				std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (a_obj->AsNode() && lower.find("spine") != std::string::npos) {
					spines.push_back(name);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			std::ranges::sort(spines);
			return cache[race] = spines.empty() ? std::string{} : spines[spines.size() / 2];
		}

		RE::NiPoint3 AimPoint(RE::Actor* a_actor)
		{
			if (auto root = a_actor->Get3D()) {
				// a dwarven centurion's bounds reach far above its head (its centre was 290 units up): the spine first
				if (!IsPerson(a_actor)) {
					const auto feet = a_actor->GetPosition().z;
					const auto height = std::max(a_actor->GetHeight(), 30.0f);
					if (const auto torso = TorsoNode(a_actor, root); !torso.empty()) {
						if (auto node = root->GetObjectByName(torso); node && node->world.translate.z > feet && node->world.translate.z < feet + height * 1.2f) {
							return node->world.translate;
						}
					}
					if (root->worldBound.radius > 0.0f) {
						auto centre = root->worldBound.center;
						centre.z = std::clamp(centre.z, feet + height * 0.2f, feet + height * 0.8f);
						return centre;
					}
				}
				static const RE::BSFixedString head{ "NPC Head [Head]" }, spine{ "NPC Spine2 [Spn2]" };
				for (const auto& name : { head, spine }) {
					if (auto node = root->GetObjectByName(name)) {
						return node->world.translate;
					}
				}
			}
			auto point = a_actor->GetPosition();
			point.z += a_actor->GetHeight() * 0.6f;
			return point;
		}

		// Model size as seen from the shot: the arrow's line may pass this close to the aim point and still be on the body.
		float BodyRadius(RE::Actor* a_actor)
		{
			auto root = a_actor->Get3D();
			return root ? root->worldBound.radius * 0.6f : a_actor->GetHeight() * 0.3f;
		}

		// Nothing solid between the shot and the actor: a physics ray with the player's collision filter (it stops on
		// the actor's own body, so anything no nearer than its bounds counts). The game's HasLineOfSight is a cached
		// detection answer: for a creature that just came into view it said no at 9 m in the open.
		bool ClearShot(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, RE::Actor* a_actor, const RE::NiPoint3& a_to)
		{
			auto cell = a_player->GetParentCell();
			auto world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				bool unused = false;
				return a_player->HasLineOfSight(a_actor, unused);
			}
			const float     scale = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.0f);
			pick.rayInput.to = RE::hkVector4(a_to.x * scale, a_to.y * scale, a_to.z * scale, 0.0f);
			a_player->GetCollisionFilterInfo(pick.rayInput.filterInfo);
			{
				RE::BSReadLockGuard lock(world->worldLock);
				world->PickObject(pick);
			}
			if (!pick.rayOutput.HasHit()) {
				return true;
			}
			auto        root = a_actor->Get3D();
			const float body = root ? root->worldBound.radius : a_actor->GetHeight();
			const float distance = a_from.GetDistance(a_to);
			return distance * pick.rayOutput.hitFraction >= distance - body;
		}

		RE::ActorHandle PickTarget(RE::PlayerCharacter* a_player, const RE::NiPoint3& a_from, const RE::NiPoint3& a_velocity)
		{
			auto direction = a_velocity;
			direction.Unitize();
			RE::Actor* enemy = nullptr;
			RE::Actor* other = nullptr;
			float      enemyCos = kAimCone, otherCos = kAimConePeaceful;
			RE::ProcessLists::GetSingleton()->ForEachHighActor([&](RE::Actor* a_actor) {
				if (!a_actor || a_actor == a_player || a_actor->IsDead() || a_actor->IsPlayerTeammate() || a_actor->IsCommandedActor() || !a_actor->Is3DLoaded()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto to = AimPoint(a_actor) - a_from;
				const float distance = to.Length();
				if (distance < 1.0f || distance > kAimRange) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				// angle to the nearest edge of the body, not its centre: a huge creature is easy to hit off-centre
				const float centre = std::acos(std::clamp(to.Dot(direction) / distance, -1.0f, 1.0f));
				const float edge = std::asin(std::min(1.0f, BodyRadius(a_actor) / distance));
				const float cos = std::cos(std::max(0.0f, centre - edge));
				// wild creatures count as prey even before they attack (a wounded spider has aggression 0);
				// people, livestock and pets that are not hostile only when aimed right at
				// a horse hit by a stray shot is "in combat" with the player but only runs: it is no enemy (it took a crossbow
				// bolt meant for an atronach in a test)
				static const auto horse = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("ActorTypeHorse");
				const bool        fighting = a_actor->GetActorRuntimeData().currentCombatTarget.get().get() == a_player && !(horse && a_actor->HasKeyword(horse));
				const bool        hostile = a_actor->IsHostileToActor(a_player) || fighting || !IsTame(a_actor);
				float&     bestCos = hostile ? enemyCos : otherCos;
				if (cos > bestCos && ClearShot(a_player, a_from, a_actor, AimPoint(a_actor))) {
					(hostile ? enemy : other) = a_actor;
					bestCos = cos;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			auto best = enemy ? enemy : other;
			return best ? best->GetHandle() : RE::ActorHandle{};
		}

		bool IsHostileSpell(RE::MagicItem* a_spell)
		{
			return std::ranges::any_of(a_spell->effects, [](RE::Effect* a_effect) { return a_effect && a_effect->baseEffect && a_effect->baseEffect->IsHostile(); });
		}

		RE::ProjectileHandle* LaunchProjectile(RE::ProjectileHandle* a_result, RE::Projectile::LaunchData& a_data)
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			auto base = a_data.projectileBase;
			if (!Cheats::Get().aimbot || !player || a_data.shooter != player || !base || base->IsGrenade() || base->IsBarrier() ||
				(a_data.spell && !IsHostileSpell(a_data.spell))) {
				return g_launch(a_result, a_data);  // runes and walls are put down, not shot
			}
			const float        flat = std::cos(a_data.angleX);
			const RE::NiPoint3 direction{ std::sin(a_data.angleZ) * flat, std::cos(a_data.angleZ) * flat, -std::sin(a_data.angleX) };
			const auto         target = PickTarget(player, a_data.origin, direction).get();
			if (target) {
				auto to = AimPoint(target.get()) - a_data.origin;
				to.Unitize();
				a_data.angleX = -std::asin(std::clamp(to.z, -1.0f, 1.0f));
				a_data.angleZ = std::atan2(to.x, to.y);
				a_data.desiredTarget = target.get();
				a_data.autoAim = false;  // the game's own auto-aim would turn it to its pick
			}
			auto result = g_launch(a_result, a_data);
			g_lastShot = std::chrono::steady_clock::now().time_since_epoch().count();
			if (!base->IsFlamethrower()) {  // flames launch many times a second
				logger::info("Aimbot: {} -> {}", a_data.spell ? a_data.spell->GetName() : "arrow",
					target ? std::format("{:08X} {}", target->GetFormID(), target->GetName()) : "no target");
			}
			if (result && target && (base->IsMissile() || base->IsArrow())) {
				std::scoped_lock lock{ g_pendingLock };
				g_pending.emplace_back(*result, target->GetHandle());
			}
			return result;
		}

		// Flames follow the caster's hand and turn to the target set at launch only when this virtual answers yes
		// (fenix31415's NewProjectiles hooks the same one for homing flames): the player's do while it is alive and in reach.
		bool FlameUsesTarget(RE::Projectile* a_flame)
		{
			const auto& data = a_flame->GetProjectileRuntimeData();
			if (Cheats::Get().aimbot && data.shooter.get().get() == RE::PlayerCharacter::GetSingleton()) {
				auto target = data.desiredTarget.get();
				auto actor = target ? target->As<RE::Actor>() : nullptr;
				auto base = a_flame->GetProjectileBase();
				if (actor && !actor->IsDead() && base && actor->GetPosition().GetDistance(a_flame->GetPosition()) < base->data.range + BodyRadius(actor)) {
					return true;
				}
			}
			return g_flameUsesTarget(a_flame);
		}

		void KeepArrowsStraight()
		{
			auto player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Is3DLoaded()) {
				g_launchVelocity.clear();
				g_targets.clear();
				return;
			}
			{
				std::scoped_lock lock{ g_pendingLock };
				for (const auto& [projectile, target] : g_pending) {
					if (auto ref = projectile.get()) {
						g_targets[ref->GetFormID()] = target;
					}
				}
				g_pending.clear();
			}
			const auto&                    st = Cheats::Get();
			std::unordered_set<RE::FormID> seen;
			RE::TES::GetSingleton()->ForEachReferenceInRange(player, 60000.0f, [&](RE::TESObjectREFR* a_ref) {
				auto missile = a_ref ? a_ref->As<RE::MissileProjectile>() : nullptr;  // arrows and bolts are missiles too
				if (!missile) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto& data = missile->GetProjectileRuntimeData();
				if (data.shooter.get().get() == player && !data.impacts.empty()) {
					if (auto aimed = g_targets.find(a_ref->GetFormID()); aimed != g_targets.end()) {  // what an aimed shot really hit
						auto hit = data.impacts.front()->collidee.get();
						auto pos = a_ref->GetPosition();
						logger::info("Aimbot: {:08X} hit {} at {:.0f} {:.0f} {:.0f} after {:.2f} s", a_ref->GetFormID(),
							hit ? std::format("{:08X} {}", hit->GetFormID(), hit->GetName()) : "ground/static", pos.x, pos.y, pos.z, data.livingTime);
						g_targets.erase(aimed);
					}
				}
				if (data.shooter.get().get() != player || data.livingTime > 10.0f || !data.impacts.empty()) {
					return RE::BSContainer::ForEachResult::kContinue;  // someone else's, old, or it has hit something
				}
				const bool arrow = a_ref->As<RE::ArrowProjectile>() != nullptr;
				const auto id = a_ref->GetFormID();
				seen.insert(id);
				auto [it, added] = g_launchVelocity.try_emplace(id, data.linearVelocity);
				if (it->second.Length() < 100.0f) {  // caught before it got its speed: take it on the next frame
					it->second = data.linearVelocity;
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto velocity = it->second;
				bool steered = false;
				if (st.aimbot) {
					if (arrow && !g_launchHooked && !g_targets.contains(id)) {  // launch not hooked (another game version): pick now
						g_targets[id] = PickTarget(player, a_ref->GetPosition(), velocity);
						auto picked = g_targets[id].get();
						logger::info("Aimbot: arrow {:08X} -> {}", id, picked ? std::format("{:08X} {}", picked->GetFormID(), picked->GetName()) : "no target");
					}
					auto found = g_targets.find(id);
					auto target = found != g_targets.end() ? found->second.get() : nullptr;
					if (target && !target->IsDead()) {
						auto aim = AimPoint(target.get());
						auto to = aim - a_ref->GetPosition();
						// a mudcrab or skeever is aimed at a hand's width above the ground: on a flat approach any bump
						// of the terrain on the way caught the arrow, so it comes in from above and drops onto it
						if (aim.z - target->GetPosition().z < 60.0f) {
							to.z += std::min(std::sqrt(to.x * to.x + to.y * to.y) * 0.2f, 150.0f);
						}
						if (to.Length() > 1.0f) {
							to.Unitize();
							velocity = to * it->second.Length();
							steered = true;
						}
					}
				}
				// arrows fly straight with either switch; spells keep their own flight unless steered
				if (!steered && !(arrow && (st.fastBowDraw || st.aimbot))) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				data.linearVelocity = velocity;
				data.velocity = velocity;
				data.range = std::max(data.range, 100000.0f);
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::erase_if(g_launchVelocity, [&](const auto& a_entry) { return !seen.contains(a_entry.first); });
			std::erase_if(g_targets, [&](const auto& a_entry) { return !seen.contains(a_entry.first); });
		}

		bool Calls(std::uintptr_t a_rva, std::uintptr_t a_function)
		{
			const auto addr = REL::Offset(a_rva).address();
			if (*reinterpret_cast<const std::uint8_t*>(addr) != 0xE8) {
				return false;
			}
			return addr + 5 + *reinterpret_cast<const std::int32_t*>(addr + 1) == REL::Offset(a_function).address();
		}
	}

	void Install()
	{
		auto& trampoline = SKSE::GetTrampoline();
		if (std::ranges::all_of(kLaunchCallers, [](auto a_rva) { return Calls(a_rva, kLaunchFunction); })) {
			for (const auto rva : kLaunchCallers) {
				g_launch = reinterpret_cast<Launch>(trampoline.write_call<5>(REL::Offset(rva).address(), LaunchProjectile));
			}
			g_launchHooked = true;
			REL::Relocation<std::uintptr_t> flameVtable{ RE::VTABLE_FlameProjectile[0] };
			g_flameUsesTarget = reinterpret_cast<UsesTarget>(flameVtable.write_vfunc(0xC1, FlameUsesTarget));
			logger::info("Archery: projectile launch hooked");
		} else {
			logger::warn("Archery: the projectile launch calls are not where expected (another game version?), the aimbot serves arrows only");
		}
		if (!std::ranges::all_of(kCallers, [](auto a_rva) { return Calls(a_rva, kPowerFunction); })) {
			logger::warn("Archery: the draw power calls are not where expected (another game version?), fast draw is off");
			return;
		}
		for (const auto rva : kCallers) {
			g_original = reinterpret_cast<Power>(trampoline.write_call<5>(REL::Offset(rva).address(), DrawPower));
		}
		g_installed = true;
		logger::info("Archery: draw power hooked");
	}

	bool IsInstalled() { return g_installed; }

	std::string DebugAim(RE::Actor* a_actor)
	{
		auto        pos = a_actor->GetPosition();
		auto        aim = AimPoint(a_actor);
		auto        root = a_actor->Get3D();
		std::string nodes;
		if (root) {
			RE::BSVisit::TraverseScenegraphObjects(root, [&](RE::NiAVObject* a_obj) {
				const std::string name = a_obj->name.empty() ? "" : a_obj->name.c_str();
				if (name.find("Head") != std::string::npos || name.find("Spine") != std::string::npos || name.find("Pelvis") != std::string::npos) {
					const auto& t = a_obj->world.translate;
					nodes += std::format("{}\"{}\":[{:.0f},{:.0f},{:.0f}]", nodes.empty() ? "" : ",", name, t.x, t.y, t.z);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		const auto& bound = root ? root->worldBound : RE::NiBound{};
		return std::format("{{\"pos\":[{:.0f},{:.0f},{:.0f}],\"height\":{:.0f},\"scale\":{:.2f},\"aim\":[{:.0f},{:.0f},{:.0f}],\"bound\":[{:.0f},{:.0f},{:.0f},{:.0f}],\"tame\":{},\"race\":\"{}\",\"nodes\":{{{}}}}}",
			pos.x, pos.y, pos.z, a_actor->GetHeight(), a_actor->GetScale(), aim.x, aim.y, aim.z, bound.center.x, bound.center.y, bound.center.z, bound.radius, IsTame(a_actor), a_actor->GetRace() ? a_actor->GetRace()->GetFormEditorID() : "", nodes);
	}

	void Update()
	{
		// Only while the player's arrows may still be flying (a few seconds after the last shot).
		const std::chrono::steady_clock::time_point lastShot{ std::chrono::steady_clock::duration{ g_lastShot.load() } };
		if ((!g_installed && !g_launchHooked) || (g_launchVelocity.empty() && std::chrono::steady_clock::now() - lastShot > std::chrono::seconds(10))) {
			return;
		}
		SKSE::GetTaskInterface()->AddTask(KeepArrowsStraight);
	}
}

#include "Puzzles.h"

#include "Lang.h"
#include "Menu.h"
#include "Text.h"

// How the vanilla puzzles work (scripts disassembled from Skyrim - Misc.bsa):
// - Claw doors: the keyhole runs HallOfStoriesKeyholeScript (every claw door; Snowveil Sanctum has its own
//   SnowveilKeyholeScript). Its properties largeRing/mediumRing/smallRing are the rings, each running
//   HallOfStoriesDiskScript: states position01..03, every activation by the player turns it one step and keeps its
//   ringSolved and the keyhole's numRingsSolved up to date. Only with the door's claw (myMiscObject) in the
//   inventory (user: the claw is still needed): the rings are turned, then the keyhole is activated by the
//   Dragonborn, the game's own claw check, insert animation, door and quest stage. Snowveil's keyhole never opens for
//   the player's activation, so its openDoor is called instead.
// - Pillars (DefaultPuzzlePillarScript and its variants in Bleak Falls Barrow, Skuldafn, Hall of the Dead...): the
//   same three positions and a pillarSolved variable; their linked reference is the lever (or control) whose
//   doorOrDarts opens the gate when numPillarsSolved reaches pillarCount and fires the darts otherwise.
//   Variants: Halldir's Cairn (the control is a rotating door with no doorOrDarts: activated once the pillars are right)
//   and Shroud Hearth (spinners; the controller is activated too); Skuldafn (one two-stage pillar picks the gate:
//   localState solveState1 opens refActOnSuccess01, solveState2 the other one, so the first is aimed at until it is
//   open); Karthspire (Akaviri pillars with no solved flag: position 2 is right on all of them, and the bridge comes
//   down by itself after the last turn); Kolbjorn Barrow (two pillars with pillarState: both 1 open the first gate,
//   both 2 the second, like Skuldafn).
// - Raldbthar: jammed gears. Each blocker frees a chain of gears (gear -> gear -> pillar), two chains light a pillar,
//   two lit pillars open the button, which opens the door when pressed. A gear only turns for a blocker or another
//   gear, so the blockers are activated.
// - Volskygge: buttons pressed in the order of their buttonNumber (a wrong one resets the count and fires the
//   trap); the last one opens the doors itself.
// - Folgunthur's catacombs: four levers toggling four gates; the fewest levers that open them all are pulled.
// - Mzulft's oculory (MG06, once the Focusing Crystal is in): the armillary turns with frost (up) and flame (down) to
//   position 4, each dome turns through eight positions to its SolveState (DomeReady); the quest goes to stage 55.
// - Arkngthamz's resonators (dlc01dundbchimescript): struck (OnHit, they ignore activation) in ChimeNumber order; the
//   master counts NextCorrectChime once a resonator's opening animation ends, so the next one waits for that (a wrong
//   one sends spiders, spheres, ballistae). The fifth opens the way, DLC1LD_Arkngthamz stage 90.
// - Fahlbtharz's steam resonators: switching one on adds its SteamCharge, off takes it away; exactly
//   SucessfulSteamCharge (20) opens the door, more vents steam, resets them all and springs a trap, below 0 is cut to
//   0. The fewest switches are pulled in an order that keeps the charge within 0..20.
// Pieces are turned by activating them as the player would (the real animation, the scripts do their counting), one
// at a time, each finishing its turn first; then the door or lever function is called directly, so no trap fires.
namespace Puzzles
{
	namespace
	{
		using ObjectPtr = RE::BSTSmartPointer<RE::BSScript::Object>;

		struct Piece
		{
			RE::ObjectRefHandle ref;
			int                 presses = 0;
		};

		enum class Kind
		{
			kClaw,
			kPillars,
			kGears,    // Raldbthar
			kButtons,  // Volskygge, pressed in order
			kLevers,   // Folgunthur's catacomb levers
			kDomes,    // Mzulft's oculory
			kChimes,   // Arkngthamz's resonators, struck in order
			kSteam,    // Fahlbtharz's steam resonators
		};

		struct Job
		{
			Kind                kind = Kind::kPillars;
			RE::ObjectRefHandle control;  // keyhole, lever, button
			std::vector<Piece>  pieces;
			int                 waits = 0;  // steps spent waiting for the control after the last piece
		};

		constexpr float kRange = 6000.0f;  // the room the Dragonborn is in, roughly
		constexpr int   kMaxPresses = 8;   // up to eight positions (Mzulft's domes): more means the pieces turn each other
		constexpr auto  kTurnTime = 1500ms;

		std::mutex                            g_lock;
		std::vector<Job>                      g_jobs;
		std::chrono::steady_clock::time_point g_next{};
		std::atomic<bool>                     g_stepQueued{ false };

		std::mutex                            g_marksLock;  // the ESP's view, see Marks()
		std::vector<Mark>                     g_marks;
		float                                 g_marksRange = 0.0f;
		std::chrono::steady_clock::time_point g_marksWanted{};
		std::chrono::steady_clock::time_point g_marksScanned{};
		std::atomic<bool>                     g_marksQueued{ false };

		std::vector<ObjectPtr> Scripts(RE::TESObjectREFR* a_ref)
		{
			std::vector<ObjectPtr> result;
			auto                   vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto                   policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!a_ref || !policy) {
				return result;
			}
			const auto          handle = policy->GetHandleForObject(a_ref->GetFormType(), a_ref);
			RE::BSSpinLockGuard locker(vm->attachedScriptsLock);
			if (auto it = vm->attachedScripts.find(handle); it != vm->attachedScripts.end()) {
				for (const auto& script : it->second) {
					if (ObjectPtr object{ script.get() }) {
						result.push_back(object);
					}
				}
			}
			return result;
		}

		std::string Name(const ObjectPtr& a_script)
		{
			auto type = a_script->GetTypeInfo();
			return type ? Text::ToLower(type->GetName()) : std::string{};
		}

		bool Flag(const ObjectPtr& a_script, const char* a_variable)
		{
			auto var = a_script ? a_script->GetVariable(a_variable) : nullptr;
			return var && var->IsBool() && var->GetBool();
		}

		RE::TESObjectREFR* RefVariable(const ObjectPtr& a_script, const char* a_variable)
		{
			auto var = a_script ? a_script->GetVariable(a_variable) : nullptr;
			return var && var->IsObject() ? var->Unpack<RE::TESObjectREFR*>() : nullptr;
		}

		std::string State(const ObjectPtr& a_script)
		{
			return a_script ? Text::ToLower(a_script->currentState.c_str()) : std::string{};
		}

		bool Busy(const ObjectPtr& a_script)
		{
			const auto state = State(a_script);
			// Folgunthur's lever puzzle while a lever moves, Fahlbtharz's controller while the steam flows
			return state == "busy" || state == "waitingforlever" || Flag(a_script, "::isBusy_var");
		}

		bool HasFunction(const ObjectPtr& a_script, std::string_view a_function)
		{
			auto type = a_script ? a_script->GetTypeInfo() : nullptr;
			for (; type; type = type->GetParent()) {
				auto funcs = type->GetMemberFuncIter();
				for (std::uint32_t i = 0; funcs && i < type->GetNumMemberFuncs(); ++i) {
					if (funcs[i].func && Text::ToLower(funcs[i].func->GetName().c_str()) == Text::ToLower(std::string{ a_function })) {
						return true;
					}
				}
			}
			return false;
		}

		bool IsOpen(RE::TESObjectREFR* a_ref)
		{
			using State = RE::BGSOpenCloseForm::OPEN_STATE;
			const auto state = a_ref ? RE::BGSOpenCloseForm::GetOpenState(a_ref) : State::kNone;
			return state == State::kOpen || state == State::kOpening;
		}

		int Int(const ObjectPtr& a_script, const char* a_variable)
		{
			auto var = a_script ? a_script->GetVariable(a_variable) : nullptr;
			return var && var->IsInt() ? var->GetSInt() : -1;
		}

		// The script of a ring or pillar: a solved flag, Karthspire's Akaviri pillar, Kolbjorn's pillar, Raldbthar's
		// gear blocker or Volskygge's button. Shroud Hearth's spinners also carry a stray, unlinked copy of Halldir's
		// pillar script whose flag is always set: one without a linked reference does not count.
		ObjectPtr PieceScript(RE::TESObjectREFR* a_ref)
		{
			for (auto& script : Scripts(a_ref)) {
				auto linked = script->GetVariable("myLinkedRef");
				if (linked && !(linked->IsObject() && linked->Unpack<RE::TESObjectREFR*>())) {
					continue;
				}
				const auto name = Name(script);
				if (script->GetVariable("ringSolved") || script->GetVariable("pillarSolved") || script->GetVariable("::solution1_var") ||
					script->GetVariable("::buttonNumber_var") || name == "dunkarthspirepillarpuzzle" || name == "dunraldbtharpuzzlegearblockerscript" ||
					name == "mg06dome01script" || script->GetVariable("::ChimeNumber_var") || script->GetVariable("::SteamCharge_var")) {
					return script;
				}
			}
			return {};
		}

		// The script of what opens up: keyhole, lever, rotating door, controller, Karthspire's puzzle master,
		// Raldbthar's button, Volskygge's button control.
		ObjectPtr ControlScript(RE::TESObjectREFR* a_ref)
		{
			for (auto& script : Scripts(a_ref)) {
				const auto name = Name(script);
				if (name == "hallofstorieskeyholescript" || name == "snowveilkeyholescript" || name == "dunkarthspirepuzzlemaster" ||
					name == "dlc2dunkolbjornpillarpuzzlelever" || name == "dunraldbtharpuzzlebuttonscript" || name == "dunfolgunthurcataleverpuzzle" ||
					name == "mg06armillaryscript" || name == "dlc01dundbchimemasterscript" ||
					script->GetVariable("::numPillarsSolved_var") || script->GetVariable("::numPuzzleButtonsSolved_var") ||
					script->GetVariable("::SucessfulSteamCharge_var")) {
					return script;
				}
			}
			return {};
		}

		Kind KindOf(const ObjectPtr& a_control)
		{
			if (a_control && Name(a_control) == "dunraldbtharpuzzlebuttonscript") {
				return Kind::kGears;
			}
			if (a_control && Name(a_control) == "dunfolgunthurcataleverpuzzle") {
				return Kind::kLevers;
			}
			if (a_control && Name(a_control) == "mg06armillaryscript") {
				return Kind::kDomes;
			}
			if (a_control && Name(a_control) == "dlc01dundbchimemasterscript") {
				return Kind::kChimes;
			}
			if (a_control && a_control->GetVariable("::SucessfulSteamCharge_var")) {
				return Kind::kSteam;
			}
			return a_control && a_control->GetVariable("::numPuzzleButtonsSolved_var") ? Kind::kButtons : Kind::kPillars;
		}

		// Skuldafn and Kolbjorn: one solution opens the first gate, the other one the second.
		bool TwoGates(const ObjectPtr& a_control)
		{
			const auto name = a_control ? Name(a_control) : std::string{};
			return name == "dunskuldafnpuzzlecontrolscript" || name == "dlc2dunkolbjornpillarpuzzlelever";
		}

		bool PieceSolved(const ObjectPtr& a_piece, const ObjectPtr& a_control)
		{
			if (!a_piece) {
				return true;
			}
			const auto name = Name(a_piece);
			if (name == "dunkarthspirepillarpuzzle") {
				return State(a_piece) == "position02";
			}
			if (name == "dunraldbtharpuzzlegearblockerscript") {
				return State(a_piece) != "waiting";
			}
			if (name == "mg06dome01script") {
				return Flag(a_piece, "DomeReady");
			}
			if (a_piece->GetVariable("::ChimeNumber_var")) {
				return Int(a_piece, "::ChimeNumber_var") < Int(a_control, "::NextCorrectChime_var") || Flag(a_control, "AlreadySolved");
			}
			if (a_piece->GetVariable("::buttonNumber_var")) {
				return Int(a_piece, "::buttonNumber_var") <= Int(a_control, "::numPuzzleButtonsSolved_var");
			}
			const bool first = !IsOpen(RefVariable(a_control, "::refActOnSuccess01_var"));
			if (a_piece->GetVariable("::solveState1_var")) {  // Skuldafn's two-stage pillar
				return Int(a_piece, "::localState_var") == Int(a_piece, first ? "::solveState1_var" : "::solveState2_var");
			}
			if (a_piece->GetVariable("::solution1_var")) {  // Kolbjorn: pillarState 1 for the first gate, 2 for the second
				return Int(a_piece, "::pillarState_var") == (first ? 1 : 2);
			}
			return Flag(a_piece, "ringSolved") || Flag(a_piece, "pillarSolved");
		}

		// Folgunthur's catacombs: each lever toggles some of the four gates (dunFolgunthurCataLeverPuzzle: lever 1 gates
		// 1, 3, 4; lever 2 gates 2, 3; lever 3 gate 3; lever 4 gates 1, 4). Bit i = gate i + 1 closed.
		constexpr std::array<std::uint32_t, 4> kLeverGates{ 0b1101, 0b0110, 0b0100, 0b1001 };

		std::uint32_t ClosedGates(const ObjectPtr& a_control)
		{
			std::uint32_t closed = 0;
			for (std::uint32_t i = 0; i < 4; ++i) {
				if (!IsOpen(RefVariable(a_control, std::format("::Gate{}_var", i + 1).c_str()))) {
					closed |= 1u << i;
				}
			}
			return closed;
		}

		// The fewest levers (bit i = lever i + 1) that open every gate; nullopt if none does.
		std::optional<std::uint32_t> LeverPlan(const ObjectPtr& a_control)
		{
			const auto                   closed = ClosedGates(a_control);
			std::optional<std::uint32_t> best;
			for (std::uint32_t levers = 0; levers < 16; ++levers) {
				std::uint32_t toggled = 0;
				for (std::uint32_t i = 0; i < 4; ++i) {
					if (levers & (1u << i)) {
						toggled ^= kLeverGates[i];
					}
				}
				if (toggled == closed && (!best || std::popcount(levers) < std::popcount(*best))) {
					best = levers;
				}
			}
			return best;
		}

		// Fahlbtharz: the resonator to switch next (index into a_pieces) on the shortest way to the target charge.
		std::optional<std::size_t> SteamMove(const std::vector<Piece>& a_pieces, const ObjectPtr& a_control)
		{
			const int        target = Int(a_control, "::SucessfulSteamCharge_var");
			std::vector<int> deltas;
			for (const auto& piece : a_pieces) {
				auto ref = piece.ref.get();
				auto script = ref ? PieceScript(ref.get()) : ObjectPtr{};
				const int charge = script ? Int(script, "::SteamCharge_var") : 0;
				deltas.push_back(State(script) == "active" ? -charge : charge);
			}
			std::vector<std::size_t>                         path;  // the moves backwards
			std::function<bool(int, std::uint32_t, int)> search = [&](int a_charge, std::uint32_t a_used, int a_left) {
				if (a_charge == target) {
					return true;
				}
				for (std::size_t i = 0; a_left > 0 && i < deltas.size(); ++i) {
					const int next = a_charge + deltas[i];
					if (!(a_used & (1u << i)) && deltas[i] != 0 && next >= 0 && next <= target && search(next, a_used | (1u << i), a_left - 1)) {
						path.push_back(i);
						return true;
					}
				}
				return false;
			};
			for (int depth = 1; depth <= static_cast<int>(deltas.size()) && deltas.size() <= 16; ++depth) {
				path.clear();
				if (search(Int(a_control, "::CurrentSteamCharge_var"), 0, depth)) {
					return path.back();
				}
			}
			return std::nullopt;
		}

		// Already open: nothing to do there.
		bool ControlDone(const ObjectPtr& a_control)
		{
			if (!a_control) {
				return true;
			}
			const auto name = Name(a_control);
			if (name == "dunfolgunthurcataleverpuzzle") {
				return ClosedGates(a_control) == 0;
			}
			if (a_control->GetVariable("::SucessfulSteamCharge_var")) {
				return Int(a_control, "::CurrentSteamCharge_var") == Int(a_control, "::SucessfulSteamCharge_var") ||
				       IsOpen(RefVariable(a_control, "::DLC2dunFahlbtharzPuzzleDoor_var"));
			}
			if (name == "dlc01dundbchimemasterscript") {
				auto quest = a_control->GetVariable("::DLC1LD_Arkngthamz_var");
				auto arkngthamz = quest && quest->IsObject() ? quest->Unpack<RE::TESQuest*>() : nullptr;
				return Flag(a_control, "AlreadySolved") || !arkngthamz || arkngthamz->GetCurrentStageID() >= 90;
			}
			if (name == "mg06armillaryscript") {  // stage 55: the beam reaches the focus; "initial": no crystal in it yet
				auto quest = a_control->GetVariable("::MG06_var");
				auto mg06 = quest && quest->IsObject() ? quest->Unpack<RE::TESQuest*>() : nullptr;
				return !mg06 || mg06->GetCurrentStageID() >= 55 || State(a_control) == "initial";
			}
			if (TwoGates(a_control)) {
				return IsOpen(RefVariable(a_control, "::refActOnSuccess01_var")) && IsOpen(RefVariable(a_control, "::refActOnSuccess02_var"));
			}
			if (name == "dunkarthspirepuzzlemaster") {
				return Flag(a_control, "::pillarSolved_var");
			}
			const auto state = State(a_control);
			if (name == "dunraldbtharpuzzlebuttonscript") {  // closed -> waiting (opening) -> open -> waiting (pressed)
				return state == "waiting" && Int(a_control, "::PillarsLit_var") >= 2;
			}
			return Flag(a_control, "::puzzleSolved_var") || state == "done" || state == "active";
		}

		// Raldbthar: blocker -> gear -> ... -> the pillar, whose button opens the door.
		RE::TESObjectREFR* GearButton(RE::TESObjectREFR* a_blocker)
		{
			auto ref = a_blocker;
			for (int i = 0; i < 6 && ref; ++i) {
				ref = ref->GetLinkedRef(nullptr);
				for (auto& script : Scripts(ref)) {
					if (auto button = RefVariable(script, "::myButton_var")) {
						return button;
					}
				}
			}
			return nullptr;
		}

		ObjectPtr Keyhole(RE::TESObjectREFR* a_ref)
		{
			for (auto& script : Scripts(a_ref)) {
				const auto name = Name(script);
				if (name == "hallofstorieskeyholescript" || name == "snowveilkeyholescript") {
					return script;
				}
			}
			return {};
		}

		void Call(ObjectPtr a_script, const char* a_function)
		{
			auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (vm && a_script) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				vm->DispatchMethodCall(a_script, a_function, RE::MakeFunctionArguments(), none);
			}
		}

		RE::TESBoundObject* Claw(const ObjectPtr& a_keyhole)
		{
			auto var = a_keyhole ? a_keyhole->GetVariable("::myMiscObject_var") : nullptr;
			return var && var->IsObject() ? var->Unpack<RE::TESObjectMISC*>() : nullptr;
		}

		// Unsolved puzzles within a_range of the player, or anywhere in an interior cell with a_range 0 (game thread).
		// Claw doors whose claw the Dragonborn doesn't carry are left out, they and their claws' names go to a_missingClaws.
		using MissingClaws = std::vector<std::pair<RE::ObjectRefHandle, std::string>>;
		std::vector<Job> Find(RE::PlayerCharacter* a_player, float a_range = kRange, MissingClaws* a_missingClaws = nullptr)
		{
			std::vector<Job>                                 jobs;
			std::map<RE::TESObjectREFR*, std::vector<Piece>> pillars;  // lever -> its pillars
			auto visit = [&](RE::TESObjectREFR* a_ref) {
				if (!a_ref || a_ref->IsDisabled() || !a_ref->Is3DLoaded()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				if (auto keyhole = Keyhole(a_ref); keyhole && !ControlDone(keyhole)) {
					auto claw = Claw(keyhole);
					if (!claw || a_player->GetItemCount(claw) < 1) {
						if (a_missingClaws) {
							a_missingClaws->emplace_back(a_ref->GetHandle(), claw && claw->GetName() && *claw->GetName() ? claw->GetName() : Tr("puzzle.a_claw"));
						}
						return RE::BSContainer::ForEachResult::kContinue;
					}
					Job job{ Kind::kClaw, a_ref->GetHandle(), {} };
					for (auto ring : { "::largeRing_var", "::mediumRing_var", "::smallRing_var" }) {
						if (auto piece = RefVariable(keyhole, ring)) {
							job.pieces.push_back({ piece->GetHandle() });
						}
					}
					if (!job.pieces.empty()) {
						jobs.push_back(std::move(job));
					}
				} else if (auto pillar = PieceScript(a_ref); pillar && !pillar->GetVariable("ringSolved")) {
					auto lever = Name(pillar) == "dunraldbtharpuzzlegearblockerscript" ? GearButton(a_ref) : a_ref->GetLinkedRef(nullptr);
					// a resonator's linked reference is its glow or steam pipe: its master is a property
					for (auto master : { "::ChimeMaster_var", "::DLC2dunFahlbtharzSteamPuzzleControllerREf_var" }) {
						if (auto ref = RefVariable(pillar, master)) {
							lever = ref;
						}
					}
					if (!lever) {
						lever = RefVariable(pillar, "::controllerScript_var");  // Karthspire's pillars point at their master this way
					}
					if (lever && !ControlDone(ControlScript(lever))) {
						pillars[lever].push_back({ a_ref->GetHandle() });
					}
				} else if (auto control = ControlScript(a_ref); control && !ControlDone(control) && (KindOf(control) == Kind::kLevers || KindOf(control) == Kind::kDomes)) {
					// the levers or domes are the control's properties
					const auto kind = KindOf(control);
					Job        job{ kind, a_ref->GetHandle(), {} };
					for (int i = 1; i <= (kind == Kind::kLevers ? 4 : 3); ++i) {
						auto piece = RefVariable(control, (kind == Kind::kLevers ? std::format("::Lever{}_var", i) : std::format("::Dome0{}Ref_var", i)).c_str());
						job.pieces.push_back({ piece ? piece->GetHandle() : RE::ObjectRefHandle{} });
					}
					jobs.push_back(std::move(job));
				}
				return RE::BSContainer::ForEachResult::kContinue;
			};
			auto cell = a_player->GetParentCell();
			if (a_range > 0.0f || !cell || !cell->IsInteriorCell()) {
				RE::TES::GetSingleton()->ForEachReferenceInRange(a_player, a_range > 0.0f ? a_range : kRange, visit);
			} else {
				cell->ForEachReference([&](RE::TESObjectREFR* a_ref) { return visit(a_ref); });
			}
			for (auto& [lever, pieces] : pillars) {
				const auto kind = KindOf(ControlScript(lever));
				if (kind == Kind::kButtons || kind == Kind::kChimes) {  // pressed in this order
					auto number = [kind](const Piece& a_piece) {
						auto ref = a_piece.ref.get();
						return ref ? Int(PieceScript(ref.get()), kind == Kind::kButtons ? "::buttonNumber_var" : "::ChimeNumber_var") : 0;
					};
					std::ranges::sort(pieces, {}, number);
				}
				jobs.push_back({ kind, lever->GetHandle(), std::move(pieces) });
			}
			return jobs;
		}

		const char* KindName(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::kClaw:
				return "claw";
			case Kind::kGears:
				return "gears";
			case Kind::kButtons:
				return "buttons";
			case Kind::kLevers:
				return "levers";
			case Kind::kDomes:
				return "domes";
			case Kind::kChimes:
				return "chimes";
			case Kind::kSteam:
				return "steam";
			default:
				return "pillars";
			}
		}

		const char* SolvedText(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::kClaw:
				return Tr("status.puzzle_claw_door_opened");
			case Kind::kGears:
				return Tr("status.puzzle_gears_solved");
			case Kind::kButtons:
				return Tr("status.puzzle_buttons_solved");
			case Kind::kLevers:
				return Tr("status.puzzle_levers_solved");
			case Kind::kDomes:
				return Tr("status.puzzle_domes_solved");
			case Kind::kChimes:
				return Tr("status.puzzle_chimes_solved");
			case Kind::kSteam:
				return Tr("status.puzzle_steam_solved");
			default:
				return Tr("status.puzzle_pillars_solved");
			}
		}

		std::string Describe(const std::vector<Job>& a_jobs)
		{
			std::string text;
			for (auto [kind, key] : { std::pair{ Kind::kClaw, "puzzle.claw_doors" }, std::pair{ Kind::kPillars, "puzzle.pillar_puzzles" },
					 std::pair{ Kind::kGears, "puzzle.gear_puzzles" }, std::pair{ Kind::kButtons, "puzzle.button_puzzles" },
					 std::pair{ Kind::kLevers, "puzzle.lever_puzzles" }, std::pair{ Kind::kDomes, "puzzle.dome_puzzles" },
					 std::pair{ Kind::kChimes, "puzzle.chime_puzzles" }, std::pair{ Kind::kSteam, "puzzle.steam_puzzles" } }) {
				if (const auto count = std::ranges::count(a_jobs, kind, &Job::kind)) {
					text += (text.empty() ? "" : ", ") + Lang::Fmt(key, count);
				}
			}
			return text;
		}

		// One step of the first job: wait while something turns, turn the next wrong piece, or open up when all are right.
		void Step(RE::PlayerCharacter* a_player)
		{
			std::scoped_lock lock{ g_lock };
			if (g_jobs.empty()) {
				return;
			}
			auto& job = g_jobs.front();
			auto  control = job.control.get();
			auto  controlScript = control ? ControlScript(control.get()) : ObjectPtr{};
			if (!controlScript) {
				logger::warn("Puzzles: the {} control is gone", KindName(job.kind));
				g_jobs.erase(g_jobs.begin());
				return;
			}
			const bool pressed = std::ranges::any_of(job.pieces, [](const Piece& a_piece) { return a_piece.presses > 0; });
			if (job.kind == Kind::kGears && State(controlScript) == "waiting" && pressed && ++job.waits <= 150) {
				return;  // the button is opening after the last pillar lit up
			}
			if (ControlDone(controlScript)) {  // opened by itself on the last turn (Karthspire) or by the Dragonborn meanwhile
				if (pressed) {
					logger::info("Puzzles: {:08X} opened by itself", control->GetFormID());
					Menu::Toast(SolvedText(job.kind));
				}
				g_jobs.erase(g_jobs.begin());
				return;
			}
			if (Busy(controlScript)) {
				return;
			}
			if (job.kind == Kind::kLevers) {  // one lever at a time, the plan worked out again from the gates each time
				const auto plan = LeverPlan(controlScript);
				const auto index = plan && *plan ? std::countr_zero(*plan) : 0;
				auto       lever = plan && *plan ? job.pieces[index].ref.get() : nullptr;
				if (!lever || ++job.pieces[index].presses > kMaxPresses) {
					logger::warn("Puzzles: no lever plan opens every gate of {:08X}", control->GetFormID());
					Menu::Toast(Tr("status.puzzle_failed"));
					g_jobs.erase(g_jobs.begin());
					return;
				}
				// the levers reach the puzzle through their activate parent: its OnActivate takes the lever pulled
				auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				vm->DispatchMethodCall(controlScript, "OnActivate", RE::MakeFunctionArguments(lever.get()), none);
				g_next = std::chrono::steady_clock::now() + kTurnTime;
				return;
			}
			if (job.kind == Kind::kSteam) {  // one resonator at a time, the way worked out again from the charge each time
				if (std::ranges::any_of(job.pieces, [](const Piece& a_piece) {
						auto ref = a_piece.ref.get();
						return ref && Busy(PieceScript(ref.get()));
					})) {
					return;
				}
				const auto move = SteamMove(job.pieces, controlScript);
				auto       ref = move ? job.pieces[*move].ref.get() : nullptr;
				if (!ref || ++job.pieces[*move].presses > kMaxPresses) {
					logger::warn("Puzzles: no way to the steam charge of {:08X}", control->GetFormID());
					Menu::Toast(Tr("status.puzzle_failed"));
					g_jobs.erase(g_jobs.begin());
					return;
				}
				// the resonators ignore the player's activation (they are meant to be hit): their OnActivate directly
				auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				auto resonator = PieceScript(ref.get());
				vm->DispatchMethodCall(resonator, "OnActivate", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_player)), none);
				g_next = std::chrono::steady_clock::now() + kTurnTime;
				return;
			}
			if (job.kind == Kind::kDomes) {
				// the armillary first: frost turns it up, flame down, to position 4 (the beams are ready then)
				const auto position = Int(controlScript, "::Positionvar_var");
				if (position != 4) {
					auto effect = controlScript->GetVariable(position < 4 ? "::FrostEffect_var" : "::FlameEffect_var");
					auto magic = effect && effect->IsObject() ? effect->Unpack<RE::EffectSetting*>() : nullptr;
					if (!magic || ++job.waits > kMaxPresses) {
						logger::warn("Puzzles: the armillary {:08X} never reaches position 4", control->GetFormID());
						Menu::Toast(Tr("status.puzzle_failed"));
						g_jobs.erase(g_jobs.begin());
						return;
					}
					auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(controlScript, "OnMagicEffectApply", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_player), std::move(magic)), none);
					g_next = std::chrono::steady_clock::now() + 4s;  // no busy state while it turns
					return;
				}
				job.waits = 0;
			}
			// every wrong piece turns at the same time (user: not one after another); each one keeps its own busy state.
			// Volskygge's buttons and Arkngthamz's resonators go one at a time, in order.
			const bool inOrder = job.kind == Kind::kButtons || job.kind == Kind::kChimes;
			bool       turning = false;
			for (auto& piece : job.pieces) {
				auto ref = piece.ref.get();
				auto script = ref ? PieceScript(ref.get()) : ObjectPtr{};
				// a struck resonator stays struck: it waits until the master has counted it
				const bool struck = Flag(script, "::AlreadyHit_var") && !PieceSolved(script, controlScript);
				if (Busy(script) || struck) {
					turning = true;  // still turning (counts as right already on its last turn): the door waits for it
					if (inOrder) {
						break;
					}
					continue;
				}
				if (PieceSolved(script, controlScript)) {
					continue;
				}
				turning = true;
				if (++piece.presses > kMaxPresses) {
					logger::warn("Puzzles: {:08X} never comes right, giving up", ref->GetFormID());
					Menu::Toast(Tr("status.puzzle_failed"));
					g_jobs.erase(g_jobs.begin());
					return;
				}
				if (script && script->GetVariable("::ChimeNumber_var")) {  // a resonator only answers a hit
					auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(script, "OnHit",
						RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_player), static_cast<RE::TESForm*>(nullptr),
							static_cast<RE::BGSProjectile*>(nullptr), false, false, false, false),
						none);
				} else {
					ref->ActivateRef(a_player, 0, nullptr, 1, false);
				}
				g_next = std::chrono::steady_clock::now() + kTurnTime;
				if (inOrder) {
					break;
				}
			}
			if (turning) {
				return;
			}
			const auto name = Name(controlScript);
			if (job.kind == Kind::kGears || job.kind == Kind::kButtons || job.kind == Kind::kDomes || job.kind == Kind::kChimes) {
				// the gears light the pillars, which open the button; the last of Volskygge's buttons opens the doors
				if (State(controlScript) != "open") {
					if (++job.waits > 150) {  // 15 s
						logger::warn("Puzzles: {:08X} never opened, giving up", control->GetFormID());
						Menu::Toast(Tr("status.puzzle_failed"));
						g_jobs.erase(g_jobs.begin());
					}
					return;
				}
				control->ActivateRef(a_player, 0, nullptr, 1, false);  // Raldbthar's button: the door opens
			} else if (job.kind == Kind::kClaw && name == "snowveilkeyholescript") {
				Call(controlScript, "openDoor");
			} else if (job.kind == Kind::kClaw) {
				control->ActivateRef(a_player, 0, nullptr, 1, false);  // the claw goes in: the game checks it and opens up
			} else {
				// each pillar does numPillarsSolved += 1 on the lever as a property get then set: pillars turning at the
				// same time lose some of them (2 of 3 counted, darts instead of the gate; a lost -1 overcounts the same
				// way). Recount from their flags, when every pillar has one (not Skuldafn's two-stage pillar).
				auto       count = controlScript->GetVariable("::numPillarsSolved_var");
				const bool flagged = std::ranges::all_of(job.pieces, [](const Piece& a_piece) {
					auto ref = a_piece.ref.get();
					auto script = ref ? PieceScript(ref.get()) : ObjectPtr{};
					return script && script->GetVariable("pillarSolved");
				});
				const auto solved = std::ranges::count_if(job.pieces, [](const Piece& a_piece) {
					auto ref = a_piece.ref.get();
					return ref && Flag(PieceScript(ref.get()), "pillarSolved");
				});
				if (flagged && count && count->IsInt() && count->GetSInt() != solved) {
					logger::info("Puzzles: {:08X} counted {} solved pillars of {}, fixed", control->GetFormID(), count->GetSInt(), solved);
					count->SetSInt(static_cast<std::int32_t>(solved));
				}
				if (HasFunction(controlScript, "doorOrDarts")) {
					Call(controlScript, "doorOrDarts");
				} else if (name != "dunkarthspirepuzzlemaster") {  // Karthspire opens by itself after the last turn
					// Halldir's rotating door, Shroud Hearth's controller (a trigger volume: activating the reference
					// never reached its script), so its OnActivate is called directly, with the Dragonborn as activator
					auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
					vm->DispatchMethodCall(controlScript, "OnActivate", RE::MakeFunctionArguments(static_cast<RE::TESObjectREFR*>(a_player)), none);
				}
			}
			logger::info("Puzzles: {} {:08X} solved", KindName(job.kind), control->GetFormID());
			Menu::Toast(SolvedText(job.kind));
			auto done = std::move(job);
			g_jobs.erase(g_jobs.begin());
			if (TwoGates(controlScript)) {  // the second gate next: the pillars now aim at it
				for (auto& piece : done.pieces) {
					piece.presses = 0;
				}
				g_jobs.push_back(std::move(done));
				g_next = std::chrono::steady_clock::now() + 3s;
				return;
			}
			g_next = std::chrono::steady_clock::now() + kTurnTime;
		}
	}

	std::string SolveNearby()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			return Tr("status.load_a_game_first");
		}
		MissingClaws missing;
		auto         jobs = Find(player, kRange, &missing);
		const auto   needClaw = missing.empty() ? std::string{} : Lang::Fmt("status.puzzle_needs_claw", missing.front().second);
		if (jobs.empty()) {
			return missing.empty() ? std::string{ Tr("status.puzzle_none") } : needClaw;
		}
		auto text = Describe(jobs);
		{
			std::scoped_lock lock{ g_lock };
			g_jobs = std::move(jobs);
			g_next = {};
		}
		logger::info("Puzzles: solving {}", text);
		return Lang::Fmt("status.puzzle_solving", text) + (needClaw.empty() ? "" : ". " + needClaw);
	}

	std::vector<Mark> Marks(float a_range)
	{
		std::scoped_lock lock{ g_marksLock };
		g_marksRange = a_range;
		g_marksWanted = std::chrono::steady_clock::now();
		return g_marks;
	}

	void Update()
	{
		const auto now = std::chrono::steady_clock::now();
		bool       scan = false;
		{
			std::scoped_lock lock{ g_marksLock };
			if (now - g_marksWanted > 3s) {
				g_marks.clear();  // the ESP is off: nothing kept, nothing scanned
			} else if (now - g_marksScanned > 1s && !g_marksQueued) {
				g_marksScanned = now;
				scan = true;
			}
		}
		if (scan) {
			g_marksQueued = true;
			SKSE::GetTaskInterface()->AddTask([] {
				std::vector<Mark> marks;
				if (auto player = RE::PlayerCharacter::GetSingleton(); player && player->Is3DLoaded()) {
					float range;
					{
						std::scoped_lock lock{ g_marksLock };
						range = g_marksRange;
					}
					MissingClaws missing;
					for (const auto& job : Find(player, range, &missing)) {
						marks.push_back({ job.control, Tr(job.kind == Kind::kClaw ? "esp.puzzle_claw" : "esp.puzzle") });
					}
					for (const auto& [ref, claw] : missing) {
						marks.push_back({ ref, Lang::Fmt("esp.puzzle_claw_needs", claw) });
					}
				}
				{
					std::scoped_lock lock{ g_marksLock };
					g_marks = std::move(marks);
				}
				g_marksQueued = false;
			});
		}
		{
			std::scoped_lock lock{ g_lock };
			if (g_jobs.empty() || std::chrono::steady_clock::now() < g_next || g_stepQueued) {
				return;
			}
		}
		g_stepQueued = true;
		SKSE::GetTaskInterface()->AddTask([] {
			if (auto player = RE::PlayerCharacter::GetSingleton(); player && player->Is3DLoaded()) {
				Step(player);
			}
			g_stepQueued = false;
		});
	}

	std::string Debug()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Is3DLoaded()) {
			return "[]";
		}
		std::string  out;
		MissingClaws missing;
		for (const auto& job : Find(player, 0.0f, &missing)) {
			auto        control = job.control.get();
			auto        controlScript = control ? ControlScript(control.get()) : ObjectPtr{};
			std::string pieces;
			for (const auto& piece : job.pieces) {
				auto ref = piece.ref.get();
				auto script = ref ? PieceScript(ref.get()) : ObjectPtr{};
				pieces += std::format("{}{{\"ref\":\"{:08X}\",\"state\":\"{}\",\"solved\":{}}}", pieces.empty() ? "" : ",", ref ? ref->GetFormID() : 0,
					script ? script->currentState.c_str() : "", PieceSolved(script, controlScript));
			}
			out += std::format("{}{{\"kind\":\"{}\",\"control\":\"{:08X}\",\"script\":\"{}\",\"state\":\"{}\",\"pieces\":[{}]}}", out.empty() ? "" : ",",
				KindName(job.kind), control ? control->GetFormID() : 0, controlScript ? Name(controlScript) : "",
				controlScript ? controlScript->currentState.c_str() : "", pieces);
		}
		{
			std::scoped_lock lock{ g_lock };
			std::string claws;
			for (const auto& [ref, claw] : missing) {
				claws += (claws.empty() ? "\"" : ",\"") + claw + "\"";
			}
			out = std::format("{{\"working\":{},\"found\":[{}],\"missingClaws\":[{}]}}", g_jobs.size(), out, claws);
		}
		return out;
	}
}

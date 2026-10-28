# Aimbot in the open (run where the game already stands; never change cells after spawning: a spawned, restrained
# creature that is disabled before a cell change crashed the game twice). Each creature: spawn at the player, restrain,
# make hostile, stand DIST south of it, turn OFF degrees aside, shoot (bow or a spell), compare health.
import json, re, subprocess, sys, time

T = r"G:\SkyrimModding\CheatMenu\tools\skytest.py"
CREATURES = [
    ("0003A1E0", "раненый морозный паук"), ("000999A5", "гигантский морозный паук"), ("000936D3", "великан"),
    ("00023AB4", "мамонт"), ("0006AF65", "тролль"), ("0006AF5A", "ледяной тролль"), ("000C96C2", "медведь"),
    ("000C96C3", "пещерный медведь"), ("000C96BF", "саблезуб"), ("0010F9BB", "двемерский центурион"),
    ("000EF607", "хоркер"), ("00000EA6", "ледяной атронах"), ("000E4010", "грязевой краб"), ("0010FC24", "злокрыс"),
]
# spell id, name, distance (flames reach only a few metres), expected: hit / no aim
SPELLS = [
    ("00012FD0", "огненная стрела", 1000, True), ("0002B96C", "ледяной шип", 1000, True), ("0001C789", "огненный шар", 1000, True),
    ("0002DD29", "удар молнии", 1000, True), ("00012FCD", "пламя", 350, True), ("0002DD2A", "искры", 350, True),
    ("0002F3B8", "исцеляющие руки (не наводится)", 350, False),
]
STAFFS = [("FE03497E", "посох огненных стрел"), ("FE03497A", "посох огненных шаров"), ("FE0348CC", "посох молний")]
OFF = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
what = sys.argv[2] if len(sys.argv) > 2 else "all"
only = sys.argv[3] if len(sys.argv) > 3 else ""  # e.g. "центурион,хоркер": just those
if only:
    CREATURES = [c for c in CREATURES if any(p in c[1] for p in only.split(","))]


def run(*cmds):
    out = subprocess.run([sys.executable, T, "run", *cmds], capture_output=True, text=True, encoding="utf-8").stdout
    return [json.loads(line.split(" -> ", 1)[1])["data"] for line in out.splitlines() if " -> " in line]


def health():
    m = re.search(r">> ([0-9.]+)", run("console @last getav health")[0] or "")
    return float(m.group(1)) if m else None


def aimlog():
    lines = [x.split("Aimbot: ", 1)[1] for x in run("log 12")[0] if "Aimbot: " in x]
    picks = [x for x in lines if " -> " in x]
    hits = [x for x in lines if " hit " in x]
    return (picks[-1] if picks else "-") + ("  | " + hits[-1] if hits else "")


def setup(base, dist):
    run(f"call spawn {base}")
    time.sleep(2)
    run("console @last setrestrained 1", "console @last setav aggression 2", f"call standFrom last 180 {dist}")
    time.sleep(1.5)
    run(f"call faceOff last {OFF}")
    time.sleep(1)


def done():
    # the creature is carried 80 m off while still alive (a ragdoll ignores setpos) and killed there: bodies left on the
    # spot hid the next creatures; never disabled (see the top)
    run("call standFrom last 0 0")
    m = re.search(r">> (-?[0-9.]+)", run("console @last getpos x")[0] or "")
    if m:
        run(f"console @last setpos x {float(m.group(1)) + 8000}")
        time.sleep(0.5)
    run("console @last kill")
    time.sleep(1)


run("console tgm", "set aimbot 1", "set fastBowDraw 1", "apply", "console player.additem 13841 1", "console player.additem 1397D 300",
    "console player.equipitem 13841", "console player.equipitem 1397D")
hits = total = 0
if what in ("all", "bow"):
    for base, name in CREATURES:
        setup(base, 1000)
        run("call attack last draw")
        time.sleep(1.5)
        h1 = health()
        run("console player.sae bowAttackStart")
        time.sleep(1.2)
        run("console player.sae attackRelease")
        time.sleep(3)
        h2 = health()
        hit = h1 is not None and (h2 is None or h2 < h1)  # None: it died, the console gives no value
        hits += hit; total += 1
        print(f"{'HIT ' if hit else 'MISS'} лук  {name:26} off {OFF}°  hp {h1} -> {h2}  {aimlog()}", flush=True)
        done()
if what in ("all", "xbow"):  # Dawnguard dwarven crossbow + steel bolts: the attack action readies it, attackRelease fires
    run("console player.additem 0200F19F 1", "console player.additem 02000BB3 300", "console player.equipitem 0200F19F",
        "console player.equipitem 02000BB3")
    time.sleep(1.5)
    for base, name in CREATURES:
        setup(base, 1000)
        run("call attack last draw")
        time.sleep(1.5)
        h1 = health()
        run("call attack last")
        time.sleep(1)
        run("console player.sae attackRelease")
        time.sleep(3)
        h2 = health()
        hit = h1 is not None and (h2 is None or h2 < h1)
        hits += hit; total += 1
        print(f"{'HIT ' if hit else 'MISS'} арбалет {name:24} off {OFF}°  hp {h1} -> {h2}  {aimlog()}", flush=True)
        done()
if what in ("all", "staff"):  # the staff's own enchantment from the right hand's caster (the staff animation can't be driven)
    for staff, name in STAFFS:
        ench = run(f"form {staff}")[0].get("enchantment")
        run(f"console player.additem {staff} 1", f"console player.equipitem {staff}")
        setup("000C96C2", 1000)
        h1 = health()
        run(f"call castForward {ench} right")
        time.sleep(3)
        h2 = health()
        hit = h1 is not None and (h2 is None or h2 < h1)
        hits += hit; total += 1
        print(f"{'HIT ' if hit else 'MISS'} {name:32} off {OFF}°  hp {h1} -> {h2}  {aimlog()}", flush=True)
        done()
if what in ("all", "spell"):
    for spell, name, dist, expect in SPELLS:
        setup("000C96C2", dist)
        h1 = health()
        run(f"call castForward {spell}")
        time.sleep(2 if dist < 500 else 0.2)  # flames / sparks: a two-second burst
        run("console player.interruptcast")  # an instant-cast concentration spell would go on forever
        time.sleep(3)
        h2 = health()
        hit = h1 is not None and h2 is not None and h2 < h1
        ok = hit == expect
        hits += ok; total += 1
        print(f"{'OK  ' if ok else 'FAIL'} {name:32} off {OFF}°  hp {h1} -> {h2}  {aimlog()}", flush=True)
        done()
print(f"\n{hits}/{total}")

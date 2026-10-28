"""Drive Cheat Menu's test bridge from outside the game — fully unattended.

    python skytest.py launch [--menu]            start the "Mod Menu Dev" profile via MO2 and load the latest save
                                                 ("Continue" from the main menu); --menu stops at the main menu
    python skytest.py cycle                      quit the game -> build.bat -> launch (one dev iteration)
    python skytest.py run "<cmd>" ["<cmd>" ...]  send bridge commands, print one JSON reply per command
    python skytest.py script file.txt            commands from a file ("wait <seconds>" pauses)
    python skytest.py test file.txt [...]        run test files with assertions, PASS/FAIL summary, exit code 1 on failure
    python skytest.py tour [page ...]            screenshot every menu page (top and bottom) into the bridge folder
    python skytest.py sheet out.png a.png ...    tile screenshots into one image (2 per row)
    python skytest.py status                     game running? bridge alive? new crash logs?
    python skytest.py ingame [timeout]           wait until a save is loaded
    python skytest.py quit                       close the game

Bridge verbs: run `skytest.py run help`. Highlights: console [@ref] <cmd> (returns console output),
refs [meters] [filter], inv <ref>, ui [label], click <label>, key <name>, type <text>, screenshot x.bmp.

Test file syntax (one per line, '#' comments):
    <command>                                    just run it
    wait <seconds>
    expect <command> :: <path> <op> <value>      path like .gold or .health.cur or [0].name ('.' = whole reply)
                                                 ops: == != > >= < <= contains !contains
    shot <name>                                  screenshot to <name>.png

Needs testBridge=1 in Documents\\My Games\\Skyrim Special Edition\\SKSE\\DragonbornsCodex.ini.
"""

import itertools
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

MO2 = Path(r"G:\Games\Neverland\Neverland\ModOrganizer.exe")
PROFILE = "Mod Menu Dev"
EXECUTABLE = "Neverland 1.0"
PROJECT = Path(__file__).resolve().parent.parent
SKSE_DIR = Path(os.path.expandvars(r"%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE"))
BRIDGE = SKSE_DIR / "CheatMenuBridge"
SAVES = MO2.parent / "profiles" / PROFILE / "saves"
SHOT_MAX_WIDTH = 1600
PAGES = ["home", "dovahkiin", "combat", "magic", "schools", "progress", "world", "teleport", "quests", "target", "console", "items", "actors", "service"]

_ids = itertools.count(int(time.time() * 1000) % 1_000_000)


# ---------------------------------------------------------------- process

def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SkyrimSE.exe", "/NH"], capture_output=True, text=True).stdout
    return "SkyrimSE.exe" in out


def crash_logs(since=0.0):
    found = []
    for folder in (SKSE_DIR, SKSE_DIR / "Crashlogs"):
        if folder.exists():
            found += [p for p in folder.glob("crash-*.log") if p.stat().st_mtime > since]
    return sorted(found, key=lambda p: p.stat().st_mtime)


class GameGone(RuntimeError):
    pass


# ---------------------------------------------------------------- bridge

def send(lines, timeout=20.0):
    """Sends command lines, returns the list of replies (dicts) in the same order."""
    BRIDGE.mkdir(parents=True, exist_ok=True)
    out_file = BRIDGE / "out.jsonl"
    start = out_file.stat().st_size if out_file.exists() else 0
    ids, body = [], []
    for line in lines:
        cid = f"t{next(_ids)}"
        ids.append(cid)
        body.append(f"{cid} {line}")
    target = BRIDGE / "in.txt"
    deadline = time.time() + timeout
    while target.exists():  # previous batch not taken yet
        if time.time() > deadline:
            raise TimeoutError("bridge does not take commands (game not running or testBridge=0?)")
        time.sleep(0.05)
    tmp = BRIDGE / "in.tmp"
    tmp.write_text("\n".join(body) + "\n", encoding="utf-8")
    os.replace(tmp, target)

    replies = {}
    last_alive_check = time.time()
    while len(replies) < len(ids):
        if time.time() > deadline:
            raise TimeoutError(f"no reply for {[i for i in ids if i not in replies]}")
        if time.time() - last_alive_check > 3:
            last_alive_check = time.time()
            if not game_running():
                logs = crash_logs(time.time() - 600)
                raise GameGone("the game is not running" + (f"; crash log: {logs[-1]}" if logs else ""))
        if out_file.exists() and out_file.stat().st_size > start:
            with open(out_file, encoding="utf-8") as f:
                f.seek(start)
                for raw in f:
                    try:
                        msg = json.loads(raw)
                    except json.JSONDecodeError:
                        continue
                    if msg.get("id") in ids:
                        replies[msg["id"]] = msg
        time.sleep(0.05)
    result = [replies[i] for i in ids]
    for msg in result:
        _convert_shot(msg)
    return result


def one(line, timeout=20.0):
    return send([line], timeout)[0]


def _convert_shot(msg):
    data = msg.get("data")
    if not (msg.get("ok") and isinstance(data, str) and data.lower().endswith(".bmp")):
        return
    from PIL import Image

    bmp = Path(data)
    img = Image.open(bmp).convert("RGB")
    if img.width > SHOT_MAX_WIDTH:
        img = img.resize((SHOT_MAX_WIDTH, round(img.height * SHOT_MAX_WIDTH / img.width)))
    png = bmp.with_suffix(".png")
    img.save(png)
    bmp.unlink(missing_ok=True)
    msg["data"] = str(png)


def screenshot(name, settle=0.8):
    time.sleep(settle)
    return one(f"screenshot {name}.bmp", timeout=30)["data"]


# ---------------------------------------------------------------- game lifecycle

def wait_bridge(timeout=240.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return one("ping", timeout=5)
        except TimeoutError:
            (BRIDGE / "in.txt").unlink(missing_ok=True)
        except GameGone:
            time.sleep(2)  # MO2 may still be starting the game
    raise TimeoutError("bridge did not answer")


def wait_ingame(timeout=180.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        reply = one("ping", timeout=10)
        if reply["ok"] and reply["data"].get("inGame"):
            return reply
        time.sleep(1)
    raise TimeoutError("no save loaded")


def newest_save():
    """File name (without .ess) of the profile's most recent save — MO2 keeps profile-local saves here."""
    saves = sorted(SAVES.glob("*.ess"), key=lambda p: p.stat().st_mtime)
    return saves[-1].stem if saves else None


def continue_game():
    """Same as pressing "Continue" on the main menu. Falls back to loading the newest save by name,
    and retries while the main menu is not ready yet."""
    for attempt in range(10):
        ping, menus = send(["ping", "menus"], timeout=10)
        if ping["data"].get("inGame"):
            break
        if "Loading Menu" not in menus["data"]:
            reply = one("continue", timeout=20)
            if not reply["ok"] or attempt % 2 == 1:
                save = newest_save()
                if save:
                    print(f"continue: loading {save} by name")
                    one(f"load {save}")
        try:
            wait_ingame(30)
            break
        except TimeoutError:
            time.sleep(3)
    reply = wait_ingame()
    time.sleep(6)  # the screen fades in after loading; screenshots would be black
    return reply


def launch(to_menu=False):
    started = time.time()
    if not game_running():
        BRIDGE.mkdir(parents=True, exist_ok=True)
        for name in ("in.txt", "in.tmp", "in.processing", "out.jsonl"):
            (BRIDGE / name).unlink(missing_ok=True)
        subprocess.Popen([str(MO2), "-p", PROFILE, f"moshortcut://:{EXECUTABLE}"], cwd=MO2.parent)
    print(json.dumps(wait_bridge(), ensure_ascii=False))
    if not to_menu:
        time.sleep(3)
        print(json.dumps(continue_game(), ensure_ascii=False))
    for log in crash_logs(started):
        print(f"WARNING: crash log {log}")


MO_LOG = MO2.parent / "logs" / "mo_interface.log"


def quit_game(timeout=60):
    was_running = game_running()
    log_size = MO_LOG.stat().st_size if MO_LOG.exists() else 0
    if was_running:
        try:
            send(["console qqq"], timeout=10)
        except (TimeoutError, GameGone):
            pass
    deadline = time.time() + timeout
    while game_running() and time.time() < deadline:
        time.sleep(1)
    if game_running():
        subprocess.run(["taskkill", "/IM", "SkyrimSE.exe", "/F"], capture_output=True)
        time.sleep(2)
    if was_running:
        wait_root_builder(log_size)


def wait_root_builder(log_size, timeout=180):
    # After the game exits MO2's RootBuilder copies the root files back out of the game folder; MO2's window
    # freezes meanwhile ("not responding"). Closing it then, or starting the game again, broke MO2 once
    # (2026-10-26). So wait for its "Clear complete!" line.
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(MO_LOG, encoding="utf-8", errors="replace") as f:
                f.seek(log_size)
                tail = f.read()
        except OSError:
            return
        if "Clear complete!" in tail:
            return
        if "[RootBuilder]" not in tail and time.time() > deadline - timeout + 30:
            return  # RootBuilder didn't start clearing at all
        time.sleep(1)
    print("WARNING: RootBuilder did not report 'Clear complete!' — check MO2")


def build():
    result = subprocess.run(["cmd", "/d", "/c", str(PROJECT / "build.bat")], capture_output=True, text=True, encoding="cp866", errors="replace")
    lines = (result.stdout + result.stderr).splitlines()
    errors = [l for l in lines if "error" in l.lower() or "FAILED" in l]
    if result.returncode != 0:
        print("\n".join(errors[:40] or lines[-40:]))
        raise SystemExit("build failed")
    print("build ok")


# ---------------------------------------------------------------- tests

def lookup(data, path):
    if path in (".", ""):
        return data
    for part in re.findall(r"\.([^.\[]+)|\[(\d+)\]", path):
        key, index = part
        data = data[int(index)] if index else data[key]
    return data


def check(value, op, expected_text):
    try:
        expected = json.loads(expected_text)
    except json.JSONDecodeError:
        expected = expected_text
    if op == "contains":
        if isinstance(value, (list, str)) and expected in value:
            return True
        return str(expected) in json.dumps(value, ensure_ascii=False)
    if op == "!contains":
        return not check(value, "contains", expected_text)
    if isinstance(value, (int, float)) and isinstance(expected, (int, float)):
        return {"==": abs(value - expected) < 1e-3, "!=": abs(value - expected) >= 1e-3, ">": value > expected, ">=": value >= expected - 1e-3,
                "<": value < expected, "<=": value <= expected + 1e-3}[op]
    return {"==": value == expected, "!=": value != expected}.get(op, False)


def run_test(path):
    passed = failed = 0
    for number, line in enumerate(Path(path).read_text(encoding="utf-8").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        where = f"{Path(path).name}:{number}"
        if line.startswith("wait "):
            time.sleep(float(line.split()[1]))
        elif line.startswith("shot "):
            print(f"  shot  {screenshot(line.split()[1])}")
        elif line.startswith("expect "):
            command, _, assertion = line[7:].partition("::")
            m = re.match(r"\s*(\S+)\s+(==|!=|>=|<=|>|<|!contains|contains)\s+(.*)$", assertion)
            if not m:
                print(f"  BAD   {where}: cannot parse assertion")
                failed += 1
                continue
            reply = one(command.strip(), timeout=60)
            try:
                value = lookup(reply["data"], m.group(1)) if reply["ok"] else reply["data"]
                ok = reply["ok"] and check(value, m.group(2), m.group(3).strip())
            except (KeyError, IndexError, TypeError) as e:
                value, ok = f"<{e!r}>", False
            passed += ok
            failed += not ok
            print(f"  {'PASS' if ok else 'FAIL'}  {where}: {command.strip()} :: {m.group(1)} {m.group(2)} {m.group(3).strip()}"
                  + ("" if ok else f"   (got {json.dumps(value, ensure_ascii=False)[:200]})"))
        else:
            reply = one(line, timeout=60)
            if not reply["ok"]:
                print(f"  ERROR {where}: {line} -> {reply['data']}")
                failed += 1
    return passed, failed


# ---------------------------------------------------------------- screenshots

def tour(pages):
    one("menu open")
    shots = []
    for page in pages:
        one(f"page {page}")
        time.sleep(0.6)
        one("scroll 0")
        shots.append(screenshot(f"tour_{page}_top"))
        one("scroll -1")
        shots.append(screenshot(f"tour_{page}_end"))
    one("menu close")
    for s in shots:
        print(s)
    return shots


def sheet(out, images, crop=(410, 170, 1470, 800)):
    from PIL import Image

    tiles = [Image.open(p).convert("RGB").crop(crop) for p in images]
    w, h = tiles[0].size
    rows = (len(tiles) + 1) // 2
    canvas = Image.new("RGB", (w * 2, h * rows))
    for i, tile in enumerate(tiles):
        canvas.paste(tile, ((i % 2) * w, (i // 2) * h))
    canvas.save(out)
    print(out)


# ---------------------------------------------------------------- cli

def main(argv):
    sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd, rest = argv[1], argv[2:]
    if cmd == "launch":
        launch("--menu" in rest)
    elif cmd == "cycle":
        quit_game()
        build()
        launch()
    elif cmd == "build":
        build()
    elif cmd == "ingame":
        print(json.dumps(wait_ingame(float(rest[0]) if rest else 180), ensure_ascii=False))
    elif cmd == "run":
        for line, reply in zip(rest, send(rest, timeout=60)):
            print(line, "->", json.dumps(reply, ensure_ascii=False))
    elif cmd == "script":
        for line in Path(rest[0]).read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("wait "):
                time.sleep(float(line.split()[1]))
                continue
            print(line, "->", json.dumps(one(line, timeout=60), ensure_ascii=False))
    elif cmd == "test":
        total_pass = total_fail = 0
        for path in rest:
            print(path)
            p, f = run_test(path)
            total_pass += p
            total_fail += f
        print(f"\n{total_pass} passed, {total_fail} failed")
        return 1 if total_fail else 0
    elif cmd == "tour":
        tour(rest or PAGES)
    elif cmd == "sheet":
        sheet(rest[0], rest[1:])
    elif cmd == "status":
        running = game_running()
        print(f"game running: {running}")
        if running:
            try:
                print("bridge:", json.dumps(one("ping", timeout=5)["data"], ensure_ascii=False))
            except TimeoutError:
                print("bridge: no answer")
        for log in crash_logs(time.time() - 24 * 3600):
            print(f"crash log (24h): {log}")
    elif cmd == "quit":
        quit_game()
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

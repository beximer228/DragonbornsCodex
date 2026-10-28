import json, subprocess, sys, time
T = r"G:\SkyrimModding\CheatMenu\tools\skytest.py"
LIST = [("0004359C","коза 3"),("000D206E","коза 4"),("0001CB2C","коза Гледа"),("00023A90","корова"),("000A91A0","курица"),("0002EBE2","коза 1"),("0005487E","коза 2"),("00023A92","собака"),
        ("00109487","бездомная собака"),("0003A1E0","раненый паук"),("000C96C2","медведь"),("00023AB4","мамонт"),
        ("000936D3","великан"),("000EF607","хоркер"),("000C96BF","саблезуб")]
def run(*c):
    out = subprocess.run([sys.executable, T, "run", *c], capture_output=True, text=True, encoding="utf-8").stdout
    return [json.loads(l.split(" -> ",1)[1])["data"] for l in out.splitlines() if " -> " in l]
run("console coc qasmoke"); time.sleep(10); run("console tgm")
for base, name in LIST:
    run(f"call spawn {base}"); time.sleep(2)
    info = run("call aimInfo last")[0]
    print(f"{name:18} tame={info.get('tame') if isinstance(info, dict) else info}  {info.get('race') if isinstance(info, dict) else ''}", flush=True)
    run("console @last kill", "console @last disable"); time.sleep(1)

# Public release archive for Nexus (MO2 / Vortex install it as is): dist/DragonbornsCodex-<version>.zip with
#   SKSE/Plugins/DragonbornsCodex.dll
#   SKSE/Plugins/DragonbornsCodex/   lang, fonts (+ OFL licences), icons, key glyphs, README, LICENSE.txt (GPL-3.0)
#   SKSE/Plugins/DragonbornsCodex/source/   the source the DLL is built from (GPL: CommonLibSSE-NG is GPL-3.0)
# Left out: the .pdb, and icons/item_atlas.dds (SkyUI's icons: their authors' leave is needed to ship them).
# Builds the public DLL first (build_dist.bat: preset "dist", no test bridge, into build-dist).
# python tools/package.py [version]
import sys, os, re, subprocess, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'assets', 'DragonbornsCodex')
DLL = os.path.join(ROOT, 'build-dist', 'DragonbornsCodex.dll')
README = os.path.join(ROOT, 'docs', 'README.txt')
EXCLUDE = {'icons/item_atlas.dds'}
LICENSE = os.path.join(ROOT, 'extern', 'CommonLibSSE-NG', 'COPYING.txt')  # the unmodified GPL-3.0 text
BUILDING = os.path.join(ROOT, 'docs', 'BUILDING.txt')
SOURCE_FILES = ['CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json', 'build_dist.bat']
SOURCE_EXCLUDE = {'TestBridge.cpp'}  # dev-only, compiled out of the public DLL


def version():
    if len(sys.argv) > 1:
        return sys.argv[1]
    cmake = open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    return re.search(r'VERSION (\d+\.\d+\.\d+)', cmake.split('project(')[1]).group(1)


def build():
    result = subprocess.run(['cmd', '/d', '/c', os.path.join(ROOT, 'build_dist.bat')], capture_output=True, text=True,
                            encoding='cp866', errors='replace')
    if result.returncode != 0:
        print(result.stdout[-3000:], result.stderr[-3000:])
        raise SystemExit('build failed')
    # The dev-only test bridge must not be in the public DLL.
    if b'CheatMenuBridge' in open(DLL, 'rb').read():
        raise SystemExit('the test bridge is in the DLL: build-dist must use CODEX_TEST_BRIDGE=OFF')


def main():
    ver = version()
    build()
    out = os.path.join(ROOT, 'dist', f'DragonbornsCodex-{ver}.zip')
    os.makedirs(os.path.dirname(out), exist_ok=True)
    files = 0
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(DLL, 'SKSE/Plugins/DragonbornsCodex.dll')
        z.write(README, 'SKSE/Plugins/DragonbornsCodex/README.txt')
        z.write(LICENSE, 'SKSE/Plugins/DragonbornsCodex/LICENSE.txt')
        z.write(BUILDING, 'SKSE/Plugins/DragonbornsCodex/source/BUILDING.txt')
        files += 4
        for name in SOURCE_FILES:
            z.write(os.path.join(ROOT, name), 'SKSE/Plugins/DragonbornsCodex/source/' + name)
            files += 1
        for name in sorted(os.listdir(os.path.join(ROOT, 'src'))):
            if name.endswith(('.cpp', '.h')) and name not in SOURCE_EXCLUDE:
                z.write(os.path.join(ROOT, 'src', name), 'SKSE/Plugins/DragonbornsCodex/source/src/' + name)
                files += 1
        for folder, _, names in os.walk(ASSETS):
            for name in sorted(names):
                rel = os.path.relpath(os.path.join(folder, name), ASSETS).replace('\\', '/')
                if rel in EXCLUDE or name.startswith('preview_'):
                    continue
                z.write(os.path.join(folder, name), 'SKSE/Plugins/DragonbornsCodex/' + rel)
                files += 1
    print(f'{out}: {files} files, {os.path.getsize(out) // 1024} KB')


if __name__ == '__main__':
    main()

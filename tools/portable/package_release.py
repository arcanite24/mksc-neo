#!/usr/bin/env python3
"""Build a player bundle for this OS (macOS .app zip, Linux tarball, Windows zip).

Bundles hold the release `mksc`, the SDL3 runtime and text notices only: no ROM,
pack or other game content. Players import their own ROM on first start.

    package_release.py --sdl-prefix DIR [--output DIR] [--rom ROM]
    package_release.py --web-build DIR --sdl-prefix DIR [--output DIR] [--rom ROM]
    package_release.py --check BUNDLE... [--rom ROM]

--web-build packages prebuilt mksc.html/js/wasm without invoking a build.
--sdl-prefix is an SDL3 install (only its license is used for web). --rom enables
the deep content scan, which looks for contiguous ROM fragments and encoded tables inside every file.
Python 3.9+ standard library; native builds need CMake and a C compiler.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import zipfile
import hashlib
from content_scan import RomContentIndex, content_matches

ROOT = Path(__file__).resolve().parents[2]
APP_NAME = 'MKSC Neo'
MAC_DEPLOYMENT = '11.0'
ROM_SHA1 = '9d327c030c3e2d9007990518594f70c3340ac56f'
FORBIDDEN_SUFFIXES = {'.gba', '.agb', '.rom', '.pack', '.sav', '.ghost', '.bin', '.ppm',
                      '.aif', '.aiff', '.aifc', '.wav', '.zip', '.gz', '.xz', '.bz2', '.7z', '.tar'}
FORBIDDEN_MAGIC = (b'MKSCPACK', b'MKSCIMG1', b'MKSCIMG2', b'MKSCANI1')
# First bytes of the Nintendo logo every GBA ROM header carries at offset 4.
GBA_LOGO = bytes.fromhex('24ffae51699aa221')

README = """MKSC Neo {version} ({platform})
==========================

A native port of Mario Kart: Super Circuit (USA). This download contains only
the game engine. It contains no Nintendo content: you need your own ROM dump of
Mario Kart: Super Circuit (USA), SHA-1 {sha1}.

Install and play
----------------
{install}
On first start the game asks for your ROM: press Enter (or A on a controller)
to pick the .gba file, or drag the file onto the window. The game converts it
once into game.pack in your data folder (a few seconds) and then opens the
title menu. Your ROM is only read, never changed or copied.

Data folder (game.pack, settings.json, records, ghosts, mods/):
  macOS:   ~/Library/Application Support/MKSC Neo/Portable/
  Linux:   ~/.local/share/MKSC Neo/Portable/
  Windows: %APPDATA%\\MKSC Neo\\Portable\\
Set MKSC_DATA_DIR to use another folder (for example a USB stick).
Delete game.pack to import again.

Command line
------------
  mksc                      play (asks for the ROM on first start)
  mksc --import ROM [PACK]  convert a ROM without opening a window
  mksc --rom ROM            first start with this ROM, then play
  mksc PACK                 play a specific pack
  mksc --help               controls and launch options

Licences
--------
LICENSE-NOTICE.md: project notice. LUA-LICENSE.txt: Lua 5.4 (MIT).
SDL3-LICENSE.txt: SDL 3 (zlib). Not affiliated with Nintendo.
"""

WEB_FILES = ('mksc.html', 'mksc.js', 'mksc.wasm', 'sw.js')
WEB_README = """MKSC Neo {version} (web / WebAssembly)
======================================

This ZIP contains only the game engine and license notices. It contains no ROM,
asset pack or Nintendo graphics/audio. Supply your own Mario Kart: Super Circuit
(USA) ROM, SHA-1 {sha1}.

Run locally
-----------
Extract the ZIP and open a terminal in the extracted folder. Start a local server:

  python3 -m http.server 8000 --bind 127.0.0.1

Open http://127.0.0.1:8000/mksc.html in a WebAssembly-capable browser. Opening the
HTML directly as a file may block loading JavaScript or WebAssembly. Any static
web host serving these files over HTTPS works the same way.

On first start, choose your ROM (.gba, or a .zip containing it). It is imported
in the page and kept in this browser's storage with your saves, settings,
records, ghosts and mods; nothing is uploaded. Later visits start with Play,
and the game keeps working offline once loaded. Clearing the site's data in
the browser removes everything. Controllers work after pressing a button.

Stop the local server with Ctrl-C when finished.

Licences
--------
LICENSE-NOTICE.md: project notice. LUA-LICENSE.txt: Lua 5.4 (MIT).
SDL3-LICENSE.txt: SDL 3 (zlib). Not affiliated with Nintendo.
"""


INSTALL = {
    'macos': ("Unzip, then move 'MKSC Neo.app' to Applications. The app is not\n"
              "notarized: the first time, Control-click it and choose Open (or allow\n"
              "it in System Settings > Privacy & Security).\n"),
    'linux': ("Extract the archive anywhere and run ./mksc from its folder. Needs an\n"
              "x86-64 desktop with X11 or Wayland and glibc 2.31 or newer.\n"),
    'windows': ("Extract the zip anywhere and run mksc.exe. Keep SDL3.dll next to it.\n"
                "Windows may warn about an unknown publisher: choose More info > Run anyway.\n"),
}

PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>{name}</string>
  <key>CFBundleDisplayName</key><string>{name}</string>
  <key>CFBundleExecutable</key><string>mksc</string>
  <key>CFBundleIdentifier</key><string>org.mkscneo.portable</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>{minimum}</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
</dict>
</plist>
"""


def run(command, **kwargs):
    print('+ ' + ' '.join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], check=True, **kwargs)


def capture(command):
    return subprocess.run([str(part) for part in command], check=True, capture_output=True,
                          text=True).stdout


def host_platform():
    if sys.platform == 'darwin':
        return 'macos'
    if sys.platform.startswith('linux'):
        return 'linux'
    if sys.platform == 'win32':
        return 'windows'
    raise SystemExit(f'unsupported platform {sys.platform}')


def default_version():
    try:
        return 'git-' + capture(['git', '-C', ROOT, 'rev-parse', '--short=10', 'HEAD']).strip()
    except (OSError, subprocess.CalledProcessError):
        return 'dev'


def find_one(root, patterns, what):
    for pattern in patterns:
        found = sorted(path for path in Path(root).glob(pattern) if path.is_file())
        if found:
            return found[0]
    raise SystemExit(f'{what} not found under {root} ({", ".join(patterns)})')


def sdl_library(prefix, platform):
    if platform == 'macos':
        return find_one(prefix, ['lib/libSDL3.0.dylib', 'lib/libSDL3*.dylib'], 'libSDL3.0.dylib')
    if platform == 'linux':
        return find_one(prefix, ['lib/libSDL3.so.0', 'lib64/libSDL3.so.0', 'lib/*/libSDL3.so.0'],
                        'libSDL3.so.0')
    return find_one(prefix, ['bin/SDL3.dll', 'lib/x64/SDL3.dll', 'SDL3.dll'], 'SDL3.dll')


def sdl_license(prefix):
    return find_one(prefix, ['share/licenses/SDL3/LICENSE.txt', 'LICENSE.txt', '**/LICENSE.txt'],
                    'SDL3 LICENSE.txt')


def configure_and_build(build, prefix, platform):
    command = ['cmake', '-S', ROOT / 'portable', '-B', build, '-DCMAKE_BUILD_TYPE=Release',
               '-DPORTABLE_BUILD_TESTS=OFF', '-DPORTABLE_BUILD_SDL3_APP=ON',
               '-DPORTABLE_STRICT_WARNINGS=ON', f'-DCMAKE_PREFIX_PATH={prefix}']
    if platform == 'macos':
        architectures = capture(['lipo', '-archs', sdl_library(prefix, platform)]).split()
        command += [f'-DCMAKE_OSX_DEPLOYMENT_TARGET={MAC_DEPLOYMENT}',
                    '-DCMAKE_OSX_ARCHITECTURES=' + ';'.join(sorted(architectures))]
    elif platform == 'linux':
        # CMake escapes $ORIGIN itself; the bundle keeps SDL in lib/.
        command += ['-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON', '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib']
    else:
        command += ['-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded', '-DPORTABLE_WINDOWS_GUI=ON']
    run(command)
    build_command = ['cmake', '--build', build, '--config', 'Release', '--target', 'mksc',
                     '--parallel', '4']
    if platform == 'windows':
        build_command.append('--clean-first')  # see docs/BUILDING.md (MSVC header deps)
    run(build_command)
    return find_one(build, ['mksc.exe', 'mksc', 'Release/mksc.exe'], 'built mksc')


def write_texts(directory, prefix, platform, version):
    directory.mkdir(parents=True, exist_ok=True)
    readme = (WEB_README.format(version=version, sha1=ROM_SHA1) if platform == 'web' else
              README.format(version=version, platform=platform, sha1=ROM_SHA1,
                            install=INSTALL[platform]))
    newline = '\r\n' if platform == 'windows' else '\n'
    (directory / 'README.txt').write_bytes(readme.replace('\n', newline).encode('utf-8'))
    shutil.copyfile(ROOT / 'LICENSE-NOTICE.md', directory / 'LICENSE-NOTICE.md')
    shutil.copyfile(ROOT / 'portable/third_party/lua/LICENSE', directory / 'LUA-LICENSE.txt')
    shutil.copyfile(sdl_license(prefix), directory / 'SDL3-LICENSE.txt')


def stage_macos(stage, executable, prefix, version):
    app = stage / f'{APP_NAME}.app'
    contents = app / 'Contents'
    (contents / 'MacOS').mkdir(parents=True)
    (contents / 'Frameworks').mkdir()
    (contents / 'Resources').mkdir()
    bundle_version = version[4:] if version.startswith('git-') else version
    (contents / 'Info.plist').write_text(PLIST.format(name=APP_NAME, version=bundle_version,
                                                      minimum=MAC_DEPLOYMENT))
    binary = contents / 'MacOS/mksc'
    shutil.copyfile(executable, binary)
    library = contents / 'Frameworks/libSDL3.0.dylib'
    shutil.copyfile(sdl_library(prefix, 'macos'), library)
    for path in (binary, library):
        path.chmod(0o755)
    run(['install_name_tool', '-id', '@rpath/libSDL3.0.dylib', library])
    linked = [line.split()[0] for line in capture(['otool', '-L', binary]).splitlines()[1:]]
    for name in linked:
        if 'libSDL3' in name and name != '@rpath/libSDL3.0.dylib':
            run(['install_name_tool', '-change', name, '@rpath/libSDL3.0.dylib', binary])
    rpaths, lines = [], capture(['otool', '-l', binary]).splitlines()
    for index, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            rpaths.append(lines[index + 2].split()[1])
    for rpath in dict.fromkeys(rpaths):  # fat binaries list each arch
        run(['install_name_tool', '-delete_rpath', rpath, binary])
    run(['install_name_tool', '-add_rpath', '@executable_path/../Frameworks', binary])
    write_texts(contents / 'Resources', prefix, 'macos', version)
    run(['codesign', '--force', '--sign', '-', '--timestamp=none', library])
    run(['codesign', '--force', '--sign', '-', '--timestamp=none', app])
    run(['codesign', '--verify', '--strict', '--verbose=2', app])
    return [app]


def stage_unix_or_windows(stage, executable, prefix, platform, version):
    if platform == 'linux':
        (stage / 'lib').mkdir(parents=True)
        shutil.copyfile(executable, stage / 'mksc')
        (stage / 'mksc').chmod(0o755)
        shutil.copyfile(sdl_library(prefix, platform), stage / 'lib/libSDL3.so.0')
        (stage / 'lib/libSDL3.so.0').chmod(0o755)
        dynamic = capture(['readelf', '-d', stage / 'mksc'])
        if '[$ORIGIN/lib]' not in dynamic:
            raise SystemExit('mksc lacks the $ORIGIN/lib run path:\n' + dynamic)
    else:
        stage.mkdir(parents=True)
        shutil.copyfile(executable, stage / 'mksc.exe')
        shutil.copyfile(sdl_library(prefix, platform), stage / 'SDL3.dll')
    write_texts(stage, prefix, platform, version)
    return sorted(stage.iterdir())


def stage_web(stage, build, prefix, version):
    """Copy only the standalone web runtime, never preloaded data or build output."""
    files = [Path(build) / name for name in WEB_FILES]
    for path in files:
        if not path.is_file():
            raise SystemExit(f'prebuilt web runtime file missing: {path}')
    sdl_license(prefix)  # Check required notices before creating the stage.
    stage.mkdir(parents=True)
    for source in files:
        shutil.copyfile(source, stage / source.name)
    write_texts(stage, prefix, 'web', version)
    return sorted(stage.iterdir())


def archive(stage, output, name, platform):
    if platform == 'linux':
        path = output / f'{name}.tar.gz'
        with tarfile.open(path, 'w:gz') as tar:
            for item in sorted(stage.rglob('*')):
                info = tar.gettarinfo(str(item), f'{name}/{item.relative_to(stage).as_posix()}')
                info.uid = info.gid = 0
                info.uname = info.gname = ''
                if info.isfile():
                    with open(item, 'rb') as handle:
                        tar.addfile(info, handle)
                else:
                    tar.addfile(info)
        return path
    path = output / f'{name}.zip'
    if platform == 'macos':
        # ditto keeps the bundle's symlinks, modes and signature intact.
        run(['ditto', '-c', '-k', '--norsrc', '--noextattr', '--noacl', '--keepParent', stage / f'{APP_NAME}.app', path])
        return path
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for item in sorted(stage.rglob('*')):
            if item.is_file():
                bundle.write(item, f'{name}/{item.relative_to(stage).as_posix()}')
    return path


def rom_samples(rom_path):
    """Build a complete fragment index, without writing any owner content."""
    if not rom_path:
        return None
    data = Path(rom_path).read_bytes()
    if hashlib.sha1(data).hexdigest() != ROM_SHA1:
        raise ValueError('content scan requires the verified US owner ROM (SHA-1 mismatch)')
    return RomContentIndex(data)


def check_bytes(name, data, samples):
    problems = []
    suffix = Path(name).suffix.lower()
    if suffix in FORBIDDEN_SUFFIXES:
        problems.append(f'{name}: forbidden file type {suffix}')
    if data[:8] in FORBIDDEN_MAGIC:
        problems.append(f'{name}: looks like an asset pack or image')
    if data[4:12] == GBA_LOGO or data[0xAC:0xB0] == b'AMKE':
        problems.append(f'{name}: looks like a GBA ROM')
    problems += content_matches(name, data, samples)
    return problems


def check_bundle(path, samples):
    """Scan a staged directory or a finished .zip/.tar.gz for game content."""
    path = Path(path)
    problems, count = [], 0
    if path.is_dir():
        for item in sorted(path.rglob('*')):
            if item.is_file() and not item.is_symlink():
                count += 1
                problems += check_bytes(str(item.relative_to(path)), item.read_bytes(), samples)
    elif path.suffix == '.zip':
        with zipfile.ZipFile(path) as bundle:
            for info in bundle.infolist():
                if not info.is_dir():
                    count += 1
                    problems += check_bytes(info.filename, bundle.read(info), samples)
    elif path.name.endswith('.tar.gz'):
        with tarfile.open(path) as bundle:
            for member in bundle.getmembers():
                if member.isfile():
                    count += 1
                    problems += check_bytes(member.name, bundle.extractfile(member).read(), samples)
    else:
        problems.append(f'{path}: unknown bundle type')
    if not count:
        problems.append(f'{path}: empty bundle')
    return problems, count


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sdl-prefix', type=Path, help='SDL3 install prefix (web: license only)')
    parser.add_argument('--output', type=Path, default=ROOT / '.mksc-tools/portable/runs/dist')
    builds = parser.add_mutually_exclusive_group()
    builds.add_argument('--build', type=Path, help='build directory (default check/package-OS)')
    builds.add_argument('--web-build', type=Path, help='package prebuilt mksc.html/js/wasm; no build')
    parser.add_argument('--version', default=None)
    parser.add_argument('--rom', type=Path, help='owner ROM for the deep content scan (read only)')
    parser.add_argument('--check', type=Path, nargs='+', metavar='BUNDLE',
                        help='only scan existing bundles or directories for game content')
    args = parser.parse_args()
    try:
        samples = rom_samples(args.rom)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.check:
        failed = False
        for bundle in args.check:
            problems, count = check_bundle(bundle, samples)
            for line in problems:
                print(line)
            failed |= bool(problems)
            print(f'content_check={"FAIL" if problems else "PASS"} files={count} '
                  f'rom_anchors={len(samples.anchors) if samples else 0} bundle={bundle}')
        return 1 if failed else 0
    if not args.sdl_prefix:
        parser.error('--sdl-prefix is required to package a bundle (web uses its license only)')
    platform = 'web' if args.web_build else host_platform()
    prefix = args.sdl_prefix.resolve()
    version = args.version or default_version()
    architecture = {'macos': 'universal', 'linux': 'x86_64', 'windows': 'x64', 'web': 'wasm32'}[platform]
    name = f'mksc-neo-{version}-{platform}-{architecture}'
    build = (args.build or ROOT / f'.mksc-tools/portable/check/package-{platform}').resolve()
    output = args.output.resolve()
    stage = output / name
    if args.web_build:
        web_build = args.web_build.resolve()
        if web_build == stage or stage in web_build.parents:
            parser.error('--web-build must not be inside the output staging directory')
    if stage.exists():
        shutil.rmtree(stage)
    output.mkdir(parents=True, exist_ok=True)
    if platform == 'web':
        stage_web(stage, args.web_build.resolve(), prefix, version)
    else:
        executable = configure_and_build(build, prefix, platform)
        if platform == 'macos':
            stage_macos(stage, executable, prefix, version)
        else:
            stage_unix_or_windows(stage, executable, prefix, platform, version)
    problems, count = check_bundle(stage, samples)
    if problems:
        print('\n'.join(problems))
        raise SystemExit('bundle content check failed')
    bundle = archive(stage, output, name, platform)
    problems, count = check_bundle(bundle, samples)
    if problems:
        bundle.unlink()
        print('\n'.join(problems))
        raise SystemExit('archive content check failed')
    if platform == 'macos':
        architectures = capture(['lipo', '-archs', stage / f'{APP_NAME}.app/Contents/MacOS/mksc']).strip()
        print(f'architectures={architectures} minimum_macos={MAC_DEPLOYMENT}')
    print(f'bundle={bundle} bytes={bundle.stat().st_size} files={count} '
          f'content_check=PASS rom_anchors={len(samples.anchors) if samples else 0}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

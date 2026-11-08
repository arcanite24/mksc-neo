#!/usr/bin/env python3
"""Stable native-port validation commands; no ROM, debugger or SDL required.

Use --sdl only for a separately qualified interactive host build.
Builds and full logs stay in .mksc-tools/portable/check/<profile>.
"""
import argparse
import json
from pathlib import Path
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMMAND_SECONDS = 180
# Runs are headless, so CTest may use every core.
CTEST_JOBS = os.cpu_count() or 2
# Real-device GPU routes share this many slots (PORTABLE_GPU_ROUTE).
GPU_SLOTS = 4
# One CTest command owns the whole suite: shared fixtures run once and every
# failure is reported. Individual tests keep their own CTest timeouts.
CTEST_SECONDS = 1800
CTEST = object()  # placeholder for the ctest runs in the command list
PROFILES = {
    'debug': 'Debug',
    'release': 'Release',
    'sanitize': 'Debug',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', choices=[*PROFILES, 'all'], default='debug', nargs='?')
    parser.add_argument('--sdl', action='store_true')
    parser.add_argument('--game-pack', type=Path, help='with --sdl, also CTest the imported title/TT/results/GP route')
    parser.add_argument('--rom', type=Path, help='owner ROM: also CTest the native importer against import_rom.py (and, with --sdl, the first-start screen)')
    parser.add_argument('--mkds-rom', type=Path, help='owner Mario Kart DS (US) ROM: also CTest the game importer, its CLI and (with --sdl and --rom) the Mods tab')
    parser.add_argument('--mk64-rom', type=Path, help='read-only owner Mario Kart 64 (US) ROM: importer and CLI checks')
    parser.add_argument('--test', help='CTest regex for focused iteration; omit for closeout')
    parser.add_argument('--jobs', type=int, default=CTEST_JOBS, help='parallel CTest jobs (default: CPU count)')
    parser.add_argument('--gpu-slots', type=int, default=GPU_SLOTS, help='parallel GPU/GLES route tests')
    args = parser.parse_args()
    if args.game_pack and (not args.sdl or not args.game_pack.is_file()):
        parser.error('--game-pack requires --sdl and an existing imported pack')
    if args.rom and not args.rom.is_file():
        parser.error('--rom requires an existing ROM file')
    if args.mkds_rom and not args.mkds_rom.is_file():
        parser.error('--mkds-rom requires an existing ROM file')
    if args.mk64_rom and not args.mk64_rom.is_file():
        parser.error('--mk64-rom requires an existing ROM file')
    tracked = subprocess.check_output(
        ['git', 'ls-files', '--', '.mksc-tools/portable'], cwd=ROOT, text=True).strip()
    if tracked:
        print('FAILED: portable reference/build artifacts must remain ignored:\n' + tracked, file=sys.stderr)
        return 1
    content_command = [sys.executable, str(ROOT/'tools/portable/check_repo_content.py')]
    if args.rom:
        content_command += ['--rom', str(args.rom.resolve())]
    content = subprocess.run(content_command, cwd=ROOT)
    if content.returncode:
        print('FAILED: tracked game content', file=sys.stderr)
        return content.returncode
    profiles = PROFILES if args.profile == 'all' else [args.profile]
    for profile in profiles:
        build_type = PROFILES[profile]
        build = ROOT / '.mksc-tools/portable/check' / (profile + ('-sdl' if args.sdl else ''))
        build.mkdir(parents=True, exist_ok=True)
        commands = [
            ['cmake', '-S', str(ROOT/'portable'), '-B', str(build),
             f'-DCMAKE_BUILD_TYPE={build_type}', '-DCMAKE_C_FLAGS=',
             f'-DPython3_EXECUTABLE={sys.executable}',
             '-DPORTABLE_STRICT_WARNINGS=ON',
             f'-DPORTABLE_SANITIZERS={"ON" if profile == "sanitize" else "OFF"}',
             f'-DPORTABLE_BUILD_SDL3_APP={"ON" if args.sdl else "OFF"}',
             f'-DPORTABLE_IMPORTED_GAME_PACK={args.game_pack.resolve() if args.game_pack else ""}',
             f'-DPORTABLE_ROM={args.rom.resolve() if args.rom else ""}',
             f'-DPORTABLE_MKDS_ROM={args.mkds_rom.resolve() if args.mkds_rom else ""}',
             f'-DPORTABLE_MK64_ROM={args.mk64_rom.resolve() if args.mk64_rom else ""}'],
            ['cmake', '--build', str(build), '--config', build_type, '--parallel', '4'],
            CTEST,
            [sys.executable, '-m', 'unittest', 'discover', '-s',
             str(ROOT/'tools/portable/tests'), '-p', 'test_*.py'],
        ]
        if sys.platform == 'win32':
            # Localized MSVC/Ninja combinations can silently lose header deps.
            # Always compile current headers instead of trusting stale objects.
            commands[1].append('--clean-first')
        # Multi-config generators ignore CMAKE_BUILD_TYPE; build and test the
        # requested configuration explicitly (not Visual Studio's Debug default).
        resources = build / 'ctest-resources.json'
        resources.write_text(json.dumps({'version': {'major': 1, 'minor': 0}, 'local': [
            {'gpu': [{'id': '0', 'slots': max(1, args.gpu_slots)}]}]}))
        ctest = ['ctest', '--test-dir', str(build), '-C', build_type, '--output-on-failure', '--timeout', '30',
                 '-j', str(max(1, args.jobs)), '--resource-spec-file', str(resources)]
        if args.test:
            ctest += ['-R', args.test, '--no-tests=error']
        for index, command in enumerate(commands):
            seconds = COMMAND_SECONDS
            if command is CTEST:
                command, seconds = ctest, CTEST_SECONDS
            result = bounded(profile, command, build/f'check-{index}.log', seconds)
            if result:
                return result
        print(f'{profile}: PASS (native tests and portable tooling; no gameplay acceptance)')
    return 0


def bounded(profile, command, log_path, seconds):
    """Run one check command under its wall-time bound; 0 on success."""
    print(f'{profile}: {command[0]} (log: {log_path})', flush=True)
    started = time.monotonic()
    with log_path.open('w') as log:
        try:
            result = subprocess.run(command, cwd=ROOT, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=seconds)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f'FAILED: {exc}', file=sys.stderr)
            return 1
    print(f'  {time.monotonic() - started:.0f}s of {seconds}s', flush=True)
    if result.returncode:
        print(log_path.read_text()[-8000:], file=sys.stderr)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())

"""Explore a native-converted synthetic N64 course and reject unverified ROMs.

With the owner's cartridge (--mk64-rom), import it through the real CLI and
race it: an aided three-lap Time Trial and a four-race Grand Prix with CPUs.
"""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

from test_local_video_routes import LocalVideoRoute, check, read_ppm


def verify(binary, pack, fixture, work, gpu_probe, video_driver):
    # Converted N64 courses carry vertex colours, which only the GPU/GLES
    # backends draw; without a device this route is skipped, not passed.
    probe = subprocess.run([str(gpu_probe)], timeout=30, env={**os.environ, 'SDL_VIDEO_DRIVER': video_driver,
        'SDL_AUDIO_DRIVER': 'dummy', 'SDL_MAC_BACKGROUND_APP': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0',
        'SDL_WINDOW_ACTIVATE_WHEN_RAISED': '0'})
    if probe.returncode == 77:
        print('MK64 routes skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU device probe failed its rendering checks')
    mod = work / 'mods/mario-kart-64'
    mod.mkdir(parents=True)
    subprocess.run([str(fixture), '--write-pack', str(mod / 'content.pack')], check=True, timeout=30)
    (mod / 'mod.json').write_text(json.dumps(dict(id='mario-kart-64', name='Synthetic N64',
        version='1.0.1', api_version=1, targets=['3d'], pack='content.pack')))
    original = hashlib.sha256((mod / 'content.pack').read_bytes()).digest()
    rom = work / 'synthetic.z64'
    subprocess.run([str(fixture), '--write-rom', str(rom)], check=True, timeout=30)
    env = {**os.environ, 'SDL_VIDEO_DRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy',
           'SDL_MAC_BACKGROUND_APP': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0',
           'SDL_WINDOW_ACTIVATE_WHEN_RAISED': '0', 'MKSC_DATA_DIR': str(work / 'cli-data')}
    canonical = rom.read_bytes()
    for extension, mask in [('z64', 0), ('v64', 1), ('n64', 3)]:
        source = work / f'synthetic.{extension}'
        swapped = array.array({0: 'B', 1: 'H', 3: 'I'}[mask], canonical)
        if mask:
            swapped.byteswap()
        source.write_bytes(swapped.tobytes())
        archive = work / f'{extension}.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as output:
            output.writestr('readme.txt', 'Synthetic fixture, not a game ROM.')
            output.write(source, f'cartridge.{extension.upper()}')
        for path in (source, archive):
            result = subprocess.run([str(binary), '--import-game', str(path), str(work / 'mods')],
                                    env=env, capture_output=True, text=True, timeout=30)
            check(result.returncode == 1 and 'SHA-1 mismatch' in result.stderr,
                  f'{path.name}: did not reach canonical MK64 identity check: {result.stderr}')
            check(hashlib.sha256((mod / 'content.pack').read_bytes()).digest() == original,
                  'rejected ROM replaced installed content')
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    common = dict(MKSC_DATA_DIR=str(work / 'player'), MKSC_MENU_TOUR_PERSIST_SETTINGS='1',
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360', MKSC_MENU_TOUR_DEVICE='keyboard')
    _, log = runner.run('apply', ['--menu-tour', pack, work / 'apply-save', 'DDDArrrA...',
        work / 'apply-frames', '--mods-dir', work / 'mods', '--no-classic-menus', '--short-transitions'], **common)
    check('mod selection applied: count=1' in log, 'converted N64 fixture did not apply through Mods')
    _, log = runner.run('reject-drop', ['--menu-tour', pack, work / 'drop-save', 'DDDArrrF....',
        work / 'drop-frames', '--mods-dir', work / 'mods', '--no-classic-menus', '--short-transitions'],
        **common, MKSC_MENU_TOUR_DROP_FILE=str(work / 'synthetic.n64'))
    check('game import failed:' in log and 'SHA-1 mismatch' in log, 'N64 drop did not reach native importer')
    script = 'SDAAUAAr.AE.U.R.l.S.BB...'  # Time Trial: its cup row, then Select explores.
    _, log = runner.run('explore', ['--menu-tour', pack, work / 'explore-save', script,
        work / 'explore-frames', '--mods-dir', work / 'mods', '--no-classic-menus', '--short-transitions'],
        **{**common, 'MKSC_MENU_TOUR_DEVICE': 'gamepad', 'MKSC_AID_GPU': '1',
           'SDL_VIDEO_DRIVER': video_driver})
    check('3D course preview: course=1312175105' in log and 'Menu tour: PASS' in log,
          'converted fixture did not reach ordinary exploration')
    frames = sorted((work / 'explore-frames').glob('*.ppm'))
    first = script.index('E') + 1
    before = read_ppm(frames[first])[2]
    moved = read_ppm(frames[first + 2])[2]
    def world(pixels):
        return b''.join(pixels[(y * 640 + 80) * 3:(y * 640 + 560) * 3] for y in range(95, 285))
    check(world(before) != world(moved), 'explorer controls did not move converted geometry')
    check(hashlib.sha256((mod / 'content.pack').read_bytes()).digest() == original, 'drop modified prior mod')
    print('MK64 routes: PASS synthetic conversion, Mods, explorer, raw/ZIP byte orders, rejected update')
    return 0


# Content IDs are 0x4E363400 + source course index + 1 and 0x4E364300 + cup + 1.
LUIGI_RACEWAY = 0x4E363409
MUSHROOM_CUP, MUSHROOM_COURSES = 0x4E364301, [0x4E363409, 0x4E36340A, 0x4E363407, 0x4E36340C]
PHYSICS = 0x4E363450


def race(binary, pack, rom, work, gpu_probe, video_driver):
    """Import the owner cartridge with the CLI, then race it with ordinary menu input."""
    probe = subprocess.run([str(gpu_probe)], timeout=30, env={**os.environ, 'SDL_VIDEO_DRIVER': video_driver,
        'SDL_AUDIO_DRIVER': 'dummy', 'SDL_MAC_BACKGROUND_APP': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0',
        'SDL_WINDOW_ACTIVATE_WHEN_RAISED': '0'})
    if probe.returncode == 77:
        print('MK64 race skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU device probe failed its rendering checks')
    env = {**os.environ, 'SDL_VIDEO_DRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy',
           'SDL_MAC_BACKGROUND_APP': '1', 'SDL_WINDOW_ACTIVATE_WHEN_SHOWN': '0',
           'SDL_WINDOW_ACTIVATE_WHEN_RAISED': '0', 'MKSC_DATA_DIR': str(work / 'cli-data')}
    result = subprocess.run([str(binary), '--import-game', str(rom), str(work / 'mods')],
                            env=env, capture_output=True, text=True, timeout=120)
    check(result.returncode == 0 and 'Imported Mario Kart 64: 16 courses in 4 cups race' in result.stdout,
          f'owner cartridge import failed: {result.stdout}{result.stderr}')
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    common = dict(MKSC_DATA_DIR=str(work / 'player'), MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='320x180',
                  MKSC_AID_GPU='1', SDL_VIDEO_DRIVER=video_driver)
    arguments = ['--mods-dir', work / 'mods', '--mod', 'mario-kart-64', '--no-classic-menus', '--short-transitions']
    # Select opens the 3D courses browser; G drives the ordinary race inputs
    # along the course's CPU route until the result screen.
    _, log = runner.run('time-trial', ['--menu-tour', pack, work / 'tt-save', 'SDAAUAAEA..G.B.B.',
                                       work / 'tt-frames', *arguments], **common, MKSC_MENU_TOUR_DEVICE='keyboard')
    check(f'3D Time Trial start: course={LUIGI_RACEWAY} driver=2 profile={PHYSICS} ' in log,
          'Luigi Raceway did not start a Time Trial with the imported physics profile')
    finish = re.search(rf'3D Time Trial finish: course={LUIGI_RACEWAY} ticks=(\d+) laps=3 ', log)
    check(finish and 'Menu tour: PASS' in log, 'aided Time Trial did not finish three laps')
    _, log = runner.run('grand-prix', ['--menu-tour', pack, work / 'gp-save',
                                       'SAAUAAEA.' + 'G.A.A.' * 4 + '...A.B.', work / 'gp-frames', *arguments],
                        **common, MKSC_MENU_TOUR_DEVICE='gamepad')
    races = [(int(c), int(i), int(course)) for c, i, course in
             re.findall(r'3D GP race: cup=(\d+) index=(\d+) course=(\d+)', log)]
    check(races == [(MUSHROOM_CUP, i, course) for i, course in enumerate(MUSHROOM_COURSES)],
          f'Grand Prix did not run the source Mushroom Cup in order: {races}')
    music = [int(song) for song in re.findall(r'3D audio begin: course=\d+ driver=\d+ voice=\d+ music=(\d+)', log)]
    check(len(music) == 4 and all(song & 0xc000 == 0x4000 for song in music)
          and log.count(' set=0x4e363453 ') == 4, 'MK64 GP races did not load MK64 music and sound set')
    times = re.findall(r'3D GP time: racer=(\d+) driver=\d+ rank=\d+ ticks=\d+ estimated=([01]) '
                       r'actual_finish_ticks=\d+ lap=(\d+) stage=\d+ cpu_visits=(\d+)', log)
    check(len(times) == 32 and all(int(racer) == 1 or int(visits) > 100 for racer, _, _, visits in times),
          'Grand Prix CPUs did not race their converted routes')
    check(re.search(rf'3D GP complete: cup={MUSHROOM_CUP} rank=\d+ points=\d+', log) and 'Menu tour: PASS' in log,
          'Grand Prix did not reach the cup ceremony')
    print(f'MK64 race: PASS owner import, Time Trial ticks={finish.group(1)}, Mushroom Cup GP with CPUs')
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('binary', 'pack', 'fixture', 'work', 'gpu-probe'):
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--video-driver', required=True)
    parser.add_argument('--mk64-rom', type=Path, help='owner Mario Kart 64 (US) cartridge: race it instead')
    args = parser.parse_args()
    if args.mk64_rom:
        sys.exit(race(*(getattr(args, name).resolve() for name in ('binary', 'pack', 'mk64_rom', 'work', 'gpu_probe')),
                      args.video_driver))
    sys.exit(verify(*(getattr(args, name).resolve() for name in ('binary', 'pack', 'fixture', 'work', 'gpu_probe')),
                    args.video_driver))

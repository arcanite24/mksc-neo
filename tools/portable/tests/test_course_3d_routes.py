"""Compile, enable and explore a ROM-free mesh course through ordinary menus."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from test_local_video_routes import LocalVideoRoute, check, read_ppm


def verify(binary, pack, work):
    root = Path(__file__).resolve().parents[3]
    pack_before = hashlib.sha256(pack.read_bytes()).digest()
    subprocess.run([sys.executable, str(root / 'mods/examples/bridge-course/build.py'),
                    '--output-dir', str(work / 'mods')], check=True)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    data = work / 'player'
    common = dict(MKSC_DATA_DIR=str(data), MKSC_MENU_TOUR_PERSIST_SETTINGS='1',
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
    # Folder/ZIP discovery, then one press validates, saves and loads it.
    _, log = runner.run('apply', ['--menu-tour', pack, work / 'apply-save',
        'DDDArrrA...', work / 'apply-frames', '--mods-dir', work / 'mods',
        '--no-classic-menus', '--short-transitions'], **common)
    check('mod selection applied: count=1' in log, '3D course did not apply through Mods')
    saved = (data / 'settings.json').read_bytes()
    check(json.loads(saved)['mods'] == ['bridge-course'], '3D course selection was not saved')
    # Time Trial's ordinary character/course path: R reaches the mod's cup row,
    # A opens its cup and Select explores the course. Camera movement/rotation/
    # height and reset remain presentation-owned; B returns to the cup page.
    script = 'SDAAUAAr.AE.U.R.l.S.BB...'
    _, log = runner.run('explore', ['--menu-tour', pack, work / 'explore-save', script,
        work / 'explore-frames', '--mods-dir', work / 'mods', '--no-classic-menus',
        '--short-transitions'], **common)
    check('3D cups: groups=1 cups=1' in log and '3D course preview: course=65536' in log and
          '3D cup return: course=65536' in log and '3D course selector:' not in log,
          'saved 3D content did not reach its cup on the ordinary course screen/flythrough')
    check('Game shell Time Trial:' not in log and 'GP local result:' not in log,
          'preview unexpectedly launched a recovered race')
    frames = sorted((work / 'explore-frames').glob('*.ppm'))
    check(len(frames) == len(script) + 1, 'missing ordinary-input preview frames')
    check(all(read_ppm(frame)[:2] == (640, 360) for frame in frames), 'preview changed window layout')
    first = script.index('E') + 1  # Capture after Select enters the preview.
    first_pixels = read_ppm(frames[first])[2]
    moved_pixels = read_ppm(frames[first + 2])[2]
    # Ignore menu header/footer clocks: the world itself must move.
    def world(pixels):
        return b''.join(pixels[(y * 640 + 80) * 3:(y * 640 + 560) * 3] for y in range(95, 285))
    check(world(first_pixels) != world(moved_pixels), 'free-camera input did not change world projection')
    reset_pixels = read_ppm(frames[script.rindex('S') + 1])[2]
    check(world(first_pixels) == world(reset_pixels), 'reset did not restore the authored start view')
    colors = set(world(first_pixels)[i:i + 3] for i in range(0, len(world(first_pixels)), 3))
    check(len(colors) >= 4, 'flythrough omitted textured elevated geometry')
    check((data / 'settings.json').read_bytes() == saved, 'preview altered player settings')
    check(hashlib.sha256(pack.read_bytes()).digest() == pack_before, 'preview modified the owner base pack')
    # Recompute a valid container around malformed course bytes, so this tests
    # native semantic validation in the actual review flow, not a checksum gate.
    sys.path.insert(0, str(root / 'tools/portable'))
    import build_asset_pack
    fixture = work / 'bad-source'
    fixture.mkdir()
    sections = []
    for index, (metadata, payload) in enumerate(build_asset_pack.read_sections(
            work / 'mods/bridge-course/content.pack')):
        if metadata['kind'] == 99:
            payload[:4] = b'BAD!'
        filename = f'{index}.bin'
        (fixture / filename).write_bytes(payload)
        sections.append({**metadata, 'path': filename})
    (fixture / 'pack.json').write_text(json.dumps({'sections': sections}))
    broken = work / 'mods/broken-course'
    broken.mkdir()
    (broken / 'mod.json').write_text(json.dumps(dict(id='broken-course', name='Broken Course',
        version='1.0.0', api_version=1, targets=['3d'], pack='content.pack')))
    build_asset_pack.build_pack(fixture / 'pack.json', broken / 'content.pack')
    _, log = runner.run('reject', ['--menu-tour', pack, work / 'reject-save',
        'DDDArrrDA...', work / 'reject-frames', '--mods-dir', work / 'mods',
        '--no-classic-menus', '--short-transitions'], **common)
    check('mod selection rejected:' in log and '3D course=65536 table=header' in log,
          'malformed mesh course did not get a named rejection in ordinary Mods review')
    check((data / 'settings.json').read_bytes() == saved,
          'invalid mesh candidate changed the previously enabled selection')
    print('3D course route: PASS compile manager relaunch cup-row free-camera full-u32-ID')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    verify(args.binary.resolve(), args.pack.resolve(), args.work.resolve())

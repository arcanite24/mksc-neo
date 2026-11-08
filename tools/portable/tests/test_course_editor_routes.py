#!/usr/bin/env python3
"""Exercise Course Workshop through ordinary mksc menus and device mappings."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    mods = work / 'mods'
    mods.mkdir(exist_ok=True)
    if any(mods.iterdir()):
        raise SystemExit('Use a fresh work directory; editor routes preserve existing mods.')
    env = dict(os.environ, SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
               SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
               SDL_WINDOW_ACTIVATE_WHEN_RAISED='0', MKSC_AID_MODERN_MENUS='1',
               MKSC_MODERN_MENU_SIZE='640x360', MKSC_DATA_DIR=str(work/'data'),
               MKSC_GP_PROGRESS=str(work/'progress.bin'), MKSC_MENU_TOUR_RACES='1')

    def run(name, script, device, extra=()):
        frames = work / (name + '-frames')
        frames.mkdir()
        result = subprocess.run([str(args.binary.resolve()), '--menu-tour',
                                 str(args.pack.resolve()), str(work/'save'), script,
                                 str(frames), '--mods-dir', str(mods),
                                 '--no-classic-menus', '--short-transitions', *extra],
                                env=dict(env, MKSC_MENU_TOUR_DEVICE=device,
                                         MKSC_SCREEN_DIR=str(frames)),
                                text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        (work/(name+'.log')).write_text(result.stdout)
        if result.returncode or 'Menu tour: PASS' not in result.stdout:
            raise AssertionError(result.stdout[-6000:])
        return result.stdout

    # Options -> Mods -> Workshop. Add, move and rotate a wall; undo/redo;
    # delete it; add a ramp. Save, run an ordinary TT, return to the editor.
    edit = 'DDDArrrDDDAA.EDARRAlRrASDDDDDASDDDDDDASDDDDA.EDARA.SA.SDA.A...S.DDA.B..'
    log = run('edit-test', edit, 'gamepad')
    for evidence in ('editor saved: workshop-1 pieces=32',
                     'editor test drive: workshop-1', '3D Time Trial start:'):
        if evidence not in log:
            raise AssertionError('Missing route evidence: '+evidence+'\n'+log[-6000:])
    manifest = json.loads((mods/'workshop-1/mod.json').read_text())
    assert manifest['targets'] == ['3d'] and manifest['api_version'] == 1
    before = (mods/'workshop-1/course.pack').read_bytes()
    # The saved mod adds one row to Mods. Open the same slot and save unchanged.
    log = run('reopen', 'DDDArrrDDDDAA.SA.BB.', 'keyboard')
    assert 'editor opened: workshop-1 pieces=32' in log
    assert (mods/'workshop-1/course.pack').read_bytes() == before
    log = run('normal-mod', 'SDAAUAAEA.A...S.DDA.B.', 'keyboard',
              ('--mod', 'workshop-1'))
    assert '3D Time Trial start: course=2113929217' in log
    print('Course Workshop real executable: edit, save, TT, reopen and normal mod loading PASS')


if __name__ == '__main__':
    main()

"""Real-executable Games navigation, staged sources and persistence."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--device', choices=('keyboard', 'gamepad'), required=True)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--mk64-fixture', type=Path, help='portable_mk64_import_tests: writes a synthetic converted pack')
    parser.add_argument('--mk64-rom', type=Path, help='owner Mario Kart 64 (US) ROM for a card import')
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('MKSC_')}
    env.update(SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy', SDL_MAC_BACKGROUND_APP='1',
               SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_WINDOW_ACTIVATE_WHEN_RAISED='0',
               MKSC_DATA_DIR=str(args.work / 'data'), MKSC_GP_PROGRESS=str(args.work / 'gp.bin'),
               MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x480' if args.device == 'gamepad' else '960x540',
               MKSC_MENU_TOUR_DEVICE=args.device, MKSC_MENU_TOUR_PERSIST_SETTINGS='1')

    def tour(name, script, timeout=150, **extra):
        screens = args.work / (name + '-screens')
        screens.mkdir(exist_ok=True)
        run_env = dict(env, MKSC_SCREEN_DIR=str(screens), **extra)
        command = [str(args.binary), '--menu-tour', str(args.pack), str(args.work / 'saves'),
                   script, str(args.work / (name + '-frames')), '--no-classic-menus', '--short-transitions']
        with (args.work / (name + '.log')).open('w') as log:
            result = subprocess.run(command, env=run_env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        text = (args.work / (name + '.log')).read_text()
        assert result.returncode == 0 and 'Menu tour: PASS' in text, text[-6000:]
        assert 'Imported games library opened' in text, text[-6000:]
        return text

    # Left shoulder wraps Game to Games. Pass the Mario Kart 64 card, then
    # choose base assets in all three rows and confirm the existing transaction.
    log = tour('apply', 'DDDAlRRDRDRDRDA.A...')
    assert 'mod selection applied: count=0' in log, log[-4000:]
    path = args.work / 'data/settings.json'
    saved = path.read_bytes()
    assert json.loads(saved)['asset_sources'] == ['@base'] * 3
    tour('relaunch', 'DDDAl...')
    assert path.read_bytes() == saved
    log = tour('discard', 'DDDAlDLBDA...')
    assert 'mod changes discarded' in log, log[-4000:]
    assert path.read_bytes() == saved
    if args.rom:
        # Importing and validating the DS pack takes minutes on a loaded host.
        log = tour('import', 'DDDAlRF..DRS.A...', timeout=420, MKSC_MENU_TOUR_DROP_FILE=str(args.rom))
        assert 'game import finished: Imported Mario Kart DS' in log, log[-4000:]
        assert 'mod selection applied: count=1' in log, log[-4000:]
        imported = json.loads(path.read_bytes())
        assert imported['mods'] == ['mario-kart-ds'], imported
        assert imported['asset_sources'] == ['mario-kart-ds', '@base', '@base'], imported
        # Opening Games and focusing the DS card must not stall the menu: the
        # card art loads on a worker, so no frame of the screen takes long.
        log = tour('imported-relaunch', 'DDDAlR..R..L....', timeout=300, MKSC_STARTUP_TIMING='1')
        assert 'mod loaded: mario-kart-ds' in log, log[-4000:]
        frames = [float(m) for m in re.findall(r'games screen frame ([0-9.]+) ms', log)]
        assert frames and max(frames) < 700.0, (max(frames, default=None), log[-2000:])
    if args.mk64_fixture:
        # An imported Mario Kart 64 mod lights its card: A stages it, Apply saves.
        data = args.work / 'mk64-data'
        mod = data / 'mods/mario-kart-64'
        mod.mkdir(parents=True)
        subprocess.run([str(args.mk64_fixture), '--write-pack', str(mod / 'content.pack')], check=True, timeout=30)
        (mod / 'mod.json').write_text(json.dumps(dict(id='mario-kart-64', name='Mario Kart 64',
            version='1.0.1', api_version=1, targets=['3d'], pack='content.pack')))
        log = tour('mk64-enable', 'DDDAlRRADDDDA.A...', MKSC_DATA_DIR=str(data))
        assert 'mod selection applied: count=1' in log, log[-4000:]
        assert json.loads((data / 'settings.json').read_bytes())['mods'] == ['mario-kart-64']
    if args.mk64_rom:
        data = args.work / 'mk64-import-data'
        log = tour('mk64-import', 'DDDAlRRF..DDDDA.A...', timeout=300, MKSC_DATA_DIR=str(data),
                   MKSC_MENU_TOUR_DROP_FILE=str(args.mk64_rom))
        assert 'game import finished: Imported Mario Kart 64' in log, log[-4000:]
        assert json.loads((data / 'settings.json').read_bytes())['mods'] == ['mario-kart-64']
    print(f'Imported games: PASS device={args.device} apply/relaunch/discard/import={bool(args.rom)}'
          f' mk64-card={bool(args.mk64_fixture)} mk64-import={bool(args.mk64_rom)}')


if __name__ == '__main__':
    main()

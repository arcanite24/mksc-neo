"""Ordinary Store menus against local files; no network access required."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
from build_store import archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--video-driver', default='cocoa' if sys.platform == 'darwin' else 'windows' if sys.platform == 'win32' else 'offscreen')
    parser.add_argument('--device', choices=['keyboard', 'gamepad'], default='keyboard')
    args = parser.parse_args()
    work = args.work.resolve()
    data = work / 'data'
    data.mkdir(parents=True, exist_ok=True)
    index = work / 'index.json'
    payload = work / 'example.zip'
    entry = json.loads((ROOT / 'stores/official/index.json').read_text())['mods'][0]
    def file_url(path):
        return 'file://' + ('/' if path.drive else '') + path.as_posix()
    entry['download_url'] = file_url(payload)

    def fixture(version='1.0.0', corrupt=False):
        source = archive(entry['id'])
        if version != '1.0.0':
            out = io.BytesIO()
            with zipfile.ZipFile(io.BytesIO(source)) as old, zipfile.ZipFile(out, 'w') as new:
                for name in old.namelist():
                    content = old.read(name)
                    if name == 'mod.json':
                        manifest = json.loads(content)
                        manifest['version'] = version
                        content = json.dumps(manifest).encode()
                    new.writestr(name, content)
            source = out.getvalue()
        payload.write_bytes(source)
        entry.update(version=version, size=len(source), sha256='0'*64 if corrupt else hashlib.sha256(source).hexdigest())
        index.write_text(json.dumps(dict(format_version=1, id='fixture', name='Local fixture', mods=[entry])))
        return source

    def tour(name, script):
        frames = work / (name + '-frames')
        frames.mkdir(exist_ok=True)
        env = dict(os.environ, SDL_VIDEO_DRIVER=args.video_driver, SDL_AUDIO_DRIVER='dummy',
                   SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_WINDOW_ACTIVATE_WHEN_RAISED='0',
                   MKSC_DATA_DIR=str(data), MKSC_GP_PROGRESS=str(work / 'progress'),
                   MKSC_MENU_TOUR_PERSIST_SETTINGS='1', MKSC_AID_MODERN_MENUS='1',
                   MKSC_MODERN_MENU_SIZE='640x360', MKSC_AID_GPU='1', MKSC_MENU_TOUR_DEVICE=args.device,
                   MKSC_MENU_TOUR_DROP_FILE=str(index), MKSC_MENU_TOUR_DELAY_MS='100')
        with (work/(name+'.log')).open('w') as capture:
            run = subprocess.run([str(args.binary), '--menu-tour', str(args.pack), str(work/'save'),
                                  'DDDArrr'+script, str(frames), '--no-classic-menus', '--short-transitions'],
                                 env=env, text=True, stdout=capture, stderr=subprocess.STDOUT, timeout=120)
        output = (work/(name+'.log')).read_text()
        assert run.returncode == 0 and 'Menu tour: PASS' in output, output[-5000:]
        assert 'Menu tour device adapter: '+args.device in output, output
        return output

    installed = data / 'mods' / (entry['id']+'.zip')
    original = fixture()
    log = tour('install', 'DDA.F.AA..')
    assert 'mod archive installed:' in log and 'mod selection applied: count=1' in log, log
    assert installed.read_bytes() == original
    settings = json.loads((data/'settings.json').read_text())
    assert settings['store_urls'] == [file_url(index)] and settings['mods'] == [entry['id']]
    updated = fixture('1.1.0')
    log = tour('update', 'DDDA.F.AA..')
    assert 'mod archive installed:' in log and installed.read_bytes() == updated, log
    fixture('1.2.0')
    entry['version'] = '1.3.0'
    index.write_text(json.dumps(dict(format_version=1, id='fixture', name='Local fixture', mods=[entry])))
    log = tour('identity', 'DDDA.F.AA..')
    assert 'mod archive installed:' not in log and installed.read_bytes() == updated, log
    fixture('1.2.0', corrupt=True)
    log = tour('reject', 'DDDA.F.AA...')
    assert 'SHA-256 mismatch' in log and installed.read_bytes() == updated, log
    index.unlink()
    log = tour('offline', 'DDDA.DDDA.A.')
    assert 'store catalog: fixture (1 mods) offline' in log, log
    # Remove turns the enabled mod off, loads that and deletes it in one step.
    log = tour('remove', 'DDDA.ADAA...')
    assert 'mod selection applied: count=0' in log and 'store removed:' in log and not installed.exists(), log
    assert json.loads((data/'settings.json').read_text())['mods'] == [], log
    print('Store routes: PASS local import install update corrupt-rejection offline remove '+args.device)


if __name__ == '__main__':
    main()

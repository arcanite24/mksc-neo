#!/usr/bin/env python3
"""Build the Android APK and optionally install and launch it over adb.

  python3 tools/portable/android.py [--release] [--install] [--run] [--device SERIAL]

SDL 3.4.18 source comes from --sdl-source or SDL3_SOURCE_DIR (its
android-project also provides the Gradle wrapper). Outputs stay under
.mksc-tools/portable/check/android/.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRATCH = REPO / '.mksc-tools/portable/check/android'
DEFAULT_SDL = REPO / '.mksc-tools/portable/tools/sdl3-3.4.18-macos/SDL3-3.4.18'
PACKAGE = 'io.github.mkscneo'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--sdl-source', default=os.environ.get('SDL3_SOURCE_DIR', str(DEFAULT_SDL)))
    parser.add_argument('--release', action='store_true', help='build the release variant (debug-signed)')
    parser.add_argument('--install', action='store_true', help='install on the adb device')
    parser.add_argument('--run', action='store_true', help='install, launch and follow the game log')
    parser.add_argument('--device', help='adb serial, e.g. 192.168.1.50:5555')
    args = parser.parse_args()

    sdl = Path(args.sdl_source).resolve()
    gradlew = sdl / 'android-project' / ('gradlew.bat' if os.name == 'nt' else 'gradlew')
    if not gradlew.exists():
        sys.exit(f'SDL source not found at {sdl}; pass --sdl-source or set SDL3_SOURCE_DIR')
    env = dict(os.environ)
    if 'ANDROID_HOME' not in env and 'ANDROID_SDK_ROOT' not in env:
        default_sdk = Path.home() / ('Library/Android/sdk' if sys.platform == 'darwin' else 'Android/Sdk')
        env['ANDROID_HOME'] = str(default_sdk)
    variant = 'Release' if args.release else 'Debug'
    subprocess.run([str(gradlew), '-p', str(REPO / 'portable/android'), '--project-cache-dir',
                    str(SCRATCH / 'gradle'), f'-PsdlSource={sdl}', f'assemble{variant}', '--console=plain'],
                   check=True, env=env)
    apk = SCRATCH / 'app/outputs/apk' / variant.lower() / f'app-{variant.lower()}.apk'
    print(f'apk: {apk}')
    if not (args.install or args.run):
        return
    adb = [shutil.which('adb') or str(Path(env['ANDROID_HOME']) / 'platform-tools/adb')]
    if args.device:
        adb += ['-s', args.device]
    subprocess.run(adb + ['install', '-r', str(apk)], check=True)
    if args.run:
        subprocess.run(adb + ['shell', 'am', 'force-stop', PACKAGE], check=True)
        subprocess.run(adb + ['logcat', '-c'], check=True)
        subprocess.run(adb + ['shell', 'am', 'start', '-n', f'{PACKAGE}/.MkscActivity'], check=True)
        try:
            subprocess.run(adb + ['logcat', '-s', 'SDL', 'SDL/APP', 'SDL/ERROR', 'AndroidRuntime', 'DEBUG', 'libc'])
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()

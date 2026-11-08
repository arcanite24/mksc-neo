#!/usr/bin/env python3
"""Check tracked source/assets for accidentally embedded game content.

Without --rom, enforce container/file-type exclusions. With the owner's ROM,
also inspect binary fragments and numeric source initializers. No bytes leave
the process. This cannot establish the origin of arbitrary transformed art.
"""
import argparse
from pathlib import Path
import subprocess

from package_release import check_bytes, rom_samples

ROOT = Path(__file__).resolve().parents[2]


def check_tree(root, index):
    problems = []
    names = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
        cwd=root).decode().split('\0')
    for name in names:
        path = root / name
        if not name or not path.is_file() or path.is_symlink():
            continue
        problems += check_bytes(name, path.read_bytes(), index)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path)
    args = parser.parse_args()
    try:
        problems = check_tree(ROOT, rom_samples(args.rom))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for problem in problems:
        print(problem)
    print('repository content: ' + ('FAIL' if problems else 'PASS') +
          (' (ROM fragment scan)' if args.rom else ' (file/container checks only)'))
    return int(bool(problems))


if __name__ == '__main__':
    raise SystemExit(main())

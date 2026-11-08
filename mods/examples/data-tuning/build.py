#!/usr/bin/env python3
"""Package JSON tuning for the native loader, or export tables against an owner pack."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/portable'))
from mod_kit import default_mods_directory
from game_data_mod import build, build_runtime

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path, nargs='?', help='owner pack, required only with --compiled')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--runtime-json', action='store_true', help='package JSON for native runtime loading (default)')
    mode.add_argument('--compiled', action='store_true', help='export pre-normalized tables against the owner pack')
    parser.add_argument('--data', type=Path, default=Path(__file__).with_name('data.json'))
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'data-tuning'
    try:
        manifest = Path(__file__).with_name('mod.json')
        if args.compiled and args.base is None:
            parser.error('--compiled requires an owner base pack')
        entries = build(args.base, args.data, args.output_dir, manifest) if args.compiled else build_runtime(args.data, args.output_dir, manifest)
    except ValueError as error:
        parser.exit(1, f'data-tuning: {error}\n')
    print(f'Built data-tuning: {len(entries)} section overrides')

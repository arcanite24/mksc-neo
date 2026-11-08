#!/usr/bin/env python3
"""Build the playable native asset pack with Python and the owner's US ROM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import build_asset_pack
import ui_text_mod
from course_rom import shell_orbit_rules_section
from audio_rom import RomAudio
from build_driver_assets import driver_sections
from course_rom import Data, course_sections as race_sections, shared_sections, words, gp_player_item_sections, gp_scoring_section, mushroom_rules_section, item_status_rules_section, boo_rules_section, green_shell_rules_section, red_shell_rules_section, banana_rules_section
from ui_rom import title_sections
from countdown_rom import countdown_sections, recovery_motion_payload
from menu_rom import menu_sections
from character_menu_rom import character_sections
from course_menu_rom import course_sections
from pause_menu_rom import pause_sections
from gp_failure_menu_rom import failure_sections
from build_gp_results_pack import payload as results
from build_gp_finish_banner_pack import payload as finish_banner
from build_gp_standings_pack import payload as standings
from build_lightning_effect_pack import payload as lightning
from rom_course_art import rom_grid
from ceremony_rom import ceremony_payload

ROM_SHA1 = '9d327c030c3e2d9007990518594f70c3340ac56f'
# Kind80/ID1 import stamp (MKSC_ROM_IMPORT_VERSION in portable/import/rom_import.h).
# Bump both whenever the imported bytes change so `mksc` offers a reimport.
IMPORTER_VERSION = 28


def _import_main_courses(rom, source, result):
    headers = [source.u32(0xE7464 + index * 4) for index in range(20)]
    if len(set(headers)) != 20 or any(not 4 <= h <= 23 for h in headers):
        raise ValueError("invalid GBA cup course table")
    catalog = words(0x31544143, len(headers))
    for index, header in enumerate(headers):
        course_id = source.u32(source.pointer(0xE7FEC + header * 4))
        song = source.u32(source.pointer(0xE7FEC + header * 4) + 0x18)
        if not 0 < song <= 0xFFFF:
            raise ValueError("course music ID outside native song range")
        catalog += words(course_id, index // 4 | (index % 4) << 8 | song << 16, header)
        result += race_sections(rom, header, header != 4)
        course_id, grid = rom_grid(rom, header)
        result.append(
            (
                34,
                course_id,
                words(0x31475047, len(grid)) + b"".join(words(*row) for row in grid),
                1,
            )
        )
        result.append((23, course_id, standings(rom, course_id, index), 1))
    result.append((37, 1, catalog, 2))

def _import_extra_courses(rom, source, result):
    extra_headers = [source.u32(0xE74C4 + index * 4) for index in range(20)]
    if sorted(extra_headers) != list(range(32, 52)):
        raise ValueError("invalid Extra cup course table")
    extra_catalog = words(0x31544143, len(extra_headers))
    for index, header in enumerate(extra_headers):
        definition = source.pointer(0xE7FEC + header * 4)
        native_id = 64 + source.u32(definition)
        song = source.u32(definition + 0x18)
        if not 0 < song <= 0xFFFF:
            raise ValueError("Extra course music ID outside native song range")
        extra_catalog += words(
            native_id, index // 4 | (index % 4) << 8 | song << 16, header
        )
        result += race_sections(rom, header, True, native_id)
        _, grid = rom_grid(rom, header)
        result.append(
            (
                34,
                native_id,
                words(0x31475047, len(grid)) + b"".join(words(*row) for row in grid),
                1,
            )
        )
        result.append((23, native_id, standings(rom, native_id, index, 1), 1))
    result.append((37, 2, extra_catalog, 2))

def _import_battle_courses(rom, source, result):
    battle_headers = [source.u32(0xE7524 + index * 4) for index in range(4)]
    if sorted(battle_headers) != list(range(24, 28)):
        raise ValueError("invalid Battle course table")
    battle_catalog = words(0x31544143, len(battle_headers))
    for index, header in enumerate(battle_headers):
        definition = source.pointer(0xE7FEC + header * 4)
        native_id = 96 + source.u32(definition) - 44
        song = source.u32(definition + 0x18)
        if not 96 <= native_id <= 99 or not 0 < song <= 0xFFFF:
            raise ValueError("Battle course directory or music ID outside range")
        battle_catalog += words(native_id, index << 8 | song << 16, header)
        result += race_sections(rom, header, True, native_id, arena=True)
        result.append((23, native_id, standings(rom, native_id, index, 2), 1))
        _, grid = rom_grid(rom, header, 4)
        result.append(
            (
                34,
                native_id,
                words(0x31475047, len(grid)) + b"".join(words(*row) for row in grid),
                1,
            )
        )
    result.append((37, 3, battle_catalog, 2))

def sections(rom, importer_version=IMPORTER_VERSION):
    """One ordered construction, with no pack overlays or compiled export step.
    importer_version 0 omits the stamp, like importers before it."""
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError("import requires the unmodified US revision 0 ROM")
    result, _ = shared_sections(rom)
    result += driver_sections(rom)
    source = Data(rom)
    _import_main_courses(rom, source, result)
    # track_getMKSCTrack's Extra table starts after the four unused slots.
    # Reserve native IDs64..83 for source directory entries0..19; ID0 remains
    # the content registry's default-selection sentinel.
    _import_extra_courses(rom, source, result)
    # track_getBattleTrack uses its own four-entry catalog. Arenas reserve
    # native IDs96..99 and carry no lap gate; menus select catalog3 explicitly.
    _import_battle_courses(rom, source, result)
    # Source header28 is the award stage. It is content, but never a race-menu entry.
    result += race_sections(rom, 28, True, 100)
    _, grid = rom_grid(rom, 28)
    result.append(
        (
            34,
            100,
            words(0x31475047, len(grid)) + b"".join(words(*row) for row in grid),
            1,
        )
    )
    result.extend(gp_player_item_sections(source))
    result.append(gp_scoring_section(source))
    result.append(mushroom_rules_section(source))
    result.append(item_status_rules_section(source))
    result.append(boo_rules_section(source))
    result.append(green_shell_rules_section(source))
    result.append(red_shell_rules_section(source))
    result.append(banana_rules_section(source))
    result.extend(ui_text_mod.sections())
    result.append(shell_orbit_rules_section(source))
    # Native sixteen signed x/y bob pairs used by the recovery actor.
    result.append((38, 1, recovery_motion_payload(rom), 3))
    for importer in (
        title_sections,
        menu_sections,
        countdown_sections,
        character_sections,
        course_sections,
        pause_sections,
        failure_sections,
    ):
        result.extend(
            (kind, content_id, data, 3 if kind == 92 else 1)
            for kind, content_id, data in importer(rom)
        )
    result.extend(
        (kind, content_id, data, 3 if kind == 68 else 2 if kind in (16, 69) else 1)
        for kind, content_id, data in RomAudio(rom).sections()
    )
    result += [
        (20, 1, lightning(rom), 1),
        (21, 1, finish_banner(rom), 1),
        (22, 1, results(rom), 8),
    ]
    result.append((24, 1, ceremony_payload(rom), 4))
    if importer_version:
        result.append((80, 1, words(0x31504D49, importer_version), 1))
    return result


def build(rom_path, output, importer_version=IMPORTER_VERSION):
    rom_path, output = Path(rom_path), Path(output)
    if output.exists():
        raise ValueError(f'output already exists: {output}')
    payloads = sections(rom_path.read_bytes(), importer_version)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='rom-import-', dir=output.parent) as temporary:
        root = Path(temporary)
        manifest = []
        for kind, content_id, data, version in payloads:
            name = f'{kind}-{content_id}.bin'
            path = root / name
            if path.exists():
                raise ValueError(f'duplicate imported section {kind}/{content_id}')
            path.write_bytes(data)
            manifest.append({'kind': kind, 'content_id': content_id, 'version': version,
                             'flags': 3, 'semantic_revision': 1, 'path': name})
        source = root / 'manifest.json'
        source.write_text(json.dumps({'sections': manifest}))
        digest, deterministic = build_asset_pack.build_pack(source, root / 'game.pack')
        # Publish only the finished pack; an import failure leaves no partial output.
        (root / 'game.pack').rename(output)
    return len(payloads), digest, deterministic


def compare(rom_path, native_path, kinds=None):
    """Diff a pack written by the native importer (mksc --import) against this
    reference, section by section. Returns the list of mismatch lines."""
    expected = {(kind, content_id): (version, data)
                for kind, content_id, data, version in sections(Path(rom_path).read_bytes())}
    actual = {(meta['kind'], meta['content_id']): (meta['version'], bytes(data))
              for meta, data in build_asset_pack.read_sections(native_path)}
    problems = []
    for key in sorted(set(expected) | set(actual)):
        if kinds and key[0] not in kinds:
            continue
        if key not in actual:
            problems.append(f'{key[0]}/{key[1]}: missing from native pack')
        elif key not in expected:
            problems.append(f'{key[0]}/{key[1]}: not produced by the reference')
        elif actual[key] != expected[key]:
            (version, data), (reference_version, reference) = actual[key], expected[key]
            first = next((i for i, (a, b) in enumerate(zip(data, reference)) if a != b),
                         min(len(data), len(reference)))
            problems.append(f'{key[0]}/{key[1]}: version {version}/{reference_version} '
                            f'size {len(data)}/{len(reference)} first difference at {first:#x}')
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--compare', type=Path, metavar='NATIVE_PACK',
                        help='verify a pack from `mksc --import` against this reference instead')
    parser.add_argument('--compare-binary', type=Path, metavar='MKSC',
                        help='run `MKSC --import` into a temporary pack, then --compare it')
    parser.add_argument('--kinds', help='with --compare, only these comma-separated section kinds')
    parser.add_argument('--importer-version', type=int, default=IMPORTER_VERSION,
                        help='with --output, stamp this version (0 = no stamp, an old pack for tests)')
    args = parser.parse_args()
    if sum(value is not None for value in (args.output, args.compare, args.compare_binary)) != 1:
        parser.error('pass exactly one of --output, --compare or --compare-binary')
    if args.compare or args.compare_binary:
        try:
            kinds = {int(kind) for kind in args.kinds.split(',')} if args.kinds else None
            if args.compare:
                problems = compare(args.rom, args.compare, kinds)
            else:
                with tempfile.TemporaryDirectory(prefix='native-import-') as temporary:
                    native = Path(temporary) / 'native.pack'
                    run = subprocess.run([str(args.compare_binary), '--import', str(args.rom), str(native)],
                                         capture_output=True, text=True)
                    print(run.stdout + run.stderr, end='')
                    if run.returncode:
                        raise ValueError(f'native import exited {run.returncode}')
                    problems = compare(args.rom, native, kinds)
        except (OSError, ValueError) as error:
            parser.error(str(error))
        for line in problems:
            print(line)
        print(f'native_import={"MATCH" if not problems else "MISMATCH"} problems={len(problems)}')
        return 1 if problems else 0
    try:
        count, digest, deterministic = build(args.rom, args.output, args.importer_version)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f'pack={args.output} sections={count} courses=40 arenas=4 drivers=8')
    print(f'pack_sha256={digest}\ndeterministic_sha256={deterministic}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

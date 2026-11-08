#!/usr/bin/env python3
"""MKSC Neo modding kit: export stock data, scaffold mods, build and package them.

export-base PACK DIR [--course ID ...]
    Write an editable starting point from your imported pack: data/*.json
    (every JSON table with its stock values, each a valid data.json), courses.json
    (catalog rows, songs and which courses work as track templates),
    drivers.json, strings.json, labels.json (graphic label IDs), items.json and,
    for each --course, tracks/course-ID (track folder) plus data/cpu_items-ID.json.
new lua|data|track ID DIR [--base PACK --from COURSE] [--name NAME]
    Create DIR/ID with mod.json and main.lua, data.json or tracks/<name>/
    (a stock course re-numbered as custom track 128 and cleaned for custom use).
build MOD_DIR --base PACK [--output OUT]
    Validate the mod folder and write OUT/ID/ (installable folder) and
    OUT/ID.zip. main.lua becomes the script; data.json the JSON data section;
    tracks/*/ compile into a custom cup (cup.json: cup 0..4, name, track order).

Install by copying ID.zip (or the ID folder) into the game's mods folder and
enabling it (settings.json "mods" or --mod ID). See docs/MODDING.md.
"""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import zipfile


import build_asset_pack
import game_data_mod
import kart_physics_mod
import modern_drift_format
import model_format
import driver_affine_format
import mesh_coin_format
import mesh_sky_format
import mesh_palette_format
import course_3d_format
import mesh_kart_format
import mesh_item_format
import mesh_source_actor_format
import png_indexed
import track_format
import ui_text_mod
from text_data_mod import _GLYPHS
from ui_rom import image_asset


def default_mods_directory():
    """Resolve the native SDL_GetPrefPath('MKSC Neo', 'Portable') mods root.

    Resolving never creates directories. An explicit CLI output bypasses this
    default; library builders continue to require their output argument.
    """
    override = os.environ.get('MKSC_DATA_DIR')
    if override:
        return Path(override) / 'mods'
    if sys.platform == 'darwin':
        data = Path.home() / 'Library' / 'Application Support'
    elif sys.platform == 'win32':
        roaming = os.environ.get('APPDATA')
        data = Path(roaming) if roaming else Path.home() / 'AppData' / 'Roaming'
    else:
        xdg = os.environ.get('XDG_DATA_HOME')
        data = Path(xdg) if xdg else Path.home() / '.local' / 'share'
    return data / 'MKSC Neo' / 'Portable' / 'mods'


API_VERSION = 1
ID_PATTERN = re.compile(r'[a-z0-9][a-z0-9_-]{0,62}')
VERSION_PATTERN = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')
MANIFEST_FIELDS = {'id', 'name', 'version', 'api_version', 'pack', 'depends', 'load_after', 'script', 'targets'}
SCRIPT_LIMIT = 256 * 1024
PACK_LIMIT = 64 * 1024 * 1024
CUSTOM_CATALOG = 4
PREVIEW, NAME, SELECTED = 0x23300, 0x23400, 0x23500
CUP_NAME = 0x23200 + 15
ITEMS = ('green_shell', 'triple_green_shell', 'red_shell', 'triple_red_shell', 'spiny_shell',
         'banana', 'reserved', 'mushroom', 'triple_mushroom', 'golden_mushroom', 'star', 'boo',
         'lightning')
RULES = {  # kind: (key, magic, names)
    60: ('mushroom_rules', 0x3153554d, ('boost_ticks', 'single_actor_ticks', 'golden_actor_ticks',
                                         'acceleration_units', 'max_speed_q16')),
    61: ('item_status_rules', 0x31544153, ('star_player_ticks', 'star_cpu_ticks', 'lightning_ticks')),
    62: ('boo_rules', 0x314f4f42, ('invisibility_ticks', 'lock_ticks', 'orbit_ticks', 'orbit_step',
                                   'bob_step', 'repel_velocity_q16', 'repel_gravity_q16')),
    63: ('green_shell_rules', 0x31485347, ('launch_speed_bonus', 'max_speed', 'collision_grace_ticks',
                                           'wall_contact_budget', 'release_lock_ticks', 'cpu_hold_ticks',
                                           'destruction_velocity_q16', 'destruction_gravity_q16')),
    64: ('red_shell_rules', 0x31485352, ('launch_speed_bonus', 'max_launch_speed', 'collision_grace_ticks',
                                         'route_budget', 'release_lock_ticks', 'cpu_hold_ticks',
                                         'flight_speed_50', 'flight_speed_100', 'flight_speed_150',
                                         'acceleration', 'deceleration', 'trap_arm_ticks',
                                         'trap_lifetime_ticks', 'trap_lock_ticks',
                                         'destruction_velocity_q16', 'destruction_gravity_q16')),
    65: ('banana_rules', 0x314e4142, ('throw_speed_q16', 'cpu_flight_ticks', 'throw_velocity_q16',
                                      'throw_gravity_q16', 'collision_grace_ticks', 'release_lock_ticks',
                                      'cpu_hold_ticks', 'destruction_velocity_q16',
                                      'destruction_gravity_q16')),
}


class KitError(ValueError):
    pass


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=1) + '\n')


# ------------------------------------------------------------- base data --

def weighted(data, offset, count):
    rows = []
    for index in range(count):
        item, weight = struct.unpack_from('<HH', data, offset + index * 4)
        if (item, weight) == (0, 255) or item == 0:
            break
        if weight:  # zero-weight stock rows can never be drawn; JSON omits them
            rows.append({'item': item, 'weight': weight})
    return rows


def catalog_rows(sections, catalog):
    entry = sections.get((37, catalog))
    if entry is None:
        return []
    data = entry[1]
    count = struct.unpack_from('<I', data, 4)[0]
    return [dict(zip(('course', 'cup', 'track', 'song', 'source'), struct.unpack_from('<IBBHI', data, 8 + i * 12)))
            for i in range(count)]


def template_problems(sections, course):
    """Why a stock course cannot seed a custom track (empty list: it can)."""
    if any((kind, course) not in sections for kind in track_format.REQUIRED_KINDS):
        return ['missing course sections']
    header, rules = sections[(3, course)]
    progress = track_format.parse_rules(rules, header['version'])['progress']
    if progress == 3:
        return ['Battle arena']
    problems = [] if progress == 2 else ['demo route (not a source progress course)']
    world, materials = sections[(5, course)][1], sections[(6, course)][1][8:264]
    raster_n, map_n = struct.unpack_from('<II', world, 20)
    used = {materials[tile] for tile in world[28 + raster_n:28 + raster_n + map_n]}
    odd = sorted(value for value in used
                 if value not in track_format.MATERIAL_NAMES and not (0x40 <= value <= 0x5F or 0x80 <= value <= 0x8F))
    if odd:
        problems.append('uses undocumented materials ' + ', '.join(f'{value:#04x}' for value in odd))
    return problems


def motion_profile(data, base_offset, terrain_offset):
    """Only fields inside the JSON ranges (synthetic/odd packs may hold others)."""
    profile = {}
    base = struct.unpack_from('<I', data, base_offset)[0]
    if 1 <= base <= 2016 * 65536:
        profile['base_speed_q16'] = base
    limits = list(struct.unpack_from('<6h', data, terrain_offset))
    if all(0 <= value <= 32767 for value in limits):
        profile['terrain_limits'] = limits
    return profile


def motion_record(identity, data):
    record = {'id': identity}
    for key, (offset, count, low, high) in kart_physics_mod.ARRAYS.items():
        values = list(struct.unpack_from(f'<{count}h', data, offset))
        if all(low <= value <= high for value in values):
            record[key] = values
    time_trial = motion_profile(data, 38056, 38080)
    if time_trial:
        record['time_trial'] = time_trial
    record['engines'] = []
    for engine in range(3):
        start = 38108 + engine * 452
        profile = {'id': engine, **motion_profile(data, start + 416, start + 440)}
        for key, (offset, count) in kart_physics_mod.CPU_ARRAYS.items():
            profile[key] = list(data[start + offset:start + offset + count])
        record['engines'].append(profile)
    return record


def base_tables(sections):
    """{file stem: data.json fragment} with stock values from the pack."""
    tables = {'engine_speed_percent': {'engine_speed_percent': {'50': 100, '100': 100, '150': 100}}}
    if (59, 1) in sections:
        header, data = sections[(59, 1)]
        tables['gp_points'] = {'gp_points': {'id': 1, 'awards': list(data[8:16])},
                               'gp_rules': {'id': 1, 'retry_credits': struct.unpack_from('<I', data, 16)[0]
                                            if header['version'] >= 2 else 3}}
    gp = [{'id': i, 'weights': weighted(sections[(58, i)][1], 8, 16)} for i in range(1, 12) if (58, i) in sections]
    if gp:
        tables['gp_items'] = {'gp_items': gp}
    for kind, (key, magic, names) in RULES.items():
        if (kind, 1) in sections:
            data = sections[(kind, 1)][1]
            if struct.unpack_from('<I', data)[0] == magic:
                values = struct.unpack_from(f'<{len(names)}I', data, 8)
                tables[key] = {key: {'id': 1, **dict(zip(names, values))}}
    materials = sorted((identity, header['version'], data) for (kind, identity), (header, data) in sections.items()
                       if kind == 6)
    if (67, 1) in sections:
        data = sections[(67, 1)][1]
        orbit = {'id': 1, **dict(zip(('radius_multiplier', 'angle_step', 'animation_period'),
                                     struct.unpack_from('<3I', data, 8)))}
        if materials and materials[0][1] >= 10:
            orbit['green_delays'] = list(materials[0][2][1464:1467])
            orbit['red_delays'] = list(materials[0][2][1467:1470])
        tables['shell_orbit_rules'] = {'shell_orbit_rules': orbit}
    battle = next((data for _, version, data in materials if version >= 13), None)
    if battle is not None and all(weighted(battle, 3680 + i * 40, 10) for i in range(3)):
        tables['battle_items'] = {'battle_items': [weighted(battle, 3680 + i * 40, 10) for i in range(3)]}
    cups = []
    for catalog in (1, 2):
        by_cup = collections.defaultdict(dict)
        for row in catalog_rows(sections, catalog):
            by_cup[row['cup']][row['track']] = row['course']
        cups += [{'catalog': catalog, 'cup': cup, 'courses': [tracks[t] for t in sorted(tracks)]}
                 for cup, tracks in sorted(by_cup.items()) if len(tracks) == 4]
    if cups:
        tables['cups'] = {'cups': cups}
    for (kind, identity), (header, data) in sorted(sections.items()):
        if kind == 13 and header['version'] in (6, 7, 8):
            tables[f'kart_physics-{identity}'] = {'kart_physics': [motion_record(identity, data)]}
    return tables


def cpu_items(sections, course):
    header, data = sections[(6, course)]
    if header['version'] < 6:
        return None
    records = []
    for engine in range(3):
        for variant in range(3):
            for driver in range(8):
                offset = 288 + (((engine * 3 + variant) * 8 + driver) * 8) * 2
                records.append({'course': course, 'engine': engine, 'variant': variant,
                                'driver_class': driver, 'items': list(struct.unpack_from('<8H', data, offset))})
    return {'cpu_items': records}


def export_base(pack, directory, courses=()):
    sections = track_format.load_pack(pack)
    directory = Path(directory)
    for stem, fragment in base_tables(sections).items():
        path = directory / 'data' / f'{stem}.json'
        write_json(path, fragment)
        game_data_mod.load(path)  # every exported fragment is a valid data.json
    catalog = []
    for number in (1, 2, 3):
        for row in catalog_rows(sections, number):
            entry = {'catalog': number, **row}
            if number != 3:
                entry['template_problems'] = template_problems(sections, row['course'])
            catalog.append(entry)
    write_json(directory / 'courses.json', {
        'note': 'catalog 1 = GBA cups, 2 = Extra (SNES) cups, 3 = Battle; cup/track are menu slots. '
                'A course with no template_problems can seed a custom track (new track --from).',
        'courses': catalog})
    drivers = []
    for (kind, identity), (_, data) in sorted(sections.items()):
        if kind == 2 and data[:4] == b'CHR1':
            _, _, revision, graphics, motion, seat = struct.unpack_from('<6I', data)
            drivers.append({'id': identity, 'class': seat, 'graphics': graphics, 'motion': motion,
                            'kart_physics_file': f'data/kart_physics-{motion}.json'})
    write_json(directory / 'drivers.json', {
        'note': 'Stock drivers 1..8; class is the handling/seat class 0..7. New drivers use IDs 9..255.',
        'drivers': drivers})
    strings = []
    for identity, default in sorted(ui_text_mod.CATALOG.items()):
        section = sections.get((66, identity))
        value = section[1][8:8 + struct.unpack_from('<I', section[1], 4)[0]].decode('ascii') if section else default
        strings.append({'id': identity, 'value': value})
    write_json(directory / 'strings.json', {
        'note': 'Copy at most 64 records into a data.json "strings" list; values may not grow longer.',
        'strings': strings})
    labels = {'mode_medallions': {'gp': 0x21100, 'time_trial': 0x21101, 'quick_run': 0x21102},
              'character_names_by_menu_slot': {str(slot): 0x22200 + slot for slot in range(9) if slot != 4},
              'course_names': [{'catalog_slot': cup * 4 + track + (20 if row['catalog'] == 2 else 0),
                                'course': row['course'], 'name': NAME + cup * 4 + track + (20 if row['catalog'] == 2 else 0),
                                'selected_name': SELECTED + cup * 4 + track + (20 if row['catalog'] == 2 else 0)}
                               for row in catalog if row['catalog'] in (1, 2)
                               for cup, track in [(row['cup'], row['track'])]]}
    write_json(directory / 'labels.json', {
        'note': 'Frontend graphic IDs a data.json "text" record can relabel (decimal).', **labels})
    write_json(directory / 'items.json', {
        'note': 'Lua item names and the JSON item IDs used by gp_items, battle_items and cpu_items.',
        'items': [{'name': name, 'id': 0x1000 + index} for index, name in enumerate(ITEMS)]})
    for course in courses:
        if (6, course) not in sections:
            raise KitError(f'--course {course}: not a course in this pack (see courses.json)')
        try:
            track_format.export_course(sections, course, directory / 'tracks' / f'course-{course}')
        except track_format.TrackError as error:
            print(f'note: course {course} has no exportable track folder ({error})', file=sys.stderr)
        items = cpu_items(sections, course)
        if items is not None:
            path = directory / 'data' / f'cpu_items-{course}.json'
            write_json(path, items)
            game_data_mod.load(path)
    return directory


# ------------------------------------------------------------ scaffolds --

LUA_TEMPLATE = '''-- {name}: a Lua mod for MKSC Neo (API version 1). See docs/MODDING.md.
-- Hooks are global functions; delete the ones you do not need.

function on_race_start(race)
  race.message("{upper}", 120)
end

function on_lap(kart, lap)
  -- kart 1 is the local player; lap counts completed laps (race.laps = finish)
  if kart.player and lap == race.laps - 1 then
    if race.give_item(kart.index, "mushroom") then
      race.message("Final lap mushroom!", 120)
    end
  end
end

function on_item_use(kart, item)
  -- the player or a CPU (kart.player false) uses its item; in Time Trial
  -- only keep or cancel. Return nil/true to keep, a name to replace, false to cancel
  return nil
end

function on_finish(kart, place)
  print("kart " .. kart.index .. " finished " .. place .. " at tick " .. race.tick)
end
'''


def manifest_template(mod_id, name):
    return {'id': mod_id, 'name': name, 'version': '1.0.0', 'api_version': API_VERSION}


def clean_template_tilemap(folder):
    """Replace object alias tiles 0xD0..0xDF with the most common tile of the same material."""
    tileset = json.loads((folder / 'tileset.json').read_text())
    materials = [track_format._material(entry, index) for index, entry in enumerate(tileset['materials'])]
    width, height, pixels, palette = png_indexed.read(folder / 'tilemap.png')
    pixels = bytearray(pixels)
    counts = collections.Counter(tile for tile in pixels if tile not in track_format.ALIAS_TILES)
    replaced = 0
    for index, tile in enumerate(pixels):
        if tile in track_format.ALIAS_TILES:
            same = [candidate for candidate, _ in counts.most_common() if materials[candidate] == materials[tile]]
            pixels[index] = same[0] if same else counts.most_common(1)[0][0]
            replaced += 1
    if replaced:
        png_indexed.write(folder / 'tilemap.png', width, height, bytes(pixels), palette)
    return replaced


def new_mod(kind, mod_id, directory, name=None, base=None, source_course=None, track_id=128):
    if not ID_PATTERN.fullmatch(mod_id):
        raise KitError(f'{mod_id}: ids use a-z, 0-9, "-" and "_" (first a letter or digit, at most 63)')
    root = Path(directory) / mod_id
    if root.exists():
        raise KitError(f'{root} already exists')
    name = name or mod_id.replace('-', ' ').replace('_', ' ').title()
    if kind == 'track':
        if base is None or source_course is None:
            raise KitError('new track needs --base PACK and --from COURSE (see courses.json from export-base)')
        sections = track_format.load_pack(base)
        problems = template_problems(sections, source_course)
        if problems:
            usable = sorted({row['course'] for number in (1, 2) for row in catalog_rows(sections, number)
                             if not template_problems(sections, row['course'])})
            raise KitError(f'course {source_course} cannot seed a custom track ({", ".join(problems)}); '
                           f'usable templates: {usable}')
        slug = re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-') or 'track'
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / slug
            track = track_format.export_course(sections, source_course, folder)
            clean_template_tilemap(folder)
            track.update(id=track_id, name=name.upper()[:12])
            write_json(folder / 'track.json', track)
            (folder / 'name.png').unlink(missing_ok=True)   # derived from the new name
            track_format.compile_track(folder, sections)    # the scaffold must already build
            shutil.copytree(folder, root / 'tracks' / slug)
        write_json(root / 'cup.json', {'cup': 0, 'name': name.upper()[:12], 'tracks': [slug]})
    root.mkdir(parents=True, exist_ok=True)
    write_json(root / 'mod.json', manifest_template(mod_id, name))
    if kind == 'lua':
        (root / 'main.lua').write_text(LUA_TEMPLATE.format(name=name, upper=name.upper()[:40]))
    elif kind == 'data':
        write_json(root / 'data.json', {'engine_speed_percent': {'150': 105},
                                        'gp_points': {'id': 1, 'awards': [10, 6, 4, 2, 1, 0, 0, 0]}})
    return root


# ---------------------------------------------------------------- build --

def validate_manifest(manifest):
    if type(manifest) is not dict:
        raise KitError('mod.json: expected an object')
    unknown = set(manifest) - MANIFEST_FIELDS
    if unknown:
        raise KitError(f'mod.json: unknown fields {sorted(unknown)}')
    mod_id = manifest.get('id')
    if type(mod_id) is not str or not ID_PATTERN.fullmatch(mod_id):
        raise KitError('mod.json id: a-z, 0-9, "-", "_"; first a letter or digit; at most 63')
    name = manifest.get('name')
    if type(name) is not str or not name or not name.isascii() or len(name) > 95:
        raise KitError('mod.json name: 1..95 ASCII characters')
    if type(manifest.get('version')) is not str or not VERSION_PATTERN.fullmatch(manifest['version']) or \
            len(manifest['version']) > 31:
        raise KitError('mod.json version: three numbers like 1.0.0')
    if manifest.get('api_version') != API_VERSION:
        raise KitError(f'mod.json api_version: must be {API_VERSION}')
    if 'targets' in manifest:
        targets = manifest['targets']
        if (type(targets) is not list or not targets or len(targets)>2 or
                any(type(target) is not str or target not in ('2d','3d') for target in targets) or
                len(set(targets)) != len(targets)):
            raise KitError('mod.json targets: distinct 2d and/or 3d entries')
    for key in ('depends', 'load_after'):
        value = manifest.get(key, [])
        if type(value) is not list or len(value) > 16 or len(set(value)) != len(value) or \
                any(type(v) is not str or not ID_PATTERN.fullmatch(v) or v == mod_id for v in value):
            raise KitError(f'mod.json {key}: up to 16 distinct other mod ids')
    return manifest


def text_image(text, width, height, fill_top, fill_bottom, outline, scale=2):
    """Menu-font label with outline, centred, as a native image section."""
    text = text.upper()
    unknown = sorted({c for c in text if c not in _GLYPHS})
    if unknown:
        raise KitError(f'{text!r}: characters {unknown} are not in the menu font (A-Z 0-9 : / . - > space)')
    while scale > 1 and len(text) * 4 * scale - scale + 2 > width:
        scale -= 1
    text_width = len(text) * 4 * scale - scale
    if text_width + 2 > width or 5 * scale + 2 > height:
        raise KitError(f'{text!r} does not fit {width}x{height}')
    ink = bytearray(width * height)
    left, top = (width - text_width) // 2, (height - 5 * scale) // 2
    for index, character in enumerate(text):
        glyph = _GLYPHS[character]
        for row in range(5):
            for column in range(3):
                if glyph & 1 << (14 - row * 3 - column):
                    for dy in range(scale):
                        for dx in range(scale):
                            ink[(top + row * scale + dy) * width + left + (index * 4 + column) * scale + dx] = 1
    pixels = [0] * (width * height)
    for y in range(height):
        for x in range(width):
            if ink[y * width + x]:
                pixels[y * width + x] = fill_top if y < height // 2 else fill_bottom
            elif any(ink[ny * width + nx] for ny in range(max(0, y - 1), min(height, y + 2))
                     for nx in range(max(0, x - 1), min(width, x + 2))):
                pixels[y * width + x] = outline
    return image_asset(width, height, pixels)


def minimap_preview(folder):
    """80x64 course preview from minimap.png (64x64), or None to keep the fallback."""
    path = folder / 'minimap.png'
    if not path.exists():
        return None
    width, height, pixels, palette = png_indexed.read(path)
    if (width, height) != (64, 64) or palette is None:
        return None
    image = [0xFF101010] * (80 * 64)
    for y in range(64):
        for x in range(64):
            index = pixels[y * 64 + x]
            if index and index < len(palette):
                r, g, b = palette[index]
                image[y * 80 + x + 8] = 0xFF000000 | r << 16 | g << 8 | b
    return image_asset(80, 64, image)


def track_sections(source, base_sections, mod_name):
    folders = sorted(path for path in (source / 'tracks').iterdir() if (path / 'track.json').is_file())
    cup_path = source / 'cup.json'
    cup = json.loads(cup_path.read_text()) if cup_path.exists() else {}
    if type(cup) is not dict or set(cup) - {'cup', 'name', 'tracks'}:
        raise KitError('cup.json: expected {"cup": 0..4, "name": "...", "tracks": [...]}')
    order = cup.get('tracks', [path.name for path in folders])
    by_name = {path.name: path for path in folders}
    if type(order) is not list or len(set(order)) != len(order) or any(name not in by_name for name in order):
        raise KitError(f'cup.json tracks: names of folders in tracks/ ({sorted(by_name)})')
    first_cup = cup.get('cup', 0)
    if type(first_cup) is not int or not 0 <= first_cup <= 4 or first_cup + (len(order) - 1) // 4 > 4:
        raise KitError('cup.json cup: 0..4, and every four tracks use the next cup (at most 20 tracks)')
    taken = {row['course'] for (kind, number) in base_sections if kind == 37 for row in catalog_rows(base_sections, number)}
    payloads, catalog, ids = [], b'', set()
    for index, name in enumerate(order):
        folder = by_name[name]
        try:
            compiled = track_format.compile_track(folder, base_sections)
        except (OSError, ValueError) as error:
            raise KitError(f'tracks/{name}: {error}') from None
        track = json.loads((folder / 'track.json').read_text())
        identity = track['id']
        if identity not in track_format.CUSTOM_IDS:
            raise KitError(f'tracks/{name}: id must be 128..191 for a custom track')
        if identity in ids or identity in taken:
            raise KitError(f'tracks/{name}: course id {identity} is already used')
        ids.add(identity)
        payloads += [(section, section['data']) for section in compiled]
        cup_index, slot = first_cup + index // 4, index % 4
        catalog += struct.pack('<IBBHI', identity, cup_index, slot, track['song'], slot)
        menu_slot = (11 + cup_index) * 4 + slot
        art = [(NAME, text_image(track['name'], 96, 16, 0xFFFF9400, 0xFFEF3900, 0xFF6B0094)),
               (SELECTED, text_image(track['name'], 96, 16, 0xFFFFEF94, 0xFFFFAD00, 0xFF945ABD))]
        preview = minimap_preview(folder)
        if preview is not None:
            art.append((PREVIEW, preview))
        payloads += [({'kind': 15, 'content_id': base_id + menu_slot, 'version': 1}, data) for base_id, data in art]
    catalog = struct.pack('<2I', 0x31544143, len(order)) + catalog
    payloads.append(({'kind': 37, 'content_id': CUSTOM_CATALOG, 'version': 2}, catalog))
    cup_name = cup.get('name', mod_name).upper()[:12]
    for cup_index in range(first_cup, first_cup + (len(order) + 3) // 4):
        payloads.append(({'kind': 15, 'content_id': CUP_NAME + cup_index, 'version': 1},
                         text_image(cup_name, 64, 16, 0xFFFF9400, 0xFFEF3900, 0xFF6B0094)))
    return payloads


def build(source, base, output):
    source, output = Path(source), Path(output)
    manifest = validate_manifest(json.loads((source / 'mod.json').read_text()))
    mod_id = manifest['id']
    if source.name != mod_id:
        print(f'note: folder {source.name} differs from id {mod_id}; the build is named {mod_id}', file=sys.stderr)
    manifest = {key: value for key, value in manifest.items() if key not in ('pack', 'script')}
    files = {}
    script = source / 'main.lua'
    if script.exists():
        text = script.read_bytes()
        if not text or len(text) > SCRIPT_LIMIT or b'\0' in text:
            raise KitError('main.lua: 1 byte..256 KiB of text')
        try:
            text.decode('utf-8')
        except UnicodeDecodeError:
            raise KitError('main.lua: must be UTF-8 text') from None
        manifest['script'] = 'main.lua'
        files['main.lua'] = text
    payloads = []
    if (source / 'data.json').exists():
        game_data_mod.load(source / 'data.json')
        payloads.append(({'kind': 57, 'content_id': 1, 'version': 1, 'flags': 3},
                         (source / 'data.json').read_bytes()))
    modern_drift = source / 'modern-drift.json'
    if modern_drift.exists():
        try:
            profile = modern_drift_format.load(modern_drift)
            rules = modern_drift_format.normalize(profile)
        except (OSError, ValueError) as error:
            raise KitError(f'modern-drift.json: {error}') from None
        payloads.append(({'kind': modern_drift_format.KIND, 'content_id': 1,
                          'version': profile['version'], 'flags': 3}, rules))
    physics = source / 'physics.json'
    if physics.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('physics.json: manifest targets must declare 3d')
        try:
            # Compilation validates the complete collection before adding any section.
            profiles = mesh_kart_format.compile(mesh_kart_format.load(physics))
            payloads.extend(profiles)
        except (OSError, ValueError) as error:
            raise KitError(f'physics.json: {error}') from None
    items = source / 'items.json'
    if items.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('items.json: manifest targets must declare 3d')
        try:
            payloads.extend(mesh_item_format.compile(mesh_item_format.load(items)))
        except (OSError, ValueError) as error:
            raise KitError(f'items.json: {error}') from None
    coins = source / 'coins.json'
    if coins.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('coins.json: manifest targets must declare 3d')
        try:
            payloads.extend(mesh_coin_format.compile(mesh_coin_format.load(coins)))
        except (OSError, ValueError) as error:
            raise KitError(f'coins.json: {error}') from None
    skies = source / 'skies.json'
    if skies.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('skies.json: manifest targets must declare 3d')
        try:
            payloads.extend(mesh_sky_format.compile(mesh_sky_format.load(skies)))
        except (OSError, ValueError) as error:
            raise KitError(f'skies.json: {error}') from None
    palettes = source / 'palettes.json'
    if palettes.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('palettes.json: manifest targets must declare 3d')
        try:
            payloads.extend(mesh_palette_format.compile(mesh_palette_format.load(palettes),source))
        except (OSError, ValueError) as error:
            raise KitError(f'palettes.json: {error}') from None
    source_actors = source / 'source-actors.json'
    if source_actors.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('source-actors.json: manifest targets must declare 3d')
        try:
            payloads.extend(mesh_source_actor_format.compile(mesh_source_actor_format.load(source_actors)))
        except (OSError, ValueError) as error:
            raise KitError(f'source-actors.json: {error}') from None
    affine = source / 'actor-affine.json'
    if affine.exists():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('actor-affine.json: manifest targets must declare 3d')
        try:
            payloads.extend(driver_affine_format.compile(driver_affine_format.load(affine)))
        except (OSError, ValueError) as error:
            raise KitError(f'actor-affine.json: {error}') from None
    models = source / 'models.json'
    if models.exists():
        try:
            payloads += model_format.compile(model_format.load(models), source)
        except (OSError, ValueError) as error:
            raise KitError(f'models.json: {error}') from None
    if (source / 'tracks').is_dir():
        if base is None:
            raise KitError('tracks/: building a track mod needs --base PACK')
        payloads += track_sections(source, track_format.load_pack(base), manifest['name'])
    courses3d = source / 'courses3d'
    if courses3d.is_dir():
        if '3d' not in manifest.get('targets', ['2d']):
            raise KitError('courses3d/: manifest targets must declare 3d')
        slots, identities = set(), set()
        for course in sorted(courses3d.glob('*/course.json')):
            try:
                sections = course_3d_format.compile(course_3d_format.load(course), course.parent)
            except (OSError, ValueError) as error:
                raise KitError(f'{course.relative_to(source)}: {error}') from None
            meta, catalog = sections[1]
            identity, cup, slot = struct.unpack_from('<3I', catalog, 16)
            if identity in identities or (cup,slot) in slots:
                raise KitError(f'{course}: duplicate 3D course ID or cup slot')
            identities.add(identity);slots.add((cup,slot))
            payloads += sections
    # Explicit shared texture IDs may recur only with identical declarations.
    unique = {}
    for meta, data in payloads:
        key = (meta['kind'],meta['content_id'])
        if key in unique and (meta['kind'] != 101 or unique[key] != (meta,data)):
            raise KitError(f'content: conflicting section kind/ID {key}')
        unique[key] = (meta,data)
    payloads = list(unique.values())
    if payloads:
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            entries = []
            for meta, data in payloads:
                name = f'{meta["kind"]}-{meta["content_id"]}.bin'
                (work / name).write_bytes(data)
                entries.append({'kind': meta['kind'], 'content_id': meta['content_id'], 'version': meta['version'],
                                'flags': meta.get('flags', 3), 'semantic_revision': meta.get('semantic_revision', 1),
                                'path': name})
            write_json(work / 'pack.json', {'sections': entries})
            build_asset_pack.build_pack(work / 'pack.json', work / 'content.pack')
            pack = (work / 'content.pack').read_bytes()
            list(build_asset_pack.read_sections(work / 'content.pack'))  # checksums verify
        if len(pack) > PACK_LIMIT:
            raise KitError('content.pack exceeds 64 MiB')
        manifest['pack'] = 'content.pack'
        files['content.pack'] = pack
    if not files:
        raise KitError(f'{source}: nothing to build; add main.lua, data.json, modern-drift.json, physics.json, coins.json, skies.json, palettes.json, source-actors.json, actor-affine.json, models.json, tracks/<name>/ or courses3d/<name>/course.json')
    files['mod.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    folder = output / mod_id
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for name, data in files.items():
        (folder / name).write_bytes(data)
    archive = output / f'{mod_id}.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.create_system = 3  # Canonical Unix attributes, independent of the build host.
            info.external_attr = 0o644 << 16
            bundle.writestr(info, files[name], compress_type=zipfile.ZIP_DEFLATED)
    return folder, archive


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    export = commands.add_parser('export-base', help='dump stock tables, ids and track folders')
    export.add_argument('pack', type=Path)
    export.add_argument('directory', type=Path)
    export.add_argument('--course', type=int, action='append', default=[], help='also export this course (repeatable)')
    new = commands.add_parser('new', help='create a mod folder')
    new.add_argument('kind', choices=('lua', 'data', 'track'))
    new.add_argument('id')
    new.add_argument('directory', type=Path)
    new.add_argument('--name')
    new.add_argument('--base', type=Path, help='track: imported pack')
    new.add_argument('--from', dest='source_course', type=int, help='track: stock course to start from')
    new.add_argument('--track-id', type=int, default=128, help='track: custom course id 128..191')
    build_parser = commands.add_parser('build', help='validate and package a mod folder')
    build_parser.add_argument('source', type=Path)
    build_parser.add_argument('--base', type=Path, help='imported pack (needed for tracks)')
    build_parser.add_argument('--output', type=Path, help='mods directory (default: native game data folder/mods)')
    args = parser.parse_args(argv)
    try:
        if args.command == 'export-base':
            export_base(args.pack, args.directory, args.course)
            print(f'exported base data to {args.directory}')
        elif args.command == 'new':
            root = new_mod(args.kind, args.id, args.directory, args.name, args.base, args.source_course, args.track_id)
            print(f'created {root}')
        else:
            output = args.output if args.output is not None else default_mods_directory()
            folder, archive = build(args.source, args.base, output)
            print(f'built {folder} and {archive}')
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f'mod_kit: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

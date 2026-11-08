"""Compile bounded JSON tuning records into existing deterministic content sections."""
import json
from pathlib import Path
import struct

import build_asset_pack
import kart_physics_mod
import text_data_mod
import ui_text_mod


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{field}: expected integer {low}..{high}')
    return value


def fields(value, allowed, field):
    if type(value) is not dict or set(value) - set(allowed):
        raise ValueError(f'{field}: expected object with fields {", ".join(allowed)}')
    return value


def unique_object(pairs):
    value = {}
    for key, entry in pairs:
        if key in value:
            raise ValueError(f'{key}: duplicate JSON field')
        value[key] = entry
    return value


def _validate_engine_speeds(data):
    speeds = fields(data.get('engine_speed_percent', {}), ('50', '100', '150'), 'engine_speed_percent')
    for engine, percent in speeds.items():
        integer(percent, 25, 200, f'engine_speed_percent.{engine}')
    return speeds


def _validate_battle_items(data):
    tables = data.get('battle_items')
    if 'battle_items' in data:
        if type(tables) is not list or len(tables) != 3:
            raise ValueError('battle_items: expected zero/one/two-plus balloon tables')
        for index, table in enumerate(tables):
            if type(table) is not list or not 1 <= len(table) <= 9:
                raise ValueError(f'battle_items[{index}]: expected 1..9 weighted entries')
            total = 0
            for row, entry in enumerate(table):
                field = f'battle_items[{index}][{row}]'
                fields(entry, ('item', 'weight'), field)
                integer(entry.get('item'), 4096, 4108, field + '.item')
                total += integer(entry.get('weight'), 1, 32767, field + '.weight')
            if total > 32767:
                raise ValueError(f'battle_items[{index}]: total weight exceeds 32767')
    return tables


def _validate_gp_items(data):
    gp_items = data.get('gp_items', [])
    if type(gp_items) is not list or len(gp_items) > 11:
        raise ValueError('gp_items: expected at most eleven table records')
    gp_seen = set()
    for record in gp_items:
        fields(record, ('id', 'weights'), 'gp_items')
        identity = integer(record.get('id'), 1, 11, 'gp_items.id')
        if identity in gp_seen:
            raise ValueError('gp_items.id: duplicate table ID')
        gp_seen.add(identity)
        weights = record.get('weights')
        if type(weights) is not list or not 1 <= len(weights) <= 15:
            raise ValueError('gp_items.weights: expected 1..15 weighted entries')
        total = 0
        for entry in weights:
            fields(entry, ('item', 'weight'), 'gp_items.weights')
            integer(entry.get('item'), 4096, 4108, 'gp_items.item')
            total += integer(entry.get('weight'), 1, 32767, 'gp_items.weight')
        if total > 32767:
            raise ValueError('gp_items.weights: total weight exceeds 32767')
    return gp_items


def _validate_gp_points(data):
    points = data.get('gp_points')
    if 'gp_points' in data:
        fields(points, ('id', 'awards'), 'gp_points')
        integer(points.get('id'), 1, 1, 'gp_points.id')
        awards = points.get('awards')
        if type(awards) is not list or len(awards) != 8:
            raise ValueError('gp_points.awards: expected eight rank awards')
        for award in awards:
            integer(award, 0, 63, 'gp_points.awards')
    return points


def _validate_shell_orbit_rules(data):
    if 'shell_orbit_rules' in data:
        names = ('radius_multiplier', 'angle_step', 'animation_period', 'green_delays', 'red_delays')
        rules = fields(data['shell_orbit_rules'], ('id', *names), 'shell_orbit_rules')
        integer(rules.get('id'), 1, 1, 'shell_orbit_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('shell_orbit_rules: at least one override required')
        for index, key in enumerate(names):
            if key not in rules:
                continue
            if index < 3:
                integer(rules[key], 1, 255 if index == 0 else 65535, 'shell_orbit_rules.' + key)
            else:
                if type(rules[key]) is not list or len(rules[key]) != 3:
                    raise ValueError('shell_orbit_rules.' + key + ': expected three byte delays')
                for value in rules[key]:
                    integer(value, 0, 255, 'shell_orbit_rules.' + key)


def _validate_banana_rules(data):
    if 'banana_rules' in data:
        names = ('throw_speed_q16', 'cpu_flight_ticks', 'throw_velocity_q16', 'throw_gravity_q16', 'collision_grace_ticks', 'release_lock_ticks', 'cpu_hold_ticks', 'destruction_velocity_q16', 'destruction_gravity_q16')
        limits = (16777215, 32767, 16777215, 16777215, 65535, 32767, 32767, 16777215, 16777215)
        rules = fields(data['banana_rules'], ('id', *names), 'banana_rules')
        integer(rules.get('id'), 1, 1, 'banana_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('banana_rules: at least one override required')
        for index, key in enumerate(names):
            if key in rules:
                integer(rules[key], 0 if index == 4 else 1, limits[index], 'banana_rules.' + key)


def _validate_red_shell_rules(data):
    if 'red_shell_rules' in data:
        names = ('launch_speed_bonus', 'max_launch_speed', 'collision_grace_ticks', 'route_budget', 'release_lock_ticks', 'cpu_hold_ticks', 'flight_speed_50', 'flight_speed_100', 'flight_speed_150', 'acceleration', 'deceleration', 'trap_arm_ticks', 'trap_lifetime_ticks', 'trap_lock_ticks', 'destruction_velocity_q16', 'destruction_gravity_q16')
        limits = (32767, 255, 65535, 255, 32767, 32767, 255, 255, 255, 255, 255, 32767, 32767, 32767, 16777215, 16777215)
        rules = fields(data['red_shell_rules'], ('id', *names), 'red_shell_rules')
        integer(rules.get('id'), 1, 1, 'red_shell_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('red_shell_rules: at least one override required')
        for index, key in enumerate(names):
            if key in rules:
                integer(rules[key], 0 if index in (0, 2) else 1, limits[index], 'red_shell_rules.' + key)


def _validate_green_shell_rules(data):
    if 'green_shell_rules' in data:
        names = ('launch_speed_bonus', 'max_speed', 'collision_grace_ticks', 'wall_contact_budget', 'release_lock_ticks', 'cpu_hold_ticks', 'destruction_velocity_q16', 'destruction_gravity_q16')
        limits = (32767, 255, 65535, 7, 32767, 32767, 16777215, 16777215)
        rules = fields(data['green_shell_rules'], ('id', *names), 'green_shell_rules')
        integer(rules.get('id'), 1, 1, 'green_shell_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('green_shell_rules: at least one override required')
        for index, key in enumerate(names):
            if key in rules:
                integer(rules[key], 0 if index in (0, 2) else 1, limits[index], 'green_shell_rules.' + key)


def _validate_boo_rules(data):
    if 'boo_rules' in data:
        names = ('invisibility_ticks', 'lock_ticks', 'orbit_ticks', 'orbit_step', 'bob_step', 'repel_velocity_q16', 'repel_gravity_q16')
        rules = fields(data['boo_rules'], ('id', *names), 'boo_rules')
        integer(rules.get('id'), 1, 1, 'boo_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('boo_rules: at least one override required')
        for index, key in enumerate(names):
            if key in rules:
                integer(rules[key], 1, 16777215 if index >= 5 else 255 if index == 2 else 32767, 'boo_rules.' + key)


def _validate_item_status_rules(data):
    if 'item_status_rules' in data:
        rules = fields(data['item_status_rules'], ('id', 'star_player_ticks', 'star_cpu_ticks', 'lightning_ticks'), 'item_status_rules')
        integer(rules.get('id'), 1, 1, 'item_status_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('item_status_rules: at least one override required')
        for key, value in rules.items():
            if key != 'id':
                integer(value, 48 if key == 'lightning_ticks' else 1, 32767, 'item_status_rules.' + key)


def _validate_mushroom_rules(data):
    if 'mushroom_rules' in data:
        rules = fields(data['mushroom_rules'], ('id', 'boost_ticks', 'single_actor_ticks', 'golden_actor_ticks', 'acceleration_units', 'max_speed_q16'), 'mushroom_rules')
        integer(rules.get('id'), 1, 1, 'mushroom_rules.id')
        if not set(rules) - {'id'}:
            raise ValueError('mushroom_rules: at least one override required')
        for key, value in rules.items():
            if key == 'id':
                continue
            integer(value, 0 if key == 'single_actor_ticks' else 1, 132120576 if key == 'max_speed_q16' else 32767 if key == 'acceleration_units' else 65535, 'mushroom_rules.' + key)


def _validate_gp_rules(data):
    if 'gp_rules' in data:
        rules = fields(data['gp_rules'], ('id', 'retry_credits'), 'gp_rules')
        integer(rules.get('id'), 1, 1, 'gp_rules.id')
        integer(rules.get('retry_credits'), 0, 3, 'gp_rules.retry_credits')


def _validate_cpu_items(data):
    cpu_items = data.get('cpu_items', [])
    if type(cpu_items) is not list or len(cpu_items) > 128:
        raise ValueError('cpu_items: expected at most128 records')
    seen_cpu = set()
    supported = {0, *range(4096, 4102), 4103, *range(4106, 4109)}
    for record in cpu_items:
        fields(record, ('course', 'engine', 'variant', 'driver_class', 'items'), 'cpu_items')
        identity = tuple((integer(record.get(key), low, high, 'cpu_items.' + key) for key, low, high in [('course', 1, 4294967295), ('engine', 0, 2), ('variant', 0, 2), ('driver_class', 0, 7)]))
        if identity in seen_cpu:
            raise ValueError('cpu_items: duplicate record identity')
        seen_cpu.add(identity)
        items = record.get('items')
        if type(items) is not list or len(items) != 8:
            raise ValueError('cpu_items.items: expected eight rank items')
        for item in items:
            integer(item, 0, 4108, 'cpu_items.items')
            if item not in supported:
                raise ValueError('cpu_items.items: unsupported CPU item ID')
    return cpu_items


def _validate_cups(data):
    cups = data.get('cups', [])
    if type(cups) is not list or len(cups) > 10:
        raise ValueError('cups: expected at most ten cup records')
    seen = set()
    for index, cup in enumerate(cups):
        field = f'cups[{index}]'
        fields(cup, ('catalog', 'cup', 'courses'), field)
        key = (integer(cup.get('catalog'), 1, 2, field + '.catalog'), integer(cup.get('cup'), 0, 4, field + '.cup'))
        if key in seen:
            raise ValueError(field + ': duplicate cup')
        seen.add(key)
        courses = cup.get('courses')
        if type(courses) is not list or len(courses) != 4:
            raise ValueError(field + '.courses: expected four distinct course IDs')
        for course in courses:
            integer(course, 1, 4294967295, field + '.courses')
        if len(set(courses)) != 4:
            raise ValueError(field + '.courses: expected four distinct course IDs')
    return cups


def load(path):
    path = Path(path)
    try:
        if path.stat().st_size > 16384:
            raise ValueError('data: exceeds 16384 bytes')
        data = json.loads(path.read_text(), object_pairs_hook=unique_object)
        fields(data, ('engine_speed_percent', 'battle_items', 'cups', 'gp_items', 'gp_points', 'cpu_items', 'kart_physics', 'gp_rules', 'mushroom_rules', 'text', 'item_status_rules', 'boo_rules', 'green_shell_rules', 'red_shell_rules', 'banana_rules', 'strings', 'shell_orbit_rules'), 'data')
        text_data_mod.validate(data.get('text',[]))
        ui_text_mod.validate(data.get('strings',[]))
        speeds = _validate_engine_speeds(data)
        tables = _validate_battle_items(data)
        gp_items = _validate_gp_items(data)
        points = _validate_gp_points(data)
        _validate_shell_orbit_rules(data)
        _validate_banana_rules(data)
        _validate_red_shell_rules(data)
        _validate_green_shell_rules(data)
        _validate_boo_rules(data)
        _validate_item_status_rules(data)
        _validate_mushroom_rules(data)
        _validate_gp_rules(data)
        cpu_items = _validate_cpu_items(data)
        karts = kart_physics_mod.validate(data.get('kart_physics', []))
        cups = _validate_cups(data)
        if not speeds and tables is None and not cups and not gp_items and points is None and not cpu_items and not karts and 'gp_rules' not in data and 'mushroom_rules' not in data and not data.get('text') and 'item_status_rules' not in data and 'boo_rules' not in data and 'green_shell_rules' not in data and 'red_shell_rules' not in data and 'banana_rules' not in data and not data.get('strings') and 'shell_orbit_rules' not in data:
            raise ValueError('data: no overrides requested')
        return data
    except (OSError, ValueError) as error:
        raise ValueError(f'{path}: {error}') from error


def motion(data, version, speeds, field="engine_speed_percent"):
    # v7 appends deceleration after v6's three fixed-size engine records.
    expected_header = (0x31544f4d, 1024, 64, 8, 8, 8, 256, 10, 16)
    expected_bytes = 36 + 4096 + 128 + 16 * 3 + 512 + 20 + 32 + 32768 + 416 + 52 + 3 * 452
    if version not in (6, 7, 8) or len(data) != expected_bytes + ({6:0,7:16,8:428}[version]) or struct.unpack_from('<9I', data) != expected_header:
        raise ValueError(f'{field}: reimport motion v6/v7/v8')
    if version >= 7 and any(value > 0 for value in struct.unpack_from('<8h', data, expected_bytes)):
        raise ValueError(f'{field}: invalid finished-driver deceleration')
    for engine, percent in speeds.items():
        start = expected_bytes - 3 * 452 + ('50', '100', '150').index(engine) * 452
        for offset in range(start + 128, start + 288):
            value = data[offset] * percent // 100
            if value > 255:
                raise ValueError(f'engine_speed_percent.{engine}: CPU target speed exceeds byte range')
            data[offset] = value
        base = struct.unpack_from('<i', data, start + 416)[0] * percent // 100
        if not 0 < base <= 2016 * 65536:
            raise ValueError(f'engine_speed_percent.{engine}: player base speed exceeds 2016 Q16')
        struct.pack_into('<i', data, start + 416, base)
        for offset in range(start + 420, start + 452, 2):
            value = struct.unpack_from('<h', data, offset)[0]
            if value < 0:
                continue
            value = value * percent // 100
            if value > 32767:
                raise ValueError(f'engine_speed_percent.{engine}: terrain limit exceeds halfword range')
            struct.pack_into('<h', data, offset, value)


def catalog(data, version, cups):
    if version != 2 or len(data) < 8:
        raise ValueError('cups: reimport catalog v2')
    magic, count = struct.unpack_from('<II', data)
    if magic != 0x31544143 or not 1 <= count <= 20 or len(data) != 8 + count * 12:
        raise ValueError('cups: invalid catalog layout')
    rows = [list(struct.unpack_from('<IBBHI', data, 8 + index * 12)) for index in range(count)]
    originals = {row[0]: row for row in rows}
    slots = {(row[1],row[2]):index for index,row in enumerate(rows)}
    catalog_id=cups[0]['catalog']
    if len(originals)!=count or len(slots)!=count or any(not row[0] or row[1]>=5 or row[2]>=4 or row[4]>=(44 if catalog_id==1 else 52) for row in rows):
        raise ValueError('cups.courses: invalid original course metadata or duplicate slot/ID')
    # Read every source from the preceding layer, including exchanges between cups.
    for cup in cups:
        if any((cup['cup'],track) not in slots for track in range(4)):
            raise ValueError('cups.courses: expected four original course slots')
        for track,identity in enumerate(cup['courses']):
            if identity not in originals:
                raise ValueError('cups.courses: course ID is absent from the selected catalog')
            row = originals[identity][:]
            row[1:3] = [cup['cup'],track]
            rows[slots[(cup['cup'],track)]] = row
    if len({row[0] for row in rows})!=count:
        raise ValueError('cups.courses: course repeated across cup slots; update both cups')
    if len({(row[1],row[4]) for row in rows})!=count:
        raise ValueError('cups.courses: source header repeated within cup')
    remap = {}
    for index, row in enumerate(rows):
        original = originals[row[0]]
        remap[row[1] * 4 + row[2]] = original[1] * 4 + original[2]
        struct.pack_into('<IBBHI', data, 8 + index * 12, *row)
    return remap


def _apply_shell_orbit_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=20 or struct.unpack_from('<II',data)!=(0x3142524f,3):
        raise ValueError('shell_orbit_rules: reimport orbit rules v1')
    for index,key in enumerate(('radius_multiplier','angle_step','animation_period')):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],1,255 if index==0 else 65535,'shell_orbit_rules.'+key)


def _apply_banana_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=44 or struct.unpack_from('<II',data)!=(0x314e4142,9):
        raise ValueError('banana_rules: reimport banana rules v1')
    names=('throw_speed_q16', 'cpu_flight_ticks', 'throw_velocity_q16', 'throw_gravity_q16', 'collision_grace_ticks', 'release_lock_ticks', 'cpu_hold_ticks', 'destruction_velocity_q16', 'destruction_gravity_q16')
    limits=(16777215, 32767, 16777215, 16777215, 65535, 32767, 32767, 16777215, 16777215)
    for index,key in enumerate(names):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],0 if index==4 else 1,limits[index],'banana_rules.'+key)
    ticks=struct.unpack_from('<I',data,28)[0]
    peak=struct.unpack_from('<I',data,16)[0]//struct.unpack_from('<I',data,20)[0]+1
    sample=min(peak,ticks)
    if 65536+sample*struct.unpack_from('<I',data,16)[0]-sample*(sample-1)*struct.unpack_from('<I',data,20)[0]//2>2147483647:
        raise ValueError('banana_rules.release_lock_ticks: flight overflows before lock clears')
    if 65536+ticks*struct.unpack_from('<I',data,16)[0]-ticks*(ticks-1)*struct.unpack_from('<I',data,20)[0]//2<=0:
        raise ValueError('banana_rules.release_lock_ticks: must clear before landing')


def _apply_red_shell_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=72 or struct.unpack_from('<II',data)!=(0x31485352,16):
        raise ValueError('red_shell_rules: reimport red shell rules v1')
    names=('launch_speed_bonus', 'max_launch_speed', 'collision_grace_ticks', 'route_budget', 'release_lock_ticks', 'cpu_hold_ticks', 'flight_speed_50', 'flight_speed_100', 'flight_speed_150', 'acceleration', 'deceleration', 'trap_arm_ticks', 'trap_lifetime_ticks', 'trap_lock_ticks', 'destruction_velocity_q16', 'destruction_gravity_q16')
    limits=(32767, 255, 65535, 255, 32767, 32767, 255, 255, 255, 255, 255, 32767, 32767, 32767, 16777215, 16777215)
    for index,key in enumerate(names):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],0 if index in (0,2) else 1,limits[index],'red_shell_rules.'+key)
    if struct.unpack_from('<I',data,24)[0]>struct.unpack_from('<I',data,52)[0]:
        raise ValueError('red_shell_rules.release_lock_ticks: must not exceed trap_arm_ticks')


def _apply_green_shell_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=40 or struct.unpack_from('<II',data)!=(0x31485347,8):
        raise ValueError('green_shell_rules: reimport green shell rules v1')
    names=('launch_speed_bonus','max_speed','collision_grace_ticks','wall_contact_budget','release_lock_ticks','cpu_hold_ticks','destruction_velocity_q16','destruction_gravity_q16')
    limits=(32767,255,65535,7,32767,32767,0xffffff,0xffffff)
    for index,key in enumerate(names):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],0 if index in (0,2) else 1,limits[index],'green_shell_rules.'+key)


def _apply_boo_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=36 or struct.unpack_from('<II',data)!=(0x314f4f42,7):
        raise ValueError('boo_rules: reimport Boo rules v1')
    for index,key in enumerate(('invisibility_ticks','lock_ticks','orbit_ticks','orbit_step','bob_step','repel_velocity_q16','repel_gravity_q16')):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],1,0xffffff if index>=5 else 255 if index==2 else 32767,'boo_rules.'+key)


def _apply_item_status_rules(meta, data, overrides):
    if meta['version']!=1 or len(data)!=20 or struct.unpack_from('<II',data)!=(0x31544153,3):
        raise ValueError('item_status_rules: reimport status rules v1')
    for index,key in enumerate(('star_player_ticks','star_cpu_ticks','lightning_ticks')):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
        integer(struct.unpack_from('<I',data,8+index*4)[0],48 if index==2 else 1,32767,'item_status_rules.'+key)


def _apply_mushroom_rules(meta, data, overrides):
    if meta['version'] != 1 or len(data) != 28 or struct.unpack_from('<II',data) != (0x3153554d,5):
        raise ValueError('mushroom_rules: reimport mushroom rules v1')
    for index,key in enumerate(('boost_ticks','single_actor_ticks','golden_actor_ticks','acceleration_units','max_speed_q16')):
        if key in overrides:
            struct.pack_into('<I',data,8+index*4,overrides[key])
    for index,key in enumerate(('boost_ticks','single_actor_ticks','golden_actor_ticks','acceleration_units','max_speed_q16')):
        integer(struct.unpack_from('<I',data,8+index*4)[0],0 if index==1 else 1,0x07e00000 if index==4 else 32767 if index==3 else 65535,'mushroom_rules.'+key)


def _remap_course_art(source_sections, art_remap, entries, payloads):
    for target, source_slot in sorted(art_remap.items()):
        for image_base in (0x23300, 0x23400, 0x23500):
            source_image = source_sections.get((15, image_base + source_slot))
            if source_image is None:
                raise ValueError('cups.courses: base pack lacks course menu thumbnail/name art')
            meta, data = source_image
            name = f'15-{image_base + target}.bin'
            entries.append(dict(meta, content_id=image_base + target, path=name))
            payloads.append((name, data))


def _apply_text_overrides(config, source_sections, entries, payloads):
    # Address final slots after any catalog artwork remap in this layer.
    for record in config.get('text', []):
        identity = record['id']
        key = (15, identity)
        section = source_sections.get(key)
        existing = next((i for i, entry in enumerate(entries)
                         if (entry['kind'], entry['content_id']) == key), None)
        if existing is not None:
            section = (entries[existing], payloads[existing][1])
        if section is None:
            raise ValueError('text.id: frontend graphic ID absent')
        meta, data = section
        data = text_data_mod.render(record, meta, data)
        name = f'15-{identity}.bin'
        entry = dict(meta, flags=meta['flags'] | 2, path=name)
        if existing is None:
            entries.append(entry)
            payloads.append((name, data))
        else:
            entries[existing] = entry
            payloads[existing] = (name, data)


def _append_strings(config, entries, payloads):
    for record in config.get('strings', []):
        identity = record['id']
        name = f'66-{identity}.bin'
        entries.append(dict(kind=66, content_id=identity, version=1, flags=3,
                            semantic_revision=1, path=name))
        payloads.append((name, ui_text_mod.payload(record['value'])))


def _transform_sections(config, sections):
    entries, payloads = [], []
    found_speeds, found_items, found_cups = False, False, set()
    gp_tables = {record["id"]: record["weights"] for record in config.get("gp_items", [])}
    found_gp = set()
    found_points = False
    found_mushroom = False
    found_status = False
    found_boo = False
    found_banana = False
    found_orbit = False
    found_orbit_material = False
    orbit=config.get('shell_orbit_rules',{})
    orbit_delays=any(key in orbit for key in ('green_delays','red_delays'))
    found_red = False
    found_green = False
    found_cpu = set()
    karts = {record['id']:record for record in config.get('kart_physics', [])}
    found_karts = set()
    source_sections = {(meta['kind'], meta['content_id']): (meta, data) for meta, data in sections}
    art_remap = {}
    for meta, data in sections:
        kind, identity = meta['kind'], meta['content_id']
        if kind == 13 and (config.get('engine_speed_percent') or identity in karts):
            motion(data, meta['version'], config.get('engine_speed_percent', {}), 'kart_physics' if identity in karts else 'engine_speed_percent')
            if identity in karts:
                kart_physics_mod.apply(data, karts[identity])
                found_karts.add(identity)
            found_speeds = True
        elif kind == 6 and (orbit_delays or config.get('battle_items') is not None or any(r['course']==identity for r in config.get('cpu_items', []))):
            if (len(data) != {6:1440,7:1448,8:1456,9:1464,10:1470,11:1502,12:3680,13:3804,14:3808,15:3832,16:3844}.get(meta['version']) or
                struct.unpack_from('<II', data) != (0x3154414d, 256)):
                raise ValueError(('battle_items' if config.get('battle_items') is not None else 'shell_orbit_rules' if orbit_delays else 'cpu_items') + ': reimport material v6 or later')
            if orbit_delays and meta['version']<10:raise ValueError('shell_orbit_rules: reimport material v10 or later')
            for family,key in enumerate(('green_delays','red_delays')):
                if key in orbit:data[1464+family*3:1467+family*3]=bytes(orbit[key])
            if orbit_delays:found_orbit_material=True
            if config.get('battle_items') is not None and meta['version'] < 13:
                raise ValueError('battle_items: reimport material v13 or later')
            for index, table in enumerate(config.get('battle_items', [])):
                rows = [(entry['item'], entry['weight']) for entry in table] + [(0, 255)]
                rows += [(0, 0)] * (10 - len(rows))
                struct.pack_into('<20H', data, 3680 + index * 40, *(value for row in rows for value in row))
            found_items = config.get('battle_items') is not None
            for index, record in enumerate(config.get('cpu_items', [])):
                if record['course'] != identity:
                    continue
                offset = ((record['engine']*3+record['variant'])*8+record['driver_class'])*8
                struct.pack_into('<8H',data,288+offset*2,*record['items'])
                found_cpu.add(index)
        elif kind == 58 and identity in gp_tables:
            if meta['version'] != 1 or len(data) != 72 or struct.unpack_from('<II', data) != (0x31575047, 16):
                raise ValueError('gp_items: reimport player odds v1')
            rows = [(entry['item'], entry['weight']) for entry in gp_tables[identity]] + [(0, 255)]
            rows += [(0, 0)] * (16 - len(rows))
            struct.pack_into('<32H', data, 8, *(value for row in rows for value in row))
            found_gp.add(identity)
        elif kind==67 and identity==1 and orbit:
            _apply_shell_orbit_rules(meta, data, orbit)
            found_orbit=True
        elif kind==65 and identity==1 and 'banana_rules' in config:
            _apply_banana_rules(meta, data, config['banana_rules'])
            found_banana=True
        elif kind==64 and identity==1 and 'red_shell_rules' in config:
            _apply_red_shell_rules(meta, data, config['red_shell_rules'])
            found_red=True
        elif kind==63 and identity==1 and 'green_shell_rules' in config:
            _apply_green_shell_rules(meta, data, config['green_shell_rules'])
            found_green=True
        elif kind==62 and identity==1 and 'boo_rules' in config:
            _apply_boo_rules(meta, data, config['boo_rules'])
            found_boo=True
        elif kind==61 and identity==1 and 'item_status_rules' in config:
            _apply_item_status_rules(meta, data, config['item_status_rules'])
            found_status=True
        elif kind == 60 and identity == 1 and 'mushroom_rules' in config:
            _apply_mushroom_rules(meta, data, config['mushroom_rules'])
            found_mushroom = True
        elif kind == 59 and identity == 1 and (config.get('gp_points') is not None or config.get('gp_rules') is not None):
            field = 'gp_rules' if 'gp_rules' in config else 'gp_points'
            if ((meta['version'],len(data)) not in ((1,16),(2,20),(3,112)) or
                struct.unpack_from('<II',data) != (0x31545047,8) or any(value>63 for value in data[8:16]) or
                (meta['version']>=2 and struct.unpack_from('<I',data,16)[0]>3)):
                raise ValueError(f'{field}: reimport scoring v1/v2/v3')
            if 'gp_rules' in config:
                if meta['version']==1:
                    data += bytes(4)
                    meta = dict(meta,version=2)
                struct.pack_into('<I',data,16,config['gp_rules']['retry_credits'])
            if 'gp_points' in config:
                data[8:16] = bytes(config['gp_points']['awards'])
            found_points = True
        elif kind == 37:
            cups = [cup for cup in config.get('cups', []) if cup['catalog'] == identity]
            if not cups:
                continue
            remap = catalog(data, meta['version'], cups)
            for target, source_slot in remap.items():
                if target != source_slot:
                    art_remap[target + (identity - 1) * 20] = source_slot + (identity - 1) * 20
            found_cups.add(identity)
        else:
            continue
        name = f'{kind}-{identity}.bin'
        entries.append(dict(meta, flags=meta['flags'] | 2, path=name))
        payloads.append((name, data))
    _remap_course_art(source_sections, art_remap, entries, payloads)
    _apply_text_overrides(config, source_sections, entries, payloads)
    _append_strings(config, entries, payloads)
    if orbit and not found_orbit:raise ValueError('shell_orbit_rules: base lacks orbit rule record; reimport')
    if orbit_delays and not found_orbit_material:raise ValueError('shell_orbit_rules: base lacks material delay records; reimport')
    if 'banana_rules' in config and not found_banana:
        raise ValueError('banana_rules: base pack lacks a banana rule record; reimport')
    if 'red_shell_rules' in config and not found_red:
        raise ValueError('red_shell_rules: base pack lacks a red/Spiny shell rule record; reimport')
    if 'green_shell_rules' in config and not found_green:
        raise ValueError('green_shell_rules: base pack lacks a green shell rule record; reimport')
    if 'boo_rules' in config and not found_boo:
        raise ValueError('boo_rules: base pack lacks a Boo rule record; reimport')
    if 'item_status_rules' in config and not found_status:
        raise ValueError('item_status_rules: base pack lacks a status rule record; reimport')
    if 'mushroom_rules' in config and not found_mushroom:
        raise ValueError('mushroom_rules: base pack lacks a mushroom rule record; reimport')
    if found_karts != set(karts):
        raise ValueError('kart_physics.id: base pack lacks a requested motion record')
    if (config.get('engine_speed_percent') and not found_speeds or
        config.get('battle_items') is not None and not found_items or
        found_cups != {cup['catalog'] for cup in config.get('cups', [])} or found_gp != set(gp_tables) or
        (config.get('gp_points') is not None or config.get('gp_rules') is not None) and not found_points or
        len(found_cpu) != len(config.get('cpu_items', []))):
        raise ValueError('data: base pack lacks a requested table')
    if not entries:
        raise ValueError('data: no overrides requested')
    return entries, payloads


def _emit_mod(output, manifest, entries, payloads):
    # Validate everything before writing an output mod.
    output.mkdir(parents=True, exist_ok=True)
    for name, data in payloads:
        (output / name).write_bytes(data)
    (output / 'pack.json').write_text(json.dumps({'sections': entries}, indent=2) + '\n')
    (output / 'mod.json').write_bytes(Path(manifest).read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')


def build(base, source, output, manifest):
    config = load(source)
    output = Path(output)
    try:
        sections = list(build_asset_pack.read_sections(base))
        entries, payloads = _transform_sections(config, sections)
    except ValueError as error:
        raise ValueError(f'{source}: {error}') from error
    _emit_mod(output, manifest, entries, payloads)
    return entries


def build_runtime(source, output, manifest):
    """Package JSON for the native loader; no extracted binary tables are shipped."""
    load(source)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'data.json').write_bytes(Path(source).read_bytes())
    entries = [{'kind': 57, 'content_id': 1, 'version': 1, 'flags': 3, 'path': 'data.json'}]
    (output / 'pack.json').write_text(json.dumps({'sections': entries}, indent=2) + '\n')
    (output / 'mod.json').write_bytes(Path(manifest).read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return entries

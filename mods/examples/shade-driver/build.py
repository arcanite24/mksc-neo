#!/usr/bin/env python3
"""Add a ninth selectable driver, built from the owner's pack.

    python3 build.py BASE.pack --output-dir DIR/mods/shade-driver

The new driver (character manifest 16, "SHADE") wears a recoloured copy of a
stock driver's karts, portraits and rotating menu kart (--look, default Luigi:
greens become purple, blues dark violet) and takes another stock driver's seat
(--seat, default Wario): that driver's class rows and CPU slot. It speaks,
honks and revs with --voice (default the look driver). Its motion section is a
copy of the seat's, and data.json raises its player base speeds by
--speed-percent through kart_physics. A kind79 roster row puts it on the
character menu's second page; name images spell --name on the menu and the
standings boards, and the race HUD shows its own recoloured portrait.
--full-resolution-only keeps every 64-pixel pose while omitting the authored
32/16-pixel alternatives, demonstrating a driver with sparse resolution banks.

Only this script and mod.json ship. Everything it writes is derived from the
owner's pack and stays local.
"""
import argparse
import colorsys
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/portable'))
from mod_kit import default_mods_directory
import build_asset_pack  # noqa: E402
import game_data_mod  # noqa: E402
import text_data_mod  # noqa: E402

HERE = Path(__file__).resolve().parent
MANIFEST, GRAPHICS, HUD, MOTION, FRONTEND, GP_HUD, DATA, ROSTER = 2, 10, 12, 13, 15, 33, 57, 79
CHARACTER_MAGIC, ROSTER_MAGIC = 0x31524843, 0x31565244
PORTRAIT, NAME, PREVIEW, ROSTER_ART = 0x22100, 0x22200, 0x22300, 0x22400
STOCK_GRID = (1, 2, 3, 4, 0, 5, 6, 7, 8)     # menu slot -> stock manifest ID
TT_SPEED, ENGINE_SPEED, ENGINE_STRIDE = 38056, 38108 + 416, 452
MAX_SPEED_Q16 = 2016 << 16


def recolor_rgb(r, g, b):
    """Greens turn purple and blues dark violet; skin, white and black stay."""
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    if s < 0.2 or v < 0.08:
        return r, g, b
    degrees = h * 360
    if 70 <= degrees < 170:
        h, s = 285 / 360, min(1.0, s * 1.05)
    elif 170 <= degrees < 265:
        h, s, v = 265 / 360, s * 0.6, v * 0.45
    else:
        return r, g, b
    return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(h, s, v))


def recolor_bgr555(color):
    r, g, b = ((color >> shift) & 31 for shift in (0, 5, 10))
    r, g, b = recolor_rgb(r * 255 // 31, g * 255 // 31, b * 255 // 31)
    return (color & 0x8000) | (r * 31 // 255) | (g * 31 // 255) << 5 | (b * 31 // 255) << 10


def image_header(data, what):
    if len(data) < 32 or data[:8] != b'MKSCIMG1':
        raise ValueError(f'{what}: expected a native image')
    header, version, width, height = struct.unpack_from('<4H', data, 8)
    reserved, offset, total = struct.unpack_from('<3I', data, 20)
    if (header, version, reserved, offset, total) != (32, 1, 0, 32, len(data)) or \
            len(data) != 32 + width * height * 4:
        raise ValueError(f'{what}: unsupported native image')
    return width, height


def recolor_image(data, what):
    width, height = image_header(data, what)
    out = bytearray(data)
    for offset in range(32, len(out), 4):
        argb = struct.unpack_from('<I', out, offset)[0]
        r, g, b = recolor_rgb((argb >> 16) & 255, (argb >> 8) & 255, argb & 255)
        struct.pack_into('<I', out, offset, (argb & 0xff000000) | r << 16 | g << 8 | b)
    return out


def animation_frames(data, what):
    if len(data) < 32 or data[:8] != b'MKSCANI1':
        raise ValueError(f'{what}: expected a native animation')
    _, header, version, count, loop, duration, offset, end, reserved = struct.unpack_from('<8sHHHHIIII', data)
    if (header, version, offset, end, reserved) != (32, 1, 32, 32 + count * 8, 0) or len(data) != end or not count:
        raise ValueError(f'{what}: unsupported native animation')
    return [struct.unpack_from('<II', data, 32 + i * 8) for i in range(count)], loop, duration


def name_image(stock, text, what):
    """The menu font in the stock labels' yellow with a dark outline, on the stock canvas."""
    image_header(stock, what)
    out = text_data_mod.render({'id': 1, 'value': text, 'color_rgb': 0xf8d800}, {'version': 1}, stock)
    width, height = struct.unpack_from('<HH', stock, 12)
    solid = [out[32 + i * 4 + 3] != 0 for i in range(width * height)]
    for y in range(height):
        for x in range(width):
            if solid[y * width + x]:
                continue
            if any(solid[ny * width + nx] for ny in range(max(0, y - 1), min(height, y + 2))
                   for nx in range(max(0, x - 1), min(width, x + 2))):
                struct.pack_into('<I', out, 32 + (y * width + x) * 4, 0xff302000)
    return out


def native_image(width, height, pixels, anchor=(0, 0)):
    return (struct.pack('<8s4H2h3I', b'MKSCIMG1', 32, 1, width, height, *anchor, 0, 32, 32 + width * height * 4) +
            struct.pack(f'<{width * height}I', *pixels))


def hud_portrait(gp_hud, hud, driver_class):
    """The class's 16x16 race HUD standings icon (kind33 page, HUD palette row 3), recoloured."""
    if len(gp_hud) < 24 + 0x400 or gp_hud[16 + driver_class] >= 8:
        raise ValueError('reimport the base pack for the GP race HUD (kind33 v2)')
    tiles = gp_hud[24:24 + 0x400]
    palette = struct.unpack_from('<16H', hud, 44 + struct.unpack_from('<I', hud, 4)[0] + 48 * 2)
    first = gp_hud[16 + driver_class] * 4
    pixels = []
    for y in range(16):
        for x in range(16):
            tile = tiles[(first + (y // 8) * 2 + x // 8) * 32:][:32]
            color = (tile[(y % 8) * 4 + (x % 8) // 2] >> ((x & 1) * 4)) & 15
            if not color:
                pixels.append(0)
                continue
            value = palette[color]
            r, g, b = recolor_rgb(*(((value >> shift) & 31) * 255 // 31 for shift in (0, 5, 10)))
            pixels.append(0xff000000 | r << 16 | g << 8 | b)
    return native_image(16, 16, pixels)


def driver_graphics_layout(data, version):
    """Validate the complete donor before selecting any authored texture banks."""
    if len(data) < 96:
        raise ValueError('driver graphics: truncated header or view mapping')
    magic, tile_bytes, colors, width, height, _, frames, slots = struct.unpack_from('<8I', data)
    has_lod = magic == 0x33524143
    if version not in (3, 4) or magic not in (0x32524143, 0x33524143):
        raise ValueError('reimport the base pack for CAR2/CAR3 v3/v4 driver art')
    if (width, height, slots) != (64, 64, 64) or tile_bytes != frames * 2048:
        raise ValueError('driver graphics: invalid dimensions, frame bytes or view count')
    if version == 3:
        if frames != 34 or colors != 256:
            raise ValueError('driver graphics: invalid v3 frame or palette count')
        lod_frames = 25 if has_lod else 0
    else:
        if not 1 <= frames <= (25 if has_lod else 34) or not 16 <= colors <= 256 or colors % 16:
            raise ValueError('driver graphics: invalid v4 frame or palette count')
        lod_frames = frames if has_lod else 0
    if any(slot >= frames or (version == 3 and has_lod and slot >= 25) for slot in data[32:96]):
        raise ValueError('driver graphics: view mapping exceeds authored frames')
    full_end = 96 + tile_bytes
    palette_start = full_end + lod_frames * (512 + 128)
    if len(data) != palette_start + colors * 2:
        raise ValueError('driver graphics: payload length does not match authored banks')
    return full_end, palette_start, colors


def build(base, output, look=2, seat=7, name='SHADE', driver_id=16, speed_percent=104, voice=None, full_resolution_only=False):
    voice = look if voice is None else voice
    if not 9 <= driver_id <= 255 or driver_id in STOCK_GRID or not 50 <= speed_percent <= 150:
        raise ValueError('expected driver ID 9..255 off the stock grid and speed 50..150%')
    if any(d not in STOCK_GRID or d == 0 for d in (look, seat, voice)):
        raise ValueError('--look, --seat and --voice name stock drivers 1..8')
    text_data_mod.validate([{'id': 1, 'value': name}])
    sections = {(meta['kind'], meta['content_id']): (meta, data) for meta, data in build_asset_pack.read_sections(base)}

    def section(kind, content_id, what):
        if (kind, content_id) not in sections:
            raise ValueError(f'base pack lacks {what} (kind{kind}/{content_id:#x})')
        return sections[(kind, content_id)]

    def manifest(content_id):
        meta, data = section(MANIFEST, content_id, f'character manifest {content_id}')
        if meta['version'] != 1 or len(data) != 24 or struct.unpack_from('<2I', data) != (CHARACTER_MAGIC, content_id):
            raise ValueError(f'character manifest {content_id}: unsupported layout')
        return struct.unpack_from('<6I', data)

    if (MANIFEST, driver_id) in sections:
        raise ValueError(f'base pack already has character manifest {driver_id}')
    _, _, _, look_graphics, _, look_class = manifest(look)
    manifest(voice)
    _, _, _, _, seat_motion, seat_class = manifest(seat)
    output.mkdir(parents=True, exist_ok=True)
    entries = []

    def emit(kind, content_id, data, meta=None, flags=3):
        path = f'{kind}-{content_id:x}.bin'
        (output / path).write_bytes(data)
        entry = {'kind': kind, 'content_id': content_id, 'version': 1, 'flags': flags, 'path': path}
        if meta is not None:
            entry.update(version=meta['version'], flags=meta['flags'],
                         semantic_revision=meta.get('semantic_revision', 1))
        entries.append(entry)

    emit(MANIFEST, driver_id, struct.pack('<6I', CHARACTER_MAGIC, driver_id, 1, driver_id, driver_id, seat_class))

    meta, graphics = section(GRAPHICS, look_graphics, f'driver graphics {look_graphics}')
    full_end, palette_start, colors = driver_graphics_layout(graphics, meta['version'])
    graphics = bytearray(graphics)
    for i in range(colors):
        offset = palette_start + i * 2
        struct.pack_into('<H', graphics, offset, recolor_bgr555(struct.unpack_from('<H', graphics, offset)[0]))
    if full_resolution_only:
        # CAR2 retains every full-size pose and its mapping. Removing alternate
        # banks changes neither animation nor the driver's logical footprint.
        graphics = graphics[:full_end] + graphics[palette_start:]
        struct.pack_into('<I', graphics, 0, 0x32524143)
    emit(GRAPHICS, driver_id, graphics, meta)

    meta, motion = section(MOTION, seat_motion, f'motion tables {seat_motion}')
    motion_tail = {6: 0, 7: 16, 8: 428}.get(meta['version'])
    if motion_tail is None or len(motion) != 39464 + motion_tail:
        raise ValueError('reimport the base pack for motion v6/v7/v8')
    emit(MOTION, driver_id, motion, meta)

    def faster(offset):
        return min(MAX_SPEED_Q16, struct.unpack_from('<I', motion, offset)[0] * speed_percent // 100)
    data = {'kart_physics': [{
        'id': driver_id,
        'time_trial': {'base_speed_q16': faster(TT_SPEED)},
        'engines': [{'id': e, 'base_speed_q16': faster(ENGINE_SPEED + e * ENGINE_STRIDE)} for e in range(3)],
    }]}
    (output / 'data.json').write_text(json.dumps(data, indent=2) + '\n')
    game_data_mod.load(output / 'data.json')
    entries.append({'kind': DATA, 'content_id': 1, 'version': 1, 'flags': 3, 'path': 'data.json'})

    # Menu art is placed for the look driver's grid slot; the roster row says so.
    art_slot = STOCK_GRID.index(look)
    art = ROSTER_ART + driver_id * 8
    frame_images = {}

    def recolored_frame(image_id):
        if image_id not in frame_images:
            meta, image = section(FRONTEND, image_id, 'menu frame image')
            frame_images[image_id] = 0x40000 + driver_id * 0x400 + len(frame_images)
            emit(FRONTEND, frame_images[image_id], recolor_image(image, f'image {image_id:#x}'), meta)
        return frame_images[image_id]

    def recolored_animation(source, target):
        meta, data = section(FRONTEND, source, 'menu animation')
        frames, loop, duration = animation_frames(data, f'animation {source:#x}')
        frames = [(recolored_frame(image), ticks) for image, ticks in frames]
        emit(FRONTEND, target, struct.pack('<8sHHHHIIII', b'MKSCANI1', 32, 1, len(frames), loop, duration,
                                           32, 32 + len(frames) * 8, 0) +
             b''.join(struct.pack('<II', image, ticks) for image, ticks in frames), meta)

    for state in range(3):
        recolored_animation(PORTRAIT + art_slot * 3 + state, art + state)
    meta, stock_name = section(FRONTEND, NAME + art_slot, 'menu name image')
    emit(FRONTEND, art + 3, name_image(stock_name, name, 'name'), meta)
    recolored_animation(PREVIEW + art_slot, art + 4)
    gp_huds = sorted(key for key in sections if key[0] == GP_HUD)
    if not gp_huds:
        raise ValueError('base pack lacks the GP race HUD (kind33)')
    _, hud = section(HUD, 1, 'HUD graphics')
    emit(FRONTEND, art + 5, hud_portrait(sections[gp_huds[0]][1], hud, look_class))
    canvas = native_image(40, 16, [0] * 40 * 16)
    emit(FRONTEND, art + 6, name_image(canvas, name, 'board name'))
    if len(frame_images) > 0x400:
        raise ValueError('too many menu frame images')

    emit(ROSTER, driver_id, struct.pack('<2II4B', ROSTER_MAGIC, 1, driver_id, art_slot, voice, 0, 0))
    (output / 'pack.json').write_text(json.dumps({'sections': entries}, indent=2) + '\n')
    (output / 'mod.json').write_bytes((HERE / 'mod.json').read_bytes())
    build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('base', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--look', type=int, default=2, help='stock driver whose art is recoloured (1..8)')
    parser.add_argument('--seat', type=int, default=7, help='stock driver whose seat, stats and voices it takes')
    parser.add_argument('--voice', type=int, help='stock driver whose voices, horn and engine it uses (default --look)')
    parser.add_argument('--name', default='SHADE', help='menu name (uppercase menu glyphs)')
    parser.add_argument('--id', type=int, default=16, help='character manifest ID, 9..255')
    parser.add_argument('--speed-percent', type=int, default=104)
    parser.add_argument('--full-resolution-only', action='store_true',
                        help='retain all 64-pixel poses without the authored 32/16-pixel alternatives')
    args = parser.parse_args()
    if args.output_dir is None:
        args.output_dir = default_mods_directory() / 'shade-driver'
    try:
        entries = build(args.base, args.output_dir, args.look, args.seat, args.name, args.id, args.speed_percent,
                        args.voice, args.full_resolution_only)
    except ValueError as error:
        parser.exit(1, f'shade-driver: {error}\n')
    print(f'Built shade-driver: {len(entries)} sections in {args.output_dir}')


if __name__ == '__main__':
    main()

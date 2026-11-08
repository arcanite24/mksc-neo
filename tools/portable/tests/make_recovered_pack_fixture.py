#!/usr/bin/env python3
"""Create a synthetic normalized recovered-race pack for C loader tests."""

import json
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from build_asset_pack import build_pack
from ui_rom import image_asset



def authored_results_payload():
    """Complete ROM-free GPR1 v8 art for GP/Quick action-window lifecycles."""
    header = struct.pack('<16I', 0x31525047, 0x4000, 256, 20, 2, 4,
                         1024, 640, 384, 20, 0x800, 10, 16, 16, 64, 8)
    tiles = bytes([0x11]) * 0x4000
    palette = struct.pack('<256H', *(0 if i % 16 == 0 else
        ((i & 31) | ((i * 3 & 31) << 5) | ((i * 7 & 31) << 10))
        for i in range(256)))
    cell = 0xE001
    tilemap = lambda count: struct.pack('<' + 'H' * count, *([cell] * count))
    pattern = lambda width, height: struct.pack('<HH', width, height) + tilemap(width * height)
    rows = [pattern(2, 2)] * 3 + [pattern(14, 1)]
    compact = [pattern(2, 1)] * 5 + [pattern(14, 1)]
    menu_colors = struct.pack('<16H', *(i * 0x421 for i in range(16)))
    return (header + tiles + palette + struct.pack('<20H', *([1] * 20)) +
            pattern(3, 2) * 2 + struct.pack('<4H', *(len(row) for row in rows)) +
            b''.join(row.ljust(32, b'\0') for row in rows) + tilemap(1024) +
            tiles + palette + tilemap(640) + tilemap(384) + tilemap(20 * 9) +
            bytes([0x11]) * 0x800 + struct.pack('<10H', *range(10)) +
            menu_colors + bytes(32) + struct.pack('<64H', *(i * 4 for i in range(64))) +
            bytes([0x11]) * (8 * 0x160) + menu_colors +
            struct.pack('<6H', *(len(row) for row in compact)) +
            b''.join(row.ljust(32, b'\0') for row in compact) + tilemap(1024) +
            bytes(range(7, -1, -1)) + bytes([0, 3, 1, 2, 4]))


def main():
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    rules = struct.pack("<9I2i2H2Ii4H", 0x314C5552, 24, 1, 1, 1, 2, 2, 3,
                        1, 0x03780000, 0x02640000, 0, 46, 1, 1,
                        592 * 65536, 3, 180, 180, 0)
    route = struct.pack("<2I", 0x31545552, 47) + b"".join(
        struct.pack("<iiI", 888, 608 - i * 8, 2 if i == 0 else 0)
        for i in range(47))
    world = struct.pack("<7I", 0x31444C57, 128, 128, 256, 256,
                        128 * 128, 256 * 256) + bytes(128 * 128 + 256 * 256)
    material = struct.pack("<2I", 0x3154414D, 256) + bytes([0x40]) * 256 + struct.pack("<III6H", 0x03A00000, 8, 169, 592, 288, 496, 560, 576, 656)
    motion_counts = (1024, 64, 8, 8, 8, 256, 10, 4)
    motion_prefix = bytes(1024 * 4 + sum(motion_counts[1:5]) * 2)
    motion_local = b"".join(
        struct.pack("<h", profile * 1000 + class_index * 100 + index)
        for profile in range(2) for class_index in range(8)
        for index in range(16))
    motion = (struct.pack("<9I", 0x31544F4D, *motion_counts) +
              motion_prefix + motion_local +
              bytes((motion_counts[6] + motion_counts[7]) * 2))
    collision_counts = (8, 8, 4, 4, 4, 4, 8)
    collision = struct.pack("<8I", 0x314C4F43, *collision_counts) + bytes(
        (8 + 8 + 8) * 4 + (4 + 4 + 4 + 4) * 2)
    objects = struct.pack("<2I", 0x314A424F, 2) + b"".join((
        struct.pack("<iiHBB", 61 << 19, 57 << 19, 0x8800, 9, 0x1E),
        struct.pack("<iiHBB", 63 << 19, 59 << 19, 0x8800, 9, 0x1E),
    ))
    road = struct.pack("<6I", 0x31444152, 64, 1, 0, 1, 0) + bytes(64 + 2)
    sky = struct.pack("<16I", 0x31594B53, 32, 1, 2, 2, 2,
                      1, 1, 1, 1, 1, 0, 0, 0, 0, 0) + bytes(32 + 2 + 6)
    character = struct.pack("<8I", 0x31524143, 0x800, 16, 64, 64, 2, 0x18, 0) + bytes(0x800 + 32)
    minimap = struct.pack("<6I", 0x314E494D, 30, 6, 5, 0x240, 0) + bytes(60 + 0x240)
    hud_counts = (0x1000, 96, 20, 60, 6, 2, 6, 0x700, 16)
    hud = struct.pack("<11I", 0x32445548, *hud_counts, 0) + bytes(
        0x1000 + 0x700 + (96 + 20 + 6 + 2 + 6 + 16) * 2 + 60)
    effect_sizes = (8, 8, 8, 62, 74)
    effect_durations = (2, 3, 4, 4, 2)
    effect_frames = bytearray()
    effect_offset = 0
    for size, duration in zip(effect_sizes, effect_durations):
        effect_frames += struct.pack("<IHH", effect_offset, size, duration)
        effect_offset += size
    charged_exit = (struct.pack("<8I", 0x31435846, 0x2000, 80, 5,
                                0x100, 3, 160, 0) + effect_frames +
                    bytes(0x2000 + 80 * 2 + 160))
    mushroom_burst = (struct.pack('<8I', 0x31435846, 0x2000, 80, 12,
                                 0x100, 3, 528, 0) +
                      b''.join(struct.pack('<IHH', 0, 2, 4 if i in (2, 9) else 3)
                               for i in range(12)) + bytes(0x2000 + 80 * 2 + 528))
    coin_pickup = (struct.pack('<8I', 0x31435846, 0x2000, 80, 7,
                              0x100, 3, 50, 0) +
                   b''.join(struct.pack('<IHH', 0, 2, duration)
                            for duration in (4,3,2,1,3,4,3)) + bytes(0x2000+160+50))
    entry_splash = (struct.pack('<8I', 0x31435846, 0x2000, 80, 3,
                               0x100, 3, 24, 0) +
                    b''.join(struct.pack('<IHH', i * 8, 8, 8) for i in range(3)) +
                    bytes(0x2000 + 160) + (struct.pack('<H', 1) + bytes(6)) * 3)
    course_manifest = struct.pack("<17I", 0x31535243, 24, 1, 24, 24, 24,
                                  24, 24, 24, 24, 1, 1,
                                  10108, 7424, 7147, 7133, 8152)
    character_manifest = struct.pack("<6I", 0x31524843, 1, 1, 1, 1, 3)
    files = {"rules.bin": rules, "route.bin": route, "world.bin": world,
             "material.bin": material, "motion.bin": motion,
             "collision.bin": collision, "road.bin": road, "sky.bin": sky,
             "objects.bin": objects,
             "character.bin": character, "minimap.bin": minimap,
             "hud.bin": hud, "charged-exit.bin": charged_exit,
             "mushroom-burst.bin": mushroom_burst,
             "coin-pickup.bin": coin_pickup,
             "entry-splash.bin": entry_splash,
             "course-manifest.bin": course_manifest,
             "character-manifest.bin": character_manifest}
    for name, data in files.items():
        (directory / name).write_bytes(data)
    manifest = {"sections": [
        {"kind": 1, "content_id": 24, "flags": 3, "path": "course-manifest.bin"},
        {"kind": 2, "content_id": 1, "flags": 3, "path": "character-manifest.bin"},
        {"kind": 3, "content_id": 24, "flags": 3, "path": "rules.bin"},
        {"kind": 4, "content_id": 24, "version": 2,
         "flags": 3, "path": "route.bin"},
        {"kind": 5, "content_id": 24, "flags": 3, "path": "world.bin"},
        {"kind": 6, "content_id": 24, "version": 5, "flags": 3, "path": "material.bin"},
        {"kind": 8, "content_id": 24, "path": "road.bin"},
        {"kind": 9, "content_id": 24, "path": "sky.bin"},
        {"kind": 10, "content_id": 1, "path": "character.bin"},
        {"kind": 11, "content_id": 24, "path": "minimap.bin"},
        {"kind": 12, "content_id": 1, "path": "hud.bin"},
        {"kind": 13, "content_id": 1, "version": 2,
         "flags": 3, "path": "motion.bin"},
        {"kind": 14, "content_id": 1, "flags": 3, "path": "collision.bin"},
        {"kind": 19, "content_id": 1, "path": "charged-exit.bin"},
        {"kind": 19, "content_id": 2, "path": "mushroom-burst.bin"},
        {"kind": 19, "content_id": 3, "path": "coin-pickup.bin"},
        {"kind": 19, "content_id": 6, "path": "entry-splash.bin"},
        {"kind": 25, "content_id": 24, "flags": 3,
         "path": "objects.bin"}]}
    (directory / "recovery.bin").write_bytes(struct.pack("<2I",0x31434b4c,16)+bytes(128))
    manifest["sections"].append({"kind":38,"content_id":1,"flags":3,"path":"recovery.bin"})
    path = directory / "manifest.json"
    path.write_text(json.dumps(manifest))
    scenery = struct.pack('<4IiiBBHIB3x', 0x314E4353, 1, 1, 0,
                          4 << 16, 12 << 16, 9, 0, 224, 0x0C0000, 14)
    for lod in range(4):
        image = image_asset(8, 16, [0xFF102030 + lod] * 128, (-4, -16))
        scenery += struct.pack('<II', lod, len(image)) + image
    (directory / 'scenery.bin').write_bytes(scenery)
    manifest['sections'].append({'kind': 28, 'content_id': 24, 'version': 4,
                                'flags': 3, 'path': 'scenery.bin'})
    (directory / 'lava-bubbles.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x3142564C, 1, 100 << 16, 200 << 16, 1, 9, 7))
    bubble_art = struct.pack('<II', 0x3141424C, 2)
    for frame in range(2):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            bubble_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'lava-art.bin').write_bytes(bubble_art)
    manifest['sections'].extend((
        {'kind': 39, 'content_id': 24, 'version': 2, 'path': 'lava-bubbles.bin'},
        {'kind': 40, 'content_id': 24, 'path': 'lava-art.bin'}))
    (directory / 'thwomps.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x31505754, 1, 100 << 16, 200 << 16, 1, 9, 9))
    thwomp_art = struct.pack('<II', 0x31415754, 1)
    for frame in range(1):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            thwomp_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'thwomp-art.bin').write_bytes(thwomp_art)
    manifest['sections'].extend((
        {'kind': 41, 'content_id': 24, 'version': 2, 'path': 'thwomps.bin'},
        {'kind': 42, 'content_id': 24, 'path': 'thwomp-art.bin'}))
    (directory / 'crabs.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x31425243, 1, 100 << 16, 200 << 16, 0x8001, 9, 11))
    crab_art = struct.pack('<II', 0x31415243, 4)
    for frame in range(4):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            crab_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'crab-art.bin').write_bytes(crab_art)
    manifest['sections'].extend((
        {'kind': 43, 'content_id': 24, 'path': 'crabs.bin'},
        {'kind': 44, 'content_id': 24, 'path': 'crab-art.bin'}))
    (directory / 'seagulls.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314C4753, 1, 100 << 16, 200 << 16, 0, 9, 13))
    seagull_art = struct.pack('<II', 0x31414753, 3)
    for frame in range(3):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            seagull_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'seagull-art.bin').write_bytes(seagull_art)
    manifest['sections'].extend((
        {'kind': 45, 'content_id': 24, 'path': 'seagulls.bin'},
        {'kind': 46, 'content_id': 24, 'path': 'seagull-art.bin'}))
    (directory / 'piranhas.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314E5250, 1, 100 << 16, 200 << 16, 0, 9, 13))
    piranha_art = struct.pack('<II', 0x31415250, 5)
    for frame in range(5):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            piranha_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'piranha-art.bin').write_bytes(piranha_art)
    manifest['sections'].extend((
        {'kind': 49, 'content_id': 24, 'path': 'piranhas.bin'},
        {'kind': 50, 'content_id': 24, 'path': 'piranha-art.bin'}))
    (directory / 'snowmen.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314D4E53, 1, 100 << 16, 200 << 16, 0, 9, 13))
    snowman_art = struct.pack('<II', 0x31414E53, 2)
    for frame in range(2):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            snowman_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'snowman-art.bin').write_bytes(snowman_art)
    manifest['sections'].extend((
        {'kind': 51, 'content_id': 24, 'path': 'snowmen.bin'},
        {'kind': 52, 'content_id': 24, 'path': 'snowman-art.bin'}))
    (directory / 'penguins.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314E4750, 1, 100 << 16, 200 << 16, 0, 9, 13))
    penguin_art = struct.pack('<II', 0x31414750, 6)
    for frame in range(6):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            penguin_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'penguin-art.bin').write_bytes(penguin_art)
    manifest['sections'].extend((
        {'kind': 53, 'content_id': 24, 'path': 'penguins.bin'},
        {'kind': 54, 'content_id': 24, 'path': 'penguin-art.bin'}))
    (directory / 'boos.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314F4F42, 1, 100 << 16, 200 << 16, 0, 9, 13))
    boo_art = struct.pack('<II', 0x31414F42, 2)
    for frame in range(2):
        for lod in range(4):
            image = image_asset(8, 16, [0xFF112200 + frame] * 128, (-4, -16))
            boo_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'boo-art.bin').write_bytes(boo_art)
    manifest['sections'].extend((
        {'kind': 55, 'content_id': 24, 'path': 'boos.bin'},
        {'kind': 56, 'content_id': 24, 'path': 'boo-art.bin'}))
    (directory / 'cannonballs.bin').write_bytes(
        struct.pack('<IIiiHBB', 0x314C4243, 1, 100 << 16, 200 << 16, 3, 9, 13))
    cannonball_art = struct.pack('<II', 0x31414243, 7)
    for frame in range(7):
        for lod in range(4):
            width = 67 if frame == 4 else 8
            image = image_asset(width, 16, [0xFF112200 + frame] * (width * 16), (-4, -16))
            cannonball_art += struct.pack('<II', lod, len(image)) + image
    (directory / 'cannonball-art.bin').write_bytes(cannonball_art)
    manifest['sections'].extend((
        {'kind': 47, 'content_id': 24, 'path': 'cannonballs.bin'},
        {'kind': 48, 'content_id': 24, 'path': 'cannonball-art.bin'}))
    path.write_text(json.dumps(manifest))
    build_pack(path, directory / "fixture.pack")

    gp_manifest = {"sections": [dict(section) for section in manifest["sections"]]}
    (directory / "gp-results.bin").write_bytes(authored_results_payload())
    gp_manifest["sections"].append({"kind":22,"content_id":1,"version":8,"flags":3,"path":"gp-results.bin"})
    gp_hud_source=bytearray(hud)
    struct.pack_into('<I',gp_hud_source,40,17)
    gp_hud_source+=struct.pack('<17H',*(0xa000+(i%6)*0x1000 for i in range(17)))
    (directory/'gp-hud-source-v2.bin').write_bytes(gp_hud_source)
    for section in gp_manifest['sections']:
        if section['kind']==12:section.update(version=2,path='gp-hud-source-v2.bin')
    # GP host fixtures use complete authored modern motion owners; the
    # structural legacy fixtures above retain their original schema coverage.
    gp_motion=bytearray(motion)
    # The modern donor has all sixteen steering responses. The structural
    # legacy fixture intentionally retains only four entries.
    struct.pack_into('<I',gp_motion,32,16)
    gp_motion+=bytes((16-motion_counts[7])*2)
    struct.pack_into('<i',gp_motion,36+256*4,32768)
    struct.pack_into('<i',gp_motion,36+768*4,-32768)
    cpu=bytes([8])*128+bytes([40])*160+bytes([2])*128
    stats=struct.pack('<i16h8h',300*65536,*([-1]*10+[400]*6),*([-3]*8))
    engine=cpu+struct.pack('<i16h',300*65536,*([-1]*10+[400]*6))
    gp_motion+=bytes(0x8000)+cpu+stats+engine*3+struct.pack('<8h',*[-3*(i+1) for i in range(8)])
    gp_motion+=struct.pack('<64h',*([-7,64,512,48,64,80,96,1024]*8))
    gp_motion+=struct.pack('<16i8i',*([2]*16+[0]*8))
    gp_motion+=bytes((row+7-i)&7 for row in range(8) for i in range(8))
    gp_motion+=struct.pack('<8H8i',*[0x50+i for i in range(8)],*[-3*(i+1) for i in range(8)])
    gp_motion+=bytes([8])*60+bytes((20,1,21,3,22,4,23,5,24,6,25,7,0,0,1,5))
    (directory/'gp-motion-v8.bin').write_bytes(gp_motion)
    for section in gp_manifest['sections']:
        if section['kind']==13:section.update(version=8,path='gp-motion-v8.bin')
    gp_owned=(
        (85,1,1,'gp-view-rules.bin',struct.pack('<II',0x31525644,15)+bytes([3]*15+[4]*15)),
        (59,1,3,'gp-scoring.bin',struct.pack('<II',0x31545047,8)+bytes((12,9,7,5,4,3,2,1))+
            struct.pack('<I',3)+struct.pack('<23i',*([0]*12+[300,400,500]+[0]*8))),
        (58,12,1,'gp-roulette.bin',struct.pack('<II10H27H8H',0x31544c52,10,
            *range(0x1000,0x100a),*([2]*27),*[4+4*i for i in range(8)])+
            struct.pack('<16I',*([1]*16))+bytes((0,1,2,255,3,255,0,255,1,255,2,255,3,255))),
    )
    for kind,identity,version,name,data in gp_owned:
        (directory/name).write_bytes(data)
        gp_manifest['sections'].append({'kind':kind,'content_id':identity,'version':version,'path':name})
    for identity in range(1,12):
        name=f'gp-player-odds-{identity}.bin'
        (directory/name).write_bytes(struct.pack('<II4H',0x31575047,16,0x1000,1,0,255)+bytes(56))
        gp_manifest['sections'].append({'kind':58,'content_id':identity,'version':1,'path':name})
    # Real GP host entry requires explicit authored item profiles.
    (directory/'gp-lakitu.bin').write_bytes(
        struct.pack('<II',0x31434B4C,16)+bytes(128)+
        struct.pack('<4s4HI',b'FLK1',2,20,3,5,0)+
        struct.pack('<BBHBBH',21,0,20,28,0,8)+
        struct.pack('<4sHH8H',b'IAC1',8,0,3,7,11,13,17,19,23,29))
    for section in gp_manifest['sections']:
        if section['kind']==38 and section['content_id']==1:
            section.update(version=3,path='gp-lakitu.bin')
    item_profiles = (
        (60, 0x3153554D, (9,7,13,17,0x05000000)),
        (61, 0x31544153, (85,91,96)),
        (62, 0x314F4F42, (72,79,12,1537,1025,0x22000,0x3100)),
        (63, 0x31485347, (800,101,7,6,13,31,0xD100,0x1B00)),
        (64, 0x31485352, (801,117,7,37,13,31,109,117,127,5,17,47,135,15,0xD100,0x1B00)),
        (65, 0x314E4142, (0x81000,29,0x1B000,0x1900,7,13,31,0xA100,0x1900)),
        (67, 0x3142524F, (15,1281,7)),
    )
    for kind, magic, values in item_profiles:
        name = f'gp-rules-{kind}.bin'
        (directory / name).write_bytes(struct.pack('<'+'I'*(len(values)+2), magic, len(values), *values))
        gp_manifest['sections'].append({'kind':kind,'content_id':1,'version':1,'path':name})

    (directory / "gp-hud.bin").write_bytes(
        struct.pack("<4I", 0x31484747, 24, 5, 52) + bytes(8 + 0x400 + 0x800 + 520 + 64))
    (directory / "gp-markers.bin").write_bytes(
        struct.pack("<4I", 0x314B4D47, 64, 32, 0x118) + bytes(128))
    (directory / "gp-grid.bin").write_bytes(struct.pack("<2I", 0x31475047, 8) +
        b"".join(struct.pack("<iiI", (908 if rank & 1 else 868) << 16,
                            (620 + rank * 24) << 16, 0) for rank in range(8)))
    (directory / "gp-standings.bin").write_bytes(
        struct.pack("<8I",0x31534750,0x3000,256,1024,8,18,24,4) +
        bytes(0x3000+(256+1024+8*18+9)*2))
    gp_manifest["sections"].extend((
        {"kind": 23, "content_id": 24, "path": "gp-standings.bin"},
        {"kind": 34, "content_id": 24, "path": "gp-grid.bin"},
        {"kind": 33, "content_id": 24, "version": 2, "path": "gp-hud.bin"},
        {"kind": 29, "content_id": 1, "version": 2, "path": "gp-markers.bin"}))
    for picture in range(7):
        name = f'countdown-{picture}.bin'
        (directory / name).write_bytes(struct.pack('<8sHHHHhhIII', b'MKSCIMG1',
            32, 1, 1, 1, 0, 0, 0, 32, 36) + bytes(4))
        gp_manifest['sections'].append({'kind': 15, 'content_id': 0x27000 + picture,
                                        'path': name})
    # Menu backdrop, window font and border for native race notices.
    for content_id, width, height, colour in ((0x21000, 240, 160, 0xFF112244),
                                              (0x26080, 768, 16, 0xFFE0E0E0),
                                              (0x26081, 24, 24, 0xFF8080F0)):
        name = f'menu-{content_id:x}.bin'
        (directory / name).write_bytes(image_asset(width, height, [colour] * (width * height), (0, 0)))
        gp_manifest['sections'].append({'kind': 15, 'content_id': content_id, 'path': name})
    (directory / "gp-objects.bin").write_bytes(struct.pack("<2I", 0x314A424F, 0))
    for section in gp_manifest["sections"]:
        if section["kind"] == 25:
            section["path"] = "gp-objects.bin"
    for content_id, driver_class in enumerate((0, 1, 2, 4, 5, 6, 7), 2):
        name = f"gp-driver-{content_id}.bin"
        (directory / name).write_bytes(struct.pack(
            "<6I", 0x31524843, content_id, 1, 1, 1, driver_class))
        gp_manifest["sections"].append({"kind": 2, "content_id": content_id,
                                        "flags": 3, "path": name})
    gp_path = directory / "gp-manifest.json"
    gp_path.write_text(json.dumps(gp_manifest))
    build_pack(gp_path, directory / "gp-fixture.pack")

    if "--ceremony" in sys.argv:
        ceremony_sections = [dict(section) for section in gp_manifest["sections"]]
        (directory / "ceremony-driver.bin").write_bytes(
            struct.pack("<8I", 0x32524143, 34 * 0x800, 16, 64, 64, 3, 34, 64) +
            bytes(64 + 34 * 0x800 + 32))
        for section in ceremony_sections:
            if section["kind"] == 10:
                section["path"] = "ceremony-driver.bin"
                section["version"] = 2
        image = (struct.pack('<8sHHHHhhIII',b'MKSCIMG1',32,1,1,1,0,0,0,32,36) +
                 struct.pack('<I',0xffe8d890))
        data = (struct.pack('<8I',0x33435047,43,120,180,32,
                            0xff182848,0xff506830,0xffe8d890) +
                (struct.pack('<I',len(image))+image)*43)
        (directory / 'ceremony.bin').write_bytes(data)
        ceremony_sections.append({'kind':24,'content_id':1,'version':3,'flags':1,'path':'ceremony.bin'})
        ceremony_manifest = directory / "ceremony-manifest.json"
        ceremony_manifest.write_text(json.dumps({"sections": ceremony_sections}))
        build_pack(ceremony_manifest, directory / "ceremony-fixture.pack")

    # Exercise route v4 targets and motion v6 engine profiles, retaining v2/v1 above.
    cpu = bytes((index * 7 + 3) & 255 for index in range(0x1a0))
    stats = struct.pack('<i16h8h',1100*65536,*([-1]*10+[900]*6),*([-4]*8))
    engines = b''.join(bytes((index*7+3+engine)&255 for index in range(416))+
        struct.pack('<i16h',(900+engine*100)*65536,*([-1]*10+[700+engine*100]*6))
        for engine in range(3))
    (directory / "motion-v6.bin").write_bytes(motion + bytes(0x8000) + cpu + stats + engines)
    (directory / 'route-v4.bin').write_bytes(
        struct.pack('<4I',0x31545552,47,1,40)+route[8:]+bytes(40)+
        struct.pack('<I2i',1,80,96))
    cpu_items=struct.pack('<576H',*(0x1000+index%13 for index in range(576)))
    battle_tables = (
        ((0x1007,10),(0x1005,0),(0x1000,0),(0x1001,0),(0x1002,0),(0x1003,0),(0x1004,0),(0x100c,0),(0x100a,0),(0,255)),
        ((0x1002,25),(0x100a,3),(0x1005,6),(0x1000,6),(0,255)),
        ((0x1002,35),(0x100a,5),(0x1005,10),(0x1000,25),(0,255)))
    battle = b''.join(b''.join(struct.pack('<HH',*row) for row in table) +
        bytes((10-len(table))*4) for table in battle_tables) + struct.pack('<I',120)
    (directory/'material-v16.bin').write_bytes(material+cpu_items+bytes((0,0,0,2,3,1,3,3))+bytes((2,0,0,0,3,1,3,3))+bytes((2,0,0,0,3,3,3,1))+bytes((0,17,34,0,17,34))+struct.pack('<16H', *range(202,218))+struct.pack('<1089h',*(i-544 for i in range(1089)))+battle+struct.pack('<I',10)+struct.pack('<6I',256,128,0x1000,0x3000,6,64)+struct.pack('<3I',128,0x1000,48))
    v4_manifest = json.loads(json.dumps(manifest))
    for section in v4_manifest['sections']:
        if section['kind'] == 4:
            section['version'] = 4
            section['path'] = 'route-v4.bin'
        if section['kind'] == 6:
            section['version'] = 16
            section['path'] = 'material-v16.bin'
        if section['kind'] == 13:
            section['version'] = 6
            section['path'] = 'motion-v6.bin'
    v4_path = directory / 'manifest-v4.json'
    v4_path.write_text(json.dumps(v4_manifest))
    build_pack(v4_path, directory / 'fixture-v4.pack')

    # Retain a second v1 pack so the loader's compatibility replication stays
    # covered while fixture.pack exercises the two-profile, eight-class v2.
    v1_counts = (1024, 64, 8, 8, 8, 16, 10, 4)
    v1_prefix = bytes(1024 * 4 + sum(v1_counts[1:5]) * 2)
    v1_local = b"".join(struct.pack("<h", 77 + index)
                        for index in range(16))
    (directory / "motion.bin").write_bytes(
        struct.pack("<9I", 0x31544F4D, *v1_counts) + v1_prefix + v1_local +
        bytes((v1_counts[6] + v1_counts[7]) * 2))
    manifest["sections"][11].pop("version")
    (directory / 'scenery.bin').write_bytes(
        struct.pack('<4I4B', 0x314E4353, 1, 128, 16, 2, 1, 2, 9) +
        bytes(128 + 32))
    for section in manifest['sections']:
        if section['kind'] == 28:
            section['version'] = 1
    path.write_text(json.dumps(manifest))
    build_pack(path, directory / "fixture-v1.pack")


if __name__ == "__main__":
    main()

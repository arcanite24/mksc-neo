"""The shade-driver example builds a roster driver from a synthetic pack, without game data."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ui_rom import image_asset  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('shade_driver_example', ROOT / 'mods/examples/shade-driver/build.py')
EXAMPLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXAMPLE)

GREEN, SKIN = 0xff20c020, 0xfff8b878


def animation(frames, loop=1):
    return (struct.pack('<8sHHHHIIII', b'MKSCANI1', 32, 1, len(frames), loop,
                        sum(t for _, t in frames), 32, 32 + len(frames) * 8, 0) +
            b''.join(struct.pack('<II', image, ticks) for image, ticks in frames))


def write_base(root, motion_version=6, graphics_version=3, graphics_frames=None):
    """Stock drivers 2 (look, class1) and 7 (seat, class5) plus slot-1 menu art."""
    def manifest(content_id, driver_class):
        return struct.pack('<6I', 0x31524843, content_id, 1, content_id, content_id, driver_class)
    frames = graphics_frames if graphics_frames is not None else (25 if graphics_version == 4 else 34)
    lod_frames = frames if graphics_version == 4 else 25
    car = struct.pack('<8I', 0x33524143, frames * 2048, 256, 64, 64, 0, frames, 64) + bytes(64 + frames * 2048 + lod_frames * 640)
    car += struct.pack('<256H', *([0x03e0, 0x7fff, 0x001f] * 85 + [0]))
    motion = bytearray(39464)
    struct.pack_into('<I', motion, 38056, 1096 << 16)
    for engine, speed in enumerate((808, 936, 1096)):
        struct.pack_into('<I', motion, 38108 + engine * 452 + 416, speed << 16)
    if motion_version == 7:
        motion += struct.pack('<8h', -3, -7, -13, -21, -29, -43, -59, -79)
    portrait = image_asset(2, 2, [GREEN, SKIN, 0, GREEN], (156, 36))
    # HUD palette row 3 (entries 48..63) and a GP race HUD whose class-1 page has two pixels.
    hud_palette = [0] * 64
    hud_palette[49:51] = [0x03e0, 0x7fff]
    hud = struct.pack('<11I', 0x32445548, 0, 64, *([0] * 8)) + struct.pack('<64H', *hud_palette)
    gp_hud = bytearray(16 + 8 + 0x400)
    gp_hud[16:24] = bytes([0, 1, 2, 3, 4, 5, 6, 7])
    gp_hud[24 + 4 * 32] = 0x21
    sections = [
        (2, 2, 1, manifest(2, 1)), (2, 7, 1, manifest(7, 5)),
        (12, 1, 1, hud), (33, 24, 2, bytes(gp_hud)),
        (10, 2, graphics_version, car), (13, 7, motion_version, bytes(motion)),
        (15, 0x30000, 1, portrait), (15, 0x30001, 1, image_asset(1, 1, [GREEN], (72, 83))),
        (15, 0x22201, 1, image_asset(64, 32, [0] * 64 * 32, (40, 32))),
        (15, 0x22301, 1, animation([(0x30001, 1)] * 2)),
    ] + [(15, 0x22103 + state, 1, animation([(0x30000, 4)], state != 2)) for state in range(3)]
    entries = []
    for kind, content_id, version, data in sections:
        name = f'{kind}-{content_id:x}.bin'
        (root / name).write_bytes(data)
        entries.append({'kind': kind, 'content_id': content_id, 'version': version, 'flags': 3, 'path': name})
    (root / 'base.json').write_text(json.dumps({'sections': entries}))
    EXAMPLE.build_asset_pack.build_pack(root / 'base.json', root / 'base.pack')
    return root / 'base.pack'


class ShadeDriverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.base = write_base(self.root)

    def tearDown(self):
        self.temporary.cleanup()

    def built(self, name='one'):
        EXAMPLE.build(self.base, self.root / name)
        return {(meta['kind'], meta['content_id']): (meta, bytes(data)) for meta, data in
                EXAMPLE.build_asset_pack.read_sections(self.root / name / 'content.pack')}

    def test_roster_driver_sections_are_deterministic(self):
        sections = self.built('one')
        self.built('two')
        self.assertEqual((self.root / 'one/content.pack').read_bytes(), (self.root / 'two/content.pack').read_bytes())
        self.assertEqual(json.loads((self.root / 'one/mod.json').read_text())['id'], 'shade-driver')
        # Manifest 16: own graphics/motion 16, Wario's seat class 5.
        self.assertEqual(struct.unpack('<6I', sections[(2, 16)][1]), (0x31524843, 16, 1, 16, 16, 5))
        # Roster row: art slot 1 and Luigi's voices (the look driver) by default.
        self.assertEqual(struct.unpack('<3I4B', sections[(79, 16)][1]), (0x31565244, 1, 16, 1, 2, 0, 0))
        self.assertEqual(sections[(79, 16)][0]['flags'], 3)
        self.assertEqual(sections[(13, 16)][1], (self.root / '13-7.bin').read_bytes())
        # Greens in every palette bank turn purple; whites and reds stay.
        palette = struct.unpack('<256H', sections[(10, 16)][1][-512:])
        self.assertNotEqual(palette[0], 0x03e0)
        self.assertEqual(palette[1:3], (0x7fff, 0x001f))
        r, g, b = palette[0] & 31, palette[0] >> 5 & 31, palette[0] >> 10 & 31
        self.assertTrue(r > g and b > g)
        data = json.loads(sections[(57, 1)][1])
        physics = data['kart_physics'][0]
        self.assertEqual(physics['id'], 16)
        self.assertEqual(physics['time_trial']['base_speed_q16'], (1096 << 16) * 104 // 100)
        self.assertEqual([e['base_speed_q16'] for e in physics['engines']],
                         [(s << 16) * 104 // 100 for s in (808, 936, 1096)])

    def test_full_resolution_only_preserves_every_pose_and_other_sections(self):
        for version, frames in ((3, 34), (4, 25), (4, 7)):
            with self.subTest(version=version, frames=frames):
                root = self.root / f'graphics-v{version}-{frames}'
                root.mkdir()
                base = write_base(root, graphics_version=version, graphics_frames=frames)
                donor = bytearray((root / '10-2.bin').read_bytes())
                full_end = 96 + struct.unpack_from('<I', donor, 4)[0]
                # Distinct pose bytes and mappings expose dropped/reordered frames.
                donor[32:96] = bytes(index % min(frames, 25) for index in range(64))
                donor[96:full_end] = bytes(index % 251 for index in range(full_end - 96))
                (root / '10-2.bin').write_bytes(donor)
                EXAMPLE.build_asset_pack.build_pack(root / 'base.json', base)
                for name, option in (('normal', False), ('sparse', True), ('again', True)):
                    EXAMPLE.build(base, root / name, full_resolution_only=option)
                # Explicit false has exactly the default output, not a new path.
                EXAMPLE.build(base, root / 'default')
                self.assertEqual((root / 'normal/content.pack').read_bytes(),
                                 (root / 'default/content.pack').read_bytes())
                self.assertEqual((root / 'sparse/content.pack').read_bytes(),
                                 (root / 'again/content.pack').read_bytes())
                packs = [{(meta['kind'], meta['content_id']): (meta, bytes(data))
                          for meta, data in EXAMPLE.build_asset_pack.read_sections(root / name / 'content.pack')}
                         for name in ('normal', 'sparse')]
                normal, sparse = packs
                self.assertEqual(normal.keys(), sparse.keys())
                for key in normal:
                    if key != (10, 16):
                        self.assertEqual(normal[key], sparse[key])
                original, stripped = normal[(10, 16)][1], sparse[(10, 16)][1]
                self.assertEqual(sparse[(10, 16)][0]['version'], version)
                self.assertEqual(struct.unpack_from('<I', stripped)[0], 0x32524143)
                self.assertEqual(stripped[4:full_end], original[4:full_end])
                self.assertEqual(stripped[full_end:], original[-512:])
                self.assertEqual(len(original) - len(stripped),
                                 (25 if version == 3 else frames) * (512 + 128))
                # Already sparse donors remain valid and do not lose frames again.
                (root / '10-2.bin').write_bytes(stripped)
                EXAMPLE.build_asset_pack.build_pack(root / 'base.json', base)
                EXAMPLE.build(base, root / 'already-sparse', full_resolution_only=True)
                already = {(meta['kind'], meta['content_id']): bytes(data) for meta, data in
                           EXAMPLE.build_asset_pack.read_sections(root / 'already-sparse/content.pack')}
                self.assertEqual(already[(10, 16)][4:full_end], stripped[4:full_end])
                self.assertEqual(len(already[(10, 16)]), len(stripped))

    def test_full_resolution_only_rejects_malformed_complete_donor(self):
        for version in (3, 4):
            root = self.root / f'bad-graphics-v{version}'
            root.mkdir()
            base = write_base(root, graphics_version=version)
            valid = (root / '10-2.bin').read_bytes()
            mutations = []
            for offset, value in ((0, 0), (4, 2048), (8, 15), (12, 32), (24, 26), (28, 63)):
                damaged = bytearray(valid)
                struct.pack_into('<I', damaged, offset, value)
                mutations.append(damaged)
            damaged = bytearray(valid)
            damaged[32] = 25
            mutations += [damaged, valid[:31], valid[:-1], valid + b'\0']
            for index, damaged in enumerate(mutations):
                with self.subTest(version=version, mutation=index):
                    (root / '10-2.bin').write_bytes(damaged)
                    EXAMPLE.build_asset_pack.build_pack(root / 'base.json', base)
                    with self.assertRaisesRegex(ValueError, 'driver graphics|driver art'):
                        EXAMPLE.build(base, root / f'bad-{index}', full_resolution_only=True)

    def test_v7_donor_preserves_deceleration_and_speed_offsets(self):
        root = self.root / 'v7';root.mkdir()
        base = write_base(root, motion_version=7)
        EXAMPLE.build(base, root / 'mod')
        sections = {(meta['kind'], meta['content_id']): (meta, bytes(data)) for meta, data in
                    EXAMPLE.build_asset_pack.read_sections(root / 'mod/content.pack')}
        meta, motion = sections[(13, 16)]
        self.assertEqual(meta['version'], 7)
        self.assertEqual(motion, (root / '13-7.bin').read_bytes())
        self.assertEqual(struct.unpack_from('<8h', motion, len(motion)-16),
                         (-3, -7, -13, -21, -29, -43, -59, -79))
        physics = json.loads(sections[(57, 1)][1])['kart_physics'][0]
        self.assertEqual(physics['time_trial']['base_speed_q16'], (1096 << 16)*104//100)
        self.assertEqual([row['base_speed_q16'] for row in physics['engines']],
                         [(speed << 16)*104//100 for speed in (808, 936, 1096)])

    def test_menu_art_uses_roster_ids_and_recolours(self):
        sections = self.built()
        art = 0x22400 + 16 * 8
        for state in range(3):
            frames = sections[(15, art + state)][1]
            image = struct.unpack_from('<I', frames, 32)[0]
            self.assertEqual(image, 0x40000 + 16 * 0x400)
            pixels = struct.unpack_from('<4I', sections[(15, image)][1], 32)
            self.assertNotEqual(pixels[0], GREEN)
            self.assertEqual(pixels[1:3], (SKIN, 0))
            self.assertEqual(struct.unpack_from('<hh', sections[(15, image)][1], 16), (156, 36))
        name = sections[(15, art + 3)][1]
        self.assertEqual(struct.unpack_from('<HHhh', name, 12), (64, 32, 40, 32))
        colors = set(struct.unpack_from(f'<{64 * 32}I', name, 32))
        self.assertTrue({0, 0xfff8d800, 0xff302000} == colors)
        preview = sections[(15, art + 4)][1]
        self.assertEqual(struct.unpack_from('<4H', preview, 8)[2], 2)
        self.assertNotIn((15, 0x22201), sections)
        # Race HUD/board portrait: the look class's kind33 page, recoloured.
        portrait = sections[(15, art + 5)][1]
        self.assertEqual(struct.unpack_from('<HH', portrait, 12), (16, 16))
        pixels = struct.unpack_from('<256I', portrait, 32)
        self.assertEqual((pixels[1], set(pixels[2:])), (0xffffffff, {0}))
        r, g, b = pixels[0] >> 16 & 255, pixels[0] >> 8 & 255, pixels[0] & 255
        self.assertTrue(pixels[0] >> 24 == 255 and r > g and b > g)
        board = sections[(15, art + 6)][1]
        self.assertEqual(struct.unpack_from('<HH', board, 12), (40, 16))
        self.assertTrue({0, 0xfff8d800, 0xff302000} == set(struct.unpack_from(f'<{40 * 16}I', board, 32)))

    def test_voice_choice(self):
        EXAMPLE.build(self.base, self.root / 'voiced', voice=7)
        sections = {(meta['kind'], meta['content_id']): bytes(data) for meta, data in
                    EXAMPLE.build_asset_pack.read_sections(self.root / 'voiced/content.pack')}
        self.assertEqual(sections[(79, 16)][12:16], bytes([1, 7, 0, 0]))

    def test_named_errors(self):
        with self.assertRaisesRegex(ValueError, 'off the stock grid'):
            EXAMPLE.build(self.base, self.root / 'bad', driver_id=7)
        with self.assertRaisesRegex(ValueError, 'character manifest 3'):
            EXAMPLE.build(self.base, self.root / 'bad', look=3)
        with self.assertRaisesRegex(ValueError, 'character manifest 3'):
            EXAMPLE.build(self.base, self.root / 'bad', voice=3)
        with self.assertRaisesRegex(ValueError, 'stock drivers'):
            EXAMPLE.build(self.base, self.root / 'bad', voice=0)
        with self.assertRaisesRegex(ValueError, 'menu glyphs'):
            EXAMPLE.build(self.base, self.root / 'bad', name='shade')


if __name__ == '__main__':
    unittest.main()

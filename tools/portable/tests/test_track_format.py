"""Export/compile track folders: exact round trip, derivations and named errors."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import png_indexed
import track_format

COURSE = 24
FINISH_Y = 500
START = (200, 520)


def words(*values):
    return struct.pack(f'<{len(values)}I', *values)


def loop_route():
    """Rectangle loop x 200..800, y 200..800; north up x=200 through the finish."""
    route = [(200, y) for y in (440, 360, 280)]
    route += [(x, 200) for x in range(200, 800, 80)]
    route += [(800, y) for y in range(200, 800, 80)]
    route += [(x, 800) for x in range(800, 200, -80)]
    route += [(200, y) for y in (800, 720, 640, 560, 480)]
    return route


def stock_sections():
    """A stock-shaped course 24: every compiled kind plus HUD, catalog and scenery."""
    route = loop_route()
    count = len(route)
    materials = bytearray([0x26]) * 256
    materials[1], materials[2], materials[3] = 0x40, 0x5A, 0x80
    materials[0xD3] = 0x40
    materials[0xFE] = 0x40
    tilemap = bytearray([2]) * (128 * 128)
    for x, y in route:
        for dy in range(-6, 7):
            for dx in range(-6, 7):
                tilemap[(y // 8 + dy) * 128 + x // 8 + dx] = 1
    for x in range(19, 32):
        tilemap[62 * 128 + x] = 0xFE
    tilemap[0] = 0xD3          # stock maps may use object aliases
    for i in range(128):
        tilemap[i * 128 + 127] = 3
    raster = track_format.derive_raster(route, FINISH_Y, 64, 64)
    tiles = bytes((tile * 7 + pixel) % 64 for tile in range(256) for pixel in range(64))
    palette = [(i * 1057) & 0x7FFF for i in range(64)]
    palette[5] |= 0x8000
    rows = [[(x, y, 2) for x, y in route],
            [(x + 8, y, 1) for x, y in route], [(x - 8, y, 3) for x, y in route]]
    route_payload = words(0x31545552, count, 3, 40) + b''.join(
        words(*point) for row in rows for point in row)
    route_payload += bytes(i % 3 for i in range(40)) + words(5, 16, 24, 0, 0, 40, 48)
    rules = (words(0x314C5552, COURSE, 3, 3, 1, 2, 2, 3, 3, START[0] << 16, START[1] << 16) +
             struct.pack('<HHIIIHHHH', 0, count - 1, 2, 2, FINISH_Y << 16, 3, 180, 180,
                         0x80 | count - 1) + struct.pack('<B3x', 7))
    material = words(0x3154414D, 256) + bytes(materials) + bytes(i & 255 for i in range(3844 - 264))
    road = words(0x31444152, 0x4000, 64, 1, 4, 0) + tiles + struct.pack('<64H', *palette)
    standings = bytearray(words(0x31534750, 0x3000, 256, 1024, 8, 18, COURSE, 4))
    standings += bytes((i * 31) & 0xFF for i in range(0x3000)) + bytes(range(256)) * 2
    standings += bytes(2048 + 8 * 18 * 2 + 18)
    gp_hud = words(0x31484747, COURSE, 5, 52) + bytes(8) + bytes(0x400) + \
        bytes((i * 13) & 0xFF for i in range(0x800)) + bytes(5 * 104 + 64)
    hud = words(0x32445548, 0x1000, 96, 20, 60, 6, 2, 6, 0x700, 16, 0) + bytes(0x1000) + \
        struct.pack('<96H', *((i * 999) & 0x7FFF for i in range(96))) + bytes(64)
    grid = words(0x31475047, 8) + b''.join(
        words((START[0] - 20 + 40 * (r & 1)) << 16, (START[1] + 8 + 24 * r) << 16, 0) for r in range(8))
    return [
        (1, COURSE, 1, words(0x31535243, COURSE, 3, *([COURSE] * 7), 1, 1, 0, 0, 0, 0, 0)),
        (3, COURSE, 2, rules), (4, COURSE, 4, route_payload),
        (5, COURSE, 1, words(0x31444C57, 64, 64, 128, 128, 4096, 16384) + raster + bytes(tilemap)),
        (6, COURSE, 16, material), (8, COURSE, 1, road),
        (9, COURSE, 1, words(0x31594B53, 32 * 17, 63, 2048, 4096, 2048, 32, 64, 32, 32, 0x40, 3, 1, 96, 2, 0) + bytes((i * 7) & 255 for i in range(32 * 17)) + struct.pack('<63H', *[i * 513 | (0x8000 if i == 5 else 0) for i in range(63)]) + bytes(8192)), (11, COURSE, 1, b'MIN1' + bytes(20)),
        (12, 1, 1, hud), (23, COURSE, 1, bytes(standings)),
        (25, COURSE, 1, words(0x314A424F, 2) + struct.pack('<IIHBB', 400 << 16, 200 << 16, 0x8800, 9, 0x1E) +
         struct.pack('<IIHBB', 408 << 16, 208 << 16, 0x8800, 3, 0x1E)),
        (27, COURSE, 1, words(0x314E4943, 3) + bytes((0x80, 30, 25, 0, 0x80, 31, 25, 1, 0x80, 32, 25, 2))),
        (28, COURSE, 4, b'SCN1' + bytes(28)), (33, COURSE, 2, gp_hud), (34, COURSE, 1, grid),
        (37, 1, 2, words(0x31544143, 1, COURSE, 7 << 16, 4)),
        (70, COURSE, 2, b'SRF1' + bytes(56)),
    ]


def write_pack(root, sections):
    manifest = []
    for kind, content_id, version, data in sections:
        name = f'{kind}-{content_id}.bin'
        (root / name).write_bytes(data)
        manifest.append({'kind': kind, 'content_id': content_id, 'version': version,
                         'flags': 3, 'path': name})
    (root / 'manifest.json').write_text(json.dumps({'sections': manifest}))
    build_asset_pack.build_pack(root / 'manifest.json', root / 'base.pack')
    return root / 'base.pack'


class TrackFormatTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.sections = stock_sections()
        self.pack = write_pack(self.root, self.sections)
        self.base = track_format.load_pack(self.pack)
        self.track = self.root / 'track'
        track_format.export_course(self.base, COURSE, self.track)

    def test_ambient_raw_roundtrip_override_and_template_inheritance(self):
        ambient = b'AMB1' + bytes(range(64))
        self.sections.append((84, COURSE, 1, ambient))
        self.pack = write_pack(self.root, self.sections)
        self.base = track_format.load_pack(self.pack)
        track_format.export_course(self.base, COURSE, self.track)
        self.assertEqual((self.track / 'ambient.bin').read_bytes(), ambient)
        def compiled():
            return {s['kind']: s for s in track_format.compile_track(self.track, self.base)}
        self.assertEqual(compiled()[84]['data'], ambient)
        self.edit(passthrough={})
        (self.track / 'ambient.bin').unlink()
        self.assertEqual(compiled()[84]['data'], ambient)
        replacement = b'AMB1' + bytes(80)
        (self.track / 'ambient.bin').write_bytes(replacement)
        self.assertEqual(compiled()[84]['data'], replacement)
        self.assertEqual(compiled()[84]['version'], 1)

    def test_opening_art_inherits_and_uses_custom_name_png(self):
        opening=bytearray(words(0x31544f47,0xa00)+bytes(5216))
        standings=next(data for kind,identity,version,data in self.sections if kind==23)
        opening[8+0xe0:8+0x3e0]=standings[track_format.NAME_TILE_OFFSET:track_format.NAME_TILE_OFFSET+768]
        opening[-576:]=bytes([17])*576
        self.sections.append((36,COURSE,27,bytes(opening)))
        self.pack=write_pack(self.root,self.sections);self.base=track_format.load_pack(self.pack)
        track_format.export_course(self.base,COURSE,self.track)
        self.assertEqual((self.track/'opening.bin').read_bytes(),opening)
        self.assertEqual(self.compiled()[36]['data'],opening)
        pixels=bytes([2])*96*16
        png_indexed.write(self.track/'name.png',96,16,pixels)
        rebuilt=self.compiled()[36]
        self.assertEqual(rebuilt['version'],27)
        self.assertEqual(rebuilt['data'][8+0xe0:8+0x3e0],track_format.planar_tiles(pixels,12,2))
        self.assertEqual(rebuilt['data'][-576:],bytes([17])*576)
        (self.track/'opening.bin').unlink();self.edit(passthrough={})
        self.assertEqual(self.compiled()[36]['data'],rebuilt['data'])

    def test_record_targets_roundtrip_override_and_template_inheritance(self):
        records=words(0x31525454,6)+b''.join(struct.pack('<BBH',i,0,100+i*31) for i in range(6))
        self.sections.append((93,COURSE,1,records))
        self.pack=write_pack(self.root,self.sections);self.base=track_format.load_pack(self.pack)
        track_format.export_course(self.base,COURSE,self.track)
        self.assertEqual((self.track/'records.bin').read_bytes(),records)
        self.assertEqual(self.compiled()[93]['data'],records)
        changed=bytearray(records);struct.pack_into('<H',changed,10,99)
        (self.track/'records.bin').write_bytes(changed)
        self.assertEqual(self.compiled()[93]['data'],changed)
        changed[9]=1;(self.track/'records.bin').write_bytes(changed)
        with self.assertRaises(track_format.TrackError):self.compiled()
        (self.track/'records.bin').unlink();self.edit(passthrough={})
        self.assertEqual(self.compiled()[93]['data'],records)

    def tearDown(self):
        self.temporary.cleanup()

    def edit(self, **changes):
        path = self.track / 'track.json'
        track = json.loads(path.read_text())
        for key, value in changes.items():
            if value is None:
                track.pop(key, None)
            else:
                track[key] = value
        path.write_text(json.dumps(track))
        return track

    def compiled(self):
        return {s['kind']: s for s in track_format.compile_track(self.track, self.base)}

    def test_sky_png_edits_preserve_maps_metadata_and_palette_high_bits(self):
        original = self.base[(9, COURSE)][1]
        width, height, pixels, _ = png_indexed.read(self.track / 'sky-tiles.png')
        pixels = bytearray(pixels)
        pixels[0] = (pixels[0] + 1) & 15
        png_indexed.write(self.track / 'sky-tiles.png', width, height, pixels)
        width, height, pixels, palette = png_indexed.read(self.track / 'sky-palette.png')
        palette[5] = (248, 0, 0)
        png_indexed.write(self.track / 'sky-palette.png', width, height, pixels, palette)
        rebuilt = self.compiled()[9]['data']
        tile_bytes, colours, _ = track_format.sky_layout(original)
        palette_offset = 64 + tile_bytes
        self.assertEqual(rebuilt[:64], original[:64])
        self.assertNotEqual(rebuilt[64:palette_offset], original[64:palette_offset])
        self.assertEqual(struct.unpack_from('<H', rebuilt, palette_offset + 10)[0], 0x801F)
        self.assertEqual(rebuilt[palette_offset + colours * 2:], original[palette_offset + colours * 2:])
        (self.track / 'sky-tiles.png').unlink()
        (self.track / 'sky-palette.png').unlink()
        self.assertEqual(self.compiled()[9]['data'], original)

    def test_sky_rejects_bad_headers_indices_padding_and_palette_order(self):
        original = self.base[(9, COURSE)][1]
        for payload in (original[:63], original[:-1], original[:4] + words(33) + original[8:],
                        original[:24] + words(31) + original[28:]):
            with self.subTest(payload=payload[:32]):
                with self.assertRaisesRegex(track_format.TrackError, 'SkyFormat'):
                    track_format.sky_layout(payload)
        width, height, pixels, _ = png_indexed.read(self.track / 'sky-tiles.png')
        for position, value in ((0, 16), (width * 8 + 8, 1)):
            changed = bytearray(pixels)
            changed[position] = value
            png_indexed.write(self.track / 'sky-tiles.png', width, height, changed)
            with self.assertRaisesRegex(track_format.TrackError, 'SkyFormat'):
                self.compiled()
        png_indexed.write(self.track / 'sky-tiles.png', width, height, pixels)
        width, height, pixels, palette = png_indexed.read(self.track / 'sky-palette.png')
        changed = bytearray(pixels)
        changed[0], changed[1] = changed[1], changed[0]
        png_indexed.write(self.track / 'sky-palette.png', width, height, changed, palette)
        with self.assertRaisesRegex(track_format.TrackError, 'SkyFormat'):
            self.compiled()
        png_indexed.write(self.track / 'sky-palette.png', width + 1, 1, bytes(width + 1), palette)
        with self.assertRaisesRegex(track_format.TrackError, 'ImageSize'):
            self.compiled()

    def test_motion_sky_round_trip_png_edits_and_json_override(self):
        original = self.base[(9, COURSE)][1]
        motion = {'controller': 0, 'layer_mask': 7, 'blend_mode': 0, 'blend_a': 16,
                  'blend_b': 0, 'yaw_shift': [6,7,8], 'x_bias': [-40,0,0],
                  'y_bias': [70,70,70], 'split_y': -1, 'split_layer': 2,
                  'split_yaw_shift': 8, 'split_x_bias': 0, 'lightning_hide_layer': 2, 'layer_source': [0,1,2], 'road_mode': 0}
        data = original[:60] + words(40) + original[64:] + track_format.sky_motion_bytes(motion)
        track_format.export_sky(data, self.track)
        self.assertEqual(json.loads((self.track / 'sky-motion.json').read_text()), motion)
        self.assertEqual(track_format.compile_sky(data, self.track), data)
        self.assertEqual(self.compiled()[9]['version'], 2)
        width,height,pixels,palette = png_indexed.read(self.track / 'sky-palette.png')
        palette[5] = (0,248,0)
        png_indexed.write(self.track / 'sky-palette.png',width,height,pixels,palette)
        edited = self.compiled()[9]['data']
        self.assertNotEqual(edited, data)
        self.assertEqual(edited[-40:],data[-40:])
        motion['x_bias'][0] = -80
        (self.track / 'sky-motion.json').write_text(json.dumps(motion))
        self.assertEqual(track_format.sky_motion(self.compiled()[9]['data']),motion)
        (self.track / 'sky-motion.json').unlink()
        self.assertEqual(self.compiled()[9]['data'][-40:],data[-40:])
        # The complete JSON can also upgrade a legacy folder without changing art/maps.
        (self.track / 'sky.bin').write_bytes(original)
        (self.track / 'sky-motion.json').write_text(json.dumps(motion))
        self.assertEqual(track_format.sky_motion(self.compiled()[9]['data']),motion)
        track_format.export_sky(original, self.track)
        self.assertFalse((self.track / 'sky-motion.json').exists())
        self.assertEqual(self.compiled()[9]['data'], original)

    def test_motion_sky_rejects_bad_tail_and_json_fields(self):
        motion = {'controller': 0, 'layer_mask': 7, 'blend_mode': 0, 'blend_a': 16,
                  'blend_b': 0, 'yaw_shift': [6,7,8], 'x_bias': [-40,0,0],
                  'y_bias': [70,70,70], 'split_y': -1, 'split_layer': 2,
                  'split_yaw_shift': 8, 'split_x_bias': 0, 'lightning_hide_layer': 2, 'layer_source': [0,1,2], 'road_mode': 0}
        original = self.base[(9, COURSE)][1]
        data = original[:60] + words(40) + original[64:] + track_format.sky_motion_bytes(motion)
        for index,value in ((0,0),(4,2),(8,8),(9,8),(10,2),(11,17),(13,16),
                            (30,3),(31,16),(34,3),(35,1),(36,3),(39,4)):
            changed = bytearray(data)
            changed[len(changed)-40+index] = value
            with self.subTest(index=index):
                with self.assertRaisesRegex(track_format.TrackError,'SkyFormat'):
                    track_format.sky_layout(changed)
        for key,value in (('controller',True),('yaw_shift',[6,7]),('extra',1)):
            bad = {**motion,key:value}
            (self.track / 'sky-motion.json').write_text(json.dumps(bad))
            with self.assertRaisesRegex(track_format.TrackError,'SkyFormat'):
                self.compiled()

    def test_custom_water_compiles(self):
        self.edit(id=150)
        tileset_path = self.track / 'tileset.json'
        tileset = json.loads(tileset_path.read_text())
        tileset['materials'][2] = 'water'
        tileset_path.write_text(json.dumps(tileset))
        width, height, tilemap, _ = png_indexed.read(self.track / 'tilemap.png')
        tilemap = bytearray(tilemap)
        tilemap[0] = 2  # Replace the stock object alias before creating a custom track.
        png_indexed.write(self.track / 'tilemap.png', width, height, tilemap)
        self.assertEqual(self.compiled()[6]['data'][10], 0x22)

    def test_export_then_compile_reproduces_every_section(self):
        self.assertEqual(sorted(p.name for p in self.track.iterdir()),
                         ['minimap.png', 'name.png', 'raster.png', 'scenery.bin', 'sky-palette.png', 'sky-tiles.png', 'sky.bin',
                          'tilemap.png', 'tileset.json', 'tileset.png', 'track.json'])
        track = json.loads((self.track / 'track.json').read_text())
        self.assertEqual((track['song'], track['item_strength_threshold'], track['finish_y']),
                         (7, 7, FINISH_Y))
        self.assertEqual(json.loads((self.track / 'tileset.json').read_text())['materials'][:4],
                         ['bounds', 'road', 'offroad', 'wall'])
        output = self.root / 'out'
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(track_format.main(['compile', str(self.track), '--base', str(self.pack),
                                                '--output', str(output)]), 0)
        expected = {kind: (version, data) for kind, cid, version, data in self.sections if cid == COURSE}
        rebuilt = track_format.load_pack(self._pack_from(output))
        self.assertEqual(len(rebuilt), 15)
        for (kind, content_id), (header, data) in rebuilt.items():
            self.assertEqual(content_id, COURSE)
            self.assertEqual((header['version'], data), expected[kind], f'kind {kind}')

    def _pack_from(self, output):
        build_asset_pack.build_pack(output / 'manifest.json', output / 'track.pack')
        return output / 'track.pack'

    def test_custom_id_derives_raster_rows_grid_minimap_and_name(self):
        track = self.edit(id=130, name='LOOP', ai_rows=None, grid=None, target_table=None,
                          ai_sentinels=None, passthrough=None,
                          route=[{'x': p['x'], 'y': p['y']} for p in
                                 json.loads((self.track / 'track.json').read_text())['route']])
        for name in ('raster.png', 'minimap.png', 'name.png'):
            (self.track / name).unlink()
        tilemap = bytearray(png_indexed.read(self.track / 'tilemap.png')[2])
        tilemap[0] = 2
        png_indexed.write(self.track / 'tilemap.png', 128, 128, tilemap)
        sections = self.compiled()
        self.assertEqual(sorted(sections), [1, 3, 4, 5, 6, 8, 9, 11, 23, 25, 27, 33, 34, 70])
        route = [(p['x'], p['y']) for p in track['route']]
        count = len(route)
        world = sections[5]['data']
        raster = world[28:28 + 4096]
        for index, (x, y) in enumerate(route):
            region = (index + 1) % count
            expected = region | (0x80 if region in (0, count - 1) else 0)
            self.assertEqual(raster[(y // 16) * 64 + x // 16], expected, f'route[{index}]')
        self.assertEqual(raster[(START[1] // 16) * 64 + START[0] // 16], 0x80 | count - 1)
        self.assertEqual(raster[(FINISH_Y // 16 - 1) * 64 + START[0] // 16], 0x80)
        self.assertEqual(raster[0], 0x7F)                       # bounds corner, far away
        self.assertEqual(raster[(500 // 16) * 64 + 500 // 16], 0x7F)   # loop infield centre
        self.assertTrue(all(v == 0x7F or v & 0x7F < count for v in raster))
        rules = sections[3]['data']
        self.assertEqual((len(rules), rules[68], struct.unpack_from('<H', rules, 66)[0]),
                         (72, 7, 0x80 | count - 1))
        route_data = sections[4]['data']
        modes = [struct.unpack_from('<I', route_data, 16 + i * 12 + 8)[0] for i in range(count)]
        self.assertEqual(modes[1], 3)                           # straight
        self.assertEqual(modes[2], 1)                           # into the corner
        sentinel = struct.unpack_from('<7I', route_data, len(route_data) - 28)
        self.assertEqual(sentinel, (0,) * 7)
        grid = [struct.unpack_from('<3I', sections[34]['data'], 8 + r * 12) for r in range(8)]
        self.assertEqual(grid[0], ((START[0] - 20) << 16, (START[1] + 8) << 16, 0))
        self.assertEqual(grid[7][1] >> 16, START[1] + 8 + 7 * 24)
        minimap = track_format.planar_image(sections[33]['data'][0x418:0xC18], 8, 8)
        self.assertEqual(minimap[(200 // 16) * 64 + 400 // 16], track_format.MINIMAP_ROAD)
        self.assertEqual(minimap[(500 // 16) * 64 + 500 // 16], 0)
        self.assertIn(track_format.MINIMAP_EDGE, minimap)
        self.assertEqual(struct.unpack_from('<I', sections[33]['data'], 4)[0], 130)
        standings = sections[23]['data']
        self.assertEqual(struct.unpack_from('<I', standings, 24)[0], 130)
        original = dict(((k, c), d) for k, c, _, d in self.sections)[(23, COURSE)]
        self.assertNotEqual(standings[0x420:0x720], original[0x420:0x720])
        self.assertEqual(standings[0x720:], original[0x720:])
        self.assertEqual(struct.unpack_from('<4I', sections[1]['data'], 4)[:3], (130, 3, 130))

    def test_png_round_trips_indexed_greyscale_and_filters(self):
        path = self.root / 'image.png'
        palette = [(i, 255 - i, i // 2) for i in range(20)]
        pixels = bytes((i * 7) % 20 for i in range(15 * 9))
        png_indexed.write(path, 15, 9, pixels, palette)
        self.assertEqual(png_indexed.read(path), (15, 9, pixels, palette))
        png_indexed.write(path, 3, 2, b'\x00\x80\xff\x10\x20\x30')
        self.assertEqual(png_indexed.read(path), (3, 2, b'\x00\x80\xff\x10\x20\x30', None))
        # Rows filtered with Sub, Up, Average and Paeth decode to the same bytes.
        rows = [b'\x01\x02\x03\x04', b'\x05\x06\x07\x08', b'\x09\x0a\x0b\x0c', b'\x0d\x0e\x0f\x10']
        raw = bytes([1, 1, 1, 1, 1, 2, 4, 4, 4, 4, 3, 7, 3, 3, 3, 4, 4, 1, 1, 1])
        chunk = lambda kind, data: (struct.pack('>I', len(data)) + kind + data +
                                    struct.pack('>I', zlib.crc32(kind + data)))
        path.write_bytes(png_indexed.SIGNATURE + chunk(b'IHDR', struct.pack('>IIBBBBB', 4, 4, 8, 0, 0, 0, 0)) +
                         chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
        self.assertEqual(png_indexed.read(path)[2], b''.join(rows))
        for colour in (0, 0x7FFF, 0x1234, 0x4210):
            self.assertEqual(png_indexed.rgb_to_bgr555(png_indexed.bgr555_to_rgb(colour)), colour)

    def test_validation_errors_are_named(self):
        route = json.loads((self.track / 'track.json').read_text())['route']
        tilemap = png_indexed.read(self.track / 'tilemap.png')[2]
        materials = json.loads((self.track / 'tileset.json').read_text())

        def with_tilemap(index, tile):
            data = bytearray(tilemap)
            data[index] = tile
            png_indexed.write(self.track / 'tilemap.png', 128, 128, data)

        def with_material(index, value):
            data = dict(materials)
            data['materials'] = list(materials['materials'])
            data['materials'][index] = value
            (self.track / 'tileset.json').write_text(json.dumps(data))

        def with_raster(value):
            raster = bytearray(png_indexed.read(self.track / 'raster.png')[2])
            raster[5] = value
            png_indexed.write(self.track / 'raster.png', 64, 64, raster)

        cases = {
            'BadJson': lambda: (self.track / 'track.json').write_text('{'),
            'UnknownField': lambda: self.edit(colour='red'),
            'MissingField': lambda: self.edit(finish_y=None),
            'IdOutOfRange': lambda: self.edit(id=200),
            'TemplateMissing': lambda: self.edit(template_course=99),
            'LapsOutOfRange': lambda: self.edit(laps=6),
            'SongOutOfRange': lambda: self.edit(song=0),
            'RouteTooShort': lambda: self.edit(route=route[:3]),
            'RouteTooLong': lambda: self.edit(route=route * 6),
            'RouteNotAligned': lambda: self.edit(route=[dict(route[0], x=203)] + route[1:]),
            'OutOfBounds': lambda: self.edit(route=[dict(route[0], x=4096)] + route[1:]),
            'SouthboundFinish': lambda: self.edit(route=route[::-1], ai_rows=None),
            'FinishCrossing': lambda: self.edit(route=route[1:] + route[:1], ai_rows=None),
            'StartPastFinish': lambda: self.edit(start={'x': 200, 'y': 480}),
            'StartOutsideRaster': lambda: self.edit(start={'x': 216, 'y': 632}),
            'TooManyObjects': lambda: self.edit(coins=[{'x': 8, 'y': 8}] * 65),
            'ObjectNotAligned': lambda: self.edit(item_boxes=[{'x': 9, 'y': 8}]),
            'GridFormat': lambda: self.edit(grid=[]),
            'PassthroughKind': lambda: self.edit(passthrough={'3': 'sky.bin'}),
            'MissingFile': lambda: (self.track / 'tilemap.png').unlink(),
            'ImageSize': lambda: png_indexed.write(self.track / 'minimap.png', 32, 32, bytes(1024), [(0, 0, 0)]),
            'MaterialName': lambda: with_material(1, 'mud'),
            'ReservedTileChanged': lambda: png_indexed.write(
                self.track / 'tileset.png', 128, 128, bytes(128 * 128), [(0, 0, 0)] * 64),
            'RasterValue': lambda: with_raster(100),
            'SkyFormat': lambda: (self.track / 'sky.bin').write_bytes(b'NOPE'),
        }
        custom = {
            'ReservedTileUsed': lambda: with_tilemap(5, 0xD3),
            'ForbiddenMaterial': lambda: with_material(2, '0x02'),
            'NameTooLong': lambda: (self.edit(name='A VERY LONG TRACK NAME HERE'),
                                    (self.track / 'name.png').unlink()),
        }
        snapshot = {path.name: path.read_bytes() for path in self.track.iterdir()}
        for code, mutate in list(cases.items()) + list(custom.items()):
            with self.subTest(code=code):
                for name, data in snapshot.items():
                    (self.track / name).write_bytes(data)
                if code in custom:
                    self.edit(id=150)
                    tile = bytearray(tilemap)
                    tile[0] = 2
                    png_indexed.write(self.track / 'tilemap.png', 128, 128, tile)
                mutate()
                with self.assertRaises(track_format.TrackError) as caught:
                    track_format.compile_track(self.track, self.base)
                self.assertEqual(caught.exception.code, code, str(caught.exception))
                self.assertTrue(str(caught.exception).startswith(code + ': '))

    def test_export_rejects_missing_courses(self):
        with self.assertRaises(track_format.TrackError) as caught:
            track_format.export_course(self.base, 77, self.root / 'none')
        self.assertEqual(caught.exception.code, 'MissingSection')
        with contextlib.redirect_stderr(io.StringIO()) as error:
            self.assertEqual(track_format.main(['export', str(self.pack), '77', str(self.root / 'x')]), 1)
        self.assertIn('MissingSection', error.getvalue())


if __name__ == '__main__':
    unittest.main()

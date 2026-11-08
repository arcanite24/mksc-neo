"""Independent source-image, scenery-bank and sky-screenblock oracles."""
from dataclasses import FrozenInstanceError, replace
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import course_3d_art as art
import course_3d_source
import track_format
from test_course_3d_source import fixture, COURSE


def words(*values):
    return struct.pack('<'+'I'*len(values),*values)


def argb_image(w,h,pixels,anchor=(-3,-5)):
    size = 32+w*h*4
    return struct.pack('<8s4H2h3I',b'MKSCIMG1',32,1,w,h,*anchor,0,32,size)+words(*pixels)


def indexed_image():
    # Index16 is opaque despite its low nibble0; sentinel255 is transparent.
    return struct.pack('<8s4H2h3I',b'MKSCIMG2',32,2,2,2,-1,-2,
                       2 | 16 << 16,32,40)+bytes((16,17,255,16))+struct.pack('<2H',0x801f,0x7c00)


def haunt_effect(version=2):
    cells=304 if version==2 else 160
    tiles=bytearray(8192);tiles[0x1200]=0x21;tiles[0x1200+511]=0x30
    palette=[0]*80;palette[66]=4;palette[67]=31<<10
    payload=words(0x31435846,8192,80,5,256,3,cells,0)
    payload+=b''.join(struct.pack('<IHH',0,2,1) for _ in range(5))
    payload+=tiles+struct.pack('<80H',*palette)+bytes(cells)
    return dict(version=version),payload


def scenery_payload(code=0xa10000,banks=4):
    placement = struct.pack('<iiBBHIB3x',12*65536+7,20*65536,13,0,224,code,5)
    data = words(0x314e4353,1,banks,0)+placement
    for bank in range(banks):
        for lod,side in enumerate((8,4,2,1)):
            image = argb_image(side,side,[0xff000000|(bank+1)*0x1100+lod]*(side*side))
            data += words(lod,len(image))+image
    return data


def actor_payload(magic, art_magic, frames, variant=0, order=29):
    placements = struct.pack('<4sIiiHBB',magic,1,12*65536+7,20*65536+9,variant,127,order)
    images = struct.pack('<4sI',art_magic,frames)
    for frame in range(frames):
        for lod,side in enumerate((4,2,1,1)):
            image = argb_image(side,side,[0xff000000 | frame << 8 | lod]*(side*side))
            images += words(lod,len(image))+image
    return placements,images


def sky_payload(version=1):
    # Tile0 has a position-dependent nibble, tile1 is solid palette index2.
    tiles = bytearray(64)
    for y in range(8):
        for x in range(8):
            n = (x+2*y)%4
            tiles[y*4+x//2] |= n << (4*(x&1))
    tiles[32:] = bytes([0x22])*32
    palette = [0x7fff]*32
    palette[1:4] = [31,31<<5,31<<10]
    palette[16] = 31  # Every bank's nibble0 remains transparent.
    palette[17:20] = [31<<10,31,31<<5]
    front = bytearray(struct.pack('<H',0x6001)*1024)
    mid = bytearray(struct.pack('<H',0x6001)*2048)
    back = bytearray(struct.pack('<H',0x6001)*2048)
    # First map has normal/H/V/both flipped cells and an absent tile reference.
    for i,entry in enumerate((0x6000,0x6400,0x6800,0x6c00,0x6002,0x5001,0x7000)):
        struct.pack_into('<H',front,i*2,entry)
    struct.pack_into('<H',mid,1024*2,0x7001)  # x256: second horizontal block.
    struct.pack_into('<H',back,1024*2,0x7001) # y256: second vertical block.
    motion = dict(controller=3,layer_mask=5,blend_mode=1,blend_a=8,blend_b=16,
                  yaw_shift=[1,4,7],x_bias=[-3,5,9],y_bias=[8,-10,12],split_y=75,
                  split_layer=2,split_yaw_shift=6,split_x_bias=-29,
                  lightning_hide_layer=1,layer_source=[2,0,1],road_mode=2)
    tail = track_format.sky_motion_bytes(motion) if version == 2 else b''
    header = words(0x31594b53,64,32,len(front),len(mid),len(back),32,64,32,32,
                   0x40,3,0x1234,96,19,len(tail))
    return header+tiles+struct.pack('<32H',*palette)+front+mid+back+tail


class ArtTests(unittest.TestCase):
    def test_pier_haunt_exact_tiles_palette_anchor_mirror_and_transparency(self):
        for version in (1,2):
            section=haunt_effect(version)
            first,second=art.decode_pier_haunt(section)
            self.assertEqual((first.width,first.height,first.anchor_x,first.anchor_y),(32,32,-16,-16))
            self.assertEqual(first.pixels[0],0xff000000)
            self.assertEqual(first.pixels[1],0xff200000) #4*255//31, not bit replication33.
            self.assertEqual(first.pixels[2],0)
            self.assertEqual(first.pixels[-1],0xff0000ff)
            self.assertEqual(second.pixels[31],first.pixels[0])
            self.assertEqual(second.pixels[30],first.pixels[1])
            self.assertEqual(second.pixels[-32],first.pixels[-1])
            self.assertEqual(art.decode_pier_haunt(section),(first,second))
        meta,payload=haunt_effect()
        for offset,value in ((0,0),(4,8191),(8,79),(12,6),(16,255),(20,2),(24,303),(28,1),
                             (32,305),(36,0),(38,0)):
            broken=bytearray(payload)
            struct.pack_into('<I' if offset<36 else '<H',broken,offset,value)
            with self.assertRaisesRegex(ValueError,'Pier Boo haunt:'):
                art.decode_pier_haunt((meta,bytes(broken)))
        with self.assertRaisesRegex(ValueError,'Pier Boo haunt:'):
            art.decode_pier_haunt((meta,payload[:-1]))
        with self.assertRaisesRegex(ValueError,'Pier Boo haunt:'):
            art.decode_pier_haunt((dict(version=3),payload))

    def test_all_actor_families_preserve_variants_rows_frames_and_lods(self):
        cases = (
            ('lava_bubbles',39,40,b'LVB1',b'LBA1',2,12,2),
            ('thwomps',41,42,b'TWP1',b'TWA1',1,11,2),
            ('crabs',43,44,b'CRB1',b'CRA1',4,0x8002,1),
            ('seagulls',45,46,b'SGL1',b'SGA1',3,0,1),
            ('cannonballs',47,48,b'CBL1',b'CBA1',7,3,1),
            ('piranhas',49,50,b'PRN1',b'PRA1',5,0,1),
            ('snowmen',51,52,b'SNM1',b'SNA1',2,0,1),
            ('penguins',53,54,b'PGN1',b'PGA1',6,1,1),
            ('boos',55,56,b'BOO1',b'BOA1',2,1,1),
            ('mice',73,74,b'MUS1',b'MSA1',2,0x8001,1),
            ('thunderclouds',75,76,b'TCD1',b'TCA1',72,3,1),
            ('falling_stars',77,78,b'STR1',b'SRA1',5,5,1),
        )
        sections = []
        for family,kind,art_kind,magic,art_magic,frames,variant,version in cases:
            with self.subTest(family=family):
                placements,images = actor_payload(magic,art_magic,frames,variant,200)
                before = placements,images
                value = art.decode_actor_payload(family,placements,images,version,course_id=77)
                self.assertEqual((value.family,value.placement_kind,value.art_kind),(family,kind,art_kind))
                self.assertEqual(value.placements,(art.ActorPlacement(12*65536+7,20*65536+9,variant,127,200),))
                self.assertEqual(len(value.banks),frames)
                self.assertEqual(value.palette_phases,9 if family=='thunderclouds' else 1)
                for frame,bank in enumerate(value.banks):
                    self.assertEqual(bank.distance_shifts,(0,1,2,3))
                    self.assertEqual(bank.full_resolution_index,0)
                    for lod,image in enumerate(bank.lods):
                        self.assertEqual((image.anchor_x,image.anchor_y),(-3,-5))
                        self.assertEqual(image.pixels,(0xff000000 | frame << 8 | lod,)*(image.width*image.height))
                self.assertEqual((placements,images),before)
                with self.assertRaises(FrozenInstanceError):
                    value.placements[0].variant = 0
                bad = bytearray(placements)
                struct.pack_into('<H',bad,16,0x7fff)
                with self.assertRaisesRegex(art.ArtError,f'course=77 {family}.*placement'):
                    art.decode_actor_payload(family,bad,images,version,course_id=77)
                sections.extend((course_3d_source.SourceSection(kind,version,placements),
                                 course_3d_source.SourceSection(art_kind,1,images)))
        source = replace(course_3d_source.read_course(fixture(),COURSE),passthrough=tuple(reversed(sections)))
        self.assertEqual([value.family for value in art.decode_actors(source)],[row[0] for row in cases])

    def test_actor_legacy_order_native_pixels_and_cloud_palette_phases(self):
        placements,images = actor_payload(b'LVB1',b'LBA1',2,order=0)
        placements = placements[:4]+words(2)+placements[8:]+placements[8:]
        value = art.decode_actor_payload('lava_bubbles',placements,images,version=1)
        self.assertEqual([p.placement_order for p in value.placements],[0,1])
        bad = bytearray(placements)
        bad[19] = 1
        with self.assertRaisesRegex(art.ArtError,'order'):
            art.decode_actor_payload('lava_bubbles',bad,images,version=1)
        placements,images = actor_payload(b'TCD1',b'TCA1',72)
        value = art.decode_actor_payload('thunderclouds',placements,images)
        self.assertEqual([value.banks[phase*8].image.pixels[0] for phase in range(9)],
                         [0xff000000 | phase*8 << 8 for phase in range(9)])
        # Indexed alpha sentinel and native anchors pass through the same actor
        # bank parser. The larger second LOD becomes full-resolution explicitly.
        placements,_ = actor_payload(b'TWP1',b'TWA1',1,order=0)
        native = [indexed_image(),argb_image(4,4,[0xff000000,0,0x7f112233,0xffabcdef]*4),
                  argb_image(1,1,[0]),argb_image(1,1,[0xff000000])]
        images = b'TWA1'+words(1)+b''.join(words(i,len(data))+data for i,data in enumerate(native))
        value = art.decode_actor_payload('thwomps',placements,images,version=1)
        self.assertEqual(value.banks[0].lods[0].pixels,(0xffff0000,0xff0000ff,0,0xffff0000))
        self.assertEqual(value.banks[0].full_resolution_index,1)
        self.assertEqual(value.banks[0].image.pixels[:4],(0xff000000,0,0x7f112233,0xffabcdef))

    def test_actor_malformed_pairs_headers_domains_and_images_fail_named(self):
        placements,images = actor_payload(b'CRB1',b'CRA1',4)
        for offset,value in ((8,0xffffffff),(12,2048*65536+1),(16,2),(18,128)):
            data = bytearray(placements)
            if offset==16:struct.pack_into('<H',data,offset,value)
            elif offset==18:data[offset]=value
            else:struct.pack_into('<I',data,offset,value)
            before = bytes(data)
            with self.subTest(offset=offset), self.assertRaisesRegex(art.ArtError,'course=9 crabs'):
                art.decode_actor_payload('crabs',data,images,course_id=9)
            self.assertEqual(bytes(data),before)
        for data in (images[:-1],images+b'!',b'BAD!'+images[4:],images[:4]+words(3)+images[8:]):
            with self.assertRaisesRegex(art.ArtError,'crabs'):
                art.decode_actor_payload('crabs',placements,data)
        bad = bytearray(images)
        struct.pack_into('<I',bad,8,4)
        with self.assertRaisesRegex(art.ArtError,'span/shift'):
            art.decode_actor_payload('crabs',placements,bad)
        bad = bytearray(images)
        struct.pack_into('<h',bad,16+16,-65)
        with self.assertRaisesRegex(art.ArtError,'anchors'):
            art.decode_actor_payload('crabs',placements,bad)
        with self.assertRaisesRegex(art.ArtError,'version'):
            art.decode_actor_payload('crabs',placements,images,art_version=2)
        with self.assertRaisesRegex(art.ArtError,'unknown family'):
            art.decode_actor_payload('unknown',placements,images)
        source = replace(course_3d_source.read_course(fixture(),COURSE),passthrough=())
        self.assertEqual(art.decode_actors(source),())
        p = course_3d_source.SourceSection(43,1,placements)
        a = course_3d_source.SourceSection(44,1,images)
        for sections in ((p,),(a,),(p,p,a),(p,a,a)):
            with self.subTest(kinds=[s.kind for s in sections]), self.assertRaisesRegex(art.ArtError,'paired or duplicate'):
                art.decode_actors(replace(source,passthrough=sections))

    def test_scenery_preserves_all_banks_lods_anchors_and_inverse_scale(self):
        value = art.decode_scenery_payload(scenery_payload(),course_id=99)
        self.assertEqual(len(value.banks),4)
        placement = value.placements[0]
        self.assertEqual((placement.x_q16,placement.y_q16,placement.parameter,placement.art_index,
                          placement.affine_q8,placement.source_code,placement.placement_order),
                         (12*65536+7,20*65536,13,0,224,0xa10000,5))
        self.assertEqual(placement.frame_bank_indices,(0,1,2,3))
        for bank in range(4):
            image = value.banks[bank].image
            self.assertEqual((image.width,image.height,image.anchor_x,image.anchor_y),(8,8,-3,-5))
            self.assertEqual(value.banks[bank].distance_shifts,(0,1,2,3))
            self.assertEqual(image.pixels,(0xff000000|(bank+1)*0x1100,)*64)
        with self.assertRaises(FrozenInstanceError):
            placement.affine_q8 = 256

    def test_native_images_argb_alpha_and_indexed_sentinel(self):
        images = [indexed_image(),argb_image(2,2,[0,0x7f112233,0x80112233,0xff112233]),
                  argb_image(1,1,[0xff778899]),argb_image(1,1,[0xffaabbcc])]
        data = words(0x314e4353,0,1,0)+b''.join(words(0,len(p))+p for p in images)
        value = art.decode_scenery_payload(data)
        self.assertEqual(value.banks[0].full_resolution_index,0)
        self.assertEqual(value.banks[0].image.pixels,(0xffff0000,0xff0000ff,0,0xffff0000))
        self.assertEqual(value.banks[0].lods[1].pixels,(0,0x7f112233,0x80112233,0xff112233))

    def test_missing_frame_banks_and_bad_images_reject_without_mutation(self):
        original = bytearray(scenery_payload())
        for label,mutate in (
            ('placement',lambda p: struct.pack_into('<H',p,26,0)),
            ('frame banks',lambda p: p.__setitem__(25,3)),
            ('span/shift',lambda p: struct.pack_into('<I',p,36,4)),
            ('image',lambda p: struct.pack_into('<H',p,44+12,129)),
        ):
            data = original.copy()
            mutate(data)
            before = bytes(data)
            with self.subTest(label=label), self.assertRaises(art.ArtError):
                art.decode_scenery_payload(data,course_id=77)
            self.assertEqual(bytes(data),before)
        with self.assertRaisesRegex(art.ArtError,'trailing'):
            art.decode_scenery_payload(original+b'!')
        self.assertEqual(original,bytearray(scenery_payload()))

    def test_sky_flips_palette_base_and_missing_tiles_are_transparent(self):
        value = art.decode_sky_payload(sky_payload(),course_id=7)
        colors = (0,0xffff0000,0xff00ff00,0xff0000ff)
        front = value.front
        for y in range(8):
            for x in range(8):
                for cell,(flipx,flipy) in enumerate(((False,False),(True,False),(False,True),(True,True))):
                    sx,sy = 7-x if flipx else x,7-y if flipy else y
                    self.assertEqual(front.pixels[y*256+cell*8+x],colors[(sx+2*sy)%4])
        self.assertTrue(all(front.pixels[y*256+x] == 0 for y in range(8) for x in range(32,48)))
        self.assertEqual(front.pixels[48],0) # bank7 nibble0, nonblack palette16.
        self.assertEqual(front.pixels[49],0xff0000ff)
        self.assertIsNone(value.motion)
        self.assertEqual((value.palette_hardware_base,value.background_index,value.descriptor),(96,19,0x1234))

    def test_sky_screenblocks_wrap_independent_layer_extents_and_profile(self):
        value = art.decode_sky_payload(sky_payload(2),version=2)
        self.assertEqual((value.front.width,value.front.height),(256,256))
        self.assertEqual((value.mid.width,value.mid.height),(512,256))
        self.assertEqual((value.back.width,value.back.height),(256,512))
        self.assertEqual(value.mid.pixels[256],0xffff0000)
        self.assertEqual(value.mid.pixels[0],0xff00ff00)
        self.assertEqual(value.back.pixels[256*256],0xffff0000)
        self.assertEqual(value.mid.sample_wrapped(-256,256),value.mid.pixels[256])
        self.assertEqual(value.back.sample_wrapped(256,-256),value.back.pixels[256*256])
        self.assertEqual((value.motion.x_bias,value.motion.layer_source,value.motion.split_y),
                         ((-3,5,9),(2,0,1),75))
        self.assertEqual((value.motion.blend_a,value.motion.blend_b,value.motion.road_mode),(8,16,2))
        self.assertEqual(value.layers[1].map_data[2048:2050],struct.pack('<H',0x7001))

    def test_sky_version_length_and_motion_errors_are_named(self):
        for data,version in ((sky_payload(2),1),(sky_payload(),2),(sky_payload()[:-1],1)):
            with self.assertRaisesRegex(art.ArtError,'course=42 SKY1:'):
                art.decode_sky_payload(data,version,42)
        data = bytearray(sky_payload(2))
        data[-5] = 1 # SMR1 reserved byte35.
        with self.assertRaisesRegex(art.ArtError,'SMR1'):
            art.decode_sky_payload(data,2)

    def test_opt_in_sky_indices_keep_banks_flips_and_equal_seed_colors(self):
        data=bytearray(sky_payload(2))
        # Palette-relative1 and2 intentionally have identical seed RGB.
        struct.pack_into('<H',data,64+64+2*2,31)
        saved=bytes(data)
        plain=art.decode_sky_payload(data,2)
        indexed=art.decode_sky_payload(data,2,preserve_indices=True)
        self.assertEqual(indexed.palette_hardware_base,96)
        for old,new in zip(plain.layers,indexed.layers):
            self.assertIsNone(old.image.indices)
            self.assertEqual(new.image.pixels,old.image.pixels)
            self.assertEqual(len(new.image.indices),new.image.width*new.image.height)
            self.assertEqual(new.map_data,old.map_data)
        image=indexed.layers[0].image
        def at(x,y):return image.indices[y*image.width+x]
        # Normal,H,V,both flips retain nibble identities and palette-base mapping.
        self.assertEqual([at(x,0) for x in range(8)],[255,1,2,3,255,1,2,3])
        self.assertEqual([at(x,0) for x in range(8,16)],[3,2,1,255,3,2,1,255])
        self.assertEqual(at(16,0),2)
        self.assertEqual(at(24,0),1)
        self.assertEqual(image.pixels[1],image.pixels[2])
        self.assertNotEqual(at(1,0),at(2,0))
        # Missing tile, below-base bank and transparent nibble0 stay sentinel255.
        self.assertEqual([at(x,0) for x in (32,40,48)],[255,255,255])
        self.assertEqual(at(49,0),17)
        self.assertEqual(indexed.layers[1].image.indices[256],18)
        self.assertEqual(indexed.layers[2].image.indices[256*256],18)
        # Crop both planes with identical full-width row spans; retained texels
        # still refer to the original local palette, never nearest seed colors.
        start,end=image.width,3*image.width
        cropped=replace(image,height=2,pixels=image.pixels[start:end],indices=image.indices[start:end])
        self.assertEqual(cropped.indices[:image.width],image.indices[start:start+image.width])
        self.assertEqual(data,saved)
        source=replace(course_3d_source.read_course(fixture(),COURSE),sky=saved,sky_version=2)
        self.assertEqual(art.decode_sky(source,preserve_indices=True),indexed)
        self.assertEqual(art.decode_sky_payload(data,2,preserve_indices=False),plain)
        with self.assertRaisesRegex(art.ArtError,'preserve_indices'):
            art.decode_sky_payload(data,2,preserve_indices=1)

    def test_source_course_wrappers_retain_other_hazards_and_optional_absence(self):
        source = course_3d_source.read_course(fixture(),COURSE)
        scenery = course_3d_source.SourceSection(28,4,scenery_payload())
        hazard = course_3d_source.SourceSection(47,1,b'authored moving hazard bytes')
        source = replace(source,passthrough=(scenery,hazard),sky=sky_payload(2),sky_version=2)
        self.assertEqual(len(art.decode_scenery(source).placements),1)
        self.assertEqual(art.decode_sky(source).motion.controller,3)
        self.assertEqual(source.passthrough[1],hazard)
        self.assertEqual(art.decode_scenery(replace(source,passthrough=(hazard,))),art.SceneryArt((),()))
        self.assertIsNone(art.decode_sky(replace(source,sky=None,sky_version=0)))


if __name__ == '__main__':
    unittest.main()

"""Owned source scenery, actor and sky data, without scene generation.

Native image anchors, inverse-affine placement scales, every scenery bank/LOD
and original sky motion remain data. Actor placements retain source variants,
checkpoints and order. No animation, collision or clock runs here.
"""
from dataclasses import dataclass
import struct
from typing import Optional

import course_3d_source
import track_format


class ArtError(ValueError):
    pass


@dataclass(frozen=True)
class PixelImage:
    width: int
    height: int
    anchor_x: int
    anchor_y: int
    pixels: tuple
    indices: Optional[bytes] = None  # Palette-relative0..63;255 transparent.

    def sample_wrapped(self, x, y):
        """Sample integer texels with positive wrapping in both directions."""
        return self.pixels[(y % self.height)*self.width + x % self.width]


@dataclass(frozen=True)
class SceneryBank:
    lods: tuple
    distance_shifts: tuple
    full_resolution_index: int

    @property
    def image(self):
        return self.lods[self.full_resolution_index]


@dataclass(frozen=True)
class SceneryPlacement:
    x_q16: int
    y_q16: int
    parameter: int
    art_index: int
    affine_q8: int
    source_code: int
    placement_order: int
    height_q16: int
    frame_bank_indices: tuple


@dataclass(frozen=True)
class SceneryArt:
    placements: tuple
    banks: tuple


@dataclass(frozen=True)
class ActorPlacement:
    x_q16: int
    y_q16: int
    variant: int
    checkpoint: int
    placement_order: int


@dataclass(frozen=True)
class ActorFamily:
    name: str
    placement_kind: int
    art_kind: int
    placement_magic: bytes
    art_magic: bytes
    versions: tuple
    variants: tuple
    frames: int
    max_extent: int = 64
    palette_phases: int = 1


ACTOR_FAMILIES = (
    ActorFamily('lava_bubbles',39,40,b'LVB1',b'LBA1',(1,2),tuple(range(13)),2),
    ActorFamily('thwomps',41,42,b'TWP1',b'TWA1',(1,2),tuple(range(12)),1),
    ActorFamily('crabs',43,44,b'CRB1',b'CRA1',(1,),(0,1,0x8000,0x8001,0x8002),4),
    ActorFamily('seagulls',45,46,b'SGL1',b'SGA1',(1,),(0,),3),
    ActorFamily('cannonballs',47,48,b'CBL1',b'CBA1',(1,),tuple(range(4)),7,128),
    ActorFamily('piranhas',49,50,b'PRN1',b'PRA1',(1,),(0,),5),
    ActorFamily('snowmen',51,52,b'SNM1',b'SNA1',(1,),(0,),2),
    ActorFamily('penguins',53,54,b'PGN1',b'PGA1',(1,),(0,1),6),
    ActorFamily('boos',55,56,b'BOO1',b'BOA1',(1,),(0,1),2),
    ActorFamily('mice',73,74,b'MUS1',b'MSA1',(1,),(0,1,0x8001),2),
    ActorFamily('thunderclouds',75,76,b'TCD1',b'TCA1',(1,),tuple(range(4)),72,128,9),
    ActorFamily('falling_stars',77,78,b'STR1',b'SRA1',(1,),tuple(range(6)),5),
)


@dataclass(frozen=True)
class ActorArt:
    family: str
    placement_kind: int
    art_kind: int
    placements: tuple
    banks: tuple
    palette_phases: int


@dataclass(frozen=True)
class SkyMotion:
    controller: int
    layer_mask: int
    blend_mode: int
    blend_a: int
    blend_b: int
    yaw_shift: tuple
    x_bias: tuple
    y_bias: tuple
    split_y: int
    split_layer: int
    split_yaw_shift: int
    split_x_bias: int
    lightning_hide_layer: int
    layer_source: tuple
    road_mode: int


@dataclass(frozen=True)
class SkyLayer:
    name: str
    image: PixelImage
    map_width_tiles: int
    map_height_tiles: int
    map_data: bytes


@dataclass(frozen=True)
class SkyArt:
    layers: tuple
    motion: Optional[SkyMotion]
    palette: tuple
    palette_hardware_base: int
    size: int
    compression_flags: int
    descriptor: int
    background_index: int
    header_map_height_tiles: int
    tiles: bytes

    @property
    def front(self):
        return self.layers[0].image

    @property
    def mid(self):
        return self.layers[1].image

    @property
    def back(self):
        return self.layers[2].image


def _fail(course, kind, message):
    raise ArtError(f'course={course} {kind}: {message}')


def _payload(data, course, kind):
    if not isinstance(data, (bytes, bytearray, memoryview)):
        _fail(course, kind, 'expected byte payload')
    return bytes(data)


def decode_pier_haunt(section):
    """4886C's two32px mirrored cells from global FXC1 tile190/palette7.

    The prop's feet anchor is unrelated. These centered cells preserve the
    existing boo_attack_renderer RGB555 division and index-zero transparency.
    """
    meta,data=section
    version=meta.get('version')
    if version not in (1,2) or len(data)<32:
        raise ValueError('Pier Boo haunt: unsupported FXC1 header')
    magic,tiles,palettes,frames,tile_base,palette_base,cells,reserved=struct.unpack_from('<8I',data)
    expected_cells=304 if version==2 else 160
    if (magic!=0x31435846 or tiles!=8192 or palettes!=80 or frames!=5 or tile_base!=256
            or palette_base!=3 or cells!=expected_cells or reserved
            or len(data)!=32+frames*8+tiles+palettes*2+cells):
        raise ValueError('Pier Boo haunt: invalid global FXC1 layout')
    for index in range(frames):
        offset,size,duration=struct.unpack_from('<IHH',data,32+index*8)
        if size<2 or not 1<=duration<=255 or offset+size>cells:
            raise ValueError('Pier Boo haunt: invalid FXC1 frame range')
    tile_start=32+frames*8
    palette=struct.unpack_from('<80H',data,tile_start+tiles)
    pixels=[]
    for y in range(32):
        for x in range(32):
            offset=(0x190-tile_base)*32+(y//8*4+x//8)*32+(y%8)*4+x%8//2
            color=(data[tile_start+offset]>>(4*(x%2)))&15
            value=palette[(7-palette_base)*16+color]
            pixels.append(0 if color==0 else 0xff000000 |
                          ((value&31)*255//31)<<16 | (((value>>5)&31)*255//31)<<8 |
                          ((value>>10)&31)*255//31)
    mirrored=tuple(pixels[y*32+31-x] for y in range(32) for x in range(32))
    return (PixelImage(32,32,-16,-16,tuple(pixels)),PixelImage(32,32,-16,-16,mirrored))


def _color(value):
    r, g, b = value & 31, (value >> 5) & 31, (value >> 10) & 31
    return 0xff000000 | ((r*8+r//4) << 16) | ((g*8+g//4) << 8) | (b*8+b//4)


def _image(data, course, label='SCN1 image'):
    """Match native_image.c: IMG2 uses sentinel255, not palette-nibble zero."""
    if len(data) < 32:
        _fail(course, label, 'truncated image header')
    magic, header, version, w, h, ax, ay, palette_word, offset, total = struct.unpack_from('<8s4H2h3I', data)
    if header != 32 or offset != 32 or total != len(data) or not (0 < w <= 128 and 0 < h <= 128) or \
            not (-128 <= ax <= 128 and -128 <= ay <= 128):
        _fail(course, label, 'invalid dimensions, anchors or length')
    count = w*h
    if magic == b'MKSCIMG1' and version == 1:
        if palette_word or len(data) != 32 + count*4:
            _fail(course, label, 'invalid ARGB image layout')
        pixels = struct.unpack_from('<'+'I'*count, data, 32)
    elif magic == b'MKSCIMG2' and version == 2:
        palette_count, palette_base = palette_word & 65535, palette_word >> 16
        if not 0 < palette_count <= 128 or palette_base+palette_count > 128 or \
                len(data) != 32+count+palette_count*2:
            _fail(course, label, 'invalid indexed palette layout')
        colors = tuple(_color(c) for c in struct.unpack_from('<'+'H'*palette_count, data, 32+count))
        pixels = []
        for index in data[32:32+count]:
            if index == 255:
                pixels.append(0)
            elif palette_base <= index < palette_base+palette_count:
                pixels.append(colors[index-palette_base])
            else:
                _fail(course, label, 'pixel outside indexed palette')
        pixels = tuple(pixels)
    else:
        _fail(course, label, 'unsupported native image codec/version')
    return PixelImage(w, h, ax, ay, pixels)


def decode_scenery_payload(data, version=4, course_id=0):
    """Decode SCN1/v4, retaining all banks for source-driven frame selection.

    affine_q8 is an inverse scale: consumer geometry uses 256/affine_q8, and
    each selected image additionally retains its own distance shift and anchor.
    Moving/contact families are not reduced to an initial frame here.
    """
    data = _payload(data, course_id, 'SCN1')
    if version != 4 or len(data) < 16:
        _fail(course_id, 'SCN1', 'expected version4 header')
    magic, count, banks, reserved = struct.unpack_from('<4I', data)
    if magic != 0x314e4353 or count > 64 or banks > 64 or reserved or len(data) < 16+count*20:
        _fail(course_id, 'SCN1', 'invalid placement/bank count or header')
    placements = []
    at = 16
    for i in range(count):
        x, y, parameter, art, affine, code, order, pad, zero = struct.unpack_from('<iiBBHIBBH', data, at)
        at += 20
        if not (0 <= x <= 2048*65536 and 0 <= y <= 2048*65536) or art >= banks or \
                not 0 < affine <= 256 or order >= 64 or pad or zero:
            _fail(course_id, 'SCN1', f'invalid placement {i}')
        # These extra banks are required by the existing source prop owner.
        family = code >> 16
        frames = 11 if family == 3 else 5 if family == 0xa2 else 4 if family == 0xa1 else (
            2 if family == 0xa0 or code == 0x860000 else 1)
        if art+frames > banks:
            _fail(course_id, 'SCN1', f'placement {i} lacks its source frame banks')
        height = -8*65536 if family == 1 else 8*65536 if family == 0xa3 else 0
        placements.append(SceneryPlacement(x,y,parameter,art,affine,code,order,height,
                                           tuple(range(art,art+frames))))
    decoded, at = _banks(data, at, banks, course_id, 'SCN1')
    if at != len(data):
        _fail(course_id, 'SCN1', 'trailing bytes')
    return SceneryArt(tuple(placements),decoded)


def _banks(data, at, banks, course_id, label, max_extent=128, max_anchor=128):
    """Shared bank/LOD decoder; actor families retain stricter native limits."""
    decoded = []
    for bank in range(banks):
        images, shifts = [], []
        for lod in range(4):
            if at+8 > len(data):
                _fail(course_id, label, f'truncated bank {bank} LOD {lod}')
            shift, size = struct.unpack_from('<2I', data, at)
            at += 8
            if shift > 3 or size > len(data)-at:
                _fail(course_id, label, f'invalid bank {bank} LOD {lod} span/shift')
            image = _image(data[at:at+size], course_id, f'{label} image')
            if image.width > max_extent or image.height > max_extent or \
                    abs(image.anchor_x) > max_anchor or abs(image.anchor_y) > max_anchor:
                _fail(course_id, label, f'bank {bank} LOD {lod} exceeds native dimensions/anchors')
            images.append(image)
            shifts.append(shift)
            at += size
        # Stable ties keep the source's first/full bank instead of guessing.
        full = max(range(4), key=lambda index: images[index].width*images[index].height)
        decoded.append(SceneryBank(tuple(images),tuple(shifts),full))
    return tuple(decoded), at


def decode_scenery(course):
    """Read the optional kind28 SourceCourse passthrough, without hazards."""
    if not isinstance(course, course_3d_source.SourceCourse):
        raise ArtError('SCN1: expected SourceCourse')
    sections = [section for section in course.passthrough if section.kind == 28]
    if not sections:
        return SceneryArt((),())
    if len(sections) != 1:
        _fail(course.course_id, 'SCN1', 'duplicate scenery sections')
    return decode_scenery_payload(sections[0].data,sections[0].version,course.course_id)


def decode_actor_payload(family, placements, images, version=1, art_version=1, course_id=0):
    """Decode one original actor family without running motion/contact clocks.

    Frame banks retain source order and all four LODs. Thundercloud banks are
    nine consecutive palette phases of eight frames each. Lava/Thwomp v1's
    reserved order byte is zero and its effective placement order is the row.
    """
    spec = next((item for item in ACTOR_FAMILIES if item.name == family),None)
    if spec is None:
        _fail(course_id, 'actor', f'unknown family {family!r}')
    label = f'{family} kind{spec.placement_kind}/{spec.art_kind}'
    placements = _payload(placements,course_id,label)
    images = _payload(images,course_id,label)
    if version not in spec.versions or art_version != 1 or len(placements) < 8 or len(images) < 8:
        _fail(course_id,label,'unsupported version or truncated header')
    magic,count = struct.unpack_from('<4sI',placements)
    art_magic,frames = struct.unpack_from('<4sI',images)
    if magic != spec.placement_magic or count > 64 or len(placements) != 8+count*12:
        _fail(course_id,label,'invalid placement magic/count/length')
    if art_magic != spec.art_magic or frames != spec.frames:
        _fail(course_id,label,'invalid art magic/frame count')
    result = []
    for i in range(count):
        x,y,variant,checkpoint,order = struct.unpack_from('<iiHBB',placements,8+i*12)
        legacy = spec.placement_kind in (39,41) and version == 1
        if not (0 <= x <= 2048*65536 and 0 <= y <= 2048*65536) or \
                variant not in spec.variants or checkpoint >= 128 or (legacy and order):
            _fail(course_id,label,f'invalid placement {i} position/variant/checkpoint/order')
        result.append(ActorPlacement(x,y,variant,checkpoint,i if legacy else order))
    banks,at = _banks(images,8,frames,course_id,label,spec.max_extent,64)
    if at != len(images):
        _fail(course_id,label,'trailing art bytes')
    return ActorArt(family,spec.placement_kind,spec.art_kind,tuple(result),banks,spec.palette_phases)


def decode_actors(course):
    """Return optional paired actor families in canonical source-kind order.

    Present unpaired or duplicate sections fail rather than dropping hazards.
    This only owns decoded data; it does not turn moving actors into scenery.
    """
    if not isinstance(course,course_3d_source.SourceCourse):
        raise ArtError('actors: expected SourceCourse')
    result = []
    for spec in ACTOR_FAMILIES:
        placements = [s for s in course.passthrough if s.kind == spec.placement_kind]
        images = [s for s in course.passthrough if s.kind == spec.art_kind]
        if not placements and not images:
            continue
        if len(placements) != 1 or len(images) != 1:
            _fail(course.course_id,spec.name,'missing paired or duplicate actor sections')
        result.append(decode_actor_payload(spec.name,placements[0].data,images[0].data,
            placements[0].version,images[0].version,course.course_id))
    return tuple(result)


def _sky_layer(name, data, width, tiles, palette, palette_base, preserve_indices=False):
    height = len(data)//(width*2)
    output = [0]*(width*height*64)
    indices = bytearray([255])*len(output) if preserve_indices else None
    # Text backgrounds store complete32x32 screenblocks, not a linear64-wide
    # map. This also handles a32x64 map's lower screenblock without a seam.
    for ty in range(height):
        for tx in range(width):
            index = ((ty//32)*(width//32)+tx//32)*1024+(ty%32)*32+tx%32
            entry = struct.unpack_from('<H',data,index*2)[0]
            tile = entry & 1023
            bank = (entry >> 12)*16
            for py in range(8):
                sy = 7-py if entry & 0x800 else py
                for px in range(8):
                    sx = 7-px if entry & 0x400 else px
                    address = tile*32+sy*4+sx//2
                    if address >= len(tiles):
                        continue  # Source renderer treats missing tile bytes as zero.
                    nibble = (tiles[address] >> ((sx&1)*4)) & 15
                    color = bank+nibble-palette_base
                    if nibble and 0 <= color < len(palette):
                        target=(ty*8+py)*width*8+tx*8+px
                        output[target] = _color(palette[color])
                        if indices is not None:indices[target]=color
    image = PixelImage(width*8,height*8,0,0,tuple(output),
                       bytes(indices) if indices is not None else None)
    return SkyLayer(name,image,width,height,data)


def decode_sky_payload(data, version=1, course_id=0, *, preserve_indices=False):
    """Decode normalized SKY1; layer sampling wraps at each full map extent.

    No compositing/parallax time is evaluated. Original SMR1 coefficients and
    layer remapping are returned intact for the presentation owner. Optional
    image.indices retains palette-relative identities, with255 for transparency.
    """
    if type(preserve_indices) is not bool:
        _fail(course_id,'SKY1','preserve_indices must be boolean')
    data = _payload(data,course_id,'SKY1')
    try:
        track_format.sky_layout(data)
        motion = track_format.sky_motion(data)
    except (track_format.TrackError,struct.error) as error:
        _fail(course_id,'SKY1',str(error))
    fields = struct.unpack_from('<16I',data)
    tn, pn = fields[1:3]
    if version not in (1,2) or fields[15] != (40 if version == 2 else 0) or \
            any(size > 4096 for size in fields[3:6]) or \
            fields[10] > 255 or fields[11] > 255 or fields[12] > 65535 or fields[13] > 256:
        _fail(course_id,'SKY1','invalid version, extension or source metadata bounds')
    at = 64
    tiles = data[at:at+tn]
    at += tn
    palette = struct.unpack_from('<'+'H'*pn,data,at)
    at += pn*2
    layers = []
    for i,name in enumerate(('front','mid','back')):
        map_bytes = fields[3+i]
        layers.append(_sky_layer(name,data[at:at+map_bytes],fields[6+i],tiles,palette,fields[13],
                                 preserve_indices))
        at += map_bytes
    profile = None if motion is None else SkyMotion(**{
        key:tuple(value) if isinstance(value,list) else value for key,value in motion.items()})
    return SkyArt(tuple(layers),profile,palette,fields[13],fields[10],fields[11],fields[12],
                  fields[14],fields[9],tiles)


def decode_sky(course, *, preserve_indices=False):
    if type(preserve_indices) is not bool:
        _fail(getattr(course,'course_id',0),'SKY1','preserve_indices must be boolean')
    if not isinstance(course,course_3d_source.SourceCourse):
        raise ArtError('SKY1: expected SourceCourse')
    return None if course.sky is None else decode_sky_payload(course.sky,course.sky_version,course.course_id,
                                                             preserve_indices=preserve_indices)

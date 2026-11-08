"""Normalize the US cup and course selector's art from ROM tables."""
import struct
from course_rom import Data
from ui_rom import affine_tile_map, cell_image, decode_lz77, image_asset, tile_map, tile_pixel

COURSE_BACKGROUND = 0x23000
COURSE_HEADING = 0x23010
COURSE_CUP = 0x23100
COURSE_LOCKED_CUP = 0x23120
COURSE_CUP_NAME = 0x23200
COURSE_PREVIEW = 0x23300
COURSE_NAME = 0x23400
COURSE_SELECTED_NAME = 0x23500
COURSE_AFFINE = 0x23020
COURSE_PANEL = 0x23030
COURSE_OK = 0x23038
# 0800C78C records list: names on the panel's two stripe palettes (B2310,
# B2330), the 79F18 panel, its BEST RECORD / BEST LAP labels and the
# 0800C6D4 time glyphs (digits, ', ", dash) from tiles ACAFC/ABEFC.
RECORDS_NAME = 0x23600        # +stripe*64 + course 0..39
RECORDS_PANEL = 0x23800
RECORDS_BEST = 0x23801
RECORDS_LAP = 0x23802
RECORDS_GLYPHS = 0x23803      # 14 cells of 8x16: 0-9, ', ", dash (two cells)
# GP cup records for the highlighted cup and class: sub_8009590's RANK cell
# C8FE4 (+rating 0..7: three/two/one stars, A..E) and sub_8009754's trophy
# C8F60 (+medal*16+frame: gold/silver/bronze, 16 frames of 7 ticks). Images
# keep the cell offsets as anchors, around the cell's screen position.
COURSE_RANK = 0x23900
COURSE_TROPHY = 0x23a00


def origin(image):
    data = bytearray(image)
    struct.pack_into('<hh', data, 16, 0, 0)
    return bytes(data)


def preview_panel_palette(rom, palette, cup):
    """8B58's cup colors and 9124's five bank15 panel colors."""
    if not 0 <= cup < 5 or len(palette) != 512:
        raise ValueError('invalid preview panel palette')
    colors = struct.unpack_from('<I', rom, 0xd94a0+cup*4)[0]-0x08000000
    if not 0 <= colors <= len(rom)-10:
        raise ValueError('preview panel palette outside ROM')
    result = bytearray(palette)
    result[14:32] = rom[0xae11c+cup*20:0xae12e+cup*20]
    result[0x1e0:0x1ea] = rom[colors:colors+10]
    return result


def locked_cup_palette(rom, palette, cup):
    """860C: unavailable cup banks halve RGB555; Special uses B1398."""
    if not 0 <= cup < 5 or len(palette) != 512:
        raise ValueError('invalid locked cup palette')
    result = bytearray(palette)
    start = 0x100+cup*32
    for index in range(16):
        color = struct.unpack_from('<H', result, start+index*2)[0]
        dim = sum((((color >> shift) & 31) >> 1) << shift for shift in (0, 5, 10))
        struct.pack_into('<H', result, start+index*2, dim)
    if cup == 4:
        if len(rom) < 0xb13b8:
            raise ValueError('locked cup palette outside ROM')
        result[start:start+32] = rom[0xb1398:0xb13b8]
    return result


def course_sections(rom):
    def pointer(address):
        value = struct.unpack_from('<I', rom, address)[0] - 0x08000000
        if not 0 <= value < len(rom):
            raise ValueError('course selector pointer outside ROM')
        return value

    # spm_loadBgGfx (0800A8F0): shared cells plus cup icons and names.
    tiles = bytearray(32768)
    palette = bytearray(512)
    palette[:64] = rom[0xaf2c0:0xaf300]
    palette[0xe0:0xe0+len(decode_lz77(rom,0xb06dc))] = decode_lz77(rom,0xb06dc)
    palette[0x1a0:0x1a0+len(decode_lz77(rom,0xb07e4))] = decode_lz77(rom,0xb07e4)
    palette[0x100:0x1a0] = rom[0xb75c0:0xb7660]
    for address, offset in ((0xaf320,0), (0xc2fa8,0x1000),
                            (0xb7720,0x3c00), (0xb9950,0x3800),
                            (0xb0848,0x5c00), (0xb0e28,0x4c00)):
        data = decode_lz77(rom,address)
        tiles[offset:offset+len(data)] = data
    sections = [(15,COURSE_HEADING,origin(cell_image(rom,pointer(0xd95bc),tiles,palette))),
                (15,COURSE_HEADING+1,origin(cell_image(rom,pointer(0xd9588),tiles,palette)))]
    pattern_tiles = decode_lz77(rom,0x7d1e8)
    affine_tiles = decode_lz77(rom,0xae180) + decode_lz77(rom,0xae7c8)
    affine_map = decode_lz77(rom,0xaee04)
    cup_maps = (0x7dcb4,0x7ddc8,0x7dedc,0x7dff8,0x7e10c)
    for cup in range(5):
        background_palette = bytearray(512)
        background_palette[0xa0:0xc0] = rom[0xae07c+cup*32:0xae09c+cup*32]
        background_palette[0xe0:0x120] = rom[0x7cd48:0x7cd88]
        pixels = []
        for phase in range(7):
            colors = 0x7cd88+cup*224+phase*32
            background_palette[0x120:0x140] = rom[colors:colors+32]
            _, _, row = tile_map(pattern_tiles,background_palette,decode_lz77(rom,cup_maps[cup]),32,32)
            pixels.extend(row)
        sections.append((15,COURSE_BACKGROUND+cup,
                         image_asset(256,256*7,pixels)))
        _, _, affine = affine_tile_map(affine_tiles,background_palette,affine_map,32)
        sections.append((15,COURSE_AFFINE+cup,image_asset(256,256,affine)))
        frame_palette=preview_panel_palette(rom,palette,cup)
        sections.append((15,COURSE_PANEL+cup,cell_image(rom,0xc8dac,tiles,frame_palette)))
        cell = pointer(0xd94fc+cup*20)
        sections.append((15,COURSE_CUP+cup,cell_image(rom,cell,tiles,palette)))
        extra_palette=bytearray(palette)
        extra_palette[0x100:0x1a0]=rom[0xbb5a0:0xbb640]
        sections.append((15,COURSE_CUP+5+cup,cell_image(rom,cell,tiles,extra_palette)))
        sections.append((15,COURSE_LOCKED_CUP+cup,
                         cell_image(rom,cell,tiles,locked_cup_palette(rom,palette,cup))))
        sections.append((15,COURSE_LOCKED_CUP+5+cup,
                         cell_image(rom,cell,tiles,locked_cup_palette(rom,extra_palette,cup))))
        data = decode_lz77(rom,pointer(0xd9628+cup*4))
        tiles[0x5800:0x5800+len(data)] = data
        sections.append((15,COURSE_CUP_NAME+cup,origin(cell_image(rom,0xc8d9c,tiles,palette))))
    ok_palette=bytearray(palette)
    colors=pointer(0xd8ea0)
    ok_palette[0x40:0x60]=rom[colors:colors+32]
    ok_tiles=bytearray(tiles)
    data=decode_lz77(rom,0xb83a4)
    ok_tiles[0x2800:0x2800+len(data)]=data
    sections.append((15,COURSE_OK,cell_image(rom,0xc8a58,ok_tiles,ok_palette)))
    text_palette = bytearray(512)
    text_palette[0x140:0x180] = rom[0xb23b0:0xb23f0]
    records_palette = bytearray(512)
    records_palette[0xa0:0xe0] = rom[0xb2310:0xb2350]
    # track_getMKSCTrack and gTrackDefTable supply every GBA course in cup order.
    # Covers contain a blank tile followed by ten columns by eight rows of
    # 8bpp tiles; names contain twelve columns by two rows of 4bpp tiles.
    for index in range(44):
        extra=index>=20
        arena=index>=40
        track = struct.unpack_from('<I',rom,(0xe7524+(index-40)*4) if arena else (0xe74c4+(index-20)*4) if extra else (0xe7464+index*4))[0]
        definition = pointer(0xe7fec+track*4)
        if extra:
            # 8938/8850: Extra covers are the course container's 8x8 tile map
            # thumbnails at C4 with the shared 66EAC palette, not null covers.
            source_id=struct.unpack_from('<I',rom,definition)[0]
            container=Data(rom[0x258000:0x308000])
            start=container.u32(source_id*4)
            c=Data(container.data[start:])
            cover=c.lz(c.u32(0xc4),0x800)
            colors=rom[0x66eac:0x66ecc]
            pixels=[tile_pixel(cover,colors,(y//8)*8+(x-8)//8,(x-8)%8,y%8)
                    if 8<=x<72 else 0 for y in range(64) for x in range(80)]
        else:
            cover = decode_lz77(rom,pointer(definition+0x24))
            colors = decode_lz77(rom,pointer(definition+0x28))
            if len(cover)!=81*64 or len(colors)>512:
                raise ValueError('invalid course selector cover')
            cover_palette = colors + bytes(512-len(colors))
            pixels = [tile_pixel(cover,cover_palette,1+(y//8)*10+x//8,x%8,y%8,depth=8)
                      for y in range(64) for x in range(80)]
        sections.append((15,COURSE_PREVIEW+index,image_asset(80,64,pixels)))
        name = decode_lz77(rom,pointer(definition+0x30))
        if len(name)!=24*32:
            raise ValueError('invalid course selector name')
        for base, bank in ((COURSE_NAME,10),(COURSE_SELECTED_NAME,11)):
            pixels = [tile_pixel(name,text_palette,(y//8)*12+x//8,x%8,y%8,bank)
                      for y in range(16) for x in range(96)]
            sections.append((15,base+index,image_asset(96,16,pixels)))
        if index < 40:
            for stripe, bank in enumerate((5, 6)):
                pixels = [tile_pixel(name,records_palette,(y//8)*12+x//8,x%8,y%8,bank)
                          for y in range(16) for x in range(96)]
                sections.append((15,RECORDS_NAME+stripe*64+index,image_asset(96,16,pixels)))
    return sections + records_sections(rom) + cup_record_sections(rom)


def records_sections(rom):
    # BG3 is the cup-0 mushroom pattern with records tiles; BG2 the panel.
    palette = bytearray(512)
    palette[0xa0:0xe0] = rom[0xb2310:0xb2350]
    palette[0xe0:0x120] = rom[0x7cd48:0x7cd88]
    palette[0x120:0x140] = rom[0x7cd88:0x7cda8]
    palette[0x1e0:0x200] = rom[0xaad10:0xaad30]
    pattern_tiles = bytearray(decode_lz77(rom,0x7d1e8))
    pattern_tiles[0x660:0x860] = rom[0x7d694:0x7d894]  # the records upload over tiles 0x33..0x42
    w, _, pattern = tile_map(pattern_tiles,palette,decode_lz77(rom,0x7dcb4),32,32)
    tiles = bytearray(0x8000)
    tiles[0x7d20:0x7d20+0xc0] = decode_lz77(rom,0xb3718)
    _, _, panel = tile_map(tiles,palette,decode_lz77(rom,0x79f18),32,32)
    # The pattern sits at the settled list's scroll (174, 338).
    sections = [(15,RECORDS_PANEL,image_asset(240,160,[
        panel[y*w+x] or pattern[(y+338)%256*w+(x+174)%256] for y in range(160) for x in range(240)]))]
    sections.extend(((15, 0x23804, image_asset(256,256,pattern)),
                     (15, 0x23805, image_asset(256,256,panel))))
    tiles[0x5800:0x6400] = rom[0xacafc:0xad6fc]
    tiles[0x6400:0x6c00] = rom[0xabefc:0xac6fc]

    def strip(columns):
        cells = [(tile, tile + (2 if tile < 0x2c4 else 0x20)) for tile in columns]
        return image_asset(len(columns)*8, 16, [
            tile_pixel(tiles,palette,cells[x//8][y//8],x%8,y%8,15)
            for y in range(16) for x in range(len(columns)*8)])
    sections.append((15,RECORDS_BEST,strip(range(0x32d,0x334))))
    sections.append((15,RECORDS_LAP,strip(range(0x334,0x33a))))
    sections.append((15,RECORDS_GLYPHS,strip(list(range(0x320,0x32c))+[0x2c0,0x2c1])))
    return sections + records_ghost_sections(rom)


def records_ghost_sections(rom):
    """CF3C/D5E0: source count glyphs and immutable presence schedules."""
    palette=decode_lz77(rom,0xb2068)
    counts=decode_lz77(rom,0xb3424)
    ghosts=decode_lz77(rom,0xb2c38)
    if len(palette)!=512 or len(counts)!=576 or len(ghosts)!=1024:
        raise ValueError('invalid Records ghost graphics')
    tiles=bytearray(32768)
    tiles[0x7400:0x74c0]=counts[:192]
    tiles[0x7c00:0x8000]=ghosts
    sections=[(15,0x23860,cell_image(rom,0xc96d4,tiles,palette))]
    for received,count_limit,first_id,anchor in ((False,11,0x23861,(-21,-4)),
                                                (True,3,0x2386c,(1,-4))):
        for count in range(count_limit):
            tile=(3 if received else 0) if count==0 else count+(15 if received else 5)
            pixels=[tile_pixel(counts,palette,tile,x,y,15) for y in range(8) for x in range(8)]
            sections.append((15,first_id+count,image_asset(8,8,pixels,anchor)))
    rules=bytearray(struct.pack('<4I',0x31484752,1,6,0))
    rules+=struct.pack('<15I',0x23860,*range(0x23861,0x2386c),*range(0x2386c,0x2386f))
    ids={}
    for table,count in ((0xd9684,2),(0xd9694,8),(0xd9724,2),(0xd9734,8),(0xd96d4,2),(0xd96e4,8)):
        rules+=struct.pack('<I',count)
        for row in range(count):
            pointer,duration=struct.unpack_from('<2I',rom,table+row*8)
            cell=pointer-0x08000000
            if not 0<=cell<len(rom) or duration>=65535:
                raise ValueError('invalid Records ghost frame')
            if cell not in ids:
                ids[cell]=0x23880+len(ids)
                sections.append((15,ids[cell],cell_image(rom,cell,tiles,palette)))
            rules+=struct.pack('<IHH',ids[cell],duration+1,0)
    sections.append((94,1,bytes(rules)))
    return sections


def cup_record_sections(rom):
    def pointer(address):
        value = struct.unpack_from('<I', rom, address)[0] - 0x08000000
        if not 0 <= value < len(rom):
            raise ValueError('course selector pointer outside ROM')
        return value

    palette = bytearray(512)
    palette[0x60:0x80] = rom[0x627d4:0x627f4]
    colors = decode_lz77(rom, 0xb07e4)
    palette[0x1a0:0x1a0+len(colors)] = colors
    tiles = bytearray(32768)
    label = decode_lz77(rom, 0xb0e28)
    tiles[0x4c00:0x4c00+len(label)] = label
    glyphs = decode_lz77(rom, 0xb0eb0)
    sections = []
    for rating in range(8):
        tiles[0x4800:0x4880] = glyphs[rating*128:rating*128+128]
        sections.append((15, COURSE_RANK+rating, cell_image(rom, 0xc8fe4, tiles, palette)))
    for medal in range(3):
        colors = pointer(0xd961c+medal*4)
        palette[0x1c0:0x1e0] = rom[colors:colors+32]
        for frame in range(16):
            data = decode_lz77(rom, pointer(pointer(0xdbe28+medal*4)+frame*4))
            tiles[0x5a00:0x5a00+len(data)] = data
            sections.append((15, COURSE_TROPHY+medal*16+frame, cell_image(rom, 0xc8f60, tiles, palette)))
    return sections

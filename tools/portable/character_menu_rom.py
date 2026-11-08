"""US character-selection portraits, labels and rotating kart previews."""
import struct
from ui_rom import cell_image, decode_lz77, image_asset

CHARACTER_HEADING = 0x22000
CHARACTER_CURSOR = 0x22010  # player 1's cursor, also the slot anchors
CHARACTER_PORTRAIT = 0x22100
CHARACTER_NAME = 0x22200
CHARACTER_PREVIEW = 0x22300
# sub_80065CC: speed/weight stars D93F4[source] at (60,115); Random has none.
CHARACTER_STATS = 0x22020
# sub_8006518: players 2..4 cursors D93A8 and every player's lock badge
# D93B8 at its D93C8 corner; the choosing player keeps the cursor.
CHARACTER_PLAYER_CURSOR = 0x26100  # +player*16+slot, players 1..3 (0x224xx is roster art)
CHARACTER_PLAYER_BADGE = 0x26200   # +player*16+slot


def animation_asset(frames, loop=True):
    if not frames or len(frames) > 4096 or any(t <= 0 for _, t in frames):
        raise ValueError('invalid native animation')
    duration = sum(t for _, t in frames)
    if duration > 0xffffffff:
        raise ValueError('native animation duration overflows')
    return (struct.pack('<8sHHHHIIII', b'MKSCANI1', 32, 1, len(frames), int(loop),
                        duration, 32, 32+len(frames)*8, 0) +
            b''.join(struct.pack('<II', image, ticks) for image, ticks in frames))


def placed(image, x, y):
    data = bytearray(image)
    ax, ay = struct.unpack_from('<hh', data, 16)
    struct.pack_into('<hh', data, 16, ax+x, ay+y)
    return bytes(data)


def flipped(image):
    w, h = struct.unpack_from('<HH', image, 12)
    ax, ay = struct.unpack_from('<hh', image, 16)
    pixels = struct.unpack_from(f'<{w*h}I', image, 32)
    return image_asset(w, h, [pixels[y*w+w-1-x] for y in range(h) for x in range(w)], (ax, ay))


def character_sections(rom):
    def pointer(address):
        value = struct.unpack_from('<I', rom, address)[0] - 0x08000000
        if not 0 <= value < len(rom):
            raise ValueError('character menu pointer outside ROM')
        return value

    tiles = bytearray(32768)
    palette = bytearray(512)
    palette[:64] = rom[0xaf2c0:0xaf300]
    palette[0x60:0x80] = rom[0xafe40:0xafe60]
    for address, offset in ((0xaf320, 0), (0xc2fa8, 0x1000),
                            (0xafe60, 0x5800), (0xb042c, 0x6c00), (0xb05bc, 0x7000)):
        data = decode_lz77(rom, address)
        tiles[offset:offset+len(data)] = data
    sections = [(15, CHARACTER_HEADING, placed(cell_image(rom, 0xc8828, tiles, palette),128,16))]
    next_image = 0x30000
    images = {}

    def add_image(payload):
        nonlocal next_image
        if payload not in images:
            images[payload] = next_image
            sections.append((15, next_image, payload))
            next_image += 1
        return images[payload]

    # Source menu slots are a three-by-three grid with Random at the centre.
    for slot in range(9):
        cell, _, _, x, y, source, _ = struct.unpack_from('<3I4h', rom, 0xd9130+slot*20)
        sections.append((15, CHARACTER_CURSOR+slot, placed(cell_image(rom,pointer(0xd93a8),tiles,palette),x,y)))
        data = decode_lz77(rom, pointer(0xd93d0+source*4))
        tiles[0x3c00:0x3c00+len(data)] = data
        sections.append((15, CHARACTER_NAME+slot, placed(cell_image(rom,0xc888c,tiles,palette),72,48)))
        stats = struct.unpack_from('<I', rom, 0xd93f4+source*4)[0]
        if stats:
            sections.append((15, CHARACTER_STATS+slot,
                             placed(cell_image(rom,stats-0x08000000,tiles,palette),60,115)))
        for state in range(3):
            frames = []
            if source == 8:
                # sub_8006A28's eight raw question-mark frames, four ticks each.
                for phase in range(8):
                    address = pointer(0xd945c+phase*4)
                    tiles[0x6200:0x6400] = rom[address:address+512]
                    frames.append((add_image(placed(cell_image(rom,cell-0x08000000,tiles,palette),x,y)),4))
            else:
                count = struct.unpack_from('<I',rom,0xd9c68+(source*3+state)*4)[0]
                table = pointer(0xd9cc8+(source*3+state)*4)
                colors = pointer(0xd9d28+source*8)
                palette[(source+7)*32:(source+8)*32] = rom[colors:colors+32]
                for phase in range(count):
                    ticks, address = struct.unpack_from('<2I',rom,table+phase*8)
                    data = decode_lz77(rom,address-0x08000000)
                    offset = 0x4000+source*512
                    tiles[offset:offset+len(data)] = data
                    frames.append((add_image(placed(cell_image(rom,cell-0x08000000,tiles,palette),x,y)),ticks+2))
            sections.append((15,CHARACTER_PORTRAIT+slot*3+state,animation_asset(frames,state!=2)))
        # renderCharacterSprites uses the source view-angle table, reflected
        # across the second half of a turn. No source sprite state is retained.
        preview = []
        graphics_id = struct.unpack_from('<I',rom,0xd9790+source*4)[0] if source<8 else 0
        for angle in range(128):
            view = angle if angle<64 else 127-angle
            frame = rom[0xd9344+view]
            offset = 0x345800+graphics_id*0x16000+frame*0x800
            tiles[0x5000:0x5800] = rom[offset:offset+0x800]
            palette[0x1e0:0x200] = rom[0x344600+graphics_id*512:0x344620+graphics_id*512] if source<8 else bytes(32)
            image = cell_image(rom,0xc8944,tiles,palette)
            if angle>=64:
                image = flipped(image)
            preview.append((add_image(placed(image,72,83)),1))
        sections.append((15,CHARACTER_PREVIEW+slot,animation_asset(preview)))
    tiles = bytearray(32768)
    for address, offset in ((0xaf320, 0), (0xc2fa8, 0x1000),
                            (0xafe60, 0x5800), (0xb042c, 0x6c00), (0xb05bc, 0x7000)):
        data = decode_lz77(rom, address)
        tiles[offset:offset+len(data)] = data
    for player in range(4):
        dx, dy = struct.unpack_from('<2b', rom, 0xd93c8+player*2)
        cursor = cell_image(rom, pointer(0xd93a8+player*4), tiles, palette)
        badge = cell_image(rom, pointer(0xd93b8+player*4), tiles, palette)
        for slot in range(9):
            x, y = struct.unpack_from('<2h', rom, 0xd9130+slot*20+12)
            if player:
                sections.append((15, CHARACTER_PLAYER_CURSOR+player*16+slot, placed(cursor, x, y)))
            sections.append((15, CHARACTER_PLAYER_BADGE+player*16+slot, placed(badge, x+dx, y+dy)))
    return sections

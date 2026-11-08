"""Normalize race palette scripts and callback data into bounded native lanes."""
import struct

MAGIC = 0x31424D41
MAX_LANES, MAX_FRAMES, MAX_PATCHES, MAX_DATA = 16, 256, 768, 262144
ROAD, SKY, OBJECT, SKY_TILES, FRONT_MAP, MID_MAP, BACK_MAP = range(7)
PRE, POST = range(2)
ENTRY, EVERY = range(2)
TIMER, SUNSET, DIRECTION_TILES, FINISH_BACKDROP, RAINBOW, STORM = range(6)
AUTO, MANUAL = range(2)

# 31BBE registrations, followed by the common F1B38 actor. The original pool
# inserts at its head; execution order is the reverse of registration order.
REGISTRATIONS = {
    2: (0xF128C, 0xF136C), 5: (0xF128C, 0xF136C),
    3: (0xF212C,), 35: (0xF212C,), 9: (0xF171C, 0xF176C),
    10: (0xF160C,), 13: (0xF144C, 0xF152C),
    16: (0xF17D4, 0xF188C, 0xF1984, 0xF1A34), 17: (0xF256C,),
    19: (0xF27B4, 0xF28A0), 21: (0xF256C, 0xF2664, 0xF26FC),
    23: (0xF21D0, 0xF2230, 0xF2298), 33: (0xF20B4,),
    42: (0xF2308, 0xF23E8), 44: (0xF24EC,),
    49: (0xF2664, 0xF26FC), 51: (0xF27B4,)}

# callback -> (pointer table or raw table, source, destination BG color, colors).
# Raw rows have 32-byte stride. Object callbacks are handled explicitly below.
CALLBACKS = {
    0x5A0A0: (1, 0xF1704, 26, 2), 0x5A0D0: (0, 0x71304, 16, 6),
    0x5A0FC: (0, 0x71310, 22, 4), 0x5A128: (0, 0x714BC, 32, 8),
    0x5A154: (0, 0x7159C, 0, 10), 0x5A188: (0, 0x716DC, 25, 5),
    0x5A1B4: (1, 0xF1A1C, 30, 2), 0x5A1E4: (1, 0xF1B2C, 60, 2),
    0x5A214: (1, 0xF1B88, 113, 5), 0x5A244: (1, 0xF1C1C, 97, 15),
    0x5A36C: (1, 0xF20A4, 12, 3), 0x5A39C: (1, 0xF211C, 37, 9),
    0x5A3CC: (1, 0xF21C4, 125, 3), 0x5A3FC: (1, 0xF2220, 22, 4),
    0x5A474: (1, 0xF22E8, 24, 5), 0x5A4A4: (1, 0xF23D0, 10, 4),
    0x5A4D4: (1, 0xF24E0, 10, 5), 0x5A504: (1, 0xF2554, 29, 3),
    0x5A534: (0, 0x718C4, 113, 4), 0x5A560: (0, 0x718CE, 124, 4),
    0x5A58C: (1, 0xF2794, 113, 5), 0x5A5BC: (1, 0xF287C, 147, 11)}


def callback_patches(r, callback, index):
    callback = (callback & ~1) - 0x08000000
    if callback in (0x5A020, 0x5A060):
        base = (0x70F2E, 0x7110E) if callback == 0x5A020 else (0x7104C, 0x7122C)
        address = base[index >= 10] + (index if index < 10 else index-10) * 32
        return [(ROAD, 37 if callback == 0x5A020 else 32,
                 r.span(address, 22 if callback == 0x5A020 else 10))]
    if callback == 0x5A320:
        return [(SKY, destination, r.span(r.pointer(table+index*4), 32))
                for table, destination in ((0xF1F44, 16), (0xF1F60, 0))]
    if callback == 0x5A42C:
        address = r.pointer(0xF2220+index*4)
        return [(ROAD, destination, r.span(address+offset, 6))
                for offset, destination in ((10, 16), (18, 19))]
    if callback in (0x5A274, 0x5A2A4):
        table, destination = (0xF1CB0, 40) if callback == 0x5A274 else (0xF1E08, 8)
        return [(OBJECT, destination, r.span(r.pointer(table+index*4), 12))]
    if callback not in CALLBACKS:
        raise ValueError(f'unsupported ambient palette callback {callback:x}')
    indirect, table, destination, count = CALLBACKS[callback]
    address = r.pointer(table+index*4) if indirect else table+index*32
    colors = r.span(address, count*2)
    if callback == 0x5A154:
        colors = bytes(2)+colors[2:]
    return [(SKY if destination >= 96 else ROAD,
             destination-96 if destination >= 96 else destination, colors)]


class Builder:
    def __init__(self, object_seed):
        if len(object_seed) != 256:
            raise ValueError('ambient object seed must contain 128 colors')
        self.data = bytearray(object_seed)
        self.lanes, self.frames, self.patches, self.initial = [], [], [], []

    def patch(self, target, destination, data, initial=False):
        palette = target <= OBJECT
        if palette and len(data) % 2:
            raise ValueError('unaligned ambient palette patch')
        count = len(data)//2 if palette else len(data)
        offset = len(self.data)
        self.data.extend(data)
        record = (target, 0, destination, count, 0, offset)
        (self.initial if initial else self.patches).append(record)

    def lane(self, events, *, stage=POST, policy=ENTRY, controller=TIMER,
             trigger=AUTO, loop=0, initial_frame=0, initial_counter=0, params=(0,0,0,0)):
        first = len(self.frames)
        for duration, patches in events:
            patch_first = len(self.patches)
            for target, destination, data in patches:
                self.patch(target, destination, data)
            self.frames.append((duration, patch_first, len(patches), 0, 0))
        self.lanes.append((stage, policy, controller, trigger, first, len(events),
                           loop, initial_frame, initial_counter, *params))
        return len(self.lanes)-1

    def script(self, r, address, trigger=AUTO):
        pc, seen, events, pending, index = address, {}, [], [], 0
        for _ in range(256):
            if pc in seen:
                return self.lane(events, trigger=trigger, loop=seen[pc])
            seen[pc] = len(events)
            command, argument = struct.unpack('<II', r.span(pc, 8))
            if command == 0:
                if pending:
                    raise ValueError('ambient script terminates without a wait')
                return self.lane(events, trigger=trigger, loop=0xFFFF)
            if command == 0x41:
                index = argument
            elif command == 0xF4:
                pending += callback_patches(r, argument, index)
            elif command == 0xF1:
                if not 1 <= argument <= 65535:
                    raise ValueError('invalid ambient script duration')
                events.append((argument, pending)); pending = []
            elif command == 0xF2:
                pc += struct.unpack('<i', struct.pack('<I', argument))[0] * 8
                continue
            else:
                raise ValueError(f'unsupported ambient script command {command:x}')
            pc += 8
        raise ValueError('ambient script exceeds command budget')

    def build(self):
        result = struct.pack('<8I', MAGIC, 1, len(self.lanes), len(self.frames),
                             len(self.patches), len(self.initial), len(self.data), 0)
        result += b''.join(struct.pack('<4B4HI4I', *lane) for lane in self.lanes)
        result += b''.join(struct.pack('<HHBBH', *frame) for frame in self.frames)
        result += b''.join(struct.pack('<BBHHHI', *patch) for patch in self.patches+self.initial)
        result += self.data
        validate(result)
        return result


def validate(data):
    if len(data) < 32:
        raise ValueError('truncated ambient header')
    magic, version, lanes, frames, patches, initial, size, seed = struct.unpack_from('<8I', data)
    if (magic != MAGIC or version != 1 or lanes > MAX_LANES or frames > MAX_FRAMES or
            patches > MAX_PATCHES or initial > 32 or size > MAX_DATA or seed+256 > size or
            len(data) != 32+lanes*32+frames*8+(patches+initial)*12+size):
        raise ValueError('invalid ambient header')
    for i in range(lanes):
        stage, policy, controller, trigger, first, count, loop, current, counter, *params = struct.unpack_from('<4B4HI4I', data, 32+i*32)
        if (stage > 1 or policy > 1 or controller > 5 or trigger > 1 or count == 0 or
                first+count > frames or current >= count or (loop != 65535 and loop >= count)):
            raise ValueError('invalid ambient lane')
        if ((controller == TIMER and params[0] > 1) or
                (controller == SUNSET and count < 64) or
                (controller == DIRECTION_TILES and count < 18) or
                (controller == FINISH_BACKDROP and count < 41) or
                (controller == RAINBOW and count < 18)):
            raise ValueError('invalid ambient controller frames')
        if controller == STORM:
            for lane in params:
                if lane >= lanes or data[32+lane*32] != POST:
                    raise ValueError('invalid ambient storm lane')
    base = 32+lanes*32
    for i in range(frames):
        duration, first, count, reserved, padding = struct.unpack_from('<HHBBH', data, base+i*8)
        if duration == 0 or count > 3 or first+count > patches or reserved or padding:
            raise ValueError('invalid ambient frame')
    base += frames*8
    bounds = (64,64,128,0x4000,0x1000,0x1000,0x1000)
    for i in range(patches+initial):
        target, reserved, destination, count, padding, offset = struct.unpack_from('<BBHHHI', data, base+i*12)
        if (target > 6 or reserved or padding or count == 0 or destination+count > bounds[target] or
                offset+count*(2 if target <= OBJECT else 1) > size):
            raise ValueError('invalid ambient patch')
    return True


def table_events(r, table, count, duration, target, destination, colors):
    return [(duration, [(target, destination, r.span(r.pointer(table+i*4), colors*2))])
            for i in range(count)]


def background_lanes(builder, r, behavior):
    # These callbacks copy the current palette on every invocation, unlike F4
    # script actors, and run before the shared driver stack.
    if behavior in (3, 7):
        for table, destination, count in ((0xE8130, 23, 9), (0xE8160, 0, 16)):
            builder.lane(table_events(r, table, 12, 16, SKY, destination, count),
                         stage=PRE, policy=EVERY, params=(1,0,0,0))
    if behavior == 11:
        builder.lane(table_events(r, 0xE8190, 12, 16, SKY, 6, 5),
                     stage=PRE, policy=EVERY, params=(1,0,0,0))
        events = [(5, [(SKY, 40, r.span(r.pointer(0xE81C0+i*4), 6)),
                       (SKY, 46, r.span(r.pointer(0xE81C8+i*4), 4))]) for i in range(2)]
        builder.lane(events, stage=PRE, policy=EVERY, params=(1,0,0,0))
    if behavior == 8:
        events = []
        for direction in range(3):
            table = r.pointer(0xE8124+direction*4)
            for frame in range(6):
                events.append((2, [(SKY_TILES, 0x1CA0, r.span(r.pointer(table+frame*4), 0x680))]))
        builder.lane(events, stage=PRE, controller=DIRECTION_TILES)
        builder.lane(table_events(r, 0xE80CC, 4, 2, ROAD, 42, 4),
                     stage=PRE, policy=EVERY, params=(1,0,0,0))
    if behavior == 6:
        events = []
        for frame in range(64):
            events.append((4, [(SKY, 0, r.span(0xE8374+frame*96, 96)),
                               (ROAD, 0, r.span(0xE9B74+frame*96, 64)),
                               (ROAD, 48, r.span(0xE9B74+frame*96+64, 32))]))
        builder.lane(events, stage=PRE, controller=SUNSET)
    if behavior == 17:
        events = [(16, [(ROAD, 0, r.span(0xE8322+i*2, 2))]) for i in range(33)]
        events += [(64, [(ROAD, 0, r.span(0xE8362+i*2, 2))]) for i in range(8)]
        builder.lane(events, stage=PRE, controller=FINISH_BACKDROP)
        builder.lane(table_events(r, 0xE82F0, 8, 1, ROAD, 16, 13),
                     stage=PRE, policy=EVERY, params=(1,0,0,0))
        events = [(4, [(OBJECT, 19, r.span(source, 22))])
                  for source in (0x76B84, 0x76B9C, 0x76BB4)]
        builder.lane(events, stage=PRE, initial_frame=2, initial_counter=999, params=(1,0,0,0))
    if behavior == 10:
        builder.lane(table_events(r, 0xE8260, 8, 6, ROAD, 33, 8),
                     stage=POST, policy=EVERY, params=(1,0,0,0))
    if behavior == 23:
        events = []
        for frame in range(18):
            patches = []
            for bank in range(2):
                first = struct.unpack('<13H', r.span(0x713BC+bank*32, 26))
                second = struct.unpack('<13H', r.span(0x713FC+bank*32, 26))
                colors = []
                cosine = struct.unpack('<i', r.span(0xE645C+((256-frame*32)&1023)*4, 4))[0]
                weight = (32768-cosine)//2
                for a,b in zip(first,second):
                    value = 0
                    for shift in (0,5,10):
                        x,y=(a>>shift)&31,(b>>shift)&31
                        component = x+((y-x)*weight>>15) if frame<16 else (x+y)//2 if frame==16 else y
                        value |= (component&31)<<shift
                    colors.append(value)
                patches.append((SKY, 16+bank*16, struct.pack('<13H', *colors)))
            events.append((1,patches))
        builder.lane(events, stage=PRE, controller=RAINBOW)


def payload(r, behavior, palette_animation, material, object_palette):
    builder = Builder(object_palette + r.span(0x66E4C, 160))
    source = {2:0x66EEC, 3:0x66EF4, 11:0x66EF4, 35:0x66EF4,
              6:0x66EFC, 13:0x66F04}.get(palette_animation)
    if source is not None:
        builder.patch(OBJECT, 103, r.span(source, 8), initial=True)
    if palette_animation == 14:
        builder.patch(OBJECT, 91, bytes(builder.data[123*2:126*2]), initial=True)
    builder.script(r, 0xF1B38)
    for address in reversed(REGISTRATIONS.get(palette_animation, ())):
        builder.script(r, address)
    background_lanes(builder, r, behavior)
    if behavior == 13:
        first = len(builder.lanes)+1
        builder.lane([(1, [])], controller=STORM, params=tuple(range(first, first+4)))
        for address in (0xF1B9C, 0xF1C30, 0xF1CE0, 0xF1E34):
            builder.script(r, address, MANUAL)
    return builder.build()

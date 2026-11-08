"""ROM decoders shared by the incremental course pack importers."""

from audit_course_catalog import course_offsets, read_stream, u16, u32

ROM_SHA1 = "9d327c030c3e2d9007990518594f70c3340ac56f"

def resources(rom):
    track = u32(rom, 0xe7fec + 5 * 4) - 0x08000000
    table = u32(rom, track + 20) - 0x08000000
    entries = []
    at = table
    while rom[at] != 255:
        if len(entries) >= 256:
            raise ValueError('unterminated turn table')
        entry = tuple(rom[at:at + 4])
        if entry[2] & 127 >= 8 or entry[3] & 15 > 2:
            raise ValueError('invalid ROM turn entry')
        entries.append(entry)
        at += 4
    maps = [rom[x + 4:x + 28] for x in (0xeef30, 0xeef4c, 0xeef68)]
    if any((u16(rom, x), u16(rom, x + 2)) != (4, 3)
           for x in (0xeef30, 0xeef4c, 0xeef68)):
        raise ValueError('turn map dimensions differ')
    glyphs = [rom[u32(rom, 0xee724 + i * 4) - 0x8000000:
                  u32(rom, 0xee724 + i * 4) - 0x8000000 + 384]
              for i in range(8)]
    return table, entries, maps, glyphs

def placement_records(rom: bytes, course: bytes) -> list[tuple]:
    records = []
    for index, (raw, x, y, checkpoint) in enumerate(read_stream(course, 0x8c)):
        offset = 0xf1060 + raw * 4 if raw <= 127 else 0xf1010 + raw * 4 - 0x200
        code = u32(rom, offset)
        if code & 0xffff0000 in (0x800000, 0x808000):
            records.append((index, raw, x, y, checkpoint, code))
    if len(records) != 7 or any(row[-1] not in (0x800000, 0x800001) for row in records):
        raise ValueError('unqualified Beach type1003 inventory/mode')
    return records

def records(rom):
    blob = rom[0x258000:]
    offsets = course_offsets(blob)
    course = blob[offsets[25]:offsets[26]]
    result = []
    for index, (raw, x, y, checkpoint) in enumerate(read_stream(course, 0x8c)):
        table = 0xf1060+raw*4 if raw <= 127 else 0xf1010+raw*4-0x200
        mode = u32(rom, table)
        if mode & 0xffff0000 == 0x840000:
            result.append((index, x, y, checkpoint, mode))
    if len(result) != 3 or any(r[-1] not in (0x840000, 0x840001, 0x840003)
                                for r in result):
        raise ValueError('unqualified original type1013 placements')
    return result

def rom_grid(rom: bytes, header_index: int, slots: int = 8) -> tuple[int, list[tuple[int, int, int]]]:
    if slots not in (4, 8):
        raise ValueError('source grid supports four or eight slots')
    pointer = u32(rom, 0x0E7FEC + header_index * 4)
    definition = pointer - 0x08000000
    if definition < 0 or definition + 8 > len(rom):
        raise ValueError("track definition pointer outside ROM")
    course_id = u32(rom, definition)
    if course_id >= 49:
        raise ValueError("course outside directory")
    directory = 0x258000 + course_id * 4
    if directory + 8 > len(rom):
        raise ValueError("course directory outside ROM")
    start = 0x258000 + u32(rom, directory)
    # The last entry has no following directory word; its end is the container end.
    end = 0x258000 + (u32(rom, directory + 4) if course_id + 1 < 49 else 0xB0000)
    if start < 0x258000 or end > len(rom) or start + 0x9C > end:
        raise ValueError("course block outside ROM")
    cursor = start + u32(rom, start + 0x98)
    rows: list[tuple[int, int, int] | None] = [None] * slots
    headings = rom[start + 0xCA] != 0
    if headings and slots != 4:
        raise ValueError('source directional grid has four player starts')
    while cursor + 4 <= end:
        opcode, x, y, _parameter = rom[cursor:cursor + 4]
        cursor += 4
        if opcode == 0:
            break
        # sub_8031ED8 maps boundary opcodes 0x81..0x88 to grid rows 0..7.
        if 0x81 <= opcode < 0x81 + slots:
            rank = opcode - 0x81
            if rows[rank] is not None:
                raise ValueError(f"duplicate grid rank {rank}")
            # 31ED8 omits the X center for Extra headers (32BC8 bit5).
            # Directional Battle starts use the four E745C headings.
            heading = u16(rom, 0xE745C + rank * 2) if headings else 0
            rows[rank] = ((x * 8 + (0 if header_index & 32 else 4)) << 16,
                          (y * 8 + 4) << 16, heading)
    if any(row is None for row in rows):
        raise ValueError("course grid is incomplete")
    return course_id, [row for row in rows if row is not None]

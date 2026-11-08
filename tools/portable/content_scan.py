"""ROM-content checks shared by repository and release validation.

This is a conservative fragment detector, not a copyright or licence verdict.
The owner ROM stays in memory; no fingerprints or extracted bytes are written.
"""
import re
import struct

# Lua's independently authored ceil(log2) lookup also occurs in the ROM's SDK.
# Recognize the mathematical sequence itself, not a path or a particular ROM
# address, so this exception cannot hide a different payload in the same file.
COMMON_MATH = tuple(struct.pack('<' + code * 256, *(i.bit_length() for i in range(256)))
                    for code in ('B', 'H', 'I'))
COMMON_SEQUENCES = COMMON_MATH + tuple(struct.pack('<' + code * 256, *range(256))
                                     for code in ('B', 'H', 'I'))


def common_sequence(fragment):
    # Extending an anchor can absorb incidental padding around a common lookup.
    # Fewer than32 adjoining bytes cannot establish a distinctive asset fragment.
    for trim in range(min(32, len(fragment) - 47)):
        for left in range(trim + 1):
            candidate=fragment[left:len(fragment)-(trim-left)]
            if any(candidate in sequence for sequence in COMMON_SEQUENCES):
                return True
    return False


def pe_export_ordinal_ranges(data):
    """Recognize linker-generated identity ordinals, only inside a valid PE export table."""
    if len(data) < 64 or data[:2] != b'MZ':
        return ()
    try:
        pe = struct.unpack_from('<I', data, 60)[0]
        if pe < 64 or pe + 24 > len(data) or data[pe:pe + 4] != b'PE\0\0':
            return ()
        section_count = struct.unpack_from('<H', data, pe + 6)[0]
        optional_size = struct.unpack_from('<H', data, pe + 20)[0]
        optional = pe + 24
        if not 1 <= section_count <= 96 or optional + optional_size + section_count * 40 > len(data):
            return ()
        magic = struct.unpack_from('<H', data, optional)[0]
        directory = {0x10b: 96, 0x20b: 112}.get(magic)
        if directory is None or optional_size < directory + 8:
            return ()
        if struct.unpack_from('<I', data, optional + directory - 4)[0] < 1:
            return ()
        export_rva, export_size = struct.unpack_from('<II', data, optional + directory)
        sections = []
        for index in range(section_count):
            header = optional + optional_size + index * 40
            rva, size, raw = struct.unpack_from('<III', data, header + 12)
            if raw + size > len(data) or rva + size > 0x100000000:
                return ()
            sections.append((rva, size, raw))

        def mapped(rva, size):
            matches = [raw + rva - base for base, length, raw in sections
                       if size > 0 and base <= rva and rva + size <= base + length]
            if len(matches) != 1:
                raise ValueError('unmapped or ambiguous PE field')
            return matches[0]

        if export_size < 40:
            return ()
        export = mapped(export_rva, export_size)
        _, _, _, _, dll_name, _, functions, names, function_rva, name_rva, ordinal_rva = \
            struct.unpack_from('<IIHH7I', data, export)
        if not 1 <= names <= functions <= 65536:
            return ()

        def export_field(rva, size):
            if rva < export_rva or rva + size > export_rva + export_size:
                raise ValueError('field outside PE export directory')
            return mapped(rva, size)

        function_table = export_field(function_rva, functions * 4)
        name_table = export_field(name_rva, names * 4)
        ordinal_table = export_field(ordinal_rva, names * 2)
        table_ranges = [(function_table, function_table + functions * 4),
                        (name_table, name_table + names * 4),
                        (ordinal_table, ordinal_table + names * 2)]
        if any(a < export + 40 for a, _ in table_ranges) or any(
                a < d and c < b for i, (a, b) in enumerate(table_ranges)
                for c, d in table_ranges[i + 1:]):
            return ()

        def export_string(rva):
            start = export_field(rva, 1)
            end = data.find(b'\0', start, min(export + export_size, start + 4096))
            if end <= start or any(value < 33 or value > 126 for value in data[start:end]):
                raise ValueError('invalid export name')

        export_string(dll_name)
        for index in range(functions):
            function = struct.unpack_from('<I', data, function_table + index * 4)[0]
            if not function and index < names:
                return ()
            if function:
                mapped(function, 1)
        for index in range(names):
            export_string(struct.unpack_from('<I', data, name_table + index * 4)[0])
            if struct.unpack_from('<H', data, ordinal_table + index * 2)[0] != index:
                return ()
        return ((ordinal_table, ordinal_table + names * 2),)
    except (struct.error, ValueError):
        return ()


class RomContentIndex:
    """Find contiguous fragments, including low-colour palettes and rule tables.

    An anchor every16 bytes guarantees detection of every distinctive fragment
    of at least47 bytes. Extend anchors before reporting to reject short common
    sequences. Low-entropy padding is deliberately excluded.
    """
    width = 32
    stride = 16

    def __init__(self, rom):
        self.rom = rom
        self.anchors = {}
        for offset in range(0, len(rom) - self.width + 1, self.stride):
            key = rom[offset:offset + self.width]
            if len(set(key)) >= 4:
                previous = self.anchors.get(key)
                if previous is None:
                    self.anchors[key] = offset
                elif isinstance(previous, int):
                    self.anchors[key] = [previous, offset]
                else:
                    previous.append(offset)

    def find(self, data, minimum=48):
        ordinal_ranges = pe_export_ordinal_ranges(data)
        for offset in range(len(data) - self.width + 1):
            sources = self.anchors.get(data[offset:offset + self.width])
            if sources is None:
                continue
            for source in [sources] if isinstance(sources, int) else sources:
                start, origin = offset, source
                while start and origin and data[start - 1] == self.rom[origin - 1]:
                    start -= 1
                    origin -= 1
                end, limit = offset + self.width, source + self.width
                while end < len(data) and limit < len(self.rom) and data[end] == self.rom[limit]:
                    end += 1
                    limit += 1
                if end - start >= minimum:
                    if any(low <= start and end <= high for low, high in ordinal_ranges):
                        continue
                    if not common_sequence(data[start:end]):
                        return start, origin, end - start
        return None


# Decode literal runs rather than matching source formatting. Constructors such
# as INT16_C are unwrapped, comments/addresses are not treated as asset bytes.
NUMBER = r'-?(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*'
RUN = re.compile(rf'{NUMBER}(?:\s*,\s*{NUMBER}){{7,}}')


def literal_payloads(data):
    text = data.decode('utf-8', errors='ignore')
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    text = re.sub(r'\b(?:U?INT(?:8|16|32|64)_C)\(([^()]*)\)', r'\1', text)
    # Flatten nested initializer rows, but never join unrelated declarations.
    text = re.sub(r'}\s*,\s*{', ',', text)
    for match in RUN.finditer(text):
        values = [int(re.sub(r'[uUlL]+$', '', token), 0) if 'x' in token.lower()
                  else int(re.sub(r'[uUlL]+$', '', token), 10)
                  for token in re.findall(NUMBER, match.group())]
        for width, code in ((1, 'B'), (2, 'H'), (4, 'I')):
            if all(-(1 << (width * 8 - 1)) <= value < (1 << (width * 8)) for value in values):
                yield match.start(), struct.pack('<' + code * len(values),
                    *(value & ((1 << (width * 8)) - 1) for value in values))


def content_matches(name, data, index):
    if index is None:
        return []
    match = index.find(data)
    if match:
        offset, source, size = match
        return [f'{name}: ROM fragment at file +0x{offset:x}, ROM +0x{source:x} ({size} bytes)']
    if name.endswith(('.c', '.h', '.s', '.py', '.json')):
        for position, payload in literal_payloads(data):
            match = index.find(payload)
            if match:
                _, source, size = match
                return [f'{name}: encoded ROM table near character {position}, ROM +0x{source:x} ({size} bytes)']
    return []

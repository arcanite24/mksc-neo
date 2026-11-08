#!/usr/bin/env python3
"""Audit source course metadata for the native pack importer.

This is an offline, read-only provenance tool. It never writes proprietary
course/ROM bytes; its JSON output contains only structural metadata and hashes.
"""

import argparse
import hashlib
import json
import pathlib
import struct

TRACK_DEF_TABLE_OFFSET = 0x0E7FEC
ROM_BASE = 0x08000000
TRACK_HEADER_FIRST = 4
TRACK_HEADER_LAST = 23
COURSE_DIRECTORY_ENTRIES = 49
STREAM_FIELDS = {"objects": 0x8C, "secondary_objects": 0x94,
                 "boundary": 0x98}
REQUIRED_OFFSET_FIELDS = (0x80, 0x84, 0x88, 0x8C, 0x90, 0x94, 0x98,
                          0xC4, 0xCC, 0xD4)


def u16(data, offset):
    if offset < 0 or offset + 2 > len(data):
        raise ValueError(f"u16 out of bounds at 0x{offset:x}")
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"u32 out of bounds at 0x{offset:x}")
    return struct.unpack_from("<I", data, offset)[0]


def read_stream(blob, field_offset):
    start = u32(blob, field_offset)
    if start == 0:
        return []
    if start >= len(blob):
        raise ValueError(f"stream 0x{field_offset:x} offset out of bounds")
    records = []
    cursor = start
    while cursor + 4 <= len(blob):
        record = tuple(blob[cursor:cursor + 4])
        if record[0] == 0:
            return records
        records.append(record)
        cursor += 4
    raise ValueError(f"stream 0x{field_offset:x} has no terminator")


def validate_lz77(data, offset, expected_size):
    if offset < 0 or offset + 4 > len(data) or data[offset] != 0x10:
        raise ValueError(f"invalid LZ77 header at 0x{offset:x}")
    decoded = data[offset + 1] | data[offset + 2] << 8 | data[offset + 3] << 16
    if decoded != expected_size:
        raise ValueError(f"LZ77 size {decoded} != {expected_size}")


def validate_sky(sky, background_id):
    header = u32(sky, background_id * 4)
    if header + 24 > len(sky):
        raise ValueError("sky header out of bounds")
    if sky[header] not in (2, 0x40) or sky[header + 1] & 3 != 3:
        raise ValueError("unsupported sky descriptor")
    gfx = u32(sky, header + 4)
    front = u32(sky, header + 8)
    middle = u32(sky, header + 12)
    back = u32(sky, header + 16)
    palette = u32(sky, header + 20)
    if header + gfx + 10 > len(sky) or header + palette + 126 > len(sky):
        raise ValueError("sky payload out of bounds")
    for index in range(5):
        relative = u16(sky, header + gfx + index * 2)
        validate_lz77(sky, header + gfx + relative, 0x800)
    validate_lz77(sky, header + front, 0x800)
    validate_lz77(sky, header + middle,
                  0x800 if sky[header] == 2 else 0x1000)
    validate_lz77(sky, header + back, 0x800)
    return header


def course_offsets(course):
    minimum = COURSE_DIRECTORY_ENTRIES * 4
    if len(course) < minimum:
        raise ValueError("course directory truncated")
    offsets = [u32(course, index * 4) for index in range(COURSE_DIRECTORY_ENTRIES)]
    previous = minimum
    for offset in offsets:
        if offset < previous or offset > len(course):
            raise ValueError("course directory is not monotonic and bounded")
        previous = offset
    return offsets


def inspect_course(blob, course_id, header_index, background_id):
    blockers = []
    try:
        if len(blob) < 0xD8:
            raise ValueError("course blob header truncated")
        for field in REQUIRED_OFFSET_FIELDS:
            value = u32(blob, field)
            if value and value >= len(blob):
                blockers.append(f"field_0x{field:x}_out_of_bounds")
        track_base = u32(blob, 0xCC)
        if track_base + 5 > len(blob):
            raise ValueError("track block out of bounds")
        checkpoint_count = blob[track_base]
        checkpoint_table = u16(blob, track_base + 3)
        checkpoint_start = track_base + checkpoint_table
        checkpoint_end = checkpoint_start + checkpoint_count * 8
        if checkpoint_count < 2 or checkpoint_end > len(blob):
            raise ValueError("checkpoint table out of bounds")
        points = [(u16(blob, checkpoint_start + i * 8),
                   u16(blob, checkpoint_start + i * 8 + 2))
                  for i in range(checkpoint_count)]
        streams = {name: read_stream(blob, field)
                   for name, field in STREAM_FIELDS.items()}
        starts = [record for record in streams["boundary"] if record[0] == 0x8B]
        if len(starts) != 1:
            blockers.append(f"start_finish_command_count_{len(starts)}")
            start = finish_y = None
        else:
            command = starts[0]
            start = {"x": command[1] * 8 + 40,
                     "y": command[2] * 8 + 28,
                     "route_index": checkpoint_count - 1}
            finish_y = command[2] * 8 + 8
        score = (checkpoint_count + 4 * len(streams["objects"]) +
                 8 * len(streams["secondary_objects"]) +
                 3 * len(streams["boundary"]))
        return {
            "header_index": header_index,
            "course_id": course_id,
            "background_id": background_id,
            "status": "compatible" if not blockers else "blocked",
            "blockers": blockers,
            "complexity_score": score,
            "blob_size": len(blob),
            "checkpoint_count": checkpoint_count,
            "checkpoint_bounds": {
                "min_x": min(x for x, _ in points) * 8,
                "max_x": max(x for x, _ in points) * 8,
                "min_y": min(y for _, y in points) * 8,
                "max_y": max(y for _, y in points) * 8,
            },
            "start": start,
            "finish_y": finish_y,
            "stream_counts": {name: len(records)
                              for name, records in streams.items()},
            "boundary_opcodes": sorted({record[0]
                                        for record in streams["boundary"]}),
        }
    except ValueError as error:
        return {"header_index": header_index, "course_id": course_id,
                "background_id": background_id, "status": "blocked",
                "blockers": [str(error)]}


def audit(rom, course, sky):
    offsets = course_offsets(course)
    rows = []
    for header_index in range(TRACK_HEADER_FIRST, TRACK_HEADER_LAST + 1):
        pointer = u32(rom, TRACK_DEF_TABLE_OFFSET + header_index * 4)
        if pointer < ROM_BASE or pointer - ROM_BASE + 8 > len(rom):
            rows.append({"header_index": header_index, "status": "blocked",
                         "blockers": ["track definition pointer out of bounds"]})
            continue
        definition = pointer - ROM_BASE
        course_id = u32(rom, definition)
        background_id = u32(rom, definition + 4)
        if course_id >= COURSE_DIRECTORY_ENTRIES:
            rows.append({"header_index": header_index, "course_id": course_id,
                         "background_id": background_id, "status": "blocked",
                         "blockers": ["course ID out of bounds"]})
            continue
        end = offsets[course_id + 1] if course_id + 1 < len(offsets) else len(course)
        row = inspect_course(course[offsets[course_id]:end], course_id,
                             header_index, background_id)
        try:
            row["sky_offset"] = validate_sky(sky, background_id)
        except ValueError as error:
            row.setdefault("blockers", []).append(str(error))
            row["status"] = "blocked"
        rows.append(row)
    candidates = sorted((row for row in rows if row["status"] == "compatible" and
                         row["course_id"] not in (24, 25)),
                        key=lambda row: (row["complexity_score"], row["course_id"]))
    return {
        "schema": 1,
        "inputs": {
            "rom_sha1": hashlib.sha1(rom).hexdigest(),
            "course_sha256": hashlib.sha256(course).hexdigest(),
            "sky_sha256": hashlib.sha256(sky).hexdigest(),
        },
        "source_contract": {
            "track_def_table_rom_offset": TRACK_DEF_TABLE_OFFSET,
            "header_first": TRACK_HEADER_FIRST,
            "header_last": TRACK_HEADER_LAST,
            "start_x": "command_x * 8 + 40",
            "start_y": "command_y * 8 + 28",
            "finish_y": "command_y * 8 + 8",
            "start_route_index": "checkpoint_count - 1",
        },
        "courses": rows,
        "recommended_course_id": candidates[0]["course_id"] if candidates else None,
        "recommended_header_index": candidates[0]["header_index"] if candidates else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--course", type=pathlib.Path, required=True)
    parser.add_argument("--sky", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    report = audit(args.rom.read_bytes(), args.course.read_bytes(),
                   args.sky.read_bytes())
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    compatible = sum(row["status"] == "compatible" for row in report["courses"])
    print(f"course catalog: PASS compatible={compatible}/20 "
          f"recommended={report['recommended_course_id']}", file=__import__('sys').stderr)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build a deterministic MKSC portable asset pack from normalized sections."""

import argparse
import hashlib
import json
import pathlib
import struct

MAGIC = b"MKSCPACK"
FORMAT_VERSION = 1
HEADER_SIZE = 112
SECTION_SIZE = 40
ENDIAN_MARKER = 0x12345678
FLAG_DETERMINISTIC = 2
PACK_DIGEST_OFFSET = 40
DETERMINISTIC_DIGEST_OFFSET = 72


def read_sections(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) < 112 or data[:8] != b"MKSCPACK":
        raise ValueError("expected an imported MKSCPACK")
    version, header, endian, size, count, stride, table = struct.unpack_from("<HHIQIIQ", data, 8)
    if (version, header, endian, size, stride) != (1, 112, 0x12345678, len(data), 40) or count > 4096:
        raise ValueError("unsupported pack header")
    checked = bytearray(data)
    checked[40:72] = bytes(32)
    if hashlib.sha256(checked).digest() != data[40:72]:
        raise ValueError("pack checksum mismatch")
    if table + count * stride > len(data):
        raise ValueError("truncated section table")
    for i in range(count):
        kind, content_id, version, flags, offset, length, revision, reserved = struct.unpack_from("<IIIIQQII", data, table + i * stride)
        if reserved or offset + length > len(data):
            raise ValueError("invalid section range")
        yield {"kind": kind, "content_id": content_id, "version": version,
               "flags": flags, "semantic_revision": revision}, bytearray(data[offset:offset + length])


def _align8(value):
    return (value + 7) & ~7


def build_pack(manifest_path, output_path):
    manifest_path = pathlib.Path(manifest_path)
    output_path = pathlib.Path(output_path)
    manifest = json.loads(manifest_path.read_text())
    sections = []
    for item in manifest.get("sections", []):
        path = manifest_path.parent / item["path"]
        sections.append({
            "kind": int(item["kind"]),
            "content_id": int(item["content_id"]),
            "version": int(item.get("version", 1)),
            "flags": int(item.get("flags", 0)),
            "semantic_revision": int(item.get("semantic_revision", 1)),
            "data": path.read_bytes(),
        })
    sections.sort(key=lambda section: (section["kind"], section["content_id"]))
    keys = [(section["kind"], section["content_id"]) for section in sections]
    if any(kind <= 0 or content_id <= 0 for kind, content_id in keys):
        raise ValueError("section kind and content_id must be positive")
    if len(set(keys)) != len(keys):
        raise ValueError("duplicate section kind/content_id")
    if any(not section["data"] for section in sections):
        raise ValueError("empty sections are not supported")

    table_offset = HEADER_SIZE
    payload_offset = _align8(table_offset + len(sections) * SECTION_SIZE)
    entries = []
    payload = bytearray()
    cursor = payload_offset
    for section in sections:
        aligned = _align8(cursor)
        payload.extend(b"\0" * (aligned - cursor))
        cursor = aligned
        entry = struct.pack(
            "<IIIIQQII", section["kind"], section["content_id"],
            section["version"], section["flags"], cursor,
            len(section["data"]), section["semantic_revision"], 0)
        entries.append(entry)
        payload.extend(section["data"])
        cursor += len(section["data"])

    total_size = payload_offset + len(payload)
    header = bytearray(HEADER_SIZE)
    struct.pack_into("<8sHHIQIIQ", header, 0, MAGIC, FORMAT_VERSION,
                     HEADER_SIZE, ENDIAN_MARKER, total_size, len(sections),
                     SECTION_SIZE, table_offset)
    table = b"".join(entries)
    padding = b"\0" * (payload_offset - HEADER_SIZE - len(table))
    pack = bytearray(header + table + padding + payload)

    deterministic = hashlib.sha256()
    for entry, section in zip(entries, sections):
        if section["flags"] & FLAG_DETERMINISTIC:
            deterministic.update(entry)
            deterministic.update(section["data"])
    pack[DETERMINISTIC_DIGEST_OFFSET:DETERMINISTIC_DIGEST_OFFSET + 32] = \
        deterministic.digest()
    pack_digest = hashlib.sha256(pack).digest()
    pack[PACK_DIGEST_OFFSET:PACK_DIGEST_OFFSET + 32] = pack_digest
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(pack)
    return pack_digest.hex(), deterministic.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    pack_digest, deterministic_digest = build_pack(args.manifest, args.output)
    print(f"pack_sha256={pack_digest}")
    print(f"deterministic_sha256={deterministic_digest}")


if __name__ == "__main__":
    main()

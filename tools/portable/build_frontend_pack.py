#!/usr/bin/env python3
"""Import native title and single-player menu images directly from the ROM."""

import argparse
import importlib.util
import json
import hashlib
import sys
import pathlib
import tempfile

_PACK_SCRIPT = pathlib.Path(__file__).with_name("build_asset_pack.py")
_PACK_SPEC = importlib.util.spec_from_file_location("build_asset_pack", _PACK_SCRIPT)
build_asset_pack = importlib.util.module_from_spec(_PACK_SPEC)
_PACK_SPEC.loader.exec_module(build_asset_pack)

def build_rom(rom_path, output):
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    from ui_rom import title_sections
    from menu_rom import menu_sections
    from character_menu_rom import character_sections
    from course_menu_rom import course_sections
    from pause_menu_rom import pause_sections
    from gp_failure_menu_rom import failure_sections
    rom = pathlib.Path(rom_path).read_bytes()
    if hashlib.sha1(rom).hexdigest() != "9d327c030c3e2d9007990518594f70c3340ac56f":
        raise ValueError("frontend import requires an unmodified US ROM")
    output = pathlib.Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        root = pathlib.Path(temporary)
        sections = []
        for kind, content_id, payload in title_sections(rom) + menu_sections(rom) + character_sections(rom) + course_sections(rom) + pause_sections(rom) + failure_sections(rom):
            name = f"image-{content_id}.bin"
            (root / name).write_bytes(payload)
            sections.append({"kind": kind, "content_id": content_id, "flags": 3, "path": name})
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps({"sections": sections}))
        return build_asset_pack.build_pack(manifest, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    pack_digest, deterministic_digest = build_rom(args.rom, args.output)
    print(f"pack_sha256={pack_digest}")
    print(f"deterministic_sha256={deterministic_digest}")


if __name__ == "__main__":
    main()

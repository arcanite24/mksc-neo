#!/usr/bin/env python3
import json
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).parents[3]
SCRIPT = ROOT / "tools/portable/build_frontend_pack.py"
SPEC = importlib.util.spec_from_file_location("build_frontend_pack", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main():
    output = pathlib.Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / "tools/portable"))
    from ui_rom import image_asset, TITLE_BACKGROUND, TITLE_BACKGROUND_FRAME, TITLE_LABEL, TITLE_SELECTED_LABEL, TITLE_FRAME, TITLE_UP, TITLE_DOWN
    sections = []
    images = [(TITLE_BACKGROUND, 240, 160, 0xFF002244, (0, 0))]
    images += [(TITLE_BACKGROUND_FRAME + i, 240, 160, 0xFF002244, (0, 0)) for i in range(1, 5)]
    images += [(base + i, 96, 16, 0xFF008800 + i * 0x220000 + (0x33 if base == TITLE_SELECTED_LABEL else 0), (-48, -8))
               for base in (TITLE_LABEL, TITLE_SELECTED_LABEL) for i in range(3)]
    images += [(TITLE_FRAME, 96, 16, 0xFF111111, (-48, -8)),
               (TITLE_UP, 16, 8, 0xFFFFFF00, (-8, -8)),
               (TITLE_DOWN, 16, 8, 0xFFFFFF00, (-8, -8))]
    images += [(0x21400+i,48,48,0xFF804020+i*0x2000,(-24,-24)) for i in range(4)]
    images += [(0x21000, 240, 160, 0xFF112244, (0, 0)),
               (0x21001, 120, 16, 0xFFFFCC00, (-60, -16))]
    images += [(base+i, 64, 64, 0xFF112233+i*0x220000, (-32, -32))
               for base, count in ((0x21100,4),(0x21200,3),(0x21300,2)) for i in range(count)]
    images += [(0x21104, 64, 64, 0xFF445566, (-32, -32)), (0x20203, 80, 16, 0xFF222222, (-40, -8))]
    # Breadcrumb tabs and player banners (menu_rom.tab_sections).
    images += [(0x26000+i, 32, 16, 0xFF203040+i*0x100000, (-16, -8)) for i in range(4)]
    images += [(0x26010+i, 32, 16, 0xFF304050+i*0x100000, (-16, -8)) for i in (0, 1, 2, 3, 5)]
    images += [(0x26020+i, 32, 16, 0xFF405060+i*0x100000, (-16, -8)) for i in range(3)]
    images += [(0x26030+i, 32, 16, 0xFF506070+i*0x100000, (-16, -8)) for i in range(2)]
    images += [(0x26040+i, 32, 16, 0xFF607080+i*0x100000, (-16, -8)) for i in range(8)]
    images += [(0x26050+i, 64, 32, 0xFFC02020+i*0x2000, (-32, -16)) for i in range(4)]
    images += [(0x26060, 72, 24, 0xFFE02040, (-24, -16)), (0x26061, 8, 16, 0xFFF0F0F0, (-4, -8)),
               (0x26062, 34, 16, 0xFF402010, (-17, -8)), (0x26071, 160, 72, 0xFF102030, (40, 48)),
               (0x26075, 160, 72, 0xFF203040, (40, 40)), (0x26080, 768, 16, 0xFFE0E0E0, (0, 0)),
               (0x26081, 24, 24, 0xFF8080F0, (0, 0))]
    # Seven Lakitu start-light frames (countdown_rom.py).
    images += [(0x27000+i, 48, 48, 0xFF305070+i*0x101010, (0, 0)) for i in range(7)]
    images += [(0x23000+i,240,160,0xFF112244+i*0x110000,(0,0)) for i in range(5)]
    images += [(0x23010+i,136,16,0xFFCCBB00+i*0x1100,(0,0)) for i in range(2)]
    images += [(0x23100+i,32,32,0xFF112233+i*0x110000,(0,0)) for i in range(10)]
    images += [(0x23200+i,64,16,0xFF112233+i*0x220000,(0,0)) for i in range(5)]
    images += [(0x23300+i,80,64,0xFF112233+i*0x040000,(0,0)) for i in range(40)]
    images += [(base+i,96,16,0xFF112233+i*0x040000+(0x2200 if base==0x23500 else 0),(0,0))
               for base in (0x23400,0x23500) for i in range(40)]
    images += [(0x23600+stripe*64+i,96,16,0xFF223344+stripe*0x1100+i*0x040000,(0,0))
               for stripe in range(2) for i in range(40)]
    images += [(0x23800,240,160,0xFF88CCDD,(0,0)),(0x23801,56,16,0xFFDD88AA,(0,0)),
               (0x23802,48,16,0xFFDD88AB,(0,0)),(0x23803,112,16,0xFFDD88AC,(0,0))]
    images += [(0x24000,80,96,0xFF112233,(0,0)),
               (0x24300,8,16,0xFFFF0000,(0,0))]
    images += [(base+i,56,16,0xFF112233+i*0x110000+(0x2200 if base==0x24200 else 0),(0,0))
               for base in (0x24100,0x24200) for i in range(5)]
    images += [(0x25004,240,160,0xFF7799AA,(0,0))]
    images += [(0x25000+i,96,48,0xFF112233+i*0x220000,(0,0)) for i in range(4)]
    from character_menu_rom import animation_asset
    images.append((0x22000, 207, 18, 0xFFFFCC00, (12, 7)))
    for slot in range(9):
        x, y = 136 + slot % 3 * 40, 56 + slot // 3 * 40
        images += [(0x22010 + slot, 2, 2, 0xFFFF0000, (x-16,y-16)),
                   *[(0x26100 + player*16 + slot, 2, 2, 0xFF00FF00 + player*0x40, (x-16,y-16))
                     for player in (1, 2, 3)],
                   *[(0x26200 + player*16 + slot, 2, 2, 0xFF0000FF + player*0x400000, (x-28,y-4))
                     for player in range(4)],
                   (0x22200 + slot, 64, 16, 0xFF001100 + slot*0x110000, (40,32))]
        for state in range(4):
            frames = []
            for phase in range(8):
                content_id = 0x30000 + slot*32 + state*8 + phase
                preview = state == 3
                images.append((content_id, 64 if preview else 32, 64 if preview else 32,
                               0xFF112233 + slot*0x110000 + phase*0x1100 + state*0x11,
                               (40,51) if preview else (x-16,y-16)))
                frames.append((content_id, 4))
            animation_id = 0x22300 + slot if state == 3 else 0x22100 + slot*3 + state
            name = f"animation-{animation_id}.bin"
            (output / name).write_bytes(animation_asset(frames, state != 2))
            sections.append({"kind": 15, "content_id": animation_id, "path": name})
    for content_id, w, h, color, anchor in images:
        name = f"native-{content_id}.bin"
        (output / name).write_bytes(image_asset(w, h, [color] * (w * h), anchor))
        sections.append({"kind": 15, "content_id": content_id, "path": name})
    manifest = output / "manifest.json"
    manifest.write_text(json.dumps({"sections": sections}))
    MODULE.build_asset_pack.build_pack(manifest, output / "fixture.pack")


if __name__ == "__main__":
    main()

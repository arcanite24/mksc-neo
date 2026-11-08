"""Build isolated unlocked saves for ordinary and seeded GP menu routes."""
from pathlib import Path
import struct
import sys


def progress(unlock_flags, night=False):
    # gp_progress_store.c v1: base golds permit Special Cup selection.
    ratings = bytes(15) if night else bytes([255]) * 15
    payload = bytes(15) + bytes([255]) * 15 + ratings + bytes([255]) * 15 + struct.pack('<H', unlock_flags) + bytes(2)
    checksum = 2166136261
    for value in payload:
        checksum = ((checksum ^ value) * 16777619) & 0xffffffff
    return struct.pack('<8sHHI', b'MKSCGP01', 1, 80, checksum) + payload


def make(root, unlock_only=False, title_night=False):
    root.mkdir(parents=True, exist_ok=True)
    if title_night:
        (root / 'title-night.bin').write_bytes(progress(0x7fff, night=True))
        print('GP title fixture: all base triple-star ratings unlock night panorama')
        return
    if unlock_only:
        (root / 'unlock-0-0.bin').write_bytes(progress(0))
        print('GP unlock fixture: five prior golds, no earned Extra bits')
        return
    data = progress(0x7fff)
    for cup in range(10):
        for engine in range(3):
            for kind in ('ordinary', 'seeded'):
                (root / f'{kind}-{cup}-{engine}.bin').write_bytes(data)
    print('GP cup save fixtures: base golds, Extra access, isolated route saves')


if __name__ == '__main__':
    make(Path(sys.argv[1]), "--unlock" in sys.argv[2:], "--title-night" in sys.argv[2:])

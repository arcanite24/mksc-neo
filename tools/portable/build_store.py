"""Embed the offline store from authored examples; never read ROM content."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]

def archive(mod):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_STORED) as z:
        for name in ('mod.json', 'main.lua'):
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            z.writestr(info, (ROOT / 'mods/examples' / mod / name).read_bytes())
    return output.getvalue()

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    index = (ROOT / 'stores/official/index.json').read_bytes()
    entries = json.loads(index)['mods']
    blobs = [('builtin://official', index)]
    for entry in entries:
        data = archive(entry['id'])
        if len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise SystemExit(f"Update official index size/hash for {entry['id']}")
        blobs.append((entry['download_url'], data))
    lines = ['/* Generated from authored store sources. */']
    for i, (_, data) in enumerate(blobs):
        lines.append(f'static const unsigned char store_blob_{i}[] = {{' + ','.join(str(b) for b in data) + '};')
    lines.append('static const struct {const char *url; const unsigned char *data; size_t size;} store_blobs[] = {')
    for i, (url, _) in enumerate(blobs):
        lines.append(f'{{"{url}",store_blob_{i},sizeof store_blob_{i}}},')
    lines.append('};')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n')

if __name__ == '__main__':
    main()

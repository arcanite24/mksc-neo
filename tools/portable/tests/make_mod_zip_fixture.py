"""Write ROM-free .zip mods for the native loader test: valid stored/deflated
archives, a nested root folder, and archives that must be rejected (unsafe
paths, oversized or lying sizes, bad CRC, wrong id, encryption flag).

    python3 make_mod_zip_fixture.py OUT_DIR MOD.pack
    python3 make_mod_zip_fixture.py --manager ROOT
"""
import json
from pathlib import Path
import struct
import sys
import zipfile


def manifest(mod_id, **extra):
    return json.dumps({'id': mod_id, 'name': 'Zip ' + mod_id, 'version': '1.0.0',
                       'api_version': 1, **extra})


def write(path, entries, method=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(path, 'w', method) as archive:
        for name, data in entries:
            info = zipfile.ZipInfo(name)
            info.filename = name  # ZipInfo turns '\\' into '/' on Windows; keep it raw
            archive.writestr(info, data, compress_type=method)


def patch(path, edit):
    data = bytearray(path.read_bytes())
    edit(data)
    path.write_bytes(bytes(data))


def headers(data, signature):
    at = data.find(signature)
    while at >= 0:
        yield at
        at = data.find(signature, at + 4)


def main(out, mod_pack):
    mods = Path(out) / 'mods'
    mods.mkdir(parents=True, exist_ok=True)
    pack = Path(mod_pack).read_bytes()
    lua = b'function on_tick(race) end\n' * 64
    write(mods / 'zipped.zip', [('mod.json', manifest('zipped', pack='content.pack', script='main.lua')),
                                ('content.pack', pack), ('main.lua', lua)])
    write(mods / 'stored.zip', [('mod.json', manifest('stored', script='lua/main.lua')),
                                ('lua/', b''), ('lua/main.lua', lua)], zipfile.ZIP_STORED)
    write(mods / 'nested.zip', [('nested/mod.json', manifest('nested', pack='content.pack')),
                                ('nested/content.pack', pack)])
    write(mods / 'traversal.zip', [('mod.json', manifest('traversal', script='main.lua')),
                                   ('main.lua', lua), ('../escape.lua', lua)])
    write(mods / 'backslash.zip', [('mod.json', manifest('backslash', script='main.lua')),
                                   ('main.lua', lua), ('a\\b.lua', lua)])
    write(mods / 'wrong-id.zip', [('mod.json', manifest('other', script='main.lua')), ('main.lua', lua)])
    write(mods / 'duplicate.zip', [('mod.json', manifest('duplicate', script='main.lua')),
                                   ('main.lua', lua), ('main.lua', lua)])
    # Declares a 80 MiB entry: refused when the directory is read.
    write(mods / 'bomb.zip', [('mod.json', manifest('bomb', pack='content.pack')), ('content.pack', bytes(4096))])
    patch(mods / 'bomb.zip', lambda d: [struct.pack_into('<I', d, at + 24, 80 << 20)
                                        for at in headers(d, b'PK\x01\x02')])
    # Expands past its declared size: inflate stops at the buffer and fails.
    write(mods / 'lying.zip', [('mod.json', manifest('lying', pack='content.pack')), ('content.pack', bytes(65536))])
    def lie(d):
        for at in headers(d, b'PK\x01\x02'):
            if d[at + 46:at + 58] == b'content.pack':
                struct.pack_into('<I', d, at + 24, 4096)
        for at in headers(d, b'PK\x03\x04'):
            if d[at + 30:at + 42] == b'content.pack':
                struct.pack_into('<I', d, at + 22, 4096)
    patch(mods / 'lying.zip', lie)
    write(mods / 'bad-crc.zip', [('mod.json', manifest('bad-crc', script='main.lua')), ('main.lua', lua)])
    def crc(d):
        for at in headers(d, b'PK\x01\x02'):
            if d[at + 46:at + 54] == b'main.lua':
                d[at + 16] ^= 0xff
    patch(mods / 'bad-crc.zip', crc)
    write(mods / 'encrypted.zip', [('mod.json', manifest('encrypted', script='main.lua')), ('main.lua', lua)])
    patch(mods / 'encrypted.zip', lambda d: [struct.pack_into('<H', d, at + 8, 1)
                                             for at in headers(d, b'PK\x01\x02')])
    # A folder with mod.json wins over a broken zip of the same id.
    (mods / 'both').mkdir(exist_ok=True)
    (mods / 'both/mod.json').write_text(manifest('both', script='main.lua'))
    (mods / 'both/main.lua').write_bytes(lua)
    (mods / 'both.zip').write_bytes(b'not a zip')


def manager_fixture(root):
    """Ordinary shell tours use these authored mods and isolated saved settings."""
    root = Path(root)
    mods = root / 'data' / 'mods'
    mods.mkdir(parents=True, exist_ok=True)
    entries = (
        ('a-base', {'script': 'main.lua'},
         b"function on_race_start(race) race.message('MANAGED MOD A',120) end\n"),
        ('b-after', {'script': 'main.lua', 'depends': ['a-base']}, b'-- Valid empty mod.\n'),
        ('c-bad-lua', {'script': 'main.lua'}, b'function on_race_start( broken syntax\n'),
        ('d-bad-pack', {'pack': 'content.pack'}, None),
    )
    for mod_id, fields, lua in entries:
        directory = mods / mod_id
        directory.mkdir(exist_ok=True)
        (directory / 'mod.json').write_text(manifest(mod_id, **fields), encoding='utf-8')
        if lua is not None:
            (directory / 'main.lua').write_bytes(lua)
        else:
            (directory / 'content.pack').write_bytes(b'Not an asset pack.\n')
    (mods / 'e-bad-zip.zip').write_bytes(b'Not a ZIP archive.\n')
    settings = b'{"version":1,"mods":[]}\n'
    (root / 'data' / 'settings.json').write_bytes(settings)
    (root / 'before.json').write_bytes(settings)


def installer_fixture(root):
    """Owner-selected arbitrary filenames, kept outside the installed mod root."""
    root = Path(root)
    (root / 'data' / 'mods').mkdir(parents=True, exist_ok=True)
    for name, version, lua in (
        ('picked', '1.0.0', "function on_race_start(race) race.message('INSTALLED ZIP',120) end"),
        ('updated', '2.0.0', "function on_race_start(race) race.message('UPDATED ZIP',120) end"),
        ('bad-lua', '3.0.0', 'function broken syntax !!!'),
    ):
        write(root / (name + '.zip'), [
            ('installed/mod.json', manifest('installed', version=version, script='main.lua')),
            ('installed/main.lua', lua)])
    # The outer pack is valid; only its typed frontend motion is malformed.
    # This distinguishes content preflight from ZIP, checksum and Lua checks.
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'fixture_pack_builder', Path(__file__).resolve().parents[1] / 'build_asset_pack.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    (root / 'bad-motion.bin').write_bytes(b'FEM1' + bytes(4))
    (root / 'bad-motion-sections.json').write_text(json.dumps({'sections': [{
        'kind': 92, 'content_id': 1, 'version': 1, 'path': 'bad-motion.bin',
    }]}), encoding='utf-8')
    builder.build_pack(root / 'bad-motion-sections.json', root / 'bad-motion.pack')
    write(root / 'bad-motion.zip', [
        ('mod.json', manifest('installed', version='3.0.0', pack='content.pack')),
        ('content.pack', (root / 'bad-motion.pack').read_bytes()),
    ])
    projection = struct.pack('<II', 0x31525044, 3)
    for distance, size in ((1, 64), (1, 32), (3, 16)):
        projection += struct.pack('<HHhhBBH', distance, size, 0, 0, 0, 0, 0)
    (root / 'bad-race.bin').write_bytes(projection)
    (root / 'bad-race-sections.json').write_text(json.dumps({'sections': [{
        'kind': 82, 'content_id': 1, 'version': 1, 'path': 'bad-race.bin',
    }]}), encoding='utf-8')
    builder.build_pack(root / 'bad-race-sections.json', root / 'bad-race.pack')
    write(root / 'bad-race.zip', [
        ('mod.json', manifest('installed', version='3.0.0', pack='content.pack')),
        ('content.pack', (root / 'bad-race.pack').read_bytes()),
    ])
    write(root / 'traversal.zip', [('mod.json', manifest('installed')),
                                 ('../escape.txt', 'outside')])
    write(root / 'oversized.zip', [('mod.json', manifest('installed')),
                                 ('unused.bin', bytes(16))])
    patch(root / 'oversized.zip', lambda data: [struct.pack_into('<I', data, at + 24, 80 << 20)
          for at in headers(data, b'PK\x01\x02') if data[at + 46:at + 56] == b'unused.bin'])
    settings = b'{"version":1,"mods":[]}\n'
    (root / 'data' / 'settings.json').write_bytes(settings)
    (root / 'before.json').write_bytes(settings)


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--installer':
        installer_fixture(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == '--manager':
        manager_fixture(sys.argv[2])
    else:
        main(*sys.argv[1:3])

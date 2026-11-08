import pathlib
import random
import struct
import sys
import unittest
from unittest import mock
import contextlib

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from content_scan import RomContentIndex, content_matches, literal_payloads, pe_export_ordinal_ranges
import package_release
from package_release import check_bytes, check_bundle, rom_samples
import tempfile
import zipfile
import tarfile
import io
import subprocess
from check_repo_content import check_tree


class ContentScanTests(unittest.TestCase):
    def test_every_alignment_and_low_colour_table(self):
        rom = random.Random(731).randbytes(1024)
        index = RomContentIndex(rom)
        for offset in range(16):
            self.assertTrue(index.find(b'prefix' + rom[256 + offset:320 + offset]))
        table = struct.pack('<' + 'h' * 64, *[i % 7 for i in range(64)])
        self.assertTrue(RomContentIndex(rom + table).find(table))

    def test_repeated_anchor_extends_from_every_occurrence(self):
        anchor = random.Random(45).randbytes(32)
        tail = random.Random(46).randbytes(32)
        rom = anchor + b'x' * 32 + anchor + tail
        self.assertTrue(RomContentIndex(rom).find(anchor + tail))

    def test_padding_and_short_coincidences_are_not_assets(self):
        index = RomContentIndex(bytes(range(128)) + b'\0' * 1024)
        self.assertIsNone(index.find(b'\0' * 512))
        self.assertIsNone(index.find(bytes(range(32))))

    def test_signed_multiline_constructor_literals(self):
        values = [i % 9 - 4 for i in range(64)]
        table = struct.pack('<' + 'h' * len(values), *values)
        index = RomContentIndex(b'prefix' * 16 + table)
        rows = ['{' + ', '.join(f'INT16_C({v})' for v in values[i:i + 8]) + '}'
                for i in range(0, len(values), 8)]
        source = ('static const int16_t data[8][8] = {' + ',\n'.join(rows) + '};').encode()
        self.assertTrue(content_matches('rules.c', source, index))
        self.assertFalse(content_matches('rules.c', b'/* 1,2,3,4,5,6,7,8 */', index))

    def test_decimal_leading_zero_and_unsigned_hex(self):
        payloads = list(literal_payloads(b'{01, 2u, 0x3UL, -4, 5, 6, 7, 8}'))
        self.assertIn((1, bytes([1, 2, 3, 252, 5, 6, 7, 8])), payloads)

    def test_independent_math_is_not_reported_as_game_content(self):
        data=bytes(i.bit_length() for i in range(256))
        self.assertIsNone(RomContentIndex(data).find(data))
        alphabet=struct.pack('<256H',*range(256))
        self.assertIsNone(RomContentIndex(alphabet+b'\0\xff').find(alphabet+b'\0\xff'))

    @staticmethod
    def pe_identity_exports(is_64=True):
        count = 320
        optional_size = 240 if is_64 else 224
        pe, raw, rva = 128, 1024, 8192
        function_table = raw + 40
        name_table = function_table + count * 4
        ordinals = name_table + count * 4
        strings = ordinals + count * 2
        dll_name = strings
        names = [f'Export_{i:03d}'.encode() + b'\0' for i in range(count)]
        string_data = b'vendor.dll\0' + b''.join(names)
        size = strings + len(string_data) - raw
        data = bytearray(raw + size)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 60, pe)
        data[pe:pe + 4] = b'PE\0\0'
        struct.pack_into('<HHIIIHH', data, pe + 4, 0x8664 if is_64 else 0x14c,
                         2, 0, 0, 0, optional_size, 0x2002)
        optional = pe + 24
        struct.pack_into('<H', data, optional, 0x20b if is_64 else 0x10b)
        directory = optional + (112 if is_64 else 96)
        struct.pack_into('<III', data, directory - 4, 16, rva, size)
        section = optional + optional_size
        struct.pack_into('<8s4I', data, section, b'.text', 512, 4096, 512, 512)
        struct.pack_into('<8s4I', data, section + 40, b'.edata', size, rva, size, raw)
        to_rva = lambda offset: rva + offset - raw
        struct.pack_into('<IIHH7I', data, raw, 0, 0, 0, 0, to_rva(dll_name),
                         1, count, count, to_rva(function_table), to_rva(name_table), to_rva(ordinals))
        cursor = strings + len(b'vendor.dll\0')
        for i, name in enumerate(names):
            struct.pack_into('<I', data, function_table + i * 4, 4096)
            struct.pack_into('<I', data, name_table + i * 4, to_rva(cursor))
            struct.pack_into('<H', data, ordinals + i * 2, i)
            cursor += len(name)
        data[strings:] = string_data
        return bytes(data), ordinals, raw, directory

    def test_only_validated_pe_identity_ordinals_are_excluded(self):
        # Values above255 remain distinctive outside the validated container.
        fragment = b'\0' + struct.pack('<32H', *range(256, 288))
        index = RomContentIndex(fragment)
        self.assertTrue(index.find(fragment))
        for is_64 in (False, True):
            data, ordinals, export, directory = self.pe_identity_exports(is_64)
            self.assertEqual(pe_export_ordinal_ranges(data), ((ordinals, ordinals + 640),))
            self.assertIsNone(index.find(data))
            self.assertTrue(index.find(data + b'x' + fragment))
            # A match extending even one byte outside the table stays visible.
            outside = bytes(data[ordinals + 576:ordinals + 640]) + data[ordinals + 640:ordinals + 641]
            self.assertTrue(RomContentIndex(outside).find(data))
            corruptions = ((0, b'ZZ'), (directory, struct.pack('<I', 0xfffffff0)),
                           (export + 24, struct.pack('<I', 65537)),
                           (export + 32, struct.pack('<I', 0xfffffff0)),
                           (export + 36, struct.pack('<I', 4096)),
                           (export + 40, bytes(4)),
                           (ordinals, struct.pack('<H', 2)))
            for offset, replacement in corruptions:
                broken = bytearray(data)
                broken[offset:offset + len(replacement)] = replacement
                self.assertEqual(pe_export_ordinal_ranges(broken), ())
                self.assertTrue(index.find(bytes(broken)), (is_64, offset))
            self.assertEqual(pe_export_ordinal_ranges(data[:ordinals + 600]), ())
            self.assertTrue(index.find(data[:ordinals + 600]))

    def test_indexed_images_and_renamed_assets_fail(self):
        for magic in (b'MKSCIMG1', b'MKSCIMG2', b'MKSCANI1', b'MKSCPACK'):
            self.assertTrue(check_bytes('renamed.dat', magic + b'\0' * 24, None))
        self.assertTrue(check_bytes('sample.aif', b'not an image', None))
        self.assertTrue(check_bytes('hidden.zip', b'compressed payload', None))

    def test_json_tables_are_scanned_and_rom_identity_is_verified(self):
        values=[i % 7 for i in range(80)]
        index=RomContentIndex(bytes(values))
        payload=('['+','.join(str(value) for value in values)+']').encode()
        self.assertTrue(content_matches('rules.json',payload,index))
        with tempfile.TemporaryDirectory() as directory:
            path=pathlib.Path(directory)/'wrong.gba'
            path.write_bytes(b'wrong reference')
            with self.assertRaisesRegex(ValueError,'SHA-1 mismatch'):
                rom_samples(path)

    def test_directory_zip_and_tar_scan_decompressed_members(self):
        rom=random.Random(501).randbytes(1024)
        index=RomContentIndex(rom)
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            stage=root/'stage'
            stage.mkdir()
            payload=b'prefix'+rom[311:439]+b'suffix'
            (stage/'engine').write_bytes(payload)
            archive=root/'release.zip'
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as bundle:
                bundle.writestr('engine',payload)
            tarpath=root/'release.tar.gz'
            with tarfile.open(tarpath,'w:gz') as bundle:
                member=tarfile.TarInfo('engine');member.size=len(payload)
                bundle.addfile(member,io.BytesIO(payload))
            for path in (stage,archive,tarpath):
                problems,count=check_bundle(path,index)
                self.assertEqual(count,1)
                self.assertTrue(problems)

    def test_repository_includes_new_files_but_ignores_owner_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory)
            subprocess.run(['git','init','-q',str(root)],check=True)
            (root/'.gitignore').write_text('owner.gba\n')
            (root/'owner.gba').write_bytes(b'private owner content')
            (root/'new.dat').write_bytes(b'MKSCIMG2'+bytes(32))
            problems=check_tree(root,None)
            self.assertEqual(len(problems),1)
            self.assertIn('new.dat',problems[0])


class WebPackageTests(unittest.TestCase):
    @staticmethod
    def inputs(root):
        build = root / 'web-build'
        build.mkdir()
        (build / 'mksc.html').write_text('<!doctype html><title>Engine</title>')
        (build / 'mksc.js').write_text('var Module = {};\n')
        (build / 'mksc.wasm').write_bytes(b'\0asm\1\0\0\0')
        prefix = root / 'sdl'
        prefix.mkdir()
        (prefix / 'LICENSE.txt').write_text('Authored SDL license fixture\n')
        return build, prefix

    def package(self, build, prefix, output, samples=None):
        argv = ['package_release.py', '--web-build', str(build),
                '--sdl-prefix', str(prefix), '--output', str(output), '--version', 'test']
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(package_release, 'configure_and_build',
                               side_effect=AssertionError('web packaging must not build')), \
             mock.patch.object(package_release, 'sdl_library',
                               side_effect=AssertionError('web uses SDL notices only')), \
             mock.patch.object(package_release, 'rom_samples', return_value=samples), \
             contextlib.redirect_stdout(io.StringIO()):
            return package_release.main()

    def test_web_zip_contains_only_runtime_and_notices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build, prefix = self.inputs(root)
            (build / 'game.pack').write_bytes(b'MKSCPACK excluded fixture')
            (build / 'mksc.data').write_bytes(b'preloaded data must not be bundled')
            (build / 'unrelated.js').write_text('not part of the runtime')
            output = root / 'dist'
            self.assertEqual(self.package(build, prefix, output), 0)
            archive = output / 'mksc-neo-test-web-wasm32.zip'
            expected = {*package_release.WEB_FILES, 'README.txt', 'LICENSE-NOTICE.md',
                        'LUA-LICENSE.txt', 'SDL3-LICENSE.txt'}
            with zipfile.ZipFile(archive) as bundle:
                self.assertEqual(set(bundle.namelist()),
                                 {'mksc-neo-test-web-wasm32/' + name for name in expected})
                for name in package_release.WEB_FILES:
                    self.assertEqual(bundle.read('mksc-neo-test-web-wasm32/' + name),
                                     (build / name).read_bytes())
                readme = bundle.read('mksc-neo-test-web-wasm32/README.txt').decode()
                self.assertIn('http.server 8000 --bind 127.0.0.1', readme)
                self.assertIn('temporary', readme)
                self.assertIn('Choose your game.pack', readme)
            self.assertEqual(check_bundle(archive, None), ([], len(expected)))

    def test_missing_web_runtime_rejects_before_staging(self):
        for missing in package_release.WEB_FILES:
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                build, prefix = self.inputs(root)
                (build / missing).unlink()
                stage = root / 'stage'
                with self.assertRaisesRegex(SystemExit, 'prebuilt web runtime file missing'):
                    package_release.stage_web(stage, build, prefix, 'test')
                self.assertFalse(stage.exists())

    def test_web_source_is_not_deleted_as_output_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build, prefix = self.inputs(root)
            output = root / 'dist'
            output.mkdir()
            stage = output / 'mksc-neo-test-web-wasm32'
            build.rename(stage)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                self.package(stage, prefix, output)
            self.assertTrue((stage / 'mksc.wasm').is_file())

    def test_web_packaging_rejects_content_disguised_as_runtime(self):
        rom = random.Random(297).randbytes(1024)
        cases = ((b'MKSCPACK' + bytes(64), None),
                 (b'\0asm' + rom[137:237], RomContentIndex(rom)))
        for payload, samples in cases:
            with self.subTest(payload=payload[:8]), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                build, prefix = self.inputs(root)
                (build / 'mksc.wasm').write_bytes(payload)
                output = root / 'dist'
                with self.assertRaisesRegex(SystemExit, 'bundle content check failed'):
                    self.package(build, prefix, output, samples)
                self.assertFalse((output / 'mksc-neo-test-web-wasm32.zip').exists())


if __name__ == '__main__':
    unittest.main()

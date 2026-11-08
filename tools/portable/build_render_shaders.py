#!/usr/bin/env python3
"""Build authored SDL_GPU shader arrays offline; no compiler enters the player.

Bootstrap (macOS arm64/x64, Linux x64 or Windows x64):
  build_render_shaders.py --bootstrap .mksc-tools/portable/tools/render-shaders
Repeat with explicit --dxc PATH --spirv-cross PATH --tool-lock CACHE/tools.json.
The lock records executables/libraries built from hash-checked upstream archives.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DXC_VERSION = 'v1.8.2505.1'
CROSS_VERSION = 'vulkan-sdk-1.4.313.0'
CROSS_COMMIT = '2275d0efc4f2fa46851035d9d3c67c105bc8b99e'
CROSS_SHA = '7d1de24918bea9897753f7561d4d154f68ec89c36bb70c13598222b8039d4212'
DXC_ARCHIVES = {
    'Darwin': ('DXC-1.8.2505.1.tar.gz',
               '6d2ea8579c29b87c759ddc1300f9901ad413c483b2bfa1f799cb376f6d0789b3'),
    'Linux': ('linux_dxc_2025_07_14.x86_64.tar.gz',
              'f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c'),
    'Windows': ('dxc_2025_07_14.zip',
                '9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(arguments):
    result = subprocess.run([str(arg) for arg in arguments],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError(f'{arguments[0]} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}')
    return result.stdout


def download(url, path, digest):
    if not path.exists():
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if sha(data) != digest:
            raise ValueError(f'{path.name}: archive SHA256 mismatch')
        path.write_bytes(data)
    if sha(path.read_bytes()) != digest:
        raise ValueError(f'{path.name}: cached archive SHA256 mismatch')


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as bundle:
            for entry in bundle.infolist():
                target = (destination / entry.filename).resolve()
                if not target.is_relative_to(destination.resolve()):
                    raise ValueError('tool ZIP path escapes cache')
            bundle.extractall(destination)
    else:
        with tarfile.open(archive) as bundle:
            root = destination.resolve()
            for entry in bundle.getmembers():
                target = (root / entry.name).resolve()
                if not target.is_relative_to(root):
                    raise ValueError('tool tar path escapes cache')
                if entry.issym() or entry.islnk():
                    linked = (target.parent if entry.issym() else root) / entry.linkname
                    if not linked.resolve().is_relative_to(root):
                        raise ValueError('tool tar link escapes cache')
                elif not (entry.isfile() or entry.isdir()):
                    raise ValueError('tool tar contains a special file')
                bundle.extract(entry, root)


def verify_archive(archive, destination):
    """Do not compile a locally edited tree while claiming its upstream pin."""
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as bundle:
            for entry in bundle.infolist():
                if not entry.is_dir():
                    target = destination / entry.filename
                    if not target.is_file() or sha(target.read_bytes()) != sha(bundle.read(entry)):
                        raise ValueError(f'edited tool cache: {target}')
    else:
        with tarfile.open(archive) as bundle:
            for entry in bundle.getmembers():
                if entry.isfile():
                    target = destination / entry.name
                    stream = bundle.extractfile(entry)
                    if not target.is_file() or sha(target.read_bytes()) != sha(stream.read()):
                        raise ValueError(f'edited tool cache: {target}')
                elif entry.issym():
                    target = destination / entry.name
                    if not target.is_symlink() or os.readlink(target) != entry.linkname:
                        raise ValueError(f'edited tool link: {target}')


def bootstrap(cache):
    system = platform.system()
    if system not in DXC_ARCHIVES or platform.machine().lower() not in (('x86_64', 'amd64', 'arm64') if system == 'Darwin' else ('x86_64', 'amd64')):
        raise ValueError('bootstrap requires macOS arm64/x64 or Linux/Windows x64')
    cache = cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    name, dxc_sha = DXC_ARCHIVES[system]
    archive = cache / name
    url = (f'https://github.com/GaijinEntertainment/DXC-prebuilt/releases/download/dxc-1.8.2505.1/{name}' if system == 'Darwin' else
           f'https://github.com/microsoft/DirectXShaderCompiler/releases/download/{DXC_VERSION}/{name}')
    download(url, archive, dxc_sha)
    dxc_root = cache / ('dxc-' + DXC_VERSION)
    if not dxc_root.exists():
        extract(archive, dxc_root)
    verify_archive(archive, dxc_root)
    dxc = dxc_root / ('bin/x64/dxc.exe' if system == 'Windows' else 'bin/dxc')
    if system == 'Darwin':
        native = dxc_root / 'DXC-1.8.2505.1'
        dxc = cache / 'dxc-mac-cli'
        run(['c++', '-std=c++17', '-O2', '-I', native / 'include',
             '-DDXC_LIBRARY="' + str(native / 'lib/macosx/dxcompiler.dylib') + '"',
             ROOT / 'tools/portable/shader_dxc_mac.cpp', '-o', dxc])
    cross_archive = cache / (CROSS_VERSION + '.tar.gz')
    download(f'https://codeload.github.com/KhronosGroup/SPIRV-Cross/tar.gz/refs/tags/{CROSS_VERSION}',
             cross_archive, CROSS_SHA)
    sources = cache / 'sources'
    cross_root = sources / ('SPIRV-Cross-' + CROSS_VERSION)
    if not cross_root.exists():
        extract(cross_archive, sources)
    verify_archive(cross_archive, sources)
    build = cache / 'spirv-cross-build'
    run(['cmake', '-S', cross_root, '-B', build, '-DCMAKE_BUILD_TYPE=Release',
         '-DSPIRV_CROSS_CLI=ON', '-DSPIRV_CROSS_STATIC=ON', '-DSPIRV_CROSS_SHARED=OFF',
         '-DSPIRV_CROSS_ENABLE_TESTS=OFF', '-DSPIRV_CROSS_ENABLE_GLSL=ON',
         '-DSPIRV_CROSS_ENABLE_MSL=ON', '-DSPIRV_CROSS_ENABLE_REFLECT=ON',
         '-DSPIRV_CROSS_ENABLE_HLSL=ON', '-DSPIRV_CROSS_ENABLE_CPP=ON',
         '-DCMAKE_DISABLE_FIND_PACKAGE_Git=ON',
         '-DSPIRV_CROSS_ENABLE_C_API=OFF'])
    run(['cmake', '--build', build, '--config', 'Release', '--target', 'spirv-cross', '--parallel', '4'])
    cross = build / ('Release/spirv-cross.exe' if system == 'Windows' else 'spirv-cross')
    if not cross.exists() and system == 'Windows':
        cross = build / 'spirv-cross.exe'
    # Preserve upstream licenses in the compiler cache. No tool is linked by mksc.
    dependencies = [dxc, cross]
    dependencies += sorted(dxc_root.rglob('*.dll')) if system == 'Windows' else sorted(dxc_root.rglob('*.dylib')) if system == 'Darwin' else sorted(dxc_root.rglob('*.so*'))
    lock = {'dxc': DXC_VERSION, 'spirv_cross': CROSS_VERSION, 'spirv_cross_commit': CROSS_COMMIT,
            'dxc_archive_sha256': dxc_sha, 'spirv_cross_archive_sha256': CROSS_SHA,
            'files': {str(path.relative_to(cache)): sha(path.read_bytes()) for path in dependencies}}
    lock_path = cache / 'tools.json'
    lock_path.write_text(json.dumps(lock, sort_keys=True, indent=2) + '\n')
    return dxc, cross, lock_path


def validate_tools(dxc, cross, lock_path):
    lock = json.loads(lock_path.read_text())
    if (lock.get('dxc') != DXC_VERSION or lock.get('spirv_cross') != CROSS_VERSION or
            lock.get('spirv_cross_commit') != CROSS_COMMIT or
            lock.get('spirv_cross_archive_sha256') != CROSS_SHA or
            lock.get('dxc_archive_sha256') not in [item[1] for item in DXC_ARCHIVES.values()]):
        raise ValueError('tool lock does not identify the pinned upstream archives')
    checked = set()
    for relative, digest in lock['files'].items():
        path = (lock_path.parent / relative).resolve()
        if not path.is_relative_to(lock_path.parent.resolve()) or sha(path.read_bytes()) != digest:
            raise ValueError(f'tool lock mismatch: {relative}')
        checked.add(path)
    if dxc.resolve() not in checked or cross.resolve() not in checked:
        raise ValueError('explicit compiler paths must match hash-checked tool lock')
    version = run([dxc, '--version'])
    # Upstream's pinned Linux release labels itself 1.9(dev;b106a961),
    # while Windows uses the release file version. Archive hashes are primary.
    if 'b106a96' not in version and '1.8.2505.32' not in version:
        raise ValueError(f'unexpected DXC version: {version.strip()}')


def reflect(cross, binary, stage):
    document = json.loads(run([cross, binary, '--reflect']))
    expected_set = 1 if stage == 'vertex' else 3
    expected_size = 64 if stage == 'vertex' else 144
    ubos = document.get('ubos', [])
    if len(ubos) != 1 or (ubos[0].get('set'), ubos[0].get('binding'), ubos[0].get('block_size')) != (expected_set, 0, expected_size):
        raise ValueError(f'{stage}: uniform layout is not SDL slot0/{expected_size} bytes')
    members = document['types'][ubos[0]['type']]['members']
    if stage == 'vertex':
        if [(x.get('type'), x.get('offset')) for x in members] != [('mat4', 0)]:
            raise ValueError('vertex: MVP must be one column-major mat4')
        # DXC transposes HLSL matrix types: column_major HLSL is RowMajor
        # SPIR-V. SPIRV-Cross restores the original MSL column-major product.
        # See pinned DXC docs/SPIR-V.rst, Vectors and matrices.
        if not members[0].get('row_major', False) or members[0].get('matrix_stride') != 16:
            raise ValueError('vertex: unexpected DXC matrix storage layout')
        inputs = sorted((x.get('location'), x.get('type')) for x in document.get('inputs', []))
        if inputs != [(0, 'vec3'), (1, 'vec2'), (2, 'vec4'), (3, 'vec3')]:
            raise ValueError('vertex: expected float3/float2/float4/float3 input at locations0/1/2/3')
    else:
        if [(x.get('type'), x.get('offset')) for x in members] != [('uint', 0), ('uint', 4), ('uint', 8), ('uint', 12), ('vec2', 16), ('uvec2', 24), ('vec4', 32), ('vec4', 48),
                ('vec4', 64), ('vec4', 80), ('vec4', 96), ('vec4', 112), ('vec4', 128)]:
            raise ValueError('fragment: base/tint/cutoff/mode/UV-offset/padding/UV-matrix/light/fog layout changed')
    textures = document.get('textures', [])
    if stage == 'fragment':
        if len(textures) != 1 or (textures[0].get('set'), textures[0].get('binding')) != (2, 0):
            raise ValueError('fragment: expected one combined texture/sampler in set2 slot0')
    elif textures:
        raise ValueError('vertex: unexpected sampled texture')
    if any(document.get(name) for name in ('ssbos', 'images', 'separate_images', 'separate_samplers', 'push_constants')):
        raise ValueError(f'{stage}: unexpected shader resource')
    return document


def array(name, data):
    lines = [f'_Alignas(4) static const unsigned char {name}[] = {{']
    for offset in range(0, len(data), 16):
        lines.append('    ' + ', '.join(f'0x{byte:02x}' for byte in data[offset:offset+16]) + ',')
    return '\n'.join([*lines, '};'])


def normalized_sources(sources):
    return [(sources / f'scene.{suffix}.hlsl').read_text(encoding='utf-8').replace('\r\n', '\n')
            for suffix in ('vert', 'frag')]


def source_digest(sources):
    return sha(b'\0'.join(source.encode() for source in normalized_sources(sources)))


def dxil_signature(data, tag):
    """Read the pinned DXC's ISG1/OSG1 records, including packed registers."""
    if len(data) < 32 or data[:4] != b'DXBC' or struct.unpack_from('<I', data, 24)[0] != len(data):
        raise ValueError('invalid DXIL container')
    count = struct.unpack_from('<I', data, 28)[0]
    if count > 64 or 32 + count * 4 > len(data):
        raise ValueError('invalid DXIL chunk directory')
    matches = []
    for offset in struct.unpack_from('<' + 'I' * count, data, 32):
        if offset < 32 + count * 4 or offset + 8 > len(data):
            raise ValueError('invalid DXIL chunk offset')
        size = struct.unpack_from('<I', data, offset + 4)[0]
        if offset + 8 + size > len(data):
            raise ValueError('invalid DXIL chunk size')
        if data[offset:offset+4] == tag:
            matches.append(data[offset+8:offset+8+size])
    if len(matches) != 1 or len(matches[0]) < 8:
        raise ValueError('missing or duplicate DXIL signature')
    payload = matches[0]
    count, first = struct.unpack_from('<2I', payload)
    if count > 32 or first != 8 or first + count * 32 > len(payload):
        raise ValueError('invalid DXIL signature records')
    result = []
    for i in range(count):
        stream, name, index, system, component, register, mask, precision = struct.unpack_from('<8I', payload, first + i * 32)
        if stream or name < first + count * 32 or name >= len(payload):
            raise ValueError('invalid DXIL signature semantic')
        end = payload.find(b'\0', name)
        if end < 0:
            raise ValueError('unterminated DXIL signature semantic')
        result.append((payload[name:end].decode('ascii'), index, system, component, register, mask & 255))
    return result


def validate_dxil_stage(data, stage, scene=True):
    vertex_inputs = [('TEXCOORD', 0, 0, 3, 0, 7), ('TEXCOORD', 1, 0, 3, 1, 3), ('TEXCOORD', 2, 0, 3, 2, 15),
                     ('TEXCOORD', 3, 0, 3, 3, 7)]
    # The scene links a lighting/fog TEXCOORD2; the HD2D post pass keeps the original three.
    linked = [('SV_Position', 0, 1, 3, 0, 15), ('TEXCOORD', 0, 0, 3, 1, 3), ('TEXCOORD', 1, 0, 3, 2, 15),
              ('TEXCOORD', 2, 0, 3, 3, 15)][:4 if scene else 3]
    outputs = [('SV_Target', 0, 64, 3, 0, 15)]
    if (dxil_signature(data, b'ISG1') != (vertex_inputs if stage == 'vertex' else linked) or
            dxil_signature(data, b'OSG1') != (linked if stage == 'vertex' else outputs)):
        raise ValueError(f'{stage}: DXIL packed stage signature mismatch')


def verify_sources(sources, header_path):
    """Verify the checked-in association without a shader compiler or network."""
    text = header_path.read_text(encoding='utf-8')
    constants = {
        'SOURCE_SHA256': '"' + source_digest(sources) + '"',
        'DXC': '"' + DXC_VERSION + '"', 'SPIRV_CROSS': '"' + CROSS_VERSION + '"',
        'SPIRV_CROSS_COMMIT': '"' + CROSS_COMMIT + '"',
        'GLES_VERTEX_BLOCK': '"Transform"', 'GLES_FRAGMENT_BLOCK': '"Material"',
        'GLES_SAMPLER': '"scene_texture"', 'GLES_FLIP_Y': '0u',
        'VERTEX_STRIDE': '36u', 'VERTEX_UNIFORM_BYTES': '64u', 'FRAGMENT_UNIFORM_BYTES': '144u',
        'VERTEX_SAMPLERS': '0u', 'FRAGMENT_SAMPLERS': '1u',
        'VERTEX_UNIFORMS': '1u', 'FRAGMENT_UNIFORMS': '1u',
    }
    for name, value in constants.items():
        matches = re.findall(r'^#define PORTABLE_SCENE_SHADER_' + name + r' (.+)$', text, re.MULTILINE)
        if matches != [value]:
            raise ValueError(f'shader header stale or invalid: {name}')
    for stage in ('vertex', 'fragment'):
        for format_name in ('dxil', 'spirv', 'msl', 'glsl'):
            name = f'portable_scene_{stage}_{format_name}'
            arrays = re.findall(r'static const unsigned char ' + name + r'\[\] = \{(.*?)\};', text, re.DOTALL)
            if len(arrays) != 1 or re.sub(r'0x[0-9a-f]{2}|[,\s]', '', arrays[0]):
                raise ValueError(f'shader header invalid array: {name}')
            data = bytes(int(byte, 16) for byte in re.findall(r'0x([0-9a-f]{2})', arrays[0]))
            entry = 'scene_' + stage if format_name == 'msl' else 'main'
            size = len(data) - int(format_name in ('msl', 'glsl'))
            expected = f'{{{name}, {size}u, "{entry}"}};'
            metadata = re.findall(r'static const PortableSceneShaderCode ' + name + r'_code = (.+)', text)
            if metadata != [expected]:
                raise ValueError(f'shader header invalid metadata: {name}')
            if format_name == 'dxil':
                validate_dxil_stage(data, stage)
            elif format_name == 'spirv':
                if len(data) < 20 or len(data) % 4 or data[:4] != struct.pack('<I', 0x07230203):
                    raise ValueError(f'shader header invalid SPIR-V container: {name}')
                words = struct.unpack('<' + 'I' * (len(data) // 4), data)
                offset, entries = 5, []
                while offset < len(words):
                    count, opcode = words[offset] >> 16, words[offset] & 65535
                    if not count or offset + count > len(words):
                        raise ValueError('shader header invalid SPIR-V instruction')
                    if opcode == 15:
                        if count < 4:
                            raise ValueError('shader header invalid SPIR-V entry')
                        label = data[(offset + 3) * 4:(offset + count) * 4].split(b'\0', 1)[0]
                        entries.append((words[offset + 1], label))
                    offset += count
                if entries != [(0 if stage == 'vertex' else 4, b'main')]:
                    raise ValueError('shader header incorrect SPIR-V stage/entry')
            elif format_name == 'msl' and (not data.endswith(b'\0') or entry.encode() + b'(' not in data or
                  b'[[buffer(0)]]' not in data or
                  (stage == 'vertex' and b'[[stage_in]]' not in data) or
                  (stage == 'fragment' and (b'[[texture(0)]]' not in data or b'[[sampler(0)]]' not in data))):
                raise ValueError(f'shader header invalid MSL entry/resources: {name}')
            elif format_name == 'glsl':
                if not data.endswith(b'\0'):
                    raise ValueError('shader header unterminated GLSL')
                validate_glsl(data[:-1].decode('utf-8'), stage)
    return source_digest(sources)


def normalize_glsl(text, stage):
    """Name-link ES300 stages; retain original SDL_GPU coordinate conventions."""
    text = text.replace('\r\n', '\n')
    if stage == 'vertex':
        text = text.replace('uniform type_Camera', 'uniform Transform')
        text = text.replace('#version 300 es\n',
                            '#version 300 es\nprecision highp float;\nprecision highp int;\n', 1)
    else:
        if text.count('precision mediump float;') != 1:
            raise ValueError('fragment: unexpected GLSL default precision')
        text = text.replace('precision mediump float;', 'precision highp float;')
        text = text.replace('uniform type_Material', 'uniform Material')
        text = text.replace('} Material;', '} material;').replace('Material.', 'material.')
    validate_glsl(text, stage)
    return text


def validate_glsl(text, stage):
    if not text.startswith('#version 300 es\n') or any(
            text.count(declaration) != 1 for declaration in
            ('precision highp float;', 'precision highp int;')):
        raise ValueError(f'{stage}: GLSL ES300 requires explicit highp float/int')
    direction = r'\b' + ('out' if stage == 'vertex' else 'in') + r'\s+(?:highp\s+)?'
    if (not re.search(direction + r'vec2\s+scene_uv;', text) or
            not re.search(direction + r'vec4\s+scene_color;', text) or 'void main()' not in text):
        raise ValueError(f'{stage}: GLSL stage interface mismatch')
    block = 'Transform' if stage == 'vertex' else 'Material'
    if not re.search(r'layout\(std140\) uniform ' + block + r'\s*\{', text):
        raise ValueError(f'{stage}: GLSL uniform block mismatch')
    if stage == 'vertex':
        if ('layout(location = 0) in vec3' not in text or
                'layout(location = 1) in vec2' not in text or
                'layout(location = 2) in vec4' not in text or
                'layout(location = 3) in vec3' not in text or
                'gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;' not in text or
                re.search(r'gl_Position\.y\s*=', text)):
            raise ValueError('vertex: GLSL input/depth/orientation contract changed')
    elif ('uniform highp sampler2D scene_texture;' not in text or
          'precision mediump' in text or
          '/ uvec4(255u)' not in text or '65536u' not in text):
        raise ValueError('fragment: GLSL texture/byte tint/cutout contract changed')


def generate(dxc, cross, sources, work):
    stages = []
    source_parts = normalized_sources(sources)
    for stage, suffix, target in (('vertex', 'vert', 'vs_6_0'), ('fragment', 'frag', 'ps_6_0')):
        source = source_parts[0 if stage == 'vertex' else 1]
        canonical = work / f'scene.{suffix}.hlsl'
        with canonical.open('w', encoding='utf-8', newline='\n') as output:
            output.write(source)
        dxil, spirv, msl = (work / f'scene.{suffix}.{extension}' for extension in ('dxil', 'spv', 'msl'))
        common = [dxc, canonical, '-E', 'main', '-T', target, '-O3', '-Qstrip_debug']
        run([*common, '-Fo', dxil])
        run([*common, '-spirv', '-D', 'SPIRV=1', '-fspv-target-env=vulkan1.0', '-Fo', spirv])
        reflect(cross, spirv, stage)
        entry = 'scene_' + stage
        run([cross, spirv, '--msl', '--msl-version', '20100', '--msl-decoration-binding',
             '--rename-entry-point', 'main', entry, 'vert' if stage == 'vertex' else 'frag',
             '--output', msl])
        metal = msl.read_text(encoding='utf-8').replace('\r\n', '\n')
        if f'{entry}(' not in metal or '[[buffer(0)]]' not in metal:
            raise ValueError(f'{stage}: unexpected Metal entrypoint/uniform binding')
        if stage == 'vertex' and '[[stage_in]]' not in metal:
            raise ValueError('vertex: missing Metal stage_in')
        if stage == 'fragment' and ('[[texture(0)]]' not in metal or '[[sampler(0)]]' not in metal):
            raise ValueError('fragment: Metal sampler/texture must be slot0')
        direction = 'out' if stage == 'vertex' else 'in'
        glsl_args = [cross, spirv, '--es', '--version', '300',
                     '--rename-interface-variable', direction, '0', 'scene_uv',
                     '--rename-interface-variable', direction, '1', 'scene_color']
        if stage == 'vertex':
            glsl_args.append('--fixup-clipspace')
        glsl = normalize_glsl(run(glsl_args), stage)
        # Y stays unchanged. SDL_Renderer composition of a direct GL FBO must
        # handle texture orientation and its front-face convention in the backend.
        dxil_bytes, spirv_bytes = dxil.read_bytes(), spirv.read_bytes()
        validate_dxil_stage(dxil_bytes, stage)
        if spirv_bytes[:4] != struct.pack('<I', 0x07230203):
            raise ValueError('invalid generated binary container')
        stages.append((stage, dxil_bytes, spirv_bytes, metal.encode(), glsl.encode(), entry))
    source_hash = source_digest(sources)
    lines = ['/* Generated by build_render_shaders.py; authored shaders only. Do not edit. */',
             '#ifndef PORTABLE_SCENE_SHADERS_H', '#define PORTABLE_SCENE_SHADERS_H', '#include <stddef.h>',
             f'#define PORTABLE_SCENE_SHADER_SOURCE_SHA256 "{source_hash}"',
             f'#define PORTABLE_SCENE_SHADER_DXC "{DXC_VERSION}"',
             f'#define PORTABLE_SCENE_SHADER_SPIRV_CROSS "{CROSS_VERSION}"',
             f'#define PORTABLE_SCENE_SHADER_SPIRV_CROSS_COMMIT "{CROSS_COMMIT}"',
             '#define PORTABLE_SCENE_SHADER_GLES_VERTEX_BLOCK "Transform"',
             '#define PORTABLE_SCENE_SHADER_GLES_FRAGMENT_BLOCK "Material"',
             '#define PORTABLE_SCENE_SHADER_GLES_SAMPLER "scene_texture"',
             '/* GLSL depth is [-w,w]; Y is unchanged. The GL FBO compositor owns Y/culling. */',
             '#define PORTABLE_SCENE_SHADER_GLES_FLIP_Y 0u',
             '#define PORTABLE_SCENE_SHADER_VERTEX_STRIDE 36u',
             '#define PORTABLE_SCENE_SHADER_VERTEX_UNIFORM_BYTES 64u',
             '#define PORTABLE_SCENE_SHADER_FRAGMENT_UNIFORM_BYTES 144u',
             'typedef struct { const unsigned char *bytes; size_t size; const char *entrypoint; } PortableSceneShaderCode;']
    for stage, dxil, spirv, msl, glsl, entry in stages:
        for format_name, data in (('dxil', dxil), ('spirv', spirv), ('msl', msl), ('glsl', glsl)):
            name = f'portable_scene_{stage}_{format_name}'
            lines.append(array(name, data + (b'\0' if format_name in ('msl', 'glsl') else b'')))
            lines.append(f'static const PortableSceneShaderCode {name}_code = '
                         f'{{{name}, {len(data)}u, "{entry if format_name == "msl" else "main"}"}};')
        lines.append(f'#define PORTABLE_SCENE_SHADER_{stage.upper()}_SAMPLERS {int(stage == "fragment")}u')
        lines.append(f'#define PORTABLE_SCENE_SHADER_{stage.upper()}_UNIFORMS 1u')
    lines.extend(['#endif', ''])
    return '\n'.join(lines)



UI_MEMBERS = [('quad', 0), ('rectangle', 16), ('shape', 32), ('effect', 48),
              ('paint_axis', 64), ('paint_positions', 80), ('paint_colors', 96),
              ('source', 160), ('auxiliary', 176), ('edges', 192)]


def ui_source(sources):
    return (sources / 'ui.frag.hlsl').read_text(encoding='utf-8').replace('\r\n', '\n')


def ui_reflect(document):
    ubos = document.get('ubos', [])
    if len(ubos) != 1 or tuple(ubos[0].get(key) for key in ('set', 'binding', 'block_size')) != (3, 0, 2752):
        raise ValueError('UI: expected one 2752-byte uniform at set3 slot0')
    members = document['types'][ubos[0]['type']]['members']
    if [(m.get('name'), m.get('offset')) for m in members] != UI_MEMBERS or any(
            m.get('type') != 'vec4' for m in members):
        raise ValueError('UI: uniform member layout changed')
    for index, count in ((6, 4), (9, 160)):
        if members[index].get('array') != [count] or members[index].get('array_stride') != 16:
            raise ValueError('UI: uniform array layout changed')
    if sorted((x.get('location'), x.get('type')) for x in document.get('inputs', [])) != [(0, 'vec4'), (1, 'vec2')]:
        raise ValueError('UI: expected color/UV inputs at locations0/1')
    textures = document.get('textures', [])
    if len(textures) != 1 or tuple(textures[0].get(key) for key in ('set', 'binding')) != (2, 0):
        raise ValueError('UI: expected one combined set2 slot0 sampler')
    if any(document.get(key) for key in ('ssbos', 'images', 'separate_images', 'separate_samplers', 'push_constants')):
        raise ValueError('UI: unexpected resource')


def ui_dxil(data):
    expected = [('TEXCOORD', 0, 0, 3, 0, 15), ('TEXCOORD', 1, 0, 3, 1, 3)]
    if dxil_signature(data, b'ISG1') != expected or dxil_signature(data, b'OSG1') != [('SV_Target', 0, 64, 3, 0, 15)]:
        raise ValueError('UI: SDL packed TEXCOORD0/TEXCOORD1 signature mismatch')


def ui_array(text, format_name):
    name = 'portable_ui_fragment_' + format_name
    matches = re.findall(r'static const unsigned char ' + name + r'\[\] = \{(.*?)\};', text, re.DOTALL)
    if len(matches) != 1 or re.sub(r'0x[0-9a-f]{2}|[,\s]', '', matches[0]):
        raise ValueError('UI: invalid array ' + name)
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', matches[0]))
    entry = 'ui_fragment' if format_name == 'msl' else 'main'
    size = len(data) - int(format_name in ('msl', 'glsl'))
    metadata = f'static const PortableUiShaderCode {name}_code = {{{name}, {size}u, "{entry}"}};'
    if metadata not in text:
        raise ValueError('UI: invalid entry/size metadata')
    return data


def ui_verify(sources, header):
    text = header.read_text(encoding='utf-8')
    digest = sha(ui_source(sources).encode())
    for name, value in [('SOURCE_SHA256', '"' + digest + '"'), ('DXC', '"' + DXC_VERSION + '"'),
                        ('SPIRV_CROSS', '"' + CROSS_VERSION + '"'),
                        ('SPIRV_CROSS_COMMIT', '"' + CROSS_COMMIT + '"'),
                        ('UNIFORM_BYTES', '2752u'), ('SAMPLERS', '1u'), ('UNIFORMS', '1u')]:
        if re.findall(r'^#define PORTABLE_UI_SHADER_' + name + r' (.+)$', text, re.MULTILINE) != [value]:
            raise ValueError('UI: stale or invalid ' + name)
    ui_dxil(ui_array(text, 'dxil'))
    spirv = ui_array(text, 'spirv')
    if len(spirv) < 20 or len(spirv) % 4 or spirv[:4] != struct.pack('<I', 0x07230203):
        raise ValueError('UI: invalid SPIR-V')
    for fmt in ('msl', 'glsl'):
        data = ui_array(text, fmt)
        if not data.endswith(b'\0'):
            raise ValueError('UI: unterminated shader text')
        if fmt == 'msl' and any(token not in data for token in
                (b'ui_fragment(', b'[[buffer(0)]]', b'[[texture(0)]]', b'[[sampler(0)]]')):
            raise ValueError('UI: invalid Metal resources')
        if fmt == 'glsl' and any(token not in data for token in
                (b'#version 300 es', b'precision highp float;', b'precision highp int;', b'uniform highp sampler2D ui_texture;')):
            raise ValueError('UI: invalid GLES resources/precision')
    return digest


def ui_generate(dxc, cross, sources, work):
    canonical = work / 'ui.frag.hlsl'
    canonical.write_text(ui_source(sources), encoding='utf-8', newline='\n')
    dxil, spirv = work / 'ui.frag.dxil', work / 'ui.frag.spv'
    common = [dxc, canonical, '-E', 'main', '-T', 'ps_6_0', '-O3', '-Qstrip_debug']
    run([*common, '-Fo', dxil])
    run([*common, '-spirv', '-D', 'SPIRV=1', '-fspv-target-env=vulkan1.0', '-Fo', spirv])
    ui_reflect(json.loads(run([cross, spirv, '--reflect'])))
    ui_dxil(dxil.read_bytes())
    metal = run([cross, spirv, '--msl', '--msl-version', '20100', '--msl-decoration-binding',
                 '--rename-entry-point', 'main', 'ui_fragment', 'frag']).replace('\r\n', '\n')
    glsl = run([cross, spirv, '--es', '--version', '300']).replace('\r\n', '\n')
    glsl = glsl.replace('precision mediump float;', 'precision highp float;')
    lines = ['/* Generated by build_render_shaders.py --ui; authored UI only. */',
             '#ifndef PORTABLE_UI_SHADERS_H', '#define PORTABLE_UI_SHADERS_H', '#include <stddef.h>',
             f'#define PORTABLE_UI_SHADER_SOURCE_SHA256 "{sha(ui_source(sources).encode())}"',
             f'#define PORTABLE_UI_SHADER_DXC "{DXC_VERSION}"',
             f'#define PORTABLE_UI_SHADER_SPIRV_CROSS "{CROSS_VERSION}"',
             f'#define PORTABLE_UI_SHADER_SPIRV_CROSS_COMMIT "{CROSS_COMMIT}"',
             '#define PORTABLE_UI_SHADER_UNIFORM_BYTES 2752u',
             '#define PORTABLE_UI_SHADER_SAMPLERS 1u', '#define PORTABLE_UI_SHADER_UNIFORMS 1u',
             'typedef struct { const unsigned char *bytes; size_t size; const char *entrypoint; } PortableUiShaderCode;']
    for fmt, data in [('dxil', dxil.read_bytes()), ('spirv', spirv.read_bytes()),
                      ('msl', metal.encode()), ('glsl', glsl.encode())]:
        name = 'portable_ui_fragment_' + fmt
        entry = 'ui_fragment' if fmt == 'msl' else 'main'
        lines.append(array(name, data + (b'\0' if fmt in ('msl', 'glsl') else b'')))
        lines.append(f'static const PortableUiShaderCode {name}_code = {{{name}, {len(data)}u, "{entry}"}};')
    return '\n'.join([*lines, '#endif', ''])


def ui_self_test(work, sources, header):
    import copy
    import tempfile
    import unittest

    class UiToolTests(unittest.TestCase):
        def test_uniform_resources_and_arrays(self):
            members = [{'name': name, 'offset': offset, 'type': 'vec4'}
                       for name, offset in UI_MEMBERS]
            for index, count in ((6, 4), (9, 160)):
                members[index].update(array=[count], array_stride=16)
            valid = {'ubos': [{'set': 3, 'binding': 0, 'block_size': 2752, 'type': 'ui'}],
                     'types': {'ui': {'members': members}},
                     'inputs': [{'location': 0, 'type': 'vec4'}, {'location': 1, 'type': 'vec2'}],
                     'textures': [{'set': 2, 'binding': 0}]}
            ui_reflect(valid)
            mutations = [lambda d: d['ubos'][0].update(block_size=2736),
                         lambda d: d['ubos'][0].update(binding=1),
                         lambda d: d['types']['ui']['members'][9].update(array=[159]),
                         lambda d: d['types']['ui']['members'][6].update(array_stride=32),
                         lambda d: d['types']['ui']['members'][7].update(offset=144),
                         lambda d: d['inputs'][0].update(location=1),
                         lambda d: d['textures'][0].update(set=1),
                         lambda d: d.update(separate_samplers=[{'binding': 0}])]
            for mutate in mutations:
                invalid = copy.deepcopy(valid)
                mutate(invalid)
                with self.assertRaises(ValueError):
                    ui_reflect(invalid)

        def test_freshness_and_metadata(self):
            ui_verify(sources, header)
            original = header.read_text(encoding='utf-8')
            with tempfile.TemporaryDirectory(dir=work) as directory:
                edited = Path(directory) / 'ui_shaders.h'
                for text in (original.replace('UNIFORM_BYTES 2752u', 'UNIFORM_BYTES 2736u'),
                             original.replace(DXC_VERSION, 'wrong'),
                             original.replace(sha(ui_source(sources).encode()), '0'*64),
                             original.replace('"ui_fragment"', '"wrong"')):
                    edited.write_text(text, encoding='utf-8')
                    with self.assertRaises(ValueError):
                        ui_verify(sources, edited)

        def test_packed_signature(self):
            data = ui_array(header.read_text(encoding='utf-8'), 'dxil')
            ui_dxil(data)
            count = struct.unpack_from('<I', data, 28)[0]
            offsets = struct.unpack_from('<' + 'I'*count, data, 32)
            for offset in offsets:
                if data[offset:offset+4] != b'ISG1':
                    continue
                for relative, value in ((32+20, 0), (32+8, 0)):
                    changed = bytearray(data)
                    # Reject both wrong packed register and the old TEXCOORD0
                    # UV semantic. SDL's generated vertex uses TEXCOORD1 UV.
                    struct.pack_into('<I', changed, offset+8+8+relative, value)
                    with self.assertRaises(ValueError):
                        ui_dxil(changed)
                changed = bytearray(data)
                name = struct.unpack_from('<I', data, offset+8+8+4)[0]
                changed[offset+8+name:offset+8+name+8] = b'COLOR\0\0\0'
                with self.assertRaises(ValueError):
                    ui_dxil(changed)

    work.mkdir(parents=True, exist_ok=True)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(UiToolTests))
    if not result.wasSuccessful():
        raise SystemExit(1)


def self_test(work, sources, header_path):
    """Host-independent safety tests; compiler regeneration uses --check."""
    import copy
    import io
    import tempfile
    import unittest
    from unittest.mock import patch

    class ToolTests(unittest.TestCase):
        def test_glsl_linkage_precision_and_depth(self):
            header = header_path.read_text(encoding='utf-8')
            for stage in ('vertex', 'fragment'):
                body = re.search(r'static const unsigned char portable_scene_' + stage +
                                 r'_glsl\[\] = \{(.*?)\};', header, re.DOTALL)[1]
                data = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', body))
                text = data[:-1].decode('utf-8')
                validate_glsl(text, stage)
                for edited in (text.replace('scene_uv;', 'unlinked_uv;'),
                               text.replace('precision highp float;', 'precision mediump float;'),
                               text.replace('precision highp int;', 'precision mediump int;'),
                               text.replace('uniform Transform', 'uniform Wrong') if stage == 'vertex' else
                               text.replace('uniform Material', 'uniform Wrong')):
                    with self.assertRaises(ValueError):
                        validate_glsl(edited, stage)
                if stage == 'vertex':
                    with self.assertRaisesRegex(ValueError, 'depth/orientation'):
                        validate_glsl(text.replace('2.0 * gl_Position.z - gl_Position.w', 'gl_Position.z'), stage)
                else:
                    with self.assertRaisesRegex(ValueError, 'texture/byte'):
                        validate_glsl(text.replace('sampler2D', 'samplerCube'), stage)

        def test_packed_dxil_stage_linkage(self):
            header = header_path.read_text(encoding='utf-8')
            for stage in ('vertex', 'fragment'):
                body = re.search(r'static const unsigned char portable_scene_' + stage +
                                 r'_dxil\[\] = \{(.*?)\};', header, re.DOTALL)[1]
                data = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', body))
                validate_dxil_stage(data, stage)
                changed = bytearray(data)
                count = struct.unpack_from('<I', data, 28)[0]
                for offset in struct.unpack_from('<' + 'I' * count, data, 32):
                    if data[offset:offset+4] == b'ISG1':
                        # Reproduce the original FS bug: its UV lived in reg0,
                        # while the VS wrote UV to reg1 after SV_Position.
                        struct.pack_into('<I', changed, offset + 8 + 8 + 32 + 20, 0)
                with self.assertRaisesRegex(ValueError, 'packed stage signature'):
                    validate_dxil_stage(changed, stage)
                with self.assertRaises(ValueError):
                    validate_dxil_stage(data[:31], stage)

        def test_sources_and_header_freshness(self):
            with tempfile.TemporaryDirectory(dir=work) as directory:
                root = Path(directory)
                local_sources = root / 'sources'
                local_sources.mkdir()
                for suffix, source in zip(('vert', 'frag'), normalized_sources(sources)):
                    with (local_sources / f'scene.{suffix}.hlsl').open('w', encoding='utf-8', newline='') as output:
                        output.write(source.replace('\n', '\r\n'))
                header = root / 'scene_shaders.h'
                original = header_path.read_text(encoding='utf-8')
                header.write_text(original, encoding='utf-8')
                # Checkout line-ending policy must not change the source identity.
                verify_sources(local_sources, header)
                for suffix in ('vert', 'frag'):
                    path = local_sources / f'scene.{suffix}.hlsl'
                    unchanged = path.read_bytes()
                    path.write_bytes(unchanged + b'// edited source\n')
                    with self.assertRaisesRegex(ValueError, 'SOURCE_SHA256'):
                        verify_sources(local_sources, header)
                    path.write_bytes(unchanged)
                for edited in (
                    original.replace(source_digest(sources), '0' * 64),
                    original.replace('VERTEX_STRIDE 36u', 'VERTEX_STRIDE 20u'),
                    original.replace(DXC_VERSION, 'unverified-compiler'),
                    original.replace('"scene_fragment"', '"wrong_entry"'),
                    original.replace('0x44, 0x58, 0x42, 0x43', '0x00, 0x58, 0x42, 0x43', 1),
                ):
                    header.write_text(edited, encoding='utf-8')
                    with self.assertRaises(ValueError):
                        verify_sources(local_sources, header)
                header.unlink()
                with self.assertRaises(OSError):
                    verify_sources(local_sources, header)

        def test_zip_and_tar_escape_rejected(self):
            with tempfile.TemporaryDirectory(dir=work) as directory:
                root = Path(directory)
                for name in ('../escape', '/absolute'):
                    archive = root / 'bad.zip'
                    with zipfile.ZipFile(archive, 'w') as bundle:
                        bundle.writestr(name, b'bad')
                    with self.assertRaisesRegex(ValueError, 'escapes cache'):
                        extract(archive, root / 'out')
                archive = root / 'bad.tar.gz'
                with tarfile.open(archive, 'w:gz') as bundle:
                    entry = tarfile.TarInfo('link')
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = '../escape'
                    bundle.addfile(entry)
                with self.assertRaisesRegex(ValueError, 'link escapes cache'):
                    extract(archive, root / 'out')
                with tarfile.open(archive, 'w:gz') as bundle:
                    entry = tarfile.TarInfo('device')
                    entry.type = tarfile.FIFOTYPE
                    bundle.addfile(entry)
                with self.assertRaisesRegex(ValueError, 'special file'):
                    extract(archive, root / 'out')
                good = root / 'good.tar.gz'
                with tarfile.open(good, 'w:gz') as bundle:
                    entry = tarfile.TarInfo('source.txt')
                    entry.size = 4
                    bundle.addfile(entry, io.BytesIO(b'good'))
                extract(good, root / 'good')
                verify_archive(good, root / 'good')
                (root / 'good/source.txt').write_bytes(b'edit')
                with self.assertRaisesRegex(ValueError, 'edited tool cache'):
                    verify_archive(good, root / 'good')

        def test_reflected_layout_and_resources(self):
            vertex = {'ubos': [{'set': 1, 'binding': 0, 'block_size': 64, 'type': 'camera'}],
                      'types': {'camera': {'members': [{'type': 'mat4', 'offset': 0,
                          'row_major': True, 'matrix_stride': 16}]}},
                      'inputs': [{'location': 0, 'type': 'vec3'}, {'location': 1, 'type': 'vec2'},
                                 {'location': 2, 'type': 'vec4'}, {'location': 3, 'type': 'vec3'}]}
            fragment = {'ubos': [{'set': 3, 'binding': 0, 'block_size': 144, 'type': 'material'}],
                        'types': {'material': {'members': [
                            {'type': kind, 'offset': offset} for kind, offset in
                            [('uint', 0), ('uint', 4), ('uint', 8), ('uint', 12), ('vec2', 16), ('uvec2', 24),
                             ('vec4', 32), ('vec4', 48), ('vec4', 64), ('vec4', 80), ('vec4', 96),
                             ('vec4', 112), ('vec4', 128)]]}},
                        'textures': [{'set': 2, 'binding': 0}]}
            for stage, valid in [('vertex', vertex), ('fragment', fragment)]:
                with patch(__name__ + '.run', return_value=json.dumps(valid)):
                    reflect('unused', 'unused', stage)
                bad_size = copy.deepcopy(valid)
                bad_size['ubos'][0]['block_size'] -= 4
                bad_binding = copy.deepcopy(valid)
                bad_binding['ubos'][0]['binding'] = 1
                bad_resource = dict(valid, separate_samplers=[{'binding': 0}])
                for invalid in (bad_size, bad_binding, bad_resource):
                    with patch(__name__ + '.run', return_value=json.dumps(invalid)):
                        with self.assertRaises(ValueError):
                            reflect('unused', 'unused', stage)
            invalid = copy.deepcopy(vertex)
            invalid['types']['camera']['members'][0]['row_major'] = False
            with patch(__name__ + '.run', return_value=json.dumps(invalid)):
                with self.assertRaisesRegex(ValueError, 'matrix storage'):
                    reflect('unused', 'unused', 'vertex')

        def test_archive_and_tool_hash_rejection(self):
            with tempfile.TemporaryDirectory(dir=work) as directory:
                root = Path(directory)
                cached = root / 'archive'
                cached.write_bytes(b'corrupt')
                with self.assertRaisesRegex(ValueError, 'SHA256 mismatch'):
                    download('unused', cached, sha(b'expected'))
                tool = root / 'tool'
                tool.write_bytes(b'edited')
                lock = {'dxc': DXC_VERSION, 'spirv_cross': CROSS_VERSION,
                        'spirv_cross_commit': CROSS_COMMIT,
                        'dxc_archive_sha256': DXC_ARCHIVES['Linux'][1],
                        'spirv_cross_archive_sha256': CROSS_SHA,
                        'files': {'tool': sha(b'original')}}
                path = root / 'tools.json'
                path.write_text(json.dumps(lock))
                with self.assertRaisesRegex(ValueError, 'tool lock mismatch'):
                    validate_tools(tool, tool, path)

    work.mkdir(parents=True, exist_ok=True)
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ToolTests))
    if not result.wasSuccessful():
        raise SystemExit(1)


def post_verify(sources, header):
    digest = sha((sources / 'hd2d.frag.hlsl').read_text(encoding='utf-8').encode())
    text = header.read_text(encoding='utf-8')
    if f'PORTABLE_HD2D_SOURCE_SHA256 "{digest}"' not in text:
        raise ValueError('HD2D shader source hash differs')
    for fmt in ('dxil', 'spirv', 'msl'):
        body = re.search(r'portable_hd2d_fragment_' + fmt + r'\[\] = \{(.*?)\};', text, re.DOTALL)
        if not body: raise ValueError('missing HD2D shader ' + fmt)
        data = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})', body[1]))
        if fmt == 'dxil': validate_dxil_stage(data, 'fragment', scene=False)
        elif fmt == 'spirv' and data[:4] != b'\x03\x02\x23\x07': raise ValueError('invalid HD2D SPIRV')
        elif fmt == 'msl' and any(v not in data for v in (b'hd2d_fragment(', b'[[texture(0)]]', b'[[texture(1)]]', b'[[buffer(0)]]')):
            raise ValueError('invalid HD2D Metal bindings')
    return digest


def post_generate(dxc, cross, sources, work):
    source = sources / 'hd2d.frag.hlsl'
    dxil, spirv = work / 'hd2d.frag.dxil', work / 'hd2d.frag.spv'
    common = [dxc, source, '-E', 'main', '-T', 'ps_6_0', '-O3', '-Qstrip_debug']
    run([*common, '-Fo', dxil])
    run([*common, '-spirv', '-D', 'SPIRV=1', '-fspv-target-env=vulkan1.0', '-Fo', spirv])
    reflection = json.loads(run([cross, spirv, '--reflect']))
    ubos = reflection.get('ubos', [])
    textures = reflection.get('textures', [])
    if len(ubos)!=1 or ubos[0]['block_size']!=64 or ubos[0]['set']!=3 or ubos[0]['binding']!=0 or sorted((t['set'],t['binding']) for t in textures)!=[(2,0),(2,1)]:
        raise ValueError('HD2D shader resource layout mismatch')
    metal = run([cross, spirv, '--msl', '--msl-version', '20100', '--msl-decoration-binding',
                 '--rename-entry-point', 'main', 'hd2d_fragment', 'frag']).replace('\r\n', '\n')
    lines = ['/* Generated by build_render_shaders.py --post. */',
             '#ifndef PORTABLE_HD2D_SHADERS_H', '#define PORTABLE_HD2D_SHADERS_H',
             '#include "scene_shaders.h"',
             f'#define PORTABLE_HD2D_SOURCE_SHA256 "{sha(source.read_text(encoding="utf-8").encode())}"']
    for fmt, data in [('dxil',dxil.read_bytes()), ('spirv',spirv.read_bytes()), ('msl',metal.encode())]:
        name = 'portable_hd2d_fragment_' + fmt
        lines.append(array(name, data + (b'\0' if fmt=='msl' else b'')))
        entry = 'hd2d_fragment' if fmt=='msl' else 'main'
        lines.append(f'static const PortableSceneShaderCode {name}_code = {{{name}, {len(data)}u, "{entry}"}};')
    return '\n'.join([*lines, '#endif', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--post', action='store_true', help='build HD-2D world finish shader')
    parser.add_argument('--ui', action='store_true', help='build the SDL custom UI fragment shader')
    parser.add_argument('--verify-sources', action='store_true', help='verify source freshness and generated metadata without compilers')
    parser.add_argument('--self-test', action='store_true', help='run compiler-independent tooling unit tests')
    parser.add_argument('--bootstrap', type=Path)
    parser.add_argument('--check', action='store_true', help='regenerate and compare without replacing output')
    parser.add_argument('--dxc', type=Path)
    parser.add_argument('--spirv-cross', type=Path)
    parser.add_argument('--tool-lock', type=Path)
    parser.add_argument('--sources', type=Path, default=ROOT/'portable/platform/shaders')
    parser.add_argument('--work', type=Path, default=ROOT/'.mksc-tools/portable/runs/t2/shaders')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output is None:
        args.output = ROOT / 'portable/platform/shaders' / ('hd2d_shaders.h' if args.post else 'ui_shaders.h' if args.ui else 'scene_shaders.h')
    if args.ui and args.work == ROOT/'.mksc-tools/portable/runs/t2/shaders':
        args.work = ROOT/'.mksc-tools/portable/runs/u7/shaders'
    if args.verify_sources:
        digest = (post_verify if args.post else ui_verify if args.ui else verify_sources)(args.sources, args.output)
        print(f'{"UI" if args.ui else "Scene"} shader sources: PASS sha256={digest}')
        return
    if args.self_test:
        (ui_self_test if args.ui else self_test)(args.work, args.sources, args.output)
        return
    if args.bootstrap:
        args.dxc, args.spirv_cross, args.tool_lock = bootstrap(args.bootstrap)
    if not all((args.dxc, args.spirv_cross, args.tool_lock)):
        parser.error('use --bootstrap or supply --dxc, --spirv-cross and --tool-lock')
    validate_tools(args.dxc, args.spirv_cross, args.tool_lock)
    args.work.mkdir(parents=True, exist_ok=True)
    header = (post_generate if args.post else ui_generate if args.ui else generate)(args.dxc.resolve(), args.spirv_cross.resolve(), args.sources.resolve(), args.work.resolve())
    if args.check:
        if args.output.read_bytes() != header.encode():
            raise ValueError(f'{args.output}: generated shader header differs')
        print(f'{"UI" if args.ui else "Scene"} shaders reproducible: PASS sha256={sha(header.encode())}')
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Complete validation precedes replacement; never leave a partial shader header.
    temporary = args.output.with_name(args.output.name+'.tmp')
    with temporary.open('w', encoding='utf-8', newline='\n') as output:
        output.write(header)
    if args.post:
        post_verify(args.sources, temporary)
    if args.ui:
        ui_verify(args.sources, temporary)
    os.replace(temporary, args.output)
    print(f'{"UI" if args.ui else "Scene"} shaders: PASS {args.output} sha256={sha(header.encode())}')


if __name__ == '__main__':
    main()

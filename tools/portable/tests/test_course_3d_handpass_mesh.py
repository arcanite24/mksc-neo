"""ROM-free static mesh addition/material/resource integration tests."""
from copy import deepcopy
from dataclasses import FrozenInstanceError
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import course_3d_format as wire
from course_3d_gltf import Scene,AssetIds,argb_png,write_json
import course_3d_handpass_mesh as mesh


class MeshAdditions(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[3]/'.mksc-tools/portable/runs/t5'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ids = AssetIds()
        self.scene = Scene(self.root/'output')
        self.scene.material()
        self.scene.mesh('Base',[(0,0,0,0,1,0,0,0),(4,0,0,0,1,0,1,0),(0,0,-4,0,1,0,0,1)],[(0,1,2,0)])
        self.document = dict(version=1,id=70000,scene='scene.gltf',render_nodes=['Base'],
            collision_nodes=[dict(node='Base',surface=0)],surfaces=[dict(type='road')],checkpoints=[],
            meta=dict(name='Mesh addition test',modes=['battle'],laps=0,cup_id=1,cup_order=0),
            routes=[dict(kind='cpu',closed=True,nodes=[dict(position=[0,0,0],radius=1,speed=1,successors=[0])])],
            grid=[dict(position=[0,0,0])],respawns=[dict(position=[0,0,0],checkpoint=None,radius=1)])

    def addition(self,textured=True,mirrored=True):
        scene = Scene(self.root/'addition')
        if textured:
            image = scene.texture(2,1,(0xffff1234,0x00123456),AssetIds())
            scene.material(0x987654ef,image,repeat=(True,False),cutout=True)
            material = scene.document['materials'][0]
            material.update(emissiveFactor=[.1,.2,.3],doubleSided=True,
                            extensions={'KHR_materials_unlit':{}},alphaCutoff=.25)
            material['pbrMetallicRoughness'].update(metallicFactor=.25,roughnessFactor=.75,
                            metallicRoughnessTexture={'index':0})
            material['normalTexture'] = {'index':0,'scale':1}
            material['emissiveTexture'] = {'index':0}
        else:
            scene.material()
            scene.document['materials'][0]['extensions'] = {'KHR_materials_unlit':{}}
        # Roof and a wall: rigid anchoring must preserve the clearance between them.
        vertices = [(0,3,0,0,1,0,0,0),(4,3,0,0,1,0,2,0),(0,3,-4,0,1,0,0,1),
                    (0,0,0,1,0,0,0,0),(0,3,0,1,0,0,1,0),(0,0,-4,1,0,0,0,1)]
        scene.mesh('Tunnel',vertices,[(0,1,2,0),(3,4,5,0)])
        if mirrored:
            scene.document['nodes'][0].update(translation=[5,0,2],scale=[-1,1,1])
        # Exercise area-weighted normals recovered from transformed geometry.
        del scene.document['meshes'][0]['primitives'][0]['attributes']['NORMAL']
        scene.save()
        return dict(scene='addition/scene.gltf',render_nodes=['Tunnel'],
                    collision_nodes=[dict(node='Tunnel',surface=0,respawn=0)],
                    surfaces=[dict(type='wall',restitution=.25)])

    def compile(self):
        self.scene.save()
        return wire.compile(self.document,self.scene.directory)

    def test_textured_transformed_mirrored_roundtrip(self):
        row = self.addition()
        before = deepcopy(row)
        additions = mesh.load(self.root,[row])
        with self.assertRaises(FrozenInstanceError):
            additions[0].anchor = (0,0)
        old_binary = bytes(self.scene.binary)
        old_material = deepcopy(self.scene.document['materials'][0])
        mesh.apply(additions,self.scene,self.document,self.ids)
        self.assertEqual(row,before)
        self.assertTrue(self.scene.binary.startswith(old_binary))
        self.assertEqual(self.scene.document['materials'][0],old_material)
        self.assertEqual(self.document['surfaces'],[dict(type='road'),dict(restitution=.25,type='wall')])
        self.assertEqual(self.document['collision_nodes'][1]['surface'],1)
        self.assertEqual(self.document['collision_nodes'][1]['respawn'],0)
        material = self.scene.document['materials'][1]
        self.assertEqual(material['alphaMode'],'MASK')
        self.assertEqual(material['extensions'],{'KHR_materials_unlit':{}})
        self.assertEqual(material['pbrMetallicRoughness']['metallicFactor'],.25)
        texture = self.scene.document['textures'][material['normalTexture']['index']]
        sampler = self.scene.document['samplers'][texture['sampler']]
        self.assertEqual((sampler['wrapS'],sampler['wrapT']),(10497,33071))
        sections = self.compile()
        self.assertEqual(sum(meta['kind']==101 for meta,_ in sections),1)
        gltf = wire.Gltf(self.scene.directory.resolve(),'scene.gltf')
        points,normals,uvs,faces,_ = next(gltf.primitives(self.document['render_nodes'][1]))
        self.assertEqual(points[0],[5.,3.,2.])
        self.assertEqual(faces[0],[0,1,2])
        self.assertIn((2.,0.),uvs)
        self.assertTrue(all(any(n) for n in normals))

    def test_rigid_anchor_flat_unlit_tunnel(self):
        row = self.addition(False,False)
        row['ground_anchor'] = [2,-2]
        samples = []
        field = SimpleNamespace(sample_world=lambda x,z: samples.append((x,z)) or 1.25)
        mesh.apply(mesh.load(self.root,[row]),self.scene,self.document,self.ids,field)
        self.compile()
        gltf = wire.Gltf(self.scene.directory.resolve(),'scene.gltf')
        points,*_ = next(gltf.primitives(self.document['render_nodes'][1]))
        self.assertEqual(samples,[(2,-2)])
        self.assertEqual(min(p[1] for p in points),1.25)
        self.assertEqual(max(p[1] for p in points),4.25)
        self.assertEqual(len(self.scene.document['images']),0)

    def test_embedded_glb_png(self):
        row = self.addition()
        folder = self.root/'addition'
        document = json.loads((folder/'scene.gltf').read_text())
        binary = bytearray((folder/'scene.bin').read_bytes())
        binary.extend(bytes((-len(binary))%4))
        start = len(binary)
        png = argb_png(1,1,(0xff1256ab,))
        binary.extend(png)
        document['bufferViews'].append(dict(buffer=0,byteOffset=start,byteLength=len(png)))
        document['images'] = [dict(bufferView=len(document['bufferViews'])-1,mimeType='image/png')]
        document['buffers'] = [dict(byteLength=len(binary))]
        raw = json.dumps(document).encode()
        raw += b' '*((-len(raw))%4)
        binary.extend(bytes((-len(binary))%4))
        glb = struct.pack('<3I',0x46546c67,2,12+8+len(raw)+8+len(binary))
        glb += struct.pack('<2I',len(raw),0x4e4f534a)+raw+struct.pack('<2I',len(binary),0x004e4942)+binary
        (folder/'scene.glb').write_bytes(glb)
        row['scene'] = 'addition/scene.glb'
        additions = mesh.load(self.root,[row])
        self.assertEqual(additions[0].images,((1,1,(0xff1256ab,)),))
        mesh.apply(additions,self.scene,self.document,self.ids)
        self.compile()

    def test_references_and_apply_rollback(self):
        row = self.addition()
        row['collision_nodes'][0]['respawn'] = 1
        additions = mesh.load(self.root,[row])
        state = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
        files = tuple(self.scene.directory.iterdir())
        with self.assertRaisesRegex(mesh.MeshError,'respawn'):
            mesh.apply(additions,self.scene,self.document,self.ids)
        self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),state)
        self.assertEqual(tuple(self.scene.directory.iterdir()),files)
        row['collision_nodes'][0]['surface'] = 1
        with self.assertRaisesRegex(mesh.MeshError,'surface'):
            mesh.load(self.root,[row])

    def test_invalid_material_surface_caps_and_paths(self):
        row = self.addition()
        original = deepcopy(row)
        row['surfaces'][0]['friction'] = -1
        with self.assertRaisesRegex(mesh.MeshError,'surface'):
            mesh.load(self.root,[row])
        row = original
        path = self.root/'addition/scene.gltf'
        document = json.loads(path.read_text())
        document['materials'][0]['occlusionTexture'] = {'index':0}
        write_json(path,document)
        with self.assertRaisesRegex(mesh.MeshError,'material'):
            mesh.load(self.root,[row])
        self.addition()
        for name in ('MAX_VERTICES','MAX_TRIANGLES','MAX_BYTES'):
            with patch.object(mesh,name,1),self.assertRaises(mesh.MeshError):
                mesh.load(self.root,[row])
        row['scene'] = '../escape.gltf'
        with self.assertRaises(mesh.MeshError):
            mesh.load(self.root,[row])

    def test_composed_cap_and_bad_anchor_rollback(self):
        row = self.addition()
        prepared = mesh.load(self.root,[row])
        self.scene.document['materials'] *= 256
        before = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
        with self.assertRaisesRegex(mesh.MeshError,'cap'):
            mesh.apply(prepared,self.scene,self.document,self.ids)
        self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),before)
        self.scene.document['materials'] = self.scene.document['materials'][:1]
        row['ground_anchor'] = [0,0]
        before = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
        field = SimpleNamespace(sample_world=lambda x,z: float('nan'))
        with self.assertRaises(mesh.MeshError):
            mesh.apply(mesh.load(self.root,[row]),self.scene,self.document,self.ids,field)
        self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),before)

    def test_image_symlink_escape_and_surface_reference(self):
        row = self.addition()
        folder = self.root/'addition'
        image_path = next(folder.glob('*.png'))
        with tempfile.TemporaryDirectory(dir=self.root.parent) as outside:
            target = Path(outside)/'image.png'
            target.write_bytes(image_path.read_bytes())
            image_path.unlink()
            image_path.symlink_to(target)
            with self.assertRaises(mesh.MeshError):
                mesh.load(self.root,[row])
        image_path.unlink()
        row = self.addition(False)
        row['surfaces'][0]['properties'] = [dict(key='test:owner',type='object',value=0)]
        prepared = mesh.load(self.root,[row])
        before = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
        with self.assertRaisesRegex(mesh.MeshError,'reference'):
            mesh.apply(prepared,self.scene,self.document,self.ids)
        self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),before)

    def test_source_budget_refuses_buffers_and_png_before_read(self):
        row = self.addition()
        folder = self.root/'addition'
        scene = folder/'scene.gltf'
        buffer = folder/'scene.bin'
        image = next(folder.glob('*.png'))
        read_bytes = Path.read_bytes
        for blocked,limit in ((buffer,scene.stat().st_size+buffer.stat().st_size-1),
                              (image,scene.stat().st_size+buffer.stat().st_size+image.stat().st_size-1)):
            reads = []
            def checked_read(path):
                reads.append(path)
                if path.resolve() == blocked.resolve():
                    self.fail(f'over-budget resource read: {path}')
                return read_bytes(path)
            with self.subTest(resource=blocked.name),patch.object(mesh,'MAX_BYTES',limit),\
                    patch.object(Path,'read_bytes',checked_read),self.assertRaisesRegex(mesh.MeshError,'resource limit'):
                mesh.load(self.root,[row])
            self.assertNotIn(blocked,reads)

    def test_second_image_publication_failure_restores_files_and_owners(self):
        row = self.addition()
        folder = self.root/'addition'
        document = json.loads((folder/'scene.gltf').read_text())
        (folder/'other.png').write_bytes(argb_png(1,1,(0xffabcdef,)))
        document['images'].append(dict(uri='other.png'))
        write_json(folder/'scene.gltf',document)
        prepared = mesh.load(self.root,[row])
        targets = []
        ids = deepcopy(self.ids)
        for width,height,pixels in prepared[0].images:
            payload = struct.pack('<2I',width,height)+struct.pack(f'<{len(pixels)}I',*pixels)
            targets.append(self.scene.directory/f'texture-{ids.intern(101,payload)}.png')
        targets.sort()
        original_replace = Path.replace
        for old_bytes in (None,b'prior destination content'):
            with self.subTest(existing=old_bytes is not None):
                for target in targets:
                    target.unlink(missing_ok=True)
                if old_bytes is not None:
                    targets[0].write_bytes(old_bytes)
                before = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
                files = {p.name:p.read_bytes() for p in self.scene.directory.iterdir() if p.is_file()}
                calls = []
                def failing_replace(path,target):
                    if path.suffix == '.png':
                        calls.append(path)
                        if len(calls) == 2:
                            raise OSError('second PNG publication failed')
                    return original_replace(path,target)
                with patch.object(Path,'replace',failing_replace),self.assertRaisesRegex(mesh.MeshError,'second PNG'):
                    mesh.apply(prepared,self.scene,self.document,self.ids)
                self.assertEqual(len(calls),2)
                self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),before)
                self.assertEqual({p.name:p.read_bytes() for p in self.scene.directory.iterdir() if p.is_file()},files)
                self.assertFalse(any(p.is_dir() for p in self.scene.directory.iterdir()))

    def test_absent_noop_and_collision_only(self):
        before = deepcopy((self.scene.__dict__,self.document,self.ids.__dict__))
        self.assertEqual(mesh.load(self.root,[]),())
        mesh.apply((),self.scene,self.document,self.ids)
        self.assertEqual((self.scene.__dict__,self.document,self.ids.__dict__),before)
        row = self.addition(False)
        row['render_nodes'] = []
        mesh.apply(mesh.load(self.root,[row]),self.scene,self.document,self.ids)
        self.assertEqual(self.document['render_nodes'],['Base'])
        self.compile()


if __name__ == '__main__':
    unittest.main()

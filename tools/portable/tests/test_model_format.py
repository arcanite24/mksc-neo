"""Model registry replacement and opt-in overlay authoring contracts."""
from copy import deepcopy
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import model_format


class ModelBindingModes(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[3]/'.mksc-tools/portable/check/model-format'
        scratch.mkdir(parents=True,exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.document = dict(version=1,materials=[dict(id=9,argb='#FF123456')],
            meshes=[dict(id=7,materials=[9],vertices=[[0,0,0,0,0],[1,0,0,1,0],[0,1,0,0,1]],
                         triangles=[[0,1,2,0]])],
            bindings=[dict(kind='driver',target=16,mesh=7)])

    def test_default_and_explicit_replace_have_identical_legacy_bytes(self):
        default = model_format.compile(self.document,self.root)
        explicit = model_format.compile(dict(self.document,binding_mode='replace'),self.root)
        self.assertEqual(default,explicit)
        self.assertEqual([(m['kind'],m['content_id'],m['version'],m['flags'])for m,_ in default],
                         [(97,9,1,0),(96,7,1,0),(98,1,1,0)])
        self.assertEqual(default[-1][1][:8],b'BND1'+struct.pack('<I',1))

    def test_overlay_only_changes_registry_section_version(self):
        before = deepcopy(self.document)
        replace = model_format.compile(self.document,self.root)
        overlay = model_format.compile(dict(self.document,binding_mode='overlay'),self.root)
        self.assertEqual(self.document,before)
        self.assertEqual(overlay[:-1],replace[:-1])
        self.assertEqual(overlay[-1][1],replace[-1][1])
        self.assertEqual(overlay[-1][0],dict(replace[-1][0],version=2))

    def test_independent_disjoint_authoring_keeps_own_bindings(self):
        first = dict(self.document,binding_mode='overlay')
        second = deepcopy(first)
        second['materials'][0]['id'] = 10
        second['meshes'][0].update(id=8,materials=[10])
        second['bindings'] = [dict(kind='scenery',target=128,art_bank=4,mesh=8)]
        a = model_format.compile(first,self.root)
        b = model_format.compile(second,self.root)
        self.assertEqual(struct.unpack_from('<5I',a[-1][1],8),(1,16,0,0,7))
        self.assertEqual(struct.unpack_from('<5I',b[-1][1],8),(2,128,4,0,8))
        self.assertTrue(all(m['version']==(2 if m['kind']==98 else 1)for result in(a,b)for m,_ in result))

    def test_empty_bindings_omit_registry_in_both_modes(self):
        default = model_format.compile(dict(self.document,bindings=[]),self.root)
        self.assertEqual({m['kind']for m,_ in default},{96,97})
        for mode in ('replace','overlay'):
            self.assertEqual(model_format.compile(dict(self.document,bindings=[],binding_mode=mode),self.root),default)

    def test_invalid_modes_and_unknown_fields_fail(self):
        for value in ('','merge','OVERLAY',None,True,1,[],{}):
            with self.subTest(mode=value),self.assertRaisesRegex(model_format.ModelError,'binding_mode'):
                model_format.compile(dict(self.document,binding_mode=value),self.root)
        with self.assertRaisesRegex(model_format.ModelError,'unknown fields'):
            model_format.compile(dict(self.document,binding_modes='overlay'),self.root)
        duplicate = dict(self.document,binding_mode='overlay',bindings=self.document['bindings']*2)
        with self.assertRaisesRegex(model_format.ModelError,'duplicate binding'):
            model_format.compile(duplicate,self.root)

    def test_duplicate_authoring_keys_still_fail_before_compilation(self):
        path = self.root/'models.json'
        path.write_text('{"version":1,"binding_mode":"replace","binding_mode":"overlay"}')
        with self.assertRaisesRegex(model_format.ModelError,'duplicate JSON'):
            model_format.load(path)


if __name__ == '__main__':
    unittest.main()

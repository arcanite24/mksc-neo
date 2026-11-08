"""Generate ROM-free native JSON normalization and composition fixtures."""
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_asset_pack
import game_data_mod
import struct

ROOT = Path(__file__).resolve().parents[3]

def fixture(root):
    payload = bytearray(struct.pack('<9I', 0x31544f4d, 1024, 64, 8, 8, 8, 256, 10, 16))
    payload += bytes(4096 + 128 + 16 + 16 + 16 + 512 + 20 + 32 + 32768 + 416 + 52)
    for engine in range(3):
        payload += bytes(128) + bytes([100 + engine * 10]) * 160 + bytes(128)
        payload += struct.pack('<i16h', (900 + engine * 100) * 65536, *([-1] * 10 + [1000] * 6))
    material = struct.pack('<II', 0x3154414d, 256) + bytes(3836)
    catalog = struct.pack('<II', 0x31544143, 8)
    for index, identity in enumerate((24, 25, 29, 27)):
        catalog += struct.pack('<IBBHI', identity, 0, index, 20 + index, 4 + index)
    for index,identity in enumerate((32,37,38,31)):
        catalog += struct.pack('<IBBHI',identity,1,index,24+index,12+index)
    entries = []
    for kind, version, data in ((13, 6, payload), (6, 16, material), (37, 2, catalog)):
        path = f'{kind}.bin'
        (root / path).write_bytes(data)
        entries.append({'kind': kind, 'content_id': 1, 'version': version, 'flags': 2, 'path': path})
    (root / '13-2.bin').write_bytes(payload)
    entries.append({'kind':13,'content_id':2,'version':6,'flags':2,'path':'13-2.bin'})
    (root / '6-27.bin').write_bytes(material)
    entries.append({'kind':6,'content_id':27,'version':16,'flags':2,'path':'6-27.bin'})
    for identity in range(1, 12):
        name = f'58-{identity}.bin'
        (root / name).write_bytes(struct.pack('<II4H', 0x31575047, 16, 4096, 10, 0, 255) + bytes(56))
        entries.append({'kind': 58, 'content_id': identity, 'version': 1, 'flags': 2, 'path': name})
    (root / '59.bin').write_bytes(struct.pack('<II8B',0x31545047,8,9,6,3,1,0,0,0,0))
    entries.append({'kind':59,'content_id':1,'version':1,'flags':2,'path':'59.bin'})
    (root/'60.bin').write_bytes(struct.pack('<7I',0x3153554d,5,50,30,600,50,0x07e00000))
    entries.append({'kind':60,'content_id':1,'version':1,'flags':3,'path':'60.bin'})
    (root/'61.bin').write_bytes(struct.pack('<5I',0x31544153,3,372,210,360))
    entries.append({'kind':61,'content_id':1,'version':1,'flags':3,'path':'61.bin'})
    (root/'62.bin').write_bytes(struct.pack('<9I',0x314f4f42,7,360,360,64,1536,1024,135168,12288))
    entries.append({'kind':62,'content_id':1,'version':1,'flags':3,'path':'62.bin'})
    (root/'63.bin').write_bytes(struct.pack('<10I',0x31485347,8,768,100,8,7,16,244,53248,6656))
    entries.append({'kind':63,'content_id':1,'version':1,'flags':3,'path':'63.bin'})
    (root/'64.bin').write_bytes(struct.pack('<18I',0x31485352,16,768,116,8,48,16,244,108,116,126,4,16,96,2700,30,53248,6656))
    entries.append({'kind':64,'content_id':1,'version':1,'flags':3,'path':'64.bin'})
    (root/'67.bin').write_bytes(struct.pack('<5I',0x3142524f,3,14,1280,8))
    entries.append({'kind':67,'content_id':1,'version':1,'flags':3,'path':'67.bin'})
    (root/'65.bin').write_bytes(struct.pack('<11I',0x314e4142,9,524288,32,106496,6656,8,16,244,39936,6656))
    entries.append({'kind':65,'content_id':1,'version':1,'flags':3,'path':'65.bin'})
    extra=struct.pack('<II',0x31544143,8)
    for index in range(8):
        extra+=struct.pack('<IBBHI',64+index,index//4,index%4,40+index,32+index)
    (root/'37-2.bin').write_bytes(extra)
    entries.append({'kind':37,'content_id':2,'version':2,'flags':2,'path':'37-2.bin'})
    for base in (0x23300, 0x23400, 0x23500):
        for slot in (*range(8),*range(20,28)):
            name = f'{base + slot}.bin'
            (root / name).write_bytes(bytes([slot + 1]))
            entries.append({'kind': 15, 'content_id': base + slot, 'version': 1, 'path': name})
    text_image=struct.pack('<8s4H2h3I',b'MKSCIMG1',32,1,64,16,-32,-8,0,32,32+64*16*4)+bytes([255])*(64*16*4)
    (root/'text.bin').write_bytes(text_image)
    entries.append({'kind':15,'content_id':0x21100,'version':1,'flags':2,'path':'text.bin'})
    (root / 'base.json').write_text(json.dumps({'sections': entries}))
    build_asset_pack.build_pack(root / 'base.json', root / 'base.pack')
    return payload, material, catalog

def main(root):
    root.mkdir(parents=True, exist_ok=True)
    fixture(root)
    manifest = ROOT / 'mods/examples/data-tuning/mod.json'
    source = ROOT / 'mods/examples/data-tuning/data.json'
    game_data_mod.build(root / 'base.pack', source, root / 'compiled', manifest)
    cross=ROOT/'mods/examples/data-tuning/cross-cup.json'
    game_data_mod.build(root/'base.pack',cross,root/'cross-compiled',manifest)
    game_data_mod.build_runtime(cross,root/'cross-runtime',manifest)
    text=root/'text.json'
    text.write_text(json.dumps({'text':[{'id':0x21100,'value':'NEO GP','color_rgb':0xffdd00}]}))
    game_data_mod.build(root/'base.pack',text,root/'text-compiled',manifest)
    game_data_mod.build_runtime(text,root/'text-runtime',manifest)
    strings=ROOT/'mods/examples/data-tuning/strings.json'
    game_data_mod.build(root/'base.pack',strings,root/'strings-compiled',manifest)
    game_data_mod.build_runtime(strings,root/'strings-runtime',manifest)
    orbit=ROOT/'mods/examples/data-tuning/shell-orbits.json'
    game_data_mod.build(root/'base.pack',orbit,root/'orbit-compiled',manifest)
    game_data_mod.build_runtime(orbit,root/'orbit-runtime',manifest)
    status=root/'status.json'
    status.write_text(json.dumps({'item_status_rules':{'id':1,'star_player_ticks':90,'star_cpu_ticks':60,'lightning_ticks':120}}))
    game_data_mod.build(root/'base.pack',status,root/'status-compiled',manifest)
    game_data_mod.build_runtime(status,root/'status-runtime',manifest)
    boo=root/'boo.json'
    boo.write_text(json.dumps({'boo_rules':{'id':1,'invisibility_ticks':90,'lock_ticks':60,'orbit_ticks':16,'orbit_step':3072,'bob_step':2048,'repel_velocity_q16':65536,'repel_gravity_q16':16384}}))
    game_data_mod.build(root/'base.pack',boo,root/'boo-compiled',manifest)
    game_data_mod.build_runtime(boo,root/'boo-runtime',manifest)
    green=root/'green.json'
    green.write_text(json.dumps({'green_shell_rules':{'id':1,'launch_speed_bonus':384,'max_speed':60,'collision_grace_ticks':4,'wall_contact_budget':2,'release_lock_ticks':8,'cpu_hold_ticks':60,'destruction_velocity_q16':98304,'destruction_gravity_q16':12288}}))
    game_data_mod.build(root/'base.pack',green,root/'green-compiled',manifest)
    game_data_mod.build_runtime(green,root/'green-runtime',manifest)
    red=root/'red.json'
    red.write_text(json.dumps({'red_shell_rules': {'id': 1, 'launch_speed_bonus': 384, 'max_launch_speed': 60, 'collision_grace_ticks': 4, 'route_budget': 6, 'release_lock_ticks': 8, 'cpu_hold_ticks': 60, 'flight_speed_50': 80, 'flight_speed_100': 90, 'flight_speed_150': 100, 'acceleration': 8, 'deceleration': 24, 'trap_arm_ticks': 24, 'trap_lifetime_ticks': 180, 'trap_lock_ticks': 10, 'destruction_velocity_q16': 98304, 'destruction_gravity_q16': 12288}}))
    game_data_mod.build(root/'base.pack',red,root/'red-compiled',manifest)
    game_data_mod.build_runtime(red,root/'red-runtime',manifest)
    banana=root/'banana.json'
    banana.write_text(json.dumps({'banana_rules': {'id': 1, 'throw_speed_q16': 262144, 'cpu_flight_ticks': 24, 'throw_velocity_q16': 155648, 'throw_gravity_q16': 12288, 'collision_grace_ticks': 4, 'release_lock_ticks': 8, 'cpu_hold_ticks': 60, 'destruction_velocity_q16': 65536, 'destruction_gravity_q16': 16384}}))
    game_data_mod.build(root/'base.pack',banana,root/'banana-compiled',manifest)
    game_data_mod.build_runtime(banana,root/'banana-runtime',manifest)
    physics = {'id':1, 'auto_acceleration':[-32768,32767]*32,
        'brake':list(range(-11,-19,-1)), 'brake_accelerate':list(range(7,-1,-1)),
        'forward_response':list(range(-100,-108,-1)), 'local_acceleration':[-32768,-1,0,32767]*64,
        'steering_delta':list(range(-20,-10)), 'steering_response':list(range(-31,-15)),
        'forward_target_response':list(range(-1,-9,-1)),
        'time_trial':{'base_speed_q16':1,'terrain_limits':[0,1,100,500,1000,32767]},
        'engines':[{'id':0,'base_speed_q16':2016*65536,'terrain_limits':[1,2,3,4,5,6],
            'cpu_acceleration':list(range(128)), 'cpu_target_speeds':list(range(255,95,-1)),
            'cpu_heading_steps':[0,255]*20},
            {'id':2,'base_speed_q16':1500*65536,'terrain_limits':list(range(1000,1006))}]}
    (root / 'physics.json').write_text(json.dumps({'kart_physics':[physics]}))
    game_data_mod.build(root/'base.pack',root/'physics.json',root/'physics-compiled',manifest)
    game_data_mod.build_runtime(root/'physics.json',root/'physics-runtime',manifest)
    for name, data in [('a', source.read_text()), ('b', '{"mushroom_rules":{"id":1,"golden_actor_ticks":240},"gp_rules":{"id":1,"retry_credits":3},"engine_speed_percent":{"150":50},"kart_physics":[{"id":1,"engines":[{"id":2,"terrain_limits":[1000,1001,1002,1003,1004,1005]}]}],"cpu_items":[{"course":27,"engine":2,"variant":0,"driver_class":0,"items":[0,0,0,0,0,0,0,0]}],"gp_items":[{"id":1,"weights":[{"item":4098,"weight":1}]}],"cups":[{"catalog":1,"cup":0,"courses":[24,25,29,27]}]}'),
                       ('bad', '{"engine_speed_percent":{"150":200}}')]:
        output = root / 'mods' / name
        output.mkdir(parents=True, exist_ok=True)
        (output / 'data.json').write_text(data)
        (output / 'mod.json').write_text(json.dumps({'id':name,'name':name,'version':'1.0.0','api_version':1,'pack':'content.pack'}))
        (output / 'pack.json').write_text(json.dumps({'sections':[{'kind':57,'content_id':1,'version':1,'flags':3,'path':'data.json'}]}))
        build_asset_pack.build_pack(output / 'pack.json', output / 'content.pack')

if __name__ == '__main__':
    main(Path(sys.argv[1]))

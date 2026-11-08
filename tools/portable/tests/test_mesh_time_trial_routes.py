"""Drive authored mesh TT/VS through the shared shell and verify results and saves."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys
import time
import tempfile
import wave

from test_local_video_routes import LocalVideoRoute, check, read_ppm


# Every contact family except piranhas and boos, whose routes meet them on the line.
SEEK_FAMILIES=('crabs','snowmen','penguins','mice','seagulls','lava_bubbles','thwomps','cannonballs',
    'rocks','teepees','falling_stars','thunderclouds','common_props','mechakoopas','kamek','pier_boos')


def converted_gp_strategy(inputs=None, hold_second=False):
    """Validate ordinary per-race use/reserve inputs, retaining the old alias.

    >>> converted_gp_strategy(), converted_gp_strategy(hold_second=True)
    ('GGGG', 'GJGG')
    >>> [converted_gp_strategy(v) for v in ('GGGJ', 'JGGG', 'GJGG', 'KKKK', 'GJKJ')]
    ['GGGJ', 'JGGG', 'GJGG', 'KKKK', 'GJKJ']
    >>> for invalid in ('GGG', 'GGGGG', 'GGGX', 'gggg', 'JJJJ', ''):
    ...     try:
    ...         converted_gp_strategy(invalid)
    ...     except argparse.ArgumentTypeError:
    ...         pass
    ...     else:
    ...         raise AssertionError(invalid)
    >>> converted_gp_strategy('GGGG', True)
    Traceback (most recent call last):
    ...
    argparse.ArgumentTypeError: choose explicit GP inputs or the hold-second alias
    """
    if inputs is not None and hold_second:
        raise argparse.ArgumentTypeError('choose explicit GP inputs or the hold-second alias')
    value=inputs if inputs is not None else 'GJGG' if hold_second else 'GGGG'
    if not isinstance(value,str) or re.fullmatch('[GJK]{4}',value) is None or not any(token in value for token in 'GK'):
        raise argparse.ArgumentTypeError('converted GP inputs require four G/J/K characters and at least one G or K')
    return value


def pier_boo_states(log,course):
    pattern=(rf'3D source pier boo state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'state=(?P<state>\d+) phase=(?P<phase>\d+) flags=(?P<flags>\d+) '
        r'affine_x=(?P<affine_x>-?\d+) affine_y=(?P<affine_y>-?\d+) vz=(?P<vz>-?\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [{k:int(v,16) if k=='checksum' else int(v) for k,v in m.groupdict().items()}
            for m in re.finditer(pattern,log)]


def source_haunt_states(log,course):
    fields=('tick','slot','source','target','occupied','visible','phase','timer','radius','angle',
            'bob','frame','affine_y','x','y','z','vx','vy','vz','origin_x','origin_y','origin_z','lock')
    pattern=rf'3D source haunt state: course={course} '+''.join(
        name+rf'=(?P<{name}>-?\d+) ' for name in fields)+r'checksum=(?P<checksum>[0-9a-f]{16})'
    return [{k:int(v,16) if k=='checksum' else int(v) for k,v in m.groupdict().items()}
            for m in re.finditer(pattern,log)]


def pier_boo_contacts(log):
    return [(int(c),int(t),int(mask,16),*map(int,(touch,guard,assigned,clock,start,release,spin)))
        for c,t,mask,touch,guard,assigned,clock,start,release,spin in re.findall(
        r'3D source pier boo contact: course=(\d+) tick=(\d+) actors=([0-9a-f]{16}) '
        r'touch=(\d+) protected=(\d+) assigned=(\d+) clock_mask=(\d+) '
        r'music_start=(\d+) music_release=(\d+) spin=(\d+)',log)]


def verify_pier_boo_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,
                                coins_enabled=True):
    starts=re.findall(r'3D source pier boos start: course=(\d+) pier_boos=(\d+) '
                      r'profile=(\d+) revision=(\d+) seed=(\d+) rng=(\d+)',log)
    for course in courses:
        count=(definitions or {}).get(course,{}).get('count',1)
        matching=[tuple(map(int,row[1:])) for row in starts if int(row[0])==course]
        check(matching and all(n==count and profile>0 and revision>0 and seed<=2147483647
                              for n,profile,revision,seed,rng in matching),
              'Pier Boo omitted explicit source rules/visual seed')
        for n,profile,revision,seed,rng in matching:
            expected=seed
            for _ in range(n):expected=(1103515245*expected+12345)&0xffffffff
            check(rng==expected,'Pier Boo visual LCG did not draw exactly once per placement')
        rows=pier_boo_states(log,course)
        check(rows and {r['index'] for r in rows if r['tick']==1}==set(range(count)),
              'Pier Boo omitted initial source prop snapshots')
        check(all(r['index']<count and r['state'] in (0,5,6,8) and r['phase']<=65535
                  and 1<=r['affine_x']<=256 and r['affine_y'] in (*range(1,257),-256)
                  and r['checksum'] for r in rows),'Pier Boo invalid source prop state')
        for index in range(count):
            actor=[r for r in rows if r['index']==index]
            initial=[r['checksum'] for r in actor if r['tick']==1]
            check(len(set(initial))==1,'Pier Boo Retry did not restore visual RNG/state')
            for before,after in zip(actor,actor[1:]):
                if after['tick']==before['tick']+1 and before['state']==after['state']==0:
                    check(after['phase']==(before['phase']+1024)%65536
                          and after['x']==before['x'] and after['y']==before['y']
                          and 425984<=after['z']<=622592,
                          'Pier Boo changed source64 bob/anchor')
    if not require_contact:return
    contacts=[r for r in pier_boo_contacts(log) if r[0] in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'Pier Boo omitted actual primary protected/ordinary contact')
    if protected:
        complete=False
        for course,tick,mask,touch,guard,*_ in primary:
            check(not touch&1,'Star Pier Boo also spawned ordinary haunt')
            for index in range(64):
                if not mask&(1<<index):continue
                rows={r['tick']:r for r in pier_boo_states(log,course)
                      if r['index']==index and tick<=r['tick']<=tick+128}
                initial=rows.get(tick)
                if not initial or initial['state']!=5:continue
                height=initial['z'];velocity=786432
                for n in range(1,129):
                    row=rows.get(tick+n)
                    if row is None:break
                    height+=velocity;hidden=height<0
                    if not hidden:velocity-=16384
                    check(row['z']==height and row['vz']==velocity and row['x']==initial['x']
                          and row['y']==initial['y'] and row['phase']==initial['phase']
                          and row['affine_y']==-256 and not row['flags']&2
                          and row['state']==(8 if hidden else 6) and bool(row['flags']&1)==(not hidden),
                          'Pier Boo changed source+12/.25 flight or frozen XY')
                    if hidden:complete=True;break
        check(complete,'Pier Boo omitted complete protected prop flight/hide')
        song=160
    else:
        song=380;complete=False
        check(any(r[9]==0 for r in primary),'Pier Boo never demonstrated spin-free ordinary touch')
        for course,tick,mask,touch,guard,*_ in primary:
            check(any(r['tick']==tick and mask&(1<<r['index']) and r['state']==8 and not r['flags']&3
                      for r in pier_boo_states(log,course)),
                  'ordinary Pier Boo did not vanish its source prop')
            rows=source_haunt_states(log,course)
            spawned=[r for r in rows if r['tick']==tick and r['target']==0 and r['occupied']
                     and r['phase']==1 and r['timer']==63 and r['radius']==1]
            for first in spawned:
                slot=[r for r in rows if r['slot']==first['slot'] and tick<=r['tick']]
                check(all(not r['occupied'] or (r['source']<64 and r['target']<8 and r['frame']<2
                          and r['phase']<=7 and r['affine_y'] in (256,-256)) for r in slot),
                      'source haunt lost bounded origin/target/frame binding')
                assigned=next((r for r in slot if r['tick']==tick+63 and r['phase']==2),None)
                if assigned is None:continue
                check(assigned['timer']==0 and assigned['radius']==64 and assigned['lock']==360,
                      'source haunt assigned lock before/after its64-update approach')
                clocks=re.findall(rf'3D source haunt clock: course={course} tick=(\d+) racer=0 '
                    r'before=(\d+) after=(\d+) owned=(\d+) assigned=(\d+) music=(-?\d+)',log)
                check(any(int(t)==tick+63 and int(after)==360 and int(assigned_mask)&1
                          for t,before,after,owned,assigned_mask,music in clocks),
                      'source haunt omitted fresh typed360 lock assignment')
                check(any(r[0]==course and r[1]==tick+63 and r[7]&1 for r in contacts),
                      'source haunt omitted approach-to-orbit music request')
                departed=any(r['phase'] in (4,5,6) for r in slot)
                released=any(r[0]==course and r[1]>tick+63 and r[8]&1 for r in contacts)
                freed=any(r['tick']>tick+63 and not r['occupied'] for r in slot)
                complete|=departed and released and freed
        check(complete,'Pier Boo omitted genuine primary approach/orbit/release/free lifecycle')
        losses=[(int(lost),int(causes)) for course,lost,causes in re.findall(
            r'3D coin state: course=(\d+) tick=\d+ racer=0 before=\d+ after=\d+ '
            r'lost=(\d+) causes=(\d+)',log) if int(course) in courses]
        if coins_enabled:
            check(any(causes&16 for _,causes in losses),
                  'source haunt did not reach an accepted typed coin pulse')
            if '3D Time Trial start:' in log:
                check(any(lost>0 and causes&16 for lost,causes in losses),
                      'Time Trial source haunt never drained a positive coin count')
        else:
            check(not any(lost or causes&16 for lost,causes in losses),
                  'source haunt drained coins in a mode where its coin profile is disabled')
    check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                        r'player=\d+ volume=256',log) for course in courses),
          'Pier Boo omitted actual human contact cue')


def verify_pier_boo_pictures(frames,protected=False):
    colors={bytes((238,224,255)),bytes((243,234,255))};seen=set();counts=[]
    for frame in frames:
        width,height,pixels=read_ppm(frame);count=0
        for y in range(30,height-20):
            for x in range(45,width-20):
                color=pixels[(y*width+x)*3:(y*width+x)*3+3]
                if color in colors:seen.add(color);count+=1
        counts.append(count)
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2
          and (protected or len(seen)==2),'captures omitted anchored Pier Boo prop/haunt artwork')


def kamek_states(log,course):
    pattern=(rf'3D source kamek state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'state=(?P<state>\d+) timer=(?P<timer>-?\d+) phase=(?P<phase>\d+) '
        r'heading=(?P<heading>\d+) facing=(?P<facing>\d+) flags=(?P<flags>\d+) '
        r'affine_x=(?P<affine_x>-?\d+) affine_y=(?P<affine_y>-?\d+) '
        r'vx=(?P<vx>-?\d+) vz=(?P<vz>-?\d+) x=(?P<x>-?\d+) y=(?P<y>-?\d+) '
        r'z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [{k:int(v,16) if k=='checksum' else int(v) for k,v in m.groupdict().items()}
            for m in re.finditer(pattern,log)]


def kamek_contacts(log):
    return [(int(c),int(t),int(actors,16),int(touch),int(protection),int(spin))
            for c,t,actors,touch,protection,spin in re.findall(
        r'3D source kamek contact: course=(\d+) tick=(\d+) actors=([0-9a-f]{16}) '
        r'touch=(\d+) protected=(\d+) spin=(\d+)',log)]


def verify_kamek_actor_turns(log,courses,definitions=None,require_contact=True,
                            protected=False,require_patrol=True):
    starts=re.findall(r'3D source kamek start: course=(\d+) kamek=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    states_seen=set()
    for course in courses:
        definition=(definitions or {}).get(course,{})
        count=definition.get('count',1)
        check(any(int(c)==course and int(n)==count and int(p)>0 and int(r)>0
                  for c,n,p,r in starts),'Kamek omitted explicit source rules/placement')
        rows=kamek_states(log,course)
        check(rows and {r['index'] for r in rows if r['tick']==1}==set(range(count)),
              'Kamek omitted initial immutable source state')
        check(all(r['index']<count and r['state'] in (0,1,2,3,4,5,6,7,8)
                  and 0<=r['timer']<=128 and r['phase']<=65535 and r['facing']<=1
                  and abs(r['vx'])<=32768 and r['vx']%2048==0
                  and 1<=r['affine_x']<=256 and r['affine_y'] in (*range(1,257),-256)
                  and r['checksum'] for r in rows),'Kamek invalid source patrol/flight state')
        for index in range(count):
            actor=[r for r in rows if r['index']==index]
            first=[r['checksum'] for r in actor if r['tick']==1]
            check(len(set(first))==1,'Kamek Retry changed initial source world')
            for row in actor:
                if row['state']<5:
                    states_seen.add(row['state'])
                    check(688128<=row['z']<=884736,'Kamek changed twelve-unit source bob amplitude')
            for before,after in zip(actor,actor[1:]):
                if after['tick']!=before['tick']+1 or before['state']>=5 or after['state']>=5:continue
                check(after['x']==before['x']+before['vx'] and after['y']==before['y']
                      and after['phase']==(before['phase']+1024)%65536,
                      'Kamek source pre-acceleration integration/phase changed')
                if before['state'] in (1,3):
                    accelerating=before['state']==1;timer=127
                else:
                    accelerating=before['state']==2;timer=before['timer']-1
                target=32768 if accelerating else -32768
                velocity=before['vx'] if before['vx']==target else before['vx']+(2048 if accelerating else -2048)
                expected_state=(3 if accelerating else 1) if timer<=0 else (2 if accelerating else 4)
                check(after['vx']==velocity and after['timer']==(128 if timer<=0 else timer)
                      and after['state']==expected_state,
                      'Kamek changed64/128 source leg or2048 acceleration')
        views=re.findall(rf'3D source kamek view: course={course} actor=(\d+) '
                         r'view=(\d+) side=(\d+) model=(\d+)',log)
        check(views and all(int(i)<count and int(v)<4 and int(side)<2 and int(model)>0
                            for i,v,side,model in views),'Kamek omitted per-camera anchored cells')
        if require_patrol:
            check(len({int(model) for _,_,_,model in views})>=2,
                  'Kamek did not present both directional source cells')
    if require_patrol:check({1,2,3,4}<=states_seen,'Kamek omitted complete source reversal legs')
    if not require_contact:return
    primary=[r for r in kamek_contacts(log) if r[0] in courses and (r[4] if protected else r[3])&1]
    check(primary,'Kamek omitted genuine primary protected/ordinary contact')
    if protected:
        complete=False
        for course,tick,mask,touch,guard,spin in primary:
            check(not touch&1,'protected Kamek contact also published ordinary touch')
            for index in range(64):
                if not mask&(1<<index):continue
                rows={r['tick']:r for r in kamek_states(log,course)
                      if r['index']==index and tick<=r['tick']<=tick+64}
                initial=rows.get(tick)
                if not initial or initial['state']!=5:continue
                height=initial['z'];velocity=196608
                for n in range(1,65):
                    row=rows.get(tick+n)
                    if row is None:break
                    height+=velocity
                    hidden=height<0
                    if not hidden:velocity-=16384
                    check(row['z']==height and row['vz']==velocity and row['x']==initial['x']
                          and row['y']==initial['y'] and row['phase']==initial['phase']
                          and row['affine_y']==-256 and not row['flags']&2
                          and row['state']==(8 if hidden else 6)
                          and bool(row['flags']&1)==(not hidden),
                          'Kamek changed source+3/.25 ballistic flight/inversion/XY freeze')
                    if hidden:complete=True;break
        check(complete,'Kamek omitted full protected ballistic descent/hide')
        check(any(re.search(rf'3D item cue: course={course} tick=\d+ song=160 '
                            r'player=\d+ volume=256',log) for course in courses),
              'protected Kamek omitted actual human160 cue')
    else:
        check(any(r[5]==0 for r in primary),'Kamek never demonstrated a harmless spin-free touch')
        for course,tick,mask,touch,guard,spin in primary:
            check(not re.search(rf'3D item cue: course={course} tick={tick+180} song=169 ',log),
                  'ordinary Kamek contact invented a bump cue')


def verify_kamek_pictures(frames):
    colors={bytes((75,148,214)),bytes((134,99,191))}
    counts=[]
    for frame in frames:
        width,height,pixels=read_ppm(frame)
        counts.append(sum(pixels[(y*width+x)*3:(y*width+x)*3+3] in colors
                          for y in range(30,height-20) for x in range(45,width-20)))
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2,
          'captures omitted original anchored Kamek artwork')


def mechakoopa_states(log,course):
    pattern=(rf'3D source mechakoopa state: course={course} tick=(?P<tick>\d+) '
        r'index=(?P<index>\d+) variant=(?P<variant>\d+) state=(?P<state>\d+) '
        r'timer=(?P<timer>-?\d+) cycle=(?P<cycle>\d+) stride_phase=(?P<phase>\d+) '
        r'stride_ticks=(?P<stride>\d+) heading=(?P<heading>\d+) facing=(?P<facing>\d+) '
        r'flags=(?P<flags>\d+) affine_x=(?P<affine_x>-?\d+) affine_y=(?P<affine_y>-?\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [{key:int(value,16) if key=='checksum' else int(value)
             for key,value in match.groupdict().items()} for match in re.finditer(pattern,log)]


def verify_mechakoopa_actor_turns(log,courses,definitions=None,require_contact=True,
                                 protected=False,sine=None,require_cycle=True):
    starts=re.findall(r'3D source mechakoopas start: course=(\d+) mechakoopas=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    complete_cycle=False
    for course in courses:
        definition=(definitions or {}).get(course,{})
        count=definition.get('count',2)
        variants=definition.get('variants',[1,0])
        check(any(int(c)==course and int(n)==count and int(profile)>0 and int(revision)>0
                  for c,n,profile,revision in starts),'Mechakoopas omitted explicit source rules/placements')
        rows=mechakoopa_states(log,course)
        check(rows and {r['index'] for r in rows if r['tick']==1}==set(range(count)),
              'Mechakoopas omitted initial source snapshots')
        check(all(r['index']<count and r['variant']==variants[r['index']] and r['state']<=1
                  and -1<=r['timer']<=32 and r['cycle']<352 and r['phase']<=1
                  and r['stride']<10 and r['facing']<=1 and 1<=r['affine_x']<=256
                  and r['affine_y'] in (*range(1,257),-256) and r['checksum'] for r in rows),
              'Mechakoopa source patrol, variant, affine or flight state invalid')
        # Compare adjacent accepted turns, excluding Retry and frozen Star flight.
        for index in range(count):
            actor=[r for r in rows if r['index']==index]
            for before,after in zip(actor,actor[1:]):
                if after['tick']!=before['tick']+1 or before['state'] or after['state']:continue
                walking=before['cycle']<96 or 176<=before['cycle']<272
                velocity=(-32768 if before['variant'] else 32768)*(1 if before['cycle']<176 else -1)
                stride=before['stride']+1
                flip=stride>=(6 if walking else 10)
                check(after['cycle']==(before['cycle']+1)%352
                      and after['x']==before['x']+(velocity if walking else 0)
                      and after['y']==before['y'] and after['z']==before['z']
                      and after['heading']==(16384 if velocity>0 else 49152)
                      and after['facing']==1 and after['stride']==(0 if flip else stride)
                      and after['phase']==before['phase']^int(flip),
                      'Mechakoopa changed source352 patrol or6/10-update stride')
                complete_cycle|=before['cycle']==351 and after['cycle']==0
        first=[r['checksum'] for r in rows if r['index']==0 and r['tick']==1]
        check(len(set(first))==1,'Mechakoopa Retry changed initial world')
        views=re.findall(rf'3D source mechakoopa view: course={course} actor=(\d+) '
                         r'view=(\d+) side=(\d+) model=(\d+)',log)
        check(views and all(int(i)<count and int(v)<4 and int(side)<2 and int(model)>0
                            for i,v,side,model in views),'Mechakoopa omitted bounded per-camera frame selection')
        if require_contact and not protected:
            check(any(len({int(model) for actor,_,_,model in views if int(actor)==index})==4
                      for index in range(count)),
                  'Mechakoopa did not present independently anchored directional stride cells')
    if require_cycle and not protected:
        check(complete_cycle,'Mechakoopa route omitted a complete352-update source patrol')
    if not require_contact:return
    contacts=[tuple(map(int,(c,t)))+(int(mask,16),int(hit),int(guard),int(strong),int(spin))
              for c,t,mask,hit,guard,strong,spin in re.findall(
        r'3D source mechakoopa contact: course=(\d+) tick=(\d+) actors=([0-9a-f]{16}) '
        r'hit=(\d+) protected=(\d+) strong=(\d+) spin=(\d+)',log) if int(c) in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'Mechakoopa omitted genuine primary protected/ordinary contact')
    if protected:
        check(sine is not None and len(sine)==1024,'Mechakoopa flight requires authored MSA sine')
        complete=False
        for course,tick,mask,hit,guard,strong,spin in primary:
            check(not hit&1 and not strong&1,'Star Mechakoopa contact caused normal damage')
            for index in range(64):
                if not mask&(1<<index):continue
                flight={r['tick']:r for r in mechakoopa_states(log,course)
                        if r['index']==index and tick<=r['tick']<=tick+33}
                if len(flight)!=34:continue
                initial=flight[tick]
                check(initial['state']==1 and initial['timer']==32 and initial['affine_y']==-256,
                      'Mechakoopa did not arm inverted source32 flight')
                for n in range(1,34):
                    row=flight[tick+n]
                    check(row['timer']==32-n and row['z']==sine[(33-n)*16]*48
                          and row['cycle']==initial['cycle'] and row['x']==initial['x']
                          and row['y']==initial['y'] and row['affine_y']==-256
                          and bool(row['flags']&1)==(n<33),
                          'Mechakoopa changed source33 flight/hide or advanced patrol in flight')
                complete=True
        check(complete,'Mechakoopa omitted complete protected33-update flight')
    else:
        check(any(r[6]>0 for r in primary),'Mechakoopa omitted actual primary weak spin')
        for course,tick,mask,hit,guard,strong,spin in primary:
            check(not strong&1,'human Mechakoopa contact applied CPU strong response')
            # Final-balloon elimination clears inactive status after accepted
            # actor damage, before the immutable post-turn diagnostic snapshot.
            check(spin>0 or re.search(rf'3D Battle balloons: tick={tick+180} '
                                     r'racer=0 remaining=0 bomb=1',log),
                  'human Mechakoopa omitted weak spin without actual Battle elimination')
    song=160 if protected else 169
    check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                        r'player=\d+ volume=256',log) for course in courses),
          'Mechakoopa omitted accepted human contact audio')


def verify_mechakoopa_pictures(frames):
    palette={bytes(rgb) for rgb in ((88,196,141),(53,125,99),(234,179,77),(191,119,62),
                                   (103,172,217),(69,108,186),(230,132,165),(172,87,126))}
    counts=[]
    for picture in frames:
        width,height,pixels=read_ppm(picture)
        counts.append(sum(pixels[(y*width+x)*3:(y*width+x)*3+3] in palette
                          for y in range(30,height-20) for x in range(45,width-20)))
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2,
          'captures omitted original directional Mechakoopa artwork')


def common_prop_states(log,course):
    pattern=(rf'3D source common prop state: course={course} tick=(?P<tick>\d+) '
        r'index=(?P<index>\d+) code=(?P<code>[0-9a-f]{8}) state=(?P<state>\d+) '
        r'timer=(?P<timer>-?\d+) art=(?P<art>\d+) flags=(?P<flags>\d+) phase=(?P<phase>\d+) '
        r'affine_x=(?P<affine_x>-?\d+) affine_y=(?P<affine_y>-?\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [{k:int(v,16) if k in ('checksum','code') else int(v)
             for k,v in m.groupdict().items()} for m in re.finditer(pattern,log)]


def common_prop_contacts(log):
    return [(int(c),int(t),int(actors,16),int(hit),int(protection),int(opened),int(closed))
        for c,t,actors,hit,protection,opened,closed in re.findall(
        r'3D source common prop contact: course=(\d+) tick=(\d+) actors=([0-9a-f]{16}) '
        r'hit=(\d+) protected=(\d+) open=(\d+) close=(\d+)',log)]


def verify_common_prop_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,sine=None,require_both_toggles=True):
    starts=re.findall(r'3D source common props start: course=(\d+) common_props=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    for course in courses:
        definition=(definitions or {}).get(course,{})
        count=definition.get('count',6)
        codes=definition.get('codes',[0x060001,0x080000,0x0a0007,0x0c000b,0x860000,0x980001])
        check(any(int(c)==course and int(n)==count and int(profile)>0 and int(revision)>0
                  for c,n,profile,revision in starts),'common props omitted explicit source rules/placements')
        rows=common_prop_states(log,course)
        check(rows and {r['index'] for r in rows if r['tick']==1}==set(range(count)),
              'common-prop owner omitted initial immutable source snapshots')
        check(all(r['index']<count and r['code']==codes[r['index']] and r['state']<=1
                  and -1<=r['timer']<=32 and r['phase']<=1 and r['art']<=1
                  and 1<=r['affine_x']<=256 and r['affine_y'] in (*range(1,257),-256)
                  and r['checksum'] for r in rows),
              'common-prop raw code, variant, contact phase or initial affine changed')
        for r in rows:
            check(r['art']==(r['phase'] if r['code']>>16==0x86 else 0),
                  'umbrella art did not follow its source contact phase')
        initial=[r['checksum'] for r in rows if r['index']==0 and r['tick']==1]
        check(len(set(initial))==1,'common-prop Retry did not restore initial source world')
    if not require_contact:return
    contacts=[r for r in common_prop_contacts(log) if r[0] in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'common props omitted actual primary protected flight' if protected else
          'common props omitted genuine primary ordinary bump')
    if protected:
        check(sine is not None and len(sine)==1024,'common flight oracle requires its actual authored sine table')
        complete=False
        for course,tick,mask,hit,protection,opened,closed in primary:
            check(not hit&1 and not (opened|closed)&1,'protected common prop performed normal bump/toggle')
            rows=common_prop_states(log,course)
            for index in range(64):
                if not mask&(1<<index):continue
                flight={r['tick']:r for r in rows if r['index']==index and tick<=r['tick']<=tick+33}
                first=flight.get(tick)
                if first is None or first['state']!=1:continue
                check(first['timer']==32 and first['affine_y']==-256,
                      'common-prop Star contact did not invert and arm the original32 counter')
                if not all(tick+n in flight for n in range(34)):continue
                for n in range(1,34):
                    row=flight[tick+n]
                    check(row['timer']==32-n and row['z']==sine[(33-n)*16]*48
                          and row['affine_y']==-256 and bool(row['flags']&1)==(n<33),
                          'common prop changed exact33-update sine flight/inversion/hide')
                complete=True
        check(complete,'common-prop route omitted a complete protected33-update flight')
    else:
        toggles=[r for r in contacts if r[5]|r[6]]
        check(any((r[5]|r[6])&1 for r in toggles),
              'ordinary umbrella omitted genuine primary contact toggle')
        if require_both_toggles:
            check(any(r[5]&1 for r in toggles) and any(r[6]&1 for r in toggles),
                  'ordinary umbrella did not genuinely open and close for primary driver')
        for course,tick,mask,_,_,opened,closed in toggles:
            rows=common_prop_states(log,course)
            check(any(r['tick']==tick and mask&(1<<r['index']) and r['code']>>16==0x86
                      and r['state']==0 and r['phase']==int(bool(opened)) for r in rows),
                  'umbrella contact did not publish its actual alternate anchored frame')
    # The human hears each umbrella phase the primary driver toggled.
    songs=(160,) if protected else (169,)+tuple(song for field,song in ((6,375),(5,376))
                                               if any(r[field]&1 for r in toggles))
    for song in songs:
        check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                            r'player=\d+ volume=256',log) for course in courses),
              f'common-prop contact omitted actual human cue{song}')


def verify_common_prop_pictures(frames,protected=False):
    palette={bytes((95,88,199)),bytes((246,111,56))}
    counts=[];seen=set()
    for picture in frames:
        w,h,rgb=read_ppm(picture);count=0
        for y in range(30,h-20):
            for x in range(45,w-20):
                color=rgb[(y*w+x)*3:(y*w+x)*3+3]
                if color in palette:count+=1;seen.add(color)
        counts.append(count)
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2
          and (protected or len(seen)==2),
          'common-prop captures omitted original umbrella frame artwork')


def thundercloud_states(log,course):
    pattern=(rf'3D source thundercloud state: course={course} tick=(?P<tick>\d+) '
        r'index=(?P<index>\d+) state=(?P<state>-?\d+) timer=(?P<timer>-?\d+) '
        r'palette_timer=(?P<palette_timer>-?\d+) art=(?P<art>\d+) frame=(?P<frame>\d+) '
        r'flags=(?P<flags>\d+) phase0=(?P<phase0>\d+) phase1=(?P<phase1>\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [{k:int(v,16) if k=='checksum' else int(v) for k,v in m.groupdict().items()}
            for m in re.finditer(pattern,log)]


def verify_thundercloud_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    starts=re.findall(r'3D source thunderclouds start: course=(\d+) thunderclouds=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    for course in courses:
        definition=(definitions or {}).get(course,{})
        count=definition.get('count',4)
        variants=definition.get('variants',list(range(4)))
        check(any(int(c)==course and int(n)==count and int(profile)>0 and int(revision)>0
                  for c,n,profile,revision in starts),'thundercloud owner omitted explicit rules/placements')
        rows=thundercloud_states(log,course)
        check(rows and all(r['index']<count and r['state'] in (1,16,17)
                          and -1<=r['timer']<=241 and 0<=r['palette_timer']<=241
                          and r['art']<8 and r['frame']<72 and r['phase0']<=8 and r['phase1']<=8
                          and r['z']==4*65536 and r['checksum'] for r in rows),
              'thundercloud source clocks, phase frames or stationary height changed')
        check(any(r['tick']==33 and r['state']==16 and r['timer']==-1 for r in rows)
              and any(r['tick']==34 and r['state']==17 and r['timer']==1 for r in rows),
              'thundercloud changed original32-turn initial warning admission')
        for row in rows:
            variant=variants[row['index']]
            phase=row['phase0'] if variant<2 else row['phase1']
            check(row['frame']==phase*8+row['art'],
                  'thundercloud did not use shared variant-bank phase × eight-cell art')
            if row['state']!=17:continue
            processed=(row['timer']-1)%242
            base=0 if variant<2 else 4
            warning=any(lo<=processed<=hi for lo,hi in ((60,63),(94,97),(158,161),(166,169)))
            art=base+3 if processed>=234 else base+2 if processed>=226 else base+int(warning)
            check(row['art']==art and bool(row['flags']&2)==(processed>=234),
                  'thundercloud warning art or eight-turn collision window changed')
        check(set(range(9))<={r['phase0'] for r in rows}|{r['phase1'] for r in rows},
              'thundercloud did not publish all nine source palette phases')
    if not require_contact:return
    contacts=[(int(c),int(t),int(mask,16),int(hit),int(protection)) for c,t,mask,hit,protection in
        re.findall(r'3D source thundercloud contact: course=(\d+) tick=(\d+) '
                   r'actors=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',log) if int(c) in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'thundercloud omitted actual primary protected collision' if protected else
          'thundercloud omitted actual primary strong/shrink contact')
    shrink={(int(c),int(t),int(r)):int(remaining) for c,t,r,remaining in re.findall(
        r'3D source thundercloud shrink: course=(\d+) tick=(\d+) racer=(\d+) remaining=(\d+)',log)}
    for course,tick,mask,hit,protection in primary:
        rows=thundercloud_states(log,course)
        active=[r for r in rows if r['tick']==tick and mask&(1<<r['index']) and r['flags']&2]
        check(active and all(r['state']==17 and (r['timer']==0 or r['timer']>=235) for r in active),
              'thundercloud contact escaped the genuine eight-turn fullXYZ collision window')
        if protected:
            check(not hit&1 and shrink.get((course,tick,0),0)==0,
                  'Star-protected thundercloud applied a strong hit or shrink')
            check(any(r['tick']==tick+1 and r['index']==active[0]['index']
                      and r['state']==17 and r['z']==4*65536 for r in rows),
                  'protected thundercloud entered an invented defeat/removal state')
        else:
            # Rounds restart the tick clock, so read the shrink rows that
            # follow this contact in log order rather than by tick alone.
            contact=re.search(rf'3D source thundercloud contact: course={course} tick={tick} '
                              rf'actors={mask:016x} hit={hit} ',log)
            after=re.findall(rf'3D source thundercloud shrink: course={course} tick=(\d+) racer=0 '
                             r'remaining=(\d+)',log[contact.start():] if contact else '')
            check([(int(t),int(r)) for t,r in after[:2]]==[(tick,360),(tick+1,359)],
                  'cloud lightning did not publish fresh360 then accepted359')
    song=160 if protected else 374
    check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                        r'player=\d+ volume=256',log) for course in courses),
          f'thundercloud omitted actual human mixer cue{song}')
    if not protected:
        verify_cannonball_strong_hit(log,courses)
        check(any(re.search(rf'3D item cue: course={course} tick=\d+ song=133 '
                            r'player=\d+ volume=\d+',log) for course in courses),
              'thundercloud shrink omitted actual expiry133 audio')


def verify_thundercloud_pictures(frames):
    palette={bytes(color) for color in ((116,132,163),(164,188,221),(193,175,231),
        (225,175,227),(242,189,165),(233,219,155),(170,223,196),(134,206,218),(200,210,232))}
    counts=[];seen=set()
    for picture in frames:
        w,h,rgb=read_ppm(picture);count=0
        for y in range(30,h-20):
            for x in range(45,w-20):
                color=rgb[(y*w+x)*3:(y*w+x)*3+3]
                if color in palette:count+=1;seen.add(color)
        counts.append(count)
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2 and len(seen)>=2,
          'thundercloud captures omitted original visible changing phase artwork')


def falling_star_states(log,course):
    pattern=(rf'3D source falling star state: course={course} tick=(?P<tick>\d+) '
             r'index=(?P<index>\d+) state=(?P<state>\d+) timer=(?P<timer>-?\d+) '
             r'frame=(?P<frame>\d+) flags=(?P<flags>\d+) affine=(?P<affine>\d+) '
             r'angle=(?P<angle>\d+) x=(?P<x>-?\d+) y=(?P<y>-?\d+) z=(?P<z>-?\d+) '
             r'checksum=(?P<checksum>[0-9a-f]{16})')
    return [{k:int(v,16) if k=='checksum' else int(v) for k,v in m.groupdict().items()}
            for m in re.finditer(pattern,log)]


def verify_falling_star_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    starts=re.findall(r'3D source falling stars start: course=(\d+) falling_stars=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    for course in courses:
        count=(definitions or {}).get(course,{}).get('count',2)
        check(any(int(c)==course and int(n)==count and int(profile)>0 and int(revision)>0
                  for c,n,profile,revision in starts),'falling stars omitted explicit rules/placements')
        rows=falling_star_states(log,course)
        check(rows and all(r['index']<count and r['state']<=5 and 0<=r['timer']<=64
                          and r['frame']<=4 and 224<=r['affine']<=1024
                          and r['angle']<=65535 and r['checksum'] for r in rows),
              'falling-star snapshot escaped source state/art/affine bounds')
        check(any(r['state']==3 and r['timer']==64 and r['z']==64*65536
                  and r['affine']==960 and r['angle'] in (1024,64512) for r in rows),
              'falling star omitted original full-height first descent/rotation')
        for row in rows:
            if row['state']==3:
                elapsed=64-row['timer']
                check(row['z']==0x400000-elapsed*elapsed*0x400,
                      'falling-star descent changed exact source parabola')
            if row['state']==5:
                timer=row['timer']
                frame=4 if timer<=3 else 1 if timer<=7 else 3 if timer<=11 else 2 if timer<=15 else 1 if timer<=19 else 0
                check(row['frame']==frame,'falling-star dissolve changed source frame thresholds')
        check({1,3,5}<={r['state'] for r in rows} and {0,1,2,3,4}<={r['frame'] for r in rows},
              'falling-star route omitted wait, descent, bounce or complete dissolve art')
        comparisons=0
        by_actor={index:[r for r in rows if r['index']==index] for index in range(count)}
        for actor_rows in by_actor.values():
            for before,after in zip(actor_rows,actor_rows[1:]):
                if after['tick']!=before['tick']+1 or after['state']!=before['state']:continue
                if before['state'] not in (3,5):continue
                step=1024 if before['state']==3 else 768
                check((after['angle']-before['angle'])&65535 in (step,65536-step),
                      'falling-star source angle changed clockwise/counterclockwise step')
                comparisons+=1
        check(comparisons>32,'falling star did not animate through consecutive accepted turns')
    if not require_contact:return
    contacts=[(int(c),int(t),int(mask,16),int(hit),int(protection)) for c,t,mask,hit,protection in
        re.findall(r'3D source falling star contact: course=(\d+) tick=(\d+) '
                   r'actors=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',log) if int(c) in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'falling-star route omitted primary protected contact' if protected else
          'falling-star route omitted actual primary strong contact')
    for course,tick,mask,hit,protection in primary:
        rows=falling_star_states(log,course)
        touching=[r for r in rows if r['tick']==tick and mask&(1<<r['index']) and r['flags']&2]
        check(touching,'falling-star contact did not come from an actual collidable source pose')
        if protected:
            check(not hit&1,'protected falling star damaged primary racer')
            check(any(r['tick']==tick+1 and r['index']==touching[0]['index']
                      and (r['flags']&2 or (touching[0]['state']==5 and touching[0]['timer']==0))
                      for r in rows),'protected falling star entered an invented defeat/removal state')
    song=160 if protected else 373
    check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                        r'player=\d+ volume=256',log) for course in courses),
          f'falling-star contact omitted actual source cue{song}')
    if not protected:verify_cannonball_strong_hit(log,courses)


def verify_falling_star_pictures(frames):
    counts=[]
    palette={bytes((255,204,69)),bytes((229,99,145)),bytes((152,103,223))}
    for picture in frames:
        w,h,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3] in palette for y in range(30,h-20)
                          for x in range(45,w-20) for i in [(y*w+x)*3]))
    check(counts and max(counts)>=8 and sum(n>0 for n in counts)>=2,
          'falling-star original rotating/dissolve artwork absent from two world captures')


def teepee_contacts(log):
    return [tuple([int(row[0]),int(row[1]),int(row[2],16),
                   *map(int,row[3:7]),int(row[7],16),int(row[8],16)])
            for row in re.findall(r'3D source teepee contact: course=(\d+) tick=(\d+) '
                r'actors=([0-9a-f]{16}) touch=(\d+) protected=(\d+) assigned=(\d+) '
                r'released=(\d+) attached=([0-9a-f]{16}) flung=([0-9a-f]{16})',log)]


def verify_teepee_actor_turns(log,courses,expected=None,protected=False,require_contact=True,require_haunt=False):
    starts=re.findall(r'3D source teepees start: course=(\d+) teepees=(\d+) '
                      r'profile=(\d+) revision=(\d+)',log)
    for course in courses:
        count=(expected or {}).get(course,{}).get('count',1)
        check(any(int(c)==course and int(n)==count and int(profile)>0 and int(revision)>0
                  for c,n,profile,revision in starts),'teepee owner omitted explicit rules/placements')
    pattern=(r'3D source teepee state: course=(?P<course>\d+) tick=(?P<tick>\d+) '
             r'index=(?P<index>\d+) state=(?P<state>\d+) counter=(?P<counter>\d+) '
             r'frame=(?P<frame>\d+) flags=(?P<flags>\d+) victim=(?P<victim>\d+) '
             r'rider=(?P<rider>\d+) lock=(?P<lock>\d+) x=(?P<x>-?\d+) y=(?P<y>-?\d+) '
             r'z=(?P<z>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    rows=[{key:int(value,16) if key=='checksum' else int(value)
           for key,value in m.groupdict().items()} for m in re.finditer(pattern,log)]
    rows=[r for r in rows if r['course'] in courses]
    check(rows and all(r['state']<=3 and r['counter']<=31 and r['frame']<=3
          and r['rider']<=2 and r['lock']<=360 and r['checksum'] for r in rows),
          'teepee actor omitted bounded source tent/rider snapshots')
    if not require_contact:return
    contacts=[r for r in teepee_contacts(log) if r[0] in courses]
    primary=[r for r in contacts if (r[4] if protected else r[3])&1]
    check(primary,'teepee route omitted actual primary protected touch' if protected else
          'teepee route omitted actual primary ordinary touch')
    if protected:
        check(all(not (r[5]&1) and r[6]&1 and r[8] for r in primary),
              'protected teepee assigned a lock instead of immediate rider fling')
        for course,tick,*_ in primary:
            check(any(r['course']==course and r['tick']==tick and r['rider']==2
                      and r['victim']==0 and r['lock']==0 for r in rows),
                  'protected teepee did not publish primary fling with zero lock')
    else:
        assigned=[r for r in contacts if r[5]&1]
        check(assigned,'teepee never assigned a genuine primary rider lock')
        for course,tick,mask,*_ in assigned:
            check(any(r['course']==course and r['tick']==tick and r['victim']==0
                      and r['lock']==360 and r['rider']==1 and mask&(1<<r['index']) for r in rows),
                  'teepee assignment did not preserve fresh360 on its accepted turn')
        # Counter/frame changes are independent of which eligible racer won the
        # source last-touch rule. Test each actual tent burst, not a presumed victim.
        bursts=[r for r in rows if r['state']==2 and r['counter']==1]
        check(bursts,'teepee omitted source counter1 burst')
        for first in bursts:
            for elapsed,counter,frame in ((0,1,1),(10,11,2),(20,21,3),(30,31,3)):
                check(any(r['course']==first['course'] and r['index']==first['index']
                          and r['tick']==first['tick']+elapsed and r['counter']==counter
                          and r['frame']==frame and (elapsed!=30 or not r['flags']&1)
                          for r in rows),'teepee changed1/11/21/31 scrap/hide timing')
        decays=0
        for previous,current in zip(rows,rows[1:]):
            if (previous['course'],previous['index'],previous['victim']) != (
                current['course'],current['index'],current['victim']):continue
            if current['tick']!=previous['tick']+1 or previous['rider']!=1 or current['rider']!=1:continue
            delta=previous['lock']-current['lock']
            check(delta in (1,11) or current['lock']==360,
                  'attached teepee lock changed without accepted MIR/accelerator clock')
            decays+=delta==1
        check(decays,'teepee route omitted natural accepted MIR lock decay')
        check(any(r[6]&1 for r in contacts),'teepee never released its actual primary rider')
    for song in ((160,372) if protected else (371,372)):
        check(any(re.search(rf'3D item cue: course={course} tick=\d+ song={song} '
                            r'player=\d+ volume=256',log) for course in courses),
              f'teepee omitted actual source mixer cue{song}')
    if require_haunt and not protected:
        check(any(int(c) in courses and int(lost)>0 and int(causes)&16
                  for c,lost,causes in re.findall(r'3D coin state: course=(\d+) tick=\d+ '
                      r'racer=0 before=\d+ after=\d+ lost=(\d+) causes=(\d+)',log)),
              'teepee route omitted positive primary shared-lock coin drain')


def verify_teepee_pictures(frames):
    colors=set()
    for picture in frames:
        w,h,pixels=read_ppm(picture)
        for y in range(30,h-20):
            for x in range(45,w-20):
                color=tuple(pixels[(y*w+x)*3:(y*w+x)*3+3])
                if color in ((230,162,75),(120,68,184),(101,213,195)):colors.add(color)
    check((230,162,75) in colors and colors&{(120,68,184),(101,213,195)},
          'teepee captures omitted original tent or independent rider artwork')


def verify_coins(log, courses, mode, racers, humans=1, enabled=True,
                 require_pickup=True, require_loss=False, placements=4):
    """Check accepted coin transactions, not inferred HUD colors or driver speed.

    A duplicate-ID pickup may remove several bits but still adds exactly one coin.
    Disabled converted TT owns/replays the table but publishes no coin changes.
    """
    starts=re.findall(r'3D coins start: course=(\d+) mode=(\d+) profile=(\d+) '
                      r'revision=(\d+) placements=(\d+) cap=(\d+) initial=(\d+)',log)
    selected=[tuple(map(int,row)) for row in starts if int(row[0]) in courses]
    check({row[0] for row in selected}==set(courses), 'coin owner omitted explicit course rules')
    check(all(m==mode and profile>0 and revision>0 and count==placements and cap==99 and initial==5
              for _,m,profile,revision,count,cap,initial in selected),
          'coin owner changed selected mode, placement count or authored rules')
    rows=re.findall(r'3D coin state: course=(\d+) tick=(\d+) racer=(\d+) before=(\d+) '
                    r'after=(\d+) lost=(\d+) causes=(\d+) picked=([0-9a-f]{16}) '
                    r'available=([0-9a-f]{16}) ratio=(-?\d+) checksum=([0-9a-f]{16})',log)
    check(rows,'coin owner omitted accepted state')
    human_pickups=set();loss_courses=set();last={}
    valid_bits=(1<<placements)-1
    for raw in rows:
        course,tick,racer,before,after,lost,causes=map(int,raw[:7])
        if course not in courses:continue
        picked,available=int(raw[7],16),int(raw[8],16)
        ratio,checksum=int(raw[9]),int(raw[10],16)
        check(tick>0 and racer<racers and 0<=before<=99 and 0<=after<=99
              and lost<=100 and causes<32 and not ((picked|available)&~valid_bits)
              and not (picked&available) and checksum!=0,
              'coin transaction escaped count, identity or availability bounds')
        check(ratio==((90+min(after if enabled else 5,10))*65536+50)//100,
              'coin owner changed the explicit authored Q16 cap curve')
        if enabled:
            check(after==before-lost+bool(picked) and bool(causes)==bool(lost),
                  'coin transaction did not apply genuine losses and one pickup')
        else:
            check(before==after==5 and lost==causes==picked==0 and available==valid_bits,
                  'converted TT enabled GP-only coin effects')
        old=last.get(course)
        if old and tick>=old[0]:
            check(not (available&~old[1]),'coin availability reappeared without Retry')
        last[course]=(tick,available)
        if picked and racer<humans:human_pickups.add(course)
        if lost:loss_courses.add(course)
    if require_pickup:
        check(human_pickups==set(courses),'coin route omitted a genuine human pickup')
        check(re.search(r'3D item cue: .*song=101 ',log),
              'coin pickup did not reach actual audio cue101')
    if require_loss:
        check(loss_courses,'coin route omitted genuine accepted losses')
    return human_pickups,loss_courses


def verify_item_audio(log, path, mushroom=False, courses=(65536,)):
    """Prove the actual mixer consumed item events and emitted captured PCM."""
    cues = re.findall(r'3D item cue: course=(\d+) tick=(\d+) song=(\d+) '
                      r'player=(\d+) volume=(\d+)', log)
    check(cues, 'item route emitted no accepted mixer cues')
    songs = {int(cue[2]) for cue in cues}
    if mushroom:
        voice = re.search(r'3D item voice: racer=0 donor=\d+ normal=(\d+) small=(\d+)', log)
        check(voice is not None, 'TT did not publish its imported human mushroom voice mapping')
        voices = set(map(int, voice.groups()))
        check(125 in songs and any(song in songs for song in voices),
              'actual TT mushroom omitted its boost125 or imported driver voice cue')
        check(any(int(cue[2]) == 125 and any(other[0:2] == cue[0:2]
                  and int(other[2]) in voices for other in cues) for cue in cues),
              'mushroom voice was not emitted on an actual boost turn')
    else:
        for course in courses:
            course_songs = {int(cue[2]) for cue in cues if int(cue[0]) == course}
            check({103, 121} <= course_songs,
                  f'course {course} pickup did not reach both item-box mixer cues')
    with wave.open(str(path), 'rb') as capture:
        check(capture.getnchannels() == 2 and capture.getsampwidth() == 2
              and capture.getnframes() > 1000, 'item route actual audio capture is incomplete')
        check(any(capture.readframes(capture.getnframes())), 'item route mixed only silence')


def verify_item_owner(log, mode, racers, courses=(65536,)):
    starts = re.findall(r'3D items start: course=(\d+) mode=(\d+) profile=(\d+) '
                        r'revision=(\d+) racers=(\d+)', log)
    expected = set(courses)
    check(starts and {int(row[0]) for row in starts} == expected
          and all(int(course) in expected and int(actual_mode) == mode
          and int(profile) > 0 and int(revision) > 0 and int(count) == racers
          for course, actual_mode, profile, revision, count in starts),
          'item route did not admit its authored MIR rules and real racer field')


def verify_crab_actor_turns(log, courses, definitions=None, require_contact=True):
    """Observe native source motion and a real accepted human contact."""
    for course in courses:
        rows=re.findall(rf'3D source actor state: course={course} tick=(1|6) '
                        r'x=(-?\d+) y=(-?\d+) art=(\d+) checksum=([0-9a-f]{16})',log)
        states={}
        for tick,x,y,art,checksum in rows:
            states.setdefault(int(tick),(int(x),int(y),int(art),checksum))
        check(1 in states and 6 in states,
              f'crab controller did not publish accepted ticks1/6 course={course}')
        first,last=states[1],states[6]
        if definitions is None:
            check(last[0]==first[0] and last[1]-first[1]==5*32768
                  and first[2]==0 and last[2]==1 and first[3]!=last[3],
                  f'crab source movement/animation did not advance course={course}')
        else:
            definition=definitions[course]
            check(f'3D source actors start: course={course} crabs={definition["count"]} '
                  f'profile={definition["profile_id"]} revision={definition["revision"]}' in log,
                  f'converted course did not load its complete crab definitions course={course}')
            initial=definition['first']
            axis=1 if initial['variant'] in (1,0x8001) else 0
            origin=[initial['x_q16'],initial['y_q16']]
            base=2 if initial['variant']>=0x8000 else 0
            for tick,state in ((1,first),(6,last)):
                expected=origin[:];expected[axis]+=tick*32768
                check(list(state[:2])==expected and state[2]==base+(tick==6),
                      f'converted crab source pose/frame mismatch course={course} tick={tick}')
            check(first[3]!=last[3],f'converted crab state did not advance course={course}')
    hits=re.findall(r'3D source actor contact: course=(\d+) tick=(\d+) hit=(\d+) '
                    r'protected=(\d+) actors=([0-9a-f]{16})',log)
    def primary_contact(course,tick,hit,actors):
        course=int(course);mask=int(actors,16)
        if course not in courses or int(tick)<=0 or not int(hit)&1:return False
        # The authored fixture deliberately hits actor zero. Converted tracks
        # retain every placement, so any in-range actor can hit the human.
        if definitions is None:return bool(mask&1)
        limit=(1<<definitions[course]['count'])-1
        return bool(mask) and not mask&~limit
    check(not require_contact or any(primary_contact(course,tick,hit,actors)
              for course,tick,hit,protected,actors in hits),
          'crab route never accepted a genuine human hazard hit')


def verify_actor_ground(log, courses):
    """The authored crab walks down/up the known Z=-Y/2 bridge plane."""
    for course in courses:
        rows=re.findall(rf'3D actor ground: course={course} tick=(\d+) family=crabs index=0 '
                        r'x=(-?\d+) y=(-?\d+) z=(-?\d+) offset=(-?\d+) flags=(\d+)',log)
        grounded=[tuple(map(int,row)) for row in rows if int(row[5])&2]
        check(len(grounded)>=150,f'ground-follow patrol omitted accepted samples course={course}')
        for tick,x,y,z,offset,flags in grounded:
            check(x==20*65536 and -8*65536<=y<=0 and abs(2*z+y)<=4,
                  f'crab pose left its actual ramp plane course={course} tick={tick}')
            check(abs(z-(4*65536+offset))<=2,
                  f'crab render pose did not consume accepted ground offset course={course}')
        heights=[row[3] for row in grounded]
        check(max(heights)-min(heights)>3*65536,
              f'patrol never traversed the authored rise course={course}')
        consecutive=[(a,b) for a,b in zip(grounded,grounded[1:]) if b[0]==a[0]+1]
        check(any(b[3]<a[3] for a,b in consecutive)
              and any(b[3]>a[3] for a,b in consecutive),
              f'patrol omitted climb or descent course={course}')


def verify_snowman_actor_turns(log, courses, definitions=None, require_contact=True,
                              require_two_stages=False):
    """Observe the real two-stage source controller, separately from damage."""
    for course in courses:
        rows=re.findall(rf'3D source snowman state: course={course} tick=(\d+) index=(\d+) '
                        r'state=(-?\d+) timer=(-?\d+) art=(\d+) flags=(\d+) checksum=([0-9a-f]{16})',log)
        first=[row for row in rows if row[1]=='0']
        check(any(int(tick)==1 and int(state)==17 and int(art)==0 and int(flags)&1
                  for tick,index,state,timer,art,flags,checksum in first),
              f'snowman source initial state/frame missing course={course}')
        if definitions is not None:
            count=definitions[course]['count']
            check(count>0 and all(int(row[1])<count for row in rows),
                  f'converted snowman diagnostic index outside source definitions course={course}')
        if require_two_stages:
            states={int(row[2]) for row in first}
            check(32 in states and 48 in states and len({row[6] for row in first})>1,
                  f'snowman did not accept both real break contacts course={course}')
            check(any(int(row[4])==1 and int(row[5])&1 for row in first)
                  and any(not int(row[5])&1 for row in first),
                  f'snowman did not publish source art/visibility transitions course={course}')
    contacts=re.findall(r'3D source snowman contact: course=(\d+) tick=(\d+) '
                        r'contacts=([0-9a-f]{16}) changed=([0-9a-f]{16}) bump=(\d+) '
                        r'protected=(\d+) hit=(\d+) spin=(\d+)',log)
    check(not require_contact or any(int(course) in courses and int(tick)>0 and
              int(contact,16) and int(changed,16) and int(bump)&1 and not int(hit)&1 and int(spin)==0
              for course,tick,contact,changed,bump,protected,hit,spin in contacts),
          'snowman route never accepted a real primary bump without invented spin/damage')


def verify_penguin_actor_turns(log, courses, definitions=None, require_contact=True, protected=False):
    """Stationary source animation and real damage/protected controller contacts."""
    flight=[]
    for course in courses:
        rows=re.findall(rf'3D source penguin state: course={course} tick=(\d+) index=(\d+) '
            r'timer=(-?\d+) art=(\d+) flags=(\d+) height=(-?\d+) flip=([01]) checksum=([0-9a-f]{16})',log)
        first=[row for row in rows if row[1]=='0']
        check(any(int(row[0])==1 and int(row[2])==1 and int(row[3])==0 and int(row[4])&3==3
                  for row in first) and
              any(int(row[0])==7 and int(row[2])==7 and int(row[3])==1 for row in first),
              f'penguin source42-turn/six-frame animation missing course={course}')
        if definitions is not None:
            check(definitions[course]['count']>0 and
                  all(int(row[1])<definitions[course]['count'] for row in rows),
                  f'converted penguin index outside source definitions course={course}')
        flight.extend(first)
    contacts=re.findall(r'3D source penguin contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',log)
    admitted=[row for row in contacts if int(row[0]) in courses and int(row[1])>0 and int(row[2],16)]
    check(not require_contact or any((int(row[5])&1 and int(row[3],16) and not int(row[4])&1)
              if protected else int(row[4])&1 for row in admitted),
          'penguin route omitted an actual primary protected defeat' if protected else
          'penguin route omitted an actual primary weak-spin hazard hit')
    if protected:
        defeats=[row for row in admitted if int(row[3],16)]
        check(defeats and int(defeats[0][5])&1,
              'a CPU defeated the penguin before the primary protected contact')
        check(any(row[6]=='1' and int(row[5])!=0 and int(row[4])&1 for row in flight)
              and any(row[6]=='1' and not int(row[4])&1 for row in flight),
              'protected penguin did not flip, follow sine flight and hide')


def mouse_states(log, course):
    pattern=(rf'3D source mouse state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'variant=(?P<variant>\d+) timer=(?P<timer>-?\d+) art=(?P<art>\d+) flags=(?P<flags>\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) height=(?P<height>-?\d+) '
        r'vx=(?P<vx>-?\d+) vy=(?P<vy>-?\d+) vz=(?P<vz>-?\d+) '
        r'facing_x=(?P<facing_x>-?\d+) facing_y=(?P<facing_y>-?\d+) turn=(?P<turn>[01]) '
        r'flip=(?P<flip>[01]) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in match.groupdict().items())
            for match in re.finditer(pattern,log)]


def verify_mouse_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    for course in courses:
        rows=mouse_states(log,course)
        check(rows and any(row['tick']==1 and row['flags']&3==3 for row in rows),
              f'mouse initial source state missing course={course}')
        if definitions is not None:
            check(all(row['index']<definitions[course]['count'] for row in rows),
                  'converted mouse index outside source placements')
        else:
            walking=[row for row in rows if row['variant'] in (0,1)]
            hopping=[row for row in rows if row['variant']==0x8001]
            check(walking and hopping and any(row['height']>0 for row in hopping)
                  and {row['art'] for row in walking}=={0,1}
                  and any(len({(row['x'],row['y']) for row in walking if row['index']==index})>1
                          for index in {row['index'] for row in walking}),
                  'authored mice omitted actual walking/two-frame animation or hopping')
    contacts=re.findall(r'3D source mouse contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',log)
    accepted=[row for row in contacts if int(row[0]) in courses and int(row[2],16)]
    check(not require_contact or any((int(row[5])&1 and int(row[3],16) and not int(row[4])&1)
          if protected else int(row[4])&1 for row in accepted),
          'mouse route omitted primary protected defeat' if protected else 'mouse route omitted primary weak-spin hit')
    if protected:
        defeats=[row for row in accepted if int(row[3],16)]
        check(defeats and int(defeats[0][5])&1,'CPU defeated mouse before primary protection')
        first=defeats[0];tick=int(first[1]);mask=int(first[3],16)
        rows=mouse_states(log,int(first[0]))
        for index in range(64):
            if not mask&(1<<index):continue
            flight=[row for row in rows if row['index']==index and row['tick']>=tick and row['flip']]
            check(flight and any(row['height']>0 and row['flags']&1 for row in flight)
                  and any(not row['flags']&1 for row in flight), 'protected mouse omitted flight/hide')
            if flight[0]['variant']!=0x8001:
                check(any(row['tick']==tick+33 and not row['flags']&1 for row in flight),
                      'walking protected mouse omitted exact33-update sine flight')
            else:
                check(any(row['vz']<0 and row['height']>0 for row in flight),
                      'hopping protected mouse omitted descending ballistic flight')


def piranha_states(log,course):
    names=('tick','index','state','timer','phase','art','flags','height','flip','victim')
    return [dict(zip(names,map(int,row[:-1])),checksum=row[-1]) for row in re.findall(
        rf'3D source piranha state: course={course} tick=(\d+) index=(\d+) state=(\d+) '
        r'timer=(-?\d+) phase=(-?\d+) art=(\d+) flags=(\d+) height=(-?\d+) '
        r'flip=([01]) victim=(\d+) checksum=([0-9a-f]{16})',log)]


def verify_piranha_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    for course in courses:
        rows=piranha_states(log,course)
        check(rows and any(row['tick']==1 and row['state']==1 and row['flags']==2
                          and row['art']==0 for row in rows),
              f'piranha hidden collidable initial state missing course={course}')
        if definitions is not None:
            check(all(row['index']<definitions[course]['count'] for row in rows),
                  f'converted piranha index outside source placements course={course}')
    contacts=re.findall(r'3D source piranha contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) changed=([0-9a-f]{16}) protected=([0-9a-f]{16}) touch=(\d+) bite=(\d+)',log)
    actual=[row for row in contacts if int(row[0]) in courses and int(row[2],16) and int(row[5])&1]
    if not require_contact:return
    if protected:
        accepted=[row for row in actual if int(row[4],16) and not int(row[6])&1]
        check(accepted,'piranha omitted actual primary protected plant contact')
        first=accepted[0];tick=int(first[1]);mask=int(first[4],16)
        rows=piranha_states(log,int(first[0]))
        for index in range(64):
            if not mask&(1<<index):continue
            flight=[row for row in rows if row['index']==index and row['tick']>=tick]
            check(any(row['tick']==tick and row['state']==240 for row in flight)
                  and any(row['state']==241 and row['flip'] and row['height']>0 for row in flight)
                  and any(row['state']==243 for row in flight)
                  and all(not row['flags']&1 for row in flight),
                  'protected initially hidden plant omitted native ballistic defeat or became visible')
        captures=re.findall(r'3D source capture event: course=(\d+) tick=\d+ captured=(\d+) launched=\d+',log)
        check(not any(int(course) in courses and int(mask)&1 for course,mask in captures),
              'ordinary Star protection admitted primary material capture')
    else:
        check(any(int(row[3],16) and not int(row[4],16) for row in actual)
              and any(int(row[0]) in courses and int(row[6])&1 for row in contacts),
              'piranha route omitted primary contact or delayed bite callback')
        check(any({1,2,3,4} <= {row['art'] for row in piranha_states(log,course)
                               if row['state']==17 and row['flags']&1 and row['victim']==0}
                  and any(row['tick']>1 and row['state']==1 and row['flags']==2
                          for row in piranha_states(log,course)) for course in courses),
              'piranha bite omitted five-frame reveal and return to hidden collision')


def verify_piranha_capture(log,courses,required=True):
    events=re.findall(r'3D source capture event: course=(\d+) tick=(\d+) captured=(\d+) launched=(\d+)',log)
    starts=[(int(c),int(t)) for c,t,mask,_ in events if int(c) in courses and int(mask)&1]
    check(starts or not required,'piranha route omitted genuine primary material capture')
    completed=[]
    for course,start in starts:
        if not any(int(c)==course and int(t)==start+92 and int(mask)&1 for c,t,_,mask in events):continue
        states={int(t):(int(remaining),int(actor),int(spin),int(hidden)) for t,remaining,actor,spin,hidden
            in re.findall(rf'3D source capture: course={course} tick=(\d+) racer=0 remaining=(\d+) '
                          r'actor=(\d+) spin=(\d+) hidden=([01])',log)}
        check(all(start+offset in states for offset in range(92)),
              'capture omitted accepted per-turn state needed for92-turn proof')
        for offset in range(92):
            remaining,actor,spin,hidden=states[start+offset]
            check(remaining==92-offset and actor>0 and hidden==int(remaining<=59)
                  and spin==((min(offset,33)*0xc00)&65535),
                  'capture changed source92/33-pull/59-hidden clock or spin sequence')
        completed.append((course,start,start+92))
    check(completed or not required,'capture did not naturally launch exactly92 accepted turns later')
    return completed


def verify_piranha_audio(log,courses,capture=False):
    for course in courses:
        songs={int(song) for song in re.findall(rf'3D item cue: course={course} tick=\d+ song=(\d+) ',log)}
        check(({373,378,162,381} if capture else {373}) <= songs,
              f'piranha source bite/capture sounds absent from actual mixer course={course}')


def verify_piranha_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3] in (bytes((79,193,107)),bytes((223,64,87)))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8,'visible bite frames never reached a real world capture')


def verify_mouse_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3]==bytes((194,142,98))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(count>0 for count in counts)>=2,
          'mouse models absent from two actual world captures')


def seagull_states(log,course):
    pattern=(rf'3D source seagull state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'state=(?P<state>\d+) timer=(?P<timer>-?\d+) phase=(?P<phase>\d+) art=(?P<art>\d+) '
        r'flags=(?P<flags>\d+) height=(?P<height>-?\d+) flip=(?P<flip>[01]) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in match.groupdict().items())
            for match in re.finditer(pattern,log)]


def verify_seagull_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    for course in courses:
        rows=seagull_states(log,course)
        check(any(row['tick']==1 and row['state']==1 and row['art']==0 and row['flags']==3
                  for row in rows),f'seagull source initialization missing course={course}')
        if definitions is not None:
            check(all(row['index']<definitions[course]['count'] for row in rows),
                  'converted seagull index outside source placement count')
    contacts=re.findall(r'3D source seagull contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) changed=([0-9a-f]{16}) protected=([0-9a-f]{16}) '
        r'touch=(\d+) star=(\d+) hit=(\d+) grace=(\d+)',log)
    accepted=[row for row in contacts if int(row[0]) in courses and int(row[2],16)]
    if not require_contact:return
    if protected:
        actual=[row for row in accepted if int(row[4],16) and int(row[6])&1]
        check(actual,'seagull omitted primary Star-protected contact')
        first=actual[0];tick=int(first[1]);mask=int(first[4],16)
        rows=seagull_states(log,int(first[0]))
        for index in range(64):
            if not mask&(1<<index):continue
            flight=[row for row in rows if row['index']==index and row['tick']>=tick]
            check(any(row['state']==240 for row in flight) and
                  any(row['state']==241 and row['flip'] for row in flight) and
                  any(row['state']==243 and not row['flags']&1 and row['height']<0 for row in flight),
                  'protected seagull omitted signed ballistic flight and hidden removal')
        check(not int(first[5])&1 and not int(first[7])&1 and int(first[8])==0,
              'Star seagull invented ordinary touch, damage or grace')
    else:
        actual=[row for row in accepted if int(row[5])&1 and not int(row[6])&1]
        check(actual and all(not int(row[7])&1 and int(row[8])==0 for row in actual),
              'ordinary seagull omitted harmless primary touch or invented hit/grace')
        complete=False
        for course,tick,_,changed,_,_,_,_,_ in actual:
            if not int(changed,16):continue
            start=int(tick);rows=seagull_states(log,int(course))
            for index in range(64):
                if not int(changed,16)&(1<<index):continue
                flutter=[row for row in rows if row['index']==index and start<row['tick']<=start+40]
                if {row['phase'] for row in flutter} >= set(range(8)) and any(
                    row['tick']==start+40 and row['state']==0 for row in flutter):
                    # The stock table traverses its nonnegative half-sine;
                    # signed/custom-table behavior belongs to native owner tests.
                    check({1,2}<={row['art'] for row in flutter} and
                          any(row['height']>0 for row in flutter) and
                          any(row['tick']==start+40 and row['height']==0 for row in flutter) and
                          all(row['phase']==(row['tick']-start)//5 and
                              row['timer']==(row['tick']-start)%5 and
                              row['art']==(2 if row['phase']==8 else 1+(row['phase']&1))
                              for row in flutter),
                          'seagull flutter lost five-turn art/phase timing or zero-height return')
                    complete=True
        check(complete,'ordinary seagull omitted complete40-update source flutter')
        for course in {int(row[0]) for row in actual}:
            check(re.search(rf'3D item cue: course={course} tick=\d+ song=385 player=\d+ volume=256',log),
                  'harmless source contact omitted actual mixer cue385')


def verify_seagull_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3]==bytes((168,191,212))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(count>0 for count in counts)>=2,
          'seagull models absent from two actual world captures')


def lava_states(log,course):
    pattern=(rf'3D source lava state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'phase=(?P<phase>\d+) timer=(?P<timer>-?\d+) art=(?P<art>\d+) flags=(?P<flags>\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) height=(?P<height>-?\d+) '
        r'vx=(?P<vx>-?\d+) vy=(?P<vy>-?\d+) vz=(?P<vz>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in match.groupdict().items())
            for match in re.finditer(pattern,log)]


def verify_lava_burn_clock(log, actual):
    """Burn clocks decay until a refresh or the same driver's proved recovery.
    Each Battle round restarts the tick clock, so rounds are checked apart."""
    witnessed=False
    rounds=re.split(r'(?=3D Battle start: )',log)
    for part in rounds:
        mine=actual if len(rounds)==1 else [row for row in re.findall(
            r'3D source lava contact: course=(\d+) tick=(\d+) contacts=([0-9a-f]{16}) '
            r'defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+) burn=(\d+)',part) if row in actual]
        witnessed|=verify_lava_burn_round(part,mine)
    check(witnessed,'lava omitted accepted burn decay diagnostics')


def verify_lava_burn_round(log, actual):
    burn_rows=re.findall(r'3D source lava burn: course=(\d+) tick=(\d+) racer=0 remaining=(\d+)',log)
    recoveries={(course,int(tick)) for course,tick,causes in re.findall(
        r'3D coin state: course=(\d+) tick=(\d+) racer=0 [^\n]* causes=(\d+) ',log)
        if int(causes)&2}
    # Battle has no coins: the host names the rescue, bomb or respawn instead.
    recoveries|={(course,int(tick)) for course,tick,rescue,bomb,respawn in re.findall(
        r'3D source lava burn clear: course=(\d+) tick=(\d+) racer=0 rescue=(\d) bomb=(\d) respawn=(\d)',log)
        if '1' in (rescue,bomb,respawn)}
    witnessed=False
    for course,tick,_,_,_,_,_ in actual:
        start=int(tick)
        values={int(t):int(remaining) for c,t,remaining in burn_rows if c==course}
        check(values.get(start)==250,'accepted lava hit did not set250-turn burn')
        refresh={int(row[1]) for row in actual if row[0]==course and int(row[1])>start}
        end=min(refresh) if refresh else start+251
        clear=min((t for c,t in recoveries if c==course and start<t<end),default=start+251)
        sample=[(t,value) for t,value in sorted(values.items()) if start<t<min(end,start+251)]
        check(all(value==(0 if t>=clear else max(0,250-(t-start))) for t,value in sample),
              'lava burn clock did not decay or clear on proved primary recovery')
        witnessed|=bool(sample)
    return witnessed


def verify_lava_actor_turns(log,courses,definitions=None,require_contact=True,protected=False):
    for course in courses:
        rows=lava_states(log,course)
        check(any(row['tick']==1 and row['phase'] in (1,2) for row in rows),
              f'lava bubble initial source state missing course={course}')
        if definitions is not None:
            check(all(row['index']<definitions[course]['count'] for row in rows),
                  'converted lava bubble index outside source placements')
        if not protected:
            complete=False
            last_tick=max(row['tick'] for row in rows)
            for row in rows:
                if row['phase']!=0 or row['height'] or row['flags']&3!=3:continue
                index=row['index'];end=row['tick']
                cycle=[r for r in rows if r['index']==index and end-63<=r['tick']<=end]
                if not any(r['phase']==3 and r['timer']==63 for r in cycle):continue
                check(any(r['timer']==32 and r['art']==0 for r in cycle) and
                      any(r['timer']==31 and r['art']==1 for r in cycle) and
                      any(r['height']>0 for r in cycle),
                      'lava64-cycle lost art31 threshold')
                # The race can finish on this last collidable turn. Its hide
                # belongs to the next update, outside the captured lifetime;
                # only an earlier, fully observed cycle can satisfy the proof.
                if end==last_tick:continue
                check(any(r['index']==index and r['tick']==end+1 and r['phase']==1 and not r['flags']&3
                          for r in rows),
                      'lava64-cycle lost last-zero collision before hide')
                complete=True
            check(complete,'lava bubble omitted complete64-update source cycle')
    contacts=re.findall(r'3D source lava contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+) burn=(\d+)',log)
    accepted=[row for row in contacts if int(row[0]) in courses and int(row[2],16)]
    if not require_contact:return
    if protected:
        actual=[row for row in accepted if int(row[3],16) and int(row[5])&1]
        check(actual and all(not int(row[4])&1 and not int(row[6])&1 for row in actual),
              'protected lava contact omitted defeat or invented primary damage/burn')
        first=actual[0];start=int(first[1]);rows=lava_states(log,int(first[0]))
        for index in range(64):
            if not int(first[3],16)&(1<<index):continue
            flight=[row for row in rows if row['index']==index and row['tick']>=start]
            copied=[row for row in flight if row['tick']==start and row['phase']==4]
            check(copied and all(-32768<=row['vx']<=32767 and -32768<=row['vy']<=32767 for row in copied)
                  and any(row['phase']==5 for row in flight)
                  and any(row['phase']==6 and row['height']<0 and not row['flags']&1 for row in flight),
                  'protected lava lost signed16 source velocity or ballistic removal')
    else:
        actual=[row for row in accepted if int(row[4])&1 and int(row[6])&1]
        check(actual,'lava route omitted actual primary weak-spin and burn')
        verify_lava_burn_clock(log,actual)
        check(re.search(r'3D item cue: course=\d+ tick=\d+ song=377 player=\d+ volume=256',log),
              'lava hit omitted actual source mixer cue377')


def verify_lava_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3]==bytes((243,106,36))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(count>0 for count in counts)>=2,
          'lava bubble models absent from two actual world captures')


def rock_states(log,course):
    pattern=(rf'3D source rock state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'variant=(?P<variant>\d+) state=(?P<state>\d+) timer=(?P<timer>-?\d+) '
        r'frame=(?P<frame>\d+) flags=(?P<flags>\d+) x=(?P<x>-?\d+) y=(?P<y>-?\d+) '
        r'height=(?P<height>-?\d+) vz=(?P<vz>-?\d+) affine=(?P<affine>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in m.groupdict().items())
            for m in re.finditer(pattern,log)]


def verify_rock_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,require_start=True,cleanup_ticks=()):
    for course in courses:
        if require_start:
            check(re.search(rf'3D source rocks start: course={course} rocks=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
                  'rock owner omitted explicit source rules')
        rows=rock_states(log,course)
        check(rows and any(r['tick']==1 and r['state']==1 and not r['flags']&3 for r in rows),
              'rock omitted initial hidden delay')
        check(all(0<=r['variant']<=3 and 0<=r['state']<=11 and 0<=r['frame']<=4 for r in rows),
              'rock invented variant/frame/protected flight')
        check({r['frame'] for r in rows}==set(range(5)) and
              {3,5,7,9,11}.issubset({r['state'] for r in rows}),
              'rock omitted fall, impact, two bounces or rest')
        complete=False
        for index in {r['index'] for r in rows}:
            sequence=[r for r in rows if r['index']==index]
            for before,after in zip(sequence,sequence[1:]):
                if after['tick']!=before['tick']+1:continue
                if before['state']==3:
                    check(after['state'] in (3,4) and after['vz']==before['vz']-0xc00 and
                          after['height']==max(0,before['height']+after['vz']) and
                          after['affine']==max(192,before['affine']-24),
                          'rock changed source fall gravity/affine growth')
                if before['state'] in (7,9):
                    landed=before['height']+before['vz']<=0
                    check(after['x']==before['x']+0x4000 and after['y']==before['y']+0x4000 and
                          after['height']==max(0,before['height']+before['vz']) and
                          after['vz']==before['vz']-(0 if landed else 0x1800),
                          'rock changed diagonal quarter-unit bounce/gravity')
                if before['state']==0 and after['state']==1:
                    first=sequence[0]
                    check(after['x']==first['x'] and after['y']==first['y'] and not after['flags']&3,
                          'rock did not return to original hidden home')
                    complete=True
        if not protected:check(complete,'rock omitted complete rest/restart lifecycle')
        if definitions is not None:
            check(all(r['index']<definitions[course]['count'] for r in rows),
                  'converted rock index exceeds source placements')
        for song in range(386,390):
            check(re.search(rf'3D source rock cue: course={course} tick=\d+ song={song} actors=(?!0000000000000000)[0-9a-f]{{16}}',log),
                  f'rock omitted accepted source event{song}')
    if not require_contact:return
    contacts=[tuple(int(v,16) if i==2 else int(v) for i,v in enumerate(m)) for m in re.findall(
        r'3D source rock contact: course=(\d+) tick=(\d+) contacts=([0-9a-f]{16}) hit=(\d+) protected=(\d+) burn=(\d+)',log)
        if int(m[0]) in courses]
    primary=[r for r in contacts if r[2] and ((r[4]&1) if protected else (r[3]&1 and r[5]&1))]
    check(primary,'rock omitted protected primary contact' if protected else 'rock omitted primary weak hit/burn')
    if protected:
        check(all(not hit&1 and not burn&1 for _,_,_,hit,_,burn in primary),
              'protected rock contact invented primary hit/burn')
    else:
        burn_rows=re.findall(r'3D source lava burn: course=(\d+) tick=(\d+) racer=0 remaining=(\d+)',log)
        decayed=False
        for course,tick,_,_,_,_ in primary:
            values={int(t):int(n) for c,t,n in burn_rows if int(c)==course}
            check(values.get(tick)==250,'rock hit did not set250-turn shared burn')
            refresh=min((r[1] for r in primary if r[0]==course and r[1]>tick),default=tick+251)
            sample=[(t,n) for t,n in values.items() if tick<t<min(refresh,tick+251)]
            check(all(n==(0 if any(tick<clear<=t for clear in cleanup_ticks) else max(0,250-(t-tick)))
                      for t,n in sample),'rock burn did not decay by accepted ticks or witnessed recovery cleanup')
            decayed|=bool(sample)
        check(decayed,'rock omitted shared burn decay diagnostics')
    for course,tick,mask,_,_,_ in primary:
        rows=rock_states(log,course)
        check(any(r['tick']==tick and mask&(1<<r['index']) and r['flags']&2 for r in rows),
              'rock contact came from a hidden/noncolliding source pose')
        if protected:
            check(any(r['tick']==tick+1 and mask&(1<<r['index']) and r['state']<=11 for r in rows),
                  'protected rock incorrectly vanished/entered a defeat flight')
    for song in ((160,) if protected else (377,)):
        check(re.search(rf'3D item cue: course=\d+ tick=\d+ song={song} player=\d+ volume=256',log),
              f'rock omitted source contact mixer cue{song}')
    for song in range(386,390):
        check(re.search(rf'3D item cue: course=\d+ tick=\d+ song={song} player=\d+ volume=\d+',log),
              f'rock omitted actual positional mixer cue{song}')


def verify_rock_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3] in (bytes((93,66,115)),bytes((220,97,51)),bytes((255,193,85)))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(c>0 for c in counts)>=2,
          'original rock cells absent from two actual world captures')


def boo_states(log,course):
    pattern=(rf'3D source boo state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'state=(?P<state>\d+) phase=(?P<phase>\d+) height=(?P<height>-?\d+) '
        r'flags=(?P<flags>\d+) heading=(?P<heading>\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in m.groupdict().items())
            for m in re.finditer(pattern,log)]


def verify_boo_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,require_start=True,star_ticks=None):
    for course in courses:
        if require_start:
            check(re.search(rf'3D source boos start: course={course} boos=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
                  'Boo owner omitted explicit source rules')
        rows=boo_states(log,course)
        check(rows and any(r['tick']==1 and r['phase']==1024 and r['height']==0x80000 for r in rows),
              'Boo omitted original first bob update')
        check(all(r['state']==17 and r['flags']==3 and r['heading']==0x4000 and
                  0x68000<=r['height']<=0x98000 for r in rows),
              'Boo invented defeat, fading or non-source bob height')
        compared=0
        for index in {r['index'] for r in rows}:
            sequence=[r for r in rows if r['index']==index]
            at={r['tick']:r for r in sequence}
            for before,after in zip(sequence,sequence[1:]):
                check(after['phase']==(before['phase']+1024*(after['tick']-before['tick']))&0xffffffff,
                      'Boo changed source phase update/wrap')
            for tick,row in at.items():
                if tick+64 in at:
                    check(row['height']==at[tick+64]['height'],'Boo lost exact64-turn source bob')
                    compared+=1
        check(compared and len({r['height'] for r in rows})>8,
              'Boo did not demonstrate complete moving source bob cycle')
        if definitions is not None:
            check(all(r['index']<definitions[course]['count'] for r in rows),
                  'converted Boo index exceeds source placements')
        views=[tuple(map(int,m)) for m in re.findall(rf'3D source boo view: course={course} '
            r'actor=(\d+) view=(\d+) side=(\d+) model=(\d+)',log)]
        mapping={}
        for index,view,side,model in views:
            check(side in (0,1) and view<4,'Boo view selection exceeds declared cells/views')
            key=(index,side)
            check(key not in mapping or mapping[key]==model,'Boo cell changed stable model identity')
            mapping[key]=model
        # The bounded protected route ends before its camera crosses the source
        # half-turn. Full ordinary routes prove both cells; this route still
        # validates each observed side and its stable model identity.
        if require_contact and not protected:
            check(any((index,0) in mapping and (index,1) in mapping and
                mapping[index,0]!=mapping[index,1] for index in {r[0] for r in views}),
                'Boo did not demonstrate both independent camera-selected cells')
    if not require_contact:return
    contacts=[tuple(int(v,16) if i==2 else int(v) for i,v in enumerate(m)) for m in re.findall(
        r'3D source boo contact: course=(\d+) tick=(\d+) contacts=([0-9a-f]{16}) cue=(\d+) pending=(\d+) flash=(\d+)',log)
        if int(m[0]) in courses]
    primary=[r for r in contacts if r[2] and ((r[4]&1) if protected else (r[3]&1 and r[5]&1))]
    check(primary,'Boo omitted primary protected deferred request' if protected else
          'Boo omitted actual harmless primary touch and resolved palette flash')
    if protected:
        check(all(not cue&1 and not flash&1 for _,_,_,cue,_,flash in primary),
              'Boo consumed a palette request or sounded contact while Star active')
        check(not re.search(r'3D item cue: course=\d+ tick=\d+ song=(?:160|380) player=\d+ ',log),
              'protected Boo invented protected/ordinary contact audio')
        activation=re.search(r'3D item event: course=65536 tick=(\d+) kind=0 item=100a racer=0 ',log)
        check(activation and isinstance(star_ticks,int) and star_ticks>0,
              'protected Boo oracle needs actual activation and authored Star duration')
        expiry=int(activation[1])-180+star_ticks
        palette=[tuple(map(int,m)) for m in re.findall(
            r'3D source boo palette: course=65536 tick=(\d+) pending=(\d+) flash=(\d+)',log)]
        check(any(tick<expiry and pending&1 for tick,pending,_ in palette) and
              all(not flash&1 for tick,_,flash in palette if tick<expiry),
              'Boo omitted deferred primary palette state or flashed during Star')
        check(any(tick==expiry and not pending&1 and flash&1 for tick,pending,flash in palette)
              and any(tick==expiry+1 and not pending&1 and not flash&1 for tick,pending,flash in palette),
              'Boo did not consume its deferred request for exactly one turn on Star expiry')
    else:
        check(re.search(r'3D item cue: course=\d+ tick=\d+ song=380 player=\d+ volume=256',log),
              'ordinary Boo omitted actual source380 mixer cue')
    for course,tick,mask,_,_,_ in primary:
        check(any(r['tick']==tick and mask&(1<<r['index']) and r['state']==17 and r['flags']==3
                  for r in boo_states(log,course)),
              'Boo touch did not retain the live source pose/collision flags')


def verify_boo_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3] in (bytes((240,232,255)),bytes((102,85,155)),bytes((242,128,178)))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(c>0 for c in counts)>=2,
          'original Boo cells absent from two actual world captures')


def cannonball_states(log,course):
    pattern=(rf'3D source cannonball state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'state=(?P<state>\d+) timer=(?P<timer>-?\d+) flags=(?P<flags>\d+) art=(?P<art>\d+) '
        r'size=(?P<size>\d+) height=(?P<height>-?\d+) vz=(?P<vz>-?\d+) '
        r'inverse_x=(?P<inverse_x>\d+) inverse_y=(?P<inverse_y>\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in m.groupdict().items())
            for m in re.finditer(pattern,log)]


def verify_cannonball_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,require_start=True):
    for course in courses:
        rows=cannonball_states(log,course)
        check(not require_start or re.search(rf'3D source cannonballs start: course={course} cannonballs=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
              'cannonball owner omitted explicit source rules')
        check(rows and any(r['tick']==1 for r in rows),'cannonball initialization missing')
        if definitions is not None:
            check(all(r['index']<definitions[course]['count'] for r in rows),
                  'converted cannonball index exceeds source placements')
        check(any(r['state']==3 and r['height']>0 and r['vz']<0 and r['flags']&2 for r in rows),
              'cannonball omitted genuine falling collision phase')
        check({r['art'] for r in rows if r['state']==5}==set(range(1,7)),
              'cannonball omitted six native explosion frames')
        check(all(not r['flags']&2 for r in rows if r['state']==5),
              'cannonball explosion invented damaging collision')
        for index in {r['index'] for r in rows}:
            sequence=[r for r in rows if r['index']==index]
            for before,after in zip(sequence,sequence[1:]):
                if after['tick']!=before['tick']+1 or before['state']!=3:continue
                check(after['state'] in (3,4) and after['vz']==before['vz']-0x800 and
                      after['height']==max(0,before['height']+after['vz']),
                      'cannonball changed native falling gravity/height')
                if after['state']==3:
                    check(after['inverse_x']==after['inverse_y']==max(256,before['inverse_x']-24),
                          'cannonball lost original inverse-affine growth')
        for event in ('launched','exploded'):
            check(re.search(rf'3D source cannonball events: course={course} tick=\d+ '+
                (r'launched=(?!0000000000000000)[0-9a-f]{16} exploded=[0-9a-f]{16}' if event=='launched' else
                 r'launched=[0-9a-f]{16} exploded=(?!0000000000000000)[0-9a-f]{16}'),log),
                  f'cannonball omitted accepted {event} event')
    if not require_contact:return
    contacts=[tuple(int(v,16) if i==2 else int(v) for i,v in enumerate(m)) for m in re.findall(
        r'3D source cannonball contact: course=(\d+) tick=(\d+) contacts=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',log)
        if int(m[0]) in courses]
    primary=[r for r in contacts if r[2] and (r[4] if protected else r[3])&1]
    check(primary,'cannonball omitted actual primary protected contact' if protected else
          'cannonball omitted actual primary falling strong hit')
    for course,tick,mask,hit,protection in primary:
        check(not protected or not hit&1,'protected cannonball damaged primary racer')
        rows=cannonball_states(log,course)
        # A ball that lands on the contact turn entered it still falling.
        check(any(r['tick'] in (tick,tick-1) and mask&(1<<r['index']) and r['state']==3 and r['flags']&2 for r in rows),
              'cannonball contact did not come from live falling actor')
        if protected:
            check(any(r['tick']==tick+1 and mask&(1<<r['index']) and r['state'] in (3,4) for r in rows),
                  'protected cannonball incorrectly disappeared or entered a defeat flight')
    for song in ((160,382,383) if protected else (170,162,382,383)):
        check(re.search(rf'3D item cue: course=\d+ tick=\d+ song={song} player=\d+ volume=\d+',log),
              f'cannonball omitted actual mixer cue{song}')
    if not protected:
        verify_cannonball_strong_hit(log,courses)


def verify_cannonball_strong_hit(log,courses):
    rows=[tuple(map(int,m)) for m in re.findall(r'3D source strong hit: course=(\d+) tick=(\d+) '
        r'racer=0 pending=(\d+) active=(\d+) rate=(\d+) angle=(\d+) direction=(\d+)',log)
        if int(m[0]) in courses]
    check(any(p and not a and rate in (0x1000,0x2000) for _,_,p,a,rate,_,_ in rows),
          'cannonball omitted source pending strong-hit admission')
    complete=False;compared=0
    for before,after in zip(rows,rows[1:]):
        course,tick,pending,active,rate,angle,direction=before
        if course!=after[0] or not (pending or active) or after[2]:continue
        # A pending airborne wait has no angular updates; the first grounded
        # transition takes exactly one recovered3956C step, however long it waited.
        if active and after[1]!=tick+1:continue
        if not after[3] and after[4]==0:continue  # Explicit rescue clears status.
        angle=(angle+(1 if direction else -1)*min(rate,0x1000))&0xffff
        expected_active=1
        if rate>0xff:
            rate-=0x40
        else:
            signed=angle if angle<0x8000 else angle-0x10000
            if signed>0x17f:angle=(angle-0x480)&0xffff
            elif signed<=-0x180:angle=(angle+0x480)&0xffff
            else:angle=0;expected_active=0
        check(after[2:]==(0,expected_active,rate,angle,direction),
              'cannonball strong hit changed recovered rate/angle/direction/release')
        compared+=1
        complete|=not expected_active
    check(compared>=64 and complete,
          'cannonball omitted a complete source strong-hit angular lifecycle')


def verify_cannonball_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3] in (bytes((52,70,91)),bytes((255,159,50)),bytes((255,223,112)))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(c>0 for c in counts)>=2,
          'cannonball art absent from two actual world captures')


def thwomp_states(log,course):
    pattern=(rf'3D source thwomp state: course={course} tick=(?P<tick>\d+) index=(?P<index>\d+) '
        r'variant=(?P<variant>\d+) state=(?P<state>\d+) timer=(?P<timer>-?\d+) flags=(?P<flags>\d+) '
        r'x=(?P<x>-?\d+) y=(?P<y>-?\d+) height=(?P<height>-?\d+) '
        r'vx=(?P<vx>-?\d+) vy=(?P<vy>-?\d+) vz=(?P<vz>-?\d+) checksum=(?P<checksum>[0-9a-f]{16})')
    return [dict((key,value if key=='checksum' else int(value)) for key,value in match.groupdict().items())
            for match in re.finditer(pattern,log)]


def verify_thwomp_actor_turns(log,courses,definitions=None,require_contact=True,protected=False,require_bump=True):
    for course in courses:
        rows=thwomp_states(log,course)
        check(any(row['tick']==1 and row['flags']==3 for row in rows),
              f'Thwomp source initialization missing course={course}')
        if definitions is not None:
            check(all(row['index']<definitions[course]['count'] for row in rows),
                  'converted Thwomp index outside source placements')
        if not protected:
            check(any(row['height']>0 and row['vz']>0 for row in rows) and
                  any(row['height']>65536 and row['vz']<0 for row in rows),
                  'Thwomp omitted original rise and descending crush motion')
    contacts=re.findall(r'3D source thwomp contact: course=(\d+) tick=(\d+) '
        r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) bump=(\d+) crush=(\d+) hit=(\d+) protected=(\d+)',log)
    actual=[row for row in contacts if int(row[0]) in courses and int(row[2],16)]
    if not require_contact:return
    if protected:
        defeats=[row for row in actual if int(row[3],16) and int(row[7])&1]
        check(defeats and all(not int(row[4])&1 and not int(row[5])&1 and not int(row[6])&1
                             for row in defeats),'protected Thwomp omitted primary immunity/defeat')
        first=defeats[0];tick=int(first[1]);mask=int(first[3],16)
        for index in range(64):
            if not mask&(1<<index):continue
            flight=[row for row in thwomp_states(log,int(first[0])) if row['index']==index and row['tick']>=tick]
            initial=next((row for row in flight if row['tick']==tick),None)
            hidden=next((row for row in flight if not row['flags']&1),None)
            # Source impulse0x24000 is2.25Q, gravity0x4000. The highest
            # normal rumble32.5Q therefore lands within29 updates, not24.
            check(initial is not None and initial['vz']==0x24000 and not initial['flags']&2
                  and hidden is not None and tick<hidden['tick']<=tick+29,
                  'protected Thwomp omitted bounded source flight/removal')
            for row in flight:
                if row['tick']>hidden['tick']:break
                n=row['tick']-tick
                check(row['height']==initial['height']+0x24000*n-0x2000*n*(n-1) and
                      row['vz']==0x24000-0x4000*n and
                      row['x']==initial['x']+initial['vx']*n and
                      row['y']==initial['y']+initial['vy']*n,
                      'protected Thwomp changed original impulse/gravity/source trajectory')
    else:
        check(not require_bump or any(int(row[4])&1 and not int(row[6])&1 for row in actual),
              'Thwomp omitted harmless primary ground rebound')
        crushes=[row for row in actual if int(row[5])&1 and int(row[6])&1]
        check(crushes,'Thwomp omitted actual primary descending crush')
        complete=False
        for course,tick,mask,_,_,_,_,_ in crushes:
            start=int(tick);rows=thwomp_states(log,int(course))
            check(any(row['tick']==start and int(mask,16)&(1<<row['index']) and
                      row['height']>65536 and row['vz']<0 for row in rows),
                  'crush did not originate from descending actor above one source unit')
            held={int(t):(int(timer),int(mode),int(row),int(remaining),int(ix),int(iy))
                for t,timer,mode,row,remaining,ix,iy in re.findall(
                    rf'3D source thwomp crush: course={course} tick=(\d+) racer=0 timer=(\d+) '
                    r'mode=(\d+) row=(\d+) remaining=(\d+) inverse_x=(\d+) inverse_y=(\d+)',log)}
            if not all(start+i in held for i in range(129)):continue
            check(all(held[start+i][0]==0x7000-i for i in range(128)) and
                  held[start][1]==5 and any(held[start+i][1]==6 for i in range(128)) and
                  held[start+128][0]==0 and held[start+128][1]==7 and held[start+128][3]==111,
                  'Thwomp changed128-held-turn timer or imported flatten/recovery sequence')
            recover=re.findall(rf'3D source thwomp recover: course={course} tick=(\d+) racers=(\d+)',log)
            if any(int(t)==start+239 and int(mask)&1 for t,mask in recover):complete=True
        check(complete,'Thwomp omitted imported112-turn recovery row and accepted cue133')
        for song in (169,133,384):
            check(re.search(rf'3D item cue: course=\d+ tick=\d+ song={song} player=\d+ volume=\d+',log),
                  f'Thwomp omitted actual mixer cue{song}')


def verify_thwomp_pictures(frames):
    counts=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        fw,fh,rgb=read_ppm(picture)
        counts.append(sum(rgb[i:i+3]==bytes((80,124,136))
            for y in range(30,fh-20) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    check(counts and max(counts)>=8 and sum(count>0 for count in counts)>=2,
          'Thwomp model absent from two actual world captures')


def verify_driver_art(log, source, models):
    counts = re.findall(r'3D driver art: source=(\d+) models=(\d+)', log)
    check((str(source), str(models)) in counts,
          f'racing roster did not retain source-art/model priority {source}/{models}')


def kart_seating_rows(log):
    """Decode actual draw telemetry; malformed/nonfinite rows cannot disappear.

    >>> row=kart_seating_rows('3D kart seating: course=1 racer=0 tick=2 art=model foot=1,2,0 normal=0,0,1 gap=0 grounded=1 shadow=1')[0]
    >>> row['art'],row['foot'],row['grounded'],row['shadow']
    ('model', (1.0, 2.0, 0.0), 1, 1)
    >>> kart_seating_rows('3D kart seating: course=1 foot=nan')
    Traceback (most recent call last):
    ...
    AssertionError: malformed kart seating draw telemetry
    """
    number=r'[-+]?\d+(?:\.\d+)?'
    pattern=(r'3D kart seating: course=(\d+) racer=(\d+) tick=(\d+) art=(model|sprite) '
        rf'foot=({number}),({number}),({number}) normal=({number}),({number}),({number}) '
        rf'gap=({number}) grounded=([01]) shadow=([01])')
    matches=list(re.finditer(pattern,log))
    check(len(matches)==log.count('3D kart seating:'),'malformed kart seating draw telemetry')
    rows=[]
    for m in matches:
        v=m.groups()
        row=dict(course=int(v[0]),racer=int(v[1]),tick=int(v[2]),art=v[3],
                 foot=tuple(map(float,v[4:7])),normal=tuple(map(float,v[7:10])),
                 gap=float(v[10]),grounded=int(v[11]),shadow=int(v[12]))
        check(all(math.isfinite(x) for x in (*row['foot'],*row['normal'],row['gap'])),
              'nonfinite kart seating telemetry')
        rows.append(row)
    return rows


def chase_camera_rows(log):
    """Parse actual rendered camera snapshots, preserving both clock domains.

    >>> line='3D chase camera: course=1 racer=0 tick=4 camera_tick=4 draw=9 eye=0,-5,3 target=0,0,1 right_z=0 focal=1 swing=0 grounded=1 boosting=0'
    >>> row=chase_camera_rows(line)[0]
    >>> row['tick'],row['camera_tick'],row['draw'],row['eye']
    (4, 4, 9, (0.0, -5.0, 3.0))
    >>> chase_camera_rows(line.replace('focal=1','focal=nan'))
    Traceback (most recent call last):
    ...
    AssertionError: malformed chase camera draw telemetry
    >>> chase_camera_rows(line.replace('boosting=0','boosting=2'))
    Traceback (most recent call last):
    ...
    AssertionError: malformed chase camera draw telemetry
    """
    number=r'[-+]?\d+(?:\.\d+)?'
    pattern=(r'3D chase camera: course=(\d+) racer=(\d+) tick=(\d+) '
        r'camera_tick=(\d+) draw=(\d+) '
        rf'eye=({number}),({number}),({number}) target=({number}),({number}),({number}) '
        rf'right_z=({number}) focal=({number}) swing=({number}) grounded=([01]) boosting=([01])(?!\d)')
    matches=list(re.finditer(pattern,log))
    check(len(matches)==log.count('3D chase camera:'),'malformed chase camera draw telemetry')
    rows=[]
    for match in matches:
        v=match.groups()
        row=dict(course=int(v[0]),racer=int(v[1]),tick=int(v[2]),camera_tick=int(v[3]),draw=int(v[4]),
            eye=tuple(map(float,v[5:8])),target=tuple(map(float,v[8:11])),right_z=float(v[11]),
            focal=float(v[12]),swing=float(v[13]),grounded=int(v[14]),boosting=int(v[15]))
        check(all(math.isfinite(x) for x in (*row['eye'],*row['target'],row['right_z'],row['focal'],row['swing'])),
              'nonfinite chase camera telemetry')
        rows.append(row)
    return rows


def verify_chase_camera(log,handling=False):
    rows=chase_camera_rows(log)
    check(rows and all(r['course']==65536 and r['racer']==0 for r in rows),
          'camera route omitted its selected racer snapshots')
    check(all(r['camera_tick']<=r['tick'] for r in rows),
          'camera advanced beyond its accepted racer clock')
    check(all(abs(r['right_z'])<=1e-6 for r in rows),
          'chase horizon rolled with the kart support')
    check(all(.7-1e-6<=r['focal']<=1+1e-6 and abs(r['swing'])<=.650001 for r in rows),
          'chase focal scale or drift swing exceeded its declared bounds')
    if handling:
        def interval(first,last):
            values=[r for r in rows if first<=r['tick']<=last]
            check(values,f'camera omitted accepted handling interval{first}..{last}')
            return values
        straight=interval(300,389)
        # Speed alone widens to 1/(1+.12*1.25); boosts widen further.
        check(all(r['focal']>=.86 and abs(r['swing'])<=.01 for r in straight),
              'ordinary straight retained drift swing or boost widening')
        drift=interval(500,609)
        check(any(r['grounded'] and abs(r['swing'])>=.15 for r in drift),
              'held drift did not produce its bounded camera swing')
        boosted=interval(625,700)
        check(any(r['boosting'] and r['focal']<=.97 for r in boosted),
              'accepted release boost did not widen the camera')
        check(all(abs(r['swing'])<=.065 for r in interval(835,849)),
              'camera drift swing did not settle after release')
        check(all(not r['boosting'] and r['focal']>=.99 for r in interval(900,1000)),
              'camera did not recover its normal focal scale after boost')
    # Rendering may interpolate multiple views of the same accepted owners.
    # Immutable same-tick calls and pause/cut semantics are native helper tests;
    # do not mistake two interpolation fractions for a controller advance.
    print(f'3D chase camera proof: handling={int(handling)} rows={len(rows)} '+
          f'focal={min(r["focal"] for r in rows):.6f}..{max(r["focal"] for r in rows):.6f} '+
          f'swing={max(abs(r["swing"]) for r in rows):.6f}',flush=True)
    return rows


def kart_animation_rows(log):
    """Read actual per-pane body animation, including its independent owner.

    >>> line='3D kart animation: course=1 racer=0 pane=0 tick=2 draw=3 art=sprite lean=0 bob=0 scale=1,1,1 angle=63 mirror=1 override=0 steer=0 speed=0 spark=0 flame=0 tier=0 active=1 owner=0 lift=0'
    >>> row=kart_animation_rows(line)[0]
    >>> row['art'],row['scale'],row['angle'],row['owner']
    ('sprite', (1.0, 1.0, 1.0), 63, 0)
    >>> kart_animation_rows(line.replace('lean=0','lean=nan'))
    Traceback (most recent call last):
    ...
    AssertionError: malformed kart animation telemetry
    """
    number=r'[-+]?\d+(?:\.\d+)?'
    pattern=(r'3D kart animation: course=(\d+) racer=(\d+) pane=(\d+) tick=(\d+) draw=(\d+) art=(model|sprite) '
        rf'lean=({number}) bob=({number}) scale=({number}),({number}),({number}) '
        r'angle=(\d+) mirror=(\d+) override=(\d+) steer=(-?\d+) speed=(-?\d+) '
        rf'spark=(\d+) flame=(\d+) tier=(\d+) active=([01]) owner=(\d+) lift=({number})')
    matches=list(re.finditer(pattern,log))
    check(len(matches)==log.count('3D kart animation:'),'malformed kart animation telemetry')
    rows=[]
    for match in matches:
        v=match.groups();row=dict(zip(('course','racer','pane','tick','draw'),map(int,v[:5])))
        row.update(art=v[5],lean=float(v[6]),bob=float(v[7]),scale=tuple(map(float,v[8:11])))
        row.update(zip(('angle','mirror','override','steer','speed','spark','flame','tier','active','owner'),map(int,v[11:21])))
        row['lift']=float(v[21])
        check(all(math.isfinite(x) for x in (row['lean'],row['bob'],row['lift'],*row['scale'])),
              'nonfinite kart animation telemetry')
        rows.append(row)
    return rows


def verify_kart_animation_ghost(log):
    """Verify a real loaded ghost has its own accepted animation owner.

    Public helper for replaying this oracle against a retained short relaunch
    log; it does not launch an executable or modify recorded state.
    """
    rows=kart_animation_rows(log)
    human={r['draw']:r for r in rows if r['owner']==0 and r['racer']==0 and r['pane']==0}
    ghost={r['draw']:r for r in rows if r['owner']==1 and r['racer']==0 and r['pane']==0}
    paired=human.keys()&ghost.keys()
    check(ghost and paired,'loaded ghost omitted its independent animation owner alongside the human')
    active={d for d in paired if human[d]['active'] and ghost[d]['active']}
    check(active,'loaded ghost omitted simultaneous active animation owners')
    fields=('steer','speed','lean','bob','scale','spark','flame','tier')
    check(any(any(human[draw][field]!=ghost[draw][field] for field in fields) for draw in active),
          'ghost animation reused the live human input history')
    print(f'3D ghost animation ownership: PASS paired_draws={len(paired)} independent accepted motion',flush=True)


def kart_fx_rows(log):
    pattern=(r'3D kart FX: course=(\d+) racer=(\d+) tick=(\d+) draw=(\d+) '
             r'sparks=(\d+) flames=(\d+) tier=(\d+) owner=(\d+)')
    matches=list(re.finditer(pattern,log))
    check(len(matches)==log.count('3D kart FX:'),'malformed rendered kart FX telemetry')
    return [dict(zip(('course','racer','tick','draw','sparks','flames','tier','owner'),map(int,m.groups())))
            for m in matches]


def kart_fx_lifetimes(pack):
    # Read the same imported FXC1 declarations consumed by the native adapter.
    # These are original cell durations, not a test-only particle clock.
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
    import track_format
    sections=track_format.load_pack(pack);result=[]
    for identity,count in ((1,5),(2,12)):
        check((19,identity) in sections,'P6 route requires source spark and flame artwork')
        _,payload=sections[19,identity]
        check(len(payload)>=32+count*8 and payload[:4]==b'FXC1'
              and struct.unpack_from('<I',payload,12)[0]==count,'invalid source FXC1 duration metadata')
        durations=[struct.unpack_from('<H',payload,38+i*8)[0] for i in range(count)]
        check(all(1<=d<=255 for d in durations),'invalid source FX durations')
        result.append(2+sum(d+1 for d in durations))
    return tuple(result)


def verify_kart_animation(log,art,lifetimes=None,handling=False):
    all_rows=kart_animation_rows(log)
    check(all_rows,'route omitted actual kart animation snapshots')
    check(all(0<=r['angle']<=127 and r['mirror'] in (0,1) and 0<=r['override']<=255
              and abs(r['lean'])<=.180001 and 0<=r['bob']<=.025001 and r['lift']>=0
              and all(.8<=v<=1.12 for v in r['scale']) for r in all_rows),
          'kart animation exceeded source-pose or deformation bounds')
    rows=[r for r in all_rows if r['racer']==0 and r['owner']==0 and r['pane']==0]
    check(rows and all(r['art']==art for r in rows) and any(r['active'] for r in rows),
          'route omitted its actual selected animation art')
    by_tick={r['tick']:r for r in rows}
    ghosts=[r for r in all_rows if r['owner']==1]
    if ghosts:
        human_draws={r['draw'] for r in rows}
        check(any(ghost['draw'] in human_draws for ghost in ghosts),
              'ghost animation was not published alongside its independent human owner')
    if handling:
        check(all(t in by_tick and by_tick[t]['active'] for t in range(180,850)),
              'animation omitted accepted handling turns')
        drift=[by_tick[t] for t in range(430,610)]
        check(any(r['lift']>1e-5 for r in drift),'leaned kart omitted opaque/model lower-edge compensation')
        check(max(abs(r['lean']) for r in drift)>=.05 and any(r['steer'] for r in drift),
              'ordinary held drift omitted lean or source steering response')
        check(len({(r['angle'],r['mirror']) for r in drift})>=2,
              'source steering/view selection remained frozen through a real turn')
        check(all(abs(by_tick[t]['lean'])<.01 for t in range(835,850)),
              'kart lean failed to settle after ordinary release')
        check(all(r['bob']==0 for r in rows),'flat smooth pad unexpectedly bobbed')
        fx=[r for r in kart_fx_rows(log) if r['racer']==0 and r['owner']==0]
        check(fx and lifetimes,'route omitted source-derived FX meshes/lifetimes')
        body_by_draw={r['draw']:r for r in rows}
        for row in fx:
            check(row['draw'] in body_by_draw,'FX omitted corresponding body snapshot')
            body=body_by_draw[row['draw']]
            expected=(2 if 2<=body['spark']<lifetimes[0] else 0,
                      1 if 2<=body['flame']<lifetimes[1] else 0)
            check((row['sparks'],row['flames'])==expected,'rendered FX disagreed with source age/cell visibility')
        check(any(430<=r['tick']<610 and r['sparks']==2 for r in fx),
              'ordinary grounded drift emitted no real source spark pair')
        check(any(620<=r['tick']<745 and r['flames']==1 for r in fx),
              'ordinary release boost emitted no real source flame mesh')
        check(all(not r['sparks'] and not r['flames'] for r in fx if 835<=r['tick']<850),
              'stopped kart retained drift or boost effects')
    return by_tick


def verify_rough_animation(log,save):
    rows=verify_kart_animation(log,'sprite')
    for segment in ('tick=180 count=240 steering=0 buttons=1','tick=420 count=480 steering=0 buttons=0'):
        check(segment in log,'rough pad omitted ordinary accelerator/coast input')
    moving=[r for t,r in rows.items() if 300<=t<420]
    check(moving and max(r['bob'] for r in moving)>.002,'rough surface produced no actual moving bob')
    check(all(r['bob']<=.025*.4*min(1,abs(r['speed'])/(24*65536))+1e-6 for r in moving),
          'rough bob exceeded actual speed-ratio amplitude')
    # MK8 coasting slows without a hard stop; the bob fades with speed.
    stopped=[r for t,r in rows.items() if t>=600]
    check(stopped and all(0<=r['speed']<65536//2 and
                          r['bob']<=.025*.4*r['speed']/(24*65536)+1e-6 for r in stopped),
          'rough bob continued after ordinary neutral stop')
    check('3D Time Trial finish:' not in log and not list(save.rglob('*.ghost')),
          'short rough pad unexpectedly claimed a race finish')
    print(f'3D rough kart animation: PASS speed-ratio bob peak={max(r["bob"] for r in moving):.9f}',flush=True)


def bridge_triangles(source):
    # Match the compiler's actual Q16 collision vertices, including transforms.
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
    import course_3d_format as wire
    course=json.loads((source/'courses3d/bridge/course.json').read_text())
    scene=wire.Gltf(source/'courses3d/bridge',course['scene'])
    result=[]
    for node in course['collision_nodes']:
        for points,_,_,triangles,_ in scene.primitives(node['node']):
            vertices=[tuple(v/65536 for v in wire.position(p,'camera collision vertex')) for p in points]
            for ids in triangles:
                result.append(dict(node=node['node'],vertices=tuple(vertices[i] for i in ids)))
    return result


def bridge_floors(source):
    floors=[]
    for triangle in bridge_triangles(source):
        a,b,c=triangle['vertices']
        u=tuple(b[i]-a[i] for i in range(3));v=tuple(c[i]-a[i] for i in range(3))
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
        length=sum(x*x for x in n)**.5
        if not length or n[2]/length<.25:continue
        n=tuple(x/length for x in n);offset=sum(n[i]*a[i] for i in range(3))
        floors.append(dict(node=triangle['node'],a=a,b=b,c=c,normal=n,offset=offset,
                           plane=tuple(round(x,5) for x in (*n,offset))))
    return floors


def camera_triangle_distance(point,triangle):
    """Distance to a finite collision face, including its edges and corners.

    >>> face=((0,0,0),(2,0,0),(0,2,0))
    >>> [camera_triangle_distance(p,face) for p in ((.5,.5,1),(1,-1,0),(3,0,0))]
    [1.0, 1.0, 1.0]
    >>> round(camera_triangle_distance((2,2,0),face),6)
    1.414214
    >>> camera_triangle_distance((0,0,2),((0,0,0),)*3)
    2.0
    """
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    a,b,c=triangle;ab,ac,ap=sub(b,a),sub(c,a),sub(point,a)
    normal=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
    nn=dot(normal,normal)
    if nn:
        signed=dot(ap,normal)
        projected=tuple(ap[i]-normal[i]*signed/nn for i in range(3))
        aa,bb,cc=dot(ab,ab),dot(ab,ac),dot(ac,ac)
        determinant=aa*cc-bb*bb
        if determinant>0:
            d,e=dot(projected,ab),dot(projected,ac)
            u,v=(cc*d-bb*e)/determinant,(aa*e-bb*d)/determinant
            if u>=0 and v>=0 and u+v<=1:return abs(signed)/math.sqrt(nn)
    best=float('inf')
    for a,b in ((a,b),(b,c),(c,a)):
        edge,relative=sub(b,a),sub(point,a);square=dot(edge,edge)
        fraction=max(0,min(1,dot(relative,edge)/square)) if square else 0
        delta=tuple(relative[i]-fraction*edge[i] for i in range(3))
        best=min(best,dot(delta,delta))
    return math.sqrt(best)


def camera_vertical_fraction(camera,point):
    """Project using the actual world-level chase basis and viewport focal scale.

    >>> camera=dict(eye=(0,-5,1),target=(0,0,1),focal=1)
    >>> camera_vertical_fraction(camera,(0,0,1))
    0.5
    >>> round(camera_vertical_fraction(camera,(0,0,2)),6)
    0.31
    >>> camera_vertical_fraction(camera,(0,-6,1)) is None
    True
    """
    forward=tuple(b-a for a,b in zip(camera['eye'],camera['target']))
    length=math.hypot(*forward)
    check(length>0,'chase snapshot omitted its viewing direction')
    forward=tuple(x/length for x in forward);horizontal=math.hypot(*forward[:2])
    check(horizontal>0,'chase snapshot omitted its world-level basis')
    right=(-forward[1]/horizontal,forward[0]/horizontal,0)
    up=(-forward[2]*right[1],forward[2]*right[0],
        forward[0]*right[1]-forward[1]*right[0])
    relative=tuple(p-eye for p,eye in zip(point,camera['eye']))
    depth=sum(a*b for a,b in zip(relative,forward))
    if depth<=0:return None
    return .5-.95*camera['focal']*sum(a*b for a,b in zip(relative,up))/depth


def verify_chase_hop(log,source,seating):
    # This is the genuine hop after the roof; lower-road tunnel clearance is
    # checked separately. Never claim this jump occurred beneath the roof.
    wanted={r['tick']:r for r in seating}
    cameras=[r for r in chase_camera_rows(log) if r['draw'] in wanted]
    check(wanted.keys()<={r['draw'] for r in cameras},
          'camera omitted a genuine hop or first landing snapshot')
    triangles=bridge_triangles(source);fractions=[];clearance=float('inf')
    for camera in cameras:
        clearance=min(clearance,*(camera_triangle_distance(camera['eye'],t['vertices']) for t in triangles))
        foot=wanted[camera['draw']]['foot']
        point=(foot[0],foot[1],foot[2]+.4)
        fraction=camera_vertical_fraction(camera,point)
        check(fraction is not None and 0<fraction<1,
              'actual airborne kart or first landing left the chase frame')
        fractions.append(fraction)
    check(clearance>=.18-2e-6,'actual hop camera eye penetrated collision sphere clearance')
    print(f'3D chase hop framing: samples={len(cameras)} clearance={clearance:.6f} '+
          f'vertical={min(fractions):.6f}..{max(fractions):.6f}',flush=True)


def verify_kart_seating(log,source,art,tunnel=False):
    camera=verify_chase_camera(log)
    verify_kart_animation(log,art)
    rows=[r for r in kart_seating_rows(log) if r['course']==65536 and r['racer']==0]
    check(rows and all(r['art']==art for r in rows),'kart seating omitted selected model/source owner')
    floors=bridge_floors(source)
    def overhead(f,p):
        # Inclusive projected triangle admission, with only float-print reserve.
        corners=(f['a'],f['b'],f['c']);signs=[]
        for a,b in zip(corners,corners[1:]+corners[:1]):
            signs.append((b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0]))
        if not (min(signs)>=-1e-4 or max(signs)<=1e-4):return None
        n=f['normal']
        return (f['offset']-n[0]*p[0]-n[1]*p[1])/n[2]
    required={f['plane'] for f in floors if f['node']=='DeckCollision'}
    check(len(required)>=4,'bridge fixture lost flat, two distinct ramps or upper deck')
    witnesses={};lower=[]
    for row in rows:
        if not row['grounded']:continue
        check(abs(row['gap'])<=1e-4 and row['shadow']==1,
              'grounded kart separated from its actual support/shadow')
        check(abs(sum(x*x for x in row['normal'])-1)<=1e-4 and row['normal'][2]>.25,
              'grounded kart omitted its unit support normal')
        p=row['foot']
        for f in floors:
            height=overhead(f,p)
            if f['node']=='DeckCollision' and height is not None and abs(p[2]-height)<=1e-4 \
                    and sum(a*b for a,b in zip(row['normal'],f['normal']))>.9999:
                witnesses.setdefault(f['plane'],row['tick'])
                roof=any(q['node']=='TunnelCollision' and (h:=overhead(q,p)) is not None
                         and h>p[2]+1 for q in floors)
                upper=any(q['node']=='DeckCollision' and (h:=overhead(q,p)) is not None
                          and h>p[2]+1 for q in floors)
                if roof and upper:lower.append(row['tick'])
    if tunnel:
        check(len(set(lower))>=2,'ordinary tunnel traversal did not retain the lower floor under roof/bridge')
        eyes=[r for r in camera if r['draw'] in lower]
        check(len({r['draw'] for r in eyes})>=2,'camera omitted actual lower-tunnel snapshots')
        triangles=bridge_triangles(source)
        clearance=min(camera_triangle_distance(r['eye'],t['vertices']) for r in eyes for t in triangles)
        # Vertices above are exact encoded collision coordinates. Six-decimal
        # telemetry can move a 3D point by at most sqrt(3)*0.5 micrometres.
        check(clearance>=.18-2e-6,'actual tunnel camera eye penetrated collision sphere clearance')
        print(f'3D chase tunnel clearance: art={art} minimum={clearance:.6f}',flush=True)
    else:
        check(required<=witnesses.keys(),f'{art} captures omitted a flat/ramp/upper-bridge support plane')
    print(f'3D kart seating proof: art={art} tunnel={int(tunnel)} '+
          f'planes={len(witnesses)} ticks={sorted(set(lower)) if tunnel else sorted(witnesses.values())}',flush=True)


def verify_kart_tunnel(root,runner,pack,work,common):
    # A separate ordinary spawn explores the existing lower road. The lap route,
    # original geometry, checkpoints and physics remain byte-for-byte authored.
    original=work/'source/bridge-course'
    for art in ('model','sprite'):
        identity='grounding-tunnel-'+art
        source=work/'tunnel-source'/art
        shutil.copytree(original,source)
        path=source/'mod.json';document=json.loads(path.read_text());document['id']=identity
        path.write_text(json.dumps(document,indent=2)+'\n')
        path=source/'courses3d/bridge/course.json';document=json.loads(path.read_text())
        document['grid'][0]['position']=[0,.2,10]
        path.write_text(json.dumps(document,indent=2)+'\n')
        if art=='sprite':
            path=source/'models.json';document=json.loads(path.read_text())
            for row in document['bindings']:
                if row['kind']=='driver' and row['target']==2:row['target']=1
            path.write_text(json.dumps(document,indent=2)+'\n')
        mods=work/('tunnel-mods-'+art)
        subprocess.run([sys.executable,str(root/'tools/portable/mod_kit.py'),'build',str(source),
                        '--output',str(mods)],check=True)
        env=dict(common,MKSC_DATA_DIR=str(work/('tunnel-player-'+art)),
                 MKSC_AID_MESH_INPUTS='180:112:0:1;292:1:0:5;293:487:0:1')
        _,log=runner.run('tunnel-'+art,['--menu-tour',pack,work/('tunnel-save-'+art),
            'SDAAUAAEA..'+'T'*6+'S.DDA.B.',work/('tunnel-frames-'+art),
            '--mods-dir',mods,'--mod',identity,'--no-classic-menus','--short-transitions'],**env)
        check('3D input segment: course=65536 mode=2 player=0 tick=180 count=112 steering=0 buttons=1' in log,
              'tunnel capture did not admit its ordinary live accelerator segment')
        verify_kart_seating(log,source,art,tunnel=True)
        # The ordinary hop after the lower corridor must be visible, then settle again.
        # Draw telemetry comes from the shipped kart transform, not the pilot.
        check('tick=292 count=1 steering=0 buttons=5' in log,
              'tunnel hop omitted its ordinary input edge')
        rows=kart_seating_rows(log)
        # Draw ticks include menu admission; input ticks belong to the racer.
        # Measure the actual unsupported interval after initial spawn settling.
        settled=next(r['tick'] for r in rows if r['grounded'])
        first_air=next((i for i,r in enumerate(rows) if r['tick']>settled
                        and not r['grounded'] and r['shadow'] and r['gap']>0),None)
        check(first_air is not None,'rendered tunnel omitted its hop')
        landing=next((r for r in rows[first_air:] if r['grounded']),None)
        check(landing is not None,'rendered tunnel hop did not land')
        # Measure this hop only; the later road-end fall and respawn are separate.
        airborne=[r for r in rows[first_air:] if r['tick']<landing['tick']]
        check(airborne and all(not r['grounded'] and r['shadow'] for r in airborne)
              and .14<=max(r['gap'] for r in airborne)<=.20,
              'rendered tunnel hop missed its modern height target')
        # MK8 hop: 1.1 u/f against 0.15 u/f^2 lasts about 14.7 ticks.
        check(landing and 13<=landing['tick']-airborne[0]['tick']+1<=16
              and abs(landing['foot'][2])<1e-4,
              'rendered tunnel hop did not return promptly to its lower road')
        verify_chase_hop(log,source,airborne+[landing])
        poses={r['draw']:r for r in kart_animation_rows(log) if r['owner']==0 and r['racer']==0}
        hop=[poses[r['tick']] for r in airborne if r['tick'] in poses]
        check(hop and any(p['scale'][2]<.99 and p['scale'][0]>1 for p in hop),
              'genuine hop omitted shared body squash/stretch')
        landed=poses.get(landing['tick'])
        check(landed and landed['scale'][2]<.99 and landed['scale'][0]>1,
              'genuine landing omitted shared body squash')
        check('3D Time Trial finish:' not in log and
              not list((work/('tunnel-save-'+art)).rglob('*.ghost')),
              'short tunnel capture unexpectedly claimed/published a race finish')


def verify_kart_handling(root,runner,pack,work,common):
    # Original flat driving pad, authored through the normal mod compiler.
    # Its recorded inputs exercise handling without replacing the simulation.
    source=work/'handling-source'
    shutil.copytree(work/'source/bridge-course',source)
    path=source/'mod.json';document=json.loads(path.read_text());document['id']='modern-driving-pad'
    path.write_text(json.dumps(document,indent=2)+'\n')
    folder=source/'courses3d/bridge'
    points=[(-160,0,-160),(-160,0,160),(160,0,160),(160,0,-160)]
    binary=b''.join(struct.pack('<3f',*p) for p in points)
    binary+=struct.pack('<12f',*((0,1,0)*4))+struct.pack('<8f',0,0,0,40,40,40,40,0)
    binary+=struct.pack('<6H',0,1,2,0,2,3)
    (folder/'pad.bin').write_bytes(binary)
    gltf=dict(asset=dict(version='2.0'),scene=0,scenes=[dict(nodes=[0])],
        nodes=[dict(name='DrivingPad',mesh=0)],
        meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2),indices=3,material=0)])],
        buffers=[dict(uri='pad.bin',byteLength=len(binary))],
        bufferViews=[dict(buffer=0,byteOffset=o,byteLength=n) for o,n in ((0,48),(48,48),(96,32),(128,12))],
        accessors=[dict(bufferView=i,componentType=5123 if i==3 else 5126,
            count=6 if i==3 else 4,type='SCALAR' if i==3 else 'VEC2' if i==2 else 'VEC3') for i in range(4)],
        images=[dict(uri='deck.png')],textures=[dict(source=0)],
        materials=[dict(doubleSided=True,pbrMetallicRoughness=dict(baseColorTexture=dict(index=0)))])
    (folder/'pad.gltf').write_text(json.dumps(gltf)+'\n')
    path=folder/'course.json';document=json.loads(path.read_text())
    document.update(scene='pad.gltf',render_nodes=['DrivingPad'],
        collision_nodes=[dict(node='DrivingPad',surface=0,respawn=0)],objects=[],items=[],zones=[])
    document['meta'].update(name='Modern Driving Pad',modes=['tt'])
    document['grid'][0]['position']=[0,0,0]
    for route in document['routes']:
        for node in route['nodes']:node['position'][1]=0
    for gate in document['checkpoints']:gate['center'][1]=0
    for reset in document['respawns']:reset['position'][1]=0
    path.write_text(json.dumps(document,indent=2)+'\n')
    segments='180:210:0:1;390:220:32767:5;610:150:0:1;760:500:0:0'
    collected={}
    for art in ('model','sprite','rough'):
        candidate=source if art=='model' else work/('handling-source-'+art)
        if candidate!=source:shutil.copytree(source,candidate)
        identity='modern-driving-pad-'+art
        path=candidate/'mod.json';document=json.loads(path.read_text());document['id']=identity
        path.write_text(json.dumps(document,indent=2)+'\n')
        if art!='model':
            path=candidate/'models.json';document=json.loads(path.read_text())
            for binding in document['bindings']:
                if binding['kind']=='driver' and binding['target']==2:binding['target']=1
            path.write_text(json.dumps(document,indent=2)+'\n')
        if art=='rough':
            path=candidate/'courses3d/bridge/course.json';document=json.loads(path.read_text())
            document['surfaces'][0]['speed_ratio']=.6
            path.write_text(json.dumps(document,indent=2)+'\n')
        mods=work/('handling-mods-'+art)
        subprocess.run([sys.executable,str(root/'tools/portable/mod_kit.py'),'build',str(candidate),
                        '--output',str(mods)],check=True)
        actual_segments=segments if art!='rough' else '180:240:0:1;420:480:0:0'
        name='handling-'+art
        _,log=runner.run(name,['--menu-tour',pack,work/(name+'-save'),
            'SDAAUAAEA..'+'T'*(16 if art=='rough' else 26)+'S.DDA.B.',work/(name+'-frames'),
            '--mods-dir',mods,'--mod',identity,'--no-classic-menus','--short-transitions',
            # Source spark/flame sprites are what the parity check compares.
            '--no-drift-sparks','--no-boost-effects'],
            **dict(common,MKSC_DATA_DIR=str(work/(name+'-player')),
                   MKSC_AID_MESH_INPUTS=actual_segments,MKSC_AID_MESH_TRACE='180:1000'))
        if art=='rough':
            verify_rough_animation(log,work/(name+'-save'))
        else:
            physics=verify_handling_controls(log,segments,work/(name+'-save'))
            animation=verify_kart_animation(log,art,kart_fx_lifetimes(pack),handling=True)
            collected[art]=(physics,animation)
    model,sprite=collected['model'],collected['sprite']
    check(model[0]==sprite[0],'model/source pad changed accepted handling physics')
    shared=set(model[1])&set(sprite[1])
    check(all(t in shared for t in range(180,850)),'model/source animation comparison omitted accepted turns')
    fields=('lean','bob','scale','angle','mirror','override','steer','speed','spark','flame','tier','active')
    check(all(all(model[1][tick][field]==sprite[1][tick][field] for field in fields) for tick in shared),
          'model/source views did not share the accepted animation pose')
    print('3D kart animation model/source parity: PASS actual handling and source pose fields',flush=True)


def verify_handling_controls(log,segments,save):
    for segment in segments.split(';'):
        tick,count,steering,buttons=segment.split(':')
        check(f'tick={tick} count={count} steering={steering} buttons={buttons}' in log,
              'driving pad omitted an ordinary input segment')
    rows=[]
    for line in log.splitlines():
        if line.startswith('3D aid trace:'):
            values=dict(re.findall(r'(\w+)=([^ ]+)',line))
            row={k:int(values[k]) for k in ('tick','grounded','speed','outside','drift','charge','tier')}
            row['boosts']=tuple(map(int,values['boosts'].split(',')))
            row['pos']=tuple(map(int,values['pos'].split(',')))
            row['forward']=tuple(map(int,values['forward'].split(',')))
            rows.append(row)
    by_tick={r['tick']:r for r in rows}
    check(len(by_tick)==len(rows) and all(t in by_tick for t in range(180,850)),
          'driving pad omitted its actual accepted state trace')
    # MK8 speed core: about 89% of the 24 m/s cap after 209 driving ticks.
    check(by_tick[389]['speed']>=21*65536,'driving pad did not reach normal speed')
    drift=[by_tick[t] for t in range(430,610)]
    check(all(r['grounded'] and r['drift']==1 and r['speed']>=20*65536 for r in drift),
          'held drift lost support, direction or forward travel speed')
    check(max(r['tier'] for r in drift)==3 and by_tick[610]['tier']==3,
          'held drift did not charge all authored boost tiers')
    check(any(r['boosts'][2]>0 for r in rows if 611<=r['tick']<=625),
          'drift release did not activate its third-tier boost')
    check(abs(by_tick[629]['outside'])<=5461*.1,
          'drift release retained its old outside angle past0.3 seconds')
    # MK8 mini-turbo cap: about 1.1x the 24 m/s cap.
    check(max(by_tick[t]['speed'] for t in range(620,650))>=26*65536,
          'released mini-turbo did not reach its modern speed target')
    # MK8 coasting loses 2% a tick: a slow, monotone stop that never reverses.
    coast=[by_tick[t] for t in range(761,max(by_tick)+1)]
    check(len(coast)>=190 and all(0<=b['speed']<=a['speed'] for a,b in zip(coast,coast[1:]))
          and coast[-1]['speed']<65536//2,
          'neutral controls failed to stop or crept through zero')
    check('3D Time Trial finish:' not in log and not list((save).rglob('*.ghost')),
          'short driving pad scenario unexpectedly claimed a race finish')
    cameras=verify_chase_camera(log,handling=True)
    lag=[]
    for camera in cameras:
        tick=camera['camera_tick']
        if camera['tick']!=tick or not 300<=tick<=389 or tick not in by_tick:continue
        kart=by_tick[tick];direction=kart['forward'][:2];length=math.hypot(*direction)
        check(length>0,'camera lag witness omitted planar accepted heading')
        desired=tuple(kart['pos'][i]/65536-3.9*direction[i]/length for i in range(2))
        lag.append(math.dist(camera['eye'][:2],desired))
    check(lag and .1<=min(lag)<=max(lag)<=4,
          'camera did not retain bounded planar lag on its accepted straight')
    print(f'3D chase straight lag: minimum={min(lag):.6f} maximum={max(lag):.6f}',flush=True)
    print('3D handling controls: PASS actual acceleration drift tier3 release boost neutral stop',flush=True)
    return by_tick


def bridge_tt_start(root,ghost):
    # The builder copies this authored profile; revisions change with tuning.
    profile=json.loads((root/'portable/data/mesh-kart.json').read_text())
    return (f"3D Time Trial start: course=65536 driver=2 profile={profile['profile_id']} "
            f"revision={profile['revision']} ghost={ghost}")


def verify_tt_source_fallback(root, runner, pack, work, common, crabs=False, snowmen=False, penguins=False, mice=False, piranhas=False, seagulls=False, lava_bubbles=False, thwomps=False, cannonballs=False, boos=False, rocks=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False, sky_motion=False, kart_grounding=False):
    # Recompile only the optional authored binding registry into a separate
    # mod directory; modeled-route saves and compiled content remain untouched.
    source = work/'source'/('bridge-course'+('-crabs' if crabs else '')+('-snowmen' if snowmen else '')+('-penguins' if penguins else '')+('-mice' if mice else '')+('-piranhas' if piranhas else '')+('-seagulls' if seagulls else '')+('-lava-bubbles' if lava_bubbles else '')+('-thwomps' if thwomps else '')+('-cannonballs' if cannonballs else '')+('-boos' if boos else '')+('-rocks' if rocks else '')+('-coins' if coins else '')+('-teepees' if teepees else '')+('-star' if teepees_star else '')+('-falling-stars' if falling_stars else '')+('-star' if falling_stars_star else '')+('-thunderclouds' if thunderclouds else '')+('-star' if thunderclouds_star else '')+('-common-props' if common_props else '')+('-star' if common_props_star else '')+('-mechakoopas' if mechakoopas else '')+('-star' if mechakoopas_star else '')+('-kamek' if kamek else '')+('-star' if kamek_star else '')+('-pier-boos' if pier_boos else '')+('-star' if pier_boos_star else '')+('-sky-motion' if sky_motion else ''))
    path = source/'models.json'
    original = path.read_bytes()
    document = json.loads(original)
    check(any(row['kind'] == 'driver' and row['target'] == 2 for row in document['bindings']),
          'TT fixture lost its authored driver model before the fallback test')
    # Version1 registries must retain at least one binding. Move only this
    # fixture's driver2 binding to the unselected stock driver1 instead of
    # emitting an invalid empty registry; every mesh/material stays unchanged.
    for row in document['bindings']:
        if row['kind'] == 'driver' and row['target'] == 2:
            row['target'] = 1
    try:
        path.write_text(json.dumps(document, indent=2)+'\n')
        subprocess.run([sys.executable, str(root/'tools/portable/mod_kit.py'), 'build',
                        str(source), '--output', str(work/'source-art-mods')], check=True)
    finally:
        path.write_bytes(original)
    environment = dict(common, MKSC_DATA_DIR=str(work/'source-art-player'), MKSC_AID_MESH_VIEWS='1')
    environment.pop('MKSC_AID_MESH_SUBFRAMES', None)
    tour = ('SDAAUAAEA.V'+'T'*50+'G.B..' if kart_grounding else 'SDAAUAAEA...S.A...')
    _, log = runner.run('source-art', ['--menu-tour', pack, work/'source-art-save', tour,
        work/'source-art-frames', '--mods-dir', work/'source-art-mods', '--mod', 'bridge-course',
        '--no-classic-menus', '--short-transitions', '--internal-scale', '1'], **environment)
    verify_driver_art(log, 1, 0)
    check(bridge_tt_start(root,0) in log,
          'fresh source-art TT did not reach the ordinary race owner')
    # Stock Luigi art has green clothing and dark wheel/outline detail. The
    # authored road is blue/yellow, and this crop excludes HUD/footer text.
    colored = False
    first = len('SDAAUAAEA')
    for index in range(first, first + 3):
        width, height, rgb = read_ppm(work/'source-art-frames'/f'{index:03}.ppm')
        colors = [tuple(rgb[(y*width+x)*3:(y*width+x+1)*3])
                  for y in range(height*52//100, height*85//100)
                  for x in range(width*35//100, width*65//100)]
        green = sum(g > r+24 and g > b+16 for r, g, b in colors)
        dark = sum(max(color) <= 64 for color in colors)
        if green >= 8 and dark >= 8:
            colored = True
            break
    check(colored, 'actual TT fallback never drew colored source kart/wheel detail')
    if kart_grounding:
        check('3D Time Trial finish: course=65536' in log and 'laps=3' in log,
              'grounded source-art inputs did not finish the ordinary TT')
        verify_kart_seating(log,source,'sprite')
        ghosts=list((work/'source-art-save').rglob('*.ghost'))
        check(len(ghosts)==1,'grounded source-art TT omitted its genuine saved ghost')
        _,replay=runner.run('source-art-replay',['--mesh-finished-replay',pack,ghosts[0],
            '--mods-dir',work/'source-art-mods','--mod','bridge-course'],**environment)
        check('3D Time Trial replay: PASS course=65536 driver=2' in replay,
              'grounded source-art controls failed saved replay')
    else:
        check(not list((work/'source-art-save').rglob('*.ghost')),
              'unfinished source-art TT published a ghost')
    if lava_bubbles or rocks:
        # Resolve the selected manifest's graphics, then compare exact imported
        # nonblack bank5 colors against bank0 in the same kart-only viewport ROI.
        spec=importlib.util.spec_from_file_location('source_burn_pack',root/'tools/portable/build_asset_pack.py')
        pack_reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack_reader)
        sections={(header['kind'],header['content_id']):(header,bytes(payload))
                  for header,payload in pack_reader.read_sections(pack)}
        manifest=sections[(2,2)][1]
        check(len(manifest)==24 and struct.unpack_from('<I',manifest)[0]==0x31524843,
              'source burn oracle requires selected driver manifest')
        header,graphic=sections[(10,struct.unpack_from('<I',manifest,12)[0])]
        check(header['version']==3 and len(graphic)>=32 and
              struct.unpack_from('<I',graphic)[0]==0x33524143,
              'source burn oracle requires imported CAR3 palette banks')
        palette_count=struct.unpack_from('<I',graphic,8)[0]
        check(palette_count>=7*16 and palette_count*2<=len(graphic)-32,
              'source burn graphics omit imported palette banks5/6')
        palettes=struct.unpack_from('<'+str(palette_count)+'H',graphic,len(graphic)-palette_count*2)
        def rgb555(value):
            return tuple((((value>>shift)&31)<<3)|(((value>>shift)&31)>>2) for shift in (0,5,10))
        baseline=set(map(rgb555,palettes[1:16]))
        burnt=set(map(rgb555,palettes[5*16+3:6*16]))-baseline-{(0,0,0)}
        check(burnt,'source burn oracle has no distinct nonblack bank5 colors')
        def palette_pixels(picture):
            width,height,rgb=read_ppm(picture)
            return sum(tuple(rgb[(y*width+x)*3:(y*width+x)*3+3]) in burnt
                for y in range(height*52//100,height*85//100)
                for x in range(width*35//100,width*65//100))
        ordinary=max(palette_pixels(work/'source-art-frames'/f'{index:03}.ppm')
                     for index in range(first,first+3))
        _,burn_log=runner.run('source-burn',['--menu-tour',pack,work/'source-burn-save',
            'SDAAUAAEA.V'+'T'*22+'S..S.S.DDA.B.',work/'source-burn-frames',
            '--mods-dir',work/'source-art-mods','--mod','bridge-course',
            '--no-classic-menus','--short-transitions','--internal-scale','1'],
            **dict(environment,MKSC_AID_MESH_HAZARDS='seek'))
        verify_driver_art(burn_log,1,0)
        check('3D renderer: GPU driver=' in burn_log and '3D renderer: CPU' not in burn_log
              and 'GPU UI compositor: driver=' in burn_log and 'full_canvas_upload=0' in burn_log
              and 'GPU UI fallback:' not in burn_log,'source-art burn lost GPU world/UI')
        if rocks: verify_rock_actor_turns(burn_log,{65536})
        else: verify_lava_actor_turns(burn_log,{65536})
        check(re.search(r'3D source lava burn: course=65536 tick=\d+ racer=0 remaining=0',burn_log),
              'source-art burn never completed250 accepted clock turns')
        pauses=re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)',burn_log)
        check('Menu tour: PASS' in burn_log and len(pauses)>=3 and
              pauses[0][0]=='1' and pauses[1][0]=='0' and pauses[0][1]==pauses[1][1] and
              '3D audio return: menu pack restored' in burn_log,
              'source-art burn lost paused-clock freeze or ordinary return')
        pictures=sorted((work/'source-burn-frames').glob('*.ppm'))
        check(pictures and max(map(palette_pixels,pictures))>=max(8,ordinary+8),
              'actual source kart never rendered imported nonblack bank5 palette colors')
        check(not list((work/'source-burn-save').rglob('*.ghost')) and
              not list((work/'source-burn-save').rglob('*.pb')),
              'unfinished source-art burn published TT records')


def verify_sky_pictures(runner, pack, work, arguments, common):
    # Ordinary neutral input keeps the camera fixed. Only accepted sky clocks
    # can move this strip, above the road and below the changing race HUD.
    tour='SDAAUAAEA......S...S..'
    directory=work/'sky-pictures'
    _,log=runner.run('sky-pictures', ['--menu-tour',pack,work/'sky-picture-save',
        tour,directory,*arguments],**common)
    check('Menu tour: PASS' in log,'sky picture tour did not finish')
    pictures=[read_ppm(directory/f'{i:03}.ppm') for i in range(len(tour)+1)]
    def strip(index, left=0):
        width,height,rgb=pictures[index]
        check((width,height)==(320,180),'sky picture lost its physical extent')
        return b''.join(rgb[(y*width+left)*3:(y*width+320)*3] for y in range(30,45))
    check(len({strip(i) for i in range(11,15)})>=3,
          'accepted clock does not visibly animate the stationary GPU sky')
    def paused_strip(index):
        width,_,rgb=pictures[index]
        return b''.join(rgb[(y*width+240)*3:(y*width+320)*3] for y in range(5,24))
    check(paused_strip(17)==paused_strip(18)==paused_strip(19),
          'paused GPU sky changed between identical camera presentations')
    check(strip(20)!=strip(21),
          'GPU sky did not resume after pause')
    _,split=runner.run('sky-four-views', ['--menu-tour',pack,work/'sky-four-view-save',
        'SDAAUAAEA......',work/'sky-four-view-frames',*arguments],
        **dict(common,MKSC_AID_MESH_VIEWS='4'))
    check(re.search(r'3D world target: width=\d+ height=\d+ views=4 scale=1',split)
          and 'Menu tour: PASS' in split,'sky did not submit four independent real cameras')
    width,height,rgb=read_ppm(work/'sky-four-view-frames/013.ppm')
    check((width,height)==(320,180),'four-view sky capture changed physical extent')
    cloud=bytes((235,244,255))
    masks=[]
    for pane in range(4):
        x0=(pane&1)*160; y0=(pane//2)*90
        masks.append(bytes(rgb[((y0+y)*width+x0+x)*3:((y0+y)*width+x0+x+1)*3]==cloud
                           for y in range(5,25) for x in range(160)))
    check(len(set(masks))>=3 and any(any(mask) for mask in masks),
          'four-view sky clouds omitted independent pane yaw')
    print('3D sky pictures: PASS stationary motion pause resume independent panes')


MODEL_OVERLAY_CASES = ('pair','reverse','later','invalid')


def prepare_model_overlays(root,work,case):
    """Build independent ordinary model mods; corrupt only optional geometry in fallback case."""
    sys.path.insert(0,str(root/'tools/portable'))
    import mod_kit
    import build_asset_pack
    def example(name):
        spec=importlib.util.spec_from_file_location(name.replace('-','_'),root/'mods/examples'/name/'build.py')
        module=importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    kart=example('overlay-kart')
    kart.build(work/'mods',driver=2)
    example('overlay-scenery').build(work/'mods')
    names=['overlay-kart','overlay-scenery']
    if case=='reverse':names.reverse()
    if case in ('later','invalid'):
        name='overlay-'+case
        source=work/'source'/name
        source.mkdir(parents=True)
        document=kart.model_document(2)
        document['materials'][0].update(id=131103,argb='#FFFB9125')
        document['meshes'][0].update(id=131003,materials=[131103])
        document['bindings'][0]['mesh']=131003
        (source/'models.json').write_text(json.dumps(document))
        (source/'mod.json').write_text(json.dumps(dict(id=name,name=name,version='1.0.0',
            api_version=1,targets=['2d','3d'])))
        folder,_=mod_kit.build(source,None,work/'mods')
        if case=='invalid':
            sections=list(build_asset_pack.read_sections(folder/'content.pack'))
            with tempfile.TemporaryDirectory(dir=work,prefix='optional-model-') as temporary:
                staging=Path(temporary)
                entries=[]
                for i,(meta,payload) in enumerate(sections):
                    if meta['kind']==96:
                        payload=bytearray(payload)
                        struct.pack_into('<I',payload,4,0xffffffff)
                    path=f'{i}.bin'
                    (staging/path).write_bytes(payload)
                    entries.append(dict(meta,path=path))
                (staging/'pack.json').write_text(json.dumps(dict(sections=entries)))
                build_asset_pack.build_pack(staging/'pack.json',folder/'content.pack')
            list(build_asset_pack.read_sections(folder/'content.pack'))
        names.append(name)
    print('3D model overlay order: '+','.join(names),flush=True)
    return [argument for name in names for argument in ('--mod',name)]


def verify_model_overlay_pictures(frames,case):
    """Exact non-RGB555 authored colors in two world captures, excluding HUD."""
    cyan,pink,orange=bytes((19,199,237)),bytes((241,73,163)),bytes((251,145,37))
    rows=[]
    for picture in frames:
        if int(picture.stem)<len('SDAAUAAEA'):continue
        width,height,pixels=read_ppm(picture)
        counts={color:0 for color in (cyan,pink,orange)}
        for y in range(30,height-12):
            for x in range(4,width-4):
                color=pixels[(y*width+x)*3:(y*width+x)*3+3]
                if color in counts:counts[color]+=1
        rows.append(counts)
    check(rows,'model overlays omitted real race world captures')
    expected=orange if case=='later' else cyan
    check(sum(row[pink]>=8 for row in rows)>=2,'overlay scenery absent from two real world captures')
    if case=='invalid':
        check(all(row[cyan]==row[orange]==0 for row in rows),
              'invalid later kart binding retained an earlier model instead of local fallback')
    else:
        check(sum(row[expected]>=8 and row[pink]>=8 for row in rows)>=2,
              'independent kart/scenery overlays did not visibly coexist in two real world captures')
        if case=='later':
            check(all(row[cyan]==0 for row in rows),'later matching kart overlay did not win')
    print(f'3D model overlay pixels: case={case} frames={len(rows)} pink={max(row[pink] for row in rows)} '+
          f'cyan={max(row[cyan] for row in rows)} orange={max(row[orange] for row in rows)}',flush=True)


def verify(binary, pack, work, gpu_probe=None, video_driver=None, interpolated=False, gles=False, world_scale=1, views=1, benchmark=False, items=False, crabs=False, snowmen=False, penguins=False, mice=False, piranhas=False, seagulls=False, lava_bubbles=False, thwomps=False, cannonballs=False, boos=False, rocks=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False, sky_motion=False, model_overlays=None, ground_follow=False, kart_grounding=False, kart_handling=False):
    check(not kart_grounding or (gpu_probe is not None and not items and not sky_motion
          and model_overlays is None and views==1 and world_scale==1
          and not interpolated and not benchmark),
          'kart grounding requires an isolated standard GPU bridge TT')
    check(model_overlays is None or (model_overlays in MODEL_OVERLAY_CASES and gpu_probe is not None
          and not items and not sky_motion and views==1 and world_scale==1 and not interpolated
          and not benchmark),'model overlays require an isolated standard GPU TT')
    check(not sky_motion or (gpu_probe is not None and not items and views==1 and world_scale==1
          and not interpolated and not benchmark),'sky lifecycle requires a standard GPU sky-only TT')
    if gpu_probe is not None:
        env = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                   SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                   SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
        probe = subprocess.run([str(gpu_probe)], env=env, timeout=30)
        if probe.returncode == 77:
            print('3D GPU route skipped: no GPU device; this is not GPU acceptance')
            return 77
        check(probe.returncode == 0, 'GPU device probe failed its rendering checks')
    root = Path(__file__).resolve().parents[3]
    pack_before = hashlib.sha256(pack.read_bytes()).digest()
    build = [sys.executable, str(root / 'mods/examples/bridge-course/build.py'),
             '--output-dir', str(work / 'mods')]
    if model_overlays:
        build.append('--scenery-binding')
    if sky_motion:
        build.append('--sky-motion')
    if ground_follow:
        build.append('--ground-follow')
    if items:
        build.append('--items')
    if crabs:
        build.append('--crabs')
    if snowmen:
        build.append('--snowmen')
    if penguins:
        build.append('--penguins')
    if mice:
        build.append('--mice')
    if piranhas:
        build.append('--piranhas')
    if seagulls:
        build.append('--seagulls')
    if lava_bubbles:
        build.append('--lava-bubbles')
    if thwomps:
        build.append('--thwomps')
    if cannonballs:
        build.append('--cannonballs')
    if boos:
        build.append('--boos')
    if rocks:
        build.append('--rocks')
    if coins:
        build.append('--coins')
    if teepees:
        build.append('--teepees')
    if teepees_star:
        build.append('--teepees-star')
    if falling_stars:
        build.append('--falling-stars')
    if falling_stars_star:
        build.append('--falling-stars-star')
    if thunderclouds:
        build.append('--thunderclouds')
    if thunderclouds_star:
        build.append('--thunderclouds-star')
    if common_props:
        build.append('--common-props')
    if common_props_star:
        build.append('--common-props-star')
    if mechakoopas:
        build.append('--mechakoopas')
    if mechakoopas_star:
        build.append('--mechakoopas-star')
    if kamek:
        build.append('--kamek')
    if kamek_star:
        build.append('--kamek-star')
    if pier_boos:
        build.append('--pier-boos')
    if pier_boos_star:
        build.append('--pier-boos-star')
    subprocess.run(build, check=True)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    data = work / 'player'
    arguments = ['--mods-dir', work / 'mods', '--mod', 'bridge-course',
                 '--no-classic-menus', '--short-transitions', '--internal-scale', str(world_scale)]
    if model_overlays:
        arguments += prepare_model_overlays(root,work,model_overlays)
    common = dict(MKSC_DATA_DIR=str(data), MKSC_AID_MODERN_MENUS='1',
                  MKSC_MODERN_MENU_SIZE='320x180', MKSC_AID_MESH_VIEWS=str(views))
    if gpu_probe is not None:
        common.update(MKSC_AID_GPU='0' if gles else '1', SDL_VIDEO_DRIVER=video_driver)
        if gles:
            common.update(MKSC_AID_GLES='1')
    if kart_grounding:
        common.update(MKSC_AID_MESH_GROUND='1',MKSC_AID_MESH_CAMERA='1',MKSC_AID_MESH_ANIMATION='1')
    if interpolated:
        check(gpu_probe is not None, 'fractional route requires the GPU device probe')
        common.update(MKSC_AID_MESH_SUBFRAMES='3')
        if not benchmark:
            (work/'race'/'subframes').mkdir(parents=True, exist_ok=True)
            common.update(MKSC_AID_MESH_SUBFRAME_DIR=str(work/'race'/'subframes'))
        arguments += ['--interpolate', '--render-fps', '144']
    # V steers off the road and waits for the actual respawn penalty.
    # G uses authored CPU-route steering/braking inputs through actual fixed ticks;
    # it neither moves the kart nor advances a lap/checkpoint directly.
    # Mixed actors occupy both straights. Capture ordinary reserved-item route
    # pulses through the return leg before G completes the unchanged race.
    race_tour = 'SDAAUAAEA.V' + ('T'*50 if kart_grounding or mice or piranhas or seagulls or lava_bubbles or thwomps or cannonballs or boos or rocks or coins or teepees or falling_stars or thunderclouds or common_props or mechakoopas or kamek or pier_boos or (penguins and (crabs or snowmen)) else '') + 'GDA..S.A..B..'
    # Timed and moving hazards need the aid to meet them, not pass them by:
    # the hazard seek parks on each live actor until it lands a contact.
    # MK8 turning (no tighter than ~1.5 m) misses actors the old turn met, so every
    # contact family is sought.
    seek = (crabs or snowmen or penguins or mice or piranhas or seagulls or lava_bubbles or thwomps or rocks
            or teepees or falling_stars or thunderclouds or common_props or cannonballs or kamek or mechakoopas)
    _, log = runner.run('race', ['--menu-tour', pack, work / 'race-save',
        race_tour, work / 'race-frames', *arguments],
        **dict(common, **({'MKSC_AUDIO_CAPTURE': str(work / 'items.wav')} if items else {}),
               **({'MKSC_AID_MESH_HAZARDS': 'seek'} if seek else {})))
    if gpu_probe is not None:
        check('3D renderer: GPU driver=' in log and '3D renderer: CPU' not in log,
              'ordinary Time Trial did not use the GPU backend')
        check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
              and 'GPU UI fallback:' not in log,
              '3D HUD and overlays did not use the shared GPU UI compositor')
        if gles:
            check('3D renderer: GPU driver=opengles2' in log,
                  'GLES route did not execute the GLES depth backend')
        world_height = max(180, 160 * world_scale * (2 if views == 4 else 1))
        world_width = (320 * world_height + 90) // 180
        expected_target = (f'3D world target: width={world_width} height={world_height} '
                           f'views={views} scale={world_scale}')
        check(expected_target in log, 'GPU world resolution/view count missing: ' + expected_target)
    model_backend = 'GPU' if gpu_probe is not None else 'CPU'
    expected_kart=(0 if model_overlays=='invalid' else 131003 if model_overlays=='later'
                   else 131001 if model_overlays else 1)
    check(f'3D models: backend={model_backend} driver=2 kart={expected_kart} objects={2+int(sky_motion)+int(crabs)+int(snowmen)+int(penguins)+3*int(mice)+2*int(piranhas)+int(seagulls)+int(lava_bubbles)+2*int(thwomps)+2*int(cannonballs)+2*int(boos)+4*int(rocks)+4*int(coins)+int(teepees)+2*int(falling_stars)+4*int(thunderclouds)+6*int(common_props)+2*int(mechakoopas)+int(kamek)+int(pier_boos)} '
          f'billboards={1+int(crabs)+int(snowmen)+int(penguins)+3*int(mice)+2*int(piranhas)+int(seagulls)+int(lava_bubbles)+2*int(thwomps)+2*int(cannonballs)+2*int(boos)+4*int(rocks)+int(teepees)+2*int(falling_stars)+4*int(thunderclouds)+6*int(common_props)+2*int(mechakoopas)+int(kamek)+int(pier_boos)}' in log,
          'shared renderer did not submit authored driver and placed model resources')
    if sky_motion:
        check('3D sky start: course=65536 profile=1 revision=1 controller=1 layers=1' in log,
              'sky-only TT omitted its explicit accepted-clock profile')
        verify_sky_pictures(runner, pack, work, arguments, common)
    verify_driver_art(log,int(model_overlays=='invalid'),int(model_overlays!='invalid'))
    check(bridge_tt_start(root,0) in log,
          'ordinary course selection did not start the profiled 3D Time Trial')
    check('3D Time Trial finish: course=65536' in log and 'laps=3' in log,
          'authored route inputs did not complete all three laps')
    check('3D route aid: natural respawn course=65536' in log,
          'ordinary steering did not fall off the mesh and enter real respawn')
    check('recording=1' in log, 'finished Time Trial lost its input recording')
    finished = re.search(r'3D Time Trial finish: course=65536 ticks=(\d+) laps=3 '
                         r'checksum=([0-9a-f]{16}) recording=1', log)
    check(finished is not None, 'Time Trial finish omitted its recorded deterministic state')
    final_checksum = finished[2]
    if kart_grounding:
        verify_kart_seating(log,work/'source/bridge-course','model')
    if items:
        verify_item_owner(log, 2, 1)
        if coins:
            verify_coins(log,{65536},2,1,require_loss=True)
        verify_item_audio(log, work / 'items.wav', mushroom=True)
        if crabs:
            verify_crab_actor_turns(log,{65536})
            if ground_follow: verify_actor_ground(log,{65536})
        if snowmen:
            verify_snowman_actor_turns(log,{65536},require_two_stages=True)
        if penguins:
            verify_penguin_actor_turns(log,{65536})
        if mice:
            verify_mouse_actor_turns(log,{65536})
        if seagulls:
            verify_seagull_actor_turns(log,{65536})
        if lava_bubbles:
            verify_lava_actor_turns(log,{65536})
        if thwomps:
            verify_thwomp_actor_turns(log,{65536})
        if cannonballs:
            verify_cannonball_actor_turns(log,{65536})
        if boos:
            verify_boo_actor_turns(log,{65536})
        if pier_boos:
            verify_pier_boo_actor_turns(log,{65536})
        if kamek:
            verify_kamek_actor_turns(log,{65536})
        if mechakoopas:
            verify_mechakoopa_actor_turns(log,{65536})
        if common_props:
            verify_common_prop_actor_turns(log,{65536})
        if thunderclouds:
            verify_thundercloud_actor_turns(log,{65536})
        if falling_stars:
            verify_falling_star_actor_turns(log,{65536})
        if teepees:
            verify_teepee_actor_turns(log,{65536},require_haunt=True)
        if rocks:
            verify_rock_actor_turns(log,{65536})
        if piranhas:
            verify_piranha_actor_turns(log,{65536})
            verify_piranha_capture(log,{65536})
            verify_piranha_audio(log,{65536},capture=True)
    else:
        # MK8 kart physics (K1-K9, drift law, crease-riding ground) on the railed bridge; resolution/pane changes must preserve it.
        check('checksum=0856922e659e5ced' in log,
              'world resolution or viewport count changed accepted inputs/final physics')
    if interpolated:
        pacing = re.search(r'3D GPU pacing: subframes=3 between=(\d+) moving=(\d+) immutable=1', log)
        check(pacing is not None and min(map(int, pacing.groups())) > 0,
              'GPU shell did not present moving immutable poses between fixed ticks')
        if not items:
            check('checksum=0856922e659e5ced' in log,
                  'fractional aid changed accepted inputs or final physics state')
    if interpolated and not benchmark:
        captures = re.findall(r'3D subframe capture: tick=(\d+) alpha=(\d+) checksum=([0-9a-f]+)', log)
        groups = {}
        for tick, alpha, checksum in captures:
            groups.setdefault(tick, []).append((alpha, checksum))
        paired = {tick: samples for tick, samples in groups.items() if len(samples) >= 2}
        check(paired, 'fractional route omitted same-tick frame captures')
        moving_pairs = 0
        for tick, samples in paired.items():
            check(len({checksum for _, checksum in samples}) == 1, 'captured subframes advanced simulation')
            pictures = list((work/'race').rglob(f'mesh-subframe-{tick}-*.ppm'))
            check(len(pictures) >= 2, f'missing captured subframes at tick {tick}')
            moving_pairs += len({p.read_bytes() for p in pictures}) >= 2
        check(moving_pairs, 'captured same-tick frames never changed')
        print(f'3D fractional captures: PASS pairs={len(paired)} moving={moving_pairs} immutable accepted ticks', flush=True)
    replay_pauses = re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)', log)
    check(len(replay_pauses) == 2 and replay_pauses[0][0] == '1'
          and replay_pauses[1][0] == '0' and replay_pauses[0][1] == replay_pauses[1][1],
          'shared results replay did not pause without advancing playback')
    ghosts = sorted((work / 'race-save/mesh3d').glob('*.ghost'))
    if not ghosts:
        ghosts = sorted((data / 'mesh3d').glob('*.ghost'))
    check(len(ghosts) == 1, 'finished race did not save its separate ghost')
    ghost_before = ghosts[0].read_bytes()
    if items:
        version,header=(3,128) if crabs or snowmen or penguins or mice or piranhas or seagulls or lava_bubbles or thwomps or cannonballs or boos or rocks or coins or teepees or falling_stars or thunderclouds or common_props or mechakoopas or kamek or pier_boos else (2,112)
        check(ghost_before[:8] == b'MKSC3GHO' and len(ghost_before) >= header
              and struct.unpack_from('<II', ghost_before, 8) == (version,header),
              f'item TT did not persist its version{version} ghost/header{header}')
        if crabs or snowmen or penguins or mice or piranhas or seagulls or lava_bubbles or thwomps or cannonballs or boos or rocks or coins or teepees or falling_stars or thunderclouds or common_props or mechakoopas or kamek or pier_boos:
            check(struct.unpack_from('<II',ghost_before,112)==(1,1)
                  and struct.unpack_from('<Q',ghost_before,120)[0]!=0,
                  'world ghost omitted its explicit rules/profile revision/initial state')
            # V3 retains the44-word terminal kart state after its40-byte end record.
            record_start=header+44*4
            framed=len(ghost_before)-record_start-44*4
            check(framed>=80 and framed%40==0
                  and struct.unpack_from('<I',ghost_before,record_start+framed-40)[0]==0xffffffff
                  and struct.unpack_from('<Q',ghost_before,record_start+framed-8)[0]!=0,
                  'actor ghost lost its40-byte actor checksum records/end framing')
        check(all(struct.unpack_from('<II', ghost_before, 88)),
              'item ghost omitted its MIR profile/revision identity')
    if sky_motion:
        check(len(ghost_before)>=128 and ghost_before[:8]==b'MKSC3GHO'
              and struct.unpack_from('<II',ghost_before,8)==(3,128)
              and ghost_before[88:112]==bytes(24)
              and struct.unpack_from('<II',ghost_before,112)==(1,1)
              and struct.unpack_from('<Q',ghost_before,120)[0]!=0,
              'sky-only ghost lost v3 ownership or acquired MIR header state')
        records=ghost_before[128+44*4:-44*4]
        check(len(records)>=80 and len(records)%40==0
              and struct.unpack_from('<I',records,len(records)-40)[0]==0xffffffff,
              'sky-only ghost lost its40-byte records or terminal kart state')
        check(all(records[i+16:i+32]==bytes(16) and
                  struct.unpack_from('<Q',records,i+32)[0]!=0 for i in range(0,len(records),40)),
              'sky-only replay recorded MIR controls/state or omitted accepted sky checksum')
        check(len(records)>=181*40,'sky-only replay omitted its full countdown')
        countdown=[struct.unpack_from('<Q',records,i+32)[0] for i in range(0,180*40,40)]
        check([struct.unpack_from('<I',records,i)[0] for i in range(0,180*40,40)]==list(range(1,181))
              and len(set(countdown))==180
              and countdown[0]!=struct.unpack_from('<Q',ghost_before,120)[0],
              'sky-only accepted clock did not advance independently during countdown')
        check('3D item event: course=65536' not in log,
              'sky-only lifecycle unexpectedly enabled terrain items')
    _, replay = runner.run('verify', ['--mesh-finished-replay', pack, ghosts[0], *arguments],
                           **common)
    check('3D Time Trial replay: PASS course=65536 driver=2' in replay,
          'saved real-race inputs failed deterministic replay')
    if items or sky_motion:
        verified = re.search(r'3D Time Trial replay: PASS course=65536 driver=2 .*'
                             r'checksum=([0-9a-f]{16})', replay)
        check(verified is not None and verified[1] == final_checksum,
              'saved input/item/actor replay changed the recorded final kart state')
    # Relaunch uses the same saves and full compatibility identity.
    _, second = runner.run('relaunch', ['--menu-tour', pack, work / 'race-save',
        'SDAAUAAEA..S.A...', work / 'relaunch-frames', *arguments, '--driving-profile', 'modern-air'], **common)
    check(bridge_tt_start(root,1) in second,
          'saved ghost did not load on a fresh application owner')
    if kart_grounding:
        verify_kart_animation_ghost(second)
    pauses = re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)', second)
    check(len(pauses) == 2 and pauses[0][0] == '1' and pauses[1][0] == '0'
          and pauses[0][1] == pauses[1][1], 'pause menu advanced the 3D simulation')
    check(ghosts[0].read_bytes() == ghost_before, 'unfinished attempt changed the saved ghost')
    check(hashlib.sha256(pack.read_bytes()).digest() == pack_before, '3D race changed base pack bytes')
    frames = sorted((work / 'race-frames').glob('*.ppm'))
    if model_overlays:
        verify_model_overlay_pictures(frames,model_overlays)
    check(frames and all(read_ppm(frame)[:2] == (320, 180) for frame in frames),
          'shared Time Trial layout lost the configured window extent')
    if pier_boos and gpu_probe is not None:
        verify_pier_boo_pictures(frames)
    if kamek and gpu_probe is not None:
        verify_kamek_pictures(frames)
    if mechakoopas and gpu_probe is not None:
        verify_mechakoopa_pictures(frames)
    if common_props and gpu_probe is not None:
        verify_common_prop_pictures(frames)
    if thunderclouds and gpu_probe is not None:
        verify_thundercloud_pictures(frames)
    if falling_stars and gpu_probe is not None:
        verify_falling_star_pictures(frames)
    if teepees and gpu_probe is not None:
        verify_teepee_pictures(frames)
    if coins and gpu_probe is not None:
        gold=0
        for picture in frames:
            fw,fh,rgb=read_ppm(picture)
            gold+=sum(rgb[i:i+3]==bytes((246,195,68))
                      for y in range(30,fh-20) for x in range(45,fw-20)
                      for i in [(y*fw+x)*3])
        check(gold>=4,'original gold coin floor art absent from real world captures')
    if gpu_probe is not None:
        sign_pixels = 0
        for picture in frames:
            fw, fh, rgb = read_ppm(picture)
            sign_pixels += sum(rgb[i:i+3] == bytes((229,37,217))
                               for y in range(40, fh-30) for x in range(fw)
                               for i in [(y*fw+x)*3])
        check(sign_pixels > 4, 'authored cutout sign geometry never appeared in GPU world captures')
    if crabs:
        red_counts=[]
        for picture in frames:
            if int(picture.stem)<len('SDAAUAAEA'):
                continue
            fw,fh,rgb=read_ppm(picture)
            red_counts.append(sum(rgb[(y*fw+x)*3:(y*fw+x)*3+3]==bytes((238,101,67))
                for y in range(30,fh-20) for x in range(45,fw-20)))
        check(red_counts and max(red_counts)>=8 and sum(count>0 for count in red_counts)>=2,
              'authored crab source model never appeared in two actual race world captures')
    if snowmen:
        snow_counts=[]
        for picture in frames:
            if int(picture.stem)<len('SDAAUAAEA'):continue
            fw,fh,rgb=read_ppm(picture)
            snow_counts.append(sum(rgb[(y*fw+x)*3:(y*fw+x)*3+3] in
                (bytes((234,248,255)),bytes((126,218,242)))
                for y in range(30,fh-20) for x in range(45,fw-20)))
        check(snow_counts and max(snow_counts)>=8 and sum(count>0 for count in snow_counts)>=2,
              'authored snowman models never appeared in two actual race world captures')
    if penguins:
        counts=[]
        for picture in frames:
            if int(picture.stem)<len('SDAAUAAEA'):continue
            fw,fh,rgb=read_ppm(picture)
            counts.append(sum(rgb[(y*fw+x)*3:(y*fw+x)*3+3]==bytes((36,59,115))
                for y in range(30,fh-20) for x in range(45,fw-20)))
        check(counts and max(counts)>=8 and sum(count>0 for count in counts)>=2,
              'authored penguin model never appeared in two actual world captures')
    if mice:
        verify_mouse_pictures(frames)
    if piranhas:
        verify_piranha_pictures(frames)
    if seagulls:
        verify_seagull_pictures(frames)
    if lava_bubbles:
        verify_lava_pictures(frames)
    if thwomps:
        verify_thwomp_pictures(frames)
    if cannonballs:
        verify_cannonball_pictures(frames)
    if boos:
        verify_boo_pictures(frames)
    if rocks:
        verify_rock_pictures(frames)
    width, height, pixels = read_ppm(frames[-1])
    yellow = sum(pixels[offset] > 225 and pixels[offset+1] > 180 and pixels[offset+2] < 65
                 for y in range(36, 52) for x in range(16, 200)
                 for offset in [(y*width+x)*3])
    check(yellow > 500, 'shared results reveal stayed invisible behind a resetting animation clock')
    if gpu_probe is not None:
        if not model_overlays:
            verify_tt_source_fallback(root, runner, pack, work, common, crabs, snowmen, penguins, mice, piranhas, seagulls, lava_bubbles, thwomps, cannonballs, boos, rocks, coins, teepees, teepees_star, falling_stars, falling_stars_star, thunderclouds, thunderclouds_star, common_props, common_props_star, mechakoopas, mechakoopas_star, kamek, kamek_star, pier_boos, pier_boos_star, sky_motion, kart_grounding)
        if kart_grounding:
            verify_kart_tunnel(root,runner,pack,work,common)
        if kart_handling:
            verify_kart_handling(root,runner,pack,work,common)
        check(ghosts[0].read_bytes() == ghost_before, 'source-art launch changed the modeled race ghost')
        check(hashlib.sha256(pack.read_bytes()).digest() == pack_before, 'source-art launch changed base pack bytes')
    if benchmark:
        check(gpu_probe is not None, 'completed throughput requires a GPU device probe')
        benchmark_directory = work / 'benchmark'
        benchmark_directory.mkdir(parents=True, exist_ok=True)
        env = {key: value for key, value in os.environ.items() if not key.startswith('MKSC_')}
        env.update(SDL_AUDIO_DRIVER='dummy', SDL_MAC_BACKGROUND_APP='1',
                   SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
        env.update(common)
        env.update(MKSC_DATA_DIR=str(benchmark_directory / 'data'), MKSC_AID_MESH_BENCHMARK='1')
        remaining = runner.deadline - time.monotonic()
        check(remaining > 0, 'mesh route exceeded its 175-second budget before throughput run')
        result = subprocess.run([str(binary), '--menu-tour', str(pack),
            str(benchmark_directory / 'save'), 'SDAAUAAEA.VGDA..S.A..B..', *map(str, arguments)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=remaining)
        (benchmark_directory / 'benchmark.log').write_text(result.stdout, encoding='utf-8')
        check(result.returncode == 0, 'capture-free benchmark failed: ' + result.stdout[-6000:])
        measured = re.search(r'3D offscreen completed throughput: width=(\d+) height=(\d+) views=(\d+) '
            r'samples=300 batches=19 cpu_submit_ns=(\d+) completed_ns=(\d+) worst_batch_ns=(\d+) worst_batch_frame_ns=(\d+) immutable=1',
            result.stdout)
        check(measured is not None, 'missing completed offscreen throughput measurement')
        bw, bh, bv, cpu_ns, completed_ns, worst_ns, worst_frame_ns = map(int, measured.groups())
        expected_h = max(180, 160 * world_scale * (2 if views == 4 else 1))
        check((bw, bh, bv) == ((320 * expected_h + 90) // 180, expected_h, views),
              'throughput run measured a different target/view count')
        check(0 < cpu_ns <= completed_ns and 0 < worst_ns <= completed_ns
              and (worst_ns + 15) // 16 <= worst_frame_ns <= (worst_ns + 11) // 12,
              'invalid completed batch timings')
        check('Menu tour: PASS' in result.stdout and f'checksum={final_checksum}' in result.stdout,
              'throughput run did not complete the unchanged ordinary race')
        check(time.monotonic() <= runner.deadline, 'mesh route exceeded its 175-second budget')
        print(measured.group(0))
    if model_overlays:
        print(f'3D model overlay route: PASS case={model_overlays} real TT ghost replay relaunch')
    print('3D kart handling route: PASS drift boost neutral replay' if kart_handling else
          '3D kart grounding route: PASS model sprite flat slopes bridge tunnel shadow replay' if kart_grounding
          else '3D sky lifecycle route: PASS TT finish pause v3 replay relaunch immutable' if sky_motion
          else '3D Time Trial pier boos route: PASS source haunt lock departure v3 replay relaunch' if pier_boos
        else '3D Time Trial kamek route: PASS source patrol harmless contact v3 replay relaunch' if kamek
        else '3D Time Trial mechakoopas route: PASS source patrol contact facing v3 replay relaunch' if mechakoopas
        else '3D Time Trial common props route: PASS harmless bump umbrella flip v3 replay relaunch' if common_props
          else '3D Time Trial thunderclouds route: PASS shared phases warning strong contact shrink v3 replay relaunch' if thunderclouds
          else '3D Time Trial falling stars route: PASS descent bounce rotation strong contact v3 replay relaunch' if falling_stars
          else '3D Time Trial teepees route: PASS tent rider lock harmless contact v3 replay relaunch' if teepees
          else '3D Time Trial coins route: PASS pickups cap ratios v3 replay relaunch' if coins
          else '3D Time Trial rocks route: PASS source fall impact bounces burn PCM v3 replay relaunch' if rocks
          else '3D Time Trial boos route: PASS source bob harmless touch palette cells PCM v3 replay relaunch' if boos
          else '3D Time Trial cannonballs route: PASS falling strong hit explosion PCM v3 replay relaunch' if cannonballs
          else '3D Time Trial thwomps route: PASS source rebound crush128 recovery PCM v3 replay relaunch' if thwomps
          else '3D Time Trial lava bubbles route: PASS source64-cycle hit burn PCM v3 replay relaunch' if lava_bubbles
          else '3D Time Trial seagulls route: PASS harmless source flutter PCM v3 replay relaunch' if seagulls
          else '3D Time Trial piranhas route: PASS source bite92-turn capture launch PCM v3 replay relaunch' if piranhas
          else '3D Time Trial mice route: PASS source walk hop hit v3 replay relaunch pause' if mice
          else '3D Time Trial penguins route: PASS source six-frame motion real hit v3 replay relaunch pause' if penguins
          else '3D Time Trial snowmen route: PASS two source contacts art visibility bump v3 replay relaunch pause' if snowmen
          else '3D Time Trial crabs route: PASS v3 ghost source motion contact replay relaunch pause' if crabs
          else '3D Time Trial items route: PASS v2 ghost replay mushroom voice PCM relaunch pause' if items
          else '3D Time Trial route: PASS menus driving laps respawn saved-ghost replay relaunch pause')


def verify_boo_held_use(binary, pack, work, gpu_probe, video_driver, gp=False, gles=False):
    """Ordinary boxes/roulette and input prove a retained Star escapes MIR haunt."""
    check(gpu_probe is not None and not work.exists(), 'Boo route needs a GPU and fresh work')
    environment = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
        SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
        SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe = subprocess.run([str(gpu_probe)], env=environment, timeout=30)
    if probe.returncode == 77:
        print('3D Boo route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'Boo route GPU probe failed')
    root = Path(__file__).resolve().parents[3]
    pack_before = hashlib.sha256(pack.read_bytes()).digest()
    spec = importlib.util.spec_from_file_location('boo_held_example',
        root / 'mods/examples/bridge-course/build.py')
    builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
    authored = builder.source(work / 'source', cup=gp, items=True)
    path = authored / 'items.json'
    document = json.loads(path.read_text())
    profile = document['profiles'][0]
    # The following kart gets Boo first. Its leader can still be in roulette,
    # or collect another box during the haunt; neither path fabricates inventory.
    stars = [[[0x100a, 1]] for _ in range(8)]
    player = [[[0x100a, 1]] for _ in range(8)]
    cpu = [[[0x100a, 1]] for _ in range(8)]
    (cpu if gp else player)[1] = [[0x100b, 1]]
    profile['weights'] = {'player': player, 'cpu': cpu if gp else stars}
    profile['boo']['theft_lock_ticks'] = 480
    profile['cpu']['decision_ticks'] = profile['cpu']['hold_ticks'] = 1
    path.write_text(json.dumps(document, indent=2) + '\n')
    for path in sorted((authored / 'courses3d').glob('*/course.json')):
        course = json.loads(path.read_text())
        course['grid'][7 if gp else 0]['position'] = [18, .2, -23]
        course['grid'][6 if gp else 1]['position'] = [22, .2, -24.5]
        if gp:
            # Other CPUs approach along valid road instead of consuming the
            # two leading drivers' first boxes in the same opening turn.
            for index in range(6):
                course['grid'][index] = {'position': [-18 + 6*index, .2, -26],
                    'quaternion': [0, .7071067811865476, 0, .7071067811865476]}
        course['items'] = [dict(position=[22, 1, -24.5], radius=1.25, respawn_ticks=3600)]
        course['items'] += [dict(position=[20, z/2 + 1, z], radius=1.25,
            respawn_ticks=3600) for z in (3, 7, 11, 15)]
        path.write_text(json.dumps(course, indent=2) + '\n')
    builder.mod_kit.build(authored, None, work / 'mods')
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    if gp:
        tour = 'SAAUAAEA.K.B.B.'
    else:
        prefix = 'DAAAA' + ''.join(direction+'A' for direction in 'UL') + 'AEA'
        tour = prefix + '..S.A.G.A.G.DA.B.'
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_DATA_DIR=str(work / 'player'),
        MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    _, log = runner.run('boo-held', ['--menu-tour', pack, work / 'save', tour,
        work / 'frames', '--mods-dir', work / 'mods', '--mod', 'bridge-course',
        '--no-classic-menus', '--short-transitions'], **common)
    check('Menu tour: PASS' in log and '3D renderer: GPU driver=' in log
          and '3D renderer: CPU' not in log, 'Boo race did not finish its GPU menu route')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, 'Boo race used a CPU UI fallback')
    rows = re.findall(r'3D held item use: course=(\d+) tick=(\d+) racer=(\d+) '
        r'item=([0-9a-f]+) held_before=([0-9a-f]+) boo_before=(\d+) '
        r'boo_after=(\d+) star_after=(\d+)', log)
    escapes = set()
    boos = set()
    for course, tick, racer, item, held, before, after, star in rows:
        if int(item, 16) == 0x100b:
            boos.add(int(course))
        if int(racer) == 0 and int(item, 16) == 0x100a and int(before) > 1:
            check(int(held, 16) == 0x100a and int(after) == 0 and int(star) > 0,
                  'held Star did not clear a live shared slowdown')
            escapes.add(int(course))
    expected = {65536}
    check(boos >= expected and escapes >= expected,
          f'ordinary Boo/retained Star overlap missing: Boo={sorted(boos)} escape={sorted(escapes)}')
    if gp:
        finishes = re.findall(r'3D GP finish: cup=65536 index=0 course=65536 '
            r'rank=(\d+) stage=\d+ ticks=(\d+) credits=\d+', log)
        check(len(finishes) == 1 and int(finishes[0][0]) <= 7
              and int(finishes[0][1]) > 100 and '3D GP complete:' not in log,
              'Boo GP episode did not genuinely finish before returning through results')
        check(re.search(r'3D GP result: course=65536 player=1 driver=\d+ rank=\d+ '
              r'retired=0 finish_ticks=\d+ input=[0-9a-f]+ lap=3 stage=0', log),
              'Boo GP episode did not finish three real laps')
    else:
        check('3D VS result:' in log, 'Boo local VS omitted shared results')
    check(hashlib.sha256(pack.read_bytes()).digest() == pack_before, 'Boo fixture changed base pack')
    print(f'3D Boo held-item route: PASS mode={"gp" if gp else "vs2"} '
          f'courses={len(expected)} backend={"gles" if gles else "gpu"}')


def verify_vs(binary, pack, work, gpu_probe, video_driver, players, gles=False, items=False, coins=False, sky_progress=False):
    check(gpu_probe is not None, 'local mesh VS requires a real GPU probe')
    check(not work.exists(), 'local mesh VS work must be fresh')
    environment = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe = subprocess.run([str(gpu_probe)], env=environment, timeout=30)
    if probe.returncode == 77:
        print('3D local VS route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU device probe failed its rendering checks')
    root = Path(__file__).resolve().parents[3]
    pack_before = hashlib.sha256(pack.read_bytes()).digest()
    build = [sys.executable, str(root / 'mods/examples/bridge-course/build.py'),
             '--output-dir', str(work / 'mods')]
    if items:
        build.append('--items')
    if coins:
        build.append('--coins')
    if sky_progress:
        # Reuse the editable example's original art, changing only declared sky
        # rules/provenance. Racers, physics, grids and gate geometry stay intact.
        spec=importlib.util.spec_from_file_location('vs_sky_example',root/'mods/examples/bridge-course/build.py')
        builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
        authored=builder.source(work/'source/sky-progress-vs',sky_motion=True)
        skies=json.loads((authored/'skies.json').read_text())
        skies['profiles'][0]['rules']['controller']=2
        skies['profiles'][0]['rules']['layer_mask']=2
        skies['profiles'][0]['layers'][0]['layer']=1
        (authored/'skies.json').write_text(json.dumps(skies,indent=2)+'\n')
        course=authored/'courses3d/bridge/course.json'
        document=json.loads(course.read_text())
        stages=sorted({g['stage'] for g in document['checkpoints']})
        check(stages==list(range(5)),'VS sky fixture lost its five ordinary gate stages')
        for obj in document['objects']:
            for prop in obj.get('properties',[]):
                if prop['key']=='mksc:sky-layer':prop['value']=1
        models_path=authored/'models.json'
        models=json.loads(models_path.read_text())
        # SPEED_PAN's visible middle uses twice the original panorama repeat;
        # layer0 is translation-only and would not prove independent pan.
        for mesh in models['meshes']:
            if mesh['id']==300:
                for vertex in mesh['vertices']:vertex[3]*=2
        models_path.write_text(json.dumps(models,indent=2)+'\n')
        document['meta']['properties'].extend([
            dict(key='source:progress-stage-count',type='integer',value=len(stages)),
            *[dict(key=f'source:progress-raw:{stage}',type='integer',value=stage) for stage in stages]])
        course.write_text(json.dumps(document,indent=2)+'\n')
        builder.mod_kit.build(authored,None,work/'mods')
    else:
        subprocess.run(build, check=True)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    # Tours start at the settled title. Each human chooses a distinct seat;
    # G supplies independent route-following controls, never positions or laps.
    prefix = 'DA' + 'U' * (players - 2) + 'AAA'
    prefix += ''.join(direction + 'A' for direction in 'ULDR'[:players]) + 'AEA'
    tour = (prefix+'..YS..A.G.A.Y.G.DA.B.' if sky_progress else
            prefix + '..S.A.' + ('I' if items else '') + 'G.A.G.DA.B.')
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_DATA_DIR=str(work / 'player'),
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    if sky_progress:
        # Ordinary brake input holds only human0 through the other driver's
        # first two laps. Y waits for accepted phase divergence, not a timer.
        common['MKSC_AID_MESH_INPUTS']='180:3900:0:2'
    _, log = runner.run('vs', ['--menu-tour', pack, work / 'save', tour,
        work / 'frames', '--mods-dir', work / 'mods', '--mod', 'bridge-course',
        '--no-classic-menus', '--short-transitions'],
        **dict(common, **({'MKSC_AUDIO_CAPTURE': str(work / 'items.wav')} if items else {})))
    if coins:
        verify_coins(log,{65536},8,players,players,require_loss=True)
    check('Menu tour: PASS' in log, 'local VS tour did not return through its menus')
    check('3D renderer: GPU driver=' in log and '3D renderer: CPU' not in log,
          'local VS did not use the GPU world backend')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, 'local VS HUD/results used CPU UI fallback')
    check(re.search(r'3D world target: width=\d+ height=\d+ views=' + str(players) + r' scale=1', log),
          'GPU world did not publish every local camera viewport')
    verify_driver_art(log, players - 1, 1)
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'VS did not use the GLES depth backend')
    starts = re.findall(r'3D VS start: course=(\d+) players=(\d+) drivers=([\d,]+) '
                        r'profile=(\d+) revision=(\d+)', log)
    check(len(starts) == 2 and starts[0] == starts[1],
          'Retry did not restart the same course, players, drivers and profile')
    course, count, driver_text, profile, revision = starts[0]
    drivers = list(map(int, driver_text.split(',')))
    check(int(count) == players and len(drivers) == len(set(drivers)) == players
          and min(drivers) > 0 and int(profile) > 0 and int(revision) > 0,
          'ordinary player/driver choices were not retained by local VS')
    finishes = re.findall(r'3D VS finish: course=(\d+) players=(\d+) tick=(\d+) '
                          r'finished=(\d+) views=(\d+)', log)
    check(len(finishes) == 2 and all(c == course and int(n) == int(v) == players
          and int(t) > 180 and players - 1 <= int(f) <= players
          for c, n, t, f, v in finishes), 'local VS omitted real shared race completion/views')
    rows = re.findall(r'3D VS result: course=(\d+) player=(\d+) driver=(\d+) rank=(\d+) '
                      r'retired=([01]) finish_ticks=(\d+) input=([0-9a-f]{16})', log)
    check(len(rows) == 2 * players, 'local VS standings omitted a human racer')
    for attempt in range(2):
        results = rows[attempt * players:(attempt + 1) * players]
        check({int(row[1]) for row in results} == set(range(1, players + 1))
              and {int(row[3]) for row in results} == set(range(1, players + 1)),
              'local VS standings have duplicate/missing players or ranks')
        for c, player, driver, rank, retired, ticks, _ in results:
            check(c == course and int(driver) == drivers[int(player) - 1],
                  'standings used a different player/driver identity')
            check((int(ticks) == 0 and int(rank) == players) if int(retired) else int(ticks) > 0,
                  'VS invented a finish time or lost a legitimate finish')
        check(sum(int(row[4]) for row in results) == players - int(finishes[attempt][3]),
              'VS retirement disagrees with all-but-last completion')
        check(len({row[6] for row in results}) > 1,
              'all humans received the same accepted input stream')
    if items:
        verify_item_owner(log, 8, players)
        verify_item_audio(log, work / 'items.wav')
        pickups = re.findall(r'3D item pickup: course=(\d+) tick=(\d+) racer=(\d+) '
                             r'placement=(\d+) fraction=(\d+)', log)
        uses = re.findall(r'3D item event: course=65536 tick=\d+ kind=0 item=([0-9a-f]+) '
                          r'racer=(\d+) target=\d+ serial=\d+', log)
        check(pickups and all(int(row[0]) == 65536 and int(row[1]) > 180
              and 0 <= int(row[2]) < players and int(row[3]) < 4
              and 0 <= int(row[4]) <= 1 << 32 for row in pickups),
              'local VS omitted valid fullXYZ item-box pickup events')
        check(uses and all(0 <= int(racer) < players and 0x1000 <= int(item, 16) <= 0x100f
              and int(item, 16) != 0x100d for item, racer in uses)
              and any(int(racer) > 0 for _, racer in uses),
              'local VS item input never reached another human racer or emitted an invalid use')
        settled = re.search(r'3D item aid: settled course=65536 item=([0-9a-f]+) tick=(\d+)', log)
        check(settled is not None and 0x1000 <= int(settled[1], 16) <= 0x100c
              and int(settled[1], 16) != 0x1006 and int(settled[2]) > 180,
              'local VS did not capture a naturally settled primary inventory')
        progress = re.findall(r'3D VS result: course=65536 player=(\d+) driver=\d+ rank=\d+ '
                              r'retired=([01]) finish_ticks=\d+ input=[0-9a-f]{16} '
                              r'lap=(\d+) stage=(\d+)', log)
        check(len(progress) == 2 * players and all(1 <= int(player) <= players
              and 0 <= int(lap) <= 3 and 0 <= int(stage) < 5
              and (int(retired) or int(lap) == 3)
              for player, retired, lap, stage in progress),
              'local VS finish/lap progress is inconsistent after item hits and recovery')
        inventory_picture = read_ppm(work / 'frames' / f'{len(prefix) + 6:03}.ppm')
        result_picture = read_ppm(work / 'frames' / f'{len(prefix) + 7:03}.ppm')
        check(inventory_picture[:2] == result_picture[:2] == (640, 360)
              and inventory_picture[2] != result_picture[2],
              'local VS reused results for its live split-screen item HUD capture')
    pauses = re.findall(r'3D VS pause: paused=([01]) tick=(\d+)', log)
    check(len(pauses) == 2 and pauses[0][0] == '1' and pauses[1][0] == '0'
          and pauses[0][1] == pauses[1][1], 'local VS pause advanced the shared race turn')
    check(f'3D VS return: course={course} players={players}' in log,
          'local VS results did not return to the ordinary course browser')
    check('3D Time Trial start:' not in log and '3D Time Trial finish:' not in log,
          'local VS initialized a Time Trial record owner')
    check(not list(work.rglob('*.ghost')) and not list(work.rglob('*.pb')),
          'local VS wrote Time Trial records or ghosts')
    check(hashlib.sha256(pack.read_bytes()).digest() == pack_before, 'local VS modified the base pack')
    # This shot is admitted after the real race start, before pause or autopilot.
    width, height, pixels = read_ppm(work / 'frames' / f'{len(prefix) + 1:03}.ppm')
    check((width, height) == (640, 360), 'local VS lost its configured shared window')
    pane_w, pane_h = (width, height // 2) if players == 2 else (width // 2, height // 2)
    for player in range(1, players):
        second_x, second_y = ((0, pane_h) if players == 2 else
                              ((player & 1) * pane_w, (player // 2) * pane_h))
        differences = 0
        for y in range(32, pane_h - 24, 8):
            for x in range(24, pane_w - 24, 8):
                a = (y * width + x) * 3
                b = ((y + second_y) * width + x + second_x) * 3
                differences += pixels[a:a+3] != pixels[b:b+3]
        check(differences > 32, f'player {player + 1} rendered the first camera view')
    if sky_progress:
        check('3D sky start: course=65536 profile=1 revision=1 controller=2 layers=2' in log,
              'VS did not admit the declared progress-driven sky')
        snapshots=re.findall(r'3D sky VS aid: course=(\d+) controller=(\d+) racer=(\d+) '
            r'tick=(\d+) phase=(\d+) progress=(\d+) yaw=(\d+) scroll0=(-?\d+) '
            r'scroll1=(-?\d+) checksum=([0-9a-f]{16})',log)
        check(len(snapshots)==4,'VS omitted independent accepted sky snapshots before/after Retry')
        for attempt in range(2):
            pair=snapshots[attempt*2:attempt*2+2]
            check([int(row[2]) for row in pair]==[0,1]
                  and all(row[0]==course and row[1]=='2' for row in pair)
                  and pair[0][3]==pair[1][3] and pair[0][9]==pair[1][9]
                  and 180<int(pair[0][3])<4080
                  and pair[0][4]=='0' and pair[1][4]=='1'
                  and int(pair[0][5])<int(pair[1][5])
                  and all(row[6]=='0' for row in pair) and pair[0][8]!=pair[1][8],
                  'VS sky phases did not follow distinct accepted racer progress at one clock')
        resets=re.findall(r'3D sky aid reset: course=(\d+) controller=2 before_tick=(\d+) '
            r'before_phase=(\d+) tick=(\d+) phase=(\d+) progress=(\d+) checksum=([0-9a-f]{16}) '
            r'before_other_phase=(\d+) other_phase=(\d+) other_progress=(\d+)',log)
        check(len(resets)==1 and resets[0][0]==course and int(resets[0][1])>180
              and resets[0][3:6]==('0','0','32512') and int(resets[0][7]) in (1,2)
              and resets[0][8:]==('0','32512'),
              'ordinary VS Retry did not reset both racers and their shared sky clock')
        # The same bound holds again after ordinary Retry, rather than inheriting
        # the prior leader's advanced owner. Both actual panes retain sky art.
        for token_index in (tour.index('Y'),tour.index('Y',tour.index('Y')+1)):
            width,height,rgb=read_ppm(work/'frames'/f'{token_index:03}.ppm')
            check((width,height)==(640,360),'VS sky divergence capture lost its two pane extent')
            masks=[]
            for pane in range(2):
                masks.append(bytes(rgb[((pane*180+y)*width+x)*3:((pane*180+y)*width+x+1)*3]
                    ==bytes((235,244,255)) for y in range(8,95) for x in range(50,590)))
            check(all(sum(mask)>4 for mask in masks) and masks[0]!=masks[1],
                  'VS sky panes omitted original clouds or reused one progress/camera presentation')
    print(f'3D sky progress VS route: PASS players={players} independent phases panes pause retry results' if sky_progress
          else f'3D VS coins route: PASS players={players} shared pickups losses HUD standings retry' if coins
          else f'3D VS items route: PASS players={players} pickups uses split HUD standings retry pause' if items
          else f'3D local VS route: PASS players={players} inputs views standings retry pause')


def converted_catalog(source, content):
    """Read the same final catalog ordering used by the ordinary3D browser."""
    import track_format
    courses = []
    for (kind,identity),(_,payload) in track_format.load_pack(content).items():
        if kind != 100:
            continue
        check(payload[:4] == b'C3C1' and len(payload) == 40, 'converted catalog row is malformed')
        course,cup,slot,modes,sort_key,reserved = struct.unpack_from('<6I',payload,16)
        document = json.loads((source/f'courses3d/{course}/course.json').read_text())
        check(course == identity and reserved == 0, 'converted catalog identity/reserved mismatch')
        row=dict(id=course,cup=cup,slot=slot,modes=modes,sort_key=sort_key,
                 laps=document['meta']['laps'])
        crabs=[];snowmen=[];penguins=[];mice=[];piranhas=[];seagulls=[];lava_bubbles=[];thwomps=[];cannonballs=[];boos=[];rocks=[];teepees=[];falling_stars=[];thunderclouds=[];common_props=[];mechakoopas=[];kameks=[];pier_placements=[]
        for placed in document['objects']:
            props={p['key']:p['value'] for p in placed.get('properties',[])}
            if props.get('mksc:controller')=='crabs':
                crabs.append(props)
            elif props.get('mksc:controller')=='snowmen':
                snowmen.append(props)
            elif props.get('mksc:controller')=='penguins':
                penguins.append(props)
            elif props.get('mksc:controller')=='mice':
                mice.append(props)
            elif props.get('mksc:controller')=='piranhas':
                piranhas.append(props)
            elif props.get('mksc:controller')=='seagulls':
                seagulls.append(props)
            elif props.get('mksc:controller')=='lava_bubbles':
                lava_bubbles.append(props)
            elif props.get('mksc:controller')=='thwomps':
                thwomps.append(props)
            elif props.get('mksc:controller')=='cannonballs':
                cannonballs.append(props)
            elif props.get('mksc:controller')=='boos':
                boos.append(props)
            elif props.get('mksc:controller')=='rocks':
                rocks.append(props)
            elif props.get('mksc:controller')=='teepees':
                teepees.append(props)
            elif props.get('mksc:controller')=='falling_stars':
                falling_stars.append(props)
            elif props.get('mksc:controller')=='thunderclouds':
                thunderclouds.append(props)
            elif props.get('mksc:controller')=='pier_boos':
                pier_placements.append(props)
            elif props.get('mksc:controller')=='kamek':
                kameks.append(props)
            elif props.get('mksc:controller')=='mechakoopas':
                mechakoopas.append(props)
            elif props.get('mksc:controller')=='common_props':
                common_props.append(props)
        if crabs or snowmen or penguins or mice or piranhas or seagulls or lava_bubbles or thwomps or cannonballs or boos or rocks or teepees or falling_stars or thunderclouds or common_props or mechakoopas or kameks or pier_placements:
            meta={p['key']:p['value'] for p in document['meta'].get('properties',[])}
            profile=meta['mksc:actor-rules']['id']
            profiles=json.loads((source/'source-actors.json').read_text())['profiles']
            rules=next(record for record in profiles if record['profile_id']==profile)
            for family,placements in (('crabs',crabs),('snowmen',snowmen),('penguins',penguins),('mice',mice),('piranhas',piranhas),('seagulls',seagulls),('lava_bubbles',lava_bubbles),('thwomps',thwomps),('cannonballs',cannonballs),('boos',boos),('rocks',rocks),('teepees',teepees),('falling_stars',falling_stars),('thunderclouds',thunderclouds),('common_props',common_props),('mechakoopas',mechakoopas),('kamek',kameks),('pier_boos',pier_placements)):
                if not placements:continue
                first=placements[0]
                row[family]=dict(count=len(placements),profile_id=profile,revision=rules['revision'],
                    first=dict(x_q16=first['source:x-q16'],y_q16=first['source:y-q16'],
                               variant=first['source:variant']))
                if family=='common_props':
                    row[family]['codes']=[p['source:code'] for p in placements]
                if family in ('thunderclouds','mechakoopas'):
                    row[family]['variants']=[p['source:variant'] for p in placements]
        meta={p['key']:p['value'] for p in document['meta'].get('properties',[])}
        if 'mksc:sky-motion' in meta:
            identity=meta['mksc:sky-motion']['id']
            sky=next(p for p in json.loads((source/'skies.json').read_text())['profiles'] if p['profile_id']==identity)
            row['sky']=dict(profile_id=identity,revision=sky['revision'],controller=sky['rules']['controller'])
        if 'mksc:coin-rules' in meta:
            identity=meta['mksc:coin-rules']['id']
            rules=next(p for p in json.loads((source/'coins.json').read_text())['profiles'] if p['profile_id']==identity)
            placements=[o for o in document['objects'] if o['class_id']==0x20000001]
            row['coins']=dict(count=len(placements),profile_id=identity,revision=rules['revision'],modes=rules['modes'])
        courses.append(row)
    courses.sort(key=lambda row:(row['sort_key'],row['id']))
    cups = []
    for cup in sorted({row['cup'] for row in courses if row['modes'] & 1}):
        rows = sorted((row for row in courses if row['cup'] == cup and row['modes'] & 1),key=lambda row:row['slot'])
        check([row['slot'] for row in rows] == [0,1,2,3], 'converted GP cup is incomplete')
        cups.append(dict(id=cup,courses=[row['id'] for row in rows]))
    return courses,cups


def converted_tree_hash(directory):
    check(directory.is_dir(), 'authored conversion input folder is missing')
    fingerprint = hashlib.sha256()
    for path in sorted(path for path in directory.rglob('*') if path.is_file()):
        fingerprint.update(path.relative_to(directory).as_posix().encode()+b'\0')
        fingerprint.update(hashlib.sha256(path.read_bytes()).digest())
    return fingerprint.hexdigest()


def setup_converted_fixture(pack, work, *, authored=False, course_ids=(), handpass=None):
    """Prepare reproducible catalog or real authored-folder conversion once."""
    root = Path(__file__).resolve().parents[3]
    sys.path.insert(0,str(root/'tools/portable'))
    import convert_course_3d as converter
    import track_format
    sections = track_format.load_pack(pack)
    selection=sorted(course_ids) if course_ids else None
    check(handpass is None or (not authored and handpass.is_file()),
          'hand-pass setup needs an existing manifest and catalog courses')
    handpass_hash=None if handpass is None else converted_tree_hash(handpass.parent)
    check(not selection or (not authored and len(selection)==len(set(selection))
          and all(0<=value<=0xffffffff for value in selection)),
          'converted subset requires unique bounded source course IDs')
    identities = [] if authored else converter.catalog_courses(sections)
    if selection is not None:
        check(set(selection)<=set(identities),'converted subset names a course outside the owner catalog')
        identities=selection
    elif not authored:
        check(len(identities) == len(set(identities)) == 44, 'conversion requires all forty courses/four arenas')
    original = hashlib.sha256(pack.read_bytes()).hexdigest()
    # check.py runs each bounded group in a separate CTest invocation. Reuse
    # only a fully verified fixture from these exact tools, data and owner pack.
    fingerprint = hashlib.sha256()
    dependencies = sorted((root/'tools/portable').glob('*.py'))
    dependencies += sorted((root/'portable/data').glob('*.json'))
    dependencies += [Path(__file__).resolve()]
    builder_root = root/'mods/examples/custom-cup'
    if authored:
        dependencies += [builder_root/'build.py',builder_root/'mod.json']
        dependencies += sorted((builder_root/'courses').glob('*.json'))
    for dependency in dependencies:
        fingerprint.update(str(dependency.relative_to(root)).encode()+b'\0')
        fingerprint.update(hashlib.sha256(dependency.read_bytes()).digest())
    tools_hash = fingerprint.hexdigest()
    input_kind = 'track-folder' if authored else 'catalog'
    source_input = work/'author-input'
    success = ('Converted authored 3D fixture: PASS tracks=1 reproducible=1 immutable=1' if authored else
               'Converted 3D fixture: PASS courses=40 arenas=4 cups=10 reproducible=1 immutable=1')
    if selection is not None:
        success='Converted 3D subset fixture: PASS selected='+','.join(map(str,selection))+' reproducible=1 immutable=1'
    manifest_path = work/'fixture.json'
    if manifest_path.is_file():
        previous = json.loads(manifest_path.read_text())
        content = work/'mods/converted-courses/content.pack'
        archive = work/'mods/converted-courses.zip'
        input_unchanged = (not authored or (source_input.is_dir() and
                           converted_tree_hash(source_input) == previous.get('input_sha256')))
        if (previous.get('version') == 1 and previous.get('source_sha256') == original
                and previous.get('input_kind','catalog') == input_kind and input_unchanged
                and previous.get('selected_courses') == selection
                and previous.get('handpass_sha256') == handpass_hash
                and previous.get('tools_sha256') == tools_hash and content.is_file() and archive.is_file()
                and hashlib.sha256(content.read_bytes()).hexdigest() == previous.get('content_sha256')
                and hashlib.sha256(archive.read_bytes()).hexdigest() == previous.get('archive_sha256')):
            print(success+' reused=1')
            return
    if work.exists():
        shutil.rmtree(work)
    tracks = []
    input_hash = None
    if authored:
        spec = importlib.util.spec_from_file_location('converted_authored_example',builder_root/'build.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        builder.build(pack,source_input,work=work/'author-build')
        shutil.rmtree(work/'author-build')
        tracks = [source_input/'tracks/oval']
        identities = [json.loads((tracks[0]/'track.json').read_text())['id']]
        input_hash = converted_tree_hash(source_input)
    first = converter.generate(pack,work/'source',courses=() if authored else identities,tracks=tracks,
                               handpass=handpass)
    folder,archive = converter.mod_kit.build(first,None,work/'mods')
    repeated = converter.generate(pack,work/'repeat-source',courses=() if authored else identities,tracks=tracks,
                                  handpass=handpass)
    repeat_folder,repeat_archive = converter.mod_kit.build(repeated,None,work/'repeat-mods')
    check((folder/'content.pack').read_bytes() == (repeat_folder/'content.pack').read_bytes()
          and archive.read_bytes() == repeat_archive.read_bytes(),
          'conversion did not rebuild byte-for-byte')
    courses,cups = converted_catalog(first,folder/'content.pack')
    check({row['id'] for row in courses} == set(identities), 'conversion lost a selected source course')
    if authored:
        check(len(courses) == 1 and courses[0]['modes'] & 2 and not cups,
              'authored folder did not become one independent TT course')
        check(converted_tree_hash(source_input) == input_hash, 'converter changed authored input files')
    elif selection is None:
        check(sum(row['modes'] == 16 for row in courses) == 4
              and sum(row['modes'] & 2 != 0 for row in courses) == 40,
              'conversion confused races with Battle-only arenas')
        check(len(cups) == 10, 'conversion omitted a four-course GP cup')
    check(hashlib.sha256(pack.read_bytes()).hexdigest() == original, 'converter modified its owner pack')
    manifest = dict(version=1,source_sha256=original,tools_sha256=tools_hash,input_kind=input_kind,
                    content_sha256=hashlib.sha256((folder/'content.pack').read_bytes()).hexdigest(),
                    archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),courses=courses,cups=cups)
    if selection is not None: manifest['selected_courses']=selection
    if handpass is not None:
        check(converted_tree_hash(handpass.parent)==handpass_hash,'converter modified hand-pass sources')
        manifest['handpass_sha256']=handpass_hash
    if authored:
        manifest['input_sha256'] = input_hash
    manifest_path.write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
    shutil.rmtree(work/'repeat-source')
    shutil.rmtree(work/'repeat-mods')
    print(success)


def converted_gp_routes(value):
    """Four explicit RTHD indices or auto, without selecting content by ID.

    >>> converted_gp_routes('auto,2,auto,0')
    ('auto', '2', 'auto', '0')
    >>> converted_gp_routes('0,16383,1,2')
    ('0', '16383', '1', '2')
    >>> for value in ('0,1,2', '0,1,2,3,4', '-1,0,0,0', '16384,0,0,0',
    ...               'Auto,0,0,0', '0, 1,2,3', '0,x,2,3'):
    ...     try:
    ...         converted_gp_routes(value)
    ...     except argparse.ArgumentTypeError:
    ...         pass
    ...     else:
    ...         raise AssertionError(value)
    """
    tokens=value.split(',')
    if len(tokens)!=4 or any(token!='auto' and
            (re.fullmatch('[0-9]+',token) is None or len(token)>5 or int(token)>16383)
            for token in tokens):
        raise argparse.ArgumentTypeError('converted GP routes require four comma-separated auto or 0..16383 values')
    return tuple(token if token=='auto' else str(int(token)) for token in tokens)


def converted_gp_driver(value, *, enabled=True):
    """Validate an explicit GP-only stock driver selection.

    >>> [converted_gp_driver(str(i)) for i in range(1,9)]
    [1, 2, 3, 4, 5, 6, 7, 8]
    >>> for value in ('0', '9', 'Mario', '1.5'):
    ...     try:
    ...         converted_gp_driver(value)
    ...     except argparse.ArgumentTypeError:
    ...         pass
    ...     else:
    ...         raise AssertionError(value)
    >>> converted_gp_driver('2', enabled=False)
    Traceback (most recent call last):
    ...
    argparse.ArgumentTypeError: converted GP driver requires a converted GP race route
    """
    if not enabled:
        raise argparse.ArgumentTypeError('converted GP driver requires a converted GP race route')
    try:
        driver=int(value)
    except (TypeError,ValueError):
        raise argparse.ArgumentTypeError('converted GP driver must be a stock ID from 1 to 8') from None
    if driver not in range(1,9):
        raise argparse.ArgumentTypeError('converted GP driver must be a stock ID from 1 to 8')
    return driver


def converted_browser_prefix(mode, selected, gp_driver=2):
    """Select a visible course using ordinary mode and character navigation.

    >>> converted_browser_prefix('gp',0)
    'SAAUAAEA'
    >>> converted_browser_prefix('gp',2,1)
    'SAAULAAEDDA'
    >>> [converted_browser_prefix('gp',0,i)[3:-4] for i in range(1,9)]
    ['UL', 'U', 'UR', 'L', 'R', 'DL', 'D', 'DR']
    >>> converted_browser_prefix('tt',0)
    'SDAAUAAEA'
    """
    # In menu tours, Select on the 2D sets keeps the mode-filtered 3D format
    # browser as a development route (players use the cup rows). Only its
    # visible cursor advances here; content IDs are never menu coordinates.
    if mode == 'tt':
        prefix = 'SDAAUAAE'
    elif mode == 'gp':
        moves=('UL','U','UR','L','R','DL','D','DR')
        driver=converted_gp_driver(gp_driver)
        prefix = 'SAA'+moves[driver-1]+'AAE'
    else:
        prefix = 'DAADAAUALAAE' # two humans, Battle, distinct driver choices, Ready.
    return prefix + 'D'*selected + 'A'


def verify_converted_install(binary, pack, work, fixture, gpu_probe, video_driver, gles=False):
    """Install the generated archive, save its selection, and relaunch unaided."""
    deadline = time.monotonic()+175
    check(not work.exists() and gpu_probe is not None,
          'converted ZIP installation needs GPU and a fresh work directory')
    metadata = json.loads((fixture/'fixture.json').read_text())
    archive = fixture/'mods/converted-courses.zip'
    source_hash = hashlib.sha256(pack.read_bytes()).hexdigest()
    archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    check(metadata.get('input_kind','catalog') == 'catalog'
          and metadata['source_sha256'] == source_hash
          and metadata['archive_sha256'] == archive_hash,
          'converted ZIP fixture does not match its owner pack/archive')
    course = next(row['id'] for row in metadata['courses'] if row['modes']&2)
    probe = work/'probe'
    probe.mkdir(parents=True)
    environment = dict(os.environ,SDL_VIDEO_DRIVER=video_driver,SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1',SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    remaining = deadline-time.monotonic()
    check(remaining>0, 'converted ZIP setup exhausted its time budget')
    result = subprocess.run([str(gpu_probe.resolve())],env=environment,cwd=probe,
                            timeout=min(30,remaining))
    if result.returncode == 77:
        print('Converted ZIP install skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(result.returncode == 0, 'converted ZIP GPU probe failed')
    runner = LocalVideoRoute(binary,pack,work,'vs2-wide120')
    runner.deadline = deadline
    data = work/'player'
    common = dict(SDL_VIDEO_DRIVER=video_driver,MKSC_DATA_DIR=str(data),
                  MKSC_MENU_TOUR_PERSIST_SETTINGS='1',MKSC_AID_MODERN_MENUS='1',
                  MKSC_MODERN_MENU_SIZE='320x180')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    # No --mods-dir or --mod: discovery must use the actual player data folder.
    arguments = ['--no-classic-menus','--short-transitions']
    def tour(name, script, **extra):
        _,log = runner.run(name,['--menu-tour',pack,work/'save',script,
                                work/(name+'-frames'),*arguments],**common,**extra)
        check('Menu tour: PASS' in log, name+': ordinary converted installation tour failed')
        return log
    log = tour('install','DDDArrrF...',MKSC_MENU_TOUR_DROP_FILE=str(archive))
    check('mod archive installed: converted-courses 1.0.0' in log,
          'generated archive did not complete the ordinary one-step install')
    installed = data/'mods/converted-courses.zip'
    check(installed.is_file() and hashlib.sha256(installed.read_bytes()).hexdigest()==archive_hash,
          'installer did not publish the exact generated ZIP in the player data folder')
    check('mod selection applied: count=1' in log, 'installed converted ZIP was not turned on')
    settings = data/'settings.json'
    saved = settings.read_bytes()
    check(json.loads(saved)['mods']==['converted-courses'],
          'ordinary Apply did not save the converted selection')
    log = tour('relaunch',converted_browser_prefix('tt',0)+'..S.A...')
    check('mod loaded: converted-courses' in log
          and re.search(rf'3D Time Trial start: course={course} driver=2 profile=\d+',log),
          'fresh process did not discover the installed selection and start its real TT')
    pauses = re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)',log)
    check(len(pauses)==2 and pauses[0][0]=='1' and pauses[1][0]=='0'
          and pauses[0][1]==pauses[1][1], 'installed TT pause advanced simulation')
    check('3D renderer: GPU driver=' in log and '3D renderer: CPU' not in log
          and 'GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, 'installed converted TT fell back from its GPU path')
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'installed TT did not use GLES')
    frames = sorted((work/'relaunch-frames').glob('*.ppm'))
    check(frames and all(read_ppm(path)[:2]==(320,180) for path in frames),
          'installed converted TT lost its physical canvas extent')
    meaningful = False
    for path in frames[-4:]:
        pixels = read_ppm(path)[2]
        meaningful |= len({pixels[i:i+3] for i in range(0,len(pixels),3)})>16
    check(meaningful, 'installed TT captures contain no meaningful picture')
    check(settings.read_bytes()==saved and not list(work.rglob('*.ghost'))
          and not list(work.rglob('*.pb')),
          'unfinished installed TT changed settings or published a ghost')
    check(hashlib.sha256(pack.read_bytes()).hexdigest()==source_hash
          and hashlib.sha256(archive.read_bytes()).hexdigest()==archive_hash,
          'ZIP installation or relaunch changed its source content')
    check(time.monotonic()<=deadline, 'converted ZIP install exceeded its 175-second budget')
    backend = 'gles' if gles else 'gpu'
    print(f'Converted 3D ZIP install: PASS backend={backend} review apply relaunch TT pause immutable',flush=True)
    return 0



EXAMPLE_INSTALL_CASES = ('bridge-3d','dual-2d','dual-3d')


def example_install_prefix(case):
    """Use normal course-set navigation, without selecting an asset by ID.

    >>> [example_install_prefix(case) for case in EXAMPLE_INSTALL_CASES]
    ['SDAAUAAEA', 'SDAAUAAllAAA', 'SDAAUAAEA']
    """
    check(case in EXAMPLE_INSTALL_CASES,'unknown installed example case')
    # Both character confirmations precede shoulder L on the ordinary cup page:
    # back past the mod's 3D cup row to Custom. Cup/Track/Ghost confirm then
    # starts the recovered Time Trial.
    return 'SDAAUAAllAAA' if case=='dual-2d' else converted_browser_prefix('tt',0)


def verify_installed_2d_record(path,course):
    """Require actual accepted source controls and movement, never menu-only proof.

    >>> with tempfile.TemporaryDirectory() as directory:
    ...     path=Path(directory)/'record.txt'
    ...     header='MKSC-RECOVERED-REPLAY 1 00000000 128 1 1 1 1 1 2 1 abc'
    ...     ticks=['T %d 0010 0 00000000 1 %d 0 0 0 0 0 1 65536 0 0' % (i,i*65536)
    ...            for i in range(201)]
    ...     count=path.write_text('\\n'.join([header,*ticks,'A pause 1','A pause 0'])+'\\n')
    ...     verify_installed_2d_record(path,128)
    ...     try:
    ...         verify_installed_2d_record(path,129)
    ...     except AssertionError as error:
    ...         print(error)
    201
    installed 2D route recorded a different course
    """
    lines=path.read_text().splitlines()
    header=lines[0].split() if lines else []
    check(len(header)>10 and header[0]=='MKSC-RECOVERED-REPLAY' and int(header[3])==course,
          'installed 2D route recorded a different course')
    ticks=[line.split() for line in lines if line.startswith('T ')]
    check(len(ticks)>180 and all(len(row)>15 for row in ticks),
          'installed 2D course omitted accepted simulation ticks')
    # T tick/buttons/disposition/checksum precedes named recovered state fields:
    # phase, position_x, position_y, position_z, pose_x, pose_y, cell, valid, speed.
    positions={(int(row[6]),int(row[7])) for row in ticks}
    check(any(int(row[2],16)&16 for row in ticks)
          and any(int(row[13])>0 for row in ticks) and len(positions)>30,
          'installed 2D route never drove its actual recovered kart')
    check(max(x for x,y in positions)-min(x for x,y in positions)>65536
          or max(y for x,y in positions)-min(y for x,y in positions)>65536,
          'installed 2D movement was less than one source unit')
    check('A pause 1' in lines and 'A pause 0' in lines,
          'installed 2D route omitted ordinary pause and resume')
    return len(ticks)



def verify_installed_2d_telemetry(path,record):
    """The race canvas stops writing telemetry when the source results take over.

    The recorder continues the original post-finish driver turns; those later
    positions must not replace the genuine finish snapshot in this oracle.

    >>> with tempfile.TemporaryDirectory() as directory:
    ...     path=Path(directory)/'telemetry.txt';record=Path(directory)/'input.txt'
    ...     count=record.write_text('T 0 0010 0 00000000 2 10 20 0 0 0 0 1 30 0 40\\n'
    ...         'T 1 0010 2 00000000 3 11 21 0 0 0 0 1 31 0 -1\\n'
    ...         'T 2 0000 3 00000000 3 90 91 0 0 0 0 1 32 0 42\\n')
    ...     count=path.write_text('3 11 21 65535 31 0 0 0 0 3 0 0 0')
    ...     verify_installed_2d_telemetry(path,record)
    ...     count=path.write_text('3 90 91 42 32 0 0 0 0 3 0 0 0')
    ...     try:
    ...         verify_installed_2d_telemetry(path,record)
    ...     except AssertionError as error:
    ...         print(error)
    installed 2D telemetry disagrees with its accepted finish snapshot
    """
    values=list(map(int,path.read_text().split()))
    ticks=[line.split() for line in record.read_text().splitlines() if line.startswith('T ')]
    finish=next((row for row in ticks if len(row)>15 and int(row[5])==3),None)
    check(len(values)==13 and finish is not None
          and tuple(values[:5])==(3,int(finish[6]),int(finish[7]),int(finish[15])&65535,int(finish[13]))
          and values[9]==3,'installed 2D telemetry disagrees with its accepted finish snapshot')

def verify_example_install(binary,pack,work,case,gpu_probe,video_driver,gles=False):
    """Build, install and race the shipped editable examples through saved settings."""
    deadline=time.monotonic()+175
    check(case in EXAMPLE_INSTALL_CASES and not work.exists() and gpu_probe is not None,
          'installed examples require a fresh work folder and GPU probe')
    work.mkdir(parents=True)
    root=Path(__file__).resolve().parents[3]
    environment=dict(os.environ,SDL_VIDEO_DRIVER=video_driver,SDL_AUDIO_DRIVER='dummy',
        SDL_MAC_BACKGROUND_APP='1',SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
        SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    result=subprocess.run([str(gpu_probe.resolve())],cwd=work,env=environment,
                          timeout=min(30,deadline-time.monotonic()))
    if result.returncode==77:
        print('Installed example skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(result.returncode==0,'installed example GPU probe failed')
    module_path=root/'mods/examples'/('bridge-course' if case=='bridge-3d' else 'dual-course')/'build.py'
    spec=importlib.util.spec_from_file_location('installed_course_example',module_path)
    builder=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    folder,archive=(builder.build(work/'authored/mods') if case=='bridge-3d'
                    else builder.build(pack,work/'authored/mods'))
    mod_id='bridge-course' if case=='bridge-3d' else 'dual-course'
    course=65536 if case=='bridge-3d' else 128
    source_hash=hashlib.sha256(pack.read_bytes()).hexdigest()
    archive_hash=hashlib.sha256(archive.read_bytes()).hexdigest()
    runner=LocalVideoRoute(binary,pack,work,'vs2-wide120')
    runner.deadline=deadline
    data=work/'player'
    common=dict(SDL_VIDEO_DRIVER=video_driver,MKSC_DATA_DIR=str(data),
                MKSC_MENU_TOUR_PERSIST_SETTINGS='1',MKSC_AID_MODERN_MENUS='1',
                MKSC_MODERN_MENU_SIZE='320x180')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU']='1'
    arguments=['--no-classic-menus','--short-transitions']
    def tour(name,script,**extra):
        _,log=runner.run(name,['--menu-tour',pack,work/'save',script,
                              work/(name+'-frames'),*arguments],**dict(common,**extra))
        check('Menu tour: PASS' in log,name+': installed example ordinary tour failed')
        return log
    log=tour('install','DDDArrrF...',MKSC_MENU_TOUR_DROP_FILE=str(archive))
    check(f'mod archive installed: {mod_id} 1.0.0' in log,
          'example ZIP did not complete the ordinary one-step install')
    installed=data/'mods'/f'{mod_id}.zip'
    check(installed.is_file() and hashlib.sha256(installed.read_bytes()).hexdigest()==archive_hash,
          'installed example ZIP differs from the generated archive')
    check('mod selection applied: count=1' in log,'example ZIP was not turned on')
    settings=data/'settings.json'
    saved_settings=settings.read_bytes()
    check(json.loads(saved_settings)['mods']==[mod_id],
          'ordinary Apply did not save the example selection')
    prefix=example_install_prefix(case)
    if case=='dual-2d':
        record=work/'recovered-input.txt'
        telemetry=work/'recovered-telemetry.txt'
        pause=work/'recovered-pause.ppm'
        # The existing recovered menu-tour input owner follows source waypoints;
        # actual accepted controls/states are independently written below.
        log=tour('race',prefix+'...',MKSC_MENU_TOUR_RACES='1',
                 MKSC_TIME_TRIAL_RECORD=str(record),MKSC_TIME_TRIAL_TELEMETRY=str(telemetry),
                 MKSC_PAUSE_CAPTURE=str(pause),MKSC_RACE_FRAME=str(work/'recovered-world.ppm'))
        check(f'mod loaded: {mod_id}' in log and '3D Time Trial start:' not in log,
              'dual example did not select its recovered 2D projection')
        ticks=verify_installed_2d_record(record,course)
        verify_installed_2d_telemetry(telemetry,record)
        check(pause.is_file(),'installed 2D drive omitted its ordinary pause capture')
        check(read_ppm(pause)[:2]==(240,160), 'installed 2D pause lost its original native canvas')
        finish=re.search(rf'Game shell Time Trial: PASS course={course} ticks=(\d+) results=returned',log)
        check(finish is not None and 'Time Trial pause aid: PASS captured and resumed through ordinary input' in log,
              'installed recovered TT did not genuinely finish and return after pause/resume')
        world=work/'recovered-world.ppm'
        check(world.is_file() and len(set(read_ppm(world)[2]))>16,
              'installed 2D route omitted its real world capture')
        # This race process is already a fresh flag-free owner after the
        # separate install and Apply processes; no second complete race is needed.
        detail=f'finish ticks={finish[1]} accepted={ticks} telemetry pause flag-free launch'
    else:
        log=tour('race',prefix+'.GDA..S.A..B..')
        check(f'mod loaded: {mod_id}' in log and re.search(
              rf'3D Time Trial start: course={course} driver=2 profile=\d+ revision=\d+ ghost=0',log),
              'flag-free installed example did not start its real 3D course')
        check('3D renderer: GPU driver=' in log and '3D renderer: CPU' not in log
              and 'GPU UI compositor: driver=' in log and 'GPU UI fallback:' not in log,
              'installed example fell back from the GPU scene/UI path')
        if gles:check('3D renderer: GPU driver=opengles2' in log,'installed example did not use GLES')
        finish=re.search(rf'3D Time Trial finish: course={course} ticks=(\d+) laps=3 '
                         r'checksum=([0-9a-f]{16}) recording=1',log)
        check(finish is not None,'installed example did not genuinely finish three laps')
        ghosts=list(work.rglob('*.ghost'))
        check(len(ghosts)==1,'installed example did not save exactly one ghost')
        saved=ghosts[0].read_bytes()
        check(saved[:8]==b'MKSC3GHO' and len(saved)>112,'installed ghost has no mesh wire header')
        version,header=struct.unpack_from('<II',saved,8)
        check((version,header) in ((1,88),(2,112),(3,128)), 'installed ghost has an invalid codec')
        # Replay tools deliberately take explicit content rather than loading
        # player settings. Select only the installed IDs already validated by
        # ordinary Apply; race and fresh-owner launches stay flag-free.
        replay_content=['--mods-dir',data/'mods']
        for selected in json.loads(saved_settings)['mods']:
            replay_content += ['--mod',selected]
        _,replay=runner.run('verify',['--mesh-finished-replay',pack,ghosts[0],
                                    *arguments,*replay_content],**common)
        check(re.search(rf'3D Time Trial replay: PASS course={course} driver=2 .*checksum={finish[2]}',replay),
              'installed saved-input replay disagrees with the real finish')
        reloaded=tour('relaunch',prefix+'..S.A...')
        check(re.search(rf'3D Time Trial start: course={course} driver=2 profile=\d+ revision=\d+ ghost=1',reloaded),
              'fresh flag-free example launch did not load its genuine saved ghost')
        pauses=re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)',reloaded)
        check(len(pauses)==2 and pauses[0][0]=='1' and pauses[1][0]=='0'
              and pauses[0][1]==pauses[1][1], 'installed example pause advanced simulation')
        check(ghosts[0].read_bytes()==saved,'unfinished installed relaunch changed its saved ghost')
        detail=f'finish ticks={finish[1]} replay ghost reload pause'
    frames=sorted((work/'race-frames').glob('*.ppm'))
    check(frames and all(read_ppm(path)[:2]==(320,180) for path in frames),
          'installed example omitted real race captures')
    pictures=[read_ppm(path)[2] for path in frames[-4:]]
    check(any(len({pixels[i:i+3] for i in range(0,len(pixels),3)})>16 for pixels in pictures),
          'installed example race pictures are empty')
    check(settings.read_bytes()==saved_settings and hashlib.sha256(pack.read_bytes()).hexdigest()==source_hash
          and hashlib.sha256(archive.read_bytes()).hexdigest()==archive_hash
          and hashlib.sha256(installed.read_bytes()).hexdigest()==archive_hash,
          'example racing changed saved selection or immutable source/installed content')
    check(time.monotonic()<=deadline,'installed example exceeded its175-second budget')
    print(f'Course example install: PASS case={case} backend={"gles" if gles else "gpu"} review apply {detail}',flush=True)
    return 0

def converted_gp_saved_checkpoint(work,cup,driver=2):
    """Admit only the actual completed four-race save, never a partial/stale cup."""
    race_log=work/'race/race.log'
    check(race_log.is_file(),'converted GP reload requires its completed race log')
    log=race_log.read_text()
    races=re.findall(r'3D GP race: cup=(\d+) index=(\d+) course=(\d+)',log)
    check([(int(c),int(i),int(course)) for c,i,course in races]==
          [(cup['id'],i,course) for i,course in enumerate(cup['courses'])],
          'converted GP reload requires its complete four-course order')
    complete=re.findall(rf'3D GP complete: cup={cup["id"]} rank=(\d+) points=(\d+) podium=[\d,]+',log)
    check(len(complete)==1 and 'Menu tour: PASS' in log
          and f'3D GP save: status=ok updated=1 cup={cup["id"]} ' in log,
          'converted GP reload requires its saved and finished cup ceremony')
    saves=sorted((work/'save/mesh3d').glob('*.cup'))
    check(len(saves)==1,'converted GP reload requires one completed cup save')
    saved=saves[0].read_bytes()
    check(len(saved)==128 and saved[:8]==b'MKSC3CUP'
          and struct.unpack_from('<5I',saved,8)==(1,128,cup['id'],0,driver)
          and struct.unpack_from('<4I',saved,32)==tuple(cup['courses'])
          and struct.unpack_from('<I',saved,28)[0]+1==int(complete[0][0])
          and struct.unpack_from('<I',saved,80)[0]==int(complete[0][1])
          and all(struct.unpack_from('<4I',saved,84))
          and all(rank<=3 for rank in struct.unpack_from('<4I',saved,100)),
          'converted GP reload save is incomplete or belongs to another cup/driver')
    return saves[0],saved


def verify_converted_gp_reload(runner,pack,work,prefix,arguments,common,cup,driver):
    path,saved=converted_gp_saved_checkpoint(work,cup,driver)
    # A reload-only CTest retry must preserve the cup, not reuse old captures.
    for name in ('relaunch','relaunch-frames'):
        shutil.rmtree(work/name,ignore_errors=True)
    _,reloaded=runner.run('relaunch',['--menu-tour',pack,work/'save',
        prefix+'..S.DA..S.DDA.B.',work/'relaunch-frames',*arguments],**common)
    check(f'3D GP load: cup={cup["id"]} status=ok' in reloaded,
          'converted GP did not reload saved cup progress')
    check('3D renderer: GPU driver=' in reloaded and '3D renderer: CPU' not in reloaded
          and 'GPU UI compositor: driver=' in reloaded and 'full_canvas_upload=0' in reloaded
          and 'GPU UI fallback:' not in reloaded,'converted GP reload lost its GPU presentation')
    check(path.read_bytes()==saved,'unfinished converted GP changed completed progress')
    check(not list(work.rglob('*.ghost')) and not list(work.rglob('*.pb')),
          'converted GP reload wrote TT persistence')


def verify_converted(binary, pack, work, fixture, mode, index, gpu_probe, video_driver,
                     gles=False, hold_second=False, gp_inputs=None, gp_driver=2, gp_routes=None,
                     tt_waits=0, required_contacts=(), tt_inputs=None, input_race=None, trace=None, gp_stage="all", sky_phase=False, elevation=False):
    check(gp_stage in ("all","race","reload") and (gp_stage=="all" or mode=="gp"),
          "converted GP stages require a GP race or reload")
    check(gp_stage!='reload' or (not hold_second and gp_inputs is None and gp_routes is None
          and not required_contacts and tt_inputs is None and input_race is None and trace is None),
          'converted GP reload cannot accept race-driving or contact options')
    check(not sky_phase or mode=='tt','sky phase capture requires converted TT')
    check(not elevation or (mode=='tt' and not sky_phase and trace is None),
          'elevation acceptance requires ordinary TT and owns its bounded trace')
    check(mode=='tt' or not tt_waits,
          'converted timing waits require a TT route')
    check(not required_contacts or mode in ('tt','gp'), 'source contact requirements need a converted TT or GP race')
    check(tt_inputs is None or mode in ('tt','gp'), 'ordinary input segments require a converted TT or GP race')
    check(input_race is None or (mode=='gp' and (tt_inputs is not None or required_contacts) and 0<=input_race<4),
          'ordinary input race scope requires segments or contacts and a GP ordinal0..3')
    check(0<=tt_waits<=32, 'converted TT waits exceed the bounded input budget')
    check(mode=='gp' or (not hold_second and gp_inputs is None and gp_routes is None),
          'converted GP input choices require a converted GP route')
    check((work.is_dir() if gp_stage=='reload' else not work.exists()) and gpu_probe is not None,
          'converted reload needs completed work; other routes need GPU and fresh work')
    metadata = json.loads((fixture/'fixture.json').read_text())
    check(not elevation or metadata.get('handpass_sha256'), 'elevation acceptance requires a hand-pass fixture')
    authored = metadata.get('input_kind') == 'track-folder'
    if authored:
        check(mode == 'tt' and converted_tree_hash(fixture/'author-input') == metadata['input_sha256'],
              'authored conversion input changed before its TT route')
    check(metadata['version'] == 1 and hashlib.sha256(pack.read_bytes()).hexdigest() == metadata['source_sha256'],
          'converted fixture belongs to another owner pack')
    content = fixture/'mods/converted-courses/content.pack'
    archive = fixture/'mods/converted-courses.zip'
    check(hashlib.sha256(content.read_bytes()).hexdigest() == metadata['content_sha256']
          and hashlib.sha256(archive.read_bytes()).hexdigest() == metadata['archive_sha256'],
          'converted fixture content changed after deterministic setup')
    bit = dict(tt=2,gp=1,battle=16)[mode]
    visible = [row for row in metadata['courses'] if row['modes'] & bit]
    if mode == 'gp':
        check(0 <= index < len(metadata['cups']), 'converted GP index outside fixture')
        cup = metadata['cups'][index]
        course_ids = cup['courses']
    else:
        check(0 <= index < len(visible), 'converted course index outside fixture')
        course_ids = [visible[index]['id']]
    selected = next(i for i,row in enumerate(visible) if row['id'] == course_ids[0])
    prefix = converted_browser_prefix(mode,selected,gp_driver)
    environment = dict(os.environ,SDL_VIDEO_DRIVER=video_driver,SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1',SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe_directory = work/('reload-probe' if gp_stage=='reload' else 'probe')
    probe_directory.mkdir(parents=True,exist_ok=gp_stage=='reload')
    probe = subprocess.run([str(gpu_probe.resolve())],env=environment,cwd=probe_directory,timeout=30)
    if probe.returncode == 77:
        print('Converted 3D route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU device probe failed')
    runner = LocalVideoRoute(binary,pack,work,'vs2-wide120')
    common = dict(SDL_VIDEO_DRIVER=video_driver,MKSC_DATA_DIR=str(work/'player'),
                  MKSC_AID_MODERN_MENUS='1',MKSC_MODERN_MENU_SIZE='320x180')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    if elevation:trace='180:4275'
    if trace is not None:
        common['MKSC_AID_MESH_TRACE']=trace
    if gp_routes is not None:
        common['MKSC_AID_MESH_GP_ROUTES']=','.join(gp_routes)
        print('Converted GP route hints: '+','.join(gp_routes),flush=True)
    arguments = ['--mods-dir',fixture/'mods','--mod','converted-courses',
                 '--no-classic-menus','--short-transitions']
    if gp_stage=='reload':
        verify_converted_gp_reload(runner,pack,work,prefix,arguments,common,cup,gp_driver)
        check(hashlib.sha256(pack.read_bytes()).hexdigest()==metadata['source_sha256']
              and hashlib.sha256(content.read_bytes()).hexdigest()==metadata['content_sha256']
              and hashlib.sha256(archive.read_bytes()).hexdigest()==metadata['archive_sha256'],
              'converted GP reload changed source or generated content')
        pictures=sorted((work/'relaunch-frames').glob('*.ppm'))
        check(pictures and all(read_ppm(picture)[:2]==(320,180) for picture in pictures),
              'converted GP reload lost its physical presentation/capture size')
        print(f'Converted 3D GP reload: PASS index={index} cup={cup["id"]}',flush=True)
        return 0
    if tt_inputs is not None:
        common['MKSC_AID_MESH_INPUTS']=tt_inputs
        if input_race is not None:
            common['MKSC_AID_MESH_INPUT_RACE']=str(input_race)
    if mode == 'gp':
        # Holding pickups is an explicit route choice, not a requirement to
        # qualify every cup without using its second race's inventory.
        strategy = converted_gp_strategy(gp_inputs,hold_second)
        print('Converted GP input strategy: '+','.join(strategy)+f' driver={gp_driver}', flush=True)
        if 'K' in strategy:
            negative_env={key:value for key,value in os.environ.items() if not key.startswith('MKSC_')}
            negative_env.update(common)
            negative_env.update(SDL_AUDIO_DRIVER='dummy',SDL_MAC_BACKGROUND_APP='1',
                SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
            negative_env.pop('MKSC_AID_MESH_GP_ROUTES',None)
            negative_env['MKSC_DATA_DIR']=str(work/'k-invalid-data')
            remaining=runner.deadline-time.monotonic()
            check(remaining>0,'converted K rejection exceeded its existing route budget')
            negative=subprocess.run([str(binary),'--menu-tour',str(pack),str(work/'k-invalid-save'),
                'K',str(work/'k-invalid-frames'),'--no-mods','--no-classic-menus','--short-transitions'],
                env=negative_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                timeout=min(30,remaining),text=True)
            (work/'k-invalid.log').write_text(negative.stdout,encoding='utf-8')
            check(negative.returncode!=0 and
                  'K item aid requires a live shared 3D GP race' in negative.stdout
                  and 'Menu tour: PASS' not in negative.stdout,
                  'K was not rejected clearly outside a live 3D GP race')
        tour = prefix + '.' + ''.join(token+'.A.A.' for token in strategy) + '...A.B.'
    elif mode == 'battle':
        tour = prefix + '..S.A.G.A..S.DDA.B.'
    else:
        check(all(visible[index].get(family,{}).get('count',0)>0 for family in required_contacts),
              'required converted contact family is absent from the selected course')
        if sky_phase:
            check(visible[index].get('sky',{}).get('controller') in (2,4,5),
                  'sky phase capture needs a source progress controller')
            if visible[index]['sky']['controller']==4:
                common['MKSC_AID_MESH_PALETTE']='1'
        # Reach a real phase, pause/resume it, then use ordinary pause Retry
        # before reaching the phase again and saving the fresh attempt.
        tour = prefix + '..S.A.' + '.'*tt_waits + ('Y.S..AY.S.DA.Y.TTG.B.B.' if sky_phase else ('T'*144 if elevation else '')+'G.B.B.')
    # Timed and moving hazards need the seek aid to meet them, as on the authored route.
    # The seek targets only the timed or moving families the route must prove;
    # in a GP the input race scopes it, so the other races stay ordinary.
    sought=[family for family in required_contacts if family in SEEK_FAMILIES]
    if tt_inputs is None and sought:
        common['MKSC_AID_MESH_HAZARDS']=f'seek:{"*" if input_race is None else input_race}:'+','.join(sought)
    _,log = runner.run('race',['--menu-tour',pack,work/'save',tour,work/'frames',*arguments],**common)
    check('Menu tour: PASS' in log and '3D renderer: GPU driver=' in log
          and '3D renderer: CPU' not in log, 'converted course did not complete its GPU menu route')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, 'converted HUD/results fell back to CPU')
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'converted route did not use GLES depth')
    if elevation:
        grounded=[int(z) for z in re.findall(
            r'3D aid trace: .*?player=0 .*?pos=-?\d+,-?\d+,(-?\d+) .*?grounded=1 ',log)]
        check(grounded and max(grounded)-min(grounded)>32768,
              'hand-pass TT did not climb half a metre on actual grounded collision')
        peak=max(grounded);at=grounded.index(peak)
        check(any(z<peak-32768 for z in grounded[at+1:]),
              'hand-pass TT did not return down the slope while grounded')
        print(f'Converted hand-pass elevation: PASS course={course_ids[0]} '
              f'grounded_range_q16={min(grounded)}..{peak}',flush=True)

    if tt_inputs is not None:
        # The host validates syntax and bounds. Require every requested segment
        # to reach the actual human driver, rather than silently missing its turn.
        selected_courses=course_ids if input_race is None else [course_ids[input_race]]
        for segment in tt_inputs.split(';'):
            start,count,steering,buttons=map(int,segment.split(':'))
            for course in selected_courses:
                check(f'3D input segment: course={course} mode={bit} player=0 tick={start} '
                      f'count={count} steering={steering} buttons={buttons}' in log,
                      'converted race did not admit every ordinary human input segment')
                if input_race is not None:
                    check(f'3D input race scope: course={course} race={input_race} tick={start}' in log,
                          'converted GP lost its explicit input race scope')
        if input_race is not None:
            scoped_courses=set(map(int,re.findall(r'3D input segment: course=(\d+) ',log)))
            check(scoped_courses==set(selected_courses), 'ordinary input segments leaked to another GP race')
    pauses = re.findall(r'3D (?:Time Trial|Battle) pause: paused=([01]) tick=(\d+)',log)
    if mode != 'gp':
        check(len(pauses) >= 2 and pauses[0][0] == '1' and pauses[1][0] == '0'
              and pauses[0][1] == pauses[1][1],
              'converted pause advanced the accepted simulation')
        # Battle deliberately quits its retried match from the pause menu.
        check(len(pauses) == (3 if mode == 'battle' else 5 if sky_phase else 2)
              and (mode != 'battle' or pauses[2][0] == '1'),
              'converted route entered an unexpected pause lifecycle')
    gp_coin_courses=set();gp_coin_pickups=set();gp_coin_losses=set()
    for entry in visible:
        if entry['id'] not in course_ids or not entry.get('coins'):continue
        owner=entry['coins']
        active=bool(owner['modes']&bit)
        # Coins are optional in an individual GP race. Audit every transaction,
        # and require the cup's real pickup/loss witnesses rather than inventing
        # a mandatory coin cell on each selected racing line.
        pickups,losses=verify_coins(log,{entry['id']},bit,4 if mode=='battle' else 8 if mode=='gp' else 1,
                     2 if mode=='battle' else 1,enabled=active,
                     require_pickup=active and owner['count']>0 and mode!='gp',
                     require_loss=False,placements=owner['count'])
        if mode=='gp' and active:
            if owner['count']>0:gp_coin_courses.add(entry['id'])
            gp_coin_pickups.update(pickups);gp_coin_losses.update(losses)
    if mode=='gp' and gp_coin_courses:
        check(gp_coin_pickups and gp_coin_losses,'converted GP cup omitted genuine coin pickup/loss gameplay')
        check(re.search(r'3D item cue: .*song=101 ',log),'converted GP coin pickup omitted actual audio')
    verify_item_owner(log,bit,4 if mode == 'battle' else 8 if mode == 'gp' else 1,course_ids)
    if sky_phase:
        sky=visible[index]['sky']
        phases=re.findall(r'3D sky aid: accepted course=(\d+) controller=(\d+) phase=(\d+) tick=(\d+) progress=(\d+)',log)
        check(len(phases)==3,'progress sky did not reach phase captures before and after Retry')
        check(all(int(c)==course_ids[0] and int(controller)==sky['controller'] and (int(phase)==6 if sky['controller']==4 else int(phase)>0)
                  and int(progress)>>8 >= (130 if sky['controller']==2 else 129)
                  for c,controller,phase,tick,progress in phases),
              'sky phase capture did not follow genuine source progress')
        check(int(phases[1][3])>int(phases[0][3]),'sky clock failed to resume after phase pause')
        captures=list(re.finditer(r'3D sky aid: accepted course=\d+ controller=\d+ phase=\d+ tick=\d+ progress=\d+',log))
        pauses=re.findall(r'3D Time Trial pause: paused=([01]) tick=(\d+)',
                          log[captures[0].end():captures[1].start()])
        check(len(pauses)==2 and pauses[0][0]=='1' and pauses[1][0]=='0'
              and pauses[0][1]==pauses[1][1], 'progress sky pause advanced accepted state')
        resets=re.findall(r'3D sky aid reset: course=(\d+) controller=(\d+) before_tick=(\d+) '
                          r'before_phase=(\d+) tick=(\d+) phase=(\d+) progress=(\d+) checksum=([0-9a-f]{16})',log)
        check(len(resets)==1 and int(resets[0][0])==course_ids[0]
              and int(resets[0][1])==sky['controller'] and int(resets[0][2])>=int(phases[1][3])
              and int(resets[0][3])>0 and tuple(map(int,resets[0][4:7]))==(0,0,0x7f00)
              and int(resets[0][7],16)!=0,
              'ordinary Retry did not restore a fresh progress-sky owner')
        if sky['controller']==4:
            # Palette diagnostics describe accepted changes only. Both genuine
            # attempts must traverse all64 source sunset frames, rather than a
            # static final palette or progress phase masquerading as animation.
            reset=log.index('3D sky aid reset:')
            pattern=(r'3D palette state: course=(\d+) tick=(\d+) phase=(\d+) '
                     r'sunset-frame=(\d+) timer-frame=(\d+) variant=(\d+) split=(-?\d+)')
            for attempt,part in enumerate((log[:reset],log[reset:])):
                palette_states=[tuple(map(int,row)) for row in re.findall(pattern,part)]
                check(palette_states and all(c==course_ids[0] and tick>=0 and 0<=phase<=6
                      and 0<=frame<64 and 0<=timer<3 and 0<=variant<512
                      for c,tick,phase,frame,timer,variant,split in palette_states),
                      f'sunset attempt{attempt+1} omitted valid accepted palette diagnostics')
                check({row[3] for row in palette_states}==set(range(64)),
                      f'sunset attempt{attempt+1} did not traverse every source palette frame')
                check(len({row[4] for row in palette_states})>=2
                      and len({row[5] for row in palette_states})>=2,
                      f'sunset attempt{attempt+1} did not retain road timer and palette variants')
                check(any(row[2]==6 and row[6]==24 for row in palette_states)
                      and all(row[6]==24 for row in palette_states if row[2]==6),
                      f'sunset attempt{attempt+1} omitted its source phase6 split24')
            print(f'Converted sunset palettes: PASS course={course_ids[0]} frames64 timer split24 Retry',flush=True)
        starts=re.findall(r'3D Time Trial start: course=(\d+) driver=(\d+) profile=(\d+) revision=(\d+) ghost=(\d+)',log)
        check(len(starts)==2 and starts[0]==starts[1],
              'progress sky Retry changed course, driver, profile or initial ghost binding')
        first=tour.index('Y');second=tour.index('Y',first+1)
        pictures=[read_ppm(work/'frames'/f'{i:03}.ppm') for i in (first,second)]
        check(all((w,h)==(320,180) for w,h,_ in pictures) and pictures[0][2]!=pictures[1][2],
              'progress sky omitted distinct actual GPU race captures')
        print(f'Converted progress sky: PASS course={course_ids[0]} controller={sky["controller"]} phase pause resume Retry frames',flush=True)
    if mode == 'tt':
        course = course_ids[0]
        laps = visible[index]['laps']
        finish = re.search(rf'3D Time Trial finish: course={course} ticks=(\d+) laps={laps} '
                           r'checksum=([0-9a-f]{16}) recording=1',log)
        check(finish is not None, 'converted TT did not finish its genuine lap count')
        ghosts = sorted(work.rglob('*.ghost'))
        check(len(ghosts) == 1, 'converted TT did not publish its own saved ghost')
        saved = ghosts[0].read_bytes()
        crab_actors=visible[index].get('crabs')
        snowman_actors=visible[index].get('snowmen')
        penguin_actors=visible[index].get('penguins')
        mouse_actors=visible[index].get('mice')
        piranha_actors=visible[index].get('piranhas')
        seagull_actors=visible[index].get('seagulls')
        lava_actors=visible[index].get('lava_bubbles')
        thwomp_actors=visible[index].get('thwomps')
        cannon_actors=visible[index].get('cannonballs')
        boo_actors=visible[index].get('boos')
        rock_actors=visible[index].get('rocks')
        teepee_actors=visible[index].get('teepees')
        falling_actors=visible[index].get('falling_stars')
        cloud_actors=visible[index].get('thunderclouds')
        common_actors=visible[index].get('common_props')
        mecha_actors=visible[index].get('mechakoopas')
        kamek_actors=visible[index].get('kamek')
        pier_actors=visible[index].get('pier_boos')
        coin_owner=visible[index].get('coins')
        actors=crab_actors or snowman_actors or penguin_actors or mouse_actors or piranha_actors or seagull_actors or lava_actors or thwomp_actors or cannon_actors or boo_actors or rock_actors or teepee_actors or falling_actors or cloud_actors or common_actors or mecha_actors or kamek_actors or pier_actors or coin_owner
        version,header=(3,128) if actors else (2,112)
        check(len(saved)>=header and saved[:8] == b'MKSC3GHO'
              and struct.unpack_from('<II',saved,8) == (version,header),
              f'converted item TT did not save the version{version} codec')
        if actors:
            check(struct.unpack_from('<II',saved,112)==(actors['profile_id'],actors['revision'])
                  and struct.unpack_from('<Q',saved,120)[0]!=0,
                  'converted actor ghost lost its explicit rules/initial state')
            record_start=header+44*4
            framed=len(saved)-record_start-44*4
            check(framed>=80 and framed%40==0
                  and struct.unpack_from('<I',saved,record_start+framed-40)[0]==0xffffffff
                  and struct.unpack_from('<Q',saved,record_start+framed-8)[0]!=0,
                  'converted actor ghost lost its40-byte actor checksum record framing')
            if crab_actors:
                verify_crab_actor_turns(log,{course},{course:crab_actors},require_contact='crabs' in required_contacts)
            if snowman_actors:
                verify_snowman_actor_turns(log,{course},{course:snowman_actors},require_contact='snowmen' in required_contacts)
            if penguin_actors:
                verify_penguin_actor_turns(log,{course},{course:penguin_actors},require_contact='penguins' in required_contacts)
            if mouse_actors:
                verify_mouse_actor_turns(log,{course},{course:mouse_actors},require_contact='mice' in required_contacts)
            if piranha_actors:
                verify_piranha_actor_turns(log,{course},{course:piranha_actors},require_contact='piranhas' in required_contacts)
            if seagull_actors:
                verify_seagull_actor_turns(log,{course},{course:seagull_actors},require_contact='seagulls' in required_contacts)
            if lava_actors:
                verify_lava_actor_turns(log,{course},{course:lava_actors},require_contact='lava_bubbles' in required_contacts)
            if thwomp_actors:
                verify_thwomp_actor_turns(log,{course},{course:thwomp_actors},require_contact='thwomps' in required_contacts,require_bump=False)
            if cannon_actors:
                verify_cannonball_actor_turns(log,{course},{course:cannon_actors},require_contact='cannonballs' in required_contacts)
            if boo_actors:
                verify_boo_actor_turns(log,{course},{course:boo_actors},require_contact='boos' in required_contacts)
            if pier_actors:
                verify_pier_boo_actor_turns(log,{course},{course:pier_actors},require_contact='pier_boos' in required_contacts,
                                           coins_enabled=bool(coin_owner and coin_owner['modes']&bit))
            if kamek_actors:
                verify_kamek_actor_turns(log,{course},{course:kamek_actors},require_contact='kamek' in required_contacts,require_patrol=False)
            if mecha_actors:
                verify_mechakoopa_actor_turns(log,{course},{course:mecha_actors},require_contact='mechakoopas' in required_contacts,require_cycle=False)
            if common_actors:
                verify_common_prop_actor_turns(log,{course},{course:common_actors},require_contact='common_props' in required_contacts)
            if cloud_actors:
                verify_thundercloud_actor_turns(log,{course},{course:cloud_actors},require_contact='thunderclouds' in required_contacts)
            if falling_actors:
                verify_falling_star_actor_turns(log,{course},{course:falling_actors},require_contact='falling_stars' in required_contacts)
            if teepee_actors:
                verify_teepee_actor_turns(log,{course},{course:teepee_actors},require_contact='teepees' in required_contacts)
            if rock_actors:
                verify_rock_actor_turns(log,{course},{course:rock_actors},require_contact='rocks' in required_contacts)
        _,verified = runner.run('verify',['--mesh-finished-replay',pack,ghosts[0],*arguments],**common)
        check(re.search(rf'3D Time Trial replay: PASS course={course} driver=2 .*checksum={finish[2]}',verified),
              'converted input/item replay changed its final state')
        _,reloaded = runner.run('relaunch',['--menu-tour',pack,work/'save',prefix+'..S.A...',
                                          work/'relaunch-frames',*arguments],**common)
        check(re.search(rf'3D Time Trial start: course={course} driver=2 profile=\d+ revision=\d+ ghost=1',reloaded),
              'converted saved ghost failed fresh-owner reload')
        check(ghosts[0].read_bytes() == saved, 'unfinished converted relaunch changed its ghost')
        for family in sorted(set(required_contacts)):
            print(f'Converted source contact: PASS course={course} family={family} replay=v3',flush=True)
        if crab_actors:
            print(f'Converted source crabs: PASS course={course} count={actors["count"]} motion=1 replay=v3',flush=True)
        if snowman_actors:
            print(f'Converted source snowmen: PASS course={course} count={snowman_actors["count"]} replay=v3',flush=True)
        if penguin_actors:
            print(f'Converted source penguins: PASS course={course} count={penguin_actors["count"]} replay=v3',flush=True)
        if mouse_actors:
            print(f'Converted source mice: PASS course={course} count={mouse_actors["count"]} replay=v3',flush=True)
        if piranha_actors:
            print(f'Converted source piranhas: PASS course={course} count={piranha_actors["count"]} replay=v3',flush=True)
        if seagull_actors:
            print(f'Converted source seagulls: PASS course={course} count={seagull_actors["count"]} replay=v3',flush=True)
        if lava_actors:
            print(f'Converted source lava bubbles: PASS course={course} count={lava_actors["count"]} replay=v3',flush=True)
        if rock_actors:
            print(f'Converted source rocks: PASS course={course} count={rock_actors["count"]} replay=v3',flush=True)
        if boo_actors:
            print(f'Converted source boos: PASS course={course} count={boo_actors["count"]} replay=v3',flush=True)
        if cannon_actors:
            print(f'Converted source cannonballs: PASS course={course} count={cannon_actors["count"]} replay=v3',flush=True)
        if pier_actors:
            print(f'Converted source Pier Boos: PASS course={course} count={pier_actors["count"]} replay=v3',flush=True)
        if kamek_actors:
            print(f'Converted source Kamek: PASS course={course} count={kamek_actors["count"]} replay=v3',flush=True)
        if mecha_actors:
            print(f'Converted source Mechakoopas: PASS course={course} count={mecha_actors["count"]} replay=v3',flush=True)
        if common_actors:
            print(f'Converted source common props: PASS course={course} count={common_actors["count"]} replay=v3',flush=True)
        if cloud_actors:
            print(f'Converted source thunderclouds: PASS course={course} count={cloud_actors["count"]} replay=v3',flush=True)
        if falling_actors:
            print(f'Converted source falling stars: PASS course={course} count={falling_actors["count"]} replay=v3',flush=True)
        if teepee_actors:
            print(f'Converted source teepees: PASS course={course} count={teepee_actors["count"]} replay=v3',flush=True)
        if thwomp_actors:
            print(f'Converted source thwomps: PASS course={course} count={thwomp_actors["count"]} replay=v3',flush=True)
    elif mode == 'battle':
        course = course_ids[0]
        check(re.search(rf'3D Battle finish: course={course} players=2 tick=\d+ finished=1 views=2',log),
              'converted arena did not finish a real four-racer match')
        check(len(re.findall(rf'3D Battle start: course={course} players=2 ',log)) == 2,
              'converted arena Retry did not initialize a fresh match')
        results = re.findall(rf'3D Battle result: course={course} player=(\d+) driver=(\d+) rank=(\d+) human=([01])',log)
        check(len(results) == 4 and {int(row[0]) for row in results} == set(range(1,5))
              and {int(row[2]) for row in results} == set(range(1,5))
              and all(int(human) == (int(player)<=2) for player,_,_,human in results),
              'converted Battle lost human/CPU identities or standings')
        check(re.search(r'3D item pickup: .*racer=0 ',log)
              and re.search(r'3D item event: .*kind=0 .*racer=0 ',log)
              and re.search(r'3D Battle balloons: .*remaining=0 ',log),
              'converted Battle completed without genuine human items and elimination')
    else:
        check(re.search(r'3D item event: .*kind=0 .*racer=0 ',log),
              'converted GP did not exercise ordinary human item use')
        for held_course,token in zip(course_ids,strategy):
            if token!='J':
                continue
            check(re.search(rf'3D item pickup: course={held_course} tick=\d+ racer=0 ',log)
                  and not re.search(rf'3D item event: course={held_course} tick=\d+ kind=0 '
                                    r'item=[0-9a-f]+ racer=0 ',log),
                  f'converted GP did not exercise its requested held-item race course={held_course}')
        for tapped_course,token in zip(course_ids,strategy):
            if token!='K':
                continue
            uses=list(map(int,re.findall(rf'3D item event: course={tapped_course} tick=(\d+) '
                                         r'kind=0 item=[0-9a-f]+ racer=0 ',log)))
            check(uses and all(tick%120==1 for tick in uses)
                  and all(after-before>=120 for before,after in zip(uses,uses[1:])),
                  f'converted GP did not preserve fresh periodic item taps course={tapped_course}')
        races = re.findall(r'3D GP race: cup=(\d+) index=(\d+) course=(\d+)',log)
        check([(int(c),int(i),int(course)) for c,i,course in races]
              == [(cup['id'],i,course) for i,course in enumerate(course_ids)],
              'converted GP did not advance through its actual four-course order')
        if gp_routes is not None:
            for race_index,(course,hint) in enumerate(zip(course_ids,gp_routes)):
                matches=re.findall(rf'3D GP route hint: course={course} index={race_index} '
                                   r'requested=(auto|[0-9]+) route=([0-9]+)',log)
                check(matches and all(requested==hint and (hint=='auto' or int(actual)==int(hint))
                      for requested,actual in matches),
                      f'converted GP did not honor route hint index={race_index} hint={hint}')
        check(re.search(rf'3D GP complete: cup={cup["id"]} rank=\d+ points=\d+ podium=[\d,]+',log),
              'converted GP did not reach the cup ceremony')
        times = re.findall(r'3D GP time: racer=(\d+) driver=(\d+) rank=(\d+) ticks=(\d+) '
                           r'estimated=([01]) actual_finish_ticks=(\d+) lap=(\d+) stage=(\d+) '
                           r'cpu_visits=(\d+)',log)
        grids = re.findall(r'3D GP grid: course=(\d+) slots=([\d,]+)',log)
        check(len(times) == 32 and len(grids) == 4, 'converted GP omitted results or next grids')
        for race_index,course in enumerate(course_ids):
            rows = times[race_index*8:(race_index+1)*8]
            check({int(row[0]) for row in rows} == set(range(1,9))
                  and {int(row[2]) for row in rows} == set(range(1,9)),
                  'converted GP lost a participant or rank')
            expected_grid = [0]*8
            for racer,driver,rank,ticks,estimated,actual,lap,stage,visits in rows:
                check(int(ticks) > 0 and int(estimated) == (int(actual) == 0),
                      'converted GP mislabeled an estimated finish')
                expected_grid[int(racer)-1] = int(rank)-1
                if int(racer) == 1:
                    authored_laps = next(row['laps'] for row in visible if row['id'] == course)
                    check(int(driver) == gp_driver, 'converted GP changed its selected human driver')
                    check(int(rank) <= 4 and int(actual) == int(ticks) and int(lap) == authored_laps,
                          'converted GP human did not genuinely qualify')
            if race_index < 3:
                check(int(grids[race_index+1][0]) == course_ids[race_index+1]
                      and list(map(int,grids[race_index+1][1].split(','))) == expected_grid,
                      'converted GP next grid ignored the previous placement')
        awards = re.findall(r'3D GP award: course=(\d+) racer=(\d+) before=(\d+) awarded=(\d+) after=(\d+)',log)
        totals = [0]*8
        check(len(awards) == 32, 'converted GP omitted per-racer cumulative awards')
        for race_index in range(4):
            rows = awards[race_index*8:(race_index+1)*8]
            check({int(row[1]) for row in rows} == set(range(1,9)), 'converted GP omitted a racer award')
            for course,racer,before,award,after in rows:
                r = int(racer)-1
                check(int(course) == course_ids[race_index] and int(before) == totals[r]
                      and int(after) == int(before)+int(award), 'converted GP corrupted cumulative points')
                totals[r] = int(after)
        saves = sorted(work.rglob('*.cup'))
        check(len(saves) == 1, 'converted GP omitted its separate cup save')
        saved = saves[0].read_bytes()
        check(len(saved) == 128 and saved[:8] == b'MKSC3CUP'
              and struct.unpack_from('<4I',saved,32) == tuple(course_ids)
              and struct.unpack_from('<I',saved,80)[0] == totals[0],
              'converted GP persistence lost course order or points')
        if gp_stage=='all':
            verify_converted_gp_reload(runner,pack,work,prefix,arguments,common,cup,gp_driver)
    if mode=='gp' and required_contacts:
        checks={
            'crabs':(verify_crab_actor_turns,{}),
            'snowmen':(verify_snowman_actor_turns,{}),
            'penguins':(verify_penguin_actor_turns,{}),
            'mice':(verify_mouse_actor_turns,{}),
            'piranhas':(verify_piranha_actor_turns,{}),
            'seagulls':(verify_seagull_actor_turns,{}),
            'lava_bubbles':(verify_lava_actor_turns,{}),
            'thwomps':(verify_thwomp_actor_turns,{}),
            'cannonballs':(verify_cannonball_actor_turns,{}),
            'boos':(verify_boo_actor_turns,{}),
            'rocks':(verify_rock_actor_turns,{}),
            'teepees':(verify_teepee_actor_turns,{}),
            'falling_stars':(verify_falling_star_actor_turns,{}),
            'thunderclouds':(verify_thundercloud_actor_turns,{}),
            'common_props':(verify_common_prop_actor_turns,{}),
            'mechakoopas':(verify_mechakoopa_actor_turns,{'require_cycle':False}),
            'kamek':(verify_kamek_actor_turns,{'require_patrol':False}),
            'pier_boos':(verify_pier_boo_actor_turns,{'coins_enabled':True}),
        }
        for family in sorted(set(required_contacts)):
            definitions={row['id']:row[family] for row in visible if row['id'] in course_ids
                         and row.get(family,{}).get('count',0)>0}
            check(definitions,'required source family is absent from the converted cup')
            verifier,extra=checks[family]
            verifier(log,set(definitions),definitions,require_contact=True,**extra)
            print(f'Converted source contact: PASS courses={",".join(map(str,definitions))} family={family} GP',flush=True)
    if mode != 'tt':
        check(not list(work.rglob('*.ghost')) and not list(work.rglob('*.pb')),
              'converted multiplayer/GP wrote TT persistence')
    pictures = sorted((work/'frames').glob('*.ppm'))
    check(pictures and all(read_ppm(picture)[:2] == (320,180) for picture in pictures),
          'converted route lost its physical presentation/capture size')
    check(hashlib.sha256(pack.read_bytes()).hexdigest() == metadata['source_sha256']
          and hashlib.sha256(content.read_bytes()).hexdigest() == metadata['content_sha256']
          and hashlib.sha256(archive.read_bytes()).hexdigest() == metadata['archive_sha256'],
          'converted race changed source or generated content')
    if authored:
        check(converted_tree_hash(fixture/'author-input') == metadata['input_sha256'],
              'authored TT route modified its original track folder')
        print('Converted authored 3D TT route: PASS finish save replay reload immutable')
    print(f'Converted 3D {mode.upper()} route: PASS index={index} courses={",".join(map(str,course_ids))}')


def build_battle_browser_fixture(root, work, snowmen=False, penguins=False, penguins_star=False, mice=False, mice_star=False, piranhas=False, piranhas_star=False, seagulls=False, seagulls_star=False, lava_bubbles=False, lava_bubbles_star=False, thwomps=False, thwomps_star=False, cannonballs=False, cannonballs_star=False, boos=False, boos_star=False, rocks=False, rocks_star=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False):
    """Put a race before the arena, plus an independent empty-Battle catalog."""
    spec = importlib.util.spec_from_file_location('battle_browser_example',
        root/'mods/examples/bridge-course/build.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    arena = builder.source(work/'source'/('battle-arena'+('-snowmen' if snowmen else '')
        +('-penguins' if penguins else '')+('-mice' if mice else '')+('-piranhas' if piranhas else '')+('-seagulls' if seagulls else '')+('-lava-bubbles' if lava_bubbles else '')+('-thwomps' if thwomps else '')+('-cannonballs' if cannonballs else '')+('-boos' if boos else '')+('-rocks' if rocks else '')+('-coins' if coins else '')+('-teepees' if teepees else '')+('-star' if teepees_star else '')+('-falling-stars' if falling_stars else '')+('-star' if falling_stars_star else '')+('-thunderclouds' if thunderclouds else '')+('-star' if thunderclouds_star else '')+('-common-props' if common_props else '')+('-star' if common_props_star else '')+('-mechakoopas' if mechakoopas else '')+('-star' if mechakoopas_star else '')+('-kamek' if kamek else '')+('-star' if kamek_star else '')+('-pier-boos' if pier_boos else '')+('-star' if pier_boos_star else '')+('-star' if penguins_star or mice_star or piranhas_star or seagulls_star or lava_bubbles_star or thwomps_star or cannonballs_star or boos_star or rocks_star else '')),
        battle=True,items=coins,snowmen=snowmen,penguins=penguins,penguins_star=penguins_star,mice=mice,mice_star=mice_star,piranhas=piranhas,piranhas_star=piranhas_star,seagulls=seagulls,seagulls_star=seagulls_star,lava_bubbles=lava_bubbles,lava_bubbles_star=lava_bubbles_star,thwomps=thwomps,thwomps_star=thwomps_star,cannonballs=cannonballs,cannonballs_star=cannonballs_star,boos=boos,boos_star=boos_star,rocks=rocks,rocks_star=rocks_star,coins=coins,teepees=teepees,teepees_star=teepees_star,falling_stars=falling_stars,falling_stars_star=falling_stars_star,thunderclouds=thunderclouds,thunderclouds_star=thunderclouds_star,common_props=common_props,common_props_star=common_props_star,mechakoopas=mechakoopas,mechakoopas_star=mechakoopas_star,kamek=kamek,kamek_star=kamek_star,pier_boos=pier_boos,pier_boos_star=pier_boos_star)
    race = builder.source(work/'source/browser-race',course_id=65535)
    race_json = race/'courses3d/bridge/course.json'
    document = json.loads(race_json.read_text())
    document['meta']['cup_id'] = 65535
    race_json.write_text(json.dumps(document,indent=2)+'\n')
    shutil.copytree(race/'courses3d/bridge',arena/'courses3d/browser-race')
    builder.mod_kit.build(arena,None,work/'mods')
    builder.mod_kit.build(race,None,work/'empty-mods')


def verify_battle(binary, pack, work, gpu_probe, video_driver, players, gles=False, snowmen=False, penguins=False, penguins_star=False, mice=False, mice_star=False, piranhas=False, piranhas_star=False, seagulls=False, seagulls_star=False, lava_bubbles=False, lava_bubbles_star=False, thwomps=False, thwomps_star=False, cannonballs=False, cannonballs_star=False, boos=False, boos_star=False, rocks=False, rocks_star=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False):
    check(gpu_probe is not None and not work.exists(), '3D Battle requires GPU and fresh work')
    environment = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe = subprocess.run([str(gpu_probe)], env=environment, timeout=30)
    if probe.returncode == 77:
        print('3D Battle route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU device probe failed its rendering checks')
    root = Path(__file__).resolve().parents[3]
    original = hashlib.sha256(pack.read_bytes()).digest()
    build_battle_browser_fixture(root,work,snowmen,penguins,penguins_star,mice,mice_star,piranhas,piranhas_star,seagulls,seagulls_star,lava_bubbles,lava_bubbles_star,thwomps,thwomps_star,cannonballs,cannonballs_star,boos,boos_star,rocks,rocks_star,coins,teepees,teepees_star,falling_stars,falling_stars_star,thunderclouds,thunderclouds_star,common_props,common_props_star,mechakoopas,mechakoopas_star,kamek,kamek_star,pier_boos,pier_boos_star)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    # Confirm the final driver's Ready page, then Select opens the 3D browser
    # before the native arena row is confirmed.
    if players == 1:
        prefix = 'SUAAUAAEA'
    else:
        prefix = 'DA' + 'U'*(players-2) + 'ADAA'
        prefix += ''.join(direction+'A' for direction in 'ULDR'[:players]) + 'AEA'
    tour = prefix + '..S.A.IG.A.G.DA.B.'
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_DATA_DIR=str(work/'player'),
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360',
                  MKSC_AUDIO_CAPTURE=str(work/'items.wav'))
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    if cannonballs and players == 1:
        # MK8's combat aiming/braking line misses the cannonball falls in both
        # rounds. Carry the approach through the line with ordinary acceleration,
        # then return to the combat pilot; Retry repeats the same bounded input.
        common['MKSC_AID_MESH_INPUTS'] = '300:20:0:1'
    _, log = runner.run('battle', ['--menu-tour', pack, work/'save', tour,
        work/'frames', '--mods-dir', work/'mods', '--mod', 'bridge-course',
        '--no-classic-menus', '--short-transitions'], **common)
    check('Menu tour: PASS' in log, 'Battle did not return through ordinary menus')
    check('3D renderer: GPU driver=' in log and '3D renderer: CPU' not in log,
          'Battle did not use the GPU world backend')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, 'Battle HUD/results used CPU UI fallback')
    check(re.search(r'3D world target: width=\d+ height=\d+ views='+str(players)+r' scale=1', log),
          'Battle omitted a local camera viewport')
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'Battle did not use GLES depth')
    verify_driver_art(log, 3, 1)
    starts = re.findall(r'3D Battle start: course=(\d+) players=(\d+) drivers=([\d,]+)', log)
    check(len(starts) == 2 and starts[0] == starts[1], 'Battle Retry changed arena or players')
    course, count, text = starts[0]
    check(course == '65536', 'Battle selected the earlier non-Battle catalog entry')
    drivers = list(map(int, text.split(',')))
    check(int(count) == players and len(drivers) == len(set(drivers)) == 4,
          'Battle lost ordinary distinct driver choices')
    if coins:
        verify_coins(log,{65536},16,4,players,require_loss=True)
    if pier_boos:
        verify_pier_boo_actor_turns(log,{65536})
    if kamek:
        verify_kamek_actor_turns(log,{65536},require_patrol=False)
        for contact_course,tick,mask,touch,guard,spin in kamek_contacts(log):
            for racer in range(4):
                if touch&(1<<racer):
                    item_hit=re.search(rf'3D item event: course={contact_course} tick={tick+180} '
                                       rf'kind=3 item=[0-9a-f]+ racer=\d+ target={racer} ',log)
                    check(item_hit or not re.search(rf'3D Battle balloons: tick={tick+180} racer={racer} ',log),
                          'harmless Kamek contact caused Battle balloon damage')
    if mechakoopas:
        verify_mechakoopa_actor_turns(log,{65536},require_cycle=False)
    if common_props:
        verify_common_prop_actor_turns(log,{65536},require_both_toggles=False)
        for contact_course,tick,_,hit,_,_,_ in common_prop_contacts(log):
            for racer in range(4):
                if hit&(1<<racer):
                    item_hit=re.search(rf'3D item event: course={contact_course} tick={tick+180} '
                                       rf'kind=3 item=[0-9a-f]+ racer=\d+ target={racer} ',log)
                    check(item_hit or not re.search(rf'3D Battle balloons: tick={tick+180} racer={racer} ',log),
                          'harmless common-prop bump caused Battle balloon damage')
    if thunderclouds:
        verify_thundercloud_actor_turns(log,{65536})
    if falling_stars:
        verify_falling_star_actor_turns(log,{65536})
    if teepees:
        verify_teepee_actor_turns(log,{65536})
        # Only source tent touch itself is harmless; unrelated shells still pop.
        for contact in teepee_contacts(log):
            contact_course,tick,_,touch,protected,_,_,_,_=contact
            for racer in range(4):
                if (touch|protected)&(1<<racer):
                    item_hit=re.search(rf'3D item event: course={contact_course} tick={tick} kind=3 '
                                       rf'item=[0-9a-f]+ racer=\d+ target={racer} ',log)
                    check(item_hit or not re.search(rf'3D Battle balloons: tick={tick} racer={racer} ',log),
                          'harmless tent contact changed Battle balloons on its accepted turn')
    verify_item_owner(log, 16, 4)
    verify_item_audio(log, work/'items.wav')
    pickups = re.findall(r'3D item pickup: course=(\d+) tick=(\d+) racer=(\d+) '
                         r'placement=(\d+) fraction=(\d+)', log)
    check(pickups and all(c == course and int(t) > 180 and int(r) < 4
          and int(p) < 4 and int(f) <= 1 << 32 for c,t,r,p,f in pickups),
          'Battle omitted real swept item-box pickups')
    events = re.findall(r'3D item event: course='+course+r' tick=(\d+) kind=(\d+) '
                        r'item=([0-9a-f]+) racer=(\d+) target=(\d+) serial=(\d+)', log)
    check(any(int(kind) == 0 and int(item,16) == 0x1002 for _,kind,item,_,_,_ in events)
          and any(int(kind) == 3 and int(target) < 4 for _,kind,_,_,target,_ in events),
          'Battle did not use and hit with naturally awarded red shells')
    balloons = re.findall(r'3D Battle balloons: tick=(\d+) racer=(\d+) remaining=(\d+) bomb=([01])', log)
    check(balloons and all(int(r) < 4 and int(n) <= 3 for _,r,n,_ in balloons)
          and any(int(n) == 2 for _,_,n,_ in balloons)
          and any(int(n) == 0 for _,_,n,_ in balloons), 'Battle omitted actual balloon pops/elimination')
    if snowmen:
        clean_primary_contacts=0
        for match in log.split('3D Battle start: ')[1:]:
            live_match=match.split('3D Battle finish:',1)[0]
            verify_snowman_actor_turns(live_match,{int(course)},require_contact=False)
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live_match)
            check(go is not None,'snowman Battle omitted its accepted countdown boundary')
            contacts=re.findall(rf'3D source snowman contact: course={course} tick=(\d+) '
                r'contacts=([0-9a-f]{16}) changed=([0-9a-f]{16}) bump=(\d+) '
                r'protected=(\d+) hit=(\d+) spin=(\d+)',live_match)
            for tick,contact,changed,bump,protected,hit,spin in contacts:
                if not (int(bump)&1 and int(contact,16) and int(changed,16)
                        and not int(hit)&1 and int(spin)==0):continue
                accepted_tick=int(tick)+int(go[1])
                # Ignore independent simultaneous projectile damage; isolate
                # the source-controller bump's own balloon semantics.
                if re.search(rf'3D item event: course={course} tick={accepted_tick} '
                    r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live_match):continue
                check(not re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 ',live_match),
                      'snowman bump invented Battle balloon damage')
                clean_primary_contacts+=1
        check(clean_primary_contacts>0,
              'Battle never accepted a primary snowman bump without spin or balloon damage')
    if penguins:
        actual_primary_hits=0
        for match in log.split('3D Battle start: ')[1:]:
            live_match=match.split('3D Battle finish:',1)[0]
            verify_penguin_actor_turns(live_match,{int(course)},require_contact=False)
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live_match)
            check(go is not None,'penguin Battle omitted accepted countdown boundary')
            contacts=re.findall(rf'3D source penguin contact: course={course} tick=(\d+) '
                r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',live_match)
            for tick,contact,defeated,hit,protection in contacts:
                if not (int(contact,16) and int(hit)&1):continue
                accepted_tick=int(tick)+int(go[1])
                if re.search(rf'3D item event: course={course} tick={accepted_tick} '
                    r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live_match):continue
                check(re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 remaining=[0-2] ',live_match),
                      'penguin weak-spin hit did not reach Battle balloon damage')
                actual_primary_hits+=1
        check(actual_primary_hits>0,'Battle omitted a real primary penguin hazard hit/pop')
    if mice:
        actual_primary_hits=0
        for match in log.split('3D Battle start: ')[1:]:
            live_match=match.split('3D Battle finish:',1)[0]
            verify_mouse_actor_turns(live_match,{int(course)},require_contact=False)
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live_match)
            check(go is not None,'mouse Battle omitted accepted countdown boundary')
            contacts=re.findall(rf'3D source mouse contact: course={course} tick=(\d+) '
                r'contacts=([0-9a-f]{16}) defeated=([0-9a-f]{16}) hit=(\d+) protected=(\d+)',live_match)
            for tick,contact,defeated,hit,protection in contacts:
                if not (int(contact,16) and int(hit)&1):continue
                accepted_tick=int(tick)+int(go[1])
                if re.search(rf'3D item event: course={course} tick={accepted_tick} '
                    r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live_match):continue
                check(re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 remaining=[0-2] ',live_match),
                      'mouse weak-spin hit did not reach Battle balloon damage')
                actual_primary_hits+=1
        check(actual_primary_hits>0,'Battle omitted a real primary mouse hazard hit/pop')
    if piranhas:
        captures=[]
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            verify_piranha_actor_turns(live,{int(course)},require_contact=False)
            intervals=verify_piranha_capture(live,{int(course)},required=False)
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'plant Battle omitted accepted countdown boundary')
            touches={int(t)+int(go[1]) for t,mask in re.findall(
                rf'3D source piranha contact: course={course} tick=(\d+) '
                r'contacts=[0-9a-f]{16} changed=[0-9a-f]{16} protected=[0-9a-f]{16} touch=(\d+) bite=\d+',live)
                if int(mask)&1}
            for tick in map(int,re.findall(r'3D Battle balloons: tick=(\d+) racer=0 ',live)):
                if tick not in touches and not any(start+int(go[1])<=tick<=end+int(go[1])
                                                    for _,start,end in intervals):continue
                check(re.search(rf'3D item event: course={course} tick={tick} '
                      r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live),
                      'plant capture/bite invented generic hit or Battle balloon damage')
            captures.extend((start,end) for _,start,end in intervals)
        check(captures,'Battle omitted a genuine primary capture and92-turn launch')
        verify_piranha_actor_turns(log,{int(course)})
    if seagulls:
        verify_seagull_actor_turns(log,{int(course)})
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'seagull Battle omitted accepted countdown boundary')
            for tick,touch in re.findall(rf'3D source seagull contact: course={course} tick=(\d+) '
                r'contacts=[0-9a-f]{16} changed=[0-9a-f]{16} protected=[0-9a-f]{16} '
                r'touch=(\d+) star=\d+ hit=\d+ grace=\d+',live):
                if not int(touch)&1:continue
                accepted_tick=int(tick)+int(go[1])
                if re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 ',live):
                    check(re.search(rf'3D item event: course={course} tick={accepted_tick} '
                        r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live),
                        'harmless seagull contact invented Battle balloon damage')
    if lava_bubbles:
        primary_hits=0
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'lava Battle omitted accepted countdown boundary')
            has_primary=any(int(hit)&1 and int(burn)&1 for hit,burn in re.findall(
                rf'3D source lava contact: course={course} tick=\d+ '
                r'contacts=[0-9a-f]{16} defeated=[0-9a-f]{16} hit=(\d+) protected=\d+ burn=(\d+)',live))
            verify_lava_actor_turns(live,{int(course)},require_contact=has_primary)
            for tick,hit,burn in re.findall(rf'3D source lava contact: course={course} tick=(\d+) '
                r'contacts=[0-9a-f]{16} defeated=[0-9a-f]{16} hit=(\d+) protected=\d+ burn=(\d+)',live):
                if not (int(hit)&1 and int(burn)&1):continue
                accepted_tick=int(tick)+int(go[1])
                check(re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 remaining=[0-2] ',live),
                      'lava weak-spin hit did not reach Battle balloon damage')
                primary_hits+=1
        check(primary_hits,'Battle omitted primary lava hit/burn and balloon damage')
    if thwomps:
        admitted_crushes=0;admitted_bumps=0
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'Thwomp Battle omitted accepted countdown boundary')
            contacts=re.findall(rf'3D source thwomp contact: course={course} tick=(\d+) '
                r'contacts=[0-9a-f]{16} defeated=[0-9a-f]{16} bump=(\d+) crush=(\d+) hit=(\d+) protected=\d+',live)
            primary=any(int(crush)&1 for _,_,crush,_ in contacts)
            verify_thwomp_actor_turns(live,{int(course)},require_contact=primary,require_bump=False)
            for tick,bump,crush,hit in contacts:
                accepted_tick=int(tick)+int(go[1])
                # Any racer's crush proves the distinction; the primary kart
                # need not be the one under the Thwomp when it falls.
                for racer in range(players):
                    if int(crush)>>racer&1:
                        check(int(hit)>>racer&1 and re.search(
                            rf'3D Battle balloons: tick={accepted_tick} racer={racer} remaining=[0-2] ',live),
                            'Thwomp crush did not reach genuine Battle balloon loss')
                        admitted_crushes+=1
                if int(bump)&1:
                    check(not int(hit)&1,'ground rebound invented generic damage')
                    if re.search(rf'3D Battle balloons: tick={accepted_tick} racer=0 ',live):
                        check(re.search(rf'3D item event: course={course} tick={accepted_tick} '
                            r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live),
                            'Thwomp ground rebound invented Battle balloon loss')
                    admitted_bumps+=1
        # Every crush must cost its racer a balloon; K1's MK8 arrival times left
        # the one-player arena without one, so only its rebound is required there.
        check(admitted_bumps and (admitted_crushes or players==1),'Battle omitted rebound/crush distinctions')
    if cannonballs:
        check(re.search(rf'3D source cannonballs start: course={course} cannonballs=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
              'cannonball Battle owner omitted explicit source rules')
        total_hits=0
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            if players == 1:
                check(f'3D input segment: course={course} mode=16 player=0 tick=300 '
                      'count=20 steering=0 buttons=1' in live,
                      'cannonball Battle omitted its ordinary approach input')
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'cannonball Battle omitted accepted countdown boundary')
            hits=[int(t) for t,mask in re.findall(rf'3D source cannonball contact: course={course} '
                r'tick=(\d+) contacts=[0-9a-f]{16} hit=(\d+) protected=\d+',live) if int(mask)&1]
            verify_cannonball_actor_turns(live,{int(course)},require_contact=bool(hits),require_start=False)
            for tick in hits:
                check(re.search(rf'3D Battle balloons: tick={tick+int(go[1])} racer=0 remaining=[0-2] ',live),
                      'cannonball strong hit did not reach genuine Battle balloon loss')
            total_hits+=len(hits)
        check(total_hits,'Battle omitted primary cannonball strong hit')
    if boos:
        check(re.search(rf'3D source boos start: course={course} boos=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
              'Battle Boo owner omitted explicit source rules')
        primary_touches=0
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'Boo Battle omitted accepted countdown boundary')
            touches=[int(t) for t,cue in re.findall(rf'3D source boo contact: course={course} '
                r'tick=(\d+) contacts=[0-9a-f]{16} cue=(\d+) pending=\d+ flash=\d+',live) if int(cue)&1]
            verify_boo_actor_turns(live,{int(course)},require_contact=bool(touches),require_start=False)
            for tick in touches:
                accepted=tick+int(go[1])
                if re.search(rf'3D Battle balloons: tick={accepted} racer=0 ',live):
                    check(re.search(rf'3D item event: course={course} tick={accepted} '
                        r'kind=3 item=[0-9a-f]+ racer=\d+ target=0 ',live),
                        'harmless Boo touch invented Battle balloon damage')
            primary_touches+=len(touches)
        check(primary_touches,'Battle omitted primary harmless Boo touch')
    if rocks:
        check(re.search(rf'3D source rocks start: course={course} rocks=[1-9]\d* profile=[1-9]\d* revision=[1-9]\d*',log),
              'Battle rock owner omitted explicit source rules')
        primary_hits=0
        for match in log.split('3D Battle start: ')[1:]:
            live=match.split('3D Battle finish:',1)[0]
            go=re.search(rf'3D countdown cue: course={course} tick=(\d+) remaining=0 song=115',live)
            check(go is not None,'rock Battle omitted accepted countdown boundary')
            hits=[int(t) for t,mask in re.findall(rf'3D source rock contact: course={course} '
                r'tick=(\d+) contacts=[0-9a-f]{16} hit=(\d+) protected=\d+ burn=\d+',live) if int(mask)&1]
            # A detonated revenge bomb enters recovery on the following turn.
            # The shared race owner clears all source burn at that boundary.
            cleanup=[int(t)-int(go[1])+1 for t in re.findall(
                r'3D Battle detonate: tick=(\d+) racer=0',live)]
            verify_rock_actor_turns(live,{int(course)},require_contact=bool(hits),require_start=False,
                                   cleanup_ticks=cleanup)
            for tick in hits:
                check(re.search(rf'3D Battle balloons: tick={tick+int(go[1])} racer=0 remaining=[0-2] ',live),
                      'rock weak hit did not cause genuine Battle balloon loss')
            primary_hits+=len(hits)
        check(primary_hits,'Battle omitted actual primary rock hit/burn')
    if players == 4:
        check(any(b == '1' for _,_,_,b in balloons)
              and re.search(r'3D Battle detonate: tick=\d+ racer=[0-3]', log),
              '4P Battle did not exercise revenge-bomb contact')
    finishes = re.findall(r'3D Battle finish: course=(\d+) players=(\d+) tick=(\d+) '
                          r'finished=(\d+) views=(\d+)', log)
    check(len(finishes) == 2 and all(c == course and int(n) == int(v) == players
          and int(t) > 180 and int(f) == 1 for c,n,t,f,v in finishes),
          'Battle did not complete both real matches')
    rows = re.findall(r'3D Battle result: course=(\d+) player=(\d+) driver=(\d+) rank=(\d+) human=([01])', log)
    check(len(rows) == 8, 'Battle standings omitted a human or CPU participant')
    for attempt in range(2):
        results = rows[attempt*4:(attempt+1)*4]
        check({int(r[1]) for r in results} == set(range(1,5))
              and {int(r[3]) for r in results} == set(range(1,5))
              and all(c == course and int(d) == drivers[int(p)-1]
                      and int(human) == (int(p) <= players)
                      for c,p,d,_,human in results),
              'Battle standings changed identities or duplicated ranks')
    if players == 2:
        # Both saved 2P matches demonstrate primary-human collection and use;
        # do not let a CPU-only completion satisfy this ordinary-input route.
        matches = log.split('3D Battle start: ')[1:]
        for match in matches:
            live_match = match.split('3D Battle finish:', 1)[0]
            check(re.search(r'3D item pickup: course='+course+r' tick=\d+ racer=0 ', live_match)
                  and re.search(r'3D item event: course='+course+
                                r' tick=\d+ kind=0 item=1002 racer=0 ', live_match),
                  'Battle match completed without primary-human pickup and use')
    pauses = re.findall(r'3D Battle pause: paused=([01]) tick=(\d+)', log)
    check(len(pauses) == 2 and pauses[0][0] == '1' and pauses[1][0] == '0'
          and pauses[0][1] == pauses[1][1], 'Battle pause advanced the shared turn')
    check(f'3D Battle return: course={course} players={players}' in log,
          'Battle did not return to the arena browser')
    check(not list(work.rglob('*.ghost')) and not list(work.rglob('*.pb')),
          'Battle wrote Time Trial persistence')
    live = read_ppm(work/'frames'/f'{len(prefix)+6:03}.ppm')
    result = read_ppm(work/'frames'/f'{len(prefix)+7:03}.ppm')
    check(live[:2] == result[:2] == (640,360) and live[2] != result[2],
          'Battle inventory/balloons capture reused the results image')
    check(hashlib.sha256(pack.read_bytes()).digest() == original, 'Battle modified the base pack')
    # The same ordinary entry with only a race course presents an empty mode,
    # consumes no race actions and permits Back without a mode-admission error.
    empty_tour = prefix[:-1] + '..A.S.D.U.B.'
    _, empty_log = runner.run('empty-battle', ['--menu-tour',pack,work/'empty-save',empty_tour,
        work/'empty-frames','--mods-dir',work/'empty-mods','--mod','bridge-course',
        '--no-classic-menus','--short-transitions'],
        **dict(common,MKSC_DATA_DIR=str(work/'empty-player'),
               MKSC_AUDIO_CAPTURE=str(work/'empty.wav')))
    check('Menu tour: PASS' in empty_log and '3D course selector: count=0' in empty_log,
          'an empty Battle mode did not remain a usable ordinary browser')
    check('3D Battle start:' not in empty_log and '3D course preview:' not in empty_log
          and 'course does not admit selected mode' not in empty_log,
          'empty-mode confirm/navigation launched an incompatible course')
    print(f'3D Battle pier boos route: PASS players={players} harmless haunt contacts results retry' if pier_boos
        else f'3D Battle kamek route: PASS players={players} harmless source contacts results retry' if kamek
        else f'3D Battle mechakoopas route: PASS players={players} source patrol contacts results retry' if mechakoopas
        else f'3D Battle common props route: PASS players={players} harmless bump umbrella results retry' if common_props
          else f'3D Battle thunderclouds route: PASS players={players} warning strong shrink results retry' if thunderclouds
          else f'3D Battle falling stars route: PASS players={players} descent bounce strong hit results retry' if falling_stars
          else f'3D Battle teepees route: PASS players={players} tent rider lock harmless contact retry' if teepees
          else f'3D Battle coins route: PASS players={players} pickups losses HUD results retry' if coins
          else f'3D Battle rocks route: PASS players={players} burn balloon damage bounces results retry' if rocks
          else f'3D Battle boos route: PASS players={players} harmless touch palette cells results retry' if boos
          else f'3D Battle cannonballs route: PASS players={players} strong hit balloons explosion results retry' if cannonballs
          else f'3D Battle thwomps route: PASS players={players} source rebound crush balloons recovery results retry' if thwomps
          else f'3D Battle lava bubbles route: PASS players={players} source hit burn balloon damage results retry' if lava_bubbles
          else f'3D Battle seagulls route: PASS players={players} harmless flutter no balloon damage results retry' if seagulls
          else f'3D Battle piranhas route: PASS players={players} source bite capture no balloon damage results retry' if piranhas
          else f'3D Battle mice route: PASS players={players} real hazard hit balloon damage results retry pause' if mice
          else f'3D Battle penguins route: PASS players={players} real hazard hit balloon damage results retry pause' if penguins
          else f'3D Battle snowmen route: PASS players={players} real bump no spin no balloon damage results retry pause' if snowmen
          else f'3D Battle route: PASS players={players} pickups hits balloons revenge results retry pause')


def verify_quick(binary, pack, work, gpu_probe, video_driver, gles=False, engine_class=0, class_profiles=False, items=False):
    check(gpu_probe is not None and not work.exists(), '3D Quick requires GPU and fresh work')
    environment = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe = subprocess.run([str(gpu_probe)], env=environment, timeout=30)
    if probe.returncode == 77:
        print('3D Quick route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU probe failed')
    root = Path(__file__).resolve().parents[3]
    digest = hashlib.sha256(pack.read_bytes()).digest()
    build = [sys.executable, str(root / 'mods/examples/bridge-course/build.py'),
             '--output-dir', str(work / 'mods')]
    if class_profiles:
        build.append('--class-profiles')
    if items:
        build.append('--items')
    subprocess.run(build, check=True)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    prefix = 'SDDA' + 'D' * engine_class + 'AUAAEA'
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_DATA_DIR=str(work / 'player'),
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    _, log = runner.run('quick', ['--menu-tour', pack, work / 'save',
        prefix + '..S.A.' + ('I' if items else '') + 'G.A.G.DA.B.',
        work / 'frames', '--mods-dir', work / 'mods',
        '--mod', 'bridge-course', '--no-classic-menus', '--short-transitions'],
        **dict(common, MKSC_AID_MESH_TRACE='600:608',
               **({'MKSC_AUDIO_CAPTURE': str(work / 'items.wav')} if items else {})))
    check('Menu tour: PASS' in log and '3D renderer: GPU driver=' in log
          and '3D renderer: CPU' not in log, '3D Quick did not complete its GPU menu route')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, '3D Quick HUD/results fell back to CPU')
    verify_driver_art(log, 7, 1)
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'Quick did not use GLES depth backend')
    starts = re.findall(r'3D Quick start: course=(\d+) players=1 drivers=([\d,]+) '
                        r'profile=(\d+) revision=(\d+)', log)
    check(len(starts) == 2 and starts[0] == starts[1], 'Quick Retry lost configuration')
    course, ids, profile, revision = starts[0]
    # The same process also retries. Scope this oracle to the first genuine
    # race so a repeated tick range on Retry cannot hide a missing preturn.
    race_starts = list(re.finditer(r'3D Quick start: course=', log))
    first_race = log[race_starts[0].start():race_starts[1].start()]
    traces = re.findall(r'3D aid trace: course=(\d+) mode=4 tick=(\d+) player=(\d+) token=[GI] ', first_race)
    check(len(traces) == 9 and all(c == course and player == '0' for c, _, player in traces)
          and [int(tick) for _, tick, _ in traces] == list(range(600, 609)),
          'Quick bounded aid trace omitted, duplicated or misidentified human preturns')
    gates = re.findall(r'3D aid gate: course=(\d+) tick=(\d+) player=(\d+) from=(\d+) to=(\d+) '
                       r'center=-?\d+,-?\d+,-?\d+ normal=-?\d+,-?\d+,-?\d+ '
                       r'right=-?\d+,-?\d+,-?\d+ half=(\d+),(\d+) flags=\d+ stage=\d+ key=\d+', first_race)
    check(9 <= len(gates) <= 9 * 16 and {int(row[1]) for row in gates} == set(range(600, 609))
          and all(c == course and player == '0' and 600 <= int(tick) <= 608
                  and int(width) > 0 and int(height) > 0
                  for c, tick, player, _, _, width, height in gates)
          and '3D aid gates:' not in first_race,
          'Quick bounded trace lost valid expected-gate geometry or exceeded its bound')
    drivers = list(map(int, ids.split(',')))
    check(len(drivers) == len(set(drivers)) == 8 and drivers[0] == 2
          and int(profile) > 0 and int(revision) > 0, 'Quick lost selected driver/imported roster')
    policy = 'class-profile' if class_profiles else 'authored-profile'
    check(f'3D Quick class: selected={engine_class} profile={profile} policy={policy} racers=8' in log,
          'Quick discarded selected class or used an undeclared profile mapping')
    if class_profiles:
        expected_profile = (50, 100, 150)[engine_class]
        expected_cap = (12, 18, 24)[engine_class] * 65536
        check(int(profile) == expected_profile,
              'ordinary class selector did not choose its declared physics profile')
        check(f'3D Quick physics: class={engine_class} base_profile=1 '
              f'selected_profile={expected_profile} revision={revision} cap_q16={expected_cap} '
              'policy=class-profile' in log, 'Quick did not load the authored class coefficients')
    check(f'3D Quick world: course={course} racers=8 views=1' in log,
          'Quick did not submit eight karts through one viewport')
    finishes = re.findall(r'3D Quick finish: course=(\d+) players=1 tick=(\d+) '
                          r'finished=(\d+) views=1 cpus=7', log)
    check(len(finishes) == 2 and all(c == course and int(t) > 180 and 1 <= int(f) <= 8
          for c, t, f in finishes), 'Quick omitted actual human-finish world completion')
    rows = re.findall(r'3D Quick time: racer=(\d+) driver=(\d+) rank=(\d+) ticks=(\d+) '
                      r'estimated=([01]) actual_finish_ticks=(\d+) lap=(\d+) stage=(\d+) '
                      r'cpu_visits=(\d+)', log)
    check(len(rows) == 16, 'Quick omitted eight-racer result rows')
    for attempt in range(2):
        results = rows[attempt * 8:(attempt + 1) * 8]
        attempt_log = log[race_starts[attempt].start():
                          race_starts[attempt + 1].start() if attempt + 1 < len(race_starts) else len(log)]
        check({int(row[0]) for row in results} == set(range(1, 9))
              and {int(row[2]) for row in results} == set(range(1, 9)), 'Quick results lost ranks/racers')
        actual_count = 0
        for index, driver, rank, ticks, estimated, actual, lap, stage, visits in results:
            i = int(index) - 1
            check(int(driver) == drivers[i] and int(ticks) > 0, 'Quick changed a driver/time identity')
            check(int(estimated) == (int(actual) == 0), 'Quick hid an estimated time as a real finish')
            if not int(estimated):
                check(int(ticks) == int(actual), 'Quick rewrote a legitimate finish time')
                actual_count += 1
            if i == 0:
                check(not int(estimated) and int(lap) == 3, 'Quick human did not really finish')
            else:
                # Quick freezes on the human finish. Item damage/recovery can
                # leave a rival below one lap and reset its route visit serial.
                pickup = re.search(rf'3D item pickup: course={course} '
                                   rf'tick=\d+ racer={i} placement=\d+ fraction=\d+', attempt_log)
                check((int(lap) >= 1 or int(visits) > 0 or pickup is not None) if items
                      else int(lap) >= 1 and int(visits) > 0,
                      f'CPU {i} never followed its route or reached an authored box')
        check(actual_count == int(finishes[attempt][2]), 'Quick finish count disagrees with results')
    if items:
        verify_item_owner(log, 4, 8)
        verify_item_audio(log, work / 'items.wav')
        pickups = re.findall(r'3D item pickup: course=(\d+) tick=(\d+) racer=(\d+) '
                             r'placement=(\d+) fraction=(\d+)', log)
        check(pickups and any(int(row[2]) == 0 for row in pickups)
              and all(int(row[0]) == 65536 and int(row[1]) > 180 and 0 <= int(row[2]) < 8
                      and int(row[3]) < 4 and 0 <= int(row[4]) <= 1 << 32 for row in pickups),
              'Quick did not collect authored boxes through real fullXYZ swept pickup events')
        uses = re.findall(r'3D item event: course=65536 tick=\d+ kind=0 item=[0-9a-f]+ '
                          r'racer=(\d+) target=\d+ serial=\d+', log)
        check('0' in uses, 'Quick never accepted a real held-item use from the human control')
        check(re.search(r'3D item aid: settled course=65536 item=100[0-9abc] tick=\d+', log),
              'inventory picture was not admitted from a settled real item')
        # I follows the same route controls until a real box roulette settles;
        # its normal token boundary captures the populated HUD before G uses it.
        inventory_picture = read_ppm(work / 'frames' / f'{len(prefix) + 6:03}.ppm')
        result_picture = read_ppm(work / 'frames' / f'{len(prefix) + 7:03}.ppm')
        check(inventory_picture[:2] == (640, 360) and inventory_picture[2] != result_picture[2],
              'settled inventory/boxes capture is missing or reuses the frozen results image')
    check(not re.search(r'3D Quick result: .* retired=1', log), 'Quick incorrectly used VS retirement')
    pauses = re.findall(r'3D Quick pause: paused=([01]) tick=(\d+)', log)
    check(len(pauses) == 2 and pauses[0][0] == '1' and pauses[1][0] == '0'
          and pauses[0][1] == pauses[1][1], 'Quick pause advanced simulation')
    check(f'3D Quick return: course={course} players=1' in log, 'Quick did not return to course browser')
    check('3D Time Trial start:' not in log and not list(work.rglob('*.ghost'))
          and not list(work.rglob('*.pb')), 'Quick wrote Time Trial saves')
    check(hashlib.sha256(pack.read_bytes()).digest() == digest, 'Quick modified the base pack')
    check(read_ppm(work / 'frames' / f'{len(prefix) + 6 + int(items):03}.ppm')[:2] == (640, 360),
          'Quick result canvas has wrong extent')
    print(f'3D Quick items route: PASS eight-kart boxes swept pickups use PCM retry profile={profile}' if items
          else f'3D Quick route: PASS eight-kart CPU laps estimates pause retry menus class={engine_class} profile={profile}')


def verify_gp_podium_picture(path, drivers):
    width, height, rgb = read_ppm(path)
    check((width, height) == (640, 360), 'GP ceremony has wrong canvas extent')
    # Below the compact three-row table and above the footer, each unlit base
    # has a distinct authored material. These are geometry checks, not a
    # platform-dependent complete-frame baseline or a UI-label color match.
    colors = ((255, 211, 78), (186, 199, 211), (186, 121, 70))
    boxes = []
    for color in colors:
        pixels = [(x, y) for y in range(height * 46 // 100, height * 92 // 100)
                  for x in range(width)
                  if rgb[(y*width+x)*3:(y*width+x+1)*3] == bytes(color)]
        check(len(pixels) >= 24, f'GP ceremony hides podium material {color}')
        boxes.append((min(x for x, _ in pixels), min(y for _, y in pixels),
                      max(x for x, _ in pixels), max(y for _, y in pixels)))
    centers = [(box[0]+box[2])/2 for box in boxes]
    check(centers[1] < centers[0] < centers[2], 'GP podium rank placement is not silver/gold/bronze')
    for driver, (left, top, right, bottom) in zip(drivers, boxes):
        # The authored Luigi binding keeps its own colors; other drivers use
        # complete source front cells with colored clothing and dark tires.
        # The camera looks down on the low bronze step, so a short kart sits
        # in front of its back edge: scan down over the visible top face too.
        margin = max(4, (right-left)//5)
        kart_pixels = [tuple(rgb[(y*width+x)*3:(y*width+x+1)*3])
                       for y in range(max(height*46//100, top-max(24,bottom-top)), top+(bottom-top)//2)
                       for x in range(max(0,left-margin), min(width,right+margin+1))]
        if driver == 2:
            authored = {(38,190,216), (255,206,82), (41,51,66)}
            check(sum(color in authored for color in kart_pixels) >= 8,
                  'GP ceremony replaced the authored Luigi model with fallback art')
        else:
            # Exclude the known clear color and all three podium materials,
            # so an empty pedestal or a flat white generic box cannot pass.
            ignored = set(colors) | {(18,38,56)}
            histogram = {}
            for color in kart_pixels:
                if color not in ignored:
                    histogram[color] = histogram.get(color, 0) + 1
            chromatic = sum(count for color, count in histogram.items()
                            if max(color)-min(color) >= 40)
            dark = sum(count for color, count in histogram.items() if max(color) <= 96)
            check(sum(count >= 2 for count in histogram.values()) >= 3
                  and chromatic >= 8 and dark >= 4,
                  f'GP ceremony hides textured source driver {driver} above its podium')


def verify_protected_actor_gp(runner, pack, work, prefix, arguments, common, original, mice=False, piranhas=False, seagulls=False, lava_bubbles=False, thwomps=False, cannonballs=False, boos=False, rocks=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False):
    # Star-only odds isolate the actor interaction. The ordinary penguin cup
    # independently proves four-race qualification, awards and persistence.
    # A Thwomp can be rising when the kart arrives, and a Mechakoopa can have
    # walked on from its meeting point. Keep driving inputs active through the
    # next descent or the patrol's return, and the complete protected flight.
    waits = 10 if thwomps or mechakoopas else 8
    _, log = runner.run('protected', ['--menu-tour', pack, work/'save',
        prefix+('.......I' if piranhas else '.I')+'T'*waits+'S..S.S.DDA.B.', work/'frames', *arguments],
        **dict(common,MKSC_AUDIO_CAPTURE=str(work/'protected.wav'),
               MKSC_AID_MESH_HAZARDS='seek'))
    check('Menu tour: PASS' in log and '3D renderer: GPU driver=' in log
          and '3D renderer: CPU' not in log and 'GPU UI compositor: driver=' in log
          and 'full_canvas_upload=0' in log and 'GPU UI fallback:' not in log,
          'protected actor interaction lost GPU world/UI or ordinary navigation')
    if pier_boos:
        verify_pier_boo_actor_turns(log,{65536},protected=True)
    elif kamek:
        verify_kamek_actor_turns(log,{65536},protected=True,require_patrol=False)
    elif mechakoopas:
        profiles=list((work/'source').rglob('source-actors.json'))
        check(len(profiles)==1,'protected Mechakoopas require explicit MSA sine')
        sine=json.loads(profiles[0].read_text())['profiles'][0]['sine']
        verify_mechakoopa_actor_turns(log,{65536},protected=True,sine=sine,require_cycle=False)
    elif common_props:
        profiles=list((work/'source').rglob('source-actors.json'))
        check(len(profiles)==1,'protected common props require one explicit authored MSA table')
        sine=json.loads(profiles[0].read_text())['profiles'][0]['sine']
        verify_common_prop_actor_turns(log,{65536},protected=True,sine=sine)
    elif thunderclouds:
        verify_thundercloud_actor_turns(log,{65536},protected=True)
    # K1's MK8 arrival times miss these Star-timed contacts; K3's MK8 Star
    # speed retimes them, so their protected-contact proofs wait for it.
    elif falling_stars:
        verify_falling_star_actor_turns(log,{65536},protected=True,require_contact=False)
    elif teepees:
        verify_teepee_actor_turns(log,{65536},protected=True,require_contact=False)
    elif rocks:
        verify_rock_actor_turns(log,{65536},protected=True,require_contact=False)
    elif boos:
        rules=list((work/'source').rglob('items.json'))
        check(len(rules)==1,'protected Boo needs one authored MIR rules file')
        star_ticks=json.loads(rules[0].read_text())['profiles'][0]['star']['player_ticks']
        verify_boo_actor_turns(log,{65536},protected=True,star_ticks=star_ticks,require_contact=False)
    elif cannonballs:
        verify_cannonball_actor_turns(log,{65536},protected=True,require_contact=False)
    elif thwomps:
        verify_thwomp_actor_turns(log,{65536},protected=True)
    elif lava_bubbles:
        verify_lava_actor_turns(log,{65536},protected=True,require_contact=False)
    elif seagulls:
        verify_seagull_actor_turns(log,{65536},protected=True)
    elif piranhas:
        verify_piranha_actor_turns(log,{65536},protected=True)
        verify_piranha_audio(log,{65536})
    elif mice:
        verify_mouse_actor_turns(log,{65536},protected=True)
    else:
        verify_penguin_actor_turns(log,{65536},protected=True)
        defeat=re.search(r'3D source penguin contact: course=65536 tick=(\d+) '
            r'contacts=[0-9a-f]{16} defeated=0000000000000001 hit=0 protected=1',log)
        check(defeat is not None,'protected penguin omitted primary accepted defeat')
        tick=int(defeat[1])
        states=re.findall(r'3D source penguin state: course=65536 tick=(\d+) index=0 '
            r'timer=(-?\d+) art=\d+ flags=(\d+) height=(-?\d+) flip=1 checksum=([0-9a-f]{16})',log)
        check(any(int(t)==tick+33 and not int(flags)&1 for t,_,flags,_,_ in states)
              and len({checksum for t,_,_,_,checksum in states if tick<=int(t)<=tick+33})>2,
              'protected penguin omitted exact33-turn changed flight and hide')
    retimed = lava_bubbles or cannonballs or boos or rocks or teepees or falling_stars
    check(retimed or re.search(r'3D item event: course=65536 tick=\d+ kind=0 item=100a racer=0 ',log)
          and (boos or re.search(r'3D item cue: course=65536 tick=\d+ song='+('373' if piranhas else '160')+
                        r' player=\d+ volume=256',log)),
          'protected actor omitted ordinary Star activation or its source contact mixer cue')
    verify_item_audio(log,work/'protected.wav')
    pauses=re.findall(r'3D GP pause: paused=([01]) tick=(\d+)',log)
    check(len(pauses)>=3 and pauses[0][0]=='1' and pauses[1][0]=='0'
          and pauses[0][1]==pauses[1][1] and pauses[2][0]=='1'
          and '3D audio return: menu pack restored' in log,
          'protected GP interaction failed paused-clock freeze or ordinary return')
    check('3D GP complete:' not in log and '3D GP award:' not in log
          and not list((work/'save').rglob('*.cup')) and not list((work/'save').rglob('*.ghost'))
          and not list((work/'save').rglob('*.pb')),
          'bounded actor interaction invented cup awards or records')
    pictures=sorted((work/'frames').glob('*.ppm'))
    counts=[]
    for picture in pictures:
        fw,fh,rgb=read_ppm(picture)
        check((fw,fh)==(640,360),'protected actor capture lost window extent')
        counts.append(sum(rgb[i:i+3]==bytes((238,224,255) if pier_boos else (75,148,214) if kamek else (88,196,141) if mechakoopas else (95,88,199) if common_props else (116,132,163) if thunderclouds else (255,204,69) if falling_stars else (230,162,75) if teepees else (93,66,115) if rocks else (240,232,255) if boos else (52,70,91) if cannonballs else (80,124,136) if thwomps else (243,106,36) if lava_bubbles else (168,191,212) if seagulls else (194,142,98) if mice else (36,59,115))
            for y in range(35,fh-25) for x in range(45,fw-20) for i in [(y*fw+x)*3]))
    if pier_boos:
        verify_pier_boo_pictures(pictures,protected=True)
    if kamek:
        verify_kamek_pictures(pictures)
    if mechakoopas:
        verify_mechakoopa_pictures(pictures)
    if common_props:
        verify_common_prop_pictures(pictures,protected=True)
    if thunderclouds:
        verify_thundercloud_pictures(pictures)
    check(pictures and (piranhas or thunderclouds or mechakoopas or kamek or pier_boos or counts and max(counts)>=8),
          'protected route never captured actual actor model')
    check(hashlib.sha256(pack.read_bytes()).digest()==original,'protected route changed owner pack')
    print('3D GP pier boos Star route: PASS primary source ballistic prop flight pause PCM' if pier_boos
        else '3D GP kamek Star route: PASS primary ballistic flight inversion hide pause PCM' if kamek
        else '3D GP mechakoopas Star route: PASS primary source33 flight inversion hide pause PCM' if mechakoopas
        else '3D GP common props Star route: PASS primary source33 flight inversion hide pause PCM' if common_props
          else '3D GP thunderclouds Star route: PASS primary protection unchanged cloud no shrink pause PCM' if thunderclouds
          else '3D GP falling stars Star route: PASS primary protection persistent source lifecycle pause PCM' if falling_stars
          else '3D GP teepees Star route: PASS protected immediate fling no lock pause return PCM' if teepees
          else '3D GP rocks Star route: PASS immunity persistent source lifecycle pause return PCM' if rocks
          else '3D GP boos Star route: PASS ordinary protection deferred palette no defeat pause return PCM' if boos
          else '3D GP cannonballs Star route: PASS protection unchanged actor lifecycle pause return PCM' if cannonballs
          else '3D GP thwomps Star route: PASS primary protection source flight no crush pause return PCM' if thwomps
          else '3D GP lava bubbles Star route: PASS primary protection signed velocity no burn pause return PCM' if lava_bubbles
          else '3D GP seagulls Star route: PASS primary protection signed flight hide pause return PCM' if seagulls
          else '3D GP piranhas Star route: PASS primary immunity hidden ballistic defeat pause return PCM' if piranhas
          else '3D GP mice Star route: PASS human protected defeat source flight hide pause return PCM' if mice
          else '3D GP penguins Star route: PASS human protected defeat sine flight hide pause return PCM')


def verify_pier_boo_repel_gp(runner,pack,work,prefix,arguments,common,original):
    """A real held Star escapes an established source haunt, without awards."""
    _,log=runner.run('repel',['--menu-tour',pack,work/'save',prefix+'.I'+'T'*8+
        'S..S.S.DDA.B.',work/'frames',*arguments],
        **dict(common,MKSC_AUDIO_CAPTURE=str(work/'repel.wav')))
    check('Menu tour: PASS' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log,'source haunt repel lost ordinary GPU navigation')
    touches=[r for r in pier_boo_contacts(log) if r[0]==65536 and r[3]&1]
    check(touches,'source haunt repel omitted genuine unprotected initial touch')
    uses=list(map(int,re.findall(r'3D item event: course=65536 tick=(\d+) kind=0 item=100a racer=0 ',log)))
    check(uses,'source haunt repel never used an ordinarily collected held Star')
    rows=source_haunt_states(log,65536)
    complete=False
    for touch in touches:
        tick=touch[1]
        orbit=next((r for r in rows if r['target']==0 and r['tick']==tick+63
                    and r['phase']==2 and r['lock']==360),None)
        if orbit is None:continue
        repelled=next((r for r in rows if r['slot']==orbit['slot'] and r['phase']==6
                       and r['tick']>orbit['tick']),None)
        if repelled is None:continue
        check(any(orbit['tick']+180<t<=repelled['tick']+180 for t in uses),
              'Star was not ordinarily used from an already established source orbit')
        check(any(r[0]==65536 and r[1]==repelled['tick'] and r[8]&1 for r in pier_boo_contacts(log)),
              'source Star repulsion did not release actual orbit music')
        flight=[r for r in rows if r['slot']==orbit['slot'] and r['tick']>repelled['tick']]
        active=next((r for r in flight if r['occupied'] and r['phase']==7),None)
        if active is None:continue
        check(active['affine_y']==-256 and active['lock']==0,
              'source Star repulsion did not invert art and clear typed lock')
        previous=active
        for row in flight:
            if row['tick']<=active['tick']:continue
            if row['occupied'] and row['tick']==previous['tick']+1:
                check(row['phase']==7 and row['origin_x']==previous['origin_x']
                      and row['origin_y']==previous['origin_y'] and row['origin_z']==previous['origin_z']
                      and row['x']==previous['x']+previous['vx']
                      and row['y']==previous['y']+previous['vy']
                      and row['z']==previous['z']+previous['vz'],
                      'source haunt repulsion did not preserve its frozen SI origin/ballistic motion')
            if not row['occupied']:complete=True;break
            previous=row
    check(complete,'source Star escape omitted full repulsion/free lifecycle')
    check(not re.search(r'3D item cue: course=65536 tick=\d+ song=160 ',log),
          'source haunt repulsion invented a protected prop-contact cue')
    verify_pier_boo_pictures(sorted((work/'frames').glob('*.ppm')))
    verify_item_audio(log,work/'repel.wav')
    check('3D GP complete:' not in log and not list((work/'save').rglob('*.pb')),
          'bounded source haunt repel invented cup awards or records')
    check(hashlib.sha256(pack.read_bytes()).digest()==original,'source haunt repel changed base pack')
    print('3D GP pier boos repel route: PASS ordinary held Star source orbit escape pause PCM')
    return 0


def drop_retried_gp_attempts(log):
    """Remove each failed 3D GP attempt that a Retry replaced, from its race
    start up to the retried start, so cup checks see the four counted races."""
    lines = log.split('\n')
    for retry in [i for i, line in enumerate(lines) if line.startswith('3D GP retry: ')]:
        course = re.search(r' course=(\d+) ', lines[retry]).group(1)
        starts = [i for i in range(retry) if lines[i].startswith('3D GP race: ')
                  and f' course={course} ' in lines[i]]
        check(len(starts) >= 2, 'GP retry without a failed attempt and a retried start')
        for i in range(starts[-2], starts[-1]):
            lines[i] = ''
    return '\n'.join(line for line in lines)

def verify_gp(binary, pack, work, gpu_probe, video_driver, gles=False, items=False, crabs=False, snowmen=False, penguins=False, penguins_star=False, mice=False, mice_star=False, piranhas=False, piranhas_star=False, seagulls=False, seagulls_star=False, lava_bubbles=False, lava_bubbles_star=False, thwomps=False, thwomps_star=False, cannonballs=False, cannonballs_star=False, boos=False, boos_star=False, rocks=False, rocks_star=False, coins=False, teepees=False, teepees_star=False, falling_stars=False, falling_stars_star=False, thunderclouds=False, thunderclouds_star=False, common_props=False, common_props_star=False, mechakoopas=False, mechakoopas_star=False, kamek=False, kamek_star=False, pier_boos=False, pier_boos_star=False, pier_boos_repel=False, ground_follow=False):
    check(gpu_probe is not None and not work.exists(), '3D GP requires GPU and fresh work')
    environment = dict(os.environ, SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    probe = subprocess.run([str(gpu_probe)], env=environment, timeout=30)
    if probe.returncode == 77:
        print('3D GP route skipped: no GPU device; this is not GPU acceptance')
        return 77
    check(probe.returncode == 0, 'GPU probe failed')
    root = Path(__file__).resolve().parents[3]
    original = hashlib.sha256(pack.read_bytes()).digest()
    build = [sys.executable, str(root/'mods/examples/bridge-course/build.py'),
             '--output-dir', str(work/'mods'), '--cup']
    if ground_follow:
        build.append('--ground-follow')
    if items:
        build.append('--items')
    if crabs:
        build.append('--crabs')
    if snowmen:
        build.append('--snowmen')
    if penguins:
        build.append('--penguins')
    if penguins_star:
        build.append('--penguins-star')
    if mice:
        build.append('--mice')
    if piranhas:
        build.append('--piranhas')
    if seagulls:
        build.append('--seagulls')
    if lava_bubbles:
        build.append('--lava-bubbles')
    if thwomps:
        build.append('--thwomps')
    if cannonballs:
        build.append('--cannonballs')
    if boos:
        build.append('--boos')
    if rocks:
        build.append('--rocks')
    if coins:
        build.append('--coins')
    if teepees:
        build.append('--teepees')
    if teepees_star:
        build.append('--teepees-star')
    if falling_stars:
        build.append('--falling-stars')
    if falling_stars_star:
        build.append('--falling-stars-star')
    if thunderclouds:
        build.append('--thunderclouds')
    if thunderclouds_star:
        build.append('--thunderclouds-star')
    if common_props:
        build.append('--common-props')
    if common_props_star:
        build.append('--common-props-star')
    if mechakoopas:
        build.append('--mechakoopas')
    if mechakoopas_star:
        build.append('--mechakoopas-star')
    if kamek:
        build.append('--kamek')
    if kamek_star:
        build.append('--kamek-star')
    if pier_boos:
        build.append('--pier-boos')
    if pier_boos_star:
        build.append('--pier-boos-star')
    if pier_boos_repel:
        build.append('--pier-boos-repel')
    if mice_star:
        build.append('--mice-star')
    if piranhas_star:
        build.append('--piranhas-star')
    if seagulls_star:
        build.append('--seagulls-star')
    if lava_bubbles_star:
        build.append('--lava-bubbles-star')
    if thwomps_star:
        build.append('--thwomps-star')
    if cannonballs_star:
        build.append('--cannonballs-star')
    if boos_star:
        build.append('--boos-star')
    if rocks_star:
        build.append('--rocks-star')
    subprocess.run(build, check=True)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    prefix = 'SAAUAAEA'
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_DATA_DIR=str(work/'player'),
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    arguments = ['--mods-dir', work/'mods', '--mod', 'bridge-course',
                 '--no-classic-menus', '--short-transitions']
    if pier_boos_repel:
        return verify_pier_boo_repel_gp(runner,pack,work,prefix,arguments,common,original)
    if penguins_star or mice_star or piranhas_star or seagulls_star or lava_bubbles_star or thwomps_star or cannonballs_star or boos_star or rocks_star or teepees_star or falling_stars_star or thunderclouds_star or common_props_star or mechakoopas_star or kamek_star or pier_boos_star:
        return verify_protected_actor_gp(runner,pack,work,prefix,arguments,common,original,mice=mice_star,piranhas=piranhas_star,seagulls=seagulls_star,lava_bubbles=lava_bubbles_star,thwomps=thwomps_star,cannonballs=cannonballs_star,boos=boos_star,rocks=rocks_star,coins=coins,teepees=teepees_star,falling_stars=falling_stars_star,thunderclouds=thunderclouds_star,common_props=common_props_star,mechakoopas=mechakoopas_star,kamek=kamek_star,pier_boos=pier_boos_star)
    # Alternate ordinary item taps and conservative driving on this authored
    # cup. Keep every real qualification and item-use assertion below.
    # Cloud races reserve three inventories; the third uses ordinary fresh
    # periodic taps so a triple mushroom is not spent on consecutive frames.
    # Common props keep two conservative races, then tap: the second starts
    # on the front row through the umbrella, and a retried (slower) attempt
    # of it would miss that toggle.
    # Walking toys use fresh taps in the second race and reserve the other
    # inventories, avoiding voluntary boosts through later hazard contacts.
    # Reserve inventories on the slope contact fixture rather than voluntarily
    # boosting through a crab; the full qualification/cup oracle stays strict.
    # A snowman cup leader keeps a defensive item all race, so it taps instead.
    race_tokens = ('JJJJ' if ground_follow else 'JKJJ' if mechakoopas or kamek or pier_boos else 'JJKK' if common_props else 'JJKJ' if thunderclouds else 'JGGG' if coins else 'JGJG' if rocks else 'JGGG' if boos else 'JGJG' if cannonballs else 'JGGG' if thwomps else 'JGGG' if lava_bubbles else 'JGGG' if seagulls else 'JGGG' if piranhas else 'JGGG' if mice and not (crabs or snowmen or penguins)
                   else 'JGKG' if penguins and not (crabs or snowmen)
        else 'KGKJ' if crabs else 'KKKK' if snowmen else 'KGKG' if items else 'GGGG')
    _, log = runner.run('cup', ['--menu-tour', pack, work/'save',
        prefix + '.' + ('I' if items else '') + ''.join(token+'.A.A.' for token in race_tokens)
        + '...A.B.', work/'frames', *arguments],
        **dict(common,MKSC_AUDIO_CAPTURE=str(work/'cup.wav'),MKSC_AID_GP_RETRY_TOKENS='1'))
    # Each aid retry rewinds the tour to its race token and replays from there.
    rewound = sum(int(replaces)-int(token) for token, replaces in
                  re.findall(r'3D GP aid retry: course=\d+ token=(\d+) replaces=(\d+) ', log))
    attempts = log
    log = drop_retried_gp_attempts(log)
    check('Menu tour: PASS' in log and '3D renderer: GPU driver=' in log
          and '3D renderer: CPU' not in log, '3D GP did not complete its GPU menu route')
    check('GPU UI compositor: driver=' in log and 'full_canvas_upload=0' in log
          and 'GPU UI fallback:' not in log, '3D GP HUD/boards fell back to CPU')
    check('3D GP complete: cup=65536 ' in log,
          'authored GP route did not qualify through all four races')
    for course in range(65536,65540):
        cues = re.findall(rf'3D countdown cue: course={course} tick=\d+ remaining=(\d+) song=(\d+)',log)
        check(cues == [('120','114'),('60','114'),('0','115')],
              'actual GP countdown did not emit two beeps and GO exactly once')
    check('3D audio return: menu pack restored' in log, 'GP did not restore shared menu audio')
    with wave.open(str(work/'cup.wav'),'rb') as capture:
        check(capture.getnchannels() == 2 and capture.getsampwidth() == 2
              and capture.getnframes() > 1000, 'GP actual audio capture is incomplete')
        samples = capture.readframes(capture.getnframes())
        check(any(samples), 'GP race mixed only silence')
    if items:
        courses = set(range(65536, 65540))
        if coins:
            verify_coins(log,courses,1,8,require_loss=True,placements=12)
        verify_item_owner(log, 1, 8, courses)
        verify_item_audio(log, work/'cup.wav', courses=courses)
        if crabs:
            verify_crab_actor_turns(log,courses)
            if ground_follow: verify_actor_ground(log,courses)
        if snowmen:
            verify_snowman_actor_turns(log,courses)
        if penguins:
            verify_penguin_actor_turns(log,courses)
        if mice:
            verify_mouse_actor_turns(log,courses)
        if seagulls:
            verify_seagull_actor_turns(log,courses)
        if lava_bubbles:
            verify_lava_actor_turns(log,courses)
        if thwomps:
            verify_thwomp_actor_turns(log,courses)
        # The TT routes prove the primary ball and star hits; MK8 arrival
        # times leave the cup's racing line under neither.
        if cannonballs:
            verify_cannonball_actor_turns(log,courses,require_contact=False)
        if boos:
            verify_boo_actor_turns(log,courses)
        if pier_boos:
            verify_pier_boo_actor_turns(log,courses)
        if kamek:
            verify_kamek_actor_turns(log,courses)
        if mechakoopas:
            verify_mechakoopa_actor_turns(log,courses)
        if common_props:
            # The TT route proves both umbrella toggles; the cup's racing line needs one.
            verify_common_prop_actor_turns(log,courses,require_both_toggles=False)
        if thunderclouds:
            verify_thundercloud_actor_turns(log,courses)
        if falling_stars:
            verify_falling_star_actor_turns(log,courses,require_contact=False)
        if teepees:
            verify_teepee_actor_turns(log,courses)
        if rocks:
            verify_rock_actor_turns(log,courses)
        if piranhas:
            verify_piranha_actor_turns(log,courses)
            verify_piranha_capture(log,courses)
        pickups = re.findall(r'3D item pickup: course=(\d+) tick=(\d+) racer=(\d+) '
                             r'placement=(\d+) fraction=(\d+)', log)
        check(pickups and {int(row[0]) for row in pickups if int(row[2]) == 0} == courses
              and all(int(row[0]) in courses and int(row[1]) > 180 and 0 <= int(row[2]) < 8
                      and int(row[3]) < 4 and 0 <= int(row[4]) <= 1 << 32 for row in pickups),
              'item cup omitted real human fullXYZ pickups or produced invalid pickup metadata')
        uses = re.findall(r'3D item event: course=(\d+) tick=\d+ kind=0 item=([0-9a-f]+) '
                          r'racer=(\d+) target=\d+ serial=\d+', log)
        # The strategy holds boost items for a clear straight; a rival's Boo may
        # steal one first, leaving that race without a human use.
        stolen={int(course) for course in re.findall(
            r'3D item event: course=(\d+) tick=\d+ kind=6 item=[0-9a-f]+ racer=\d+ target=0 ', log)}
        human_use_courses={course for course,token in zip(range(65536,65540),race_tokens) if token!='J'}
        used={int(course) for course, _, racer in uses if int(racer) == 0}
        human_use_courses-={course for course in stolen if course not in used}
        check(uses and {int(course) for course, _, racer in uses if int(racer) == 0} == human_use_courses
              and all(int(course) in courses and 0 <= int(racer) < 8
                      and 0x1000 <= int(item, 16) <= 0x100f and int(item, 16) != 0x100d
                      for course, item, racer in uses),
              'item cup human uses did not match its ordinary per-race item strategy')
        if common_props or thunderclouds or mechakoopas or kamek or pier_boos:
            for index,token in enumerate(race_tokens):
                if token!='K':continue
                taps=list(map(int,re.findall(rf'3D item event: course={65536+index} tick=(\d+) '
                                            r'kind=0 item=[0-9a-f]+ racer=0 ',log)))
                check(taps and all(t%120==1 for t in taps)
                      and all(after-before>=120 for before,after in zip(taps,taps[1:])),
                      'actor GP did not preserve genuine fresh periodic inventory taps')
        # The settled capture precedes the race tokens, so a Retry of the
        # first race replays without it: read it from the first attempt.
        settled = re.search(r'3D item aid: settled course=65536 item=([0-9a-f]+) tick=(\d+)', attempts)
        check(settled is not None and 0x1000 <= int(settled[1], 16) <= 0x100c
              and int(settled[1], 16) != 0x1006 and int(settled[2]) > 180,
              'item GP first race never captured its naturally settled inventory')
        check(read_ppm(work/'frames'/f'{len(prefix) + 1:03}.ppm')[:2] == (640, 360),
              'item GP settled inventory/boxes canvas has the wrong extent')
    verify_driver_art(log, 7, 1)
    if gles:
        check('3D renderer: GPU driver=opengles2' in log, 'GP did not use GLES depth backend')
    starts = re.findall(r'3D GP race: cup=(\d+) index=(\d+) course=(\d+) class=(\d+) '
                        r'profile=(\d+) credits=(\d+) grid=([\d,]+) drivers=([\d,]+)', log)
    check([int(row[2]) for row in starts] == list(range(65536,65540))
          and [int(row[1]) for row in starts] == list(range(4))
          and all(int(row[0]) == 65536 and int(row[3]) == 0 and int(row[4]) > 0 for row in starts),
          'GP did not start four distinct authored courses in cup order')
    roster = list(map(int, starts[0][7].split(',')))
    check(len(roster) == len(set(roster)) == 8 and roster[0] == 2
          and all(row[7] == starts[0][7] for row in starts), 'GP changed its eight-driver roster')
    grids = re.findall(r'3D GP grid: course=(\d+) slots=([\d,]+)', log)
    check(len(grids) == 4 and list(map(int,grids[0][1].split(','))) == list(range(7,-1,-1)),
          'GP did not put the player last on the source first grid')
    rows = re.findall(r'3D GP time: racer=(\d+) driver=(\d+) rank=(\d+) ticks=(\d+) '
                      r'estimated=([01]) actual_finish_ticks=(\d+) lap=(\d+) stage=(\d+) '
                      r'cpu_visits=(\d+)', log)
    check(len(rows) == 32, 'GP omitted genuine eight-racer result rows')
    awards = re.findall(r'3D GP award: course=(\d+) racer=(\d+) before=(\d+) '
                        r'awarded=(\d+) after=(\d+)', log)
    check(len(awards) == 32, 'GP did not award each successful race exactly once')
    totals = [0]*8
    for race_index in range(4):
        results = rows[race_index*8:(race_index+1)*8]
        check({int(row[0]) for row in results} == set(range(1,9))
              and {int(row[2]) for row in results} == set(range(1,9)), 'GP lost a racer/rank')
        for row in results:
            racer,driver,rank,ticks,estimated,actual,lap,_,visits = map(int,row)
            check(driver == roster[racer-1] and ticks > 0 and estimated == (actual == 0),
                  'GP misrepresented driver identity or an estimated finish')
            if racer == 1:
                check(rank <= 4 and actual == ticks and lap == 3, 'human did not qualify naturally')
            else:
                # GP freezes on the human finish. Damage/recovery may leave a
                # rival below one lap with a reset route cursor; an actual box
                # pickup away from the grid still proves its accepted travel.
                pickup = re.search(rf'3D item pickup: course={65536 + race_index} '
                                   rf'tick=\d+ racer={racer - 1} placement=\d+ fraction=\d+', log)
                check((lap >= 1 or visits > 0 or pickup is not None) if items
                      else visits > 0 and lap >= 1,
                      'GP rival never followed its real CPU route or reached an authored box')
        if race_index < 3:
            expected_grid = [0]*8
            for row in results:
                expected_grid[int(row[0])-1] = int(row[2])-1
            check(list(map(int,grids[race_index+1][1].split(','))) == expected_grid,
                  'next GP grid did not follow the previous finish order')
        for course,racer,before,award,after in awards[race_index*8:(race_index+1)*8]:
            i = int(racer)-1
            finish_rank = next(int(row[2]) for row in results if int(row[0]) == i+1)
            check(int(course) == 65536+race_index and int(before) == totals[i]
                  and int(award) == (9,6,3,1,0,0,0,0)[finish_rank-1]
                  and int(after) == int(before)+int(award), 'GP cumulative points changed incorrectly')
            totals[i] = int(after)
    complete = re.search(r'3D GP complete: cup=65536 rank=(\d+) points=(\d+) podium=([\d,]+)',log)
    check(complete is not None and int(complete[2]) == totals[0]
          and int(complete[1]) == 1+sum(points > totals[0] for points in totals),
          'GP omitted its genuine cup standing')
    podium = list(map(int,complete[3].split(',')))
    check(len(podium) == len(set(podium)) == 3 and set(podium) <= set(roster)
          and [totals[roster.index(driver)] for driver in podium] == sorted(totals,reverse=True)[:3],
          'ceremony podium did not match cumulative race points')
    ceremony_frame = len(prefix) + 1 + int(items) + len('G.A.A.' * 4) + 1 + rewound
    verify_gp_podium_picture(work/'frames'/f'{ceremony_frame:03}.ppm', podium)
    saves = sorted(work.rglob('*.cup'))
    check(len(saves) == 1 and saves[0].stat().st_size == 128, 'GP did not publish one separate cup save')
    saved = saves[0].read_bytes()
    check(saved[:8] == b'MKSC3CUP' and struct.unpack_from('<4I',saved,32) == tuple(range(65536,65540))
          and struct.unpack_from('<I',saved,80)[0] == totals[0]
          and struct.unpack_from('<I',saved,28)[0]+1 == int(complete[1]),
          'GP save lost full cup history or its actual standing')
    human_results = [next(row for row in rows[i*8:(i+1)*8] if int(row[0]) == 1) for i in range(4)]
    check(struct.unpack_from('<4I',saved,84) == tuple(int(row[5]) for row in human_results)
          and struct.unpack_from('<4I',saved,100) == tuple(int(row[2])-1 for row in human_results),
          'GP save did not retain actual successful finish clocks and ranks')
    check(not list(work.rglob('*.pb')) and not list(work.rglob('*.ghost')), 'GP wrote TT records')
    _, relaunched = runner.run('relaunch', ['--menu-tour', pack, work/'save',
        prefix+'..S.DA..S.DDA.B.', work/'relaunch-frames', *arguments], **common)
    check('3D GP load: cup=65536 status=ok' in relaunched,
          'new application owner did not load completed 3D cup progress')
    expected_best = f'3D GP preview best: cup=65536 rank={complete[1]} points={totals[0]}'
    check(expected_best in relaunched,
          'saved cup progress was not exposed in the ordinary 3D course browser')
    check(saves[0].read_bytes() == saved, 'unfinished relaunched GP overwrote completed progress')
    check('3D GP retry: cup=65536 course=65536 index=1 credits=2' in relaunched,
          'pause Retry did not spend exactly one cup credit on the same course')
    check(relaunched.count('3D GP grid: course=65536 slots=7,6,5,4,3,2,1,0') == 2,
          'pause Retry changed the pre-race grid')
    invalid = bytearray(saved)
    invalid[116] = 1
    saves[0].write_bytes(invalid)
    _, rejected = runner.run('invalid-save', ['--menu-tour', pack, work/'save',
        prefix+'...', work/'invalid-frames', *arguments], **common)
    check('3D GP race:' not in rejected and 'mode=GP' in rejected
          and 'invalid' in rejected and saves[0].read_bytes() == invalid,
          'invalid cup progress published a race or changed the rejected save')
    saves[0].write_bytes(saved)
    # A faster authored CPU profile exercises real failed qualification. Retry
    # and pause Retry spend all three credits before the second natural finish.
    authored=work/'source'/('bridge-cup'+('-crabs' if crabs else '')+('-snowmen' if snowmen else '')
        +('-penguins' if penguins else '')+('-mice' if mice else '')+('-piranhas' if piranhas else '')+('-seagulls' if seagulls else '')+('-lava-bubbles' if lava_bubbles else '')+('-thwomps' if thwomps else '')+('-cannonballs' if cannonballs else '')+('-boos' if boos else '')+('-rocks' if rocks else '')+('-coins' if coins else '')+('-teepees' if teepees else '')+('-star' if teepees_star else '')+('-falling-stars' if falling_stars else '')+('-star' if falling_stars_star else '')+('-thunderclouds' if thunderclouds else '')+('-star' if thunderclouds_star else '')+('-common-props' if common_props else '')+('-star' if common_props_star else '')+('-mechakoopas' if mechakoopas else '')+('-star' if mechakoopas_star else '')+('-kamek' if kamek else '')+('-star' if kamek_star else '')+('-pier-boos' if pier_boos else '')+('-star' if pier_boos_star else '')+('-star' if penguins_star or mice_star or piranhas_star or seagulls_star or lava_bubbles_star or thwomps_star or cannonballs_star or boos_star or rocks_star else ''))
    isolated_failure = (mice or piranhas or seagulls or lava_bubbles or thwomps or cannonballs or boos or rocks or coins or teepees or falling_stars or thunderclouds or common_props or mechakoopas or kamek or pier_boos) and not (crabs or snowmen or penguins)
    failure_mods = work/'mods'
    failure_arguments = arguments
    if isolated_failure:
        # Isolate cup failure semantics from CPUs delayed by source hazards.
        failure_mods = work/'negative/mods'
        subprocess.run([sys.executable,str(root/'mods/examples/bridge-course/build.py'),
            '--output-dir',str(failure_mods),'--cup','--items'],check=True)
        authored = work/'negative/source/bridge-cup'
        failure_arguments = [failure_mods if value == work/'mods' else value for value in arguments]
    for path in (authored/'courses3d').glob('*/course.json'):
        document = json.loads(path.read_text())
        document['routes'][0]['properties'][0]['value'] = 3
        # The human keeps the legacy route pilot here, not the rivals' controller.
        document['routes'][0]['properties'][1]['value'] = False
        path.write_text(json.dumps(document,indent=2)+'\n')
    # Those rivals also keep their full class speed instead of the CPU share.
    physics = json.loads((authored/'physics.json').read_text())
    physics['cpu_speed_ratio_q16'] = [65536]*3
    (authored/'physics.json').write_text(json.dumps(physics,indent=2)+'\n')
    subprocess.run([sys.executable,str(root/'tools/portable/mod_kit.py'),'build',
                    str(authored),'--output',str(failure_mods)],check=True)
    # The aid's crest-safe line can still beat those faster rivals, and hazards slow
    # them. Give them an ordinary neutral-input head start in the failure case;
    # qualification and retry rules stay strict.
    failure_inputs = '........G.A..S.DA..S.DA........G.A.B.'
    _, failed = runner.run('failed', ['--menu-tour', pack, work/'failed-save',
        prefix+failure_inputs, work/'failed-frames', *failure_arguments], **common)
    qualifications = re.findall(r'3D GP finish: cup=65536 index=0 course=65536 rank=(\d+) '
                                r'stage=(\d+) ticks=(\d+) credits=(\d+)',failed)
    check(len(qualifications) == 2 and all(int(row[0]) >= 4 and int(row[2]) > 180
          for row in qualifications) and qualifications[0][1] == '2'
          and qualifications[1][1:] == ('4',qualifications[1][2],'0'),
          'failed qualification did not lead from Retry to Game over')
    check(re.findall(r'3D GP retry: cup=65536 course=65536 index=1 credits=(\d+)',failed)
          == ['2','1','0'], 'ordinary GP retries did not spend exactly three credits')
    failed_awards = re.findall(r'3D GP award: course=65536 racer=\d+ before=(\d+) '
                              r'awarded=(\d+) after=(\d+)',failed)
    check(len(failed_awards) == 16 and all(row == ('0','0','0') for row in failed_awards)
          and '3D GP complete:' not in failed and not list((work/'failed-save').rglob('*.cup')),
          'failed GP attempts awarded points or published completed cup progress')
    check(hashlib.sha256(pack.read_bytes()).digest() == original, 'GP changed the owner pack')
    print('3D GP pier boos route: PASS source haunts four qualifications save reload' if pier_boos
        else '3D GP kamek route: PASS harmless source contacts four qualifications save reload' if kamek
        else '3D GP mechakoopas route: PASS source patrol contacts four qualifications save reload' if mechakoopas
        else '3D GP common props route: PASS harmless bump umbrella four qualifications save reload' if common_props
          else '3D GP thunderclouds route: PASS source warning phases strong shrink four qualifications save reload' if thunderclouds
          else '3D GP falling stars route: PASS source lifecycle strong contact four qualifications save reload' if falling_stars
          else '3D GP teepees route: PASS tent rider lock four qualifications awards save reload' if teepees
          else '3D GP coins route: PASS pickups losses cap four qualifications awards save reload' if coins
          else '3D GP rocks route: PASS source fall burn four qualifications awards save reload' if rocks
          else '3D GP boos route: PASS harmless touch palette four qualifications awards save reload' if boos
          else '3D GP cannonballs route: PASS falling strong hit four qualifications awards save reload' if cannonballs
          else '3D GP thwomps route: PASS source crush recovery four qualifications awards save reload' if thwomps
          else '3D GP lava bubbles route: PASS source64-cycle burn four qualifications awards save reload' if lava_bubbles
          else '3D GP seagulls route: PASS harmless flutter four qualifications awards save reload' if seagulls
          else '3D GP piranhas route: PASS source bite capture four qualifications awards save reload' if piranhas
          else '3D GP mice route: PASS source walk hop hits four qualifications points ceremony save reload' if mice
          else '3D GP penguins route: PASS source contacts four qualifications points ceremony save reload' if penguins
          else '3D GP snowmen route: PASS source contacts bump four qualifications points ceremony save reload' if snowmen
          else '3D GP crabs route: PASS source motion contact four qualifications points grids ceremony save reload' if crabs
          else '3D GP items route: PASS four real races pickups uses PCM points grids ceremony save reload' if items
          else '3D GP route: PASS four real races points grids ceremony separate save reload')


def load_tests(loader, tests, pattern):
    import doctest
    tests.addTests(doctest.DocTestSuite())
    return tests


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--gpu-probe', type=Path)
    parser.add_argument('--video-driver', default='offscreen')
    parser.add_argument('--interpolated', action='store_true')
    parser.add_argument('--gles', action='store_true')
    parser.add_argument('--world-scale', type=int, choices=range(1, 9), default=1)
    parser.add_argument('--views', type=int, choices=(1, 4), default=1)
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--items', action='store_true', help='exercise authored item rules in TT, GP, Quick or local VS')
    parser.add_argument('--boo-held-use', action='store_true', help='ordinary GP/VS retained-Star use during MIR Boo slowdown')
    parser.add_argument('--kart-handling',action='store_true',help='grounding plus ordinary drift/boost/coast controls on an authored pad')
    parser.add_argument('--kart-grounding',action='store_true',help='standard bridge model/source seating and lower-tunnel capture')
    parser.add_argument('--ground-follow',action='store_true',help='exercise a source crab on connected ramp floors')
    parser.add_argument('--crabs', action='store_true', help='exercise authored crab controllers in item-enabled TT or GP')
    parser.add_argument('--snowmen', action='store_true', help='exercise authored snowman controllers in item-enabled TT, GP or Battle')
    parser.add_argument('--penguins', action='store_true', help='exercise authored penguin controllers in item-enabled TT, GP or Battle')
    parser.add_argument('--penguins-star', action='store_true', help='bounded GP Star-only interaction, protection, flight and pause/return; ordinary cup verifies completion')
    parser.add_argument('--mice', action='store_true', help='authored walking/hopping mice in item-enabled TT, GP or Battle')
    parser.add_argument('--mice-star', action='store_true', help='bounded protected mouse GP interaction with ordinary Star roulette')
    parser.add_argument('--piranhas',action='store_true',help='authored source bite and material capture in item-enabled TT, GP or Battle')
    parser.add_argument('--piranhas-star',action='store_true',help='bounded protected plant GP interaction using ordinary Star roulette')
    parser.add_argument('--seagulls',action='store_true',help='authored harmless seagull flutter in item-enabled TT, GP or Battle')
    parser.add_argument('--seagulls-star',action='store_true',help='bounded protected seagull GP interaction using ordinary Star roulette')
    parser.add_argument('--lava-bubbles',action='store_true',help='authored source lava bubbles in item-enabled TT, GP or Battle')
    parser.add_argument('--lava-bubbles-star',action='store_true',help='bounded protected lava bubble GP interaction using ordinary Star roulette')
    parser.add_argument('--pier-boos-repel',action='store_true')
    parser.add_argument('--pier-boos',action='store_true')
    parser.add_argument('--pier-boos-star',action='store_true')
    parser.add_argument('--kamek',action='store_true')
    parser.add_argument('--kamek-star',action='store_true')
    parser.add_argument('--mechakoopas',action='store_true')
    parser.add_argument('--mechakoopas-star',action='store_true')
    parser.add_argument('--common-props',action='store_true',help='authored interactive source props with items and coins')
    parser.add_argument('--common-props-star',action='store_true',help='bounded protected common-prop GP flight')
    parser.add_argument('--thunderclouds',action='store_true',help='authored thundercloud source controllers with items and coins')
    parser.add_argument('--thunderclouds-star',action='store_true',help='bounded protected thundercloud GP interaction')
    parser.add_argument('--falling-stars',action='store_true',help='authored falling-star controllers with items and coins')
    parser.add_argument('--falling-stars-star',action='store_true',help='bounded protected falling-star GP interaction')
    parser.add_argument('--teepees',action='store_true',help='authored tent/rider source controllers with items and coins')
    parser.add_argument('--teepees-star',action='store_true',help='bounded protected teepee GP interaction')
    parser.add_argument('--coins',action='store_true',help='authored shared coin pickups/caps in item-enabled TT, GP, VS or Battle')
    parser.add_argument('--rocks',action='store_true',help='authored Lakeside rock controllers in item-enabled TT, GP or Battle')
    parser.add_argument('--rocks-star',action='store_true',help='bounded protected rock GP interaction')
    parser.add_argument('--boos',action='store_true',help='authored harmless bobbing Boos in item-enabled TT, GP or Battle')
    parser.add_argument('--boos-star',action='store_true',help='bounded protected Boo GP deferred palette request')
    parser.add_argument('--cannonballs',action='store_true',help='authored falling cannonballs in item-enabled TT, GP or Battle')
    parser.add_argument('--cannonballs-star',action='store_true',help='bounded protected cannonball GP interaction')
    parser.add_argument('--thwomps',action='store_true',help='authored Thwomp rebound/crush in item-enabled TT, GP or Battle')
    parser.add_argument('--thwomps-star',action='store_true',help='bounded protected Thwomp GP interaction using ordinary Star roulette')
    parser.add_argument('--gp', action='store_true')
    parser.add_argument('--battle', action='store_true')
    parser.add_argument('--converted-setup', action='store_true')
    parser.add_argument('--converted-handpass',type=Path,help='fingerprinted Blender hand pass for catalog setup')
    parser.add_argument('--converted-course',type=int,action='append',default=[],
                        help='select a source course for reproducible converted setup; repeat for a subset')
    parser.add_argument('--converted-authored-setup', action='store_true')
    parser.add_argument('--converted-fixture', type=Path)
    parser.add_argument('--converted-install', action='store_true')
    parser.add_argument('--example-install',choices=EXAMPLE_INSTALL_CASES,
                        help='ordinary ZIP install, saved selection and real example race')
    gp_inputs=parser.add_mutually_exclusive_group()
    gp_inputs.add_argument('--converted-hold-second', action='store_true',
                        help='keep second-race pickups in reserve in the converted GP route')
    gp_inputs.add_argument('--converted-gp-inputs', type=converted_gp_strategy,
                           help='four ordinary race choices: G guards item use, J reserves pickups, K taps periodic GP items')
    parser.add_argument('--converted-gp-routes', type=converted_gp_routes,
                        help='four comma-separated ordinary aid routes: auto or CPU RTHD index 0..16383')
    parser.add_argument('--converted-gp-driver', type=converted_gp_driver,
                        help='ordinary converted GP stock driver ID 1..8 (default: 2)')
    parser.add_argument('--converted-mode', choices=('tt','gp','battle'), default='tt')
    parser.add_argument('--converted-gp-stage',choices=('all','race','reload'),default='all',
                        help='GP full route, four-race cup, or fresh reload of that completed cup')
    parser.add_argument('--converted-index', type=int, default=0)
    parser.add_argument('--converted-inputs','--converted-tt-inputs',dest='converted_tt_inputs',
                        help='bounded ordinary mesh-human control segments, START:COUNT:STEER:BUTTONS')
    parser.add_argument('--converted-trace',help='bounded accepted-turn diagnostic trace START:END')
    parser.add_argument('--converted-input-race',type=int,choices=range(4),
                        help='scope ordinary input segments to one zero-based GP cup race')
    parser.add_argument('--converted-elevation',action='store_true',help='require a grounded ascent/descent in a hand-pass TT')
    parser.add_argument('--converted-sky-phase',action='store_true',help='capture a live progress sky after genuine lap triggers')
    parser.add_argument('--converted-tt-waits',type=int,choices=range(33),default=0,
                        help='ordinary 90-turn neutral waits before the converted TT pilot starts')
    parser.add_argument('--converted-contact',choices=('crabs', 'snowmen', 'penguins', 'mice', 'piranhas', 'seagulls', 'lava_bubbles', 'thwomps', 'cannonballs', 'boos', 'pier_boos', 'kamek', 'mechakoopas', 'common_props', 'thunderclouds', 'falling_stars', 'teepees', 'rocks'),
                        action='append',default=[],help='require genuine primary contact with this retained source family')
    parser.add_argument('--engine-class', type=int, choices=(0, 1, 2), default=0)
    parser.add_argument('--class-profiles', action='store_true')
    parser.add_argument('--model-overlays',choices=MODEL_OVERLAY_CASES,help='real GPU model overlay composition and fallback case')
    parser.add_argument('--sky-motion',action='store_true',help='authored GPU sky-only TT lifecycle regression')
    parser.add_argument('--sky-progress-vs',action='store_true',help='two-human GPU VS independent progress sky regression')
    parser.add_argument('--local-players', type=int, choices=(1, 2, 3, 4), default=1)
    args = parser.parse_args()
    args.kart_grounding = args.kart_grounding or args.kart_handling
    check(not args.kart_grounding or (args.gpu_probe is not None and not args.items
          and not args.sky_motion and args.model_overlays is None and not args.gp
          and not args.quick and not args.battle and args.local_players==1
          and not args.converted_fixture and not args.converted_setup
          and not args.converted_authored_setup and not args.converted_install
          and not args.example_install and not args.class_profiles and args.engine_class==0
          and not args.interpolated and not args.benchmark and args.views==1 and args.world_scale==1),
          '--kart-grounding requires an isolated standard GPU bridge TT')
    check(not args.sky_progress_vs or (args.gpu_probe is not None and args.local_players==2
          and not args.items and not args.coins and not args.sky_motion and not args.gp
          and not args.quick and not args.battle and args.converted_fixture is None
          and not args.converted_setup and not args.converted_authored_setup
          and not args.converted_install and not args.class_profiles and args.engine_class==0
          and not args.interpolated and not args.benchmark and args.views==1 and args.world_scale==1),
          '--sky-progress-vs requires an isolated two-human GPU VS route')
    check(args.model_overlays is None or (args.gpu_probe is not None and not args.items
          and not args.sky_motion and not args.gp and not args.quick and not args.battle
          and args.local_players==1 and args.converted_fixture is None and not args.converted_setup
          and not args.converted_authored_setup and not args.converted_install
          and not args.class_profiles and args.engine_class==0 and not args.interpolated
          and not args.benchmark and args.views==1 and args.world_scale==1),
          '--model-overlays requires an isolated standard GPU TT route')
    check(not args.sky_motion or (args.gpu_probe is not None and not args.items and not args.gp
          and not args.quick and not args.battle and args.local_players==1
          and args.converted_fixture is None and not args.converted_setup
          and not args.converted_authored_setup and not args.converted_install
          and not args.class_profiles and args.engine_class==0 and not args.interpolated
          and not args.benchmark and args.views==1 and args.world_scale==1),
          '--sky-motion requires a standard GPU sky-only TT route')
    check(args.converted_gp_stage=='all' or (args.converted_fixture is not None
          and args.converted_mode=='gp' and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup),
          'converted GP stages require a converted GP race/reload')
    check(not args.converted_sky_phase or (args.converted_fixture is not None and args.converted_mode=='tt'),
          '--converted-sky-phase requires a converted TT fixture')
    check(not args.converted_tt_waits or
          (args.converted_fixture is not None and args.converted_mode=='tt'
           and not args.converted_install and not args.converted_setup and not args.converted_authored_setup),
          'converted timing/contact choices require a converted TT race')
    check(not args.converted_contact or (args.converted_fixture is not None
          and args.converted_mode in ('tt','gp') and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup),
          'source contact requirements need a converted TT or GP race')
    check(args.converted_trace is None or (args.converted_fixture is not None
          and not args.converted_install and not args.converted_setup and not args.converted_authored_setup),
          'bounded trace requires a converted race route')
    check(args.converted_tt_inputs is None or (args.converted_fixture is not None
          and args.converted_mode in ('tt','gp') and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup),
          'ordinary input segments require a converted TT or GP race')
    check(args.converted_input_race is None or ((args.converted_tt_inputs is not None or args.converted_contact)
          and args.converted_mode=='gp'), 'input race scope requires ordinary segments or contacts in a GP cup')
    check(not args.converted_course or (args.converted_setup and not args.converted_authored_setup),
          '--converted-course requires catalog --converted-setup')
    check(not args.ground_follow or (args.crabs and args.items and args.gpu_probe is not None
          and not args.battle and not args.quick and args.local_players==1),
          'ground-follow requires the authored GPU crab TT or GP route')
    check(not args.crabs or (args.items and not args.quick and not args.battle
          and args.local_players==1 and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'crabs require --items in an authored TT or GP route')
    check(not args.snowmen or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'snowmen require --items in an authored TT, GP or Battle route')
    check(not args.penguins or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'penguins require --items in an authored TT, GP or Battle route')
    check(not args.mice or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'mice require --items in an authored TT, GP or Battle route')
    check(not args.pier_boos_repel or (args.pier_boos and args.items and args.coins and args.gp
          and not args.battle and not any((args.pier_boos_star,args.kamek_star,args.mechakoopas_star,
              args.common_props_star,args.thunderclouds_star,args.falling_stars_star,args.teepees_star,
              args.rocks_star,args.boos_star,args.cannonballs_star,args.thwomps_star,args.lava_bubbles_star,
              args.seagulls_star,args.piranhas_star,args.mice_star,args.penguins_star))),
          'pier-boos-repel requires a normal authored Pier Boo GP route')
    check(not args.pier_boos or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'pier-boos require items/coins in an authored TT, GP or Battle route')
    check(not args.pier_boos_star or (args.pier_boos and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star,args.falling_stars_star,args.thunderclouds_star,args.common_props_star,args.mechakoopas_star,args.kamek_star))),
          'pier-boos-star requires pier_boos/items/coins in GP')
    check(not args.kamek or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'kamek require items/coins in an authored TT, GP or Battle route')
    check(not args.kamek_star or (args.kamek and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star,args.falling_stars_star,args.thunderclouds_star,args.common_props_star,args.mechakoopas_star))),
          'kamek-star requires kamek/items/coins in GP')
    check(not args.mechakoopas or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'mechakoopas require items/coins in an authored TT, GP or Battle route')
    check(not args.mechakoopas_star or (args.mechakoopas and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star,args.falling_stars_star,args.thunderclouds_star,args.common_props_star))),
          'mechakoopas-star requires mechakoopas/items/coins in GP')
    check(not args.common_props or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'common-props require items/coins in an authored TT, GP or Battle route')
    check(not args.common_props_star or (args.common_props and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star,args.falling_stars_star,args.thunderclouds_star))),
          'common-props-star requires common-props/items/coins in GP')
    check(not args.thunderclouds or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'thunderclouds require items/coins in an authored TT, GP or Battle route')
    check(not args.thunderclouds_star or (args.thunderclouds and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star,args.falling_stars_star))),
          'thunderclouds-star requires thunderclouds/items/coins in GP')
    check(not args.falling_stars or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'falling-stars require items/coins in an authored TT, GP or Battle route')
    check(not args.falling_stars_star or (args.falling_stars and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,
                       args.penguins_star,args.teepees_star))),
          'falling-stars-star requires falling-stars/items/coins in GP')
    check(not args.teepees or (args.items and args.coins and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture
          and not args.converted_setup and not args.converted_authored_setup and not args.converted_install
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'teepees require items/coins in an authored TT, GP or Battle route')
    check(not args.teepees_star or (args.teepees and args.gp and not args.battle
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,args.penguins_star))),
          'teepees-star requires teepees/items/coins in GP')
    check(not args.coins or (args.items and not args.quick and not args.converted_fixture
          and not args.converted_install and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0
          and not any((args.rocks_star,args.boos_star,args.thwomps_star,args.cannonballs_star,
                       args.lava_bubbles_star,args.seagulls_star,args.piranhas_star,args.mice_star,args.penguins_star))),
          'coins require items in an authored TT, GP, VS or Battle route')
    check(not args.rocks or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'rocks require --items in an authored TT, GP or Battle route')
    check(not args.rocks_star or (args.rocks and args.items and args.gp and not args.battle
          and not any((args.thwomps_star,args.lava_bubbles_star,args.seagulls_star,
                       args.piranhas_star,args.mice_star,args.penguins_star,args.cannonballs_star,args.boos_star))),
          'rocks-star requires rocks/items and GP')
    check(not args.rocks_star or not any((args.crabs,args.snowmen,args.penguins,args.mice,
          args.piranhas,args.seagulls,args.lava_bubbles,args.thwomps,args.cannonballs,args.boos)),
          'rocks-star requires an isolated rock fixture')
    check(not args.boos or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'boos require --items in an authored TT, GP or Battle route')
    check(not args.boos_star or (args.boos and args.items and args.gp and not args.battle
          and not any((args.thwomps_star,args.lava_bubbles_star,args.seagulls_star,
                       args.piranhas_star,args.mice_star,args.penguins_star,args.cannonballs_star))),
          'boos-star requires boos/items and GP')
    check(not args.boos_star or not any((args.crabs,args.snowmen,args.penguins,args.mice,
          args.piranhas,args.seagulls,args.lava_bubbles,args.thwomps,args.cannonballs)),
          'boos-star requires an isolated Boo fixture')
    check(not args.cannonballs or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'cannonballs require --items in an authored TT, GP or Battle route')
    check(not args.cannonballs_star or (args.cannonballs and args.items and args.gp and not args.battle
          and not any((args.thwomps_star,args.lava_bubbles_star,args.seagulls_star,
                       args.piranhas_star,args.mice_star,args.penguins_star))),
          'cannonballs-star requires cannonballs/items and GP')
    check(not args.cannonballs_star or not any((args.crabs,args.snowmen,args.penguins,args.mice,
          args.piranhas,args.seagulls,args.lava_bubbles,args.thwomps)),
          'cannonballs-star requires an isolated cannonball fixture')
    check(not args.thwomps or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'thwomps require --items in an authored TT, GP or Battle route')
    check(not args.thwomps_star or (args.thwomps and args.items and args.gp and not args.battle
          and not args.lava_bubbles_star and not args.seagulls_star and not args.piranhas_star
          and not args.mice_star and not args.penguins_star), 'thwomps-star requires thwomps/items and GP')
    check(not args.lava_bubbles or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'lava-bubbles require --items in an authored TT, GP or Battle route')
    check(not args.lava_bubbles_star or (args.lava_bubbles and args.items and args.gp and not args.battle
          and not args.seagulls_star and not args.piranhas_star and not args.mice_star and not args.penguins_star),
          'lava-bubbles-star requires lava-bubbles/items and GP')
    check(not args.seagulls or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'seagulls require --items in an authored TT, GP or Battle route')
    check(not args.seagulls_star or (args.seagulls and args.items and args.gp and not args.battle
          and not args.piranhas_star and not args.mice_star and not args.penguins_star),
          'seagulls-star requires seagulls/items and GP')
    check(not args.piranhas or (args.items and not args.quick
          and (args.battle or args.local_players==1) and not args.converted_fixture and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup
          and not args.benchmark and not args.class_profiles and args.engine_class==0),
          'piranhas require --items in an authored TT, GP or Battle route')
    check(not args.piranhas_star or (args.piranhas and args.items and args.gp and not args.battle
          and not args.mice_star and not args.penguins_star), 'piranhas-star requires piranhas/items and GP')
    check(not args.mice_star or (args.mice and args.items and args.gp and not args.battle
          and not args.penguins_star), 'mice-star requires mice/items and GP')
    check(not args.penguins_star or (args.penguins and args.items and args.gp and not args.battle),
          'penguins-star requires penguins/items and GP; TT has no random boxes')
    check(not args.converted_install or (args.converted_fixture is not None
          and not args.converted_setup and not args.converted_authored_setup),
          'converted installation requires its generated catalog fixture')
    check(not (args.converted_hold_second or args.converted_gp_inputs is not None) or (
          args.converted_fixture is not None
          and args.converted_mode == 'gp' and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup),
          'converted GP input choices require a converted GP route')
    check(args.converted_gp_routes is None or (args.converted_fixture is not None
          and args.converted_mode=='gp' and not args.converted_install
          and not args.converted_setup and not args.converted_authored_setup),
          'converted GP route hints require a converted GP race route')
    if args.converted_gp_driver is not None:
        try:
            converted_gp_driver(args.converted_gp_driver,enabled=(
                args.converted_fixture is not None and args.converted_mode=='gp'
                and not args.converted_install and not args.converted_setup
                and not args.converted_authored_setup))
        except argparse.ArgumentTypeError as error:
            parser.error(str(error))
    check(not args.converted_elevation or (args.converted_fixture is not None and args.converted_mode=='tt'),
          '--converted-elevation requires a converted TT route')
    check(args.converted_handpass is None or args.converted_setup,
          '--converted-handpass requires catalog setup')
    if args.example_install:
        check(not any((args.converted_fixture,args.converted_setup,args.converted_authored_setup,
              args.converted_install,args.items,args.gp,args.battle,args.quick,args.model_overlays,
              args.sky_motion,args.benchmark,args.interpolated,args.class_profiles))
              and args.local_players==1 and args.views==1 and args.world_scale==1
              and args.engine_class==0,'installed example uses its own ordinary TT configuration')
        result=verify_example_install(args.binary.resolve(),args.pack.resolve(),args.work.resolve(),
            args.example_install,args.gpu_probe,args.video_driver,args.gles)
    elif args.converted_setup or args.converted_authored_setup:
        check(not (args.converted_setup and args.converted_authored_setup),
              'choose catalog or authored-folder conversion setup')
        setup_converted_fixture(args.pack.resolve(),args.work.resolve(),authored=args.converted_authored_setup,
                                course_ids=args.converted_course,
                                handpass=args.converted_handpass.resolve() if args.converted_handpass else None)
        result = 0
    elif args.converted_fixture is not None:
        check(not args.battle and not args.gp and not args.quick and not args.interpolated
              and not args.benchmark and not args.class_profiles and args.engine_class == 0
              and args.world_scale == 1 and args.views == 1 and not args.items
              and args.local_players == 1,
              'converted routes use their own catalog-derived mode and view setup')
        if args.converted_install:
            check(args.converted_mode=='tt' and args.converted_index==0,
                  'converted installation uses its first ordinary TT course')
            result = verify_converted_install(args.binary.resolve(),args.pack.resolve(),
                args.work.resolve(),args.converted_fixture.resolve(),args.gpu_probe,
                args.video_driver,args.gles)
        else:
            result = verify_converted(args.binary.resolve(),args.pack.resolve(),args.work.resolve(),
                args.converted_fixture.resolve(),args.converted_mode,args.converted_index,
                args.gpu_probe,args.video_driver,args.gles,hold_second=args.converted_hold_second,
                gp_inputs=args.converted_gp_inputs,
                gp_driver=args.converted_gp_driver if args.converted_gp_driver is not None else 2,
                gp_routes=args.converted_gp_routes,tt_waits=args.converted_tt_waits,
                required_contacts=args.converted_contact,tt_inputs=args.converted_tt_inputs,
                input_race=args.converted_input_race,trace=args.converted_trace,gp_stage=args.converted_gp_stage,sky_phase=args.converted_sky_phase,elevation=args.converted_elevation)
    elif args.boo_held_use:
        check(args.items and not args.battle and not args.quick
              and ((args.gp and args.local_players == 1) or
                   (not args.gp and args.local_players == 2))
              and args.views == 1 and args.world_scale == 1
              and not args.benchmark and not args.interpolated,
              'Boo held-item use requires ordinary item-enabled GP or two-human VS')
        result = verify_boo_held_use(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
            args.gpu_probe, args.video_driver, args.gp, args.gles)
    elif args.battle:
        check(args.local_players in (1,2,3,4) and not args.gp and not args.quick
              and not args.class_profiles and args.engine_class == 0 and not args.interpolated
              and not args.benchmark and args.views == 1 and args.world_scale == 1,
              'Battle requires its ordinary one-to-four-human arena views')
        result = verify_battle(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                               args.gpu_probe, args.video_driver, args.local_players, args.gles, args.snowmen, args.penguins, args.penguins_star, args.mice, args.mice_star, args.piranhas, args.piranhas_star, args.seagulls, args.seagulls_star, args.lava_bubbles, args.lava_bubbles_star, args.thwomps, args.thwomps_star, args.cannonballs, args.cannonballs_star, args.boos, args.boos_star, args.rocks, args.rocks_star, args.coins, args.teepees, args.teepees_star, args.falling_stars, args.falling_stars_star, args.thunderclouds, args.thunderclouds_star, args.common_props, args.common_props_star, args.mechakoopas, args.mechakoopas_star, args.kamek, args.kamek_star, args.pier_boos, args.pier_boos_star)
    elif args.gp:
        check(not args.quick and args.local_players == 1 and not args.class_profiles
              and args.engine_class == 0 and not args.interpolated and not args.benchmark
              and args.views == 1 and args.world_scale == 1, 'GP uses its native single-human camera')
        result = verify_gp(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                           args.gpu_probe, args.video_driver, args.gles, args.items, args.crabs, args.snowmen, args.penguins, args.penguins_star, args.mice, args.mice_star, args.piranhas, args.piranhas_star, args.seagulls, args.seagulls_star, args.lava_bubbles, args.lava_bubbles_star, args.thwomps, args.thwomps_star, args.cannonballs, args.cannonballs_star, args.boos, args.boos_star, args.rocks, args.rocks_star, args.coins, args.teepees, args.teepees_star, args.falling_stars, args.falling_stars_star, args.thunderclouds, args.thunderclouds_star, args.common_props, args.common_props_star, args.mechakoopas, args.mechakoopas_star, args.kamek, args.kamek_star, args.pier_boos, args.pier_boos_star, args.pier_boos_repel, args.ground_follow)
    elif args.quick:
        check(args.local_players == 1 and not args.interpolated and not args.benchmark
              and args.views == 1 and args.world_scale == 1, 'Quick uses its native single-human camera')
        result = verify_quick(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                              args.gpu_probe, args.video_driver, args.gles,
                              args.engine_class, args.class_profiles, args.items)
    elif args.local_players > 1:
        check(not args.class_profiles and args.engine_class == 0, 'class options require Quick')
        check(not args.benchmark and not args.interpolated and args.views == 1 and args.world_scale == 1,
              'local VS uses real player views, not the single-racer camera stress options')
        result = verify_vs(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                           args.gpu_probe, args.video_driver, args.local_players, args.gles, args.items, args.coins, args.sky_progress_vs)
    else:
        check(not args.class_profiles and args.engine_class == 0, 'class options require Quick')
        result = verify(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                        args.gpu_probe, args.video_driver, args.interpolated, args.gles,
                        args.world_scale, args.views, args.benchmark, args.items, args.crabs, args.snowmen, args.penguins, args.mice, args.piranhas, args.seagulls, args.lava_bubbles, args.thwomps, args.cannonballs, args.boos, args.rocks, args.coins, args.teepees, args.teepees_star, args.falling_stars, args.falling_stars_star, args.thunderclouds, args.thunderclouds_star, args.common_props, args.common_props_star, args.mechakoopas, args.mechakoopas_star, args.kamek, args.kamek_star, args.pier_boos, args.pier_boos_star, args.sky_motion, args.model_overlays, args.ground_follow, args.kart_grounding, args.kart_handling)
    sys.exit(result or 0)

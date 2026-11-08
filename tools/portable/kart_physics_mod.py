"""Validate and normalize absolute overrides of typed motion-v6/v7/v8 records."""
import struct

ARRAYS = {
    'auto_acceleration': (4132, 64, -32768, 32767),
    'brake': (4260, 8, -32768, 32767),
    'brake_accelerate': (4276, 8, -32768, 32767),
    'forward_response': (4292, 8, -32768, 32767),
    'local_acceleration': (4308, 256, -32768, 32767),
    'steering_delta': (4820, 10, -32768, 32767),
    'steering_response': (4840, 16, -32768, 32767),
    'forward_target_response': (38092, 8, -32768, 0),
}
CPU_ARRAYS = {'cpu_acceleration':(0,128), 'cpu_target_speeds':(128,160), 'cpu_heading_steps':(288,40)}


def integer(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'kart_physics.{field}: expected integer {low}..{high}')
    return value


def fields(value, allowed, field):
    if type(value) is not dict or set(value)-set(allowed):
        raise ValueError(f'kart_physics.{field}: unknown field or expected object')


def array(value, count, low, high, field):
    if type(value) is not list or len(value) != count:
        raise ValueError(f'kart_physics.{field}: expected {count} integers')
    for entry in value:
        integer(entry,low,high,field)


def profile(record, engine):
    allowed = {'base_speed_q16', 'terrain_limits'} | ({'id', *CPU_ARRAYS} if engine else set())
    fields(record,allowed,'engines' if engine else 'time_trial')
    if engine:
        integer(record.get('id'),0,2,'engines.id')
    if not set(record)-{'id'}:
        raise ValueError('kart_physics.profile: at least one override required')
    if 'base_speed_q16' in record:
        integer(record['base_speed_q16'],1,2016*65536,'base_speed_q16')
    if 'terrain_limits' in record:
        array(record['terrain_limits'],6,0,32767,'terrain_limits')
    for key, (_,count) in CPU_ARRAYS.items():
        if key in record:
            array(record[key],count,0,255,key)


def validate(records):
    if type(records) is not list or len(records)>16:
        raise ValueError('kart_physics: expected at most16 records')
    seen=set()
    for record in records:
        fields(record,{'id','time_trial','engines',*ARRAYS},'record')
        identity=integer(record.get('id'),1,0xffffffff,'id')
        if identity in seen:
            raise ValueError('kart_physics.id: duplicate motion ID')
        seen.add(identity)
        if not set(record)-{'id'}:
            raise ValueError('kart_physics.record: at least one override required')
        for key,(_,count,low,high) in ARRAYS.items():
            if key in record:
                array(record[key],count,low,high,key)
        if 'time_trial' in record:
            profile(record['time_trial'],False)
        if 'engines' in record:
            engines=record['engines']
            if type(engines) is not list or not 1<=len(engines)<=3:
                raise ValueError('kart_physics.engines: expected1..3 engine records')
            ids=set()
            for engine in engines:
                profile(engine,True)
                if engine['id'] in ids:
                    raise ValueError('kart_physics.engines.id: duplicate engine ID')
                ids.add(engine['id'])
    return records


def apply_profile(data, record, start, engine):
    if 'base_speed_q16' in record:
        struct.pack_into('<I',data,start+(416 if engine else 0),record['base_speed_q16'])
    if 'terrain_limits' in record:
        struct.pack_into('<6h',data,start+(440 if engine else 24),*record['terrain_limits'])
    for key,(offset,count) in CPU_ARRAYS.items():
        if key in record:
            data[start+offset:start+offset+count]=bytes(record[key])


def apply(data, record):
    # The caller validates motion v6/v7/v8 and applies relative percentages first.
    for key,(offset,count,_,_) in ARRAYS.items():
        if key in record:
            struct.pack_into(f'<{count}h',data,offset,*record[key])
    if 'time_trial' in record:
        apply_profile(data,record['time_trial'],38056,False)
    for engine in record.get('engines',[]):
        apply_profile(data,engine,38108+engine['id']*452,True)

"""Saved local video settings exercised through ordinary menus and races.

Each independently bounded case compares against the same-player-count stock
race and verifies its recorded local inputs by replaying the shared simulation.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import subprocess
import sys
import time


# mode, players, per-pane aspect width, render FPS, sampling scale, renderer
CASES = {
    'vs2-wide120': ('vs', 2, 373, 120, 1, 'original'),
    'vs3-auto144': ('vs', 3, 0, 144, 1, '3d'),
    'vs4-wide60': ('vs', 4, 284, 60, 2, 'original'),
    'battle2-auto0': ('battle', 2, 0, 0, 1, '3d'),
    'battle3-wide144': ('battle', 3, 373, 144, 2, 'original'),
    'battle4-auto120': ('battle', 4, 0, 120, 1, '3d'),
    'vs2-4x3d': ('vs', 2, 373, 120, 4, '3d'),
    'battle3-4x3d': ('battle', 3, 373, 144, 4, '3d'),
}

# Classic first-open frames retain exact reviewed baselines. Modern Options
# gains categories independently; its label positions/ink are checked below,
# and the Games category has its own real-executable navigation/capture tests.
OPTIONS_FIRST_OPEN = {
    ('darwin', 'arm64'): {
        'classic': '6553840ca7eb075d232cdbfbbff92607ccd6fba652748d9d9aeb8015e5f4a764',
    },
    ('win32', 'x86_64'): {
        'classic': '6553840ca7eb075d232cdbfbbff92607ccd6fba652748d9d9aeb8015e5f4a764',
    },
    ('linux', 'x86_64'): {
        'classic': '6553840ca7eb075d232cdbfbbff92607ccd6fba652748d9d9aeb8015e5f4a764',
    },
}
OPTIONS_AIR_FIRST_OPEN = {
    ('darwin', 'arm64'): {
        'classic': '8c3e0f3ce163e18dc6788eedf6ee0e5b6ca4a42501ec268049b0a9127a5942c7',
    },
    ('win32', 'x86_64'): {
        'classic': '8c3e0f3ce163e18dc6788eedf6ee0e5b6ca4a42501ec268049b0a9127a5942c7',
    },
    ('linux', 'x86_64'): {
        'classic': '8c3e0f3ce163e18dc6788eedf6ee0e5b6ca4a42501ec268049b0a9127a5942c7',
    },
}

def check(condition, message):
    if not condition:
        raise AssertionError(message)


def read_ppm(path):
    data = path.read_bytes()
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    check(header is not None, f'{path}: invalid capture header')
    width, height = map(int, header.groups())
    check(len(data) - header.end() == width * height * 3, f'{path}: truncated capture')
    return width, height, data[header.end():]


class LocalVideoRoute:
    def __init__(self, binary, pack, work, case):
        self.binary, self.pack, self.work = binary, pack, work
        self.mode, self.players, self.aspect, self.fps, self.scale, self.renderer = CASES[case]
        self.deadline = time.monotonic() + 175

    def run(self, name, arguments, **extra):
        directory = self.work / name
        (directory / 'screens').mkdir(parents=True)
        env = {key: value for key, value in os.environ.items() if not key.startswith('MKSC_')}
        env.update(SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                   SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                   SDL_WINDOW_ACTIVATE_WHEN_RAISED='0',
                   MKSC_DATA_DIR=str(directory / 'data'), MKSC_GP_PROGRESS=str(directory / 'gp.bin'),
                   MKSC_SCREEN_DIR=str(directory / 'screens'))
        env.update(extra)
        remaining = self.deadline - time.monotonic()
        check(remaining > 0, 'local video case exceeded its 175-second budget')
        log_path = directory / f'{name}.log'
        try:
            # Keep partial evidence readable while a long route is running;
            # a timeout or interrupted wrapper must not discard its output.
            with log_path.open('w', encoding='utf-8') as capture:
                result = subprocess.run([str(self.binary), *map(str, arguments)], env=env,
                                        stdout=capture, stderr=subprocess.STDOUT,
                                        timeout=min(remaining, 180))
        except subprocess.TimeoutExpired as error:
            output = log_path.read_text(encoding='utf-8', errors='replace')
            raise AssertionError(f'{name}: exceeded its time budget\n{output[-6000:]}') from error
        output = log_path.read_text(encoding='utf-8', errors='replace')
        check(result.returncode == 0, f'{name}: exit {result.returncode}\n{output[-6000:]}')
        return directory, output

    def race_arguments(self):
        command = '--vs-smoke' if self.mode == 'vs' else '--battle-smoke'
        return [command, self.pack, self.players, 24 if self.mode == 'vs' else 0,
                '--no-mods', '--classic-menus']

    def configure(self):
        # Enter title Options, toggle Fast Menus and Interpolation, choose FPS,
        # then Internal Scale, Fit scaling and Aspect Ratio. Every edit is saved
        # by the same input handlers as a physical keyboard or controller.
        fps_steps = (0, 60, 120, 144).index(self.fps)
        aspect_steps = (240, 284, 373, 0).index(self.aspect)
        tour = ('DDDA' + 'D' * 6 + 'R' + 'DR' + 'D' + 'R' * fps_steps +
                'D' + 'R' * (self.scale - 1) + 'DR' + 'D' + 'R' * aspect_steps)
        directory = self.work / 'configured'
        _, log = self.run('menu', ['--menu-tour', self.pack, self.work / 'menu-save', tour,
                                  self.work / 'menu-frames', '--classic-menus'],
                          MKSC_MENU_TOUR_PERSIST_SETTINGS='1',
                          MKSC_DATA_DIR=str(directory / 'data'))
        check('Menu tour: PASS' in log, 'ordinary Options tour did not finish')
        settings_path = directory / 'data/settings.json'
        saved = settings_path.read_bytes()
        settings = json.loads(saved)
        for key, expected in dict(aspect_width=self.aspect, render_fps=self.fps,
                                  interpolate=True, internal_scale=self.scale,
                                  fit_scaling=True, short_transitions=True).items():
            check(settings[key] == expected, f'Options failed to save {key}={expected}')
        return settings_path, saved

    @staticmethod
    def simulation(log):
        result = re.search(r'^(?:VS|Battle) local result:.*$', log, re.MULTILINE)
        replay = re.search(r'^Local replay:.* PASS$', log, re.MULTILINE)
        check(result is not None and replay is not None, 'race omitted results or local input replay')
        return result.group(), replay.group()

    def verify(self):
        _, base = self.run('baseline', self.race_arguments() + ['--short-transitions'],
                           MKSC_AID_VERIFY_LOCAL_REPLAY='1')
        known_digest = {('vs', 2): '5b0784531d501f38',
                        ('battle', 3): 'de2289dd5ff70ca4'}.get((self.mode, self.players))
        if known_digest:
            check('digest=' + known_digest in self.simulation(base)[0],
                  'stock race changed its previously accepted simulation digest')
        settings_path, saved = self.configure()
        directory, log = self.run('configured', self.race_arguments() +
                                  ['--race-renderer', self.renderer],
                                  MKSC_AID_PERSIST_SETTINGS='1', MKSC_AID_VERIFY_LOCAL_REPLAY='1',
                                  MKSC_AID_LOCAL_RESIZE='1', MKSC_AID_LOCAL_PRESENTATION='1')
        check(self.simulation(base) == self.simulation(log),
              'local video settings changed inputs, simulation digest, finish or replay')
        check(settings_path.read_bytes() == saved, 'race changed persisted video settings')
        check(re.search(r'Local video:.*players=' + str(self.players) + r'\b.*aspect=' +
                        str(self.aspect) + r'\b.*fps=' + str(self.fps) + r'\b.*interpolate=1', log),
              'saved local video settings did not reach the real host')
        pacing = re.search(r'Local presentation: PASS players=(\d+) fps=(\d+) presents=(\d+) '
                           r'intermediate=(\d+) interpolated=(\d+)', log)
        check(pacing is not None, 'between-tick immutable presentation proof missing')
        players, fps, presents, intermediate, interpolated = map(int, pacing.groups())
        check((players, fps) == (self.players, self.fps) and intermediate > 0 and interpolated > 0,
              'fractional presentation did not use the saved rate or move an immutable endpoint')
        # The aid observes 32 world ticks in sixteenth-tick clock steps. This
        # checks the actual deadline gate, rather than trusting a printed FPS.
        if self.fps:
            expected = 32 * 4389 * self.fps / 262144
            check(abs(presents - expected) <= 3,
                  f'actual presentation count {presents} did not honor {self.fps} FPS')
        else:
            check(presents >= 400, 'unlocked rendering still followed a frame cap')
        check('Local resize: PASS' in log, 'live resize/paused restoration proof missing')
        check('3D unavailable' not in log, 'requested 3D world fell back')
        stages = re.findall(r'Local resize view: stage=(\d+) players=(\d+) width=(\d+) '
                            r'columns=(\d+) rows=(\d+)', log)
        check(len(stages) == 3, 'resize did not publish all three actual pane dimensions')
        names = (('vs-resize-stacked', 'vs-resize-side', 'vs-resize-restored')
                 if self.mode == 'vs' and self.players == 2 else
                 ('local-resize-before', 'local-resize-wide', 'local-resize-restored'))
        captures = []
        widths = []
        for expected_stage, (metadata, name) in enumerate(zip(stages, names), 1):
            stage, players, width, columns, rows = map(int, metadata)
            check((stage, players) == (expected_stage, self.players), 'resize stages out of order')
            capture = read_ppm(directory / 'screens' / (name + '.ppm'))
            check(capture[:2] == (columns * width * self.scale, rows * 160 * self.scale),
                  'capture dimensions disagree with the real split framebuffer')
            check(width >= 240 and (not self.aspect or width == self.aspect),
                  'fixed aspect was not retained per pane')
            widths.append(width)
            captures.append(capture)
        check(captures[0] == captures[2], 'restored paused world/HUD pixels changed')
        if not self.aspect:
            check(widths[1] > widths[0], 'Auto aspect did not expand the actual world view')
        print(f'Local video routes: PASS players={self.players} mode={self.mode} '
              f'aspect={self.aspect} fps={self.fps} scale={self.scale} renderer={self.renderer}', flush=True)


def options_rows(binary, pack, work):
    """Observe visible labels, rather than trusting a host's option count."""
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    for modern, rows in ((True, 5), (False, 5)):
        style = 'modern' if modern else 'classic'
        frames = work / f'{style}-frames'
        tour = 'DDDAE' + '.D' * (rows - 1) + '.'
        _, log = runner.run(style, ['--menu-tour', pack, work / f'{style}-save', tour,
                                  frames, '--no-classic-menus' if modern else '--classic-menus',
                                  '--short-transitions', '--no-mods'],
                            MKSC_AID_MODERN_MENUS='1',
                            MKSC_MODERN_MENU_SIZE='640x360')
        check('Menu tour: PASS' in log, f'{style}: ordinary Options tour failed')
        captures = [read_ppm(frames / f'{5 + cursor * 2:03d}.ppm') for cursor in range(rows)]
        expected = (640, 360) if modern else (240, 160)
        check(all(capture[:2] == expected for capture in captures), f'{style}: wrong menu dimensions')
        first = frames / '005.ppm'
        first_hash = hashlib.sha256(first.read_bytes()).hexdigest()
        print(f'Options rows capture: style={style} rows={rows} cursors={rows} '
              f'first={first} sha256={first_hash}', flush=True)
        machine = platform.machine().lower().replace('amd64', 'x86_64').replace('aarch64', 'arm64')
        host = (sys.platform, machine)
        check(host in OPTIONS_FIRST_OPEN, f'unreviewed Options baseline for {host}: {first_hash}')
        if not modern:
            check(first_hash == OPTIONS_FIRST_OPEN[host][style],
                  f'{style}: first-open Options changed from the reviewed all-rows capture')
        for row in range(rows):
            # Classic selection adds only a cursor outside the label bounds.
            # Modern selection changes white glyphs to dark gold; their fully
            # covered interiors remain at exactly the same physical positions.
            reference_cursor = 1 if row == 0 else 0
            width, _, pixels = captures[reference_cursor]
            if modern:
                left, right, top, bottom = 68, 320, 92 + 28 * row, 120 + 28 * row
                mask = [y * width + x for y in range(top, bottom) for x in range(left, right)
                        if pixels[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\xff\xff\xff']
                check(len(mask) >= 20, f'{style}: row {row} has no visible label before selection')
                for cursor, (_, _, current) in enumerate(captures):
                    color = b'\x3a\x2a\x00' if cursor == row else b'\xff\xff\xff'
                    check(all(current[index * 3:index * 3 + 3] == color for index in mask),
                          f'{style}: row {row} disappears or moves at cursor {cursor}')
            else:
                top = 26 + 14 * row
                positions = [(y * width + x) * 3 for y in range(top, top + 14) for x in range(20, 156)]
                label = b''.join(pixels[index:index + 3] for index in positions)
                check(len(set(label)) > 2, f'{style}: row {row} has no visible label')
                for cursor, (_, _, current) in enumerate(captures):
                    check(label == b''.join(current[index:index + 3] for index in positions),
                          f'{style}: row {row} disappears or moves at cursor {cursor}')
        # The longest renamed value must stay readable in both layouts. This
        # selects the same numeric profile as historical modern-air commands.
        air_frames = work / f'{style}-air-frames'
        _, air_log = runner.run(style + '-air', ['--menu-tour', pack,
            work / f'{style}-air-save', 'DDDAED...', air_frames,
            '--no-classic-menus' if modern else '--classic-menus',
            '--short-transitions', '--no-mods', '--drift-extras', 'tiers-air-boost'],
            MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')
        check('Menu tour: PASS' in air_log and 'Driving profile active: modern-air' in air_log,
              f'{style}: canonical Drift Extras did not select the historical air profile')
        air_first = air_frames / '005.ppm'
        if not modern:
            check(hashlib.sha256(air_first.read_bytes()).hexdigest() == OPTIONS_AIR_FIRST_OPEN[host][style],
                  f'{style}: longest Drift Extras value changed from the reviewed capture')
        check(read_ppm(air_frames / '006.ppm')[:2] == expected,
              f'{style}: selecting Drift Extras lost the menu extent')
    print('Options rows: PASS modern=5 classic=5 all cursors visible drift-extras=off,tiers-air-boost', flush=True)


def gpu_ui_probe(probe, work, video_driver, gles, deadline):
    environment = {key: value for key, value in os.environ.items() if not key.startswith('MKSC_')}
    environment.update(SDL_VIDEO_DRIVER=video_driver, SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
    command = [str(probe)] + (['--ui-only'] if gles else [])
    remaining = deadline - time.monotonic()
    check(remaining > 0, 'GPU UI probe exceeded its time budget')
    try:
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=min(30, remaining))
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b''
        if isinstance(output, bytes):
            output = output.decode('utf-8', errors='replace')
        (work / 'probe.log').write_text(output, encoding='utf-8')
        raise AssertionError('GPU UI probe exceeded its bounded deadline') from error
    (work / 'probe.log').write_text(result.stdout, encoding='utf-8')
    if result.returncode == 77:
        print('GPU UI route unavailable: ' + result.stdout.strip(), flush=True)
        return False
    check(result.returncode == 0, f'GPU UI probe failed: {result.returncode}\n{result.stdout[-6000:]}')
    return True


def gpu_ui(binary, pack, work, probe, video_driver, gles=False):
    """Exercise ordinary modern Options on the actual accelerated compositor."""
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    if not gpu_ui_probe(probe, work, video_driver, gles, runner.deadline):
        return 77
    common = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_AID_MODERN_MENUS='1')
    common['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'

    def picture(path, dimensions):
        width, height, pixels = read_ppm(path)
        check((width, height) == dimensions, f'{path}: accelerated canvas was capped or resized')
        # The checker/panels and ink must survive capture/composition, rather
        # than merely producing a correctly sized solid or uninitialized PPM.
        colors = {pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]
                  for y in range(0, height, max(1, height // 80))
                  for x in range(0, width, max(1, width // 120))}
        check(len(colors) >= 64, f'{path}: missing meaningful composed UI picture')
        return width, height, pixels

    def ink(capture, box, selected=False):
        width, height, pixels = capture
        scale = width / 1280
        left, top, right, bottom = [round(value * scale) for value in box]
        count = 0
        columns = set()
        rows = set()
        for y in range(top, min(bottom, height)):
            for x in range(left, min(right, width)):
                offset = (y * width + x) * 3
                r, g, b = pixels[offset:offset + 3]
                visible = (35 <= r <= 85 and 20 <= g <= 65 and b <= 20) if selected else min(r, g, b) >= 245
                if visible:
                    count += 1
                    columns.add(x)
                    rows.add(y)
        # This observes actual glyph interiors in the label-only area. Value
        # pills are to the right and the highlight is gold, not dark ink.
        check(count >= 60 * scale * scale and len(columns) >= 35 * scale
              and len(rows) >= 7 * scale, 'Options label ink is missing, clipped or unreadable')
        return count

    def tour(name, size, script, retained):
        frames = work / (name + '-frames')
        _, log = runner.run(name, ['--menu-tour', pack, work / (name + '-save'), script,
            frames, '--no-mods', '--no-classic-menus', '--short-transitions'],
            **dict(common, MKSC_MODERN_MENU_SIZE=f'{size[0]}x{size[1]}'))
        check('Menu tour: PASS' in log, f'{name}: ordinary Options tour failed')
        compositor = re.search(r'GPU UI compositor: driver=(\S+) commands=(\d+) full_canvas_upload=0', log)
        check(compositor is not None and int(compositor[2]) > 0,
              f'{name}: accelerated UI compositor did not submit commands')
        check('GPU UI fallback:' not in log, f'{name}: silently fell back to CPU UI')
        if gles:
            check(compositor[1] == 'opengles2', f'{name}: did not exercise GLES geometry')
        captures = {}
        paths = sorted(frames.glob('*.ppm'))
        check(paths, f'{name}: no real menu captures')
        for path in paths:
            if path.stem in retained:
                captures[path.stem] = picture(path, size)
            else:
                # Only this fresh route's redundant captures are removed.
                path.unlink()
        check(set(captures) == set(retained), f'{name}: required settled captures missing')
        return captures, compositor[1]

    native, driver = tour('native-options', (1280, 720), 'DDDA.r.r.r.',
                          ('005', '007', '009', '011'))
    for page in ('005', '007', '009'):
        ink(native[page], (136, 193, 700, 232), selected=True)
        ink(native[page], (136, 249, 700, 288))
    # The fourth page has its own list geometry; test its Installed Mods label
    # and the populated detail/footer rather than applying ordinary row bounds.
    ink(native['011'], (116, 184, 500, 210))
    check(len({hashlib.sha256(capture[2]).digest() for capture in native.values()}) == 4,
          'ordinary shoulder navigation did not reveal all four distinct Options pages')
    large, large_driver = tour('4k-options', (3840, 2160), 'DDDArr.', ('007',))
    check(driver == large_driver, 'backend changed between native and 4K UI')
    ink(large['007'], (136, 193, 700, 232), selected=True)
    ink(large['007'], (136, 249, 700, 288))
    check(time.monotonic() <= runner.deadline, 'GPU UI route exceeded its 175-second budget')
    print(f'GPU UI routes: PASS driver={driver} native=1280x720 tabs=4 '
          'large=3840x2160 full_canvas_upload=0 readable_labels=1', flush=True)
    return 0


def gpu_ui_race(binary, pack, work, probe, video_driver, gles=False):
    """Complete real TT and GP races with modern accelerated result owners."""
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    if not gpu_ui_probe(probe, work, video_driver, gles, runner.deadline):
        return 77
    environment = dict(SDL_VIDEO_DRIVER=video_driver, MKSC_AID_MODERN_MENUS='1',
                       MKSC_MODERN_MENU_SIZE='640x360', MKSC_AID_RESULTS_HOLD='1')
    environment['MKSC_AID_GLES' if gles else 'MKSC_AID_GPU'] = '1'
    directory, log = runner.run('race', ['--shell-smoke', pack, work / 'race-save', 1,
        '--no-mods', '--no-classic-menus', '--short-transitions'], **environment)
    check(re.search(r'Game shell Time Trial: PASS .*results=returned', log),
          'GPU UI race did not finish Time Trial and return from its results')
    check('Game shell: PASS title -> Time Trial -> results -> GP race -> results' in log,
          'GPU UI race did not finish GP and its results through the shared shell')
    compositors = re.findall(r'GPU UI compositor: driver=(\S+) commands=(\d+) full_canvas_upload=0', log)
    check(compositors and all(int(count) > 0 for _, count in compositors),
          'GPU UI race never submitted accelerated UI commands')
    check('GPU UI fallback:' not in log, 'race/results UI fell back to a CPU canvas')
    if gles:
        check(all(driver == 'opengles2' for driver, _ in compositors),
              'GPU UI race did not exercise the GLES geometry backend')
    # Short transitions intentionally omit GP's point-award board. The final
    # sorted cup board remains reachable, as does TT's ordinary action menu.
    required = ('tt-results-modern', 'gp-cumulative-modern')
    for name in required:
        path = directory / 'screens' / (name + '.ppm')
        check(path.is_file(), f'{name}: settled modern results were not captured')
        width, height, pixels = read_ppm(path)
        check((width, height) == (640, 360), f'{name}: wrong modern result canvas extent')
        colors = {pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]
                  for y in range(0, height, 4) for x in range(0, width, 4)}
        check(len(colors) >= 48, f'{name}: empty or solid composed result picture')
        bright_title = sum(min(pixels[offset:offset + 3]) >= 235
                           for y in range(10, 84) for x in range(25, 500)
                           for offset in ((y * width + x) * 3,))
        check(bright_title >= 100, f'{name}: modern result title ink is absent')
    check(time.monotonic() <= runner.deadline, 'GPU UI race exceeded its 175-second budget')
    print(f'GPU UI race: PASS driver={compositors[-1][0]} TT=results-returned '
          'GP=results-returned captures=640x360 full_canvas_upload=0', flush=True)
    return 0


def mods_tab(binary, pack, work, scenario, device='semantic'):
    from make_mod_zip_fixture import manager_fixture, installer_fixture
    fixture = work / 'fixture'
    (manager_fixture if scenario == 'manager' else installer_fixture)(fixture)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    prefix = 'DDDArrr'  # Game -> Controls -> Enhancements -> Mods via shoulders.
    device_environment = {} if device == 'semantic' else {'MKSC_MENU_TOUR_DEVICE': device}
    settings = fixture / 'data/settings.json'

    def tour(name, script, **environment):
        directory, log = runner.run(name, ['--menu-tour', pack, work / f'{name}-save',
            prefix + script, work / f'{name}-frames', '--no-classic-menus', '--short-transitions'],
            MKSC_DATA_DIR=str(fixture / 'data'), MKSC_MENU_TOUR_PERSIST_SETTINGS='1',
            MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360',
            **device_environment, **environment)
        check('Menu tour: PASS' in log, f'{name}: ordinary Mods tab tour failed')
        if device != 'semantic':
            check('Menu tour device adapter: ' + device in log,
                  f'{name}: binary did not execute the requested {device} adapter')
        captures = sorted((work / f'{name}-frames').glob('*.ppm'))
        check(captures and all(read_ppm(path)[:2] == (640, 360) for path in captures),
              f'{name}: Mods tab did not retain modern full-window presentation')
        return log

    if scenario == 'manager':
        initial = settings.read_bytes()
        # Browsing the advanced rows and the empty load order publishes nothing.
        tour('browse', 'D' * 9 + 'ADABA...')
        check(settings.read_bytes() == initial, 'browsing advanced rows changed saved mods')
        # One press on b-after turns it on with the a-base it depends on.
        log = tour('enable', 'DA...')
        check('mod selection applied: count=2' in log, 'one-step enable did not load the dependency chain')
        saved = settings.read_bytes()
        check(json.loads(saved)['mods'] == ['a-base', 'b-after'], 'saved load order is wrong')
        log = tour('relaunch', '...')
        check('mod loaded: b-after' in log, 'fresh process lost the enabled set')
        # Advanced load order: move a-base after its dependent, reject that,
        # move it back, cancel the review once, then apply.
        log = tour('order', 'EADA' + 'S.' + 'AA' + 'S.B' + 'S.A...')
        check('mod selection rejected:' in log and 'mod selection applied: count=2' in log,
              'order rejection/restoration and Apply did not execute')
        check(settings.read_bytes() == saved, 'reviewed order changed the saved selection')
        log = tour('discard', 'EADDAB' + 'DA...')
        check('mod changes discarded' in log and settings.read_bytes() == saved,
              'leaving a staged load order did not offer a discard')
        for name, downs in (('lua', 2), ('pack', 3), ('zip', 4)):
            log = tour('reject-' + name, 'D' * downs + 'A...')
            check('mod selection rejected:' in log, f'{name}: invalid mod turned on')
            check(settings.read_bytes() == saved, f'{name}: rejection changed saved mods')
        blocker = settings.with_name('settings.json.tmp')
        blocker.mkdir()
        try:
            log = tour('save-failure', 'A...')
            check('mod selection save failed; previous mods remain active' in log,
                  'failed write did not retain the active owner')
            check(settings.read_bytes() == saved, 'failed write changed persisted settings')
        finally:
            blocker.rmdir()
        log = tour('disable', 'A...')
        check('mod selection applied: count=0' in log and json.loads(settings.read_bytes())['mods'] == [],
              'turning off a dependency did not turn off its dependent')
    else:
        log = tour('install', 'F...', MKSC_MENU_TOUR_DROP_FILE=str(fixture / 'picked.zip'))
        check('mod archive installed: installed 1.0.0' in log and 'mod selection applied: count=1' in log,
              'dropping a ZIP did not install and turn it on')
        installed = fixture / 'data/mods/installed.zip'
        check(installed.read_bytes() == (fixture / 'picked.zip').read_bytes(), 'installer changed ZIP bytes')
        saved = settings.read_bytes()
        check(json.loads(saved)['mods'] == ['installed'], 'installed archive was not saved as enabled')
        log = tour('update', 'F...', MKSC_MENU_TOUR_DROP_FILE=str(fixture / 'updated.zip'))
        check('mod archive installed: installed 2.0.0' in log and 'mod loaded: installed 2.0.0' in log
              and settings.read_bytes() == saved, 'update did not reload the enabled archive in place')
        archive = (fixture / 'updated.zip').read_bytes()
        check(installed.read_bytes() == archive, 'update did not publish the exact archive')
        for rejected in ('bad-lua', 'bad-motion', 'bad-race', 'traversal', 'oversized'):
            log = tour('reject-' + rejected, 'F...',
                       MKSC_MENU_TOUR_DROP_FILE=str(fixture / (rejected + '.zip')))
            check('mod archive rejected:' in log, f'{rejected}: invalid archive accepted')
            check(installed.read_bytes() == archive and settings.read_bytes() == saved,
                  f'{rejected}: failed installation changed published files')
    print(f'Modern Mods tab: PASS scenario={scenario} device={device} ordinary tabs, one-step changes, review and rollback', flush=True)



def mod_workflow(binary, pack, work, classic=False, race_only=False):
    """Edit live files only after the child captures its loaded Mods page."""
    from make_mod_zip_fixture import manager_fixture
    fixture = work / 'fixture'
    manager_fixture(fixture)
    runner = LocalVideoRoute(binary, pack, work, 'vs2-wide120')
    settings = fixture / 'data/settings.json'
    prefix = 'DDDAEE' if classic else 'DDDArrr'
    expected_size = (240, 160) if classic else (640, 360)
    style = '--classic-menus' if classic else '--no-classic-menus'
    common = dict(MKSC_DATA_DIR=str(fixture / 'data'), MKSC_MENU_TOUR_PERSIST_SETTINGS='1',
                  MKSC_AID_MODERN_MENUS='1', MKSC_MODERN_MENU_SIZE='640x360')

    def tour(name, script, edit=None, watch=False, race=False, extra_args=(), drop_path=None):
        frames = work / (name + '-frames')
        arguments = ['--menu-tour', pack, work / (name + '-save'), prefix + script,
                     frames, style, '--short-transitions', *extra_args]
        if watch:
            arguments += ['--watch-mods']
        environment = dict(common)
        if drop_path is not None:
            environment['MKSC_MENU_TOUR_DROP_FILE'] = str(drop_path)
        if edit is None:
            _, log = runner.run(name, arguments, **environment)
        else:
            directory = work / name
            directory.mkdir()
            env = {key: value for key, value in os.environ.items() if not key.startswith('MKSC_')}
            env.update(SDL_VIDEO_DRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                       SDL_MAC_BACKGROUND_APP='1', SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0',
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED='0')
            env.update(environment)
            if watch:
                env['MKSC_MENU_TOUR_DELAY_MS'] = '125'
            if race:
                env['MKSC_MENU_TOUR_RACES'] = '1'
            process = subprocess.Popen([str(binary), *map(str, arguments)], env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                # The first wait token captures the settled, already-loaded list.
                # Eight ordinary waits remain before Reload, so disk edits never
                # depend on the child's CPU speed or a runtime mutation hook.
                ready = frames / f'{len(prefix):03d}.ppm'
                while True:
                    check(process.poll() is None, f'{name}: exited before loaded Mods capture')
                    check(time.monotonic() < runner.deadline, f'{name}: capture deadline expired')
                    if ready.exists():
                        try:
                            read_ppm(ready)
                            break
                        except AssertionError:
                            pass  # The child may still be writing the P6 body.
                    time.sleep(.005)
                edit()
                remaining = runner.deadline - time.monotonic()
                check(remaining > 0, f'{name}: no remaining route budget')
                log, _ = process.communicate(timeout=remaining)
                (directory / (name + '.log')).write_text(log, encoding='utf-8')
                check(process.returncode == 0, f'{name}: exit {process.returncode}\n{log[-6000:]}')
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()
        check('Menu tour: PASS' in log, f'{name}: ordinary workflow failed')
        captures = sorted(frames.glob('*.ppm'))
        check(captures and all(read_ppm(path)[:2] == expected_size for path in captures),
              f'{name}: menu presentation changed extent')
        return log

    initial = settings.read_bytes()
    # Classic removes the selected load-order row with A; modern opens actions.
    remove = 'A' if classic else 'ADDA'
    log = tour('enable', 'A...')
    check('mod selection applied: count=1' in log, 'one press did not turn a-base on')
    check(json.loads(settings.read_bytes())['mods'] == ['a-base'], 'one-step enable saved wrong selection')
    saved = settings.read_bytes()
    if not race_only:
        log = tour('keep', 'E' + remove + 'BDDA...')
        check('mod changes pending: choose Apply, Discard or Keep editing' in log
              and 'mod changes kept for editing' in log, 'Keep editing prompt did not execute')
        check(settings.read_bytes() == saved, 'Keep editing published staged changes')
        log = tour('discard', 'E' + remove + 'BDA...')
        check('mod changes discarded' in log, 'explicit Discard did not execute')
        check(settings.read_bytes() == saved, 'Discard changed saved mods')
        log = tour('apply-back', 'E' + remove + 'BAA...')
        check('mod selection applied: count=0' in log, 'Back Apply did not validate/review/commit')
        check(json.loads(settings.read_bytes())['mods'] == [], 'Back Apply saved wrong selection')
        log = tour('reenable', 'A...')
        check(settings.read_bytes() == saved, 'turning a-base back on changed its saved form')
        log = tour('reload-dirty', 'E' + remove + 'H...')
        check('mod reload rejected: apply or discard staged changes first' in log
              and 'mod reload applied:' not in log and 'mod selection applied:' not in log,
              'F5 discarded or published a dirty staged edit')
        check(settings.read_bytes() == saved, 'dirty F5 changed persisted selection')
    manifest_path = fixture / 'data/mods/a-base/mod.json'
    script_path = manifest_path.with_name('main.lua')
    original_script = script_path.read_bytes()

    def valid_edit():
        manifest = json.loads(manifest_path.read_text())
        manifest['name'] = 'AFTER LIVE RELOAD'
        manifest['version'] = '2.0.0'
        manifest_path.write_text(json.dumps(manifest) + '\n')
        script_path.write_bytes(
            b"function on_race_start(race) race.message('UPDATED LIVE MOD',120) end\n"
            if race_only else original_script + b'-- Authored live reload revision two.\n')

    if race_only:
        log = tour('reload-race', '.' * 9 + 'H...BBBUUUARAAASAAA...', valid_edit, race=True)
        check('mod reload applied:' in log
              and 'mod loaded: a-base 2.0.0 (AFTER LIVE RELOAD)' in log,
              'live edited mod was not reloaded before entering the ordinary race')
        check('Game shell Time Trial: PASS' in log and 'Menu tour: PASS' in log,
              'reloaded content did not complete the ordinary Time Trial and return to menus')
        check(settings.read_bytes() == saved and json.loads(saved)['mods'] == ['a-base'],
              'entering a race after Reload changed persisted selection/scalars')
        check(time.monotonic() <= runner.deadline, 'reload-race exceeded its 175-second budget')
        print('Mod reload race: PASS live-edit reload ordinary-time-trial settings-retained', flush=True)
        return
    log = tour('reload-f5', '.' * 9 + 'H...', valid_edit)
    check('mod reload applied:' in log, 'F5 did not replace the loaded content owner')
    check(log.count('mod loaded: a-base') >= 2
          and 'mod loaded: a-base 2.0.0 (AFTER LIVE RELOAD)' in log,
          'F5 did not reload the edited manifest/script after the initial owner was loaded')
    check(settings.read_bytes() == saved, 'Reload changed persisted selection/scalars')
    valid_script = script_path.read_bytes()
    try:
        log = tour('reload-invalid', '.' * 9 + 'H...',
                   lambda: script_path.write_bytes(b'function broken( invalid syntax\n'))
        check('mod selection rejected:' in log and 'mod reload applied:' not in log,
              'invalid live disk edit replaced the active owner')
        check(settings.read_bytes() == saved, 'invalid Reload changed saved settings')
    finally:
        script_path.write_bytes(valid_script)
    # Five installed rows, then Install, Import, Store, Workshop and the
    # Advanced row at nine; expanded, Load order is ten and Reload eleven.
    log = tour('reload-action', '.' * 9 + 'D' * 9 + 'ADDA...', valid_edit)
    check('mod reload applied:' in log, 'ordinary Reload action did not reload content')
    check(settings.read_bytes() == saved, 'Reload action persisted temporary CLI video/menu values')
    inactive = fixture / 'data/mods/b-after'
    parked = work / 'parked-b'
    try:
        # The loaded catalog initially has five mods, so the last advanced row
        # (Add mod folder) is twelve. After removing an inactive folder it
        # must clamp to eleven; one Up then selects Reload, not a picker.
        log = tour('reload-shrink', '.' * 9 + 'D' * 9 + 'ADDDHUA...',
                   lambda: inactive.rename(parked))
        check(log.count('mod reload applied:') == 2,
              'catalog shrink did not clamp the last action cursor before Up/Reload')
        check(settings.read_bytes() == saved and json.loads(saved)['mods'] == ['a-base'],
              'catalog shrink changed the active or persisted selection')
    finally:
        if parked.exists():
            parked.rename(inactive)
    def watched_edit():
        manifest = json.loads(manifest_path.read_text())
        manifest['version'] = '3.0.0'
        manifest['name'] = 'WATCHED LIVE EDIT'
        manifest_path.write_text(json.dumps(manifest) + '\n')
        script_path.write_bytes(valid_script + b'-- Watched revision three.\n')

    log = tour('watch-valid', '.' * 12, watched_edit, watch=True)
    check('mod reload applied:' in log
          and 'mod loaded: a-base 3.0.0 (WATCHED LIVE EDIT)' in log,
          'wall-clock watcher did not reload edited content without F5')
    check(settings.read_bytes() == saved, 'watch reload changed persisted settings')
    watched_script = script_path.read_bytes()
    try:
        log = tour('watch-invalid', '.' * 12,
                   lambda: script_path.write_bytes(b'function bad( watched invalid syntax\n'), watch=True)
        check('mod selection rejected:' in log and 'mod reload applied:' not in log,
              'watcher published invalid disk content')
        check(settings.read_bytes() == saved, 'failed watch reload changed saved settings')
    finally:
        script_path.write_bytes(watched_script)
    extra_root = work / 'extra-mods'
    extra = extra_root / 'extra-only'
    extra.mkdir(parents=True)
    (extra / 'mod.json').write_text(json.dumps({'id':'extra-only','name':'Extra Folder Mod',
        'version':'1.0.0','api_version':1,'script':'main.lua'}) + '\n')
    (extra / 'main.lua').write_text('-- Original test-only external mod.\n')
    scalar_defaults = {key: value for key, value in json.loads(saved).items()
                       if key not in ('mods', 'mod_directories')}

    def verify_scalars(document):
        check({key: value for key, value in document.items()
               if key not in ('mods', 'mod_directories')} == scalar_defaults,
              'folder workflow persisted temporary CLI values or changed scalar settings')

    log = tour('external-add-folder', 'F...', drop_path=extra_root)
    remembered = json.loads(settings.read_bytes())
    check('mod folder remembered:' in log and remembered.get('mod_directories') == [str(extra_root)]
          and remembered['mods'] == ['a-base'],
          'ordinary directory drop did not remember the folder while retaining the active selection')
    verify_scalars(remembered)
    log = tour('external-enable', 'A...' + 'D' * 5 + 'A...')
    persisted = json.loads(settings.read_bytes())
    check('mod selection applied: count=1' in log and persisted['mods'] == ['extra-only']
          and persisted.get('mod_directories') == [str(extra_root)],
          'ordinary catalog toggles did not apply the external mod')
    verify_scalars(persisted)
    external_saved = settings.read_bytes()
    log = tour('external-relaunch', '...')
    check('mod loaded: extra-only' in log, 'fresh process ignored persisted extra root')
    check(settings.read_bytes() == external_saved, 'relaunch rewrote persisted roots or scalar settings')
    log = tour('external-legacy-root', '...', extra_args=('--mods-dir', extra_root))
    check('mod loaded: extra-only' in log,
          'legacy CLI root duplicated a remembered folder and prevented startup')
    check(settings.read_bytes() == external_saved,
          'legacy CLI root rewrote remembered folders or scalar settings')
    # Emulate the old command before roots were saved, then remember that
    # primary root using the same ordinary directory-drop handler.
    persisted.pop('mod_directories', None)
    settings.write_text(json.dumps(persisted) + '\n')
    log = tour('legacy-remember-primary', 'F...', extra_args=('--mods-dir', extra_root),
               drop_path=extra_root)
    migrated = json.loads(settings.read_bytes())
    check('mod folder remembered:' in log and migrated.get('mod_directories') == [str(extra_root)]
          and migrated['mods'] == ['extra-only'],
          'dropping the active CLI primary folder did not remember it exactly once')
    verify_scalars(migrated)
    migrated_bytes = settings.read_bytes()
    log = tour('legacy-migrated-relaunch', '...')
    check('mod loaded: extra-only' in log and settings.read_bytes() == migrated_bytes,
          'remembered former CLI root did not survive a fresh no-flag launch')
    check(time.monotonic() <= runner.deadline, 'workflow exceeded its 175-second budget')
    print(f'Mod workflow: PASS style={"classic" if classic else "modern"} '
          'pending apply discard keep reload f5 rollback roots relaunch watch', flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--case', choices=CASES)
    selection.add_argument('--options-rows', action='store_true')
    selection.add_argument('--gpu-ui', action='store_true')
    selection.add_argument('--gpu-ui-race', action='store_true')
    selection.add_argument('--mods-tab', choices=('manager', 'install', 'workflow', 'reload-race'))
    parser.add_argument('--gpu-probe', type=Path)
    parser.add_argument('--video-driver', default='offscreen')
    parser.add_argument('--gles', action='store_true')
    parser.add_argument('--classic-mods', action='store_true')
    parser.add_argument('--menu-device', choices=('semantic', 'keyboard', 'gamepad', 'touch'), default='semantic')
    args = parser.parse_args()
    check(args.binary.is_file() and args.pack.is_file(), 'binary and owner pack must exist')
    if args.gpu_ui or args.gpu_ui_race:
        check(args.gpu_probe is not None and args.gpu_probe.is_file(), 'GPU UI modes require an existing --gpu-probe')
        check(bool(args.video_driver), '--video-driver must not be empty')
    else:
        check(args.gpu_probe is None and not args.gles, '--gpu-probe/--gles require --gpu-ui or --gpu-ui-race')
    check(not args.work.exists(), 'work must be fresh; refusing to delete existing files')
    args.work.mkdir(parents=True)
    if args.gpu_ui_race:
        return gpu_ui_race(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                           args.gpu_probe.resolve(), args.video_driver, args.gles)
    if args.gpu_ui:
        return gpu_ui(args.binary.resolve(), args.pack.resolve(), args.work.resolve(),
                      args.gpu_probe.resolve(), args.video_driver, args.gles)
    if args.mods_tab in ('workflow', 'reload-race'):
        mod_workflow(args.binary.resolve(), args.pack.resolve(), args.work.resolve(), args.classic_mods,
                     args.mods_tab == 'reload-race')
    elif args.mods_tab:
        mods_tab(args.binary.resolve(), args.pack.resolve(), args.work.resolve(), args.mods_tab, args.menu_device)
    elif args.options_rows:
        options_rows(args.binary.resolve(), args.pack.resolve(), args.work.resolve())
    else:
        LocalVideoRoute(args.binary.resolve(), args.pack.resolve(), args.work.resolve(), args.case).verify()


if __name__ == '__main__':
    sys.exit(main())

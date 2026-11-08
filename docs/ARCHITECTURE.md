# Architecture

MKSC Neo is a native C11 program. The original game's rules (physics,
collision, CPU drivers, items and scoring) were recovered from the decompilation
and re-implemented as a deterministic simulation. SDL 3 provides windows, input,
audio and graphics. All game content comes from the player's ROM at run time.

## Source map

| Directory | Contents |
|---|---|
| `portable/core/` | Fixed-tick simulation: kart physics, collision, terrain, camera, effects, and the 3D course physics. |
| `portable/app/` | Game flow: menus, race sessions, CPU drivers, items, scoring, ceremonies, replays. |
| `portable/assets/` | Pack reading, content composition and typed views over pack sections. |
| `portable/import/` | ROM importers: Super Circuit to a game pack, and Mario Kart DS / 64 to mods. |
| `portable/audio/` | Sequencers (GBA, DS and N64 songs), voices and mixing. |
| `portable/platform/` | SDL layer: entry point, shell, renderers, input, files, mods, Lua host, store. |
| `portable/data/` | Built-in rule profiles and menu data. |
| `portable/tests/` | C unit tests and executable smoke routes. |
| `tools/portable/` | Test runner, reference importer, mod kit, format compilers, packaging. |
| `tools/site/` | Project website generator. |
| `mods/examples/` | Example mods and generators. |

`core/`, `app/`, `assets/` and `audio/` don't depend on SDL or the filesystem.
The platform layer supplies clocks, input, files and presentation.

## Simulation

- The game runs at the GBA's fixed tick rate (4389/262144 s, about 59.73 Hz),
  with up to four catch-up ticks per frame. Input is sampled once per frame.
- The simulation uses integer and fixed-point maths and keeps the original update
  order and random number draws, so races are deterministic. Replays and ghosts
  record inputs and are checked against state checksums.
- Each tick publishes an immutable snapshot (camera, karts, objects, effects, audio
  requests). Rendering reads snapshots and never changes simulation state.
  Optional interpolation blends between the last two snapshots for high frame rates.
- Sessions are owned by the caller, and the simulation doesn't allocate memory.

## Content

1. **Import.** `mksc --import` (or the first-start screen) verifies the US ROM and
   converts it into a game pack. The Python importer `tools/portable/import_rom.py`
   produces the same bytes, and the tests check that they match. Packs carry an
   importer version, so the game can offer to re-import after an update.
2. **Packs.** A pack is a list of typed, versioned sections identified by
   `(kind, content_id)`: art, palettes, tracks, rules, music, text and so on.
   Readers validate every length, range and reference before use.
3. **Mods.** Each enabled mod adds a layer. Later layers replace sections with the
   same key or add new ones, in the order the player chose. Mods are prepared and
   validated on a worker thread, then swapped in all at once, so a failed mod
   never leaves half-applied content. Lua runs in a sandbox per mod per race
   (`platform/script_host.c`, `app/race_script_hooks.h`).
4. **Saves.** Settings, records, ghosts and GP progress live in the data folder.
   They use versioned formats and are written to a temporary file that then
   replaces the old one. Records are keyed by a content identity, which covers
   the mods, rule profiles and mirror mode, so different content never shares records.

## Rendering

- **Original renderer.** A software renderer reproduces the GBA's mode-7 road,
  sprites, sky and HUD in a 240×160 logical space, at 1–8× internal resolution
  and any aspect ratio.
- **3D renderer (optional).** Draws the same 2D course as textured geometry with
  depth-sorted sprites, or with 3D models from mods.
- **3D courses.** Mesh courses render through SDL_GPU (Metal, Vulkan or D3D12),
  OpenGL ES, or a CPU fallback. Optional HD-2D post-processing (bloom, depth of
  field, ambient occlusion, colour grading, fog) runs on SDL_GPU. Shaders are
  pre-compiled and embedded (`platform/shaders/`).
- **Modern menus.** These are drawn at window resolution with anti-aliased shapes
  and SDF text from the same menu state as the classic 240×160 menus
  (`platform/modern_menu*.c`).

## Audio

The simulation emits ordered sound commands. The audio session runs the original
sequencer and voice priorities. The platform mixes engine and scene sound into an
SDL audio stream. Imported DS and N64 courses bring their own sequencers and
sound sets.

## 3D courses

3D courses are a separate domain from the recovered 2D game. They have their own
course IDs, records, ghosts and replay formats, and they never change 2D saves.

- Authored as glTF plus `course.json`, then compiled by
  `tools/portable/course_3d_format.py` into pack sections (geometry, collision,
  checkpoints, routes, objects, materials). The game reads only those sections,
  never glTF.
- Physics are integer-only and follow rules recovered from Mario Kart 8 Deluxe
  (one MK8 frame per tick). Per-course data sets kart stats, items and coins.
- Gravity, surface rules, moving objects, hazards, cannons and gliders are
  driven by data, so imported DS/N64 courses and authored mods share one engine.

## Other games

Importing Mario Kart DS or Mario Kart 64 converts the player's ROM in the
executable (`import/nds/`, `import/n64/`) into a local mod. The mod
contains 3D courses, drivers, karts, items, music and sounds. Nothing derived from
any ROM is committed or distributed.

## Platforms

- **Desktop** (macOS, Linux, Windows): SDL 3 app with a data folder in the user's
  application-data directory.
- **Android**: the same code built as `libmksc.so` inside SDL's Java activity,
  with a fullscreen landscape layout and optional touch controls.
- **Web**: Emscripten, single-threaded. The page (`platform/browser.html`) imports
  the ROM in the browser, keeps it and the data folder in IndexedDB, and fills
  the tab with the canvas. A service worker caches the engine for offline play.

## Conventions

- Four-space indentation and module-prefixed names. Comments explain lifetimes,
  arithmetic and why things happen in a particular order.
- Warnings are errors (`-Wall -Wextra -Wpedantic`; MSVC `/W4`).
- File formats are read byte by byte, never by casting structs, so the game
  behaves the same on big-endian and 32-bit CPUs (checked by the PowerPC and i686
  builds).
- Changes must keep pack bytes, replay checksums and rendered pixels the same
  unless they are meant to change them.

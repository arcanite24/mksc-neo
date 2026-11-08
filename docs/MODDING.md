# Modding

Mods can change rules and data, add Lua scripts, add 2D tracks and drivers,
add 3D courses, and replace karts and scenery with 3D models. Mods are built
on top of the player's imported game. Never ship anything taken from the ROM
in a mod.

## Install and enable

Put `ID.zip` or an `ID/` folder containing `mod.json` in the data folder's
`mods/` directory (see [Building](BUILDING.md#run) for the location). In the game,
open Options > Mods:

- Confirm on a mod turns it on or off and loads it right away. Turning a mod on
  also turns on the mods it depends on.
- **Install** (or dropping a ZIP on the window) installs and turns on a mod.
- **Store** browses mod catalogs: the built-in one plus any HTTPS catalog you add.
- **Load order** reorders mods. Later mods win when they change the same content.
- **Reload** (or F5 in menus) rebuilds the current mods after editing files.
- **Add mod folder** remembers up to four extra folders to load mods from.

If a mod fails to load, the game names the mod and the reason, and the previous
mods stay active.

From the command line (for one launch):

```sh
mksc --mods-dir mymods --mod custom-cup --mod lap-bonus   # this list instead of the saved one
mksc --no-mods                                             # stock game
mksc --watch-mods                                          # reload mods automatically when files change
```

The saved list is `"mods": [...]` in `settings.json`.

## Make a mod

`tools/portable/mod_kit.py` needs only Python. It exports the stock data,
creates mod folders, and validates and builds them:

```sh
python3 tools/portable/mod_kit.py export-base game.pack base --course 27   # stock tables and a track to copy from
python3 tools/portable/mod_kit.py new lua final-boost mymods --name "Final Boost"
python3 tools/portable/mod_kit.py build mymods/final-boost --base game.pack --output dist
mksc --mods-dir dist --mod final-boost
```

`new` makes `lua`, `data` or `track` mods. One mod can combine all three.
`build` writes `dist/ID/` and a reproducible `dist/ID.zip`.

### Manifest

```json
{"id": "loop-land", "name": "Loop Land", "version": "1.0.0", "api_version": 1,
 "pack": "content.pack", "script": "main.lua", "depends": [], "load_after": []}
```

- `id`: lowercase letters, digits, `_` and `-`, at most 63 characters. A ZIP is
  installed as `ID.zip`.
- `depends`: mods that must be enabled earlier in the load order.
- `load_after`: mods that must come earlier if they are enabled.
- `targets` (optional): `["2d"]` (default), `["3d"]` or `["2d", "3d"]`.
- Unknown fields are errors.

## Lua scripts

Each mod gets its own Lua 5.4 sandbox per race. Define any of these global
functions:

| Hook | When |
|---|---|
| `on_race_start(race)` | The race starts (GO). |
| `on_tick(race)` | Every simulation tick, including the countdown. |
| `on_lap(kart, lap)` | A kart completes a lap. |
| `on_item_use(kart, item)` | A kart uses an item. Return an item name to replace it, `false` to cancel, or nothing to keep it. |
| `on_finish(kart, place)` | A kart finishes. |
| `on_race_end(race)` | The race ends. |

The `race` table has `tick`, `phase` (`countdown`, `racing`, `finished`), `laps`,
`course`, `mode` (`gp`, `quick_run`, `battle`, `time_trial`, `vs`), `class`
(50/100/150) and `karts`, plus these functions:

| Function | Does |
|---|---|
| `race.random(n)` / `race.random(lo, hi)` | Deterministic random number (inclusive), private to the mod. |
| `race.message(text, ticks)` | Shows a HUD message (uppercase, up to 48 characters; default 120 ticks). |
| `race.kart(i)` | Snapshot of kart `i` (1-based), or `nil`. |
| `race.give_item(i, name)` | Gives a human player an item if their slot is empty. Returns true on success. |

Kart snapshots have `index`, `player`, `x`, `y`, `speed`, `item`, `lap`, `place`,
`finished` and `driver`. Item names are listed in the exported `items.json`.

```lua
function on_lap(kart, lap)
    if kart.player and lap == race.laps - 1 then
        if race.give_item(kart.index, "star") then
            race.message("Final lap star!", 120)
        end
    end
end
```

Scripts can't touch files or the OS, and `math.random` is not available (use
`race.random` so replays stay in sync). A script that errors or runs too long is
turned off for that race, and other mods keep running.

## Data mods

`data.json` holds partial tables. Fields you leave out keep their earlier values.
The tables cover engine speeds, kart physics, item odds and behavior, GP points,
cup order and text. Copy only what you change from the `export-base` output:

```json
{"gp_points": {"id": 1, "awards": [9, 6, 3, 1, 0, 0, 0, 0]}}
```

`mods/examples/data-tuning/` has more examples.

## 2D tracks and drivers

`mod_kit.py new track loop-land mymods --base game.pack --from 27` copies stock
course 27 into an editable track folder:

- `track.json`: name, music, laps, start, finish and route.
- `tileset.json`: materials (road, offroad, wall, boost, jump, pit, lava, water).
- `tileset.png` holds the 8×8 tiles, and `tilemap.png` has one tile ID per pixel.
- `cup.json` groups four tracks into a cup. Custom course IDs are 128–191.

After changing the road, update the route and delete the derived files
(`raster.png`, `minimap.png`, `ai_rows`, `target_table`, `ai_sentinels`, `grid`).
The kit rebuilds them. `mods/examples/custom-cup/` is a full example.

Custom drivers need character, graphics, physics and roster data.
`mods/examples/shade-driver/build.py` builds a new selectable driver with its own
portrait and stats.

## 3D courses

3D courses are separate from the 2D tracks. They have real height, their own
physics, and their own records and ghosts. They appear as extra cup rows on the
course screen (L/R).

- **Course Workshop** (Options > Mods) is an in-game editor for simple courses
  built from road, ramp and wall pieces. Each course is saved as a `workshop-N` mod.
- **Authored courses**: `course.json` and a glTF scene, compiled by the mod kit.
  `mods/examples/bridge-course/build.py` generates an original example (no game
  assets) and its editable source. It has flags that add items, coins, hazards,
  a GP cup, a battle arena and more (see `--help`).
- **Converted courses**: `tools/portable/convert_course_3d.py --base game.pack --all
  --source SRC --output MODS` turns stock courses into a local 3D mod (with
  editable sources in the empty folder `SRC`). Pass `--handpass` with a file from
  `mods/handpasses/` to add hand-made elevation to a cup.
- **Other games**: importing Mario Kart DS or Mario Kart 64 (Options > Mods >
  Import from another game) creates a 3D mod with that game's courses, drivers
  and karts. It stays on your machine.

Optional files in a 3D course mod:

| File | Contents |
|---|---|
| `physics.json` | Kart physics profiles (start from `portable/data/mesh-kart.json`). |
| `items.json` | Item odds and behavior. |
| `coins.json` | Coin rules. |
| `skies.json`, `palettes.json` | Animated skies and palettes. |

Course properties (`mksc:*` keys in `course.json`) control things like cannons,
gravity paths, gliders, surface types, moving objects, cameras, sounds and
effects. Their names and limits are defined in the compiler,
`tools/portable/course_3d_format.py`, which reports a precise error for anything
it rejects.

## 3D models

Add `models.json` next to `mod.json` to replace a driver's kart or a course's
scenery with 3D meshes when Race Renderer is set to 3D:

```json
{"version": 1,
 "materials": [{"id": 1, "argb": "#FF40C0E0"}],
 "meshes": [{"id": 1, "materials": [1], "obj": "kart.obj"}],
 "bindings": [{"kind": "driver", "target": 1, "art_bank": 0, "pose": 0,
   "mesh": 1, "scale": [1, 1, 1], "pivot": [0, 0, 0]}]}
```

Meshes come from OBJ files or inline `vertices`/`triangles`. Materials take a
colour and an optional PNG `texture`. Anything without a model keeps its sprite.
`mods/examples/low-poly/build.py` builds an original low-poly kart and tree.
The format is defined in `tools/portable/model_format.py`.

## Mod stores

A store is an `index.json` served over HTTPS:

```json
{"format_version": 1, "id": "publisher", "name": "My store", "mods": [
  {"id": "lap-bonus", "name": "Lap Bonus", "author": "me", "version": "1.0.0",
   "description": "…", "download_url": "https://…/lap-bonus.zip",
   "sha256": "…", "size": 1234, "license": "MIT",
   "compatible_game_versions": ["1"], "screenshots": []}]}
```

The game checks the size, hash and manifest before installing. See
`stores/official/index.json`. Add a store in Store > Add store from clipboard.
HTTPS stores need a build with libcurl. The built-in store always works.

## Saves and replays

Records and ghosts are tied to the exact mods that were active, so modded times
never mix with stock ones. Changing any mod file counts as different content.
To replay a modded ghost, enable the same mods first. GP progress is shared.

## Limits

32 mods, 64 MiB per pack, 256 KiB per script, 256 MiB in total. ZIPs: 96 MiB,
256 entries, no encryption or zip64.

## Examples

All in `mods/examples/`. `lucky-start`, `lap-bonus` and `item-swap` are plain
Lua mods you can copy straight into `mods/`. The others have a `build.py` that
generates the mod: `data-tuning`, `custom-cup`, `shade-driver`, `ambient-cycle`,
`bridge-course`, `dual-course` (one track as both 2D and 3D), `low-poly`,
`overlay-kart`, `overlay-scenery`, `neon-night`, `snow-weather`, `coin-effects`,
`impact-effects` and `surface-trails`. Generators that borrow artwork from your
pack write local output only.

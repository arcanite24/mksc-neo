# Modding

API version **1** supports ordered Lua, JSON, track, driver and artwork mods.
The base pack comes from the player's own ROM. Exported or generated game art
stays local; source examples ship scripts and generators.

## Install and enable

Put `ID.zip` or `ID/mod.json` under the data folder's `mods/` directory (locations
in [Building](BUILDING.md)). Modern Options has a fourth Mods tab, reached with
L/R (A/S keys or shoulders); Classic Options opens the same rows with Tab/controller
Back twice. Changes take one step: confirm on a mod turns it on or off and loads it
at once behind the loading screen, then saves the selection. Turning a mod on also
turns on installed mods it depends on and places it as late as its `depends`/
`load_after` rules allow (later matching content wins); turning one off also turns off
mods that depend on it. Install ZIP (or dropping a ZIP), Store downloads and Import
from another game install, turn on and load in the same step. A failure names the mod
and the reason, and the previous selection stays active and saved. Updates also
validate the running and saved selections; invalid archives, folder conflicts and
failed writes preserve the previous installation. Archives are validated without
extraction and saved as `ID.zip` by manifest ID; a containing folder must match it.

The Advanced row shows Load order and review, Reload from disk and Add mod folder.
Load order (also Tab/controller Back) stages edits: modern confirm opens Move earlier,
Move later, Disable and detail-page actions; Classic confirm removes and shoulders
reorder. Start validates the staged order, shows it for review, and confirm applies
and saves it; Back cancels the review. Leaving the load order or Options with staged
edits asks Apply, Discard or Keep editing. Left/right pages ID, version and errors.
Wide UI Backgrounds extends its scenery around the classic manager. Disabling all
entries restores stock content. Changes are blocked during input/audio recording.

Reload from disk rebuilds the current selection without restarting. F5 does the
same from menus; validation or save failures retain the running content. Reload
requires staged changes to be applied or discarded first. Races and
recordings cannot reload. Optional `--watch-mods` polls known manifests, packs
and scripts in menus, debounces edits, and uses the same transaction; it pauses
while editing a staged selection. The default is off (`--no-watch-mods`); Android document-provider folders require Reload/F5 instead of watching.

Add mod folder remembers up to four extra folders alongside the data folder's
`mods/`. Android uses the system document-tree picker and remembers its read grant; ZIP and ROM selections use SDL streams, including `content://` URIs. You can also drop a folder containing ID folders/ZIPs onto Mods. Apply or discard staged edits before adding a folder.
Each ID must occur in only one root; duplicates are visible errors, and enabled
duplicates reject validation. A folder takes precedence over a same-root ZIP.
Installs always write to the primary root; files in extra roots stay
in place. The mod kit and example builders default to this native mods folder
(or `MKSC_DATA_DIR/mods`), and explicit output paths still work.

The saved list and extra folders reload next launch. A temporary `--mods-dir`
replaces the primary root for that launch; an identical remembered path is
searched once. Applying saves IDs, never that CLI path. You can also edit `settings.json`, e.g. `"mods": ["custom-cup", "lap-bonus"]`. For one launch:

```sh
mksc PACK --mods-dir mymods --mod custom-cup --mod lap-bonus
```

Repeated `--mod` replaces the saved list temporarily; `--no-mods` starts stock.
The loader reports accepted mods and names a rejected mod/file/field on stderr.
A failed mod and its dependents are skipped; independent mods can still load.
A folder takes precedence over an archive with the same ID.
Store (in Mods) browses embedded script examples offline; Confirm opens details and Install/Update/Remove. Installs turn the mod on as above; Remove turns it (and mods needing it) off, then deletes the ZIP. Add store from clipboard accepts an HTTPS index URL; dropping a local `index.json` onto Store adds its absolute `file:///` URL. Up to four `store_urls` persist in settings; Next store, Refresh and Forget manage them. Downloads run on a worker; HTTPS requires libcurl >=7.85 at build time. Failed fetches use the last valid cached catalog; the built-in store always works. Added stores are publisher-controlled, not official approval.
Store JSON: `{"format_version":1,"id":"publisher","name":"My store","mods":[...]}`; each entry requires `id`, `name`, `author`, semantic `version`, `description`, `download_url`, lowercase hex `sha256`, byte `size`, `license`, `compatible_game_versions` (array of exact strings; current game compatibility epoch is `"1"`), and `screenshots` (array of HTTPS URLs, may be empty; metadata only). See `stores/official/index.json`. ASCII strings, 64 entries, 512 KiB indexes, 96 MiB ZIPs, 8 compatibility versions and 4 screenshots per entry; unknown/duplicate fields or IDs reject the whole index. Remote catalogs may only request HTTPS downloads; local catalogs also allow absolute file URLs. Built-in URLs are reserved for embedded authored examples. Size/hash, compatibility and manifest ID/version must match before installing; content and Lua follow existing validation/sandbox rules. No downloaded native code is launched. The SHA pins bytes to the publisher's index, not to an independent trust authority. `tools/portable/build_store.py` builds deterministic example ZIPs into the executable and checks their committed index hashes; ROM-derived example generators are excluded.
## Create a mod

The Python kit needs only the standard library. Export stock values and inspect
`courses.json`, `drivers.json`, `items.json`, `labels.json` and `data/*.json`:

```sh
python3 tools/portable/mod_kit.py export-base PACK base --course 27
python3 tools/portable/mod_kit.py new lua final-boost mymods --name "Final Boost"
python3 tools/portable/mod_kit.py build mymods/final-boost --output dist
mksc PACK --mods-dir dist --mod final-boost
```

Edit `main.lua` before building. For example:

```lua
function on_lap(kart, lap)
    if kart.player and lap == race.laps - 1 then
        if race.give_item(kart.index, "star") then
            race.message("Final lap star!", 120)
        end
    end
end
```

`new data faster-gp mymods` creates `data.json`. Copy only the stock records you
need from the export. `new track loop-land mymods --base PACK --from 27 --name
"Loop Land"` creates a track folder and cup; build track mods with `--base PACK`.
These components can coexist in one mod. The kit validates before writing the
built folder and reproducible zip. `mod_kit.py --help` lists subcommands.

## Manifest and composition

A built folder/archive contains this root manifest (a zip may instead have one
containing folder named after the ID):

```json
{"id":"loop-land","name":"Loop Land","version":"1.0.0","api_version":1,
 "pack":"content.pack","script":"main.lua","depends":[],"load_after":[]}
```

IDs contain lowercase letters, digits, `_` or `-`, begin with a letter/digit and
have at most 63 characters. Names are ASCII and versions have three numeric
components. Pack/script paths are relative without `.` or `..` segments. Unknown
or duplicate fields are errors. `depends` must be enabled earlier; `load_after`
must be earlier when enabled. The loader never sorts the list automatically.
Later packs replace sections by `(kind, content_id)` or append new sections. Modern Options > Games shows imported source games in colour and games not yet imported in monochrome. Left/Right selects a game; Confirm imports or stages enable/disable; Select reimports. Down selects Karts & drivers, Items or Course models; Left/Right chooses Mod order, Super Circuit or an enabled mod. Apply validates and saves through the same transaction as Mods; Back offers Apply/Discard for staged changes.
JSON overrides validate as a whole against earlier content before acceptance. `settings.json` accepts `"asset_sources":["@base","","mario-kart-ds"]` (drivers, items, courses): empty keeps mod order, `@base` prefers the base pack, otherwise name an enabled mod ID. CLI/replay: `--asset-source drivers=@base` (also `items=ID`, `courses=ID`, `=auto`). `portable_mods_select_assets` applies these choices to an unpublished candidate; failure requires discarding it. Matching section keys win independently: drivers use character graphics/DRV3/KRT3 and driver BND1 rows; items use HUD/item-box/race-effect/lightning graphics; courses use road/sky/scenery, C3D1/textures/bundles and scenery BND1 rows. Missing matches retain mod order. Base drivers remove optional DRV3/KRT3 and driver bindings to restore source sprites. C3D1 is indivisible, so replacing it also replaces its collision/navigation; no models are transplanted between unrelated courses. Referenced driver/kart/scenery meshes, materials and rigs follow the chosen descriptors; missing resources retain mod order. Resource IDs are global: namespace them to avoid cross-category collisions. Rules, music and scripts retain mod order; the effective digest identifies overridden content. Item presentation is per item-rules profile: BND1 kind-3 rows keyed by (profile, slot) bind a mesh to an item (slot = item - 0x1000, held, trailing and thrown; triples draw their single item), the item box (32, a RIG1 IDLE clip loops it), box shards (33), the Bob-omb blast (34), the Blooper ink (35, drawn over inked players' panes) and HUD icons (40 + item, material 0's texture). The binding's pose word holds flags 1 billboard and 2 spin; MTL1 flag 32 blends straight alpha. Unbound slots keep the stock art. The DS and MK64 importers bind their own item art this way.

Limits: 32 mods, 16 IDs per dependency list, 64 MiB per pack, 256 KiB per script
and 256 MiB total payload. Zips allow stored/deflated entries, at most 96 MiB,
256 entries, 64 MiB per entry and 320 MiB expanded total. Encryption, zip64,
split archives, duplicates and unsafe paths are rejected. Inflation must match
the declared length and CRC. Native bounds are in `platform/mod_loader.h` and
`platform/mod_archive.c`; kit validation mirrors them.

## Lua API

Each mod has a fresh Lua 5.4.7 state for every race/retry/replay. Its main chunk
also runs at startup for validation. Define hooks as global functions:

| Hook | Event |
|---|---|
| `on_race_start(race)` | First racing tick (GO). |
| `on_tick(race)` | Every simulation tick, including countdown. |
| `on_lap(kart, lap)` | Completed lap, including the finishing lap. |
| `on_item_use(kart, item)` | Human Item press or CPU activation decision. |
| `on_finish(kart, place)` | Kart finish flag rises. |
| `on_race_end(race)` | Race reaches its shared completion condition. |

Within a tick: race start, laps in kart order, finishes, race end, then tick.
Item use occurs inside the update. Events run in mod order, never in rendering.
Item hooks return `nil`/`true` to keep, an item name to replace, or `false` to
cancel while retaining the item. Replacements pass to later mods; cancellation
stops the chain. Outside Battle CPU replacements support single shells,
banana, mushroom, star, Boo and lightning; unsupported replacements keep the
CPU item. Source CPU triple selections may already be transformed by the hook.
Time Trial can keep/cancel mushrooms; replacement means keep and giving items
is disabled. Cancelled TT presses remain masked until release for ghost parity.

The global `race` table also accompanies race hooks. Fields: `tick`, `phase`
(`countdown`, `racing`, `finished`), `laps`, catalog `course`, `mode` (`gp`,
`quick_run`, `battle`, `time_trial`, `vs`), `class` (50/100/150) and `karts`.
Local multiplayer has one world tick and marks every human kart `player=true`.
Functions are available inside hooks:

| Function | Contract |
|---|---|
| `race.random(n)` / `race.random(lo, hi)` | Inclusive bounds; private deterministic stream per mod, independent of game RNG. |
| `race.message(text, ticks)` | Latest HUD message wins; default 120 ticks, range 0–600, 0 clears. Uppercase, 48 characters, unsupported glyphs become `?`. |
| `race.kart(i)` | 1-based snapshot or `nil`. |
| `race.give_item(i, name)` | True only for an eligible human empty slot; false during item-use hooks, roulette, finish or Time Trial. |

Kart fields: `index`, `player`, integer world `x`/`y`, `speed` in units/tick,
`item` or nil, completed `lap`, 1-based `place`, `finished` and driver class
`driver`. These are snapshots, not writable simulation handles. `items.json`
lists names/IDs; a partly used triple mushroom can read `double_mushroom`.

The sandbox provides restricted base, string, table and math libraries, with
`print` tagged by mod ID. No OS/files, module loading, debug library, arbitrary
chunk loading or `math.random`. Limits per mod: 4 MiB, 1,000,000 instructions
for the main chunk and 100,000 per hook. Errors/budget failures disable that
script for the race and leave other mods running. C-library pattern calls are
not instruction-counted; avoid pathological patterns. Do not derive outcomes
from `pairs` iteration over address-valued keys. API implementation and limits:
`platform/script_host.[ch]`, `app/race_script_hooks.h`.

## Data, tracks and drivers

The optional 2D Drift Extras reward rules preserve original handling; they are separate from modern 3D physics. Copy `portable/data/modern-drift.json` to a mod folder as `modern-drift.json`, edit it, then use the normal mod-kit build/install flow. The kit compiles a validated deterministic `MDR1` section (kind95, id1, version1). Three ascending charge thresholds, boost durations, speed-cap ratios and accelerations are fixed-tick data; ratios use 256 as 1× and accelerations use Q16. Off ignores this optional rules section. Drift Tiers composes its built-in profile before user mods, so later enabled mods override it. Change the profile revision when changing behavior; its payload fingerprint separates save filenames, and the full rules/content identity rejects incompatible ghosts and replays.

`data.json` contains partial tables; omitted fields retain earlier values.
Tables include engine speeds, kart physics, player/CPU item selections, item
lifetimes/trajectories, GP awards/retries, stock cup order, graphic labels and
static strings. IDs/ranges and editable stock shapes come from `export-base`;
`tools/portable/game_data_mod.py` and native `platform/game_data.[ch]` define
validation. Percentage modifiers compose in load order with integer truncation.
A minimal example changes GP awards:

```json
{"gp_points":{"id":1,"awards":[9,6,3,1,0,0,0,0]}}
```

Track folders use `track.json` for ID/name/music/laps/start/finish/route and
`tileset.json` for materials. `tileset.png` holds indexed 8×8 tiles;
`tilemap.png` is 8-bit greyscale with one tile ID per pixel. World points use
8 units per tile. Choose a template with empty `template_problems` in the export.
Custom course IDs are 128–191; names fit 12 menu glyphs and TT laps are 1–5.
GP needs four tracks per cup. Add folders to `cup.json`; custom cups appear
after GBA/Extra and are always unlocked without awarding stock medals.

The optional 3D Race Renderer uses the same full tilemap, indexed tiles,
ambient palettes and actor art; existing track/driver mods need no conversion.
Palette index zero remains transparent. It renders a flat course and source
billboards, so artwork resolution still depends on the authored images and
Artwork Detail selection. Unsupported camera projections visibly fall back
to Original; invalid map/tile references remain loading/render errors.

After changing the road, update the route and remove derived `raster.png`,
`minimap.png`, `ai_rows`, `target_table`, `ai_sentinels` and `grid` so the compiler
rebuilds them. The final route segment must cross the finish northbound once.
Object alias tiles 0xD0–0xDF are forbidden; shared tiles 0xE0–0xFF and palette
entries 48–63 must match the template. Named materials include road, offroad,
wall, boost, jump, pit, lava and water. Water uses ordinary checkpoint recovery.

Optional `sky-tiles.png`/`sky-palette.png` override source sky indices/colors;
`sky-motion.json` edits parallax/layer/controller profiles. `ambient.bin` holds
palette/tile animation. Missing optional files retain the template. Lower-level
`tools/portable/track_format.py export PACK COURSE DIR` and `compile DIR --base
PACK --output OUT` round-trip stock tracks; the format modules and exported
folders supply exact schemas without hand-editing binary offsets.

Custom drivers need character, graphics, motion and roster sections. The roster
selects a stock seat and voice donor; separate menu/HUD/board art supplies the
new driver's identity. `mods/examples/shade-driver/build.py PACK --output-dir
DIR/shade-driver` demonstrates selectable Shade with its own portrait/name,
Luigi voice and modified physics. Its CLI exposes look, seat, voice, name, ID
and speed. Add `--full-resolution-only` to keep all full-size poses without
32/16-pixel alternatives; Artwork Detail safely uses the available full-size art.
Missing driver artwork falls back to the seat driver's art. Exact section
layouts live in `assets/content_registry.[ch]` and the example builder.

Artwork Detail selects existing authored LOD alternatives by physical footprint;
it keeps the source geometry, anchors, palettes and pose. High favors additional
detail and Max selects the highest available resolution. Sparse driver artwork
retains its valid fallback. Single-bank items and effect scripts keep their art;
animation frames are never interpreted as alternate resolutions. This setting
does not change content identity, saves or replay compatibility.

Optional Wide UI Backgrounds extends existing menu pattern/affine assets. Flat
custom backgrounds stay centered. Title mods may provide kind-15 image pairs:
sky `0x20900 + 2*variant`, clouds `0x20901 + 2*variant` (0 normal, 1 sunset,
2 night). Sky is 256×256; night may use eight vertically stacked 256×256
palette rows, advanced every nine title ticks. Clouds are 256×512 with alpha.
Coordinates continue the centered title scene; source sky y=80 and cloud y=96,
cloud x advances one texel every eight ticks. Logo, drivers and flags remain in
the original central images. Supply both layers; malformed pairs are errors.
A flat title override disables stock companion wings for the whole variant,
so animations never alternate between custom centers and stock surroundings.

Drift Tiers + Air Boost (historically Modern Air) uses kind-95 version 2 rules in `portable/data/modern-air.json`.
A mod's `modern-drift.json` may retain the version-1 drift fields and add the
version-2 `air` object: minimum air ticks, trick window, boost ticks, cap and
minimum-speed ratios, acceleration and launch mask (1 ramps/protected launches,
2 ordinary hops, 3 both). The strict format tool validates bounds. Off
ignores this optional section; Drift Tiers + Air Boost refuses overrides that remove its air
rules. Drift Tiers retains its version-1 defaults and replay format. Published JSON names, IDs and fingerprints are unchanged.

## Mesh courses and modern physics

Mods → **Course Workshop** opens eight local create/open slots. D-pad/arrows move a 4m snapped cursor, A/Enter places, L/right-click picks a piece to move, R rotates, Select/Tab opens the piece wheel and Start/Space opens Actions (save, test drive, delete, undo/redo, height, projects). Mouse clicks place; wheel zooms; Ctrl+S saves, Ctrl+Z/Y undo/redo, PageUp/Down change height and F6 tests. Saves are ordinary `workshop-N` folder mods with C3D1 geometry and editable metadata; enable them in Mods for normal play. Test drive uses the shared Time Trial stack (three mushrooms, no random boxes); Change course then B returns to the editor. Gate0 is the finish; subsequent gates follow placement order. One start, two gates and road are required to save/test. This first slice edits Workshop courses, with fixed road/ramp/wall pieces and solid-colour decorations; arbitrary mesh import, splines, resizing, CPU routes and other modes remain future work. Mesh courses provide real height and fixed-point modern physics. Enable a course in Mods; its cups follow the 2D sets on the ordinary course screen (L/R), grouped by mod. Confirm drives the selected mode, Select explores; a nonzero physics profile is required. VS admits mode bit 8 and needs distinct starts for every human.
Battle admits mode bit16, laps0/empty gates and four distinct GRID starts; CPU seats need a closed patrol route and ITEM placements. Two to four humans share pause and standings, Retry and Change course, with no TT record/ghost writes. Start explores without driving. Profile zero is preview-only.
Time Trial uses the saved keyboard/controller bindings; Start pauses, with Retry and Change course actions. Finish saves a separate 3D personal best and best-total ghost; results offer Retry and View replay. A saved ghost races beside the player on the next attempt. These saves never replace
recovered 2D records. In Explore, directions move/turn, shoulders lower/raise, Confirm/Select look up/down, Start resets the camera and Back returns to the browser, then to 2D courses. Classic course screens expose the same shared browser.

Convert owner-imported courses into a separate 3D-only mod (the base pack stays unchanged):
```sh
python3 tools/portable/convert_course_3d.py --base PACK --course 24 --course 25 --source .mksc-tools/portable/runs/t4/converted-source --output .mksc-tools/portable/runs/t4/mods
```
Use `--all` for the full catalog, repeated `--course ID` for a subset or `--track FOLDER` for editable 2D folders compiled against `--base`. Enable **Converted 3D Courses** in Mods, then pick its cups (L/R) on the course screen in the desired mode. GP is offered only for complete four-course cups. Routes that cannot yet be adapted fail with the course, row and edge. Unsupported SCN constructors fail with the course, source code and placement instead of becoming static decorations; source-proven inert constructors remain static. Automatic conversion remains in development.
The generated glTF, PNGs and course.json use 0.1 metres per source unit (`--scale` changes this); physics.json and items.json are editable modern profiles. The collider adapts to five source units wide so narrow native lanes remain usable; other modern handling parameters stay editable. Sources must go into a fresh empty directory. Rebuild with the normal kit after editing; never distribute owner-derived artwork. For a reusable elevation hand pass: first add `--export-handpass .mksc-tools/portable/runs/t5/authored` to conversion (select course24 for Peach Circuit). Next import `authored/24/scene.gltf` into Blender, edit the named **Elevation** mesh in its existing X/Y/-sourceY metre space, and export static glTF back to that path with transforms applied or retained; Y is additive height. Keep a flat zero rim around each patch and no holes or overlapping projected faces; sampled terrain remains single-valued. Finally reconvert to fresh source/output folders with `--handpass authored/handpass.json`; it checks course-content SHA256 and scale, then lifts terrain, collision, ground anchors and navigation together. Navigation retains original waypoints and reduces intervening samples within a1/64m height bound. Retain this authored folder separately from every generated import. The supplied `mods/handpasses/peach-rise/handpass.json` instead authors a gentle1.2m rise on the return straight: pass it with `--course 24 --handpass mods/handpasses/peach-rise/handpass.json`. Unmodified/all-zero guides preserve original conversion bytes. For static tunnels/scenery, a course row may also declare `mesh_additions`: ordered entries with `scene`, named `render_nodes`, `collision_nodes` (`node`, local `surface` index, optional existing `respawn` index) and ordinary course `surfaces`. An optional `ground_anchor:[x,z]` translates the whole addition rigidly by the sampled elevation; omit it for world-space authoring. Export static glTF/GLB with the ordinary course material/PNG rules; existing terrain, routes and gameplay indices stay intact. Additions may overlap vertically but never implicitly replace ground or move recovery anchors. The supplied `mods/handpasses/mushroom-cup/handpass.json` selects four guides and a tunnel; convert all four with `--course 24 --course 25 --course 29 --course 27 --handpass mods/handpasses/mushroom-cup/handpass.json`, then enable the generated mod for TT or its complete GP cup. Flower Cup uses `mods/handpasses/flower-cup/handpass.json` with `--course 32 --course 37 --course 38 --course 31`; its four separate Elevation GLBs keep flat crossgrades over the retained route lanes and zero rims around the authored rises. Lightning Cup follows the same workflow with `mods/handpasses/lightning-cup/handpass.json` and `--course 28 --course 40 --course 33 --course 26`; the four rises keep source jump and hazard corridors intact. Star Cup uses `mods/handpasses/star-cup/handpass.json` with `--course 36 --course 34 --course 30 --course 35`; its distinct rises leave launch approaches, flight/landing corridors and hazard footprints unchanged.
Source material bytes remain in surface properties; jump/boost/recovery use explicit modern adaptations. Optional fixed `mksc:jump-max-distance` in `(0,1024]` metres caps jump impulse to the nominal flat-floor flight range using actual horizontal velocity and world gravity; it applies only to jump surfaces with gravity ratio 1, never increases launch or changes horizontal speed, and leaves zero-speed/gravity behavior unchanged. Surface type 9 is a cannon trigger: never solid, touching it flies the kart (input, collision and clocks paused) at `launch` m/s along a chord to vector `mksc:cannon-target` (floor point, metres) under an arc a quarter of its length high (2..64 m), leaving on horizontal vector `mksc:cannon-heading` at engine speed; both properties are required. ZONE kind 1 is an MK8 gravity-path point: its gravity vector's negation is the path's up, its largest half-extent the capture radius, integer `mksc:gravity-next` the next kind-1 zone and boolean `mksc:anti-gravity` drives the segment that starts there on the anti-gravity rows with spin bumps; a kart takes gravity from the nearest segment it projects onto, else world gravity (`bridge-course/build.py --anti-gravity` authors a loop). Hazard objects with boolean `mksc:spin-bumper` push karts out and spin anti-gravity ones into a boost instead of the half-speed knock. String `mksc:hazard-reaction` picks what touching any other hazard does after its half-speed knock: `bump` (default) nothing more, `spin` spins the kart out for the item rules' hit time, `tumble` also throws it up 4 m/s and rolls it once, `squash` stops it and flattens it (META fixed `mksc:squash-width`/`mksc:squash-height`, default 1.35/0.3, shape it at full flatten); the last three need the course's item rules and respect Star and hit grace. ZONE kind 2 is an MK8 glide-path point (up, radius and chain as kind 1, via integer `mksc:glide-next`; boolean `mksc:glide-cannon` makes the segment from it a cannon path); boolean SURF `mksc:glide` marks a glide ramp: a kart leaving it opens its glider over the nearest glide segment after 15 airborne ticks (5 on a cannon) when facing along it, steers at half rate, dives with Hop, pulls up with Brake and lands on ground contact after 20 ticks or 100 MK8 units outside the path's radius (`bridge-course/build.py --glider` authors a kicker over a drop). Point a glide path's end below the landing floor so its tangent descends onto it. Boolean `mksc:trick` marks a trick ramp (absent: every type-5 jump surface is one); a hop on it, or up to 7 ticks after leaving it, at 0.65 of top speed plays a trick that boosts on a landing 20+ ticks later. Boolean `mksc:one-sided` makes a surface's faces pass karts from behind, as DS KCL does. Integer `mksc:surface-class` picks the MK8 ground rules: 1 road (keeps `speed_ratio`), 2/3/4 dirt/grass/heavy off-road (speed 0.7/0.5/0.3, friction 1/0.98/0.97, steering 0.9/0.8/0.7), 5..7 slippery and 8..10 ice at levels 0..2 (speed and friction rise with the kart's off-road traction stat); absent derives it (type 1 by speed ratio, type 2 by friction, else road). Sideways slip keeps friction×T a tick, T easing 0.005 a tick toward the kart's traction row for that ground. Boolean `mksc:surface-gravity` (loops, wall rides) turns gravity and floor tests toward the face a kart stands on; it stays in force while airborne and any ordinary floor restores world gravity, so loop floors must curve gradually within the ground-normal cutoff. Proved sequential jump chains retain source route anchors and either add aligned approach guides or use the retained approach tangents; their landing caps target authored route speeds, excluding optional boost overspeed. Slightly curved isolated launches can instead use the declared single-strip speed envelope and conservative incoming-ray tube deadline. Incompatible strip-wide caps may split across disjoint takeoff footprints; overlapping body/phase footprints still require one cap proved for every approach. Aligned route edits publish atomically; retained-tangent caps can publish independently per launch component only when all of its approaches and contact groups pass. Unproved components retain authored launches; unproved aligned chains also retain authored routes. Both still require real-race checking. Recovery preserves checkpoint authority and reserves a three-metre dry run-up before launches. Source height-permitted progress retains finite logical raster fronts across ordinary floor, air gaps and walls without changing physical collision; finite alternatives and connected body-clear sections behind wall combs retain ordered stages while recovery stays on safe ground. A local consecutive named/unnamed raster corner can acquire ordered stages at the same finite destination front under source height permissions, independently of collision and safe recovery. Grounded recovery anchors accept parallel apertures only with matching stage, key group and successor sets; recovery cannot switch branch policy. Automatic panel/launch reach includes corner cuts using contact-segment distance and finalized finite jump caps (uncapped launches alone preserve authored arrival disks); reachable CPU waypoints get wider arrival disks only where ordinary terrain and neighboring route geometry allow; inserted launch guides and item routes stay narrow. Changing the generated route, scale or physics requires checking jumps and boosted turns again in the actual race. Converted crabs declare versioned motion/contact controllers with four anchored model frames; snowmen use two anchored frames and the original two-stage break/blink sequence, with a modern kart bump instead of spin damage. Penguins retain six anchored frames, ordinary spin hits and protected flip/sine flight. Mice retain two anchored frames, walking/turning or ballistic hopping by variant, and normal spin/protected flight contacts. Piranhas use five anchored bite frames, inverse-affine sizing and hidden collision; touch does not spin or remove Battle balloons. Boolean SURF `mksc:capture` marks supported swallow ground (ordinary when no collidable plant qualifies); positive META fixed `mksc:capture-forward-speed` and `mksc:capture-up-speed` in metres/second (at most 256) select the modern launch after the 92-turn pull/hidden hold. Seagulls use three anchored frames, harmless40-update flutter, protected ballistic removal and per-camera fixed-heading reflection; their contacts do not change kart velocity, grace or Battle balloons. Lava bubbles use variants0..12/two anchored frames, source emergence clocks, ordinary spin/burn contacts and protected source-component ballistic flight. Their250-turn burn selects imported palette5/6 or dark/pale authored-model tint; Star presentation has priority and rescue clears burn. Thwomps use variants0..11/one anchored frame, source patrol/drop clocks, ordinary rebound or128-turn squash, and protected ballistic flight. Their META ASSET81 `mksc:actor-affine-rules` selects an explicit AFR1 profile; authored actor-affine.json contains version1/profiles with profile_id and tables of modes1,4,5,6,7, each with1..32 rows `[inverse_x_q8,inverse_y_q8,duration]` (positive16-bit coefficients, duration1..127). The converter embeds the imported profile, while the ROM-free example supplies original curves. Cannonballs use variants0..3/seven independently anchored frames and source inverse-affine falling/explosion animation; only the ball collides, while Star never removes it. Strong hits keep a separate grounded source angular recovery with MIR-driven radial knockback in the support plane, uniformly bounded at the modern SI velocity limit. Source launch/explosion sounds use live-human spatial attenuation; launch audio plays only when its player is idle. Boos use variants0/1/two opaque independently anchored camera-selected cells and an exact64-turn bob; contact is harmless and requests one imported palette7 turn (cool tint for authored models/one-bank art), deferred under Star/burn and cleared by rescue. Rocks use variants0..3/five anchored frames, optional initial source:affine-q8 in1..256 (default256), and their source fall/impact/two-bounce/rest cycle. Source scale alone belongs on the model; controller inverse-affine sizing applies at runtime. Normal contacts share burn250/weak spin; Star protects the kart without changing the rock. These families declare a generated source-actors.json profile and run with actor-aware v3 ghosts. Opt-in META integer `mksc:actor-ground-follow`1 makes grounded crabs, penguins, mice and Mechakoopas follow connected upward mesh floors. Initial foot anchors must be on or at most1m above their intended floor (two Q16 LSBs tolerate encoding); that clearance is retained. Floor changes follow exact shared edges/subedges, including split edges; disconnected or ambiguous overlapping interiors fail with a named diagnostic. At most512 floor rows may intersect an initial-to-current trace; gaps, unsupported overlaps and exhausted bounds reject the turn. Other surface types do not invent NPC hazard rules. Source flight/hops freeze the accepted floor offset, and render/contact share that pose. Absent metadata preserves existing compiled mods and v3 ghosts; enabling it changes content identity and adds explicit actor checksum fields. `bridge-course/build.py --items --crabs --ground-follow` authors a crab walking down/up the existing ramp; add `--cup` for GP. Converted interactive source families are complete; clock-driven skies and progress/palette controllers continue in T4B1/T4B. Add `--crabs --items`, `--snowmen --items`, `--penguins --items`, `--mice --items`, `--piranhas --items`, `--seagulls --items`, `--lava-bubbles --items`, `--thwomps --items`, `--cannonballs --items`, `--boos --items`, `--rocks --items` or combine them to the ROM-free bridge example below to author contact hazards without game assets.

Build the original ROM-free bridge/ramp/tunnel example:

```sh
python3 mods/examples/bridge-course/build.py --output-dir .mksc-tools/portable/runs/t1/mods
mksc PACK --mods-dir .mksc-tools/portable/runs/t1/mods
```

The generator also creates editable sources beside the output mods directory; `--battle` instead builds an enclosed ROM-free arena with red-shell box odds for combat, balloon loss, revenge Bombs and rematches.
Edit `courses3d/bridge/course.json`, its static Blender-compatible `scene.gltf`
and local PNG/buffer files, then use `mod_kit.py build SOURCE --output MODS`.
Copy `portable/data/mesh-kart.json` into the mod source as `physics.json` to author
kind-102 KPH2/v2 profiles (MK8's 13-stat model). `profile_id` must match the course's `meta.physics_profile`;
change `revision` when changing physics. `ranges` give each stat-driven value at stat level 0 and 19; `base_levels_q16` (0–19), stock `driver_points_q16` "1".."8" and DRV3/KRT3 ratios (each 0.1 above 1 adds two levels) pick a racer's level, `class_speed_ratio_q16` scales ground speed for 50/100/150cc, and `cpu_speed_ratio_q16` (0.5–1) is the share of their top speed and planned driving speeds CPU racers keep at each class. Fields ending in `_q16` store signed fixed-point integers (65536 means 1);
speeds/accelerations use SI units, turn rates use turns/second, and durations
and charge thresholds use fixed ticks. Each `handling_*` triple is MK8's normal and drift turn rate in radians a tick at 7+ units/frame (the curve and drift spring are MK8's) and the spin-turn gain into its ±30 hop-less drift accumulator. `_q30` unit coefficients use 1073741824 as 1. Engine constants (coast ×0.98, brake, reverse) are MK8's and not authored.
The compiler rejects invalid ranges and cross-field combinations. Newly compiled courses default to MK8 gravity28.1635m/s²; explicit `meta.gravity` overrides it. Probe/snap are gaps below the body foot, independent of radius/ride height; the default profile uses0.10m/0.01m. Rebuild/reinstall existing mod ZIPs to adopt new defaults. Course/profile revision/content digest identify records and ghosts.
Drift Extras in Options affects only 2D courses; mesh courses always use authored profiles.
CPU routes need connected, nonzero-length closed edges, authored successor order and a positive braking or coasting rate. Node radii bound arrival/recovery; node speed is m/s (zero uses the profile cap).
Optional `mksc:cpu-ground-route` boolean requires a horizontal route and projects airborne arrival/loss checks onto that plane; acquisition and grounded checks retain height. `mksc:cpu-lookahead` is fixed Q16 metres (default 2, range 0.05–64), the minimum pursuit distance; the CPU steers toward a point 0.35 s of travel ahead and cuts a bend only inside that node's arrival radius. Bends are taken at the profile's yaw rate around the tightest arc tangent to both legs at their midpoints; `mksc:cpu-corner-speed` is fixed Q16 m/s (default 3, range 0.05–256), the floor below which no bend slows the CPU.
`mksc:cpu-recovery-ticks` is an integer (default 30, range 1–600). Slowing uses the stronger authored braking/coasting rate for anticipation and input. Above the current node speed, steering adds response/yaw-lag preview for the excess speed, capped at64m; ordinary steering and the speed-preview distance keep the authored lookahead. Zero response or yaw authority disables the extension.
Battle uses four total racers, imported rivals for empty seats, three balloons and source revenge-Bomb roles. CPUs seek available boxes and eligible rivals using authored item ranges; Bomb retrieval returns to each GRID. Quick Run uses one human and seven imported roster rivals. It freezes on the human finish and labels unfinished rivals' times Estimated; Retry resets the full field.
`physics.json` may remain one profile or use `{"version":1,"profiles":[PROFILE,...]}` with 1–256 unique IDs; a single profile compiles to the same bytes.
Optional META asset properties `mksc:physics-50cc`, `mksc:physics-100cc`, `mksc:physics-150cc` name kind 102 profile IDs. Missing mappings use the base; a mapped profile still applies its own class ratio; invalid declared profiles reject the mod before publication.
TT always uses the base physics profile and 44-word kart state; item profiles select replay v2 with separate item checksums. Base physics profile zero stays preview-only. Build with `--class-profiles` to try authored 12/18/24 m/s caps.
Menus group 3D courses by cup ID within the mod that provides them, rows by cup_order (at most eight per row), named by META cup name. GP needs exactly four GP courses with one full-width cup ID and cup_order 0–3, eight starts/CPU routes and imported scoring. Top-four finishes earn points; Retry spends a credit. Previous finish order sets the next grid; completed cups save separately under mesh3d, without 2D unlocks or quality stars.
Build with `--cup` for four authored bridge colour variants and slower editable CPU corner targets. Results → Standings → Next race reaches the podium after race four; Start pauses. META music references kind 16 audio sequences; absence means no background music, while race cues still play. Mesh courses may instead name kind 112, a self-contained DS song (`MKSCNSQ1`: SSEQ commands plus its program, region and PCM8/PCM16/ADPCM/PSG wave tables; layout in `audio/nds_sequence.h`), content IDs 1..32767; the final lap speeds it up like a stock theme. Version 2 adds a start offset so one entry of a sound-effect archive keeps its shared jumps. META asset `mksc:sound-set` names a kind-114 `MKSCSND1` set (layout in `assets/sound_set_asset.h`) the course races with: rows replace stock cues by song ID with any kind-82 or kind-112 song (0 silences) on the stock cue's player, add engine idle/coast/throttle, drift and mini-turbo spark cues the stock sounds lack, a top-speed engine pitch range, and per-driver boost and struck voices for human racers; version 2 adds an engine pitch at rest and a per-driver engine song; version 3 adds a final-lap music tempo, floor cues (`0xfc00`/`0xfc40`/`0xfc80` | class: drift, landing and a held driving sound on floors whose SURF integer `mksc:sound-surface` is that class, 1..63) and per-driver voices for the rocket start, heavy hits, hitting a rival, gaining a place, the finish (first, podium, off it) and battle win/lose. A kind-112 song's header may name a `follow` song that the same player starts when it ends (a goal fanfare into its results music). Kind-3 ZONE rows are positional sound emitters: integer `mksc:sound` (a sound-set song ID), optional `mksc:sound-period` (restart ticks; 0 when it ends), audible within the zone's largest half-extent; the nearest one in range plays, louder as the kart nears it. META asset `mksc:fx-set` names a kind-116 `FXS1` effect set (layout in `assets/fx_set_asset.h`): kart shadow decals keyed by KRT3 kart ID, driver ID or 0 for any racer (a multiply texture with half extents, forward offset and vanish height in kart-scale-1 metres; flag 1 keeps a shadow volume's full size until it vanishes instead of shrinking), up to eight particle styles (glow, spark, smoke, flame, dust, splash, mist, ember: a kind-97 flipbook strip, additive or alpha, tinted or coloured; flag 4 on the spark style holds one spark at the drift's outer rear tyre, renewed every six ticks, instead of flying sparks and glows), drift tier, smoke, dust and flame colours, and emitter rows. A set with styles replaces the generated drift, boost, dust and landing effects; a set with only shadows keeps them. Kind-4 ZONE rows are placed particle emitters: integer `mksc:emitter` names an emitter row, which runs at the zone's position and orientation (local x right, y forward, z up) on every accepted tick, scaled like the karts. SURF boolean `mksc:splash` makes wheels throw the set's splash style instead of dust. All of it is presentation only. Kind 115 (`MKSCN64S`, layout in `audio/n64_sequence.h`) is a self-contained N64 song: an M64 sequence with its instrument bank and VADPCM samples; META music may name it (content IDs 1..8191) and sound sets reference it as `0x4000 | id`, or one effect of the effect sequence (content ID 1) as `0x6000 | bank << 8 | sound`.
Build `--items --coins --pier-boos` for a contact-triggered, victim-targeted haunt; `--pier-boos-star` adds protected GP setup. `--pier-boos-repel` authors a GP fixture for approaching haunt → held-Star escape. Required `pier_boos`/version1 class0x1008 declares variant0/source x/y-q16/checkpoint/order, optional inverse affine1..256 (default256), and three dense ASSET96 refs: prop plus two centered mirrored haunt cells. BOO1 kind62/id1 supplies haunt parameters; optional META `mksc:actor-visual-seed` defaults0. Contact vanishes the prop without theft/spin; approach then assigns the data lock, source orbit drives music and post-pickup coin pulses, and Star repels it. Build `--items --coins --kamek` for a two-cell bobbing patrol; `--kamek-star` adds protected GP setup. Required `kamek`/version1 class0x101b declares variant0, source x/y-q16/checkpoint/order, optional inverse affine1..256 (default224) and two dense ASSET96 frame bindings under MSA1. Ordinary touches are harmless with8-turn grace and no cue; Star emits160 and starts source upward flight/gravity with inverted art until hidden. Build `--items --coins --mechakoopas` for moving two-variant Mechakoopas; `--mechakoopas-star` selects protected GP setup. Required `mechakoopas`/version1 class0x101c declares variant0/1, source x/y-q16/checkpoint/order, optional initial affine1..256 (default224) and four dense ASSET96 `source:frame:N` bindings under MSA1. The352-turn patrol uses6/10-turn stride and per-camera front/back art. Ordinary human contact uses weak spin, autonomous contact uses strong recovery, and Star triggers33-update inverted flight; no fresh rebound, burn or shrink is added. Build `--items --coins --common-props` for six source prop classes; `--common-props-star` adds protected GP setup. Required `common_props`/version1 class0x1019 retains integer `source:code` (classes06/08/0A/0C/86/98), source x/y-q16/checkpoint/order, optional affine1..256 and one dense `source:frame:N` ASSET96 binding (umbrella86 uses two) under MSA1. Ordinary contact rebounds without spin or balloon damage; umbrellas toggle art with375/376 cues. Star sends props through33 source sine-flight updates. Model scale contains only source metres-per-unit; runtime applies inverse affine. Build `--items --coins --thunderclouds` for source warning flashes, shared palette animation and lightning; `--thunderclouds-star` adds protected GP setup. Required `thunderclouds`/version1 class0x1010 declares source x/y-q16, variant0..3, checkpoint/order and72 dense `source:frame:N` ASSET96 bindings under MSA1. Frames are shared phase0..8 times eight art cells; odd variants read their bank without changing it. Normal contact uses strong recovery and overwrites shrink with360 after movement (359 next turn); Star leaves the cloud intact.
Build `--items --coins --falling-stars` for five-frame falling-star descent, bounce and dissolve; `--falling-stars-star` adds protected GP setup. Required `falling_stars`/version1 class0x1011 declares source x/y-q16, variant0..5, checkpoint/order and five `source:frame:N` ASSET96 bindings under MSA1. Source affine and clockwise art rotation are rendered per camera; normal contact uses strong recovery, Star leaves the actor intact.
Build `--items --coins --teepees` for separate four-frame tent debris and seven-view Shy Guy riders; add `--teepees-star` for protected spawn. Controller `teepees`/version1 uses class0x1018, source variant0/checkpoint/order, optional affine1..256 and eleven dense `source:frame:N` ASSET96 references with MSA1. Touching bursts a tent without spin/balloon damage; the rider writes shared lock360, fresh accelerator presses subtract10, Star/strong recovery releases it and shared coin parity owns drain. Courses without teepees retain their previous lock behavior.
Optional `skies.json` (`version:1`, `profiles:[...]`) compiles bounded kind106 MSK1 motion profiles for 3D targets; fields are in `mesh_sky_format.py`, with the wire design in ARCHITECTURE.md. Controllers0..7 are admitted; controller4 also requires META ASSET107 `mksc:palette-motion`. Optional `palettes.json` version1/profiles declares `profile_id`, `revision`,64-word RGB555 `road_seed`/`sky_seed`, relative AMB1 `ambient` and sorted `bindings` with kind97 material or101 texture, positive `id`, bank0 road/1 sky, `width`, `height` and a relative byte-plane `indices` path (local0..63 or255 transparent). Seeds must match each bound immutable image; mismatched overrides fail by profile and asset. Up to128 planes/4MiB and512 palette pairs are admitted, with per-image canonical palette caches bounded at2048 textures/64MiB and2048 material wrappers sharing geometry. One PRE sunset lane and an optional POST timer preserve source ordering; split layers partition each pane at logical source rows. Controllers2/4/5 require integer META `source:progress-stage-count`1..1024 and one integer `source:progress-raw:N`0..127 per dense checkpoint stage (stage0 is0; values never decrease). The converter emits this provenance automatically for progress-driven profiles; parallel checkpoint rows share their stage value. Each racer owns accepted progress and motion; controllers4/5 read the prior-standing leader; sunset palettes are shared from the primary owner. Source2 layers use front0×/middle2×/back1× camera yaw; pause/results freeze the owner and Retry resets it. Tag each ordinary model with integer `mksc:sky-layer`0..2; its local U origin faces -Y. GPU/GLES camera-center the cylinder, preserve authored Z as a camera offset, repeat horizontal motion and translate cropped geometry vertically without V smearing. `bridge-course/build.py --sky-motion` demonstrates a ROM-free animated panorama, pause and sky-only v3 replay. Optional `coins.json` (`version:1`, `profiles:[...]`) compiles kind105 MCR1 tables: `profile_id`, `revision`, `modes`, `cap`, `initial`,100 `speed_ratios_q16` and `losses` keyed hit/rescue/contact/crush/haunt. META asset `mksc:coin-rules` binds the table; the shared MIR item/status owner is required. OBJS controller `coins`/version1 uses class0x20000001, ordinary model96, positive `coin:id` and fixed `coin:half-x/y/z` pickup extents. Duplicate IDs require identical art/geometry and disappear together. Build `--items --coins` for authored opt-in GP/TT/Quick/VS/Battle pickups, count HUD, loss and speed caps; converted source courses enable GP/Quick/VS only. Defaults are cap99/initial5 with a modern `.90+.01*min(count,10)` cap ratio; this adaptation changes neither recovered 2D physics nor acceleration. Retry resets availability/counts; v3 ghosts retain the shared coin state.
Optional `items.json` (`version:1`, `profiles:[...]`) compiles SI Q16/tick MIR1 profiles (kind 103); META asset `mksc:item-rules` selects one, and optional `mksc:item-rules-vs` replaces it in Quick Run and VS. Weights may award the Super Circuit items 0x1000-0x100c and the double mushroom 0x100f, plus fake box 0x1010, Bob-omb 0x1011, triple bananas 0x1012, Bullet Bill 0x1013, Blooper 0x1014 and banana bunch 0x1015 (five trailing bananas). Fields and bounds are in `core/mesh_item_rules.h` and `tools/portable/mesh_item_format.py`; player/CPU rows select by rank, with no pixel-gap conversion. Build the bridge example with `--items` for terrain-aware GP/Quick items and TT's three mushrooms, with global imported HUD/actor art. TT has no random boxes and saves version2 item-aware ghosts. Item-enabled VS shares one terrain item world with per-player HUDs and per-camera Boo visibility. Shared Boo/teepee slowdown permits retained items; Star clears the slowdown. Ghost layouts stay unchanged; older teepee ghosts that exercised the corrected use rule stop at checksum divergence without partial state, while unaffected v1–v3 streams remain readable. Combine `--cup --items` for shared player/CPU rank-based odds and 30-tick box respawns; the non-cup item showcase keeps equal odds and 180-tick respawns. Moving collision, referenced gravity zones and section/shortcut checkpoint layouts are named unsupported until later 3D slices implement them.
No base pack is needed to compile this example. The player still supplies the base game pack for the shared menus. Generated artifacts stay out of git.

For both course formats, run `python3 mods/examples/dual-course/build.py --base PACK --output-dir .mksc-tools/portable/runs/t6/mods`. It authors the same oval as custom2D/3D course128 in one dual-target ZIP; the committed layout is original, but generation borrows shared artwork/rules from the owner pack, so keep its outputs local. Install the ZIP in Mods (it turns on); select the Custom track set for 2D or its own 3D cup row for the mesh companion. Editable source remains in `.mksc-tools/portable/runs/t6/source/dual-course`: edit `tracks/oval` or `courses3d/128`, then rebuild with `python3 tools/portable/mod_kit.py build SOURCE --base PACK --output MODS`. The builder requires a fresh source path to protect edits; ordinary kit rebuilds reuse it. Dual-target JSON inherits separately from each domain, so earlier2D-only overrides cannot leak into3D.
Manifest `targets` is an optional array: `["2d"]` (legacy default), `["3d"]`
or `["2d","3d"]`. A 3D-only mod has its own content/script identity and never
changes recovered 2D records or ghosts. In a dual mod, older section kinds and
its script deliberately contribute to both domains; exclusively 3D images use
kind 101. Course IDs and cup IDs are full uint32 values, separate from 2D IDs.
The compiler validates node selection/transforms, texture IDs, triangle indices,
collision surfaces, checkpoints/routes, grids, placements and property bounds.
The runtime reads only endian-explicit C3D1/C3C1 pack sections, never glTF/JSON.
The full table contract, limits and later-course feature audit are in [Architecture](ARCHITECTURE.md); loops, arbitrary-gravity driving, moving collision and section finishes remain future work, with explicit admission errors.
Unknown required features fail with a course/field diagnostic. Vertex colours (VCOL), alpha blend, `mksc:sky` and TXAN texture animation render on GPU/GLES, and kind-108 bundles named by META `mksc:texture-bundle` pack many textures per section (Architecture has the wire); META `preview_id` (true-colour texture) is the selector's course picture, and `minimap_id` is HUD art whose edges span the world XY rectangle of META vectors `mksc:minimap-low`/`mksc:minimap-high` (both required, low < high); CAMR rows with integer `mksc:intro-order` (0..n-1) form the pre-countdown intro, each with integer `mksc:camera-ticks`, `mksc:camera-fov-end` and vectors `mksc:camera-target-begin`/`-end`, travelling its kind-2 route (A/Start skips); META string `mksc:incomplete` names unconverted content on the course selector; optional META fixed `mksc:fall-height` (world Z) respawns karts below it instead of 20 m under the lowest collision; optional META fixed `mksc:kart-scale` (0.125..4, default 1) sizes every kart on the course together — collision radius and ride height, drawn kart and driver, wheels, shadow, effects, balloons and the default chase camera (item art reads it through `portable_course_3d_chase`); optional META vectors `mksc:camera-eye` and `mksc:camera-target` (metres from the kart's ground foot: x right, which must be 0, y forward, z up; both or neither; eye at least 1 m behind and not below the target, target not behind the kart) replace the chase rig, whose speed pull-back scales with its trail, and integer `mksc:camera-fov` (full vertical angle in 1/65536 turns, 1820..21846; absent keeps the stock lens) sets its field of view; the DS and MK64 importers emit all four from the recovered games;
the CPU fallback rejects them by name and still draws opaque and alpha-mask courses.
## Optional 3D models

Add `models.json` beside `mod.json`, then use the ordinary kit `build` command.
Race Renderer must be **3D**; Original keeps the source artwork. The included
generator creates an original low-poly kart and tree without game assets:

```sh
python3 mods/examples/low-poly/build.py --output-dir .mksc-tools/portable/runs/r2/mods
mksc PACK --mods-dir .mksc-tools/portable/runs/r2/mods --mod low-poly --race-renderer 3d
```

By default the example replaces Mario and Peach Circuit trees. Its `--driver`,
`--course` and `--art-bank` arguments select manifest IDs and a
course-local scenery art bank. Custom drivers use their own manifest ID, not
their graphics, seat or voice donor. Export the track to inspect its art banks.
Existing driving rules, collisions and saves still belong to the race content.

An authored file has this shape (the triangle is a minimal format example):

```json
{"version":1,
 "materials":[{"id":1,"argb":"#FF40C0E0"}],
 "meshes":[{"id":1,"materials":[1],
   "vertices":[[-2,0,0,0,0],[2,0,0,1,0],[0,0,4,0.5,1]],
   "triangles":[[0,1,2,0]]}],
 "bindings":[{"kind":"driver","target":1,"art_bank":0,"pose":0,
   "mesh":1,"scale":[1,1,1],"pivot":[0,0,0]}]}
```

Vertices contain XYZ and normalized UV; triangles contain three zero-based
vertex indices and a material slot. Coordinates use world pixels, with positive
Z up and forward along negative Y. `pivot` defines the local foot anchor before
`scale` and actor rotation; shrink/squash preserve this foot anchor. 3D sprites and models share steering, lean, hop/landing squash and slowdown-ground bob, with drift/boost effects from imported FX assets; cached bounds keep new lean above the supported plane. Grounded models follow the contact normal, with a small depth-tested contact shadow; source sprites anchor their lowest opaque edge to the same plane. Numbers compile to signed Q16. Materials accept an
ARGB integer or `#AARRGGBB`, an optional relative PNG `texture`, and `wrap_u` /
`wrap_v` booleans (otherwise clamp). Nearest sampling multiplies texture and
material channels; alpha below 128 is transparent and other alpha is opaque.
Blended materials are not supported. In authored 3D courses, driver bindings use
0.05 metres per source world pixel after binding scale/pivot. Course `objects`
reference mesh sections explicitly as `model: {kind: 96, id: ID}`; their positions
and mesh units are metres, with authored quaternion/scale applied directly.
An object property `{"key":"mksc:billboard","type":"boolean","value":true}`
opts into camera-facing cylindrical Z-up rendering. It preserves authored foot
position and axis scales but ignores authored rotation (a moving billboard follows its `mksc:motion` position and ignores its turn); absent/false retains it. Integer `mksc:shadow` names a kind-97 material whose texel alpha times material alpha darkens the floor below the object (posed by its motion): a decal turned with its heading (texture top row forward), sized by the required vector `mksc:shadow-size` (half width along local x, half length along local y, and the depth below the origin past which the floor no longer takes it, metres).
The bridge-course example includes an original kart, placed tree and cutout sign;
these are presentation only and do not add collision geometry. Static objects may declare integer `mksc:scenery-art-bank`0..63 to select `(scenery, course ID, bank, pose0)` from the final model catalog. A valid replacement composes binding scale/pivot before the full OBJ transform and disables its billboard; missing/invalid bindings retain the explicit model and billboard. Controlled actors, coins and sky objects reject this selector. Scenery uses OBJ-local units without the driver’s 0.05 conversion. Objects move with string `mksc:motion`: `spin` (vector `mksc:motion-axis` through the object position, or through optional vector `mksc:motion-pivot` for orbiting parts; integer `mksc:spin-rate` in turns<<32 per tick and optional `mksc:spin-run`/`-ramp`/`-stop` ticks for a ramp-run-ramp-rest cycle that reverses), `swing` (axis, `mksc:swing-amplitude` in turns<<16 up to a quarter, `mksc:swing-period` ticks) or `route` (the OBJS route, a declared closed cycle, at each node's speed, resting `mksc:route-wait` ticks per node; boolean `mksc:route-face` turns it about world up to face each leg; at most 256 nodes); optional integer `mksc:motion-phase` (0..2^24 ticks) runs any motion that far ahead of the race clock, so objects sharing one route keep their spacing; a moving object may also sway as link `mksc:sway-link` (1..16) of a stack: integer `mksc:sway-amplitude` (turns<<16, up to a quarter), `mksc:sway-period` and `mksc:sway-lag` ticks, fixed `mksc:sway-length` metres per link and a horizontal vector `mksc:sway-axis` bend each link by the sum of the sines below it (`core/mesh_motion.h`; DS Pokeys); pose is a pure function of the race tick. COLT rows naming the object as owner hold its rest pose and move with it; karts on them ride along, and the kart's slope test sorts floor from wall at each pose. Fixed `mksc:hazard-radius` (metres, up to 64) with optional vector `mksc:hazard-center` (rest pose, default the object position) makes a contact sphere, moving with the object, that knocks karts out of it at half speed; optional integers `mksc:hazard-period` and `mksc:hazard-active` make it live only while tick % period < active (burners). Vector `mksc:route-window` (route, from, to) with integer `mksc:route-window-follower` (an OBJS id) shows the object only while some moving object with that id on that route is from..to ticks into its route cycle (wrapping when from > to), or with boolean `mksc:route-window-outside` only while none is (MK64 railroad crossings swap a dark and a flashing model as the train nears); a window naming no such follower fails the course.

A mesh may replace `vertices`/`triangles` with `"obj":"kart.obj"`; the compiler
accepts `v`, `vt` and triangulated `f` records, including negative indices, and
resolves separate position/UV indices. Normals and material libraries are not
imported. Source paths must
remain inside the mod folder. Textures and geometry are embedded into the pack;
the game never opens model paths. Unknown fields, duplicate keys, invalid indices
and non-finite/out-of-range numbers are authoring errors.

Binding kind `scenery` uses `target` as the course ID and `art_bank` as its
placement's base art bank. `pose` is 0 for ordinary art, an exact driver frame
override for contact/celebration, or the scenery frame offset from that base
bank; values are 0–31. Supply another binding/model for a special pose, or let
that pose retain its source billboard. Hop, squash, bob and source-facing yaw
remain snapshot-driven; rendering never changes driving state.

Version-1 optional pack sections are 96 (MSH1 mesh), 97 (MTL1 material) and 98
(BND1 bindings, content ID 1), with flags 0 (imported packs use 3, required and deterministic). Kind 109 rigs a mesh for skeletal clips and texture swaps; kind 110 adds a 3D driver (donor, grid flag, physics ratios, portrait); kind 111 adds a 3D kart (owner, mesh, per-driver seats, physics ratios; flag 4 drops the footprint blob shadow); kind 113 (KSP1, content ID = driver ID) gives a driver without a model directional sprite frames (64 view slots per pitch row, up to 16 rows chosen by slope, tumble frames for hits, up to four wheel palettes, star colours and ghost/Boo alphas); Architecture has their wires. Caps are 2,048 vertices, 4,096
triangles and 16 materials per mesh, 1,024×1,024 ARGB textures, and 256 bindings.
The wire layouts live in `assets/model_asset.h` and `model_format.py`.
Lighting, fog and filtering (GPU/GLES; stock packs opt in to nothing). MTL1 flag 4 lights a model material and flag 8 fogs it; MSH1 normals are smoothed from faces (welded by position). Lit materials shade `emission + light*(ambient + diffuse*max(0,N.L))`, with the material colour as diffuse. C3D1 MATS PROPs: boolean `mksc:lit` (META ambient, or integer RGBA `mksc:ambient`; MATS emissive is the emission), boolean `mksc:fog`, integer `mksc:texture-filter` 0 nearest, 1 bilinear, 2 N64 three-point. Integer `mksc:present-in` on a MATS or SURF row keeps its faces or collision only in some races: bit race × 4 + class, races 0 Grand Prix, 1 Time Trial, 2 Quick Run, 3 VS, 4 Battle, classes 0..2 50/100/150cc (MK64 Choco Mountain's guardrails use 0x11F1: 50cc races and every Time Trial); previews and exploration keep everything. META: boolean `mksc:fog` turns on fog_rgba (alpha = strongest weight) from fog near to far metres of view depth, integer `mksc:fog-curve` 0 linear, 1 ease-in, 2 ease-out, 3 linear in 1/depth; the light points against the sun vector, or integer `mksc:light-camera` (Q16 turns about up) makes it follow the camera's forward; `mksc:light-color-0..3` (RGBA, default sun) are four light colours that SURF integer `mksc:light` 0..3 selects for a kart standing there; SURF integer RGBA `mksc:kart-shade` multiplies and `mksc:kart-add` adds to the colour of a kart standing there (MK64 lamp, lava and shadow zones; a star's colours replace the add); SURF boolean `mksc:reflect` mirrors a kart within 0.5 m above it across the floor at half alpha and drops its shadow (MK64 ice); `mksc:model-ambient`/`mksc:model-emission` (RGBA) light flag-4 kart, driver and object models on the course.
Missing or invalid optional models warn and fall back per binding; ambiguous
duplicate binding keys are skipped together. Invalid pack checksums/ranges still
reject the container. Omitted `binding_mode` (or `"replace"`) retains version-1 whole-registry replacement. Opt into `"binding_mode":"overlay"` in version-1 `models.json` to emit a version-2 kind98 registry: later rows replace matching `(kind,target,art_bank,pose)` keys while preserving other keys. The loader normalizes independently per target domain into ordinary version-1 bindings; mesh/material references resolve from the final pack. Invalid winners mask earlier rows and fall back locally; duplicate winning keys stay ambiguous. A malformed overlay or more than256 merged raw rows rejects the candidate atomically, including review/install. Base packs cannot contain raw overlay registries. Empty bindings emit no registry in either mode. Model mods retain normal mod-set save isolation.
Build `mods/examples/overlay-kart/build.py` and `mods/examples/overlay-scenery/build.py` into one mods directory, then enable them after `bridge-course/build.py --scenery-binding`. Each example is independently authored and ROM-free; kart `--driver` and scenery `--course`/`--art-bank`/`--pose` select keys. Swap their order to retain both; later mods sharing a key win.

## Saves and verification

Mod identity includes API version, ordered IDs/versions and content fingerprints.
Changing files changes identity even without a version bump. Modded TT records
and local ghosts use separate filenames/inventories; incompatible ghosts/replays
are refused. Older shared-name mod ghosts remain on disk and can be imported
through ghost exchange. GP progress
remains shared and records the last mod set. Mirror mode remains separate too.
Enable the recorded content before replaying a modded race.

`mods/examples/` includes lucky-start, lap-bonus, item-swap, data-tuning,
custom-cup, shade-driver and ambient-cycle. The first three run directly as a
mods root; generated examples require their builders. `check.py debug` checks
ROM-free builders, loader bounds, sandbox and identity. `--game-pack PACK` adds
real modded routes. For a local trial: `mksc --quick-run-menu-smoke PACK CUP
DRIVER --mods-dir dist --mod ID` with the automated SDL environment from
[Building](BUILDING.md). Keep exports/captures in allowed scratch and never ship
the base pack in a mod. Binary section definitions belong to `assets/` headers
and the native/reference importers; format changes must keep both importers
byte-identical and update the importer version.

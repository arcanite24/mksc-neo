# Building

## Requirements

- CMake 3.20+, a C11 compiler (Clang, GCC or MSVC) and Python 3.
- SDL 3.4 (the releases use 3.4.18) for the game itself. If CMake does not find it,
  set `CMAKE_PREFIX_PATH` to your SDL install. On Windows, build from an x64
  Visual Studio developer prompt.
- Your own Mario Kart: Super Circuit (US) ROM to play or to run the content tests
  (SHA-1 `9d327c030c3e2d9007990518594f70c3340ac56f`).

## Build and test

`tools/portable/check.py` configures, builds and tests. Build output and logs go to
`.mksc-tools/portable/check/<profile>/`.

```sh
python3 tools/portable/check.py debug                         # headless C tests (no ROM needed)
python3 tools/portable/check.py debug --test 'portable_mods'  # only tests matching a regex
python3 tools/portable/check.py debug --sdl                   # also build the SDL game
python3 tools/portable/check.py debug --sdl --rom path/to/mksc.gba   # plus game smoke routes
python3 tools/portable/check.py all                           # debug, release and sanitizer builds
```

- Warnings are errors. Run `debug` before committing and `all` before a release.
- `--game-pack PACK` reuses an imported pack instead of importing the ROM again.
- `--mkds-rom` and `--mk64-rom` add the Mario Kart DS and Mario Kart 64 importer tests.
- Don't run two checks on the same build directory at once.

Most tests are plain C unit tests in `portable/tests/test_*.c`, and they run in
seconds. The `--sdl` routes run the real executable once per mode or feature.

## Run

```sh
.mksc-tools/portable/check/debug-sdl/mksc                               # asks for your ROM on first start
.mksc-tools/portable/check/debug-sdl/mksc --import path/to/mksc.gba game.pack   # import ahead of time
.mksc-tools/portable/check/debug-sdl/mksc game.pack                     # play a specific pack
```

The first start imports the ROM (`.gba`, or a `.zip` containing it) into the data
folder. `mksc --help` lists the launch options, such as `--window-scale`,
`--aspect`, `--race-renderer 3d`, `--mod ID` and `--no-mods`. They override saved
settings for one launch.

Data folder (settings, imported pack, records, ghosts and `mods/`):

| OS | Path |
|---|---|
| macOS | `~/Library/Application Support/MKSC Neo/Portable/` |
| Linux | `~/.local/share/MKSC Neo/Portable/` |
| Windows | `%APPDATA%\MKSC Neo\Portable\` |
| Web | IndexedDB, in the browser |

`MKSC_DATA_DIR` overrides it.

To add other games' content, use Options > Mods > Import from another game, or
`mksc --import-game ROM`. A Mario Kart DS (US) or Mario Kart 64 (US) ROM becomes
a local mod.

## Controls

| Action | Keyboard | Controller |
|---|---|---|
| Steer | Left / Right | Stick or D-pad |
| Accelerate | Z | A (south) |
| Brake | Down | B (east) |
| Hop / drift | Left Shift | Right shoulder |
| Item (aim with Up / Down) | C | Left shoulder |
| Pause | P or Space | Start |
| Menus: confirm / back | Enter or Z / Esc or X | A / B |

Rebind controls in Options. Local multiplayer supports 2–4 players. More than two
players need controllers.

## Release bundles

Push a tag `vX.Y.Z` (or `vX.Y.Z-rc.N` for a prerelease) to run
`.github/workflows/release.yml`. It tests, builds macOS (universal), Linux
(x86-64), Windows (x64), web and Android (arm64), and publishes a GitHub
release with `SHA256SUMS.txt`. Running the workflow by hand builds the same
bundles as artifacts without publishing.

To build a bundle for the current OS locally:

```sh
python3 tools/portable/package_release.py --sdl-prefix PATH/TO/SDL
```

Archives go to `.mksc-tools/portable/runs/dist/`. Packaging scans each bundle and
refuses to ship anything taken from the ROM.

Android APKs are signed with the repository secrets `ANDROID_KEYSTORE_BASE64`,
`ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`;
without them, they are debug-signed.

## Android

Needs the Android SDK, NDK 27.1, JDK 17 and the SDL 3.4.18 source tree (its
`android-project` provides the Gradle wrapper and SDL's Java activity).

```sh
python3 tools/portable/android.py --sdl-source PATH/SDL3-3.4.18         # build the APK
python3 tools/portable/android.py --sdl-source PATH/SDL3-3.4.18 --run   # install, launch and follow the log
```

The app is fullscreen and landscape. The first start opens the system file
picker for the ROM. Touch controls can be turned on in Options.

## Web

Needs Emscripten 6.0.12. Build SDL with `emcmake cmake` (`-DSDL_SHARED=OFF
-DSDL_STATIC=ON -DSDL_TESTS=OFF`) and install it to `$prefix`, then:

```sh
emcmake cmake -S portable -B build-web -DCMAKE_BUILD_TYPE=Release -DPORTABLE_BUILD_TESTS=OFF \
  -DPORTABLE_BUILD_SDL3_APP=ON -DSDL3_DIR="$prefix/lib/cmake/SDL3"
cmake --build build-web --parallel
python3 -m http.server 8000 --bind 127.0.0.1 --directory build-web   # open /mksc.html
```

The page imports the ROM in the browser and keeps it, with saves, settings and
mods, in IndexedDB. A service worker (`sw.js`) caches the engine for offline
play. Controllers work through the Gamepad API.

## Project site

`tools/site/build_site.py` builds the site at <https://arcanite24.github.io/mksc-neo/>
(this documentation, downloads and the playable web build). `.github/workflows/pages.yml`
publishes it on every push to `main`.

```sh
python3 -m pip install markdown
python3 tools/site/build_site.py --output site-out --web-build build-web
```

## Other targets

`portable/platform/toolchains/` has cross toolchains for 32-bit PowerPC and i686
Linux. These builds check the engine on big-endian and 32-bit CPUs. Tests run
under QEMU user emulation:

```sh
cmake -S portable -B build-ppc -DPORTABLE_BUILD_SDL3_APP=OFF \
  -DCMAKE_TOOLCHAIN_FILE=portable/platform/toolchains/linux-powerpc.cmake
cmake --build build-ppc --parallel && ctest --test-dir build-ppc --output-on-failure
```

GPU shaders are pre-built and embedded. Only rebuild them after editing
`portable/platform/shaders/`, with `python3 tools/portable/build_render_shaders.py --help`.

# Contributing

Bug reports, fixes and new features are welcome.

## Reporting a bug

Open an [issue](https://github.com/sonofskywalker3/pokerom-trader/issues) with:

- the game(s) and save files involved (for example Crystal → Emerald)
- what you did, what you expected, and what happened instead
- your platform (Windows, Linux, macOS) and app version (shown on the About screen)

If a save loads wrong or a result looks off in-game, attaching the save (or a copy of it) helps a lot. Don't attach saves you'd mind sharing publicly.

## Building from source

### 1. Get the code

```bash
git clone https://github.com/sonofskywalker3/pokerom-trader.git
cd pokerom-trader
```

### 2. Install a compiler and CMake

- **Linux:** `sudo apt-get install build-essential cmake libxi-dev libxinerama-dev libxcursor-dev libx11-dev libxrandr-dev libgl1-mesa-dev`
- **macOS:** Xcode Command Line Tools (`xcode-select --install`) and CMake (`brew install cmake`).
- **Windows:** any MinGW-w64 GCC with `make` and CMake, such as MSYS2 (UCRT64) or the toolchain bundled with Strawberry Perl. With Strawberry Perl the compiler is called `x86_64-w64-mingw32-gcc` and `make` is `mingw32-make`, so pass `CC=x86_64-w64-mingw32-gcc` to each `make` below.

### 3. Build PKSav (the save-file library)

```bash
cd deps/pksav
mkdir build && cd build
cmake .. -DPKSAV_STATIC=ON -DPKSAV_ENABLE_TESTS=OFF -DPKSAV_ENABLE_DOCS=OFF -DPKSAV_ENABLE_APPS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build .
cd ../../..
```

On Windows, if the build fails at `pksav.rc` with "'gcc' is not recognized", add
`-DCMAKE_RC_FLAGS="--preprocessor=x86_64-w64-mingw32-gcc --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED"` to the `cmake ..` line.

### 4. Build raylib

```bash
cd deps/raylib/src
make PLATFORM=PLATFORM_DESKTOP
cd ../../..
```

### 5. Build and run Pokerom Trader

```bash
make          # builds build/pokeromtrader(.exe)
make launch   # builds and runs it
```

## Tests

```bash
make test
```

The tests are plain C programs that load, change and re-check real save files. They never write the files themselves, only temporary copies under `tests/`. The Gen 3 saves in `saves/refs/` are part of the repo; cases that need other games look for these files and are skipped when they're missing:

| Path | Game |
|---|---|
| `saves/Pokemon - Blue Version.sav`, `Pokemon - Yellow Version.sav`, `Pokemon - Red LivingDex.sav` | Gen 1 |
| `saves/Pokemon - Gold Version.sav`, `Pokemon - Crystal Version.sav` | Gen 2 |
| `saves/gen4/diamond.sav`, `pearl.sav`, `platinum.sav`, `heartgold.sav`, `soulsilver.sav` | Gen 4 |

These paths are gitignored, so you can use your own saves without committing them.

## Command-line tool

`make pkcli` builds `tools/pkcli.exe`, a headless tool that uses the same code as the app: list a save, move or trade Pokémon, evolve, transfer, give items and events. Run it with no arguments for the full command list. It's handy for scripting and for checking a change without clicking through the UI.

## Sending changes

1. Fork the repo and make a branch for your change.
2. Keep each pull request to one fix or feature, and add or update a test in `tests/` when the change touches save data.
3. Run `make test` before opening the PR, and say in the description which games you checked.
4. Save formats and game rules should match the real games: the [pret](https://github.com/pret) disassemblies, [PKHeX](https://github.com/kwsch/PKHeX) and [Bulbapedia](https://bulbapedia.bulbagarden.net) are the references used throughout the code.

By contributing you agree that your work is released under the project's [MIT License](LICENSE).

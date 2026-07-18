# Gen 3 trade module — dev notes (branch `gen3-trade`)

Fork work adding Generation 3 (GBA) trading. Scope and design: see
`gen3-trade-module-spec.md` in the project root.

## Toolchain (Windows, this machine)

The app's supported Windows build is **mingw-w64 GCC + the project Makefile**
(matches pokerom-trader's `build-windows.yml` CI, which uses MSYS2 UCRT64).
MSVC does **not** work — the app uses POSIX headers (`dirent.h`, `unistd.h`,
`sys/errno.h`) MSVC lacks. Installed via scoop: `gcc` (15.2.0) + `make` (4.4.1).

Two environment accommodations, **neither modifies the app**:

- **`sys/errno.h` shim** on `C_INCLUDE_PATH`: nuwen-mingw omits this one header;
  it is just `#include <errno.h>`. Shim lives outside the repo (scratchpad).
- **`-fpermissive`**: GCC 14+ promotes `-Wincompatible-pointer-types` to a hard
  error; the app (`src/filehelper.c`) has a latent `LPCSTR saves[]`-used-as-buffer
  bug that older compilers only warned on. `-fpermissive` restores that leniency
  without editing the source. (Current MSYS2 GCC is also 14+, so it would need
  the same flag.)

### Build the baseline app

```sh
# PATH must include: scoop gcc bin, scoop shims (make), Git\usr\bin (sh/coreutils)
# C_INCLUDE_PATH must include the sys/errno.h shim dir
cmake -S deps/pksav -B deps/pksav/build -G Ninja -DCMAKE_C_COMPILER=gcc \
      -DPKSAV_STATIC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build deps/pksav/build            # -> deps/pksav/build/lib/libpksav.a
make -C deps/raylib/src PLATFORM=PLATFORM_DESKTOP   # -> libraylib.a
make CC="gcc -fpermissive"                # -> build/pokeromtrader.exe
```

## PKSav reconciliation

`deps/pksav` (vendored, committed here, `gba/` API) is the version the app builds
against and the one we extend; PKSav changes are committed on this branch. The
separately-cloned top-level `pksav/` is upstream `master`, an **incompatible**
`gen3/`-API refactor — kept only as an upstream reference, not used by the build.

## PKSav change: validity-aware active-block selection

`deps/pksav/lib/gba/save.c` `pksav_gba_get_active_save_slot_ptr` originally chose
the active slot by raw `save_index` comparison. A freshly-saved file has one
written slot and one **uninitialized** slot whose `save_index` reads `0xFFFFFFFF`,
so the naive compare selected the empty slot — PKSav then failed to load the save
entirely. Fix: validate each slot (all 14 sections carry the `0x08012025` magic
and in-range ids) and pick the highest-index **valid** slot. This is the spec's
"#1 way to silently lose a trade."

## Gen 3 read tools (slice 1)

- `tools/gen3_species.{h,c}` — Gen 3 internal species index -> National Dex ->
  name. Mapping: internal 1..251 = National 1..251; internal 277..411 =
  National 252..386 (`national = internal - 25`); 252..276 unused.
- `tools/gen3_box_list.c` — loads a save, prints an independent validity-checked
  active-block audit next to PKSav's choice, then lists party + PC box Pokemon.
- `tools/probe.c` — minimal load/selection diagnostic.

Build & run a tool (read-only; never writes the save):

```sh
gcc -std=c11 -Wall tools/gen3_box_list.c tools/gen3_species.c \
    -Ideps/pksav/include -Ideps/pksav/build/include \
    -Ldeps/pksav/build/lib -lpksav -o build/gen3_box_list.exe
build/gen3_box_list.exe "path/to/ruby.sav"
```

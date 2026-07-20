# Gen 4 (DS) Support — Handoff & Starting Point

Snapshot for a fresh agent picking up the next phase: **Generation 4 (DS) support**.
Written 2026-07-20.

## Where things stand

- **Trunk branch: `gen3-trade`** (the fork's default; `main` is a stale upstream mirror).
- Just merged: the **event-flag** feature (merge commit "Merge event-flags…"). Everything below is on `gen3-trade`, working tree clean, **full app builds** (`build/pokeromtrader.exe`).
- **What works today:** Gen 1/2/3 same-gen trading, cross-gen Gen1↔Gen2, trade evolutions, Boxes (PC) management, forward transfer (Gen 1/2 → Gen 3), and Events (re-issue distribution tickets — now also **sets the in-game event flag** for Gen 3, verified in a real Emerald via BizHawk; GS Ball is item-only pending a Crystal ROM).
- Not started: **Gen 4 (DS)** — the subject of this handoff.

## How the project is built (Windows / MinGW here)

Tools present: gcc 13.2 (`C:\Strawberry\c\bin`), cmake, ninja, mingw32-make.
```
# 1. pksav (static)
cd deps/pksav && cmake -B build -G Ninja -DPKSAV_STATIC=ON -DPKSAV_ENABLE_TESTS=OFF \
  -DPKSAV_ENABLE_DOCS=OFF -DPKSAV_ENABLE_APPS=OFF -DCMAKE_BUILD_TYPE=Release && cmake --build build
# 2. raylib (static)
cd deps/raylib/src && mingw32-make PLATFORM=PLATFORM_DESKTOP
# 3. app
cd <repo root> && mingw32-make        # -> build/pokeromtrader.exe
```
CI equivalents live in `.github/workflows/build-*.yml`.

## Architecture a newcomer needs

- **App:** C11 + raylib GUI. `src/screens/*` (one file per screen), `src/components/*`, `src/*.c` helpers.
- **Save parsing:** the vendored **pksav** (`deps/pksav`) — **Gen 1–3 ONLY. It has no Gen 4 support.** This is the core gap.
- **Save wrapper:** `include/common.h` — `SaveGenerationType { NONE, 1, 2, 3, CORRUPTED }` and
  `union SaveGeneration { gen1_save, gen2_save, gba_save }` inside `PokemonSave`.
- **How Gen 3 was added (the pattern to mirror):** `src/gen3_species.c`, `src/gen3_stats.c`,
  `src/gen_transfer.c`, plus detection/load/save in `src/filehelper.c` and `src/pksavfilehelper.c`,
  and screen wiring in `src/screens/*`. Gen 3 base-stat/growth data was compiled from Bulbapedia/Serebii.
- **Event-flag work (just done):** flag get/set API added to vendored pksav
  (`pksav_gba_save_get/set_flag`, `pksav_gen2_save_get/set_event_flag`); `src/events.c` sets each
  event's flag. See `docs/superpowers/event-flags.md` for the decomp-sourced flag table + verification.

## The Gen 4 challenge (read before coding)

- Gen 4 = DS games: **Diamond/Pearl, Platinum (DP/Pt), HeartGold/SoulSilver (HGSS)**.
- Gen 4 saves are **512 KB**, dual main-block with a "save counter" selecting the active block,
  per-block footer/checksums, and a **different Pokémon data structure** (128-byte boxed / 136-byte
  party, 4 shuffled 32-byte blocks, encrypted with an LCRNG seeded by the checksum — conceptually
  like Gen 3's block shuffle but different math).
- **pksav cannot parse this.** Realistic options:
  1. **Extend pksav** with a `gen4` module (save layout, active-block selection, pokemon
     decrypt/encrypt, party/box access). Largest but keeps one library.
  2. Vendor/port a Gen-4-capable parser.
  3. Reference implementations: **PKHeX** (C#), **pkNX**, and the docs at Project Pokémon and
     Bulbapedia "Save data structure (Generation IV)".
- **Transfer semantics:** Gen 3 → Gen 4 is **Pal Park** (one-way, rule-limited), Gen 4 ↔ Gen 4 is
  trading. The readme's "Crystal → Platinum / SoulSilver routing" goal implies multi-hop chaining.

## Recommended first slice (smallest useful, testable)

1. **Read-only load + display first.** Add `SAVE_GENERATION_4` and a `gen4_save` member to
   `PokemonSave`; add 512 KB DP/Pt/HGSS detection in `filehelper.c`/`pksavfilehelper.c`.
2. Parse the active block + **party** (species, level, nickname) and get the app to just *list* a
   Gen 4 party in the file-select/trainer UI. No writing yet.
3. Then boxes, then trading, then Gen 3 → Gen 4 transfer.

## Fixtures & verification

- Gen 4 test saves (512 KB): `ncorgan/pksav-test-saves` has
  `diamond_pearl/pokemon_diamond.sav`, `platinum/pokemon_platinum.sav`,
  `heartgold_soulsilver/pokemon_soulsilver.sav`. (Gen 1–3 baselines already in `saves/refs/baseline/`.)
- In-game verification: this session installed **BizHawk** at `Pokemon/emu/BizHawk` (GBA via mGBA core).
  For DS, use a scriptable DS emulator — **DeSmuME** (Lua) or melonDS. The pattern used for Gen 3
  (`tools/verify_events.lua`): build the emulator's SaveRAM from a raw save, launch headlessly with a
  Lua script, drive inputs, read live memory. Reuse that approach.

## Key files to touch (mirror the Gen 3 work)

- `include/common.h` — `SaveGenerationType`, `PokemonSave` union (+`gen4_save`)
- `src/filehelper.c`, `src/pksavfilehelper.c` — detect/load/save Gen 4
- new `src/gen4_*.c` (species/stats/save parsing) — will likely need a Gen 4 save parser since pksav lacks one
- `src/screens/FileSelectScreen.c`, `TradeScreen.c`, `BillsPcScreen.c`, `TransferScreen.c` — wire Gen 4 in
- `src/gen_transfer.c` — extend for Gen 3 → Gen 4 (Pal Park) rules

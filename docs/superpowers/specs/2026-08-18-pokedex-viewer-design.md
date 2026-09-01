# Pokédex Viewer & Dex-Aware Trading — Design

- **Date:** 2026-08-18
- **Status:** Draft (awaiting review)
- **Author:** Jeff + Claude (Fable 5)
- **Feature branch base:** `gen4-support`

## Context

Every supported generation stores Pokédex progress as seen/owned bitfields, and
the app already reads/writes them the legitimate way: trades and transfers call
`update_seen_owned_pkmn()` (`src/pksavhelper.c`) so the dex reflects mons that
actually passed through the save. What's missing is *visibility*: no way to see
a save's dex progress, spot what's missing, or figure out which other save could
supply it. This design adds a **strictly read-only** Pokédex viewer plus two
dex-aware trading aids.

**Hard rule: this feature never writes dex bits.** The only thing that changes
a Pokédex remains an actual trade/transfer, exactly as today.

## Goals

1. **Viewer** — per-save Pokédex screen (Gen 1–4): every species shown as
   owned / seen-only / missing, with seen+owned totals and name/number search.
2. **"Who has it?" finder** — click a missing entry to list the *other* saves
   in the save folder that contain that species — or a member of its evolution
   line — in party or boxes, filtered to saves that can actually trade with
   this one. Selecting a source save jumps straight into the existing trade
   flow with both saves pre-selected.
3. **Trade-screen dex badges** — on the trade screen, annotate each of save 1's
   listed Pokémon with save 2's dex status for that species (and vice versa):
   `NEW` (unregistered there), `SEEN`, or nothing when already owned. Makes
   "which of these does the other save actually need?" visible at a glance.

## Non-Goals

- **No dex editing of any kind** — no toggles, no complete-the-dex, no
  "mark seen". Read-only, so no dirty/save/backup flow on the new screen.
- No form/gender/language dex extras (Unown forms, Spinda spots, etc.).
- No cross-*folder* scanning — the finder looks at the configured save dir.
- v1 finder ranks by simple rules; no move-legality or level checks.

## Per-generation storage notes (read side only)

| Gen | Where | Access | Range |
|-----|-------|--------|-------|
| 1 | `gen1_save.pokedex_lists.p_seen/p_owned` | `pksav_get_pokedex_bit`, dex-order index (map internal→dex via `species_gen1_to_gen2[]`) | 151 |
| 2 | `gen2_save.pokedex_lists.p_seen/p_owned` | same (species index == dex number) | 251 |
| 3 | `gba_save.pokedex` | `pksav_get_pokedex_bit` on `p_owned` / seen list (read one mirror; they're kept in sync by the game and our trade path) | 386 |
| 4 | `gen4_save` | new `gen4_dex_get_seen/get_caught` getters in `gen4_save.c` (write path `gen4_dex_set_seen_caught` already exists for trades — untouched) | 493 |

## Design

### Data layer (`pksavhelper.c`, mirroring the `bills_pc_*` pattern)

- `pokedex_get_entry(save, dex_no, *seen, *owned)` — gen-dispatched read.
- `pokedex_counts(save, *seen, *owned)` — totals for the header.
- `save_contains_species(save, dex_no)` — scan party + all boxes using the
  existing `bills_pc_get_view()` enumeration; returns where (party/box#/slot).
- Evolution-family table: `uint16_t dex_family[494]` mapping each national dex
  number to its line's base-form dex number (static data, authored from
  Bulbapedia family lists; cross-checked against pret). "Same line" ⇔ same
  family id. This is the one new data table the feature needs.

### Pokédex screen (`src/screens/PokedexScreen.c`)

Main menu → `SingleFileSelectScreen` (new `SINGLE_PLAYER_MENU_TYPE_POKEDEX`)
→ scrollable dex list for that save's generation:

- `#025 PIKACHU` — every entry shows its name: greyed out if not seen,
  normal black if seen, and with a Poké Ball icon next to it if owned
  (no separate SEEN/NEW tags needed on this screen — the three states are
  the styling).
- Header totals `Seen 45 · Owned 32 / 151`; search box (reuse Bills PC search).
- **Click any non-owned entry** → side panel: "Available from:" listing each
  compatible save in the folder with a match, e.g.
  `blue.sav — KADABRA (evo line) — Box 2` with the trade-evo note where it
  applies (trading Kadabra in *becomes* Alakazam — the app's trade-evolution
  support makes an evo-line source often *better* than the exact species).
- Click a source row → set save 1 = dex save, save 2 = source save (same
  wiring `FileSelectScreen` does: load both, `create_trainer`, set paths) →
  `SCREEN_TRADE`. Trading works from the party, so boxed sources are
  handled inline — no detour to the Boxes screen:
  - **Party has an empty slot** → withdraw the boxed mon into the party
    automatically (`bills_pc_move_pkmn` + flush + backup + save to disk,
    the same write path Boxes uses), then straight to trade.
  - **Party is full** → prompt in the panel: *"KADABRA is in Box 4 — pick a
    party Pokémon to swap"*, list the source save's six party mons, and the
    chosen one swaps into Box 4 (again via the normal Boxes write path,
    with auto-backup). Then straight to trade.
  - Either way the source save is modified and saved to disk *before* the
    trade screen loads it — flagged clearly in the prompt since this writes
    a save the user didn't explicitly open.

- Compatibility filter for the finder, from existing app rules: Gen1↔Gen1,
  Gen1↔Gen2 (time capsule: mon must be a Gen 1 species holding no Gen 2
  moves — v1 just filters by species range), Gen2↔Gen2, Gen3↔Gen3, Gen4↔Gen4,
  and Gen3→Gen4 one-way via Pal Park transfer (label those rows "transfer").

### Trade-screen badges (`src/screens/TradeScreen.c`)

For each party mon drawn for save 1, look up its national dex number (mapping
helpers exist for every gen) and call `pokedex_get_entry()` against save 2;
draw a small `NEW` / `SEEN` badge next to the name; owned → no badge. Same for
save 2's party against save 1. Also applies to the Transfer (Pal Park) screen.

## Implementation order

1. Read helpers: `pokedex_get_entry` / `pokedex_counts` + Gen 4 getters.
   Tests: assert known values against the real saves in `saves/`.
2. Pokédex screen, list + totals + search (pure viewer — usable on its own).
3. Trade-screen badges (small, independent of 4–5, immediate payoff).
4. `dex_family[]` table + `save_contains_species()` scanner. Test: family
   lookups for a sample across all gens; scanner against test saves.
5. "Who has it?" panel + jump-into-trade wiring.

## Open questions

- Finder scan cost: load every save in the folder and scan all boxes on click,
  or scan once when the Pokédex screen opens and cache? Folder sizes here are
  small; scan-on-open is probably fine and simplest.
- Should the badge also appear in Bills PC box views (e.g. when picking what
  to move to party for a trade)? Cheap once helpers exist; maybe v1.1.

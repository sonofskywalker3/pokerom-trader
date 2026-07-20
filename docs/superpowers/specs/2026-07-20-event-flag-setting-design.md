# Event-Flag Setting — Design

- **Date:** 2026-07-20
- **Status:** Draft (awaiting review)
- **Author:** Jeff + Claude (Opus 4.8)
- **Feature branch base:** `gen3-trade`

## Context

The Events feature (`src/events.c`, `include/events.h`) re-issues distribution
key items — GS Ball, Eon Ticket, Mystic Ticket, Aurora Ticket, Old Sea Map — so
the *game itself* generates a fully legitimate Pokémon when the player reaches
the event. Today `apply_event()` only injects the item into the bag.

For most of these events the game **also checks an in-game event flag** before
the sailor/NPC will offer the trip, or before the roamer/static encounter is
enabled. Item-without-flag is why the readme notes the events "may not appear"
(Gen 3 especially). PKSav exposes no flag API, so setting flags was deferred.
This design closes that gap.

## Goals

- Set the required in-game event flag(s) for each supported event, in addition
  to granting the ticket item, so the event actually becomes reachable in-game.
- Cover **all** current events: the four Gen 3 tickets (RS / Emerald / FRLG) and
  the Gen 2 Crystal GS Ball.
- Determine the correct flags **empirically** (save-diff), cross-checked against
  the pret decompilation — never from memory.
- Keep saves safe: idempotent writes, refuse on unrecognized layouts, retain the
  existing auto-backup, and keep the feature labeled experimental until each
  event is verified end to end.

## Non-Goals

- No Gen 4+ (DS) events.
- No general-purpose flag editor UI — only the specific flags these events need.
- No JP-region support (matches the rest of the app).
- Not fabricating Pokémon — the game still generates every mon.

## Scope: events and what each needs

The item grant already works for all of these. This design adds the flag(s).
The exact flag IDs/offsets are **filled in during implementation** from the
save-diff + decomp step below; the table names the target, not final values.

| Event | Games | Unlocks | Item (done) | Flag(s) to set (to verify) |
|-------|-------|---------|-------------|----------------------------|
| GS Ball | Crystal | Celebi | ✅ | Kurt/Ilex-shrine event enable |
| Eon Ticket | R/S/E | Latias/Latios | ✅ | Southern Island access |
| Mystic Ticket | E/FR/LG | Lugia & Ho-Oh | ✅ | Navel Rock ferry enable |
| Aurora Ticket | E/FR/LG | Deoxys | ✅ | Birth Island ferry enable |
| Old Sea Map | Emerald | Mew | ✅ | Faraway Island ferry enable |

Some events may require more than one flag (e.g. a ferry-destination-enable plus
a "map created" flag). The diff step determines the full set per game; the data
table is authored to whatever the diff proves is necessary and sufficient.

## Approach

### 1. Ground truth by save-diff (the oracle)

For each event, on each applicable game:

1. Obtain a reference save **without** the event and one **with** the event's
   flag legitimately set (received via the real distribution / played to the
   trigger). Provided by Jeff, or sourced/produced as needed.
2. Load both with PKSav and byte-diff the decrypted flag region (Gen 3:
   SaveBlock1 flag array; Gen 2: the event-flag block). The changed byte+bit is
   the flag.
3. Cross-check the hit against the pret decomp named constant
   (`pokeemerald` / `pokefirered` / `pokeruby` / `pokecrystal`) to confirm *why*
   that bit is correct and to catch region/version differences.

The diff proves the value; the decomp explains it. Neither alone is trusted.

### 2. A flag API in PKSav (chosen architecture)

Add small, focused helpers to the vendored `deps/pksav` — this is the exact gap
the readme calls out ("PKSav exposes no flag API"), it is reusable and testable,
and the app already never reaches into PKSav internals (so we do not want to
start doing so from `events.c`).

**Gen 3** (`deps/pksav/.../gba/`): flags live in SaveBlock1, which PKSav stores
as sections 1–4 of the decrypted, unshuffled `unshuffled_save_slot`
(`struct pksav_gba_save_internal`, reached via the public `p_internal`). A flag
id maps to a byte at `flag_base(game) + id/8` within SaveBlock1; SaveBlock1
offset `O` → section `1 + O/3968`, byte `O % 3968`. Proposed API:

```c
enum pksav_error pksav_gba_save_get_flag(const struct pksav_gba_save*, uint16_t flag_id, bool* out);
enum pksav_error pksav_gba_save_set_flag(struct pksav_gba_save*, uint16_t flag_id, bool value);
```

`pksav_gba_save_save()` already reshuffles sections and recomputes each section
checksum on write, so no extra checksum work is needed.

**Gen 2** (`deps/pksav/.../gen2/`): Crystal event flags are a bit array at a
fixed offset in the flat `p_raw_save` buffer. Proposed API:

```c
enum pksav_error pksav_gen2_save_get_event_flag(const struct pksav_gen2_save*, uint16_t flag_id, bool* out);
enum pksav_error pksav_gen2_save_set_event_flag(struct pksav_gen2_save*, uint16_t flag_id, bool value);
```

The Gen 2 save's checksum is recomputed by `pksav_gen2_save_save()` on write.

Flag-base offsets per game are defined as named constants next to the API,
sourced from the decomp and confirmed by the diff.

### 3. App wiring

Extend the `pkmn_event` table in `src/events.c` with the flag id(s) per event
(a small fixed-size list per row). `apply_event()` grants the item (unchanged)
and then sets each flag via the new PKSav API. Both steps are idempotent, so
re-running an event is safe. If flag-setting fails (unrecognized layout), the
call returns an error and the save is not written.

### 4. Verification loop (mGBA + diff)

`mGBA` is installed at `C:\Program Files\mGBA\mGBA.exe`.

1. **Byte-level:** run the tool on a "before" save; confirm the output matches
   the "after" reference in the flag region and differs **nowhere unexpected**
   (only the flag byte(s), item bag, and section checksums change).
2. **In-game sanity:** load the edited save + ROM in mGBA and confirm the
   sailor/NPC offers the trip (or the encounter is enabled). This is the final
   gate that flips an event from "experimental" to "verified."

## Inputs required from Jeff

- Gen 3 ROMs: Ruby/Sapphire, Emerald, FireRed/LeafGreen (US/English).
- Per event, a before/after reference save pair — or at least an "after" save
  where the event flag was legitimately set. Gen 1/2 saves for the GS Ball too.
- (mGBA is already installed; the repo already has Gen 1/2 test saves.)

## Testing strategy

- **PKSav unit level:** round-trip a save through `set_flag` → `save_save` →
  reload → `get_flag` and assert the bit persists; assert a no-op on an
  already-set flag; assert only the expected bytes change.
- **Per-event acceptance:** the save-diff match in §4.1 for every event/game.
- **Regression:** existing trade/box/transfer flows still pass on the same
  saves (no unintended section corruption).

## Risks and mitigations

- **Wrong flag / region variant** → save-diff oracle + decomp cross-check;
  per-game constants, not shared guesses.
- **Multi-flag events** → diff determines the full necessary set, not just one.
- **Save-block layout drift (hacks/regions)** → refuse and return an error on
  unrecognized layout; feature stays experimental until verified per event.
- **Availability of reference saves** → Jeff supplies; where a legit "after"
  save is unavailable, produce one via a known-good editor (e.g. PKHeX) purely
  as the diff oracle, still cross-checked against the decomp.

## Future / out of scope now

- The new PKSav flag API is generic enough to upstream to a real `pksav` fork or
  PR later (it currently lives in the vendored `deps/pksav`).
- Setting event flags for a general flag-editor feature could reuse this API.

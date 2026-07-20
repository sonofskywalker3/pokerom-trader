# Event Flags — confirmed data

Source of truth for the flag id(s) each Events entry must set (in addition to
granting the ticket item). Gen 3 values are taken from the **pret decompilation**
(`pokeruby` / `pokeemerald` / `pokefirered`) — a decomp `FLAG_*` value is the bit
index into the SaveBlock1 `flags[]` array, which is exactly what
`pksav_gba_save_set_flag(flag_id)` consumes.

**Status:** Gen 3 values are decomp-derived and byte-level testable now; the
in-game "does the trip appear" confirmation in mGBA is pending ROMs. Gen 2 (GS
Ball) is still to be researched from `pokecrystal`.

## SaveBlock1 `flags[]` base offset (confirmed against each decomp's `global.h`)

| Game            | Offset  | PKSav constant                         |
|-----------------|---------|----------------------------------------|
| Ruby / Sapphire | 0x1220  | `PKSAV_GBA_RS_EVENT_FLAGS_OFFSET`      |
| Emerald         | 0x1270  | `PKSAV_GBA_EMERALD_EVENT_FLAGS_OFFSET` |
| FireRed/LeafGrn | 0x0EE0  | `PKSAV_GBA_FRLG_EVENT_FLAGS_OFFSET`    |

Flag base per game: RSE `SYSTEM_FLAGS = 0x860` (Emerald) / `0x800` (Ruby);
FRLG `SYS_FLAGS = 0x800`.

## Gen 3 event → flag(s)

The item is already granted by `apply_event()`. The **primary** flag is the
`ENABLE_SHIP_*` gate the sailor checks (RS Eon uses `FLAG_SYS_HAS_EON_TICKET`).
The `RECEIVED_*` flag is what the real Mystery Gift also sets; listed as a
candidate secondary flag to set if the primary alone proves insufficient in
mGBA.

| Event | Game | Primary flag | id | Secondary candidate | id |
|-------|------|--------------|----|---------------------|----|
| Eon Ticket → Latias/Latios | Ruby/Sapphire | `FLAG_SYS_HAS_EON_TICKET` | 0x853 | — | — |
| Eon Ticket → Latias/Latios | Emerald | `FLAG_ENABLE_SHIP_SOUTHERN_ISLAND` | 0x8B3 | — | — |
| Mystic Ticket → Lugia & Ho-Oh | Emerald | `FLAG_ENABLE_SHIP_NAVEL_ROCK` | 0x8E0 | `FLAG_RECEIVED_MYSTIC_TICKET` | 0x13B |
| Mystic Ticket → Lugia & Ho-Oh | FRLG | `FLAG_ENABLE_SHIP_NAVEL_ROCK` | 0x84A | `FLAG_RECEIVED_MYSTIC_TICKET` | 0x2A8 |
| Aurora Ticket → Deoxys | Emerald | `FLAG_ENABLE_SHIP_BIRTH_ISLAND` | 0x8D5 | `FLAG_RECEIVED_AURORA_TICKET` | 0x13A |
| Aurora Ticket → Deoxys | FRLG | `FLAG_ENABLE_SHIP_BIRTH_ISLAND` | 0x84B | `FLAG_RECEIVED_AURORA_TICKET` | 0x2A7 |
| Old Sea Map → Mew | Emerald | `FLAG_ENABLE_SHIP_FARAWAY_ISLAND` | 0x8D6 | `FLAG_RECEIVED_OLD_SEA_MAP` | 0x13C |

Notes:
- Emerald `SYSTEM_FLAGS = 0x860`; `FLAG_ENABLE_SHIP_*` = base + {Southern 0x53,
  Birth 0x75, Faraway 0x76, Navel 0x80}.
- FRLG `SYS_FLAGS = 0x800`; Navel +0x4A, Birth +0x4B. FRLG has no Southern
  Island / Old Sea Map events (matches the existing `events.c` game masks).
- Ruby/Sapphire: Eon Ticket only. No Emerald-style `ENABLE_SHIP` symbol; the
  ferry checks `FLAG_SYS_HAS_EON_TICKET` (0x800 + 0x53 = 0x853).

## Gen 2 (Crystal) — GS Ball → Celebi

TODO (Gen 2 phase): source the event flag from `pret/pokecrystal`. The GS Ball
event (give to Kurt → Ilex Forest shrine) is gated by an event flag plus story
progress; the flag id + Crystal SRAM event-flags offset go here once confirmed
by save-diff against the repo's Crystal saves.

## Verification status

- **Flag API (Gen 2 + Gen 3):** round-trips through save + reload in
  `tests/pksav_flag_test.c`.
- **Events wiring:** `tests/events_apply_test.c` drives the real `apply_event`
  and confirms Old Sea Map (Emerald) sets `FLAG_ENABLE_SHIP_FARAWAY_ISLAND`
  (0x8D6), idempotently.
- **In-game (BizHawk / mGBA core):** `tools/verify_events.lua` loads an edited
  save into a real Pokémon Emerald and reads the LIVE `gSaveBlock1` flags the
  game loaded. All four Emerald events confirmed: applying them flips the exact
  ferry-enable flags the sailor scripts gate on, from clear → SET, with
  `FLAG_SYS_GAME_CLEAR` intact (proves our postgame save loaded, not a new game).

| Event / game | Code test | In-game (Emerald engine) |
|--------------|-----------|--------------------------|
| Old Sea Map / Emerald | ✓ | ✓ 0x8D6 clear→SET |
| Eon / Emerald | ✓ | ✓ 0x8B3 SET |
| Mystic / Emerald | ✓ | ✓ 0x8E0 clear→SET |
| Aurora / Emerald | ✓ | ✓ 0x8D5 clear→SET |
| Eon / RS, Mystic+Aurora / FRLG | ✓ (same code path, decomp flags) | pending RS/FRLG ROM |
| GS Ball / Crystal | item only (flag deferred) | pending Crystal ROM |

Emerald is fully verified in the game engine. FRLG/RS use the identical
`apply_event` → flag-API path with decomp-sourced flag ids, so they are verified
by construction pending those ROMs.

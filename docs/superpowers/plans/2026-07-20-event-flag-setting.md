# Event-Flag Setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Set the required in-game event flag(s) — in addition to granting the ticket item — for every Events entry, so re-issued distribution events actually become reachable in-game.

**Architecture:** Add a small, focused event-flag get/set API to the vendored PKSav (Gen 2 flat-buffer flags; Gen 3 SaveBlock1 flags reached via the decrypted, unshuffled save slot). The app's `events.c` names the flag id(s) per event and calls the API after the existing item grant. Exact flag ids/offsets are proven by save-diff against a legit before/after pair, cross-checked with the pret decompilation — never taken from memory.

**Tech Stack:** C (C11), MinGW-W64 gcc 13.2 (`C:\Strawberry\c\bin`), CMake + Ninja, PKSav (vendored in `deps/pksav`), mGBA (`C:\Program Files\mGBA\mGBA.exe`) for in-game verification.

## Global Constraints

- Feature branch: `event-flags` (already created off `gen3-trade`).
- All PKSav changes live in the vendored `deps/pksav`; keep them minimal and self-contained (candidate to upstream later).
- Flag values are **discovered empirically** (save-diff) and cross-checked against pret decomp constants. Do not hardcode a flag value that hasn't been diff-confirmed; where a task needs an as-yet-unconfirmed value, it consumes the value produced by Task 4.
- Writes must be **idempotent** (setting a set flag is a no-op) and must **refuse** (return an error, write nothing) on an unrecognized save-type/layout.
- The existing auto-backup on save stays. The Events feature stays labelled **experimental** in the readme/UI until an event is verified end-to-end (Task 6).
- Build compiler is gcc (a prior fork commit fixed GCC-14 build breaks); keep new code `-Wall -Wextra` clean.
- Test saves for Gen 1/2 already exist in `saves/`. **Gen 3 ROMs and before/after reference saves are supplied by Jeff** and are prerequisites for Tasks 3, 4 (Gen 3 rows), 5, 6.

## File Structure

- `deps/pksav/include/pksav/gen2/save.h` — Gen 2 event-flag API declarations + Crystal flag-base constant.
- `deps/pksav/lib/gen2/save.c` — Gen 2 event-flag get/set implementation.
- `deps/pksav/include/pksav/gba/save.h` — Gen 3 event-flag API declarations + per-game flag-base constants.
- `deps/pksav/lib/gba/save.c` — Gen 3 event-flag get/set implementation (+ SaveBlock1→section mapping helper).
- `include/events.h` — extend `struct pkmn_event` with a small fixed-size flag-id list.
- `src/events.c` — populate flag ids per event; set them in `apply_event()`.
- `tests/pksav_flag_test.c` — standalone assert-based round-trip test for the flag API.
- `tools/save_flag_diff.sh` — helper that byte-diffs two saves' decrypted flag region (Task 3 discovery + Task 6 acceptance).
- `docs/superpowers/specs/2026-07-20-event-flag-setting-design.md` — the spec (already committed).

---

### Task 1: Build PKSav locally and stand up the test harness

**Files:**
- Create: `tests/pksav_flag_test.c`

**Interfaces:**
- Produces: a repeatable build+run recipe used by every later task. No app symbols yet.

- [ ] **Step 1: Build the vendored PKSav static lib**

Run:
```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader/deps/pksav"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPKSAV_ENABLE_TESTS=OFF -DPKSAV_ENABLE_APPS=OFF -DPKSAV_ENABLE_DOCS=OFF
cmake --build build
```
Expected: `build/` produced with the PKSav static library (`build/lib/**/libpksav.a` or `pksav.lib`). Note the exact lib path for the compile step.

- [ ] **Step 2: Write a smoke test that only loads an existing save**

`tests/pksav_flag_test.c`:
```c
/* Standalone PKSav flag-API test. Plain asserts; returns nonzero on failure. */
#include <pksav.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

static int test_load_crystal(void)
{
    struct pksav_gen2_save save;
    enum pksav_error err = pksav_gen2_load_save_from_file(
        "saves/Pokemon - Crystal Version.sav", &save);
    CHECK(err == PKSAV_ERROR_NONE, "load crystal save");
    pksav_gen2_free_save(&save);
    printf("test_load_crystal PASS\n");
    return 0;
}

int main(void)
{
    if (test_load_crystal()) return 1;
    printf("ALL PASS\n");
    return 0;
}
```

- [ ] **Step 3: Compile and run the smoke test**

Run (adjust `-L`/lib name to the path from Step 1):
```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader"
gcc tests/pksav_flag_test.c -I deps/pksav/include -I deps/pksav/build/include \
  -L deps/pksav/build/lib -lpksav -o tests/flag_test.exe
./tests/flag_test.exe
```
Expected: `test_load_crystal PASS` then `ALL PASS`, exit 0. If the Crystal loader reports a save-type mismatch, try `saves/pk-crystal.sav`.

- [ ] **Step 4: Commit**

```bash
git add tests/pksav_flag_test.c
git commit -m "test: standalone PKSav flag-test harness (load smoke test)"
```

---

### Task 2: Gen 2 event-flag API in PKSav

**Files:**
- Modify: `deps/pksav/include/pksav/gen2/save.h`
- Modify: `deps/pksav/lib/gen2/save.c`
- Modify: `tests/pksav_flag_test.c`

**Interfaces:**
- Produces:
  ```c
  #define PKSAV_GEN2_CRYSTAL_EVENT_FLAGS_OFFSET /* diff-confirmed offset, see below */
  enum pksav_error pksav_gen2_save_get_event_flag(const struct pksav_gen2_save* p_save, uint16_t flag_id, bool* p_out);
  enum pksav_error pksav_gen2_save_set_event_flag(struct pksav_gen2_save* p_save, uint16_t flag_id, bool value);
  ```
  Flags are a bit array based at `PKSAV_GEN2_CRYSTAL_EVENT_FLAGS_OFFSET` in the active save buffer: byte = base + flag_id/8, bit = flag_id%8.

- [ ] **Step 1: Confirm the Crystal event-flags offset**

Source the `sEventFlags` (a.k.a. `wEventFlags`) SRAM offset for Pokémon Crystal from the `pret/pokecrystal` decompilation (`ram.asm` / `sram.asm`) and record it as `PKSAV_GEN2_CRYSTAL_EVENT_FLAGS_OFFSET`. Cross-check by confirming the byte lies inside PKSav's active Gen 2 save buffer bounds. Note: only Crystal is in scope (GS Ball).

- [ ] **Step 2: Write the failing round-trip test**

Add to `tests/pksav_flag_test.c` (and call from `main`):
```c
static int test_gen2_flag_roundtrip(void)
{
    const uint16_t TEST_FLAG = 0x10; /* arbitrary in-range flag for the mechanism test */
    struct pksav_gen2_save save;
    enum pksav_error err = pksav_gen2_load_save_from_file("saves/Pokemon - Crystal Version.sav", &save);
    CHECK(err == PKSAV_ERROR_NONE, "load crystal");

    bool before = true;
    CHECK(pksav_gen2_save_get_event_flag(&save, TEST_FLAG, &before) == PKSAV_ERROR_NONE, "get flag");
    CHECK(pksav_gen2_save_set_event_flag(&save, TEST_FLAG, true) == PKSAV_ERROR_NONE, "set flag");
    bool after = false;
    CHECK(pksav_gen2_save_get_event_flag(&save, TEST_FLAG, &after) == PKSAV_ERROR_NONE, "get flag 2");
    CHECK(after == true, "flag reads back set");
    /* idempotent */
    CHECK(pksav_gen2_save_set_event_flag(&save, TEST_FLAG, true) == PKSAV_ERROR_NONE, "set again");

    pksav_gen2_free_save(&save);
    printf("test_gen2_flag_roundtrip PASS\n");
    return 0;
}
```

- [ ] **Step 3: Run it and confirm it fails to compile/link**

Run the Task 1 Step 3 compile command.
Expected: link error — `undefined reference to pksav_gen2_save_get_event_flag`.

- [ ] **Step 4: Declare the API in the Gen 2 header**

In `deps/pksav/include/pksav/gen2/save.h`, before the closing `extern "C"`/guard, add the constant and two declarations exactly as in the Interfaces block above. Put a short comment noting the offset source (pokecrystal decomp) and that only Crystal is supported.

- [ ] **Step 5: Implement in `lib/gen2/save.c`**

The Gen 2 internal struct (`lib/gen2/save_internal.h`, already included by this file) exposes the active buffer via `p_internal`. Add:
```c
/* Event-flag bit array. Only Crystal is supported (GS Ball). */
enum pksav_error pksav_gen2_save_get_event_flag(
    const struct pksav_gen2_save* p_save, uint16_t flag_id, bool* p_out)
{
    if (p_save == NULL || p_out == NULL) return PKSAV_ERROR_NULL_POINTER;
    if (p_save->save_type != PKSAV_GEN2_SAVE_TYPE_CRYSTAL) return PKSAV_ERROR_INVALID_SAVE;
    const struct pksav_gen2_save_internal* p_internal = p_save->p_internal;
    const uint8_t* p_flags = p_internal->p_raw_save + PKSAV_GEN2_CRYSTAL_EVENT_FLAGS_OFFSET;
    *p_out = (p_flags[flag_id >> 3] >> (flag_id & 7)) & 1u;
    return PKSAV_ERROR_NONE;
}

enum pksav_error pksav_gen2_save_set_event_flag(
    struct pksav_gen2_save* p_save, uint16_t flag_id, bool value)
{
    if (p_save == NULL) return PKSAV_ERROR_NULL_POINTER;
    if (p_save->save_type != PKSAV_GEN2_SAVE_TYPE_CRYSTAL) return PKSAV_ERROR_INVALID_SAVE;
    struct pksav_gen2_save_internal* p_internal = p_save->p_internal;
    uint8_t* p_flags = p_internal->p_raw_save + PKSAV_GEN2_CRYSTAL_EVENT_FLAGS_OFFSET;
    uint8_t mask = (uint8_t)(1u << (flag_id & 7));
    if (value) p_flags[flag_id >> 3] |= mask;
    else       p_flags[flag_id >> 3] &= (uint8_t)~mask;
    return PKSAV_ERROR_NONE;
}
```
Confirm the exact internal struct/field names in `lib/gen2/save_internal.h` (`p_raw_save`) and the exact error enum names in `include/pksav/error.h` (use whatever the enum actually defines for null-pointer / invalid-save; adjust names to match).

- [ ] **Step 6: Rebuild PKSav, recompile the test, run it**

Run:
```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader/deps/pksav" && cmake --build build
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader"
gcc tests/pksav_flag_test.c -I deps/pksav/include -I deps/pksav/build/include -L deps/pksav/build/lib -lpksav -o tests/flag_test.exe && ./tests/flag_test.exe
```
Expected: `test_gen2_flag_roundtrip PASS`, `ALL PASS`, exit 0.

- [ ] **Step 7: Commit**

```bash
git add deps/pksav/include/pksav/gen2/save.h deps/pksav/lib/gen2/save.c tests/pksav_flag_test.c
git commit -m "feat(pksav): Gen 2 (Crystal) event-flag get/set API"
```

---

### Task 3: Gen 3 event-flag API in PKSav

**Files:**
- Modify: `deps/pksav/include/pksav/gba/save.h`
- Modify: `deps/pksav/lib/gba/save.c`
- Modify: `tests/pksav_flag_test.c`

**Prerequisite:** a Gen 3 save file (any of RS / Emerald / FRLG) from Jeff, placed in `saves/`. If none is present yet, implement Steps 1–5 and defer the run (Step 6) until a save exists.

**Interfaces:**
- Produces:
  ```c
  /* SaveBlock1 flag-array base per game (bytes), diff-confirmed against decomp: */
  #define PKSAV_GBA_RS_EVENT_FLAGS_OFFSET      0x1220
  #define PKSAV_GBA_EMERALD_EVENT_FLAGS_OFFSET 0x1270
  #define PKSAV_GBA_FRLG_EVENT_FLAGS_OFFSET    0x0EE0
  enum pksav_error pksav_gba_save_get_flag(const struct pksav_gba_save* p_save, uint16_t flag_id, bool* p_out);
  enum pksav_error pksav_gba_save_set_flag(struct pksav_gba_save* p_save, uint16_t flag_id, bool value);
  ```
  SaveBlock1 is stored as sections 1–4 of `p_internal->unshuffled_save_slot`, each holding `PKSAV_GBA_SAVE_SECTION_SIZE_BYTES` (3968) data bytes. A SaveBlock1 byte offset `O` maps to `sections_arr[1 + O/3968].data8[O % 3968]`. Flag byte offset = `flag_base(save_type) + flag_id/8`, bit = `flag_id%8`.

- [ ] **Step 1: Add the per-game constants + declarations to `gba/save.h`**

Insert the three `_EVENT_FLAGS_OFFSET` defines and the two function declarations from the Interfaces block. Comment that offsets are decomp-sourced (pokeruby/pokeemerald/pokefirered) and diff-confirmed in Task 4.

- [ ] **Step 2: Write the SaveBlock1→section mapping helper in `lib/gba/save.c`**

`lib/gba/save.c` already includes `gba/save_internal.h` (defines `struct pksav_gba_save_internal`, `union pksav_gba_save_slot`, `PKSAV_GBA_SAVE_SECTION_SIZE_BYTES`). Add a static helper:
```c
/* Return a pointer to SaveBlock1 byte `offset` within the unshuffled slot, or NULL. */
static uint8_t* gba_saveblock1_byte(struct pksav_gba_save_internal* p_internal, size_t offset)
{
    size_t section = 1 + (offset / PKSAV_GBA_SAVE_SECTION_SIZE_BYTES);
    size_t in_off  = offset % PKSAV_GBA_SAVE_SECTION_SIZE_BYTES;
    if (section > 4) return NULL; /* out of SaveBlock1 range */
    return &p_internal->unshuffled_save_slot.sections_arr[section].data8[in_off];
}

static size_t gba_event_flags_base(enum pksav_gba_save_type t)
{
    switch (t) {
        case PKSAV_GBA_SAVE_TYPE_RS:      return PKSAV_GBA_RS_EVENT_FLAGS_OFFSET;
        case PKSAV_GBA_SAVE_TYPE_EMERALD: return PKSAV_GBA_EMERALD_EVENT_FLAGS_OFFSET;
        case PKSAV_GBA_SAVE_TYPE_FRLG:    return PKSAV_GBA_FRLG_EVENT_FLAGS_OFFSET;
        default:                          return 0;
    }
}
```

- [ ] **Step 3: Implement get/set in `lib/gba/save.c`**

```c
enum pksav_error pksav_gba_save_get_flag(
    const struct pksav_gba_save* p_save, uint16_t flag_id, bool* p_out)
{
    if (p_save == NULL || p_out == NULL) return PKSAV_ERROR_NULL_POINTER;
    size_t base = gba_event_flags_base(p_save->save_type);
    if (base == 0) return PKSAV_ERROR_INVALID_SAVE;
    struct pksav_gba_save_internal* p_internal = p_save->p_internal;
    uint8_t* p_byte = gba_saveblock1_byte(p_internal, base + (flag_id >> 3));
    if (p_byte == NULL) return PKSAV_ERROR_INVALID_SAVE;
    *p_out = (*p_byte >> (flag_id & 7)) & 1u;
    return PKSAV_ERROR_NONE;
}

enum pksav_error pksav_gba_save_set_flag(
    struct pksav_gba_save* p_save, uint16_t flag_id, bool value)
{
    if (p_save == NULL) return PKSAV_ERROR_NULL_POINTER;
    size_t base = gba_event_flags_base(p_save->save_type);
    if (base == 0) return PKSAV_ERROR_INVALID_SAVE;
    struct pksav_gba_save_internal* p_internal = p_save->p_internal;
    uint8_t* p_byte = gba_saveblock1_byte(p_internal, base + (flag_id >> 3));
    if (p_byte == NULL) return PKSAV_ERROR_INVALID_SAVE;
    uint8_t mask = (uint8_t)(1u << (flag_id & 7));
    if (value) *p_byte |= mask;
    else       *p_byte &= (uint8_t)~mask;
    return PKSAV_ERROR_NONE;
}
```
Verify field name `unshuffled_save_slot` and that `p_internal` is assignable from the `void*` (cast if the compiler warns). Match real error-enum names.

- [ ] **Step 4: Add a Gen 3 round-trip test**

Mirror `test_gen2_flag_roundtrip` for Gen 3 using `pksav_gba_load_save_from_file` on the provided Gen 3 save, an arbitrary in-range `flag_id`, `pksav_gba_save_get_flag`/`set_flag`, asserting read-back and idempotency. Call it from `main`.

- [ ] **Step 5: Rebuild PKSav + recompile test**

```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader/deps/pksav" && cmake --build build
```

- [ ] **Step 6: Run (requires a Gen 3 save)**

```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader"
gcc tests/pksav_flag_test.c -I deps/pksav/include -I deps/pksav/build/include -L deps/pksav/build/lib -lpksav -o tests/flag_test.exe && ./tests/flag_test.exe
```
Expected: Gen 3 round-trip PASS. (Defer if no Gen 3 save yet.)

- [ ] **Step 7: Commit**

```bash
git add deps/pksav/include/pksav/gba/save.h deps/pksav/lib/gba/save.c tests/pksav_flag_test.c
git commit -m "feat(pksav): Gen 3 (GBA) SaveBlock1 event-flag get/set API"
```

---

### Task 4: Discover the exact event flags per event (save-diff oracle)

**Files:**
- Create: `tools/save_flag_diff.sh`
- Create: `docs/superpowers/event-flags.md` (the confirmed flag table — the data Task 5 consumes)

**Prerequisite:** per event, a before/after reference save pair from Jeff (after = event flag legitimately set), placed in `saves/refs/`.

**Interfaces:**
- Produces: `docs/superpowers/event-flags.md` — a table mapping each event (per game) to the confirmed flag id(s), each with the decomp constant that explains it.

- [ ] **Step 1: Write the diff helper**

`tools/save_flag_diff.sh` loads two saves, dumps the decrypted flag region for each (a tiny C dumper compiled on the fly, or reuse the test harness with a `--dump` mode), and prints byte offsets + changed bits between them. For Gen 3 it must dump the *decrypted, unshuffled* SaveBlock1 flag region (raw file bytes are encrypted/shuffled and will not diff meaningfully); reuse `pksav_gba_save_get_flag` over a flag-id sweep instead of raw bytes.

- [ ] **Step 2: Diff each pair and record the changed flag(s)**

For each event/game, run the helper over the before/after pair. Record every flag id whose bit flipped. Expect possibly more than one per event (e.g. a ferry-enable plus a map-created flag).

- [ ] **Step 3: Cross-check against decomp**

For each changed flag id, find the matching named constant in the relevant decomp (`FLAG_...` in pokeemerald/pokefirered/pokeruby; the event flag in pokecrystal for GS Ball). Confirm the name matches the event (e.g. Southern Island / Navel Rock / Birth Island / Faraway Island / GS Ball). Discard incidental flags (playtime/RTC-driven) that are not part of the event gate.

- [ ] **Step 4: Write `docs/superpowers/event-flags.md`**

Record, per event and game: the confirmed flag id(s), the decomp constant name, and a one-line note. This file is the single source of truth Task 5 reads.

- [ ] **Step 5: Commit**

```bash
git add tools/save_flag_diff.sh docs/superpowers/event-flags.md
git commit -m "docs: diff-confirmed event-flag ids per event/game"
```

---

### Task 5: Wire flags into the Events feature

**Files:**
- Modify: `include/events.h`
- Modify: `src/events.c`

**Interfaces:**
- Consumes: the confirmed flag ids from `docs/superpowers/event-flags.md`; the PKSav APIs from Tasks 2–3.
- Produces: `apply_event()` grants the item **and** sets the event's flag(s); still returns `error_none`/typed error.

- [ ] **Step 1: Extend `struct pkmn_event`**

In `include/events.h`, add a small fixed-size flag list to the struct:
```c
#define PKMN_EVENT_MAX_FLAGS 3
struct pkmn_event
{
    SaveGenerationType gen;
    uint8_t save_type_mask;
    uint16_t item_id;
    uint16_t flag_ids[PKMN_EVENT_MAX_FLAGS]; /* 0-terminated list of event flags to set */
    const char *ticket;
    const char *pokemon;
    const char *location;
};
```

- [ ] **Step 2: Populate flag ids in the `EVENTS[]` table**

In `src/events.c`, add the `flag_ids` initializer to each row using the confirmed ids from `event-flags.md` (e.g. `{FLAG_A, FLAG_B, 0}`; unused slots `0`). Keep the existing item ids and strings.

- [ ] **Step 3: Set flags in `apply_event()`**

After the existing item grant succeeds, loop the event's non-zero `flag_ids` and call the matching PKSav setter (`pksav_gen2_save_set_event_flag` for Gen 2, `pksav_gba_save_set_flag` for Gen 3). If any setter returns non-`PKSAV_ERROR_NONE`, return a typed error and do not report success. Item grant + flag set are both idempotent, so re-applying is safe.

- [ ] **Step 4: Build the app**

Build PKSav (if not current) then the app:
```bash
cd "C:/Users/Jeff/Documents/Projects/Pokemon/pokerom-trader"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```
Expected: `pokeromtrader` builds clean (no new warnings). If the app build normally uses the `Makefile`, use the same target CI uses instead.

- [ ] **Step 5: Commit**

```bash
git add include/events.h src/events.c
git commit -m "feat(events): set in-game event flags alongside ticket grant"
```

---

### Task 6: End-to-end verification and de-experimental

**Files:**
- Modify: `readme.md`
- Modify: `src/events.c` or `src/screens/EventsScreen.c` (experimental label copy, if present)

**Prerequisite:** Gen 3 ROMs from Jeff for the in-game sanity pass.

- [ ] **Step 1: Byte-level acceptance per event**

For each event: run the app (or a small driver) to apply the event on a "before" save; confirm with `tools/save_flag_diff.sh` that the output matches the "after" reference in the flag region, and that nothing changes outside {flag byte(s), item bag, section checksums}. Record pass/fail per event.

- [ ] **Step 2: In-game sanity in mGBA**

For at least one Gen 3 event (e.g. Old Sea Map → Mew, Emerald) and the GS Ball (Crystal): load the edited save + ROM in `C:\Program Files\mGBA\mGBA.exe` and confirm the sailor/NPC offers the trip / the event is reachable. Note results.

- [ ] **Step 3: Update the readme**

In `readme.md`, move Events from the "may also need an event flag" caveat toward "working": state that verified events now set the required flag(s), and list which events are verified vs. still experimental. Keep unverified events labelled experimental.

- [ ] **Step 4: Commit**

```bash
git add readme.md src/events.c
git commit -m "docs+events: verify event flags end-to-end; update status"
```

- [ ] **Step 5: Push the branch and open a PR against the fork**

```bash
git push -u origin event-flags
gh pr create --repo sonofskywalker3/pokerom-trader --base gen3-trade --title "Set in-game event flags for re-issued events" --fill
```

---

## Self-Review

- **Spec coverage:** item+flag for all events (Tasks 5, plus 2/3 APIs) ✓; save-diff oracle (Task 4) ✓; decomp cross-check (Task 4 Step 3) ✓; PKSav flag API architecture (Tasks 2–3) ✓; mGBA verification (Task 6) ✓; idempotency/refuse-on-unknown (API impls + Global Constraints) ✓; backup retained / experimental label (Global Constraints, Task 6) ✓; inputs-from-Jeff called out (Global Constraints, task prerequisites) ✓.
- **Placeholder scan:** the only deferred values are flag ids/offsets, which are *deliberately* discovered in Task 4 and consumed in Task 5 — a defined data dependency, not a TODO. The Gen 3 base offsets in Task 3 are decomp starting points, explicitly diff-confirmed in Task 4.
- **Type consistency:** `pksav_gen2_save_set_event_flag` / `pksav_gba_save_set_flag` names used identically in Tasks 2/3 (definition) and Task 5 (call); `flag_ids` field and `PKMN_EVENT_MAX_FLAGS` consistent between Task 5 Steps 1–3. Error-enum names flagged for confirmation against `pksav/error.h` at implementation time.

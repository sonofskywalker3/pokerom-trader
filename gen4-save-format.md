# Gen 4 (NDS) save format — validated constants

Empirically verified against five real English retail saves (Diamond, Pearl,
Platinum, HeartGold, SoulSilver), cross-checked with PKHeX / Project Pokémon /
Bulbapedia. Where the public docs disagreed with the real bytes, the real bytes
win — those cases are called out. All values little-endian.

## File

- Total size **0x80000** (512 KB). Two **0x40000** partitions (hot-swap pair):
  partition 0 at `0x00000`, partition 1 at `0x40000`.
- Each partition: a **general (small)** block then a **storage (big)** block.
  Both blocks exist in *both* partitions; the active copy of each is chosen
  **independently** by its footer counter (they can live in different
  partitions — verified: Diamond/Pearl/HeartGold active in partition 1,
  Platinum/SoulSilver active in partition 0).

## Per-game geometry (validated)

| Game | general_size | storage_size | storage_start (in partition) | party_off | count_off | CRC footer skip |
|------|-------------|-------------|------------------------------|-----------|-----------|-----------------|
| DP   | 0xC100 | 0x121E0 | 0xC100 | 0x98 | 0x94 | **0x14** |
| Pt   | 0xCF2C | 0x121E4 | 0xCF2C | 0xA0 | 0x9C | **0x14** |
| HGSS | 0xF628 | 0x12310 | 0xF700 | 0x98 | 0x94 | **0x10** |

- Game is detected by the (general_size, storage_size) signature, confirmed by a
  `0x20060623` footer magic at `general_size - 8` in partition 0.
- HGSS storage starts at `0xF700`, not `0xF628` (there is a 0xD8 gap after the
  general block). Bulbapedia had this right; a naive "storage = general_size"
  is wrong for HGSS.

## Footer (last 0x14 bytes of each block)

Relative to `block_end = block_start + block_size`:

| Offset | Field |
|--------|-------|
| block_end - 0x14 | u32 primary counter ("major") |
| block_end - 0x10 | u32 secondary counter ("minor" / save no.) |
| block_end - 0x0C | u32 block size |
| block_end - 0x08 | u32 magic `0x20060623` (intl/JP) / `0x20070903` (KOR) |
| block_end - 0x04 | u16 reserved |
| block_end - 0x02 | u16 **CRC-16** |

**Active-copy selection:** compare the two copies' primary counters, higher
wins; tie → compare secondary; tie → partition 0. Rollover: `0xFFFFFFFF` counts
as *older* unless the other is exactly `0xFFFFFFFE`.

## CRC-16 (validated on all 20 blocks)

CRC-16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, non-reflected, no final XOR.
CRC is the block's last 2 bytes (`block_end - 2`).

**Covered range = `[block_start, block_start + block_size - SKIP)`** where SKIP =
**0x14 for DP/Pt, 0x10 for HGSS**. This per-game skip is the one thing the
research got wrong (it claimed a flat 0x14); real HGSS saves only validate with
0x10. Getting this wrong silently fails every HGSS write.

## PK4 (136-byte stored / 236-byte party)

Header (unencrypted): `PID` u32 @0x00, `sanity` u16 @0x04, `checksum` u16 @0x06.
Blocks A/B/C/D each 32 bytes at 0x08/0x28/0x48/0x68, encrypted + shuffled.
Party form adds a 100-byte stats region at 0x88 (encrypted separately).

**Decrypt = decrypt-then-unshuffle:**
1. `sv = (PID >> 13) & 0x1F`.
2. XOR-decrypt the 64 u16 words at 0x08..0x87 with LCRNG seeded by `checksum`.
3. (party) XOR-decrypt the 50 u16 words at 0x88..0xEB with LCRNG seeded by `PID`.
4. Un-shuffle the four 32-byte blocks: `logical[i] = physical[ order[sv][i] ]`.

LCRNG: `seed = seed*0x41C64E6D + 0x6073` (mod 2^32); key = `seed >> 16`; advance
before each word; words ascending.

Shuffle table `order[24][4]` (sv uses `sv % 24`, but sv is 0..31 so 24..31 reuse
0..7). Validated: the in-mon checksum recomputes correctly for all 30 party mons
across all 5 saves.

**In-mon checksum @0x06** = sum of the 64 decrypted u16 words 0x08..0x87 mod
0x10000. Both validates the mon and seeds step 2.

**Encrypt** is the exact reverse: re-shuffle (`physical[order[sv][i]] = logical[i]`)
then run the same XOR cipher (symmetric).

## Key PK4 field offsets (decrypted, un-shuffled)

- Species (National Dex 1..493) u16 @0x08. **No internal-index remap** (unlike Gen 3).
- Held item u16 @0x0A; TID u16 @0x0C; SID u16 @0x0E; EXP u32 @0x10.
- Friendship u8 @0x14; ability u8 @0x15; language u8 @0x17.
- EVs 6×u8 @0x18 (HP,Atk,Def,Spe,SpA,SpD).
- Moves 4×u16 @0x28; PP 4×u8 @0x30; PP-ups 4×u8 @0x34.
- IV32 u32 @0x38: HP0-4 Atk5-9 Def10-14 Spe15-19 SpA20-24 SpD25-29, IsEgg bit30, IsNicknamed bit31.
- Nickname 11×u16 @0x48 (0xFFFF-terminated); origin/version u8 @0x5F.
- OT name 8×u16 @0x68 (0xFFFF-terminated).
- Met location: DP @0x80 (egg @0x7E); Pt/HGSS extended @0x46 (egg @0x44). Pal Park = 55 (0x37).
- Ball @0x83 (DP/Pt) / @0x86 (HGSS); met level bits0-6 + OT gender bit7 @0x84.
- Party stats (party form): level u8 @0x8C; cur HP u16 @0x8E; max/atk/def/spe/spa/spd u16 @0x90..0x9B.

## Storage / boxes (for slice 2)

18 boxes × 30 slots, each a 136-byte stored PK4.
- DP/Pt: current-box u32 @storage[0]; box data @storage[4]; box stride 0xFF0
  (packed). Box names @0x11EE4 (40 bytes/20 chars each); wallpaper @0x121B4.
- HGSS: box data @storage[0]; each box padded to 0x1000; current-box @storage[0x12000];
  box names @0x12008; wallpaper @0x122D8.

## Text encoding (validated)

16-bit codes, 0xFFFF terminator. Raw stored values index PKHeX's TableINT
directly (NOT the remapped internal indices some docs list). Western set is
contiguous: digits '0'..'9' @0x0121, 'A'..'Z' @0x012B, 'a'..'z' @0x0145; space
= 0x01DE. Punctuation is scattered (0x01A8..0x01D2). Verified: real-save OT and
nicknames ("Peter", "Anthony", "Statistics", "Cave Flash") decode cleanly.
Implemented in `gen4_pkmn.c` gen4_char().

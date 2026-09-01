/*
 * Gen 4 (NDS) save parser — file detection, partition/active-block selection,
 * CRC-16 footer validation, and party/box access. PKSav has no Gen 4 support;
 * this is a from-scratch parser validated against real DP/Pt/HGSS saves.
 * Distributed under the MIT License (MIT).
 */
#ifndef GEN4_SAVE_H
#define GEN4_SAVE_H

#include <pksav/error.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GEN4_SAVE_SIZE      0x80000 /* 512 KB raw */
#define GEN4_PARTITION_SIZE 0x40000
#define GEN4_PARTY_MAX      6
#define GEN4_NUM_BOXES      18
#define GEN4_BOX_SLOTS      30

enum gen4_game
{
    GEN4_GAME_NONE = 0,
    GEN4_GAME_DP,   /* Diamond / Pearl */
    GEN4_GAME_PT,   /* Platinum */
    GEN4_GAME_HGSS, /* HeartGold / SoulSilver */
};

/* Per-game geometry (all offsets validated against real saves). */
struct gen4_layout
{
    uint32_t general_size;
    uint32_t storage_size;
    uint32_t storage_start;      /* within a partition */
    uint32_t party_offset;       /* within the general block */
    uint32_t party_count_offset; /* within the general block */
    uint32_t crc_footer_skip;    /* bytes excluded from the CRC at block end */
    /* box storage (within the storage block) */
    uint32_t box_data_start;
    uint32_t box_stride;
    uint32_t box_name_start; /* first box-name string (40 bytes / 20 chars each) */
    uint32_t current_box_off; /* offset of the current-box index within storage */
    uint32_t trainer_offset; /* trainer block within the general block */
    uint32_t dex_offset;     /* Pokédex block within the general block */
};

/* Owns a heap copy of the whole 512 KB save; the *_base fields point at the
 * active (newest) copy of each block. Fits the app's SaveGeneration union. */
struct gen4_save
{
    enum gen4_game game;
    uint8_t *data;   /* malloc'd GEN4_SAVE_SIZE buffer (owned) */
    size_t size;
    uint32_t general_base; /* absolute offset of the active general block */
    uint32_t storage_base; /* absolute offset of the active storage block */
    struct gen4_layout layout;
};

/* True if the file at `path` looks like a Gen 4 save (size + footer magic). */
bool gen4_is_gen4_file(const char *path);

/* Load and select active blocks. Returns PKSAV_ERROR_NONE on success. On
 * failure, out->data is NULL. */
enum pksav_error gen4_load_save_from_file(const char *path, struct gen4_save *out);

/* Free the owned buffer and zero the struct. */
void gen4_free_save(struct gen4_save *save);

/* Party count (0..6). */
uint8_t gen4_party_count(const struct gen4_save *save);

/* Pointer to the raw 236-byte party slot i (i in 0..count-1), or NULL. */
const uint8_t *gen4_party_slot_raw(const struct gen4_save *save, int i);

/* Pointer to the raw 136-byte stored mon at (box, slot), or NULL. box in
 * 0..17, slot in 0..29. The mon is still encrypted+shuffled; decrypt with
 * gen4_pk4_decrypt (is_party=false) and check species != 0 for occupancy. */
const uint8_t *gen4_box_slot_raw(const struct gen4_save *save, int box, int slot);

/* Number of occupied slots in a box (species != 0 after decrypt). */
int gen4_box_occupied_count(const struct gen4_save *save, int box);

/* --- Mutable raw accessors (for edits/moves). These write into the active
 * block; the caller must gen4_save_save() afterward to recompute CRCs. --- */

/* Writable pointer to raw party slot i (i in 0..GEN4_PARTY_MAX-1), independent
 * of the current count (so a caller can append at index==count). NULL if oob. */
uint8_t *gen4_party_slot_raw_mut(struct gen4_save *save, int i);

/* Writable pointer to the raw 136-byte stored mon at (box, slot). NULL if oob. */
uint8_t *gen4_box_slot_raw_mut(struct gen4_save *save, int box, int slot);

/* Set the party count (0..GEN4_PARTY_MAX). */
void gen4_set_party_count(struct gen4_save *save, uint8_t count);

/* The game's currently-selected box index (0..17). */
int gen4_current_box(const struct gen4_save *save);

/* Decode box `box`'s name into ASCII. `out` must hold >= 21 bytes. Returns len. */
size_t gen4_box_name(const struct gen4_save *save, int box, char *out);

/* Trainer OT name (out >= 8 bytes) and 16-bit trainer ID from the general block. */
size_t gen4_trainer_name(const struct gen4_save *save, char *out);
uint16_t gen4_trainer_id(const struct gen4_save *save);

/* Mark National Dex number `dex` (1..493) as seen AND caught in the Pokédex.
 * Layout (validated on real DP/Pt/Pearl/HG/SS saves against PKHeX's Zukan4):
 * u32 magic 0xBEEFCAFE, caught bitfield @+0x04, seen bitfield @+0x44; bit
 * index = dex-1, LSB-first. Refuses to write (no-op) if the magic isn't there. */
void gen4_dex_set_seen_caught(struct gen4_save *save, uint16_t dex);
bool gen4_dex_get_seen(const struct gen4_save *save, uint16_t dex);
bool gen4_dex_get_caught(const struct gen4_save *save, uint16_t dex);

/* Swap the raw 236-byte party slots (a,ia) <-> (b,ib) — the whole Gen 4 <->
 * Gen 4 trade, since a PK4 is self-contained (encryption keyed by its own
 * PID/checksum) and identical across DP/Pt/HGSS. Indices must be within each
 * save's current party count. Returns false if either slot is out of range. */
bool gen4_swap_party_slots(struct gen4_save *a, int ia, struct gen4_save *b, int ib);

/* Recompute all footer CRCs and write the active copies back to `path`.
 * (Slice 3 — declared here for the module's public surface.) */
enum pksav_error gen4_save_save(const char *path, struct gen4_save *save);

#endif /* GEN4_SAVE_H */

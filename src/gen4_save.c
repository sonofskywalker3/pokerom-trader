/*
 * Gen 4 (NDS) save parser. See gen4_save.h.
 * Distributed under the MIT License (MIT).
 */
#include "gen4_save.h"
#include "gen4_pkmn.h" /* gen4_crc16_ccitt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GEN4_FOOTER_MAGIC     0x20060623u /* intl / Japanese */
#define GEN4_FOOTER_MAGIC_KOR 0x20070903u

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* Geometry per game. crc_footer_skip is the one non-obvious constant: DP/Pt
 * exclude 0x14 bytes from the block CRC, HGSS only 0x10 (verified on real saves). */
static const struct gen4_layout k_layouts[] = {
    [GEN4_GAME_DP] = {.general_size = 0xC100, .storage_size = 0x121E0, .storage_start = 0xC100,
                      .party_offset = 0x98, .party_count_offset = 0x94, .crc_footer_skip = 0x14,
                      .box_data_start = 4, .box_stride = 0xFF0,
                      .box_name_start = 0x11EE4, .current_box_off = 0, .trainer_offset = 0x64,
                      .dex_offset = 0x12DC},
    [GEN4_GAME_PT] = {.general_size = 0xCF2C, .storage_size = 0x121E4, .storage_start = 0xCF2C,
                      .party_offset = 0xA0, .party_count_offset = 0x9C, .crc_footer_skip = 0x14,
                      .box_data_start = 4, .box_stride = 0xFF0,
                      .box_name_start = 0x11EE4, .current_box_off = 0, .trainer_offset = 0x68,
                      .dex_offset = 0x1328},
    [GEN4_GAME_HGSS] = {.general_size = 0xF628, .storage_size = 0x12310, .storage_start = 0xF700,
                        .party_offset = 0x98, .party_count_offset = 0x94, .crc_footer_skip = 0x10,
                        .box_data_start = 0, .box_stride = 0x1000,
                        .box_name_start = 0x12008, .current_box_off = 0x12000, .trainer_offset = 0x64,
                        .dex_offset = 0x12B8},
};

static bool footer_magic_ok(uint32_t m)
{
    return m == GEN4_FOOTER_MAGIC || m == GEN4_FOOTER_MAGIC_KOR;
}

/* Identify the game from the (general_size, storage_size) signature: a valid
 * general footer (magic + matching size field) must sit at general_size-8 in
 * partition 0. */
static enum gen4_game detect_game(const uint8_t *data)
{
    const enum gen4_game games[] = {GEN4_GAME_DP, GEN4_GAME_PT, GEN4_GAME_HGSS};
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
    {
        const struct gen4_layout *L = &k_layouts[games[i]];
        uint32_t magic_off = L->general_size - 8;
        uint32_t size_off = L->general_size - 0x0C;
        if (footer_magic_ok(rd32(data + magic_off)) && rd32(data + size_off) == L->general_size)
        {
            return games[i];
        }
    }
    return GEN4_GAME_NONE;
}

/* Compare two block footer counters; true if copy `b` is newer than copy `a`.
 * Primary counter first, secondary (save number) as tiebreak. 0xFFFFFFFF is
 * treated as older (uninitialized) unless the other is exactly 0xFFFFFFFE. */
static bool footer_b_newer(const uint8_t *a_footer, const uint8_t *b_footer)
{
    const uint32_t a_major = rd32(a_footer + 0x00), b_major = rd32(b_footer + 0x00);
    const uint32_t a_minor = rd32(a_footer + 0x04), b_minor = rd32(b_footer + 0x04);
    const uint32_t pairs[2][2] = {{a_major, b_major}, {a_minor, b_minor}};
    for (int i = 0; i < 2; i++)
    {
        uint32_t x = pairs[i][0], y = pairs[i][1];
        if (x == y)
            continue;
        if (x == 0xFFFFFFFFu && y != 0xFFFFFFFEu)
            return true; /* a wrapped -> b newer */
        if (y == 0xFFFFFFFFu && x != 0xFFFFFFFEu)
            return false; /* b wrapped -> a newer */
        return y > x;
    }
    return false; /* tie -> keep primary (a) */
}

/* Absolute offset of the active copy of a block that lives at `block_off_in_part`
 * within each partition and is `block_size` long. */
static uint32_t select_active(const uint8_t *data, uint32_t block_off_in_part, uint32_t block_size)
{
    uint32_t base0 = block_off_in_part;
    uint32_t base1 = GEN4_PARTITION_SIZE + block_off_in_part;
    const uint8_t *f0 = data + base0 + block_size - 0x14;
    const uint8_t *f1 = data + base1 + block_size - 0x14;
    return footer_b_newer(f0, f1) ? base1 : base0;
}

static uint16_t block_crc(const struct gen4_save *s, uint32_t base, uint32_t size)
{
    return gen4_crc16_ccitt(s->data + base, size - s->layout.crc_footer_skip);
}

bool gen4_is_gen4_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz != GEN4_SAVE_SIZE)
    {
        fclose(f);
        return false;
    }
    uint8_t *buf = malloc(GEN4_SAVE_SIZE);
    if (!buf)
    {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);
    size_t got = fread(buf, 1, GEN4_SAVE_SIZE, f);
    fclose(f);
    bool ok = (got == GEN4_SAVE_SIZE) && (detect_game(buf) != GEN4_GAME_NONE);
    free(buf);
    return ok;
}

enum pksav_error gen4_load_save_from_file(const char *path, struct gen4_save *out)
{
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (!f)
        return PKSAV_ERROR_FILE_IO;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz != GEN4_SAVE_SIZE)
    {
        fclose(f);
        return PKSAV_ERROR_INVALID_SAVE;
    }
    uint8_t *buf = malloc(GEN4_SAVE_SIZE);
    if (!buf)
    {
        fclose(f);
        return PKSAV_ERROR_FILE_IO;
    }
    fseek(f, 0, SEEK_SET);
    size_t got = fread(buf, 1, GEN4_SAVE_SIZE, f);
    fclose(f);
    if (got != GEN4_SAVE_SIZE)
    {
        free(buf);
        return PKSAV_ERROR_FILE_IO;
    }

    enum gen4_game game = detect_game(buf);
    if (game == GEN4_GAME_NONE)
    {
        free(buf);
        return PKSAV_ERROR_INVALID_SAVE;
    }

    out->game = game;
    out->data = buf;
    out->size = GEN4_SAVE_SIZE;
    out->layout = k_layouts[game];
    out->general_base = select_active(buf, 0, out->layout.general_size);
    out->storage_base =
        select_active(buf, out->layout.storage_start, out->layout.storage_size);

    return PKSAV_ERROR_NONE;
}

void gen4_free_save(struct gen4_save *save)
{
    if (save && save->data)
    {
        free(save->data);
    }
    if (save)
    {
        memset(save, 0, sizeof(*save));
    }
}

uint8_t gen4_party_count(const struct gen4_save *save)
{
    if (!save || !save->data)
        return 0;
    uint8_t c = save->data[save->general_base + save->layout.party_count_offset];
    return c > GEN4_PARTY_MAX ? GEN4_PARTY_MAX : c;
}

const uint8_t *gen4_party_slot_raw(const struct gen4_save *save, int i)
{
    if (!save || !save->data || i < 0 || i >= gen4_party_count(save))
        return NULL;
    return save->data + save->general_base + save->layout.party_offset +
           (size_t)GEN4_PK4_PARTY_SIZE * i;
}

const uint8_t *gen4_box_slot_raw(const struct gen4_save *save, int box, int slot)
{
    if (!save || !save->data || box < 0 || box >= GEN4_NUM_BOXES ||
        slot < 0 || slot >= GEN4_BOX_SLOTS)
    {
        return NULL;
    }
    return save->data + save->storage_base + save->layout.box_data_start +
           (size_t)box * save->layout.box_stride +
           (size_t)slot * GEN4_PK4_STORED_SIZE;
}

uint8_t *gen4_party_slot_raw_mut(struct gen4_save *save, int i)
{
    if (!save || !save->data || i < 0 || i >= GEN4_PARTY_MAX)
        return NULL;
    return save->data + save->general_base + save->layout.party_offset +
           (size_t)GEN4_PK4_PARTY_SIZE * i;
}

uint8_t *gen4_box_slot_raw_mut(struct gen4_save *save, int box, int slot)
{
    if (!save || !save->data || box < 0 || box >= GEN4_NUM_BOXES ||
        slot < 0 || slot >= GEN4_BOX_SLOTS)
    {
        return NULL;
    }
    return save->data + save->storage_base + save->layout.box_data_start +
           (size_t)box * save->layout.box_stride +
           (size_t)slot * GEN4_PK4_STORED_SIZE;
}

void gen4_set_party_count(struct gen4_save *save, uint8_t count)
{
    if (!save || !save->data)
        return;
    if (count > GEN4_PARTY_MAX)
        count = GEN4_PARTY_MAX;
    save->data[save->general_base + save->layout.party_count_offset] = count;
}

int gen4_box_occupied_count(const struct gen4_save *save, int box)
{
    int count = 0;
    for (int slot = 0; slot < GEN4_BOX_SLOTS; slot++)
    {
        const uint8_t *raw = gen4_box_slot_raw(save, box, slot);
        if (!raw)
        {
            continue;
        }
        uint8_t dec[GEN4_PK4_STORED_SIZE];
        gen4_pk4_decrypt(raw, dec, false);
        if (gen4_pk4_species(dec) != 0)
        {
            count++;
        }
    }
    return count;
}

int gen4_current_box(const struct gen4_save *save)
{
    if (!save || !save->data)
    {
        return 0;
    }
    /* DP/Pt store a u32 at storage[0]; HGSS a byte at storage[0x12000]. Either
     * way the index (0..17) is the low byte. */
    uint8_t idx = save->data[save->storage_base + save->layout.current_box_off];
    return idx < GEN4_NUM_BOXES ? idx : 0;
}

size_t gen4_box_name(const struct gen4_save *save, int box, char *out)
{
    if (!save || !save->data || box < 0 || box >= GEN4_NUM_BOXES)
    {
        out[0] = '\0';
        return 0;
    }
    const uint8_t *field =
        save->data + save->storage_base + save->layout.box_name_start + (size_t)box * 40;
    return gen4_decode_text(field, 20, out);
}

size_t gen4_trainer_name(const struct gen4_save *save, char *out)
{
    if (!save || !save->data)
    {
        out[0] = '\0';
        return 0;
    }
    const uint8_t *field = save->data + save->general_base + save->layout.trainer_offset;
    return gen4_decode_text(field, 7, out);
}

uint16_t gen4_trainer_id(const struct gen4_save *save)
{
    if (!save || !save->data)
    {
        return 0;
    }
    return rd16(save->data + save->general_base + save->layout.trainer_offset + 0x10);
}

/* The dex block leads with this magic in every game (checked on all 5 real
 * saves); refusing to write without it protects against a layout mismatch. */
#define GEN4_DEX_MAGIC 0xBEEFCAFEu

void gen4_dex_set_seen_caught(struct gen4_save *save, uint16_t dex)
{
    if (!save || !save->data || dex < 1 || dex > GEN4_NATIONAL_DEX_MAX)
    {
        return;
    }
    uint8_t *block = save->data + save->general_base + save->layout.dex_offset;
    if (rd32(block) != GEN4_DEX_MAGIC)
    {
        return;
    }
    int bit = dex - 1;
    block[0x04 + (bit >> 3)] |= (uint8_t)(1u << (bit & 7)); /* caught */
    block[0x44 + (bit >> 3)] |= (uint8_t)(1u << (bit & 7)); /* seen */
}

bool gen4_dex_get_seen(const struct gen4_save *save, uint16_t dex)
{
    if (!save || !save->data || dex < 1 || dex > GEN4_NATIONAL_DEX_MAX)
    {
        return false;
    }
    const uint8_t *block = save->data + save->general_base + save->layout.dex_offset;
    if (rd32(block) != GEN4_DEX_MAGIC)
    {
        return false;
    }
    int bit = dex - 1;
    return (block[0x44 + (bit >> 3)] >> (bit & 7)) & 1;
}

bool gen4_dex_get_caught(const struct gen4_save *save, uint16_t dex)
{
    if (!save || !save->data || dex < 1 || dex > GEN4_NATIONAL_DEX_MAX)
    {
        return false;
    }
    const uint8_t *block = save->data + save->general_base + save->layout.dex_offset;
    if (rd32(block) != GEN4_DEX_MAGIC)
    {
        return false;
    }
    int bit = dex - 1;
    return (block[0x04 + (bit >> 3)] >> (bit & 7)) & 1;
}

bool gen4_swap_party_slots(struct gen4_save *a, int ia, struct gen4_save *b, int ib)
{
    if (ia < 0 || ia >= (int)gen4_party_count(a) ||
        ib < 0 || ib >= (int)gen4_party_count(b))
    {
        return false;
    }
    uint8_t *sa = gen4_party_slot_raw_mut(a, ia);
    uint8_t *sb = gen4_party_slot_raw_mut(b, ib);
    uint8_t tmp[GEN4_PK4_PARTY_SIZE];
    memcpy(tmp, sa, GEN4_PK4_PARTY_SIZE);
    memcpy(sa, sb, GEN4_PK4_PARTY_SIZE);
    memcpy(sb, tmp, GEN4_PK4_PARTY_SIZE);
    return true;
}

enum pksav_error gen4_save_save(const char *path, struct gen4_save *save)
{
    if (!save || !save->data)
        return PKSAV_ERROR_INVALID_SAVE;

    /* Recompute the active blocks' footer CRCs in place. The backup copies are
     * left untouched (the game overwrites them on its next save). */
    uint16_t gcrc = block_crc(save, save->general_base, save->layout.general_size);
    wr16(save->data + save->general_base + save->layout.general_size - 2, gcrc);
    uint16_t scrc = block_crc(save, save->storage_base, save->layout.storage_size);
    wr16(save->data + save->storage_base + save->layout.storage_size - 2, scrc);

    FILE *f = fopen(path, "wb");
    if (!f)
        return PKSAV_ERROR_FILE_IO;
    size_t wrote = fwrite(save->data, 1, save->size, f);
    fclose(f);
    return (wrote == save->size) ? PKSAV_ERROR_NONE : PKSAV_ERROR_FILE_IO;
}

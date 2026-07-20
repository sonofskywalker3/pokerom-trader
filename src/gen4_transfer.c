/*
 * Gen 3 -> Gen 4 forward transfer (Pal Park). See gen4_transfer.h and
 * gen4-save-format.md section 12.
 * Distributed under the MIT License (MIT).
 */
#include "gen4_transfer.h"
#include "pksavhelper.h"   /* PKMN/TRAINER text lengths */
#include "gen3_species.h"  /* gen3_internal_to_national */
#include "gen4_save.h"     /* mutable slot accessors, party count */
#include "gen4_pkmn.h"     /* PK4 encode + text encode */
#include "gen4_stats.h"    /* ability id, species name, level-from-exp, party stats */
#include <pksav.h>
#include <string.h>
#include <time.h>

static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Case-insensitive ASCII compare (mingw lacks a portable strcasecmp here). */
static int ci_equal(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

/* Gen 3 item index -> Gen 4 item id (0 = no Gen 4 equivalent, item dropped).
 * From PKHeX's ItemConverter Item3to4 table; indices beyond the table drop. */
static const uint16_t item3to4[] = {
    /*   0 */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    /*  12 */ 12, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
    /*  24 */ 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
    /*  36 */ 40, 41, 42, 65, 66, 67, 68, 69, 43, 44, 70, 71,
    /*  48 */ 72, 73, 74, 75, 0, 0, 0, 0, 0, 0, 0, 0,
    /*  60 */ 0, 0, 0, 45, 46, 47, 48, 49, 50, 51, 52, 53,
    /*  72 */ 0, 55, 56, 57, 58, 59, 60, 61, 63, 64, 0, 76,
    /*  84 */ 77, 78, 79, 0, 0, 0, 0, 0, 0, 80, 81, 82,
    /*  96 */ 83, 84, 85, 0, 0, 0, 0, 86, 87, 0, 88, 89,
    /* 108 */ 90, 91, 92, 93, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 120 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 132 */ 0, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
    /* 144 */ 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171,
    /* 156 */ 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183,
    /* 168 */ 201, 202, 203, 204, 205, 206, 207, 208, 0, 0, 0, 213,
    /* 180 */ 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225,
    /* 192 */ 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237,
    /* 204 */ 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249,
    /* 216 */ 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 0, 0,
    /* 228 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 240 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 252 */ 0, 0, 260, 261, 262, 263, 264, 0, 0, 0, 0, 0,
    /* 264 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 276 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 288 */ 0, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338,
    /* 300 */ 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350,
    /* 312 */ 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362,
    /* 324 */ 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374,
    /* 336 */ 375, 376, 377,
};

static uint16_t gen3_to_gen4_item(uint16_t g3item)
{
    if (g3item >= sizeof(item3to4) / sizeof(item3to4[0])) return 0;
    return item3to4[g3item];
}

/* PID gender threshold per National Dex: 255=genderless, 0=always male,
 * 254=always female, otherwise female if (PID & 0xFF) < threshold. */
static const uint8_t gender_threshold[GEN4_NATIONAL_DEX_MAX + 1] = {
    /*   1 */ [1]=31,31,31,31,31,31,31,31,31,127,127,127,127,127,127,127,
    /*  17 */ 127,127,127,127,127,127,127,127,127,127,127,127,254,254,254,0,
    /*  33 */ 0,0,191,191,191,191,191,191,127,127,127,127,127,127,127,127,
    /*  49 */ 127,127,127,127,127,127,127,127,127,63,63,127,127,127,63,63,
    /*  65 */ 63,63,63,63,127,127,127,127,127,127,127,127,127,127,127,127,
    /*  81 */ 255,255,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
    /*  97 */ 127,127,127,255,255,127,127,127,127,0,0,127,127,127,127,127,
    /* 113 */ 254,127,254,127,127,127,127,255,255,127,127,254,63,63,127,0,
    /* 129 */ 127,127,127,255,31,31,31,31,255,31,31,31,31,31,31,255,
    /* 145 */ 255,255,127,127,127,255,255,31,31,31,31,31,31,31,31,31,
    /* 161 */ 127,127,127,127,127,127,127,127,127,127,127,127,191,191,31,31,
    /* 177 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
    /* 193 */ 127,127,127,31,31,127,127,127,255,127,127,127,127,127,127,127,
    /* 209 */ 191,191,127,127,127,127,127,127,127,127,127,127,127,191,127,127,
    /* 225 */ 127,127,127,127,127,127,127,127,255,127,127,0,0,254,63,63,
    /* 241 */ 254,254,255,255,255,127,127,127,255,255,255,31,31,31,31,31,
    /* 257 */ 31,31,31,31,127,127,127,127,127,127,127,127,127,127,127,127,
    /* 273 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
    /* 289 */ 127,127,127,255,127,127,127,63,63,191,127,191,191,127,127,127,
    /* 305 */ 127,127,127,127,127,127,127,127,0,254,127,127,127,127,127,127,
    /* 321 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
    /* 337 */ 255,255,127,127,127,127,255,255,31,31,31,31,127,127,127,127,
    /* 353 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
    /* 369 */ 31,191,127,127,127,255,255,255,255,255,255,254,0,255,255,255,
    /* 385 */ 255,255,31,31,31,31,31,31,31,31,31,127,127,127,127,127,
    /* 401 */ 127,127,127,127,127,127,127,31,31,31,31,127,254,0,31,254,
    /* 417 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,127,191,191,
    /* 433 */ 127,127,127,255,255,127,127,254,127,127,127,127,127,31,31,31,
    /* 449 */ 127,127,127,127,127,127,127,127,127,127,127,127,127,255,127,127,
    /* 465 */ 127,63,63,31,127,31,31,127,127,255,0,127,127,254,255,255,
    /* 481 */ 255,255,255,255,127,255,255,254,255,255,255,255,255,
};

/* Build a decrypted 236-byte PK4 (Pal Park form) from a Gen 3 mon. dec must be
 * >= 236 bytes. Returns false for an egg or unrepresentable species. */
static bool build_pk4_from_gen3(const struct pksav_gba_pc_pokemon *g3,
                                enum gen4_game dest, uint8_t *dec)
{
    uint16_t internal = pksav_littleendian16(g3->blocks.growth.species);
    uint16_t dex = gen3_internal_to_national(internal);
    if (dex < 1 || dex > GEN3_NATIONAL_DEX_MAX) return false;

    uint32_t iv_egg = pksav_littleendian32(g3->blocks.misc.iv_egg_ability);
    if (iv_egg & PKSAV_GBA_POKEMON_EGG_MASK) return false; /* Pal Park won't move eggs */

    memset(dec, 0, GEN4_PK4_PARTY_SIZE);
    uint32_t pid = pksav_littleendian32(g3->personality);
    wr32(dec + 0x00, pid);
    /* 0x04 sanity = 0, 0x06 checksum set by gen4_pk4_encrypt */
    wr16(dec + 0x08, dex);
    wr16(dec + 0x0A, gen3_to_gen4_item(pksav_littleendian16(g3->blocks.growth.held_item)));
    uint16_t tid = pksav_littleendian16(g3->ot_id.pid);
    uint16_t sid = pksav_littleendian16(g3->ot_id.sid);
    wr16(dec + 0x0C, tid);
    wr16(dec + 0x0E, sid);
    wr32(dec + 0x10, pksav_littleendian32(g3->blocks.growth.exp));
    dec[0x14] = 70; /* Pal Park resets friendship */
    int ability_slot = (int)((iv_egg >> 31) & 1u);
    dec[0x15] = gen4_ability_id(dex, ability_slot);
    dec[0x16] = (uint8_t)(g3->markings & 0x0F); /* basic marks carry over */
    uint8_t lang = (uint8_t)(pksav_littleendian16(g3->language) & 0xFF);
    dec[0x17] = (lang >= 1 && lang <= 8) ? lang : 2 /* English */;

    /* EVs @0x18 (HP,Atk,Def,Spe,SpA,SpD) */
    const struct pksav_gba_pokemon_effort_block *ev = &g3->blocks.effort;
    dec[0x18] = ev->ev_hp;  dec[0x19] = ev->ev_atk; dec[0x1A] = ev->ev_def;
    dec[0x1B] = ev->ev_spd; dec[0x1C] = ev->ev_spatk; dec[0x1D] = ev->ev_spdef;
    /* contest stats @0x1E (cool,beauty,cute,smart,tough,sheen) */
    dec[0x1E] = ev->contest_stats.cool;  dec[0x1F] = ev->contest_stats.beauty;
    dec[0x20] = ev->contest_stats.cute;  dec[0x21] = ev->contest_stats.smart;
    dec[0x22] = ev->contest_stats.tough; dec[0x23] = ev->contest_stats.feel;

    /* moves/PP/PP-ups @0x28/0x30/0x34 */
    for (int i = 0; i < 4; i++)
    {
        wr16(dec + 0x28 + 2 * i, pksav_littleendian16(g3->blocks.attacks.moves[i]));
        dec[0x30 + i] = g3->blocks.attacks.move_pps[i];
        dec[0x34 + i] = (uint8_t)((g3->blocks.growth.pp_up >> (2 * i)) & 3);
    }

    /* Text: import from Gen 3, decide nickname flag, re-encode into Gen 4. */
    char nick[16] = {0}, otname[16] = {0};
    pksav_gba_import_text(g3->nickname, nick, PKMN_NAME_TEXT_MAX);
    pksav_gba_import_text(g3->otname, otname, TRAINER_NAME_TEXT_MAX);
    bool nicknamed = !ci_equal(nick, gen4_species_name(dex));
    gen4_encode_text(nick, dec + 0x48, 11);
    gen4_encode_text(otname, dec + 0x68, 8);

    /* IV32 @0x38: 30 IV bits, egg = 0, IsNicknamed = bit31. */
    uint32_t iv32 = iv_egg & 0x3FFFFFFFu;
    if (nicknamed) iv32 |= (1u << 31);
    wr32(dec + 0x38, iv32);

    /* Gender byte @0x40: bit1 female, bit2 genderless (bit0 fateful = 0). */
    uint8_t gt = gender_threshold[dex];
    if (gt == 255) dec[0x40] |= 0x04;                       /* genderless */
    else if (gt != 0 && (gt == 254 || (pid & 0xFF) < gt)) dec[0x40] |= 0x02; /* female */

    /* Origin/version @0x5F: Gen 3 origin game id aligns with Gen 4 version ids. */
    uint16_t origin_info = pksav_littleendian16(g3->blocks.misc.origin_info);
    dec[0x5F] = (uint8_t)((origin_info & PKSAV_GBA_POKEMON_ORIGIN_GAME_MASK) >>
                          PKSAV_GBA_POKEMON_ORIGIN_GAME_OFFSET);

    /* Met level (current) + OT gender @0x84. */
    int level = gen4_level_from_exp(pksav_littleendian32(g3->blocks.growth.exp),
                                    gen4_growth_rate_for_dex(dex));
    if (level < 1) level = 1;
    if (level > 100) level = 100;
    uint8_t ot_gender = (origin_info & PKSAV_GBA_POKEMON_OTGENDER_MASK) ? 1 : 0;
    dec[0x84] = (uint8_t)((level & 0x7F) | (ot_gender << 7));

    /* Met location = Pal Park (55). DP reads @0x80; Pt/HGSS read the extended
     * field @0x46 (set the DP slot too for cross-format consistency). */
    wr16(dec + 0x80, 55);
    wr16(dec + 0x7E, 0); /* egg location */
    if (dest == GEN4_GAME_PT || dest == GEN4_GAME_HGSS)
    {
        wr16(dec + 0x46, 55);
        wr16(dec + 0x44, 0);
    }

    /* Ball (preserved). @0x83 for DP/Pt, @0x86 for HGSS. Encounter type @0x85=0. */
    uint8_t ball = (uint8_t)((origin_info & PKSAV_GBA_POKEMON_BALL_MASK) >>
                             PKSAV_GBA_POKEMON_BALL_OFFSET);
    if (dest == GEN4_GAME_HGSS) dec[0x86] = ball; else dec[0x83] = ball;

    /* Met date @0x7B (year-2000, month, day) — the day it arrived. */
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    int yy = (lt ? lt->tm_year + 1900 : 2007);
    if (yy < 2000) yy = 2007;
    dec[0x7B] = (uint8_t)((yy - 2000) & 0xFF);
    dec[0x7C] = (uint8_t)(lt ? lt->tm_mon + 1 : 1);
    dec[0x7D] = (uint8_t)(lt ? lt->tm_mday : 1);

    return true;
}

/* Remove the Gen 3 party mon at index k (shift the contiguous party down). */
static void remove_gba_party_mon(PokemonSave *src, uint8_t k)
{
    struct pksav_gba_pokemon_party *p = src->save.gba_save.pokemon_storage.p_party;
    int n = (int)p->count;
    for (int i = k; i < n - 1; i++)
    {
        p->party[i] = p->party[i + 1];
    }
    memset(&p->party[n - 1], 0, sizeof(struct pksav_gba_party_pokemon));
    p->count = (uint32_t)(n - 1);
}

pksavhelper_error transfer_pkmn_gen3_to_gen4(PokemonSave *src, uint8_t src_index, PokemonSave *dst)
{
    if (src->save_generation_type != SAVE_GENERATION_3 ||
        dst->save_generation_type != SAVE_GENERATION_4)
    {
        return error_swap_pkmn;
    }
    struct pksav_gba_pokemon_party *sp = src->save.gba_save.pokemon_storage.p_party;
    if (src_index >= (int)sp->count)
    {
        return error_swap_pkmn;
    }

    struct gen4_save *g4 = &dst->save.gen4_save;
    uint8_t dec[GEN4_PK4_PARTY_SIZE];
    if (!build_pk4_from_gen3(&sp->party[src_index].pc_data, g4->game, dec))
    {
        return error_transfer_not_obtainable; /* egg / unrepresentable */
    }

    int count = (int)gen4_party_count(g4);
    if (count < GEN4_PARTY_MAX)
    {
        /* Append to the party: build the stats region, encrypt as party form. */
        gen4_build_party_stats(dec);
        uint8_t party_raw[GEN4_PK4_PARTY_SIZE];
        gen4_pk4_encrypt(dec, party_raw, true);
        memcpy(gen4_party_slot_raw_mut(g4, count), party_raw, GEN4_PK4_PARTY_SIZE);
        gen4_set_party_count(g4, (uint8_t)(count + 1));
    }
    else
    {
        /* Party full: first empty box slot, stored (136-byte) form. */
        bool placed = false;
        for (int b = 0; b < GEN4_NUM_BOXES && !placed; b++)
        {
            for (int s = 0; s < GEN4_BOX_SLOTS; s++)
            {
                uint8_t *slot = gen4_box_slot_raw_mut(g4, b, s);
                uint8_t chk[GEN4_PK4_STORED_SIZE];
                gen4_pk4_decrypt(slot, chk, false);
                if (gen4_pk4_species(chk) == 0)
                {
                    uint8_t stored_raw[GEN4_PK4_STORED_SIZE];
                    gen4_pk4_encrypt(dec, stored_raw, false);
                    memcpy(slot, stored_raw, GEN4_PK4_STORED_SIZE);
                    placed = true;
                    break;
                }
            }
        }
        if (!placed)
        {
            return error_transfer_no_space;
        }
    }

    remove_gba_party_mon(src, src_index);
    return error_none;
}

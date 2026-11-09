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
#include "gen3_stats.h"    /* move PP */
#include <pksav.h>
#include <string.h>
#include <time.h>

static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Gen 3 (international) text byte -> Gen 4 character code, generated from
 * PKHeX's StringConverter3 G3_EN and StringConverter4Util TableINT tables
 * (what Pal Park's glyph transfer does). Keeps the glyphs an ASCII round trip
 * loses: the male/female signs (0xB5/0xB6 -> 0x1BB/0x1BC), the apostrophe of
 * FARFETCH'D (0xB4 -> 0x1B3), quotes, accented letters. 0 = no equivalent. */
static const uint16_t g3_to_g4_char[256] = {
    0x1DE, 0x15F, 0x160, 0x161, 0x166, 0x167, 0x168, 0x169, 0x16A, 0x16B, 0x014, 0x16D, 0x16E, 0x171, 0x172, 0x173, /* 00 */
    0x19F, 0x178, 0x179, 0x17A, 0x170, 0x17E, 0x17F, 0x180, 0x02E, 0x186, 0x187, 0x188, 0x189, 0x18A, 0x18B, 0x03F, /* 10 */
    0x18D, 0x18E, 0x191, 0x192, 0x193, 0x1A0, 0x198, 0x199, 0x19A, 0x190, 0x1A4, 0x1A3, 0x1A5, 0x1C2, 0x1BD, 0x003, /* 20 */
    0x004, 0x006, 0x008, 0x00A, 0x044, 0x1C1, 0x1C5, 0x00D, 0x00F, 0x011, 0x013, 0x015, 0x017, 0x019, 0x01B, 0x01D, /* 30 */
    0x01F, 0x021, 0x023, 0x026, 0x028, 0x02A, 0x031, 0x034, 0x037, 0x03A, 0x03D, 0x032, 0x035, 0x038, 0x03B, 0x03E, /* 40 */
    0x024, 0x1AA, 0x1A9, 0x1E0, 0x1E1, 0x05B, 0x05C, 0x05E, 0x060, 0x062, 0x16C, 0x1D2, 0x1B9, 0x1BA, 0x06C, 0x06E, /* 50 */
    0x070, 0x072, 0x075, 0x077, 0x079, 0x07B, 0x07C, 0x07D, 0x181, 0x07F, 0x080, 0x083, 0x086, 0x089, 0x08C, 0x18C, /* 60 */
    0x090, 0x091, 0x092, 0x093, 0x095, 0x097, 0x099, 0x09A, 0x09B, 0x11C, 0x11D, 0x11B, 0x0F0, 0x0A0, 0x0A1, 0x052, /* 70 */
    0x054, 0x056, 0x058, 0x05A, 0x1DF, 0x000, 0x000, 0x05D, 0x05F, 0x061, 0x063, 0x065, 0x067, 0x069, 0x06B, 0x06D, /* 80 */
    0x06F, 0x071, 0x073, 0x076, 0x078, 0x07A, 0x081, 0x084, 0x087, 0x08A, 0x08D, 0x082, 0x085, 0x088, 0x08B, 0x08E, /* 90 */
    0x074, 0x121, 0x122, 0x123, 0x124, 0x125, 0x126, 0x127, 0x128, 0x129, 0x12A, 0x1AB, 0x1AC, 0x1AE, 0x1BE, 0x1B0, /* A0 */
    0x1AF, 0x1B4, 0x1B5, 0x1B2, 0x1B3, 0x1BB, 0x1BC, 0x1A8, 0x1AD, 0x176, 0x1B1, 0x12B, 0x12C, 0x12D, 0x12E, 0x12F, /* B0 */
    0x130, 0x131, 0x132, 0x133, 0x134, 0x135, 0x136, 0x137, 0x138, 0x139, 0x13A, 0x13B, 0x13C, 0x13D, 0x13E, 0x13F, /* C0 */
    0x140, 0x141, 0x142, 0x143, 0x144, 0x145, 0x146, 0x147, 0x148, 0x149, 0x14A, 0x14B, 0x14C, 0x14D, 0x14E, 0x14F, /* D0 */
    0x150, 0x151, 0x152, 0x153, 0x154, 0x155, 0x156, 0x157, 0x158, 0x159, 0x15A, 0x15B, 0x15C, 0x15D, 0x15E, 0x11F, /* E0 */
    0x1C4, 0x163, 0x175, 0x17B, 0x183, 0x195, 0x19B, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, /* F0 */
};

/* Convert a 0xFF-terminated Gen 3 name into a Gen 4 name field (0xFFFF
 * terminator, 0x0000 padding, as the games write it). */
static void gen3_name_to_gen4(const uint8_t *g3, int g3_len, uint8_t *field, int field_slots)
{
    int w = 0;
    for (int i = 0; i < g3_len && g3[i] != 0xFF && w < field_slots - 1; i++)
    {
        uint16_t code = g3_to_g4_char[g3[i]];
        if (code == 0)
            code = 0x01AC; /* '?' */
        wr16(field + 2 * w, code);
        w++;
    }
    wr16(field + 2 * w, 0xFFFF);
    for (w = w + 1; w < field_slots; w++)
        wr16(field + 2 * w, 0x0000);
}

/* ASCII -> Gen 3 byte for the characters used in English species names. */
static uint8_t ascii_to_gen3(char c)
{
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0xBB + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0xBB + (c - 'a')); /* stored uppercase */
    if (c >= '0' && c <= '9') return (uint8_t)(0xA1 + (c - '0'));
    switch (c)
    {
    case ' ': return 0x00;
    case '.': return 0xAD;
    case '-': return 0xAE;
    case '\'': return 0xB4;
    default: return 0xAC; /* '?' */
    }
}

/* Is the Gen 3 nickname anything but the default (uppercase English) species
 * name? Compared byte-for-byte in Gen 3 encoding, so the two NIDORANs,
 * FARFETCH'D and MR. MIME compare correctly. */
static bool gen3_is_nicknamed(const uint8_t *nick, uint16_t dex)
{
    uint8_t want[PKSAV_GBA_POKEMON_NICKNAME_LENGTH];
    memset(want, 0xFF, sizeof(want));
    int n = 0;
    if (dex == 29 || dex == 32) /* the ASCII table spells these "Nidoran-F/-M" */
    {
        const char *base = "NIDORAN";
        for (; base[n]; n++) want[n] = ascii_to_gen3(base[n]);
        want[n++] = (dex == 29) ? 0xB6 : 0xB5;
    }
    else
    {
        const char *name = gen3_national_dex_name(dex);
        for (; name[n] && n < (int)sizeof(want); n++) want[n] = ascii_to_gen3(name[n]);
    }
    for (int i = 0; i < (int)sizeof(want); i++)
    {
        uint8_t a = nick[i], b = want[i];
        if (a != b) return true;
        if (a == 0xFF) return false;
    }
    return false;
}

/* Gen 3 HM moves: Cut, Fly, Surf, Strength, Flash, Rock Smash, Waterfall, Dive.
 * Pal Park refuses a mon that knows one. */
static bool gen3_is_hm_move(uint16_t move)
{
    return move == 15 || move == 19 || move == 57 || move == 70 || move == 148 ||
           move == 249 || move == 127 || move == 291;
}

/* Max PP in Gen 4 for a Gen 3 move (#1-354): Gen 4 changed six base PP values
 * (PKHeX MoveInfo3/MoveInfo4 PP tables): Vine Whip, Absorb, Mega Drain, Giga
 * Drain, Stockpile up; Recover down. Pal Park heals to the Gen 4 value. */
static uint8_t gen4_max_pp_for_gen3_move(uint16_t move, uint8_t ups)
{
    uint8_t base;
    switch (move)
    {
    case 22:  base = 15; break; /* Vine Whip */
    case 71:  base = 25; break; /* Absorb */
    case 72:  base = 15; break; /* Mega Drain */
    case 105: base = 10; break; /* Recover */
    case 202: base = 10; break; /* Giga Drain */
    case 254: base = 20; break; /* Stockpile */
    default:  return gen3_move_max_pp(move, ups);
    }
    return (uint8_t)(base + (base * 20 * (ups & 3)) / 100);
}

static int gba_unown_form(uint32_t pid)
{
    int v = (int)((((pid >> 24) & 3) << 6) | (((pid >> 16) & 3) << 4) | (((pid >> 8) & 3) << 2) | (pid & 3));
    return v % 28;
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
    /* Keep the ability the mon had in Gen 3 (Pal Park copies it; the Gen 4
     * table would hand e.g. a Gen 3 Pidgey or Hypno a Gen 4-only ability). */
    dec[0x15] = gen3_ability_id(dex, (iv_egg >> 31) & 1u);
    /* Basic marks carry over, but the order differs: Gen 3 stores circle,
     * square, triangle, heart (bits 0-3); Gen 4 circle, triangle, square,
     * heart (then star, diamond). Swap bits 1 and 2 (PKHeX PK3 does the same). */
    uint8_t m3 = (uint8_t)(g3->markings & 0x0F);
    dec[0x16] = (uint8_t)((m3 & 0x09) | ((m3 & 0x02) << 1) | ((m3 & 0x04) >> 1));
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

    /* moves/PP/PP-ups @0x28/0x30/0x34; Pal Park heals PP (PKHeX HealPP) */
    for (int i = 0; i < 4; i++)
    {
        uint16_t move = pksav_littleendian16(g3->blocks.attacks.moves[i]);
        uint8_t ups = (uint8_t)((g3->blocks.growth.pp_up >> (2 * i)) & 3);
        wr16(dec + 0x28 + 2 * i, move);
        dec[0x30 + i] = move ? gen4_max_pp_for_gen3_move(move, ups) : 0;
        dec[0x34 + i] = move ? ups : 0;
    }

    /* Text: straight Gen 3 -> Gen 4 glyph transfer (no ASCII round trip). */
    bool nicknamed = gen3_is_nicknamed(g3->nickname, dex);
    gen3_name_to_gen4(g3->nickname, PKSAV_GBA_POKEMON_NICKNAME_LENGTH, dec + 0x48, 11);
    gen3_name_to_gen4(g3->otname, PKSAV_GBA_POKEMON_OTNAME_LENGTH, dec + 0x68, 8);

    /* IV32 @0x38: 30 IV bits, egg = 0, IsNicknamed = bit31. */
    uint32_t iv32 = iv_egg & 0x3FFFFFFFu;
    if (nicknamed) iv32 |= (1u << 31);
    wr32(dec + 0x38, iv32);

    /* Byte @0x40: bit0 fateful encounter, bit1 female, bit2 genderless,
     * bits 3-7 form (Unown's letter comes from its PID). */
    uint32_t ribbons = pksav_littleendian32(g3->blocks.misc.ribbons_obedience);
    if (ribbons & 0x80000000u) dec[0x40] |= 0x01;        /* fateful (Gen 3 bit 31) */
    uint8_t gt = gender_threshold[dex];
    if (gt == 255) dec[0x40] |= 0x04;                       /* genderless */
    else if (gt != 0 && (gt == 254 || (pid & 0xFF) < gt)) dec[0x40] |= 0x02; /* female */
    if (dex == 201) dec[0x40] |= (uint8_t)(gba_unown_form(pid) << 3);

    /* Hoenn ribbons @0x3C (PKHeX PK3.ConvertToPK4): each Gen 3 contest
     * category is a 0-4 rank count -> 4 cumulative bits; the 12 single
     * ribbons (Gen 3 bits 15-26) land on bits 20-31. */
    uint32_t hoenn = 0;
    for (int cat = 0; cat < 5; cat++)
    {
        uint32_t count = (ribbons >> (3 * cat)) & 7;
        for (uint32_t r = 0; r < 4 && r < count; r++) hoenn |= 1u << (4 * cat + r);
    }
    hoenn |= ((ribbons >> 15) & 0xFFFu) << 20;
    wr32(dec + 0x3C, hoenn);

    dec[0x82] = g3->blocks.misc.pokerus; /* Pokerus strain/days */

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
    dec[0x83] = ball;                             /* DP/Pt ball (HG/SS keeps it too) */
    if (dest == GEN4_GAME_HGSS) dec[0x86] = ball; /* HG/SS ball */

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

uint8_t pkmn_gender_threshold(uint16_t national_dex)
{
    if (national_dex < 1 || national_dex > GEN4_NATIONAL_DEX_MAX) return 255;
    return gender_threshold[national_dex];
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
    /* Pal Park won't take a mon that knows an HM, and never empties the
     * Gen 3 party (the game can't be saved/loaded with no party). */
    for (int k = 0; k < 4; k++)
    {
        if (gen3_is_hm_move(pksav_littleendian16(sp->party[src_index].pc_data.blocks.attacks.moves[k])))
        {
            return error_transfer_hm_move;
        }
    }
    if ((int)sp->count <= 1)
    {
        return error_transfer_last_pkmn;
    }

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

    /* Pal Park registers the arrival: mark it seen + caught in the Gen 4 dex. */
    gen4_dex_set_seen_caught(g4, gen4_pk4_species(dec));

    remove_gba_party_mon(src, src_index);
    return error_none;
}

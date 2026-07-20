/*
 * Gen 4 (NDS) Pokémon codec. See gen4_pkmn.h.
 * Validated against real DP/Pt/HGSS saves: the in-mon checksum recomputes for
 * every party mon after decrypt+unshuffle across all five retail saves.
 * Distributed under the MIT License (MIT).
 */
#include "gen4_pkmn.h"
#include <string.h>

/* ---- little-endian word helpers (DS saves are LE) ---- */
static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

/* ---- CRC-16/CCITT-FALSE ---- */
uint16_t gen4_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)(data[i] << 8);
        for (int b = 0; b < 8; b++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Block-shuffle table: order[sv][i] = physical slot holding logical block i.
 * sv is (PID>>13)&0x1F, reduced mod 24 (24..31 duplicate 0..7). */
static const uint8_t block_order[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 3, 0, 2}, {1, 2, 3, 0}, {1, 3, 2, 0},
    {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0},
    {3, 0, 1, 2}, {3, 0, 2, 1}, {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}};

/* Advance the 16-bit-key LCRNG and XOR each u16 word in place. */
static void crypt_words(uint8_t *base, int n_words, uint32_t seed)
{
    for (int i = 0; i < n_words; i++)
    {
        seed = seed * 0x41C64E6Du + 0x6073u;
        uint16_t key = (uint16_t)(seed >> 16);
        wr16(base + 2 * i, (uint16_t)(rd16(base + 2 * i) ^ key));
    }
}

uint16_t gen4_pk4_checksum(const uint8_t *dec)
{
    uint16_t sum = 0;
    for (int i = 0x08; i < 0x88; i += 2)
    {
        sum = (uint16_t)(sum + rd16(dec + i));
    }
    return sum;
}

void gen4_pk4_decrypt(const uint8_t *raw, uint8_t *out, bool is_party)
{
    memcpy(out, raw, is_party ? GEN4_PK4_PARTY_SIZE : GEN4_PK4_STORED_SIZE);

    uint32_t pid = rd32(out);
    uint16_t chk = rd16(out + 6);
    uint32_t sv = (pid >> 13) & 0x1F;

    /* 1. XOR-decrypt the 128-byte block region (checksum-seeded). */
    crypt_words(out + 0x08, 64, chk);
    /* 2. party stats region (PID-seeded). */
    if (is_party)
    {
        crypt_words(out + 0x88, 50, pid);
    }
    /* 3. un-shuffle: logical block i <- physical slot order[i]. */
    uint8_t phys[4][GEN4_PK4_BLOCK_SIZE];
    memcpy(phys, out + 0x08, sizeof(phys));
    const uint8_t *ord = block_order[sv % 24];
    for (int i = 0; i < 4; i++)
    {
        memcpy(out + 0x08 + GEN4_PK4_BLOCK_SIZE * i, phys[ord[i]], GEN4_PK4_BLOCK_SIZE);
    }
}

void gen4_pk4_encrypt(const uint8_t *dec, uint8_t *out, bool is_party)
{
    memcpy(out, dec, is_party ? GEN4_PK4_PARTY_SIZE : GEN4_PK4_STORED_SIZE);

    /* Recompute the in-mon checksum from the (still-decrypted) block region;
     * it also seeds re-encryption. */
    uint16_t chk = gen4_pk4_checksum(out);
    wr16(out + 6, chk);

    uint32_t pid = rd32(out);
    uint32_t sv = (pid >> 13) & 0x1F;

    /* 1. re-shuffle: physical slot order[i] <- logical block i (inverse of decrypt). */
    uint8_t logical[4][GEN4_PK4_BLOCK_SIZE];
    memcpy(logical, out + 0x08, sizeof(logical));
    const uint8_t *ord = block_order[sv % 24];
    for (int i = 0; i < 4; i++)
    {
        memcpy(out + 0x08 + GEN4_PK4_BLOCK_SIZE * ord[i], logical[i], GEN4_PK4_BLOCK_SIZE);
    }
    /* 2. XOR-encrypt (symmetric cipher). */
    crypt_words(out + 0x08, 64, chk);
    if (is_party)
    {
        crypt_words(out + 0x88, 50, pid);
    }
}

/* ---- accessors ---- */
uint32_t gen4_pk4_pid(const uint8_t *dec) { return rd32(dec); }
uint16_t gen4_pk4_stored_checksum(const uint8_t *dec) { return rd16(dec + 6); }
uint16_t gen4_pk4_species(const uint8_t *dec) { return rd16(dec + 0x08); }
uint16_t gen4_pk4_held_item(const uint8_t *dec) { return rd16(dec + 0x0A); }
uint16_t gen4_pk4_tid(const uint8_t *dec) { return rd16(dec + 0x0C); }
uint16_t gen4_pk4_sid(const uint8_t *dec) { return rd16(dec + 0x0E); }
uint32_t gen4_pk4_exp(const uint8_t *dec) { return rd32(dec + 0x10); }
uint8_t gen4_pk4_friendship(const uint8_t *dec) { return dec[0x14]; }
uint8_t gen4_pk4_language(const uint8_t *dec) { return dec[0x17]; }

uint16_t gen4_pk4_move(const uint8_t *dec, int i)
{
    if (i < 0 || i >= GEN4_PK4_NUM_MOVES)
        return 0;
    return rd16(dec + 0x28 + 2 * i);
}
uint8_t gen4_pk4_ev(const uint8_t *dec, int i)
{
    if (i < 0 || i > 5)
        return 0;
    return dec[0x18 + i];
}
uint8_t gen4_pk4_iv(const uint8_t *dec, int i)
{
    if (i < 0 || i > 5)
        return 0;
    uint32_t iv32 = rd32(dec + 0x38);
    return (uint8_t)((iv32 >> (5 * i)) & 0x1F);
}
bool gen4_pk4_is_egg(const uint8_t *dec) { return (rd32(dec + 0x38) >> 30) & 1u; }
bool gen4_pk4_is_nicknamed(const uint8_t *dec) { return (rd32(dec + 0x38) >> 31) & 1u; }
uint8_t gen4_pk4_party_level(const uint8_t *dec) { return dec[0x8C]; }

/* ---- text ----
 * Gen 4 stores 16-bit char codes (0xFFFF terminator). The raw stored value
 * indexes PKHeX's TableINT directly (it is NOT the remapped internal index that
 * some docs list). The Western set is contiguous: digits 0x0121, A-Z 0x012B,
 * a-z 0x0145; space is 0x01DE. Verified: real-save OT/nicknames ("Peter",
 * "Statistics", "Cave Flash") decode cleanly. Unmapped (accents, etc.) -> '?'. */
static char gen4_char(uint16_t v)
{
    if (v >= 0x0121 && v <= 0x012A)
        return (char)('0' + (v - 0x0121));
    if (v >= 0x012B && v <= 0x0144)
        return (char)('A' + (v - 0x012B));
    if (v >= 0x0145 && v <= 0x015E)
        return (char)('a' + (v - 0x0145));
    switch (v)
    {
    case 0x01DE: return ' ';
    case 0x0120: /* fullwidth ampersand */
    case 0x01C2: return '&';
    case 0x01A8: return '$';
    case 0x01AB: return '!';
    case 0x01AC: return '?';
    case 0x01AD: return ',';
    case 0x01AE: return '.';
    case 0x01B1: return '/';
    case 0x01B3: return '\'';
    case 0x01B9: return '(';
    case 0x01BA: return ')';
    case 0x01BD: return '+';
    case 0x01BE: return '-';
    case 0x01BF: return '*';
    case 0x01C0: return '#';
    case 0x01C1: return '=';
    case 0x01C3: return '~';
    case 0x01C4: return ':';
    case 0x01C5: return ';';
    case 0x01D0: return '@';
    case 0x01D2: return '%';
    default: return '?';
    }
}

size_t gen4_decode_text(const uint8_t *field, int max_chars, char *out)
{
    size_t n = 0;
    for (int i = 0; i < max_chars; i++)
    {
        uint16_t v = rd16(field + 2 * i);
        if (v == 0xFFFF || v == 0x0000)
            break;
        out[n++] = gen4_char(v);
    }
    out[n] = '\0';
    return n;
}

/* Inverse of gen4_char: ASCII -> Gen 4 16-bit code, or 0xFFFF if unmappable. */
static uint16_t gen4_code(char c)
{
    if (c >= '0' && c <= '9')
        return (uint16_t)(0x0121 + (c - '0'));
    if (c >= 'A' && c <= 'Z')
        return (uint16_t)(0x012B + (c - 'A'));
    if (c >= 'a' && c <= 'z')
        return (uint16_t)(0x0145 + (c - 'a'));
    switch (c)
    {
    case ' ':  return 0x01DE;
    case '&':  return 0x01C2;
    case '$':  return 0x01A8;
    case '!':  return 0x01AB;
    case '?':  return 0x01AC;
    case ',':  return 0x01AD;
    case '.':  return 0x01AE;
    case '/':  return 0x01B1;
    case '\'': return 0x01B3;
    case '(':  return 0x01B9;
    case ')':  return 0x01BA;
    case '+':  return 0x01BD;
    case '-':  return 0x01BE;
    case '*':  return 0x01BF;
    case '#':  return 0x01C0;
    case '=':  return 0x01C1;
    case '~':  return 0x01C3;
    case ':':  return 0x01C4;
    case ';':  return 0x01C5;
    case '@':  return 0x01D0;
    case '%':  return 0x01D2;
    default:   return 0xFFFF;
    }
}

void gen4_encode_text(const char *src, uint8_t *field, int field_slots)
{
    int w = 0;
    /* Leave room for the 0xFFFF terminator (last slot). */
    for (int i = 0; src && src[i] && w < field_slots - 1; i++)
    {
        uint16_t code = gen4_code(src[i]);
        if (code == 0xFFFF)
            continue; /* skip characters with no Gen 4 mapping */
        wr16(field + 2 * w, code);
        w++;
    }
    /* One 0xFFFF terminator, then 0x0000 padding — matches the real games'
     * name-field layout (verified against retail saves). */
    if (w < field_slots)
    {
        wr16(field + 2 * w, 0xFFFF);
        w++;
    }
    for (int i = w; i < field_slots; i++)
    {
        wr16(field + 2 * i, 0x0000);
    }
}

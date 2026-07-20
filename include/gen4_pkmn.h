/*
 * Gen 4 (NDS) Pokémon codec — PK4 decrypt/encrypt, block shuffle, checksum,
 * CRC-16, and field accessors. PKSav has no Gen 4 support, so this is a
 * from-scratch implementation validated against real DP/Pt/HGSS saves.
 * Distributed under the MIT License (MIT).
 */
#ifndef GEN4_PKMN_H
#define GEN4_PKMN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GEN4_PK4_STORED_SIZE 136 /* PC/box form */
#define GEN4_PK4_PARTY_SIZE  236 /* party form (stored + 100-byte stats) */
#define GEN4_PK4_BLOCK_SIZE  32
#define GEN4_PK4_NUM_MOVES   4

/* National Dex range representable in Gen 4. */
#define GEN4_NATIONAL_DEX_MAX 493

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect/xor). Used for both
 * the save-block footer checksums and available for general use. */
uint16_t gen4_crc16_ccitt(const uint8_t *data, size_t len);

/* Decrypt a raw PK4 (as stored in the save) into a decrypted, un-shuffled
 * buffer. `is_party` selects the 236-byte party form (also decrypts the
 * PID-seeded stats region). `raw` and `out` must not overlap; out must be at
 * least GEN4_PK4_PARTY_SIZE bytes (stored form uses the first 136). */
void gen4_pk4_decrypt(const uint8_t *raw, uint8_t *out, bool is_party);

/* Inverse of gen4_pk4_decrypt: re-shuffle and encrypt a decrypted buffer back
 * into raw save form. Recomputes the in-mon checksum @0x06 first. */
void gen4_pk4_encrypt(const uint8_t *dec, uint8_t *out, bool is_party);

/* Sum of the 64 decrypted u16 words 0x08..0x87 (mod 0x10000). Validates the mon
 * and seeds block decryption. */
uint16_t gen4_pk4_checksum(const uint8_t *dec);

/* --- Field accessors on a DECRYPTED, un-shuffled buffer --- */
uint32_t gen4_pk4_pid(const uint8_t *dec);
uint16_t gen4_pk4_stored_checksum(const uint8_t *dec); /* the header value @0x06 */
uint16_t gen4_pk4_species(const uint8_t *dec);         /* National Dex; 0 = empty */
uint16_t gen4_pk4_held_item(const uint8_t *dec);
uint16_t gen4_pk4_tid(const uint8_t *dec);
uint16_t gen4_pk4_sid(const uint8_t *dec);
uint32_t gen4_pk4_exp(const uint8_t *dec);
uint8_t  gen4_pk4_friendship(const uint8_t *dec);
uint8_t  gen4_pk4_language(const uint8_t *dec);
uint16_t gen4_pk4_move(const uint8_t *dec, int i);     /* i in 0..3 */
uint8_t  gen4_pk4_ev(const uint8_t *dec, int i);       /* i in 0..5 HP,Atk,Def,Spe,SpA,SpD */
uint8_t  gen4_pk4_iv(const uint8_t *dec, int i);       /* i in 0..5, same order */
bool     gen4_pk4_is_egg(const uint8_t *dec);
bool     gen4_pk4_is_nicknamed(const uint8_t *dec);
uint8_t  gen4_pk4_party_level(const uint8_t *dec);     /* party form: cached level @0x8C */

/* Decode a Gen 4 16-bit-per-char name field into ASCII (best effort; unmapped
 * chars become '?'). `dec` points at the field start, `max_chars` is the field
 * capacity (11 nickname / 8 OT). Writes a NUL-terminated string to `out`, which
 * must hold at least max_chars+1 bytes. Returns the string length. */
size_t gen4_decode_text(const uint8_t *field, int max_chars, char *out);

#endif /* GEN4_PKMN_H */

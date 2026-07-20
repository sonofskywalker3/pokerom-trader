/*
 * Gen 4 (NDS) party-stat recomputation and species data. See gen4_pkmn.h for
 * the PK4 codec this operates on.
 *
 * A boxed Gen 4 Pokémon stores only its 136-byte stored form (PID, EXP, IVs,
 * EVs, ...). When it enters the party the game derives a 100-byte stats region
 * (0x88..0xEB: status, level, current/max HP, and the five battle stats). This
 * module reproduces that derivation so a withdrawn mon has correct, legal stats.
 *
 * Data is Gen-4-accurate: base stats #1..493 come from Bulbapedia's
 * "Generations II-V" table (base stats were constant Gen II..V; first changes
 * were Gen VI) and were cross-checked against this repo's already-verified Gen 2
 * / Gen 3 tables (0 mismatches for #1..386). Growth rates match the Gen-4 games
 * (Serebii DP Pokédex) — five species (Aipom, Murkrow, Shuckle, Delibird,
 * Skarmory) changed leveling rate from Gen 3 to Gen 4 and use the Gen-4 value.
 *
 * Distributed under the MIT License (MIT).
 */
#ifndef GEN4_STATS_H
#define GEN4_STATS_H

#include <stdbool.h>
#include <stdint.h>

/* Growth-rate group (0=Medium Fast, 1=Erratic, 2=Fluctuating, 3=Medium Slow,
 * 4=Fast, 5=Slow) for a National Dex number (#1..493), 0 if out of range. */
uint8_t gen4_growth_rate_for_dex(uint16_t national_dex);

/* Total experience required to have reached `level` for a growth-rate group. */
uint32_t gen4_exp_for_level(int level, uint8_t growth_rate);

/* Level (1..100) implied by a total EXP value for a growth-rate group. */
int gen4_level_from_exp(uint32_t exp, uint8_t growth_rate);

/* Rebuild the 100-byte party-stats region (0x88..0xEB) of a DECRYPTED,
 * un-shuffled party PK4 buffer (>= 236 bytes) in place, from the mon's
 * persistent fields (species, EXP, IVs, EVs, PID/nature). Sets status=0, level,
 * current HP = max HP, and the six battle stats; zeroes the rest of the region.
 *
 * Returns false — and leaves the buffer untouched — if the species has no
 * base-stat data (empty slot, egg, or out-of-range dex), in which case the
 * caller must refuse to move it into the party. Re-encrypt with
 * gen4_pk4_encrypt(is_party=true) afterward. */
bool gen4_build_party_stats(uint8_t *dec);

#endif /* GEN4_STATS_H */

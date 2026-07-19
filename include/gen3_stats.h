/*
 * Gen 3 (GBA) party-stat recomputation (fork addition for the Gen 3 module).
 * Distributed under the MIT License (MIT), matching Pokerom Trader / PKSav.
 *
 * A boxed Gen 3 Pokémon stores only its 80-byte persistent data (EXP, IVs, EVs,
 * personality). When it enters the party the game derives the 20-byte party_data
 * (level + battle stats). This module reproduces that derivation so a withdrawn
 * mon has correct, legal stats.
 */
#ifndef GEN3_STATS_H
#define GEN3_STATS_H

#include "common.h"
#include <stdbool.h>

// Total experience required to have reached `level` for a growth-rate group
// (0=Medium Fast, 1=Erratic, 2=Fluctuating, 3=Medium Slow, 4=Fast, 5=Slow).
uint32_t gen3_exp_for_level(int level, uint8_t growth_rate);

// Level (1-100) implied by a total EXP value for a growth-rate group.
int gen3_level_from_exp(uint32_t exp, uint8_t growth_rate);

// Compute the party_data (level, stats, full HP) for a Gen 3 Pokémon from its
// persistent pc_data. Returns true on success, false if the species has no
// base-stat data (e.g. an egg or unknown index) — in which case the caller
// should refuse to move it into the party.
bool gen3_build_party_data(const struct pksav_gba_pc_pokemon *pc,
                           struct pksav_gba_pokemon_party_data *out);

#endif /* GEN3_STATS_H */

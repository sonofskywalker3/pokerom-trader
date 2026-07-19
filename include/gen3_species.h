/*
 * Gen 3 species-index -> name lookup (fork addition for the Gen 3 trade module).
 *
 * Distributed under the MIT License (MIT), matching Pokerom Trader / PKSav.
 *
 * PKSav exposes the raw Gen 3 *internal* species index stored in a mon's growth
 * substructure (pksav_gba_pokemon_growth_block.species). That index is NOT the
 * National Pokedex number for Hoenn Pokemon. The Gen 3 mapping is a clean offset:
 *
 *   internal 1..251   -> National 1..251   (identity)
 *   internal 252..276 -> unused / glitch slots (25 of them)
 *   internal 277..411 -> National 252..386  (national = internal - 25)
 *
 * This header provides that mapping plus a National Dex (1..386) name table.
 */
#ifndef GEN3_SPECIES_H
#define GEN3_SPECIES_H

#include <stdint.h>

#define GEN3_NATIONAL_DEX_MAX 386

/* Convert a Gen 3 internal species index to a National Dex number.
 * Returns 0 for an empty slot (internal index 0) or an unused/glitch index. */
uint16_t gen3_internal_to_national(uint16_t internal_index);

/* Convert a National Dex number (1..386) to its Gen 3 internal species index
 * (identity for #1..251, national+25 for Hoenn #252..386). Returns 0 if out of
 * range. This is the inverse of gen3_internal_to_national. */
uint16_t gen3_national_to_internal(uint16_t national_dex);

/* Human-readable species name for a National Dex number (1..386).
 * Returns "?" for 0 or out-of-range. */
const char *gen3_national_dex_name(uint16_t national_dex);

/* Convenience: name directly from a Gen 3 internal species index.
 * Returns "(empty)" for index 0, "?" for unused/glitch indices. */
const char *gen3_species_name_from_internal(uint16_t internal_index);

#endif /* GEN3_SPECIES_H */

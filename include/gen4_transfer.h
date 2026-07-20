/*
 * Gen 3 -> Gen 4 forward transfer (Pal Park). Fork addition, MIT.
 *
 * Migrates a Gen 3 (GBA) party Pokémon into a Gen 4 (NDS) save, reproducing the
 * Pal Park conversion: PID / IVs / EVs / moves / OT / TID are preserved, the
 * held item is re-mapped to its Gen 4 id, friendship resets to 70, the met
 * location becomes Pal Park (55), and the origin game is carried over. See
 * gen4-save-format.md section 12. Forward-only (no return), matching the games.
 */
#ifndef GEN4_TRANSFER_H
#define GEN4_TRANSFER_H

#include "common.h"
#include <stdbool.h>

/* Transfer the Gen 3 party Pokémon at src_index into the Gen 4 save `dst`
 * (appended to its party, or the first empty box slot if the party is full).
 * On success the mon is removed from the source party. Returns:
 *   error_none                    - transferred
 *   error_transfer_not_obtainable - egg or unrepresentable species (refused)
 *   error_transfer_no_space       - destination party and all boxes are full
 *   error_swap_pkmn               - invalid source slot / wrong gen pairing
 */
pksavhelper_error transfer_pkmn_gen3_to_gen4(PokemonSave *src, uint8_t src_index, PokemonSave *dst);

#endif /* GEN4_TRANSFER_H */

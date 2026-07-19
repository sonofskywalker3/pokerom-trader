/*
 * Forward (up-generation) Pokémon transfer. Fork addition, MIT.
 *
 * Moves a Pokémon from a lower generation into a higher one, doing its best to
 * produce a data-valid, dex-correct result. It refuses species that have no
 * legitimate home in the destination generation (e.g. the Johto starters have
 * no legal source in Gen 3), rather than fabricating an impossible mon.
 *
 * Note on "legal": going *into* Gen 3 can never carry an honest "came from an
 * older game" origin (Gen 3's save format has no such field), so a transferred
 * mon is stamped with a plausible Gen 3 origin. For species that ARE obtainable
 * in Gen 3 the result is indistinguishable from a legal one; the gate ensures we
 * never place a species Gen 3 can't legitimately hold.
 */
#ifndef GEN_TRANSFER_H
#define GEN_TRANSFER_H

#include "common.h"
#include <stdbool.h>

// Is this National Dex species (#1..251) legitimately obtainable in Gen 3?
bool gen3_species_obtainable(uint16_t national_dex);

// Transfer the Gen 1/2 party Pokémon at src_index into the Gen 3 save `dst`
// (added to its party, or the first empty box slot if the party is full). On
// success the mon is removed from the source party. Returns:
//   error_none                    - transferred
//   error_transfer_not_obtainable - species has no legal Gen 3 home (refused)
//   error_transfer_no_space       - destination party and all boxes are full
//   error_swap_pkmn               - source slot invalid / egg / bad gen pairing
pksavhelper_error transfer_pkmn_to_gen3(PokemonSave *src, uint8_t src_index, PokemonSave *dst);

#endif /* GEN_TRANSFER_H */

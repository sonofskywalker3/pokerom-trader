/*
 * In-game "event" restorer. Fork addition, MIT.
 *
 * Re-issues the distribution key items (tickets) that unlocked already-built,
 * server-gated in-game events, so the player can obtain the Pokémon *in-game*
 * (the cartridge generates a fully legitimate mon). This only adds an item; the
 * game does everything else. It never fabricates a Pokémon.
 *
 * Some events may also need an event flag set to appear; PKSav exposes no flag
 * API, so that is a per-game follow-up. Item injection is what this provides.
 */
#ifndef EVENTS_H
#define EVENTS_H

#include "common.h"
#include <stdbool.h>

// Max event flags a single event sets (0-terminated list in flag_ids).
#define PKMN_EVENT_MAX_FLAGS 3

struct pkmn_event
{
    SaveGenerationType gen;   // SAVE_GENERATION_2 or SAVE_GENERATION_3
    uint8_t save_type_mask;   // bit set per applicable pksav save_type value
    uint16_t item_id;         // key item to grant
    // Event flag id(s) to set so the game actually offers the event. A flag id
    // is the bit index into the game's event-flag array (pret decomp FLAG_*
    // numbering for Gen 3). 0-terminated; unused slots are 0. Flags differ per
    // game, so each row targets a single game (see events.c).
    uint16_t flag_ids[PKMN_EVENT_MAX_FLAGS];
    const char *ticket;       // "GS Ball", "Eon Ticket", ...
    const char *pokemon;      // what it unlocks
    const char *location;     // where to go in-game
};

// Fill `out` with events applicable to this save's game; returns the count.
int events_available(const PokemonSave *pkmn_save, const struct pkmn_event **out, int max);

// Grant the event's ticket item to the save. Returns error_none on success,
// error_swap_pkmn if already present or no room / wrong game.
pksavhelper_error apply_event(PokemonSave *pkmn_save, const struct pkmn_event *ev);

#endif /* EVENTS_H */

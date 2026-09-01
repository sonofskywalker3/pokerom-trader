#ifndef PKMN_EVOLUTIONS_H
#define PKMN_EVOLUTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include "pksavhelper.h"

// Evolution-chain data for National Dex #1-493 and helpers that answer:
// "would evolving this Pokemon register a dex entry the player doesn't own?"
// Everything is keyed by National Dex number, so it works for every
// generation; evolutions whose target doesn't exist in the save's own dex
// (e.g. Steelix in a Gen 1 save) are ignored automatically.

enum pkmn_evo_method
{
    PKMN_EVO_LEVEL = 0, // level up; `level` holds the level
    PKMN_EVO_STONE,     // use a stone; `detail` names it
    PKMN_EVO_TRADE,     // trade; `detail` names a required held item, or NULL
    PKMN_EVO_SPECIAL,   // anything else; `detail` describes the method
    PKMN_EVO_BREED,     // not an edge method: a pre-evolution reached by breeding (Gen 2+)
    PKMN_EVO_METHOD_COUNT
};

struct pkmn_evo_edge
{
    uint16_t from;      // National Dex of the pre-evolution
    uint16_t to;        // National Dex of the evolution
    uint8_t method;     // enum pkmn_evo_method
    uint8_t level;      // for PKMN_EVO_LEVEL, else 0
    const char *detail; // stone name / trade item / special description
};

// Eevee has 7 direct evolutions in Gen 4; nothing branches wider.
#define PKMN_MAX_DIRECT_EVOS 7
// Most unregistered species reachable ahead of one Pokemon (Eevee again).
#define PKMN_MAX_NEEDED_EVOS 8

// One evolution the player still needs for the dex, plus how to get it.
struct pkmn_needed_evo
{
    uint16_t dex;       // the unowned species
    uint8_t method;     // enum pkmn_evo_method of the edge that reaches it
    uint8_t level;      // for PKMN_EVO_LEVEL
    const char *detail; // stone / trade item / special description (may be NULL)
};

// Raw table access (tests, iteration).
uint16_t pkmn_evo_edge_count(void);
const struct pkmn_evo_edge *pkmn_evo_edge_at(uint16_t index);

// Direct evolutions of a species; returns count, fills out[].
uint8_t pkmn_direct_evolutions(uint16_t national_dex, const struct pkmn_evo_edge *out[PKMN_MAX_DIRECT_EVOS]);

// Every species reachable by evolving `national_dex` (any number of steps)
// that exists in this save's Pokedex but isn't owned yet. Returns count.
uint8_t pkmn_needed_evolutions(const PokemonSave *pkmn_save, uint16_t national_dex,
                               struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS]);

// Can breeding `national_dex` in this save produce eggs at all? False for
// Gen 1 saves (no breeding) and for Nidorina/Nidoqueen (no egg group).
bool pkmn_can_breed(const PokemonSave *pkmn_save, uint16_t national_dex);

// Every pre-evolution of `national_dex` (any number of steps back) that exists
// in this save's Pokedex but isn't owned yet -- what hatching its eggs would
// register. Entries use PKMN_EVO_BREED; `detail` names a required incense
// (Gen 3/4 babies) or is NULL. Does not check pkmn_can_breed. Returns count.
uint8_t pkmn_needed_preevolutions(const PokemonSave *pkmn_save, uint16_t national_dex,
                                  struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS]);

// Needed evolutions followed by needed pre-evolutions (only when the species
// can breed in this save): everything this Pokemon can still do for the dex.
uint8_t pkmn_needed_dex_tasks(const PokemonSave *pkmn_save, uint16_t national_dex,
                              struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS]);


// Needed evolutions followed by needed pre-evolutions (only when the species
// can breed in this save): everything this Pokemon can still do for the dex.
uint8_t pkmn_needed_dex_tasks(const PokemonSave *pkmn_save, uint16_t national_dex,
                              struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS]);

#endif // PKMN_EVOLUTIONS_H

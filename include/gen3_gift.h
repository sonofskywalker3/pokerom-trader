/*
 * Gen 3 event Pokemon distributions, re-issued exactly as the distribution
 * cartridge generated them. Fork addition, MIT.
 *
 * Gen 3 events (e.g. the 2006 US 10th Anniversary tour) wrote a finished
 * Pokemon straight into the save: fixed OT/ID/level/moves/met data, plus a PID,
 * IVs and OT gender drawn from the GBA LCRNG starting at a 16-bit seed. This
 * module reproduces that generator (per PKHeX's EncounterGift3 + CommonEvent3)
 * so the result matches the real distribution field for field.
 */
#ifndef GEN3_GIFT_H
#define GEN3_GIFT_H

#include "common.h"
#include <stdbool.h>
#include <stdint.h>

enum gen3_gift_pid
{
    GEN3_GIFT_BACD_R_A, // reversed-order PID, 16-bit origin seed, anti-shiny
};

struct gen3_gift
{
    uint16_t species;      // National Dex
    uint8_t level;         // level received at (also the met level)
    uint16_t moves[4];
    uint8_t pps[4];        // base PP of each move (no PP Ups)
    uint16_t tid, sid;
    const char *ot_name;   // as printed on the distribution, e.g. "10 ANIV"
    uint8_t origin_game;   // Gen 3 origin game id: 1 S, 2 R, 3 E, 4 FR, 5 LG
    uint8_t language;      // 2 = English
    enum gen3_gift_pid method;
};

// The US 10th Anniversary ("Journey Across America", 2006) Celebi.
extern const struct gen3_gift GEN3_GIFT_10ANIV_CELEBI;

// Build the gift Pokemon the distribution would have produced from the given
// 16-bit origin seed (0..65535). Fills a decrypted PC Pokemon.
void gen3_gift_build(const struct gen3_gift *gift, uint16_t seed16, struct pksav_gba_pc_pokemon *out);

// Generate the gift with a random origin seed and place it like the
// distribution did: first free party slot, else the first empty PC slot.
// Registers it in the Pokedex. error_transfer_no_space when everything is full.
pksavhelper_error gen3_gift_give(PokemonSave *save, const struct gen3_gift *gift);

// True if the save already holds this distribution's Pokemon (same species,
// OT ID and secret ID) in its party or PC.
bool gen3_gift_already_received(const PokemonSave *save, const struct gen3_gift *gift);

#endif /* GEN3_GIFT_H */

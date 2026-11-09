/*
 * Gen 3 event Pokemon distributions. See gen3_gift.h.
 * Distributed under the MIT License (MIT).
 *
 * Generator references (PKHeX, kwsch/PKHeX master):
 *   Legality/Encounters/Data/Gen3/EncountersWC3.cs   - the event templates
 *   Legality/Encounters/Templates/Gen3/Gifts/EncounterGift3.cs - ConvertToPKM,
 *       SetPINGA, GetSaneSeed (restricted methods: seed & 0xFFFF), GetGender
 *   Legality/RNG/ClassicEra/Gen3/CommonEvent3.cs     - GetRegularAntishiny,
 *       GetGenderBit7
 *   Legality/RNG/ClassicEra/ClassicEraRNG.cs         - GetSequentialIVs
 */
#include "gen3_gift.h"
#include "gen3_species.h"
#include "gen3_stats.h"
#include "gen4_stats.h"   // ability slots
#include "pksavhelper.h"  // pokedex_reconcile
#include <pksav.h>
#include <string.h>
#include <time.h>

const struct gen3_gift GEN3_GIFT_10ANIV_CELEBI = {
    .species = 251,
    .level = 70,
    // AncientPower, Future Sight, Baton Pass, Perish Song
    .moves = {246, 248, 226, 195},
    .pps = {5, 15, 40, 5},
    .tid = 10,
    .sid = 0,
    .ot_name = "10 ANIV",
    .origin_game = 2, // Ruby
    .language = 2,    // English
    .method = GEN3_GIFT_BACD_R_A,
};

#define MET_LOCATION_EVENT 255 // "fateful" event location used by distributions
#define BALL_POKE 4

static uint32_t lcrng_next(uint32_t s) { return s * 0x41C64E6Du + 0x6073u; }
static uint16_t next16(uint32_t *s) { *s = lcrng_next(*s); return (uint16_t)(*s >> 16); }
static uint16_t next15(uint32_t *s) { *s = lcrng_next(*s); return (uint16_t)((*s >> 16) & 0x7FFF); }

static bool is_shiny3(uint32_t id_xor, uint32_t pid)
{
    return ((id_xor ^ (pid >> 16) ^ (pid & 0xFFFF)) & 0xFFFF) < 8;
}

// Base friendship (Gen 3 personal data) for the species we distribute.
static uint8_t base_friendship(uint16_t dex)
{
    switch (dex)
    {
    case 151: case 251: case 385: return 100; // Mew, Celebi, Jirachi
    default: return 70;
    }
}

void gen3_gift_build(const struct gen3_gift *g, uint16_t seed16, struct pksav_gba_pc_pokemon *out)
{
    memset(out, 0, sizeof(*out));
    uint32_t id_xor = (uint32_t)g->tid ^ g->sid;

    // PID: BACD_R_A = two LCRNG calls, first is the high half; if that
    // lands shiny, the event code bumps it with (pid + 8) & ~7.
    uint32_t seed = seed16;
    uint16_t a = next16(&seed);
    uint16_t b = next16(&seed);
    uint32_t pid = ((uint32_t)a << 16) | b;
    if (is_shiny3(id_xor, pid))
    {
        pid = (pid + 8) & 0xFFFFFFF8u;
    }
    // IVs: two more calls, 15 bits each (HP/Atk/Def, then Spe/SpA/SpD).
    uint32_t iv1 = next15(&seed);
    uint32_t iv2 = next15(&seed);
    uint32_t iv32 = (iv2 << 15) | iv1;
    // OT gender (RandS7): bit 7 of the next call, inverted.
    uint32_t ot_gender = ((uint32_t)(lcrng_next(seed) >> 16) >> 7 & 1u) ^ 1u;

    out->personality = pksav_littleendian32(pid);
    out->ot_id.pid = pksav_littleendian16(g->tid);
    out->ot_id.sid = pksav_littleendian16(g->sid);
    // Low byte language; high byte sanity flags with bit 1 = "has species".
    out->language = pksav_littleendian16((uint16_t)(0x0200 | g->language));

    char name[16];
    const char *sp = gen3_national_dex_name(g->species);
    size_t n = 0;
    for (; sp[n] && n < sizeof(name) - 1; n++)
    {
        char c = sp[n];
        name[n] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    name[n] = '\0';
    pksav_gba_export_text(name, out->nickname, PKSAV_GBA_POKEMON_NICKNAME_LENGTH);
    pksav_gba_export_text(g->ot_name, out->otname, PKSAV_GBA_POKEMON_OTNAME_LENGTH);

    out->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(g->species));
    out->blocks.growth.held_item = 0;
    out->blocks.growth.exp = pksav_littleendian32(gen3_exp_for_level(g->level, gen3_growth_rate_for_dex(g->species)));
    out->blocks.growth.friendship = base_friendship(g->species);
    for (int i = 0; i < 4; i++)
    {
        out->blocks.attacks.moves[i] = pksav_littleendian16(g->moves[i]);
        out->blocks.attacks.move_pps[i] = g->pps[i];
    }
    out->blocks.misc.met_location = MET_LOCATION_EVENT;
    uint16_t origin = (uint16_t)((g->level & 0x7F) | ((g->origin_game & 0xF) << 7) | (BALL_POKE << 11) | (ot_gender << 15));
    out->blocks.misc.origin_info = pksav_littleendian16(origin);
    uint32_t ability_bit = gen3_species_uses_ability_bit(g->species) ? (pid & 1u) : 0u;
    out->blocks.misc.iv_egg_ability = pksav_littleendian32(iv32 | (ability_bit << 31));
}

static bool is_this_gift(const struct pksav_gba_pc_pokemon *m, const struct gen3_gift *g)
{
    return pksav_littleendian16(m->blocks.growth.species) == gen3_national_to_internal(g->species) &&
           pksav_littleendian16(m->ot_id.pid) == g->tid &&
           pksav_littleendian16(m->ot_id.sid) == g->sid;
}

bool gen3_gift_already_received(const PokemonSave *save, const struct gen3_gift *g)
{
    if (save->save_generation_type != SAVE_GENERATION_3)
    {
        return false;
    }
    const struct pksav_gba_pokemon_storage *st = &save->save.gba_save.pokemon_storage;
    for (uint32_t i = 0; i < st->p_party->count && i < PKSAV_GBA_PARTY_NUM_POKEMON; i++)
    {
        if (is_this_gift(&st->p_party->party[i].pc_data, g)) return true;
    }
    for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES; b++)
    {
        for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; s++)
        {
            if (is_this_gift(&st->p_pc->boxes[b].entries[s], g)) return true;
        }
    }
    return false;
}

pksavhelper_error gen3_gift_give(PokemonSave *save, const struct gen3_gift *g)
{
    if (save->save_generation_type != SAVE_GENERATION_3)
    {
        return error_swap_pkmn;
    }
    // A distribution cartridge's seed was effectively random per recipient
    // (any of 65536). Mix wall time, CPU time, a stack address (ASLR) and the
    // save's own bytes so separate runs and separate saves differ.
    static uint32_t salt = 0;
    uint32_t local = 0;
    salt ^= (uint32_t)time(NULL) * 2654435761u;
    salt ^= (uint32_t)clock() * 40503u;
    salt ^= (uint32_t)(uintptr_t)&local;
    salt ^= (uint32_t)save->save.gba_save.pokemon_storage.p_party->count * 0x9E3779B9u;
    if (save->save.gba_save.player_info.p_id != NULL)
    {
        salt ^= ((uint32_t)pksav_littleendian16(save->save.gba_save.player_info.p_id->pid) << 16) |
                pksav_littleendian16(save->save.gba_save.player_info.p_id->sid);
    }
    for (int i = 0; i < 4; i++)
    {
        salt = salt * 0x41C64E6Du + 0x6073u; // stir
    }
    // Skip the 8 in 65536 seeds whose raw PID would be shiny and get the
    // anti-shiny bump: current PKHeX accepts them, older releases flag them,
    // and the plain seeds are just as genuine a cartridge result.
    for (;;)
    {
        uint32_t t = salt >> 16;
        t = t * 0x41C64E6Du + 0x6073u;
        uint32_t a = t >> 16;
        t = t * 0x41C64E6Du + 0x6073u;
        uint32_t b = t >> 16;
        if (!is_shiny3((uint32_t)g->tid ^ g->sid, (a << 16) | b))
        {
            break;
        }
        salt = salt * 0x41C64E6Du + 0x6073u;
    }
    struct pksav_gba_pc_pokemon mon;
    gen3_gift_build(g, (uint16_t)(salt >> 16), &mon);

    struct pksav_gba_pokemon_storage *st = &save->save.gba_save.pokemon_storage;
    if ((int)st->p_party->count < PKSAV_GBA_PARTY_NUM_POKEMON)
    {
        int i = (int)st->p_party->count;
        st->p_party->party[i].pc_data = mon;
        gen3_build_party_data(&mon, &st->p_party->party[i].party_data);
        st->p_party->count = (uint32_t)(i + 1);
    }
    else
    {
        bool placed = false;
        for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES && !placed; b++)
        {
            for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON && !placed; s++)
            {
                if (st->p_pc->boxes[b].entries[s].blocks.growth.species == 0)
                {
                    st->p_pc->boxes[b].entries[s] = mon;
                    placed = true;
                }
            }
        }
        if (!placed)
        {
            return error_transfer_no_space;
        }
    }
    pokedex_reconcile(save);
    return error_none;
}

/*
 * Forward (up-generation) Pokémon transfer — Gen 1/2 -> Gen 3. See header.
 * Distributed under the MIT License (MIT).
 */
#include "gen_transfer.h"
#include "pksavhelper.h"   // species maps, PKMN/TRAINER text lengths
#include "gen3_stats.h"    // gen3_build_party_data, growth rate, exp
#include "gen3_species.h"  // gen3_national_to_internal
#include "gen2_items.h"    // Gen 2 -> Gen 3 held item ids
#include "gen4_stats.h"    // ability slots (unchanged Gen 3 -> 4 for #1-386)
#include "gen4_transfer.h" // gender thresholds
#include <pksav.h>
#include <string.h>

/* ============================================================
 * Obtainability map: 1 = species is NOT obtainable in Gen 3
 * (stranded / event-only), 0 = obtainable. Indexed by National
 * Dex #1..251.
 *
 * Coverage counts the ENTIRE Gen 3 family: Ruby/Sapphire/Emerald,
 * FireRed/LeafGreen (incl. Sevii Islands), AND Pokémon Colosseum /
 * XD: Gale of Darkness (GameCube), which trade with the handhelds.
 * The GameCube games are why the Johto starters are obtainable
 * (XD Mt. Battle reward; Colosseum shadow Bayleef/Quilava/Croconaw
 * bred down). Real-world distribution events are excluded, which
 * leaves only the two event-only Mythicals stranded in #1..251.
 * Verified against Bulbapedia/Serebii.
 * ============================================================ */
static const uint8_t gen3_unobtainable[252] = {
    [151] = 1, // Mew    — Gen 3 only via real-world events
    [251] = 1, // Celebi — Gen 3 only via events / bonus discs
};
/* ============================================================ */

bool gen3_species_obtainable(uint16_t national_dex)
{
    if (national_dex < 1 || national_dex > 251)
    {
        return false;
    }
    return gen3_unobtainable[national_dex] == 0;
}

// Read the fields we need from a Gen 1/2 party slot into gen-agnostic locals.
struct src_mon
{
    uint16_t dex;      // National Dex number
    uint8_t level;
    uint16_t iv_data;  // GB DV bitset
    uint8_t moves[PKSAV_GEN1_POKEMON_NUM_MOVES];
    uint8_t move_pps[PKSAV_GEN1_POKEMON_NUM_MOVES];
    uint16_t ot_id;
    const uint8_t *otname;
    const uint8_t *nickname;
    bool is_gen1;
    bool is_egg;
    uint8_t held_item;  // Gen 2 item id (0 for Gen 1: that byte is a catch rate)
    uint8_t pokerus;
    bool ot_female;     // OT gender, when the source save knows it
};

static bool read_src_mon(PokemonSave *src, uint8_t i, struct src_mon *out)
{
    if (src->save_generation_type == SAVE_GENERATION_1)
    {
        struct pksav_gen1_pokemon_party *party = src->save.gen1_save.pokemon_storage.p_party;
        if (i >= party->count)
        {
            return false;
        }
        struct pksav_gen1_party_pokemon *p = &party->party[i];
        out->dex = species_gen1_to_gen2[p->pc_data.species]; // Gen 1 internal -> dex
        // A Gen 1 party mon's real level is party_data.level; pc_data.level is
        // only refreshed when it's deposited, so it is stale after level-ups.
        out->level = p->party_data.level;
        out->iv_data = p->pc_data.iv_data;
        for (int k = 0; k < PKSAV_GEN1_POKEMON_NUM_MOVES; k++)
        {
            out->moves[k] = p->pc_data.moves[k];
            out->move_pps[k] = p->pc_data.move_pps[k];
        }
        out->ot_id = pksav_bigendian16(p->pc_data.ot_id);
        out->otname = party->otnames[i];
        out->nickname = party->nicknames[i];
        out->is_gen1 = true;
        out->is_egg = false;
        out->held_item = 0;
        out->pokerus = 0;
        out->ot_female = false;
        return true;
    }
    if (src->save_generation_type == SAVE_GENERATION_2)
    {
        struct pksav_gen2_pokemon_party *party = src->save.gen2_save.pokemon_storage.p_party;
        if (i >= party->count)
        {
            return false;
        }
        struct pksav_gen2_party_pokemon *p = &party->party[i];
        out->dex = p->pc_data.species; // Gen 2 species index == National Dex
        out->level = p->pc_data.level;
        out->iv_data = p->pc_data.iv_data;
        for (int k = 0; k < PKSAV_GEN2_POKEMON_NUM_MOVES; k++)
        {
            out->moves[k] = p->pc_data.moves[k];
            out->move_pps[k] = p->pc_data.move_pps[k];
        }
        out->ot_id = pksav_bigendian16(p->pc_data.ot_id);
        out->otname = party->otnames[i];
        out->nickname = party->nicknames[i];
        out->is_gen1 = false;
        out->is_egg = (party->species[i] == 0xFD); // eggs show 0xFD in the species list
        out->held_item = p->pc_data.held_item;
        out->pokerus = p->pc_data.pokerus;
        // Crystal records the player's gender; credit it to mons they caught.
        struct pksav_gen2_save *g2 = &src->save.gen2_save;
        out->ot_female = g2->trainer_info.p_gender != NULL && g2->trainer_info.p_id != NULL &&
                         pksav_bigendian16(*g2->trainer_info.p_id) == out->ot_id &&
                         *g2->trainer_info.p_gender != 0;
        return true;
    }
    return false;
}

// Gen 1/2 traits that live in the DVs, read the way the games read them.
static bool gb_is_shiny(const uint8_t dv[PKSAV_NUM_GB_IVS])
{
    uint8_t atk = dv[PKSAV_GB_IV_ATTACK];
    return dv[PKSAV_GB_IV_DEFENSE] == 10 && dv[PKSAV_GB_IV_SPEED] == 10 &&
           dv[PKSAV_GB_IV_SPECIAL] == 10 && (atk & 2);
}

// 0 male, 1 female, 2 genderless. Gen 2 compares (Atk DV << 4 | Spd DV)
// against the same thresholds Gen 3 compares the PID's low byte against.
static int gb_gender(uint16_t dex, const uint8_t dv[PKSAV_NUM_GB_IVS])
{
    uint8_t t = pkmn_gender_threshold(dex);
    if (t == 255) return 2;
    if (t == 0) return 0;
    if (t == 254) return 1;
    uint8_t b = (uint8_t)((dv[PKSAV_GB_IV_ATTACK] << 4) | dv[PKSAV_GB_IV_SPEED]);
    return b <= t ? 1 : 0;
}

static int gba_gender(uint16_t dex, uint32_t pid)
{
    uint8_t t = pkmn_gender_threshold(dex);
    if (t == 255) return 2;
    if (t == 0) return 0;
    if (t == 254) return 1;
    return (pid & 0xFF) < t ? 1 : 0;
}

static int gb_unown_letter(const uint8_t dv[PKSAV_NUM_GB_IVS])
{
    int v = ((dv[PKSAV_GB_IV_ATTACK] & 6) << 5) | ((dv[PKSAV_GB_IV_DEFENSE] & 6) << 3) |
            ((dv[PKSAV_GB_IV_SPEED] & 6) << 1) | ((dv[PKSAV_GB_IV_SPECIAL] & 6) >> 1);
    return v / 10;
}

static int gba_unown_letter(uint32_t pid)
{
    int v = (int)((((pid >> 24) & 3) << 6) | (((pid >> 16) & 3) << 4) | (((pid >> 8) & 3) << 2) | (pid & 3));
    return v % 28;
}

// Fabricate a personality value (Gen 1/2 have none). Deterministic per mon,
// and chosen so the Gen 3 mon keeps what the DVs decided in Gen 2: gender,
// shininess (against the transferred OT ID with secret ID 0) and Unown's
// letter. Nature and the ability slot come from whatever PID satisfies that.
static uint32_t make_gen3_pid(const struct src_mon *s, const uint8_t dv[PKSAV_NUM_GB_IVS])
{
    bool want_shiny = gb_is_shiny(dv);
    int want_gender = gb_gender(s->dex, dv);
    int want_letter = (s->dex == 201) ? gb_unown_letter(dv) : -1;
    uint32_t state = ((uint32_t)s->ot_id << 16) ^ ((uint32_t)s->dex * 2654435761u) ^ ((uint32_t)s->iv_data * 40503u);
    for (int tries = 0; tries < (1 << 24); tries++)
    {
        state = state * 0x41C64E6Du + 0x6073u;
        uint16_t lo = (uint16_t)(state >> 16);
        state = state * 0x41C64E6Du + 0x6073u;
        uint16_t hi = (uint16_t)(state >> 16);
        if (want_shiny)
        {
            hi = (uint16_t)(s->ot_id ^ lo ^ (hi & 7)); // TID ^ SID(0) ^ hi ^ lo < 8
        }
        uint32_t cand = ((uint32_t)hi << 16) | lo;
        bool shiny = (uint16_t)(s->ot_id ^ hi ^ lo) < 8;
        if (cand == 0 || shiny != want_shiny) continue;
        if (gba_gender(s->dex, cand) != want_gender) continue;
        if (want_letter >= 0 && gba_unown_letter(cand) != want_letter) continue;
        return cand;
    }
    return 0x1a2b3c4d;
}

// Build a Gen 3 Pokémon from a Gen 1/2 source mon. Returns the National Dex in
// *out_dex. false if the species can't be represented (unknown/egg-ish).
static bool build_gen3_mon(const struct src_mon *s, struct pksav_gba_pc_pokemon *out, uint16_t *out_dex)
{
    if (s->dex < 1 || s->dex > 251)
    {
        return false;
    }
    *out_dex = s->dex;
    uint16_t internal = gen3_national_to_internal(s->dex);

    uint8_t dv[PKSAV_NUM_GB_IVS];
    gb_get_dvs(s->iv_data, dv);

    memset(out, 0, sizeof(*out));

    uint32_t pid = make_gen3_pid(s, dv);
    out->personality = pksav_littleendian32(pid);
    out->ot_id.pid = pksav_littleendian16(s->ot_id);
    out->ot_id.sid = 0;
    // Low byte: language (2 = English). High byte: sanity flags; bit 1 is
    // "has species", which every real mon has set.
    out->language = pksav_littleendian16(0x0202);

    // Text: import from the source gen, re-export in Gen 3 encoding.
    char tmp[16];
    if (s->is_gen1)
    {
        pksav_gen1_import_text(s->nickname, tmp, PKMN_NAME_TEXT_MAX);
        pksav_gba_export_text(tmp, out->nickname, PKSAV_GBA_POKEMON_NICKNAME_LENGTH);
        pksav_gen1_import_text(s->otname, tmp, TRAINER_NAME_TEXT_MAX);
        pksav_gba_export_text(tmp, out->otname, PKSAV_GBA_POKEMON_OTNAME_LENGTH);
    }
    else
    {
        pksav_gen2_import_text(s->nickname, tmp, PKMN_NAME_TEXT_MAX);
        pksav_gba_export_text(tmp, out->nickname, PKSAV_GBA_POKEMON_NICKNAME_LENGTH);
        pksav_gen2_import_text(s->otname, tmp, TRAINER_NAME_TEXT_MAX);
        pksav_gba_export_text(tmp, out->otname, PKSAV_GBA_POKEMON_OTNAME_LENGTH);
    }

    // Growth block: species, EXP for the current level, base friendship.
    out->blocks.growth.species = pksav_littleendian16(internal);
    out->blocks.growth.held_item = pksav_littleendian16(s->held_item ? gen2_item_to_gen3(s->held_item) : 0);
    uint8_t growth = gen3_growth_rate_for_dex(s->dex);
    out->blocks.growth.exp = pksav_littleendian32(gen3_exp_for_level(s->level, growth));
    out->blocks.growth.friendship = 70;

    // Attacks: move indices are stable across gens 1-3, so copy directly.
    // Gen 1/2 keep a move's PP Ups in the top two bits of its PP byte; Gen 3
    // packs them two bits per move into growth.pp_up.
    uint8_t pp_up = 0;
    for (int k = 0; k < 4; k++)
    {
        out->blocks.attacks.moves[k] = pksav_littleendian16(s->moves[k]);
        out->blocks.attacks.move_pps[k] = (uint8_t)(s->move_pps[k] & 0x3F); // current PP
        if (s->moves[k] != 0)
        {
            pp_up |= (uint8_t)(((s->move_pps[k] >> 6) & 3) << (2 * k));
        }
    }
    out->blocks.growth.pp_up = pp_up;

    // Effort block left zeroed (fresh EVs / contest stats).

    // Misc block: IVs (DV*2 -> 0..30), ability bit from PID, met info.
    uint32_t ivbits = 0;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_HP] * 2) & 0x1F) << 0;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_ATTACK] * 2) & 0x1F) << 5;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_DEFENSE] * 2) & 0x1F) << 10;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPEED] * 2) & 0x1F) << 15;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPECIAL] * 2) & 0x1F) << 20; // Sp.Atk
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPECIAL] * 2) & 0x1F) << 25; // Sp.Def (shared Special DV)
    // Gen 3 only uses PID bit 0 for species that have a second ability; a
    // single-ability species with the bit set would end up with no ability.
    if ((pid & 1u) && gen3_species_uses_ability_bit(s->dex))
    {
        ivbits |= (1u << 31); // second ability slot
    }
    out->blocks.misc.pokerus = s->pokerus;
    out->blocks.misc.iv_egg_ability = pksav_littleendian32(ivbits);

    // Origin info: level met (bits 0-6), origin game (7-10) = FireRed(4),
    // ball (11-14) = Poke Ball(4). A transfer has no honest Gen 3 origin, so
    // this is the most-plausible stamp the format allows.
    uint16_t origin = 0;
    origin |= (uint16_t)(s->level & 0x7F);
    origin |= (uint16_t)(4u << 7);
    origin |= (uint16_t)(4u << 11);
    if (s->ot_female)
    {
        origin |= PKSAV_GBA_POKEMON_OTGENDER_MASK;
    }
    out->blocks.misc.origin_info = pksav_littleendian16(origin);
    // 0xFE is the games' own "met in a trade" location; 0 would read as
    // Littleroot Town / Pallet Town.
    out->blocks.misc.met_location = 0xFE;

    return true;
}

// Remove the Gen 1/2 party mon at index k (shift the contiguous party down).
static void remove_src_party_mon(PokemonSave *src, uint8_t k)
{
    if (src->save_generation_type == SAVE_GENERATION_1)
    {
        struct pksav_gen1_pokemon_party *p = src->save.gen1_save.pokemon_storage.p_party;
        int n = p->count;
        for (int i = k; i < n - 1; i++)
        {
            p->party[i] = p->party[i + 1];
            p->species[i] = p->species[i + 1];
            memcpy(p->otnames[i], p->otnames[i + 1], sizeof(p->otnames[i]));
            memcpy(p->nicknames[i], p->nicknames[i + 1], sizeof(p->nicknames[i]));
        }
        p->count = (uint8_t)(n - 1);
        p->species[p->count] = 0xFF;
    }
    else if (src->save_generation_type == SAVE_GENERATION_2)
    {
        struct pksav_gen2_pokemon_storage *st = &src->save.gen2_save.pokemon_storage;
        struct pksav_gen2_pokemon_party *p = st->p_party;
        int n = p->count;
        for (int i = k; i < n - 1; i++)
        {
            p->party[i] = p->party[i + 1];
            p->species[i] = p->species[i + 1];
            memcpy(p->otnames[i], p->otnames[i + 1], sizeof(p->otnames[i]));
            memcpy(p->nicknames[i], p->nicknames[i + 1], sizeof(p->nicknames[i]));
            st->p_party_mail->party_mail[i] = st->p_party_mail->party_mail[i + 1];
            st->p_party_mail->party_mail_backup[i] = st->p_party_mail->party_mail_backup[i + 1];
        }
        p->count = (uint8_t)(n - 1);
        p->species[p->count] = 0xFF;
        memset(&st->p_party_mail->party_mail[p->count], 0, sizeof(struct pksav_gen2_mail_msg));
        memset(&st->p_party_mail->party_mail_backup[p->count], 0, sizeof(struct pksav_gen2_mail_msg));
    }
}

pksavhelper_error transfer_pkmn_to_gen3(PokemonSave *src, uint8_t src_index, PokemonSave *dst)
{
    if (dst->save_generation_type != SAVE_GENERATION_3)
    {
        return error_swap_pkmn;
    }

    struct src_mon s;
    if (!read_src_mon(src, src_index, &s))
    {
        return error_swap_pkmn; // invalid source slot / unsupported gen
    }
    if (s.is_egg)
    {
        return error_transfer_not_obtainable; // hatch it first
    }
    // Never leave the source with an empty party (the game can't load one).
    int src_party_count = src->save_generation_type == SAVE_GENERATION_1
                              ? src->save.gen1_save.pokemon_storage.p_party->count
                              : src->save.gen2_save.pokemon_storage.p_party->count;
    if (src_party_count <= 1)
    {
        return error_transfer_last_pkmn;
    }

    struct pksav_gba_pc_pokemon mon;
    uint16_t dex;
    if (!build_gen3_mon(&s, &mon, &dex))
    {
        return error_swap_pkmn;
    }
    if (!gen3_species_obtainable(dex))
    {
        return error_transfer_not_obtainable;
    }

    struct pksav_gba_pokemon_storage *storage = &dst->save.gba_save.pokemon_storage;

    // Prefer the party; fall back to the first empty box slot.
    if ((int)storage->p_party->count < PKSAV_GBA_PARTY_NUM_POKEMON)
    {
        int i = (int)storage->p_party->count;
        storage->p_party->party[i].pc_data = mon;
        storage->p_party->party[i].party_data.pokerus_time = 0xFF; // fresh slot: no mail
        gen3_build_party_data(&mon, &storage->p_party->party[i].party_data);
        storage->p_party->count = (uint32_t)(i + 1);
    }
    else
    {
        bool placed = false;
        for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES && !placed; b++)
        {
            for (int slot = 0; slot < PKSAV_GBA_BOX_NUM_POKEMON; slot++)
            {
                if (storage->p_pc->boxes[b].entries[slot].blocks.growth.species == 0)
                {
                    storage->p_pc->boxes[b].entries[slot] = mon;
                    placed = true;
                    break;
                }
            }
        }
        if (!placed)
        {
            return error_transfer_no_space;
        }
    }

    // True transfer: remove from the source (no cloning).
    remove_src_party_mon(src, src_index);
    pokedex_reconcile(dst); // register it as seen + caught on the Gen 3 side
    return error_none;
}

/*
 * Forward (up-generation) Pokémon transfer — Gen 1/2 -> Gen 3. See header.
 * Distributed under the MIT License (MIT).
 */
#include "gen_transfer.h"
#include "pksavhelper.h"   // species maps, PKMN/TRAINER text lengths
#include "gen3_stats.h"    // gen3_build_party_data, growth rate, exp
#include "gen3_species.h"  // gen3_national_to_internal
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
        out->level = p->pc_data.level;
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
        return true;
    }
    return false;
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

    // Fabricate a personality value (Gen 1/2 have none). It deterministically
    // varies per mon and drives nature (PID % 25), ability bit, and gender.
    uint32_t pid = ((uint32_t)s->ot_id << 16) ^ ((uint32_t)s->dex * 2654435761u) ^ ((uint32_t)s->iv_data * 40503u);
    if (pid == 0)
    {
        pid = 0x1a2b3c4d;
    }
    out->personality = pksav_littleendian32(pid);
    out->ot_id.pid = pksav_littleendian16(s->ot_id);
    out->ot_id.sid = 0;
    out->language = 0x02; // English

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
    out->blocks.growth.held_item = 0; // Gen 2 item indices differ; dropped for now
    uint8_t growth = gen3_growth_rate_for_dex(s->dex);
    out->blocks.growth.exp = pksav_littleendian32(gen3_exp_for_level(s->level, growth));
    out->blocks.growth.friendship = 70;

    // Attacks: move indices are stable across gens 1-3, so copy directly.
    for (int k = 0; k < 4; k++)
    {
        out->blocks.attacks.moves[k] = pksav_littleendian16(s->moves[k]);
        out->blocks.attacks.move_pps[k] = (uint8_t)(s->move_pps[k] & 0x3F); // current PP
    }

    // Effort block left zeroed (fresh EVs / contest stats).

    // Misc block: IVs (DV*2 -> 0..30), ability bit from PID, met info.
    uint32_t ivbits = 0;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_HP] * 2) & 0x1F) << 0;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_ATTACK] * 2) & 0x1F) << 5;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_DEFENSE] * 2) & 0x1F) << 10;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPEED] * 2) & 0x1F) << 15;
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPECIAL] * 2) & 0x1F) << 20; // Sp.Atk
    ivbits |= (uint32_t)((dv[PKSAV_GB_IV_SPECIAL] * 2) & 0x1F) << 25; // Sp.Def (shared Special DV)
    if (pid & 1u)
    {
        ivbits |= (1u << 31); // second ability slot
    }
    out->blocks.misc.iv_egg_ability = pksav_littleendian32(ivbits);

    // Origin info: level met (bits 0-6), origin game (7-10) = FireRed(4),
    // ball (11-14) = Poke Ball(4). A transfer has no honest Gen 3 origin, so
    // this is the most-plausible stamp the format allows.
    uint16_t origin = 0;
    origin |= (uint16_t)(s->level & 0x7F);
    origin |= (uint16_t)(4u << 7);
    origin |= (uint16_t)(4u << 11);
    out->blocks.misc.origin_info = pksav_littleendian16(origin);
    out->blocks.misc.met_location = 0;

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
    return error_none;
}

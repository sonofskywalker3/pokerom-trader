#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include "pksavhelper.h"
#include "filehelper.h"
#include "gen3_species.h"
#include "gen3_stats.h"
#include "gen4_pkmn.h" /* Gen 4 PK4 decode for the Boxes view */
#include "gen4_save.h" /* Gen 4 raw slot accessors for moves */
#include "gen4_stats.h" /* Gen 4 party-stat rebuild on withdraw */
#include "pkmn_evolutions.h" /* Pokedex to-do box */

int error_handler(enum pksav_error error, const char *message)
{
    if (error == PKSAV_ERROR_NONE)
    {
        return 0;
    }
    printf("Error handler: %s\n", message);
    char error_message[100];
    sprintf(error_message, "Error code: %d %s", error, message);
    write_to_log(error_message, E_LOG_MESSAGE_TYPE_ERROR);
    return 0;
}

pksavhelper_error update_seen_owned_pkmn(PokemonSave *pkmn_save, uint8_t pokemon_party_index)
{
    enum pksav_error pksav_error;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        const uint8_t *raw = gen4_party_slot_raw(&pkmn_save->save.gen4_save, pokemon_party_index);
        if (raw == NULL)
        {
            return error_update_pokedex;
        }
        uint8_t dec[GEN4_PK4_PARTY_SIZE];
        gen4_pk4_decrypt(raw, dec, true);
        if (gen4_pk4_is_egg(dec))
        {
            return error_none; // an egg isn't dex-recorded until it hatches
        }
        gen4_dex_set_seen_caught(&pkmn_save->save.gen4_save, gen4_pk4_species(dec));
        return error_none;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        uint16_t internal = pksav_littleendian16(pkmn_save->save.gba_save.pokemon_storage.p_party->party[pokemon_party_index].pc_data.blocks.growth.species);
        uint16_t dex = gen3_internal_to_national(internal);
        if (dex == 0)
        {
            return error_none; // egg / unknown: nothing to record
        }
        struct pksav_gba_pokedex *pokedex = &pkmn_save->save.gba_save.pokedex;
        pksav_error = pksav_gba_pokedex_set_has_seen(pokedex, dex, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting gen3 seen bit");
        }
        pksav_error = pksav_set_pokedex_bit(pokedex->p_owned, dex, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting gen3 owned bit");
        }
        return pksav_error == PKSAV_ERROR_NONE ? error_none : error_update_pokedex;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        uint8_t pkmn_species = pkmn_save->save.gen1_save.pokemon_storage.p_party->species[pokemon_party_index];
        uint8_t pkdex_entry = species_gen1_to_gen2[pkmn_species]; // Gen2 is pokedex ordered
        pksav_error = pksav_set_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_seen, pkdex_entry, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting seen pokedex bit");
        }

        pksav_error = pksav_set_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_owned, pkdex_entry, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting owned pokedex bit");
        }
    }
    else
    {
        uint8_t pokemon_species = pkmn_save->save.gen2_save.pokemon_storage.p_party->species[pokemon_party_index];
        pksav_error = pksav_set_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_seen, pokemon_species, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting seen pokedex bit");
        }

        pksav_error = pksav_set_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_owned, pokemon_species, true);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            error_handler(pksav_error, "Error setting owned pokedex bit");
        }
    }

    return pksav_error == PKSAV_ERROR_NONE ? error_none : error_update_pokedex;
}

void swap_party_pkmn_at_indices(struct pksav_gen2_save *save, uint8_t pkmn_party_index1, uint8_t pkmn_party_index2)
{
    // swap nickname
    char tmp_nickname1[PKMN_NAME_TEXT_MAX + 1] = "\0";
    char tmp_nickname2[PKMN_NAME_TEXT_MAX + 1] = "\0";
    pksav_gen2_import_text(save->pokemon_storage.p_party->nicknames[pkmn_party_index1], tmp_nickname1, PKMN_NAME_TEXT_MAX);
    pksav_gen2_import_text(save->pokemon_storage.p_party->nicknames[pkmn_party_index2], tmp_nickname2, PKMN_NAME_TEXT_MAX);
    pksav_gen2_export_text(tmp_nickname2, save->pokemon_storage.p_party->nicknames[pkmn_party_index1], PKMN_NAME_TEXT_MAX);
    pksav_gen2_export_text(tmp_nickname1, save->pokemon_storage.p_party->nicknames[pkmn_party_index2], PKMN_NAME_TEXT_MAX);
    save->pokemon_storage.p_party->nicknames[pkmn_party_index1][strlen(tmp_nickname2)] = 0x50;
    save->pokemon_storage.p_party->nicknames[pkmn_party_index2][strlen(tmp_nickname1)] = 0x50;

    // swap party data
    struct pksav_gen2_party_pokemon tmp_pokemon = save->pokemon_storage.p_party->party[pkmn_party_index1];
    save->pokemon_storage.p_party->party[pkmn_party_index1] = save->pokemon_storage.p_party->party[pkmn_party_index2];
    save->pokemon_storage.p_party->party[pkmn_party_index2] = tmp_pokemon;

    // swap species
    uint8_t tmp_species = save->pokemon_storage.p_party->species[pkmn_party_index1];
    save->pokemon_storage.p_party->species[pkmn_party_index1] = save->pokemon_storage.p_party->species[pkmn_party_index2];
    save->pokemon_storage.p_party->species[pkmn_party_index2] = tmp_species;

    // swap otnames
    char tmp_otname1[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    char tmp_otname2[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    pksav_gen2_import_text(save->pokemon_storage.p_party->otnames[pkmn_party_index1], tmp_otname1, TRAINER_NAME_TEXT_MAX);
    pksav_gen2_import_text(save->pokemon_storage.p_party->otnames[pkmn_party_index2], tmp_otname2, TRAINER_NAME_TEXT_MAX);
    pksav_gen2_export_text(tmp_otname2, save->pokemon_storage.p_party->otnames[pkmn_party_index1], TRAINER_NAME_TEXT_MAX);
    pksav_gen2_export_text(tmp_otname1, save->pokemon_storage.p_party->otnames[pkmn_party_index2], TRAINER_NAME_TEXT_MAX);
    save->pokemon_storage.p_party->otnames[pkmn_party_index1][strlen(tmp_otname2)] = 0x50;
    save->pokemon_storage.p_party->otnames[pkmn_party_index2][strlen(tmp_otname1)] = 0x50;
}

// Extracts the trainer info from the save file and updates the trainer struct
void create_trainer(PokemonSave *pkmn_save, struct trainer_info *trainer)
{
    SaveGenerationType save_generation_type = pkmn_save->save_generation_type;

    switch (save_generation_type)
    {
    case SAVE_GENERATION_1:
    {
        // Trainer name
        char trainer_name[TRAINER_NAME_TEXT_MAX + 1] = "\0";
        pksav_gen1_import_text(pkmn_save->save.gen1_save.trainer_info.p_name, trainer_name, TRAINER_NAME_TEXT_MAX);

        // Trainer Id
        uint16_t trainer_id = pksav_bigendian16(*pkmn_save->save.gen1_save.trainer_info.p_id);

        // Update the trainer struct
        strcpy(trainer->trainer_name, trainer_name);
        trainer->trainer_id = trainer_id;

        trainer->trainer_badges[0] = *pkmn_save->save.gen1_save.trainer_info.p_badges;

        // Update the trainer's pokemon party
        trainer->pokemon_party.gen1_pokemon_party = *pkmn_save->save.gen1_save.pokemon_storage.p_party;
        trainer->trainer_generation = SAVE_GENERATION_1;
        break;
    }
    case SAVE_GENERATION_2:
    {
        // Trainer name
        char trainer_name[TRAINER_NAME_TEXT_MAX + 1] = "\0";
        pksav_gen2_import_text(pkmn_save->save.gen2_save.trainer_info.p_name, trainer_name, TRAINER_NAME_TEXT_MAX);

        // Update the trainer struct
        strcpy(trainer->trainer_name, trainer_name);
        trainer->trainer_id = pksav_bigendian16(*pkmn_save->save.gen2_save.trainer_info.p_id);
        trainer->trainer_badges[EBadge_Region_Johto] = *pkmn_save->save.gen2_save.trainer_info.p_johto_badges;
        trainer->trainer_badges[EBadge_Region_Kanto] = *pkmn_save->save.gen2_save.trainer_info.p_kanto_badges;
        // Trainer Gender (Crystal and later games only)
        if (pkmn_save->save.gen2_save.save_type == PKSAV_GEN2_SAVE_TYPE_CRYSTAL)
        {
            trainer->trainer_gender = *pkmn_save->save.gen2_save.trainer_info.p_gender;
        }

        // Update the trainer's pokemon party
        trainer->pokemon_party.gen2_pokemon_party = *pkmn_save->save.gen2_save.pokemon_storage.p_party;
        trainer->trainer_generation = SAVE_GENERATION_2;

        // update party mail
        for (int i = 0; i < 6; i++)
        {
            trainer->trainer_mail[i] = pkmn_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[i];
        }
        break;
    }
    case SAVE_GENERATION_3:
    {
        char trainer_name[TRAINER_NAME_TEXT_MAX + 1] = "\0";
        pksav_gba_import_text(pkmn_save->save.gba_save.player_info.p_name, trainer_name, TRAINER_NAME_TEXT_MAX);
        strcpy(trainer->trainer_name, trainer_name);
        trainer->trainer_id = pksav_littleendian16(pkmn_save->save.gba_save.player_info.p_id->pid);
        trainer->pokemon_party.gba_pokemon_party = *pkmn_save->save.gba_save.pokemon_storage.p_party;
        trainer->trainer_generation = SAVE_GENERATION_3;
        break;
    }
    case SAVE_GENERATION_4:
    {
        char trainer_name[TRAINER_NAME_TEXT_MAX + 1] = "\0";
        gen4_trainer_name(&pkmn_save->save.gen4_save, trainer_name);
        strcpy(trainer->trainer_name, trainer_name);
        trainer->trainer_id = gen4_trainer_id(&pkmn_save->save.gen4_save);
        // Snapshot the party decrypted so the trade UI can read it directly.
        struct gen4_pokemon_party *party = &trainer->pokemon_party.gen4_pokemon_party;
        party->count = gen4_party_count(&pkmn_save->save.gen4_save);
        for (int i = 0; i < party->count; i++)
        {
            gen4_pk4_decrypt(gen4_party_slot_raw(&pkmn_save->save.gen4_save, i), party->dec[i], true);
        }
        trainer->trainer_generation = SAVE_GENERATION_4;
        break;
    }
    default:
        break;
    }
}

// Checks if supplied Gen 1 pokemon is eligible for trade evolution
enum eligible_evolution_status check_trade_evolution_gen1(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    // Species index of pokemon being checked
    uint8_t species = pkmn_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index];
    struct pkmn_evolution_pair_data evo_pair = pkmn_evolution_pairs_gen1[species];

    // Check if the pokemon has an initialized evolution pair
    return species == evo_pair.species_index;
}

// Checks if supplied Gen 2 pokemon is eligible for trade evolution
enum eligible_evolution_status check_trade_evolution_gen2(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    // Species index of pokemon being checked
    int species = pkmn_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index];
    struct pkmn_evolution_pair_data evo_pair = pkmn_evolution_pairs_gen2[species];

    // If the pkmn species has an initialized evolution pair
    if (species == evo_pair.species_index)
    {
        return E_EVO_STATUS_ELIGIBLE;
    }

    return E_EVO_STATUS_NOT_ELIGIBLE;
}

// --- Gen 3 trade evolutions ---
// Held-item indices are Gen 3 item IDs.
#define GEN3_ITEM_KINGS_ROCK   187
#define GEN3_ITEM_DEEPSEATOOTH 226
#define GEN3_ITEM_DEEPSEASCALE 227
#define GEN3_ITEM_UPGRADE      229
#define GEN3_ITEM_METAL_COAT   233
#define GEN3_ITEM_DRAGON_SCALE 235

// Trade-evolution table keyed by National Dex. required_item == 0 is a plain
// trade evolution; otherwise the held item must match and is consumed.
struct gen3_trade_evo
{
    uint16_t from_dex;
    uint16_t to_dex;
    uint16_t required_item;
};

static const struct gen3_trade_evo gen3_trade_evos[] = {
    {64, 65, 0},                        // Kadabra   -> Alakazam
    {67, 68, 0},                        // Machoke   -> Machamp
    {75, 76, 0},                        // Graveler  -> Golem
    {93, 94, 0},                        // Haunter   -> Gengar
    {61, 186, GEN3_ITEM_KINGS_ROCK},    // Poliwhirl -> Politoed
    {79, 199, GEN3_ITEM_KINGS_ROCK},    // Slowpoke  -> Slowking
    {95, 208, GEN3_ITEM_METAL_COAT},    // Onix      -> Steelix
    {123, 212, GEN3_ITEM_METAL_COAT},   // Scyther   -> Scizor
    {117, 230, GEN3_ITEM_DRAGON_SCALE}, // Seadra    -> Kingdra
    {137, 233, GEN3_ITEM_UPGRADE},      // Porygon   -> Porygon2
    {366, 367, GEN3_ITEM_DEEPSEATOOTH}, // Clamperl  -> Huntail
    {366, 368, GEN3_ITEM_DEEPSEASCALE}, // Clamperl  -> Gorebyss
};

// Find the trade-evolution entry for a party mon. Returns the entry, or NULL if
// the species has no trade evolution. *out_missing_item is set true when the
// species does have an item-based evolution but isn't holding the right item.
static const struct gen3_trade_evo *gen3_find_trade_evo(const struct pksav_gba_pc_pokemon *pc, bool *out_missing_item)
{
    if (out_missing_item)
    {
        *out_missing_item = false;
    }
    uint16_t dex = gen3_internal_to_national(pksav_littleendian16(pc->blocks.growth.species));
    uint16_t held = pksav_littleendian16(pc->blocks.growth.held_item);
    bool species_has_evo = false;
    size_t count = sizeof(gen3_trade_evos) / sizeof(gen3_trade_evos[0]);
    for (size_t i = 0; i < count; i++)
    {
        if (gen3_trade_evos[i].from_dex != dex)
        {
            continue;
        }
        species_has_evo = true;
        if (gen3_trade_evos[i].required_item == 0 || gen3_trade_evos[i].required_item == held)
        {
            return &gen3_trade_evos[i];
        }
    }
    if (species_has_evo && out_missing_item)
    {
        *out_missing_item = true; // has an item evolution but not holding it
    }
    return NULL;
}

enum eligible_evolution_status check_trade_evolution_gen3(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    const struct pksav_gba_pc_pokemon *pc = &pkmn_save->save.gba_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data;
    bool missing_item = false;
    const struct gen3_trade_evo *evo = gen3_find_trade_evo(pc, &missing_item);
    if (evo != NULL)
    {
        return E_EVO_STATUS_ELIGIBLE;
    }
    return missing_item ? E_EVO_STATUS_MISSING_ITEM : E_EVO_STATUS_NOT_ELIGIBLE;
}

// Gen 1/2 DVs live in a big-endian 16-bit bitset; pksav's accessors expect
// the host-order value (Attack in the top nibble), so convert around them.
// Passing the raw field swaps Attack<->Speed and Defense<->Special on x86.
void gb_get_dvs(uint16_t raw_iv_data, uint8_t *dvs_out)
{
    uint16_t host = pksav_bigendian16(raw_iv_data);
    pksav_get_gb_IVs(&host, dvs_out, PKSAV_NUM_GB_IVS);
}

void gb_set_dv(enum pksav_gb_IV stat, uint8_t value, uint16_t *p_raw_iv_data)
{
    uint16_t host = pksav_bigendian16(*p_raw_iv_data);
    pksav_set_gb_IV(stat, value, &host);
    *p_raw_iv_data = pksav_bigendian16(host);
}

// Function to calculate HP based on base stat, IV, Stat Exp, and level
// (16-bit: a level-100 mon can exceed 255 HP)
uint16_t calculate_hp(uint8_t level, uint8_t base_hp, uint8_t dv_hp, uint16_t stat_exp)
{
    float hp_calc = (base_hp + dv_hp) * 2 + floor(ceil(sqrt(stat_exp)) / 4);
    hp_calc = hp_calc * level / 100.0;
    hp_calc = floor(hp_calc) + level + 10;

    return (uint16_t)hp_calc;
}

// Function to calculate stats (Attack, Defense, Special, Speed)
// based on base stat, IV, Stat Exp, and level
uint16_t calculate_stat(uint8_t level, uint8_t base_stat, uint8_t dv, uint16_t stat_exp)
{
    float stat_calc = (base_stat + dv) * 2 + floor(ceil(sqrt(stat_exp)) / 4);
    stat_calc = (stat_calc * level) / 100.0;
    stat_calc = floor(stat_calc) + 5;

    return (uint16_t)stat_calc;
}
/************************************************************************
 * Simulate the random number generation for Generation 1
 */
static uint8_t r_div = 0;
static uint8_t carry_bit = 0;
static uint8_t add_byte = 0;
static uint8_t subtract_byte = 0;
// Simulated hardware registers
static uint8_t h_random_add = 0; // h_random_add register
static uint8_t h_random_sub = 0; // h_random_sub register

// Simulate one step of the random number generation process
void generate_random_number_step(void)
{
    // Increment r_div (simulated as an 8-bit counter)
    r_div++;

    // Simulate the addition step
    uint16_t sum = r_div + carry_bit + add_byte;
    carry_bit = (sum > 255) ? 1 : 0;
    add_byte = sum & 0xFF;

    // Simulate the subtraction step
    uint16_t difference = r_div + carry_bit - subtract_byte;
    carry_bit = (difference > 255) ? 1 : 0;
    subtract_byte = difference & 0xFF;
}

// Get a random byte from the simulated random number generation process
uint8_t get_random_byte(SaveGenerationType save_generation_type)
{
    if (save_generation_type == SAVE_GENERATION_1)
    {
        return add_byte; // Return the add byte as the generated random number
    }
    else
    {
        return h_random_add; // Return the simulated hRandomAdd register
    }
}

// Gen 2 used hardware registers to generate random numbers
// Perform the random number generation step
void generate_random_number_step_gen2(void)
{
    r_div++; // Increment rDiv (simulated as an 8-bit counter)

    // Simulate the addition step
    uint16_t sum = r_div + h_random_add;
    h_random_add = (uint8_t)sum;

    // Simulate the subtraction step
    uint16_t diff = r_div - h_random_sub;
    h_random_sub = (uint8_t)diff;
}

void generate_rand_num_step(SaveGenerationType save_generation_type)
{
    for (int i = 0; i < rand(); i++)
    {
        if (save_generation_type == SAVE_GENERATION_1)
        {
            generate_random_number_step();
        }
        else
        {
            generate_random_number_step_gen2();
        }
    }
}

void randomize_dvs(uint8_t *dv_array, SaveGenerationType save_generation_type)
{
    for (int i = 0; i < PKSAV_NUM_GB_IVS - 1; i++)
    {
        // random int between 0 and 15
        dv_array[i] = (uint8_t)(get_random_byte(save_generation_type) & 0xF);
        generate_rand_num_step(save_generation_type);
    }

    // Generate HP dv from other dvs (string LSBs together)
    dv_array[PKSAV_GB_IV_HP] = (((dv_array[PKSAV_GB_IV_ATTACK] & 0x01) << 3) |
                                ((dv_array[PKSAV_GB_IV_DEFENSE] & 0x01) << 2) |
                                ((dv_array[PKSAV_GB_IV_SPEED] & 0x01) << 1) |
                                (dv_array[PKSAV_GB_IV_SPECIAL] & 0x01));
}
/*************************************************************************/

// Randomize the DVs of a pokemon
void update_pkmn_DVs(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    // randomize the dvs on trade except for HP
    uint8_t traded_pkmn_rand_dvs[PKSAV_NUM_GB_IVS] = {0};
    randomize_dvs(traded_pkmn_rand_dvs, pkmn_save->save_generation_type);

    // set the ivs to pokemon at index
    for (int i = PKSAV_GB_IV_ATTACK; i < PKSAV_NUM_GB_IVS; i++)
    {
        if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
            gb_set_dv(i, traded_pkmn_rand_dvs[i], &pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.iv_data);
        else
            gb_set_dv(i, traded_pkmn_rand_dvs[i], &pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.iv_data);
    }
}

// Calculate and update the pokemon's stats based on its level, base stats, IVs, and EVs
void update_pkmn_stats(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    // Gen 3 uses a different (nature-aware) formula; rebuild party_data wholesale.
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        struct pksav_gba_party_pokemon *p = &pkmn_save->save.gba_save.pokemon_storage.p_party->party[pkmn_party_index];
        gen3_build_party_data(&p->pc_data, &p->party_data);
        return;
    }

    // Get the pokemon's DVs
    uint8_t pkmn_dvs[PKSAV_NUM_GB_IVS];
    gb_get_dvs(pkmn_save->save_generation_type == SAVE_GENERATION_1
                   ? pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.iv_data
                   : pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.iv_data,
               pkmn_dvs);

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        // Get species index
        uint8_t species_index = pkmn_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index];
        uint8_t pkmn_level = pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.level;

        // Get the pokemon's EVs
        struct pksav_gen1_pc_pokemon pkmn_ev_data = pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data;
        const uint16_t pkmn_evs[PKSAV_NUM_GB_IVS] = {
            [PKSAV_GB_IV_ATTACK] = pksav_bigendian16(pkmn_ev_data.ev_atk),
            [PKSAV_GB_IV_DEFENSE] = pksav_bigendian16(pkmn_ev_data.ev_def),
            [PKSAV_GB_IV_SPEED] = pksav_bigendian16(pkmn_ev_data.ev_spd),
            [PKSAV_GB_IV_SPECIAL] = pksav_bigendian16(pkmn_ev_data.ev_spcl),
            [PKSAV_GB_IV_HP] = pksav_bigendian16(pkmn_ev_data.ev_hp)};

        uint16_t pkmn_stats[PKSAV_GEN1_STAT_COUNT] = {
            // Calculate the pokemon's stats
            [PKSAV_GEN1_STAT_ATTACK] = calculate_stat(pkmn_level, pkmn_base_stats_gen1[species_index].atk, pkmn_dvs[PKSAV_GB_IV_ATTACK], pkmn_evs[PKSAV_GB_IV_ATTACK]),
            [PKSAV_GEN1_STAT_DEFENSE] = calculate_stat(pkmn_level, pkmn_base_stats_gen1[species_index].def, pkmn_dvs[PKSAV_GB_IV_DEFENSE], pkmn_evs[PKSAV_GB_IV_DEFENSE]),
            [PKSAV_GEN1_STAT_SPEED] = calculate_stat(pkmn_level, pkmn_base_stats_gen1[species_index].spd, pkmn_dvs[PKSAV_GB_IV_SPEED], pkmn_evs[PKSAV_GB_IV_SPEED]),
            [PKSAV_GEN1_STAT_SPECIAL] = calculate_stat(pkmn_level, pkmn_base_stats_gen1[species_index].spcl, pkmn_dvs[PKSAV_GB_IV_SPECIAL], pkmn_evs[PKSAV_GB_IV_SPECIAL]),
            // Calculate the pokemon's HP stat
            [PKSAV_GEN1_STAT_HP] = calculate_hp(pkmn_level, pkmn_base_stats_gen1[species_index].max_hp, pkmn_dvs[PKSAV_GB_IV_HP], pkmn_evs[PKSAV_GB_IV_HP])};

        // Update the pokemon's stats
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.atk = pksav_bigendian16(pkmn_stats[PKSAV_GEN1_STAT_ATTACK]);
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.def = pksav_bigendian16(pkmn_stats[PKSAV_GEN1_STAT_DEFENSE]);
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.spd = pksav_bigendian16(pkmn_stats[PKSAV_GEN1_STAT_SPEED]);
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.spcl = pksav_bigendian16(pkmn_stats[PKSAV_GEN1_STAT_SPECIAL]);
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.max_hp = pksav_bigendian16(pkmn_stats[PKSAV_GEN1_STAT_HP]);
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.current_hp = pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.max_hp;
    }
    else
    {
        // Get the species index
        uint8_t species_index = pkmn_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index];
        uint8_t pkmn_level = pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.level;

        // Get the pokemon's EVs
        struct pksav_gen2_pc_pokemon pkmn_ev_data = pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data;
        const uint16_t pkmn_evs[PKSAV_NUM_GB_IVS] = {
            [PKSAV_GB_IV_ATTACK] = pksav_bigendian16(pkmn_ev_data.ev_atk),
            [PKSAV_GB_IV_DEFENSE] = pksav_bigendian16(pkmn_ev_data.ev_def),
            [PKSAV_GB_IV_SPEED] = pksav_bigendian16(pkmn_ev_data.ev_spd),
            [PKSAV_GB_IV_SPECIAL] = pksav_bigendian16(pkmn_ev_data.ev_spcl),
            [PKSAV_GB_IV_HP] = pksav_bigendian16(pkmn_ev_data.ev_hp)};

        uint16_t pkmn_stats[PKSAV_GEN2_STAT_COUNT] = {
            // Calculate the pokemon's stats
            [PKSAV_GEN2_STAT_ATTACK] = calculate_stat(pkmn_level, pkmn_base_stats_gen2[species_index].atk, pkmn_dvs[PKSAV_GB_IV_ATTACK], pkmn_evs[PKSAV_GB_IV_ATTACK]),
            [PKSAV_GEN2_STAT_DEFENSE] = calculate_stat(pkmn_level, pkmn_base_stats_gen2[species_index].def, pkmn_dvs[PKSAV_GB_IV_DEFENSE], pkmn_evs[PKSAV_GB_IV_DEFENSE]),
            [PKSAV_GEN2_STAT_SPEED] = calculate_stat(pkmn_level, pkmn_base_stats_gen2[species_index].spd, pkmn_dvs[PKSAV_GB_IV_SPEED], pkmn_evs[PKSAV_GB_IV_SPEED]),
            [PKSAV_GEN2_STAT_SPATK] = calculate_stat(pkmn_level, pkmn_base_stats_gen2[species_index].spatk, pkmn_dvs[PKSAV_GB_IV_SPECIAL], pkmn_evs[PKSAV_GB_IV_SPECIAL]),
            [PKSAV_GEN2_STAT_SPDEF] = calculate_stat(pkmn_level, pkmn_base_stats_gen2[species_index].spdef, pkmn_dvs[PKSAV_GB_IV_SPECIAL], pkmn_evs[PKSAV_GB_IV_SPECIAL]),
            // Calculate the pokemon's HP stat
            [PKSAV_GEN2_STAT_HP] = calculate_hp(pkmn_level, pkmn_base_stats_gen2[species_index].max_hp, pkmn_dvs[PKSAV_GB_IV_HP], pkmn_evs[PKSAV_GB_IV_HP])};

        // Update the pokemon's stats
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.atk = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_ATTACK]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.def = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_DEFENSE]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.spd = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_SPEED]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.spatk = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_SPATK]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.spdef = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_SPDEF]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.max_hp = pksav_bigendian16(pkmn_stats[PKSAV_GEN2_STAT_HP]);
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.current_hp = pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.max_hp;
    }
}

bool is_move_HM(uint8_t move_index)
{
    return move_index == MOVE_INDEX_HM01 || move_index == MOVE_INDEX_HM02 || move_index == MOVE_INDEX_HM03 || move_index == MOVE_INDEX_HM04 || move_index == MOVE_INDEX_HM05 || move_index == MOVE_INDEX_HM06;
}

enum eligible_trade_status check_trade_eligibility(struct trainer_info *trainer, uint8_t pkmn_party_index)
{
    // Gen 3 <-> Gen 3 trades have no cross-gen restrictions (only reached for
    // cross-gen pairs, which never involve Gen 3).
    if (trainer->trainer_generation == SAVE_GENERATION_3)
    {
        (void)pkmn_party_index;
        return E_TRADE_STATUS_ELIGIBLE;
    }

    // All Gen1 saves are eligible for trade except with HM moves
    if (trainer->trainer_generation == SAVE_GENERATION_1)
    {
        for (int move_index = 0; move_index < 4; move_index++)
        {
            uint8_t move = trainer->pokemon_party.gen1_pokemon_party.party[pkmn_party_index].pc_data.moves[move_index];
            if (is_move_HM(move))
            {
                return E_TRADE_STATUS_HM_MOVE;
            }
        }
        return E_TRADE_STATUS_ELIGIBLE;
    }

    if (trainer->trainer_mail[pkmn_party_index].item_id != 0)
    {
        return E_TRADE_STATUS_MAIL;
    }

    // Prevent Gen2 only pkmn from going to Gen1
    uint8_t gen2_species = trainer->pokemon_party.gen2_pokemon_party.species[pkmn_party_index];
    if (gen2_species > 151)
    {
        return E_TRADE_STATUS_GEN2_PKMN;
    }

    // Prevent Gen2 only moves from going to Gen1
    struct pksav_gen2_party_pokemon gen2_pkmn = trainer->pokemon_party.gen2_pokemon_party.party[pkmn_party_index];
    for (int i = 0; i < 4; i++)
    {
        uint8_t move_index = gen2_pkmn.pc_data.moves[i];
        if (move_index > MAX_GEN1_MOVE_INDEX)
        {
            return E_TRADE_STATUS_GEN2_MOVE;
        }
        // flag HM moves
        if (is_move_HM(move_index))
        {
            return E_TRADE_STATUS_HM_MOVE;
        }
    }

    return E_TRADE_STATUS_ELIGIBLE;
}

bool check_if_reds_pikachu(const PokemonSave *pkmn_save, const uint8_t pkmn_party_index)
{
    char tmp_otname_pkmn[PKMN_NAME_TEXT_MAX + 1] = "\0";
    pksav_gen1_import_text(pkmn_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index], tmp_otname_pkmn, PKMN_NAME_TEXT_MAX);
    char tmp_otname_trainer[PKMN_NAME_TEXT_MAX + 1] = "\0";
    pksav_gen1_import_text(pkmn_save->save.gen1_save.trainer_info.p_name, tmp_otname_trainer, PKMN_NAME_TEXT_MAX);
    return pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.species == SI_PIKACHU &&                                 // Is a Pikachu
           pkmn_save->save.gen1_save.save_type == PKSAV_GEN1_SAVE_TYPE_YELLOW &&                                                                       // Is from Yellow
           pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.ot_id == *pkmn_save->save.gen1_save.trainer_info.p_id && // has the same OT ID as the trainer
           strcmp(tmp_otname_pkmn, tmp_otname_trainer) == 0;                                                                                           // // has the same OT name as the trainer
}

pksavhelper_error swap_pkmn_at_index_between_saves_cross_gen(PokemonSave *player1_save, PokemonSave *player2_save, uint8_t pkmn_party_index1, uint8_t pkmn_party_index2)
{
    enum pksav_error pksav_error;

    // Since this is cross-gen assign the player by generation
    PokemonSave *player_gen1;
    PokemonSave *player_gen2;

    if (player1_save->save_generation_type == SAVE_GENERATION_1)
    {
        player_gen1 = player1_save;
        player_gen2 = player2_save;
    }
    else
    {
        player_gen1 = player2_save;
        player_gen2 = player1_save;
        // swap the indexes
        uint8_t tmp_index = pkmn_party_index1;
        pkmn_party_index1 = pkmn_party_index2;
        pkmn_party_index2 = tmp_index;
    }

    // swap nickname
    char tmp_nickname1[PKMN_NAME_TEXT_MAX + 1] = "\0";
    char tmp_nickname2[PKMN_NAME_TEXT_MAX + 1] = "\0";
    pksav_error = pksav_gen1_import_text(player_gen1->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], tmp_nickname1, PKMN_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen2_import_text(player_gen2->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], tmp_nickname2, PKMN_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen1_export_text(tmp_nickname2, player_gen1->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], PKMN_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen2_export_text(tmp_nickname1, player_gen2->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], PKMN_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    player_gen1->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1][strlen(tmp_nickname2)] = 0x50;
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    player_gen2->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2][strlen(tmp_nickname1)] = 0x50;
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }

    // swap party
    // party_data
    struct pksav_gen1_party_pokemon tmp_pokemon_gen1 = player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1];
    struct pksav_gen2_party_pokemon tmp_pokemon_gen2 = player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2];

    // gen1 -> gen2 party_data
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.level = tmp_pokemon_gen1.party_data.level;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.max_hp = tmp_pokemon_gen1.party_data.max_hp;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.atk = tmp_pokemon_gen1.party_data.atk;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.def = tmp_pokemon_gen1.party_data.def;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.spd = tmp_pokemon_gen1.party_data.spd;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.spatk = tmp_pokemon_gen1.party_data.spcl;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.spdef = tmp_pokemon_gen1.party_data.spcl;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.condition = PKSAV_CONDITION_NONE;

    // gen2 -> gen1 party_data
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.level = tmp_pokemon_gen2.pc_data.level;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.max_hp = tmp_pokemon_gen2.party_data.max_hp;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.atk = tmp_pokemon_gen2.party_data.atk;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.def = tmp_pokemon_gen2.party_data.def;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.spd = tmp_pokemon_gen2.party_data.spd;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.spcl = tmp_pokemon_gen2.party_data.spatk;

    // pc_data
    // gen1 -> gen2 pc_data
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.species = species_gen1_to_gen2[tmp_pokemon_gen1.pc_data.species];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ot_id = tmp_pokemon_gen1.pc_data.ot_id;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.exp[0] = tmp_pokemon_gen1.pc_data.exp[0];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.exp[1] = tmp_pokemon_gen1.pc_data.exp[1];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.exp[2] = tmp_pokemon_gen1.pc_data.exp[2];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ev_hp = tmp_pokemon_gen1.pc_data.ev_hp;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ev_atk = tmp_pokemon_gen1.pc_data.ev_atk;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ev_def = tmp_pokemon_gen1.pc_data.ev_def;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ev_spd = tmp_pokemon_gen1.pc_data.ev_spd;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.ev_spcl = tmp_pokemon_gen1.pc_data.ev_spcl;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.iv_data = tmp_pokemon_gen1.pc_data.iv_data;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.move_pps[0] = tmp_pokemon_gen1.pc_data.move_pps[0];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.move_pps[1] = tmp_pokemon_gen1.pc_data.move_pps[1];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.move_pps[2] = tmp_pokemon_gen1.pc_data.move_pps[2];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.move_pps[3] = tmp_pokemon_gen1.pc_data.move_pps[3];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.moves[0] = tmp_pokemon_gen1.pc_data.moves[0];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.moves[1] = tmp_pokemon_gen1.pc_data.moves[1];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.moves[2] = tmp_pokemon_gen1.pc_data.moves[2];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.moves[3] = tmp_pokemon_gen1.pc_data.moves[3];

    // gen2 -> gen1 pc_data
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.species = species_gen2_to_gen1[tmp_pokemon_gen2.pc_data.species];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.level = tmp_pokemon_gen2.pc_data.level;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.condition = PKSAV_CONDITION_NONE;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.moves[0] = tmp_pokemon_gen2.pc_data.moves[0];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.moves[1] = tmp_pokemon_gen2.pc_data.moves[1];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.moves[2] = tmp_pokemon_gen2.pc_data.moves[2];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.moves[3] = tmp_pokemon_gen2.pc_data.moves[3];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ot_id = tmp_pokemon_gen2.pc_data.ot_id;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.exp[0] = tmp_pokemon_gen2.pc_data.exp[0];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.exp[1] = tmp_pokemon_gen2.pc_data.exp[1];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.exp[2] = tmp_pokemon_gen2.pc_data.exp[2];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ev_hp = tmp_pokemon_gen2.pc_data.ev_hp;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ev_atk = tmp_pokemon_gen2.pc_data.ev_atk;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ev_def = tmp_pokemon_gen2.pc_data.ev_def;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ev_spd = tmp_pokemon_gen2.pc_data.ev_spd;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.ev_spcl = tmp_pokemon_gen2.pc_data.ev_spcl;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.iv_data = tmp_pokemon_gen2.pc_data.iv_data;
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.move_pps[0] = tmp_pokemon_gen2.pc_data.move_pps[0];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.move_pps[1] = tmp_pokemon_gen2.pc_data.move_pps[1];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.move_pps[2] = tmp_pokemon_gen2.pc_data.move_pps[2];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.move_pps[3] = tmp_pokemon_gen2.pc_data.move_pps[3];

    // data not found in gen1
    uint8_t item_override = trade_catch_rate_to_item[tmp_pokemon_gen1.pc_data.catch_rate];
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.held_item = item_override ? item_override : tmp_pokemon_gen1.pc_data.catch_rate;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.caught_data = (uint16_t)0;
    bool is_reds_pikachu_from_yellow = check_if_reds_pikachu(player_gen1, pkmn_party_index1);
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.friendship = is_reds_pikachu_from_yellow ? GEN2_FRIENDSHIP_BASE_REDS_PIKACHU : GEN2_FRIENDSHIP_BASE;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].pc_data.pokerus = (uint8_t)0;

    // data not found in gen2
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.types[0] = pkmn_base_stats_gen1[player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.species].types[0];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.types[1] = pkmn_base_stats_gen1[player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.species].types[1];
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.catch_rate = pkmn_base_stats_gen1[player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.species].catch_rate;

    // swap species
    uint8_t tmp_species_gen1 = player_gen1->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index1];
    uint8_t tmp_species_gen2 = player_gen2->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index2];

    // swap the indexes
    player_gen1->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index1] = species_gen2_to_gen1[tmp_species_gen2];
    player_gen2->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index2] = species_gen1_to_gen2[tmp_species_gen1];

    // swap otnames
    char tmp_otname_gen1[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    char tmp_otname_gen2[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    pksav_error = pksav_gen1_import_text(player_gen1->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1], tmp_otname_gen1, TRAINER_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen2_import_text(player_gen2->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2], tmp_otname_gen2, TRAINER_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen1_export_text(tmp_otname_gen2, player_gen1->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1], TRAINER_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    pksav_error = pksav_gen2_export_text(tmp_otname_gen1, player_gen2->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2], TRAINER_NAME_TEXT_MAX);
    if (pksav_error != PKSAV_ERROR_NONE)
    {
        return error_swap_pkmn;
    }
    player_gen1->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1][strlen(tmp_otname_gen2)] = 0x50;
    player_gen2->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2][strlen(tmp_otname_gen1)] = 0x50;

    // Fill pkmn hp to max
    player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].pc_data.current_hp = player_gen1->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1].party_data.max_hp;
    player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.current_hp = player_gen2->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2].party_data.max_hp;

    return error_none;
}

pksavhelper_error swap_pkmn_at_index_between_saves(PokemonSave *player1_save, PokemonSave *player2_save, uint8_t pkmn_party_index1, uint8_t pkmn_party_index2)
{
    enum pksav_error pksav_error;

    // Gen 4 <-> Gen 4: a PK4 is self-contained (encryption keyed by its own
    // PID/checksum, no gen-internal species indices), so trading is a raw swap
    // of the two 236-byte party slots — valid across DP/Pt/HGSS too.
    if (player1_save->save_generation_type == SAVE_GENERATION_4)
    {
        bool swapped = gen4_swap_party_slots(&player1_save->save.gen4_save, pkmn_party_index1,
                                             &player2_save->save.gen4_save, pkmn_party_index2);
        return swapped ? error_none : error_swap_pkmn;
    }

    // Gen 3 <-> Gen 3: the entire party entry (pc_data + derived party_data)
    // travels together, so a trade is just exchanging the two structs. OT, PID,
    // IVs, EVs and stats are all preserved (a real Gen 3 trade keeps OT).
    if (player1_save->save_generation_type == SAVE_GENERATION_3)
    {
        struct pksav_gba_pokemon_party *p1 = player1_save->save.gba_save.pokemon_storage.p_party;
        struct pksav_gba_pokemon_party *p2 = player2_save->save.gba_save.pokemon_storage.p_party;
        struct pksav_gba_party_pokemon tmp = p1->party[pkmn_party_index1];
        p1->party[pkmn_party_index1] = p2->party[pkmn_party_index2];
        p2->party[pkmn_party_index2] = tmp;
        return error_none;
    }

    // swap nickname
    char tmp_nickname1[PKMN_NAME_TEXT_MAX + 1] = "\0";
    char tmp_nickname2[PKMN_NAME_TEXT_MAX + 1] = "\0";
    if (player1_save->save_generation_type == SAVE_GENERATION_1)
    {
        pksav_error = pksav_gen1_import_text(player1_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], tmp_nickname1, PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_import_text(player2_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], tmp_nickname2, PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_export_text(tmp_nickname2, player1_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_export_text(tmp_nickname1, player2_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        player1_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index1][strlen(tmp_nickname2)] = 0x50;
        player2_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index2][strlen(tmp_nickname1)] = 0x50;
    }
    else
    {
        pksav_error = pksav_gen2_import_text(player1_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], tmp_nickname1, PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_import_text(player2_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], tmp_nickname2, PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_export_text(tmp_nickname2, player1_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index1], PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_export_text(tmp_nickname1, player2_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2], PKMN_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        player1_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index1][strlen(tmp_nickname2)] = 0x50;
        player2_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index2][strlen(tmp_nickname1)] = 0x50;
    }

    // swap party
    if (player1_save->save_generation_type == SAVE_GENERATION_1)
    {
        struct pksav_gen1_party_pokemon tmp_pokemon = player1_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1];
        player1_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index1] = player2_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index2];
        player2_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index2] = tmp_pokemon;
    }
    else
    {
        struct pksav_gen2_party_pokemon tmp_pokemon = player1_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index1];
        player1_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index1] = player2_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2];
        player2_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index2] = tmp_pokemon;
    }

    // swap species
    if (player1_save->save_generation_type == SAVE_GENERATION_1)
    {
        uint8_t tmp_species = player1_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index1];
        player1_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index1] = player2_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index2];
        player2_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index2] = tmp_species;
    }
    else
    {
        uint8_t tmp_species = player1_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index1];
        player1_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index1] = player2_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index2];
        player2_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index2] = tmp_species;
    }

    // swap otnames
    char tmp_otname1[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    char tmp_otname2[TRAINER_NAME_TEXT_MAX + 1] = "\0";
    if (player1_save->save_generation_type == SAVE_GENERATION_1)
    {
        pksav_error = pksav_gen1_import_text(player1_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1], tmp_otname1, TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_import_text(player2_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index2], tmp_otname2, TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_export_text(tmp_otname2, player1_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1], TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen1_export_text(tmp_otname1, player2_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index2], TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        player1_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index1][strlen(tmp_otname2)] = 0x50;
        player2_save->save.gen1_save.pokemon_storage.p_party->otnames[pkmn_party_index2][strlen(tmp_otname1)] = 0x50;
    }
    else
    {
        pksav_error = pksav_gen2_import_text(player1_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index1], tmp_otname1, TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_import_text(player2_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2], tmp_otname2, TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_export_text(tmp_otname2, player1_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index1], TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        pksav_error = pksav_gen2_export_text(tmp_otname1, player2_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2], TRAINER_NAME_TEXT_MAX);
        if (pksav_error != PKSAV_ERROR_NONE)
        {
            return error_swap_pkmn;
        }
        player1_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index1][strlen(tmp_otname2)] = 0x50;
        player2_save->save.gen2_save.pokemon_storage.p_party->otnames[pkmn_party_index2][strlen(tmp_otname1)] = 0x50;
    }

    // Swap mail
    if (player1_save->save_generation_type == SAVE_GENERATION_2 && player2_save->save_generation_type == SAVE_GENERATION_2)
    {
        struct pksav_gen2_mail_msg tmp_party_mail = player1_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[pkmn_party_index1];
        player1_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[pkmn_party_index1] = player2_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[pkmn_party_index2];
        player1_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail_backup[pkmn_party_index1] = player2_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[pkmn_party_index2];
        player2_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[pkmn_party_index2] = tmp_party_mail;
        player2_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail_backup[pkmn_party_index2] = tmp_party_mail;
    }

    return error_none;
}

// Convert party pokemon to evolution pokemon with updated stats and properties
void evolve_party_pokemon_at_index(PokemonSave *pkmn_save, uint8_t pkmn_party_index)
{
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        struct pksav_gba_party_pokemon *party = &pkmn_save->save.gba_save.pokemon_storage.p_party->party[pkmn_party_index];
        struct pksav_gba_pc_pokemon *pc = &party->pc_data;

        const struct gen3_trade_evo *evo = gen3_find_trade_evo(pc, NULL);
        if (evo == NULL)
        {
            return; // not eligible (shouldn't happen; UI gates on eligibility)
        }

        // If the nickname is still the default (pre-evo species name), update it.
        // Gen 3 in-game names are uppercase; compare uppercased forms.
        char nickname[PKMN_NAME_TEXT_MAX + 1] = "\0";
        pksav_gba_import_text(pc->nickname, nickname, PKMN_NAME_TEXT_MAX);
        char from_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        char to_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        strncpy(from_name, gen3_national_dex_name(evo->from_dex), PKMN_NAME_TEXT_MAX);
        strncpy(to_name, gen3_national_dex_name(evo->to_dex), PKMN_NAME_TEXT_MAX);
        for (int i = 0; from_name[i] != '\0'; i++)
        {
            from_name[i] = (char)toupper((unsigned char)from_name[i]);
        }
        for (int i = 0; to_name[i] != '\0'; i++)
        {
            to_name[i] = (char)toupper((unsigned char)to_name[i]);
        }
        for (int i = 0; nickname[i] != '\0'; i++)
        {
            nickname[i] = (char)toupper((unsigned char)nickname[i]);
        }
        bool is_default_name = strcmp(nickname, from_name) == 0;

        // Change species (growth block) and consume the held item if required.
        pc->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(evo->to_dex));
        if (evo->required_item != 0)
        {
            pc->blocks.growth.held_item = 0;
        }

        if (is_default_name)
        {
            pksav_gba_export_text(to_name, pc->nickname, PKMN_NAME_TEXT_MAX);
        }

        // Recompute the derived stats for the new species.
        gen3_build_party_data(pc, &party->party_data);
        return;
    }

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        // Get the species index of the pokemon being evolved
        uint8_t pkmn_species_index = pkmn_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index];

        // Get the data for the pokemon's evolution
        char evolution_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        strcpy(evolution_name, pkmn_evolution_pairs_gen1[pkmn_species_index].evolution_name);
        char species_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        strcpy(species_name, pkmn_evolution_pairs_gen1[pkmn_species_index].species_name);
        uint8_t evolution_index = pkmn_evolution_pairs_gen1[pkmn_species_index].evolution_index;

        // Update species index to evolution index to access evolution base stats
        pkmn_save->save.gen1_save.pokemon_storage.p_party->species[pkmn_party_index] = evolution_index;
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.species = evolution_index;

        // Get the pokemon's nickname
        char pkmn_save_nickname[PKMN_NAME_TEXT_MAX + 1] = "\0";
        pksav_gen1_import_text(pkmn_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index], pkmn_save_nickname, PKMN_NAME_TEXT_MAX);

        // Check if pokemon does not have custom nickname
        // if the nickname is not custom, then update the pokemon nickname
        // else do not modify the nickname
        if (strcmp(pkmn_save_nickname, species_name) == 0)
        {
            // Write default nickname for species to pokemon
            pksav_gen1_export_text(evolution_name, pkmn_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index], PKMN_NAME_TEXT_MAX);
            // Set the last character to 0x50 to terminate the string
            pkmn_save->save.gen1_save.pokemon_storage.p_party->nicknames[pkmn_party_index][strlen(evolution_name)] = 0x50;
        }

        // Update types
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.types[0] = pkmn_base_stats_gen1[evolution_index].types[0];
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.types[1] = pkmn_base_stats_gen1[evolution_index].types[1];
        // Update catch rate
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.catch_rate = pkmn_base_stats_gen1[evolution_index].catch_rate;
        // Update condition to none
        pkmn_save->save.gen1_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.condition = PKSAV_GB_CONDITION_NONE;
    }
    else
    {
        // Get the species index of the pokemon being evolved
        uint8_t pkmn_species_index = pkmn_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index];
        // Get the data for the pokemon's evolution
        char evolution_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        strcpy(evolution_name, pkmn_evolution_pairs_gen2[pkmn_species_index].evolution_name);
        char species_name[PKMN_NAME_TEXT_MAX + 1] = "\0";
        strcpy(species_name, pkmn_evolution_pairs_gen2[pkmn_species_index].species_name);
        uint8_t evolution_index = pkmn_evolution_pairs_gen2[pkmn_species_index].evolution_index;

        // Update species index to evolution index to access evolution base stats
        pkmn_save->save.gen2_save.pokemon_storage.p_party->species[pkmn_party_index] = evolution_index;
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].pc_data.species = evolution_index;

        // Get the pokemon's nickname
        char pkmn_save_nickname[PKMN_NAME_TEXT_MAX + 1] = "\0";
        pksav_gen2_import_text(pkmn_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index], pkmn_save_nickname, PKMN_NAME_TEXT_MAX);

        // Check if pokemon does not have custom nickname
        // if the nickname is not custom, then update the pokemon nickname
        // else do not modify the nickname
        if (strcmp(pkmn_save_nickname, species_name) == 0)
        {
            // Write default nickname for species to pokemon
            pksav_gen2_export_text(evolution_name, pkmn_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index], PKMN_NAME_TEXT_MAX);
            // Set the last character to 0x50 to terminate the string
            pkmn_save->save.gen2_save.pokemon_storage.p_party->nicknames[pkmn_party_index][strlen(evolution_name)] = 0x50;
        }

        // Update condition to none
        pkmn_save->save.gen2_save.pokemon_storage.p_party->party[pkmn_party_index].party_data.condition = PKSAV_CONDITION_NONE;
    }
}

/************************************************************************
 * Bill's PC — single-save box management (view, sort, move/swap)
 *
 * Data model notes:
 *  - Boxes live in pokemon_storage.pp_boxes[]. The "current" box is also
 *    mirrored in p_current_box; on a normal in-game save the two agree.
 *    We normalize p_current_box -> pp_boxes[current] on entry and flush it
 *    back before writing, so all editing happens against pp_boxes[].
 *  - Box entries store only pksav_gen*_pc_pokemon. Party slots store
 *    pksav_gen*_party_pokemon (pc_data + derived party_data). Moving a mon
 *    into the party therefore regenerates party_data from the pc_data.
 ************************************************************************/

#define BILLS_PC_NAME_STORAGE (PKMN_NAME_TEXT_MAX + 1)

// Case-insensitive string compare (portable, avoids platform strcasecmp).
static int bills_pc_ci_strcmp(const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb)
        {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

const char *pkmn_type_name(uint8_t gen1_type_value)
{
    switch (gen1_type_value)
    {
    case PKSAV_GEN1_TYPE_NORMAL:   return "NORMAL";
    case PKSAV_GEN1_TYPE_FIGHTING: return "FIGHTING";
    case PKSAV_GEN1_TYPE_FLYING:   return "FLYING";
    case PKSAV_GEN1_TYPE_POISON:   return "POISON";
    case PKSAV_GEN1_TYPE_GROUND:   return "GROUND";
    case PKSAV_GEN1_TYPE_ROCK:     return "ROCK";
    case PKSAV_GEN1_TYPE_BUG:      return "BUG";
    case PKSAV_GEN1_TYPE_GHOST:    return "GHOST";
    case PKSAV_GEN1_TYPE_FIRE:     return "FIRE";
    case PKSAV_GEN1_TYPE_WATER:    return "WATER";
    case PKSAV_GEN1_TYPE_GRASS:    return "GRASS";
    case PKSAV_GEN1_TYPE_ELECTRIC: return "ELECTRIC";
    case PKSAV_GEN1_TYPE_PSYCHIC:  return "PSYCHIC";
    case PKSAV_GEN1_TYPE_ICE:      return "ICE";
    case PKSAV_GEN1_TYPE_DRAGON:   return "DRAGON";
    default:                       return "?";
    }
}

void bills_pc_species_types(const PokemonSave *pkmn_save, uint16_t species, uint8_t *out_type1, uint8_t *out_type2)
{
    uint8_t t1 = BILLS_PC_TYPE_UNKNOWN;
    uint8_t t2 = BILLS_PC_TYPE_UNKNOWN;

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        if (species <= SI_VICTREEBEL)
        {
            t1 = pkmn_base_stats_gen1[species].types[0];
            t2 = pkmn_base_stats_gen1[species].types[1];
        }
    }
    else
    {
        // Gen 2 species index is the dex id; for Gen 3 the caller passes the
        // National Dex number. Type data is only available for the 151 Kanto
        // mons (mapped through the Gen 1 base-stat table).
        if (species >= 1 && species <= MEW)
        {
            uint8_t gen1_index = species_gen2_to_gen1[species];
            if (gen1_index != 0 && gen1_index <= SI_VICTREEBEL)
            {
                t1 = pkmn_base_stats_gen1[gen1_index].types[0];
                t2 = pkmn_base_stats_gen1[gen1_index].types[1];
            }
        }
    }

    if (out_type1)
    {
        *out_type1 = t1;
    }
    if (out_type2)
    {
        *out_type2 = t2;
    }
}

int bills_pc_num_boxes(const PokemonSave *pkmn_save)
{
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
        return PKSAV_GEN1_NUM_POKEMON_BOXES;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
        return PKSAV_GBA_NUM_POKEMON_BOXES;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
        return GEN4_NUM_BOXES;
    return PKSAV_GEN2_NUM_POKEMON_BOXES;
}

int bills_pc_box_capacity(const PokemonSave *pkmn_save)
{
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
        return PKSAV_GEN1_BOX_NUM_POKEMON;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
        return PKSAV_GBA_BOX_NUM_POKEMON;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
        return GEN4_BOX_SLOTS;
    return PKSAV_GEN2_BOX_NUM_POKEMON;
}

int bills_pc_party_capacity(const PokemonSave *pkmn_save)
{
    (void)pkmn_save;
    return PKSAV_STANDARD_POKEMON_PARTY_SIZE;
}

int bills_pc_current_box_num(const PokemonSave *pkmn_save)
{
    int num_boxes = bills_pc_num_boxes(pkmn_save);
    int cur;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        cur = *pkmn_save->save.gen1_save.pokemon_storage.p_current_box_num & PKSAV_GEN1_CURRENT_POKEMON_BOX_NUM_MASK;
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        cur = (int)pkmn_save->save.gba_save.pokemon_storage.p_pc->current_box;
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        cur = gen4_current_box(&pkmn_save->save.gen4_save);
    }
    else
    {
        cur = *pkmn_save->save.gen2_save.pokemon_storage.p_current_box_num & 0x0F;
    }
    if (cur < 0 || cur >= num_boxes)
    {
        cur = 0;
    }
    return cur;
}

// Gen 3 boxes have no count field and allow gaps; a slot is occupied when its
// growth-block species index is non-zero.
static bool gba_box_slot_occupied(const PokemonSave *pkmn_save, int box_num, int index)
{
    return pkmn_save->save.gba_save.pokemon_storage.p_pc->boxes[box_num].entries[index].blocks.growth.species != 0;
}

// A Gen 4 box slot is occupied when its decrypted species is non-zero (boxes
// allow gaps, like Gen 3). Party slots are contiguous up to the count.
static bool gen4_slot_occupied(const struct gen4_save *g4, enum bills_pc_location location, int box_num, int index)
{
    if (location == BILLS_PC_LOC_PARTY)
    {
        return index < (int)gen4_party_count(g4);
    }
    const uint8_t *raw = gen4_box_slot_raw(g4, box_num, index);
    if (!raw)
    {
        return false;
    }
    uint8_t dec[GEN4_PK4_STORED_SIZE];
    gen4_pk4_decrypt(raw, dec, false);
    return gen4_pk4_species(dec) != 0;
}

bool bills_pc_slot_occupied(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index)
{
    if (index < 0)
    {
        return false;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        return gen4_slot_occupied(&pkmn_save->save.gen4_save, location, box_num, index);
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        if (location == BILLS_PC_LOC_PARTY)
        {
            return index < (int)pkmn_save->save.gba_save.pokemon_storage.p_party->count && index < PKSAV_GBA_PARTY_NUM_POKEMON;
        }
        return index < PKSAV_GBA_BOX_NUM_POKEMON && gba_box_slot_occupied(pkmn_save, box_num, index);
    }
    // Gen 1/2 containers are contiguous.
    return index < bills_pc_container_count(pkmn_save, location, box_num);
}

int bills_pc_container_count(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num)
{
    int count;
    int capacity;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        if (location == BILLS_PC_LOC_PARTY)
        {
            return (int)gen4_party_count(&pkmn_save->save.gen4_save);
        }
        return gen4_box_occupied_count(&pkmn_save->save.gen4_save, box_num);
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        if (location == BILLS_PC_LOC_PARTY)
        {
            count = (int)pkmn_save->save.gba_save.pokemon_storage.p_party->count;
            capacity = PKSAV_GBA_PARTY_NUM_POKEMON;
        }
        else
        {
            // No count field: count the occupied (non-empty) slots.
            count = 0;
            for (int i = 0; i < PKSAV_GBA_BOX_NUM_POKEMON; i++)
            {
                if (gba_box_slot_occupied(pkmn_save, box_num, i))
                {
                    count++;
                }
            }
            capacity = PKSAV_GBA_BOX_NUM_POKEMON;
        }
        if (count < 0 || count > capacity)
        {
            count = 0;
        }
        return count;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        if (location == BILLS_PC_LOC_PARTY)
        {
            count = pkmn_save->save.gen1_save.pokemon_storage.p_party->count;
            capacity = PKSAV_GEN1_PARTY_NUM_POKEMON;
        }
        else
        {
            count = pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[box_num]->count;
            capacity = PKSAV_GEN1_BOX_NUM_POKEMON;
        }
    }
    else
    {
        if (location == BILLS_PC_LOC_PARTY)
        {
            count = pkmn_save->save.gen2_save.pokemon_storage.p_party->count;
            capacity = PKSAV_GEN2_PARTY_NUM_POKEMON;
        }
        else
        {
            count = pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[box_num]->count;
            capacity = PKSAV_GEN2_BOX_NUM_POKEMON;
        }
    }
    // Guard against uninitialized/garbage counts (e.g. 0xFF in an empty box).
    if (count < 0 || count > capacity)
    {
        count = 0;
    }
    return count;
}

void bills_pc_get_view(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index, struct bills_pc_entry_view *out_view)
{
    memset(out_view, 0, sizeof(*out_view));
    out_view->type1 = BILLS_PC_TYPE_UNKNOWN;
    out_view->type2 = BILLS_PC_TYPE_UNKNOWN;

    if (!bills_pc_slot_occupied(pkmn_save, location, box_num, index))
    {
        out_view->occupied = false;
        return;
    }
    out_view->occupied = true;

    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        const struct gen4_save *g4 = &pkmn_save->save.gen4_save;
        bool is_party = (location == BILLS_PC_LOC_PARTY);
        const uint8_t *raw = is_party ? gen4_party_slot_raw(g4, index)
                                      : gen4_box_slot_raw(g4, box_num, index);
        uint8_t dec[GEN4_PK4_PARTY_SIZE];
        gen4_pk4_decrypt(raw, dec, is_party);
        out_view->species = gen4_pk4_species(dec); // Gen 4 species == National Dex
        out_view->dex = out_view->species;
        // Party record caches the level; boxed mons store none (derive from EXP).
        out_view->level = is_party ? gen4_pk4_party_level(dec) : 0;
        // Only nicknamed mons carry a meaningful name field; for the rest the game
        // hides the raw bytes behind the IsNicknamed flag and shows the species
        // name (so hacked mons with garbage name fields render correctly).
        if (gen4_pk4_is_nicknamed(dec))
        {
            gen4_decode_text(dec + 0x48, PKMN_NAME_TEXT_MAX, out_view->nickname);
        }
        else
        {
            snprintf(out_view->nickname, sizeof(out_view->nickname), "%s",
                     gen4_species_name(out_view->species));
        }
        return;
    }

    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        const struct pksav_gba_pokemon_storage *storage = &pkmn_save->save.gba_save.pokemon_storage;
        const struct pksav_gba_pc_pokemon *pc;
        if (location == BILLS_PC_LOC_PARTY)
        {
            pc = &storage->p_party->party[index].pc_data;
            out_view->level = storage->p_party->party[index].party_data.level;
        }
        else
        {
            pc = &storage->p_pc->boxes[box_num].entries[index];
            out_view->level = 0; // Gen 3 boxed mons store no level (derived from EXP)
        }
        out_view->species = pc->blocks.growth.species; // internal index
        out_view->dex = gen3_internal_to_national(pc->blocks.growth.species);
        pksav_gba_import_text(pc->nickname, out_view->nickname, PKMN_NAME_TEXT_MAX);
        // Types via National Dex (Kanto 1-151 only; Hoenn/Johto show unknown).
        bills_pc_species_types(pkmn_save, out_view->dex, &out_view->type1, &out_view->type2);
        return;
    }

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        const struct pksav_gen1_pokemon_storage *storage = &pkmn_save->save.gen1_save.pokemon_storage;
        const uint8_t *species_arr;
        const uint8_t (*nicknames)[PKSAV_GEN1_POKEMON_NICKNAME_LENGTH + 1];
        if (location == BILLS_PC_LOC_PARTY)
        {
            species_arr = storage->p_party->species;
            nicknames = storage->p_party->nicknames;
            // pc_data.level is a stale copy from when the mon was caught/boxed;
            // party_data.level is the authoritative level for party members.
            out_view->level = storage->p_party->party[index].party_data.level;
        }
        else
        {
            species_arr = storage->pp_boxes[box_num]->species;
            nicknames = storage->pp_boxes[box_num]->nicknames;
            out_view->level = storage->pp_boxes[box_num]->entries[index].level;
        }
        out_view->species = species_arr[index];
        pksav_gen1_import_text(nicknames[index], out_view->nickname, PKMN_NAME_TEXT_MAX);
    }
    else
    {
        const struct pksav_gen2_pokemon_storage *storage = &pkmn_save->save.gen2_save.pokemon_storage;
        const uint8_t *species_arr;
        const uint8_t (*nicknames)[PKSAV_GEN2_POKEMON_NICKNAME_LENGTH + 1];
        if (location == BILLS_PC_LOC_PARTY)
        {
            species_arr = storage->p_party->species;
            nicknames = storage->p_party->nicknames;
            out_view->level = storage->p_party->party[index].pc_data.level;
        }
        else
        {
            species_arr = storage->pp_boxes[box_num]->species;
            nicknames = storage->pp_boxes[box_num]->nicknames;
            out_view->level = storage->pp_boxes[box_num]->entries[index].level;
        }
        out_view->species = species_arr[index];
        pksav_gen2_import_text(nicknames[index], out_view->nickname, PKMN_NAME_TEXT_MAX);
    }

    bills_pc_species_types(pkmn_save, out_view->species, &out_view->type1, &out_view->type2);

    // National Pokédex number (Gen 2 species index already is the dex id).
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        out_view->dex = out_view->species <= SI_VICTREEBEL ? species_gen1_to_gen2[out_view->species] : 0;
    }
    else
    {
        out_view->dex = out_view->species;
    }
}

// --- Pokédex read helpers (strictly read-only; dex bits are only ever
// written by an actual trade/transfer via update_seen_owned_pkmn) ---

uint16_t pokedex_species_count(const PokemonSave *pkmn_save)
{
    switch (pkmn_save->save_generation_type)
    {
    case SAVE_GENERATION_1:
        return 151;
    case SAVE_GENERATION_2:
        return 251;
    case SAVE_GENERATION_3:
        return 386;
    case SAVE_GENERATION_4:
        return GEN4_NATIONAL_DEX_MAX;
    default:
        return 0;
    }
}

void pokedex_get_entry(const PokemonSave *pkmn_save, uint16_t dex, bool *seen, bool *owned)
{
    *seen = false;
    *owned = false;
    if (dex < 1 || dex > pokedex_species_count(pkmn_save))
    {
        return;
    }
    switch (pkmn_save->save_generation_type)
    {
    case SAVE_GENERATION_1:
        pksav_get_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_seen, dex, seen);
        pksav_get_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_owned, dex, owned);
        break;
    case SAVE_GENERATION_2:
        pksav_get_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_seen, dex, seen);
        pksav_get_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_owned, dex, owned);
        break;
    case SAVE_GENERATION_3:
        pksav_get_pokedex_bit(pkmn_save->save.gba_save.pokedex.p_seenA, dex, seen);
        pksav_get_pokedex_bit(pkmn_save->save.gba_save.pokedex.p_owned, dex, owned);
        break;
    case SAVE_GENERATION_4:
        *seen = gen4_dex_get_seen(&pkmn_save->save.gen4_save, dex);
        *owned = gen4_dex_get_caught(&pkmn_save->save.gen4_save, dex);
        break;
    default:
        break;
    }
    if (*owned) // a caught mon has necessarily been seen, even if a bit is odd
    {
        *seen = true;
    }
}

void pokedex_counts(const PokemonSave *pkmn_save, uint16_t *seen_count, uint16_t *owned_count)
{
    *seen_count = 0;
    *owned_count = 0;
    uint16_t max = pokedex_species_count(pkmn_save);
    for (uint16_t dex = 1; dex <= max; dex++)
    {
        bool seen, owned;
        pokedex_get_entry(pkmn_save, dex, &seen, &owned);
        if (seen)
            (*seen_count)++;
        if (owned)
            (*owned_count)++;
    }
}

// Set seen+owned for one National Dex entry, any generation.
static void pokedex_mark_owned(PokemonSave *pkmn_save, uint16_t dex)
{
    if (dex == 0 || dex > pokedex_species_count(pkmn_save))
    {
        return;
    }
    switch (pkmn_save->save_generation_type)
    {
    case SAVE_GENERATION_1:
        pksav_set_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_seen, dex, true);
        pksav_set_pokedex_bit(pkmn_save->save.gen1_save.pokedex_lists.p_owned, dex, true);
        break;
    case SAVE_GENERATION_2:
        pksav_set_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_seen, dex, true);
        pksav_set_pokedex_bit(pkmn_save->save.gen2_save.pokedex_lists.p_owned, dex, true);
        break;
    case SAVE_GENERATION_3:
        pksav_gba_pokedex_set_has_seen(&pkmn_save->save.gba_save.pokedex, dex, true);
        pksav_set_pokedex_bit(pkmn_save->save.gba_save.pokedex.p_owned, dex, true);
        break;
    case SAVE_GENERATION_4:
        gen4_dex_set_seen_caught(&pkmn_save->save.gen4_save, dex);
        break;
    default:
        break;
    }
}

// The games guarantee that any Pokemon in the party or a box is recorded
// seen+owned in the Pokedex. Saves touched by older versions of this app can
// have gaps (e.g. a traded-in mon whose owned bit was never set); walk every
// occupied slot and restore the invariant.
void pokedex_reconcile(PokemonSave *pkmn_save)
{
    struct bills_pc_entry_view view;
    for (int i = 0; i < bills_pc_party_capacity(pkmn_save); i++)
    {
        bills_pc_get_view(pkmn_save, BILLS_PC_LOC_PARTY, 0, i, &view);
        if (view.occupied)
        {
            pokedex_mark_owned(pkmn_save, view.dex);
        }
    }
    int boxes = bills_pc_num_boxes(pkmn_save);
    int capacity = bills_pc_box_capacity(pkmn_save);
    for (int b = 0; b < boxes; b++)
    {
        for (int i = 0; i < capacity; i++)
        {
            bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, b, i, &view);
            if (view.occupied)
            {
                pokedex_mark_owned(pkmn_save, view.dex);
            }
        }
    }
}

bool bills_pc_slot_holds_mail(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index)
{
    (void)box_num;
    if (pkmn_save->save_generation_type != SAVE_GENERATION_2 || location != BILLS_PC_LOC_PARTY)
    {
        return false;
    }
    if (index < 0 || index >= PKSAV_GEN2_PARTY_NUM_POKEMON)
    {
        return false;
    }
    return pkmn_save->save.gen2_save.pokemon_storage.p_party_mail->party_mail[index].item_id != 0;
}

// -------------------- Gen 1 container abstraction --------------------

struct g1_ctx
{
    uint8_t *count;
    uint8_t *species; // [capacity + 1], terminated with 0xFF
    struct pksav_gen1_pc_pokemon *entries;    // non-NULL for a box
    struct pksav_gen1_party_pokemon *party;   // non-NULL for the party
    uint8_t (*otnames)[PKSAV_GEN1_POKEMON_OTNAME_STORAGE_LENGTH + 1];
    uint8_t (*nicknames)[PKSAV_GEN1_POKEMON_NICKNAME_LENGTH + 1];
    int capacity;
    bool is_party;
};

struct g1_mon
{
    struct pksav_gen1_pc_pokemon pc;
    uint8_t species;
    uint8_t otname[PKSAV_GEN1_POKEMON_OTNAME_STORAGE_LENGTH + 1];
    uint8_t nickname[PKSAV_GEN1_POKEMON_NICKNAME_LENGTH + 1];
};

static struct g1_ctx g1_get(PokemonSave *s, enum bills_pc_location loc, int box)
{
    struct g1_ctx c = {0};
    struct pksav_gen1_pokemon_storage *st = &s->save.gen1_save.pokemon_storage;
    if (loc == BILLS_PC_LOC_PARTY)
    {
        c.count = &st->p_party->count;
        c.species = st->p_party->species;
        c.party = st->p_party->party;
        c.otnames = st->p_party->otnames;
        c.nicknames = st->p_party->nicknames;
        c.capacity = PKSAV_GEN1_PARTY_NUM_POKEMON;
        c.is_party = true;
    }
    else
    {
        struct pksav_gen1_pokemon_box *b = st->pp_boxes[box];
        c.count = &b->count;
        c.species = b->species;
        c.entries = b->entries;
        c.otnames = b->otnames;
        c.nicknames = b->nicknames;
        c.capacity = PKSAV_GEN1_BOX_NUM_POKEMON;
        c.is_party = false;
    }
    return c;
}

static void g1_capture(struct g1_ctx *c, int i, struct g1_mon *m)
{
    m->pc = c->is_party ? c->party[i].pc_data : c->entries[i];
    if (c->is_party)
    {
        // Gen 1 only maintains party_data.level while a mon is in the party;
        // the game copies it into the box-level byte on deposit, so do the same.
        uint8_t live = c->party[i].party_data.level;
        if (live >= 1 && live <= 100)
        {
            m->pc.level = live;
        }
    }
    m->species = c->species[i];
    memcpy(m->otname, c->otnames[i], sizeof(m->otname));
    memcpy(m->nickname, c->nicknames[i], sizeof(m->nickname));
}

// Recompute the derived party data for the Gen 1 party slot at index i.
static void g1_finalize_party(PokemonSave *s, int i)
{
    struct pksav_gen1_party_pokemon *p = &s->save.gen1_save.pokemon_storage.p_party->party[i];
    // The Gen 1 box level byte goes stale the moment a mon levels up in the
    // party, so derive the real level from EXP the way the game's own box
    // withdrawal does, then refresh the stored byte.
    uint8_t internal = s->save.gen1_save.pokemon_storage.p_party->species[i];
    uint8_t level = p->pc_data.level;
    if (internal <= SI_VICTREEBEL && species_gen1_to_gen2[internal] > 0)
    {
        uint32_t exp = ((uint32_t)p->pc_data.exp[0] << 16) |
                       ((uint32_t)p->pc_data.exp[1] << 8) |
                       (uint32_t)p->pc_data.exp[2];
        int from_exp = gen4_level_from_exp(exp, gen4_growth_rate_for_dex(species_gen1_to_gen2[internal]));
        if (from_exp >= 2 && from_exp <= 100)
        {
            level = (uint8_t)from_exp;
        }
    }
    p->pc_data.level = level;
    p->party_data.level = level;
    update_pkmn_stats(s, i); // fills max_hp/atk/... and pc_data.current_hp
    p->pc_data.condition = PKSAV_GB_CONDITION_NONE;
}

static void g1_write(PokemonSave *s, struct g1_ctx *c, int i, const struct g1_mon *m)
{
    if (c->is_party)
    {
        c->party[i].pc_data = m->pc;
    }
    else
    {
        c->entries[i] = m->pc;
    }
    c->species[i] = m->species;
    memcpy(c->otnames[i], m->otname, sizeof(c->otnames[i]));
    memcpy(c->nicknames[i], m->nickname, sizeof(c->nicknames[i]));
    (void)s;
}

static void g1_remove(struct g1_ctx *c, int k)
{
    int n = *c->count;
    for (int i = k; i < n - 1; i++)
    {
        if (c->is_party)
        {
            c->party[i] = c->party[i + 1];
        }
        else
        {
            c->entries[i] = c->entries[i + 1];
        }
        c->species[i] = c->species[i + 1];
        memcpy(c->otnames[i], c->otnames[i + 1], sizeof(c->otnames[i]));
        memcpy(c->nicknames[i], c->nicknames[i + 1], sizeof(c->nicknames[i]));
    }
    (*c->count)--;
    c->species[*c->count] = 0xFF;
}

static int g1_append(PokemonSave *s, struct g1_ctx *c, const struct g1_mon *m)
{
    int i = *c->count;
    g1_write(s, c, i, m);
    (*c->count)++;
    c->species[*c->count] = 0xFF;
    if (c->is_party)
    {
        g1_finalize_party(s, i);
    }
    return i;
}

static pksavhelper_error bills_pc_move_gen1(PokemonSave *s, struct bills_pc_slot src, struct bills_pc_slot dst)
{
    struct g1_ctx sc = g1_get(s, src.location, src.box_num);
    struct g1_ctx dc = g1_get(s, dst.location, dst.box_num);
    bool same_container = (src.location == dst.location) &&
                          (src.location == BILLS_PC_LOC_PARTY || src.box_num == dst.box_num);

    int scount = *sc.count;
    if (src.index < 0 || src.index >= scount)
    {
        return error_swap_pkmn; // source must be occupied
    }
    bool dst_occupied = dst.index >= 0 && dst.index < *dc.count;

    if (same_container && !dst_occupied)
    {
        return error_none; // no-op; use sort to reorder within a container
    }

    if (dst_occupied)
    {
        struct g1_mon a, b;
        g1_capture(&sc, src.index, &a);
        g1_capture(&dc, dst.index, &b);
        g1_write(s, &sc, src.index, &b);
        g1_write(s, &dc, dst.index, &a);
        if (sc.is_party)
        {
            g1_finalize_party(s, src.index);
        }
        if (dc.is_party)
        {
            g1_finalize_party(s, dst.index);
        }
        return error_none;
    }

    // Move into an empty destination (deposit/withdraw).
    if (*dc.count >= dc.capacity)
    {
        return error_swap_pkmn; // destination full
    }
    if (sc.is_party && *sc.count <= 1)
    {
        return error_swap_pkmn; // cannot leave the party empty
    }
    struct g1_mon m;
    g1_capture(&sc, src.index, &m);
    g1_append(s, &dc, &m);
    g1_remove(&sc, src.index);
    return error_none;
}

// -------------------- Gen 2 container abstraction --------------------

struct g2_ctx
{
    uint8_t *count;
    uint8_t *species;
    struct pksav_gen2_pc_pokemon *entries;
    struct pksav_gen2_party_pokemon *party;
    uint8_t (*otnames)[PKSAV_GEN2_POKEMON_OTNAME_STORAGE_LENGTH + 1];
    uint8_t (*nicknames)[PKSAV_GEN2_POKEMON_NICKNAME_LENGTH + 1];
    int capacity;
    bool is_party;
};

struct g2_mon
{
    struct pksav_gen2_pc_pokemon pc;
    uint8_t species;
    uint8_t otname[PKSAV_GEN2_POKEMON_OTNAME_STORAGE_LENGTH + 1];
    uint8_t nickname[PKSAV_GEN2_POKEMON_NICKNAME_LENGTH + 1];
};

static struct g2_ctx g2_get(PokemonSave *s, enum bills_pc_location loc, int box)
{
    struct g2_ctx c = {0};
    struct pksav_gen2_pokemon_storage *st = &s->save.gen2_save.pokemon_storage;
    if (loc == BILLS_PC_LOC_PARTY)
    {
        c.count = &st->p_party->count;
        c.species = st->p_party->species;
        c.party = st->p_party->party;
        c.otnames = st->p_party->otnames;
        c.nicknames = st->p_party->nicknames;
        c.capacity = PKSAV_GEN2_PARTY_NUM_POKEMON;
        c.is_party = true;
    }
    else
    {
        struct pksav_gen2_pokemon_box *b = st->pp_boxes[box];
        c.count = &b->count;
        c.species = b->species;
        c.entries = b->entries;
        c.otnames = b->otnames;
        c.nicknames = b->nicknames;
        c.capacity = PKSAV_GEN2_BOX_NUM_POKEMON;
        c.is_party = false;
    }
    return c;
}

static void g2_capture(struct g2_ctx *c, int i, struct g2_mon *m)
{
    m->pc = c->is_party ? c->party[i].pc_data : c->entries[i];
    m->species = c->species[i];
    memcpy(m->otname, c->otnames[i], sizeof(m->otname));
    memcpy(m->nickname, c->nicknames[i], sizeof(m->nickname));
}

static void g2_finalize_party(PokemonSave *s, int i)
{
    struct pksav_gen2_party_pokemon *p = &s->save.gen2_save.pokemon_storage.p_party->party[i];
    update_pkmn_stats(s, i); // fills party_data stats and current_hp (reads pc_data.level)
    p->party_data.condition = PKSAV_CONDITION_NONE;
}

static void g2_write(PokemonSave *s, struct g2_ctx *c, int i, const struct g2_mon *m)
{
    if (c->is_party)
    {
        c->party[i].pc_data = m->pc;
    }
    else
    {
        c->entries[i] = m->pc;
    }
    c->species[i] = m->species;
    memcpy(c->otnames[i], m->otname, sizeof(c->otnames[i]));
    memcpy(c->nicknames[i], m->nickname, sizeof(c->nicknames[i]));
    (void)s;
}

static bool g2_party_has_mail(PokemonSave *s, int i)
{
    return s->save.gen2_save.pokemon_storage.p_party_mail->party_mail[i].item_id != 0;
}

static void g2_clear_party_mail(PokemonSave *s, int i)
{
    struct pksav_gen2_party_mail *pm = s->save.gen2_save.pokemon_storage.p_party_mail;
    memset(&pm->party_mail[i], 0, sizeof(struct pksav_gen2_mail_msg));
    memset(&pm->party_mail_backup[i], 0, sizeof(struct pksav_gen2_mail_msg));
}

static void g2_swap_party_mail(PokemonSave *s, int i, int j)
{
    struct pksav_gen2_party_mail *pm = s->save.gen2_save.pokemon_storage.p_party_mail;
    struct pksav_gen2_mail_msg tmp = pm->party_mail[i];
    pm->party_mail[i] = pm->party_mail[j];
    pm->party_mail[j] = tmp;
    tmp = pm->party_mail_backup[i];
    pm->party_mail_backup[i] = pm->party_mail_backup[j];
    pm->party_mail_backup[j] = tmp;
}

// Shift the party mail array down after removing party index k (n = pre-removal count).
static void g2_mail_remove(PokemonSave *s, int k, int n)
{
    struct pksav_gen2_party_mail *pm = s->save.gen2_save.pokemon_storage.p_party_mail;
    for (int i = k; i < n - 1; i++)
    {
        pm->party_mail[i] = pm->party_mail[i + 1];
        pm->party_mail_backup[i] = pm->party_mail_backup[i + 1];
    }
    g2_clear_party_mail(s, n - 1);
}

static void g2_remove(struct g2_ctx *c, int k)
{
    int n = *c->count;
    for (int i = k; i < n - 1; i++)
    {
        if (c->is_party)
        {
            c->party[i] = c->party[i + 1];
        }
        else
        {
            c->entries[i] = c->entries[i + 1];
        }
        c->species[i] = c->species[i + 1];
        memcpy(c->otnames[i], c->otnames[i + 1], sizeof(c->otnames[i]));
        memcpy(c->nicknames[i], c->nicknames[i + 1], sizeof(c->nicknames[i]));
    }
    (*c->count)--;
    c->species[*c->count] = 0xFF;
}

static int g2_append(PokemonSave *s, struct g2_ctx *c, const struct g2_mon *m)
{
    int i = *c->count;
    g2_write(s, c, i, m);
    (*c->count)++;
    c->species[*c->count] = 0xFF;
    if (c->is_party)
    {
        g2_finalize_party(s, i);
    }
    return i;
}

static pksavhelper_error bills_pc_move_gen2(PokemonSave *s, struct bills_pc_slot src, struct bills_pc_slot dst)
{
    struct g2_ctx sc = g2_get(s, src.location, src.box_num);
    struct g2_ctx dc = g2_get(s, dst.location, dst.box_num);
    bool same_container = (src.location == dst.location) &&
                          (src.location == BILLS_PC_LOC_PARTY || src.box_num == dst.box_num);

    int scount = *sc.count;
    if (src.index < 0 || src.index >= scount)
    {
        return error_swap_pkmn;
    }
    bool dst_occupied = dst.index >= 0 && dst.index < *dc.count;

    // A party mon carrying mail cannot be stored in a box.
    if (sc.is_party && !dc.is_party && g2_party_has_mail(s, src.index))
    {
        return error_swap_pkmn;
    }
    if (!sc.is_party && dc.is_party && dst_occupied && g2_party_has_mail(s, dst.index))
    {
        return error_swap_pkmn;
    }

    if (same_container && !dst_occupied)
    {
        return error_none;
    }

    if (dst_occupied)
    {
        struct g2_mon a, b;
        g2_capture(&sc, src.index, &a);
        g2_capture(&dc, dst.index, &b);
        g2_write(s, &sc, src.index, &b);
        g2_write(s, &dc, dst.index, &a);
        if (sc.is_party)
        {
            g2_finalize_party(s, src.index);
        }
        if (dc.is_party)
        {
            g2_finalize_party(s, dst.index);
        }
        // Reconcile mail so it stays attached to the party slot it belongs to.
        if (sc.is_party && dc.is_party)
        {
            g2_swap_party_mail(s, src.index, dst.index);
        }
        else if (sc.is_party && !dc.is_party)
        {
            g2_clear_party_mail(s, src.index); // now holds the former box mon
        }
        else if (!sc.is_party && dc.is_party)
        {
            g2_clear_party_mail(s, dst.index); // now holds the former box mon
        }
        return error_none;
    }

    if (*dc.count >= dc.capacity)
    {
        return error_swap_pkmn;
    }
    if (sc.is_party && *sc.count <= 1)
    {
        return error_swap_pkmn;
    }
    struct g2_mon m;
    g2_capture(&sc, src.index, &m);
    int new_index = g2_append(s, &dc, &m);
    if (dc.is_party)
    {
        g2_clear_party_mail(s, new_index); // withdrawn box mon starts with no mail
    }
    g2_remove(&sc, src.index);
    if (sc.is_party)
    {
        g2_mail_remove(s, src.index, scount);
    }
    return error_none;
}

// -------------------- Gen 3 (GBA) moves --------------------
//
// Gen 3 boxes are slot-based (30 raw slots, gaps allowed, no count) so box↔box
// moves/swaps are plain 80-byte copies. Moving a boxed mon into the party
// (withdraw, or a party↔box swap that lands a box mon in the party) rebuilds the
// 20-byte party_data (level + stats derived from EXP/IVs/EVs/nature) via
// gen3_stats; if that can't be built (egg / unknown species) the move is
// rejected. Deposit (party→box) needs no rebuild since a box stores only pc_data.

static pksavhelper_error bills_pc_move_gen3(PokemonSave *s, struct bills_pc_slot src, struct bills_pc_slot dst)
{
    struct pksav_gba_pokemon_storage *st = &s->save.gba_save.pokemon_storage;
    bool src_party = src.location == BILLS_PC_LOC_PARTY;
    bool dst_party = dst.location == BILLS_PC_LOC_PARTY;

    if (!bills_pc_slot_occupied(s, src.location, src.box_num, src.index))
    {
        return error_swap_pkmn;
    }
    bool dst_occupied = bills_pc_slot_occupied(s, dst.location, dst.box_num, dst.index);

    // box <-> box (swap if dst occupied, move if dst empty)
    if (!src_party && !dst_party)
    {
        struct pksav_gba_pc_pokemon *a = &st->p_pc->boxes[src.box_num].entries[src.index];
        struct pksav_gba_pc_pokemon *b = &st->p_pc->boxes[dst.box_num].entries[dst.index];
        if (a == b)
        {
            return error_none;
        }
        struct pksav_gba_pc_pokemon tmp = *a;
        *a = *b; // if b is an empty (zeroed) slot this becomes a move
        *b = tmp;
        return error_none;
    }

    // party <-> party
    if (src_party && dst_party)
    {
        if (src.index == dst.index || !dst_occupied)
        {
            return error_none; // party is contiguous; no empty slots to move into
        }
        struct pksav_gba_party_pokemon tmp = st->p_party->party[src.index];
        st->p_party->party[src.index] = st->p_party->party[dst.index];
        st->p_party->party[dst.index] = tmp;
        return error_none;
    }

    // party -> box
    if (src_party && !dst_party)
    {
        struct pksav_gba_pc_pokemon *box_entry = &st->p_pc->boxes[dst.box_num].entries[dst.index];
        if (!dst_occupied)
        {
            // Deposit into an empty box slot (a box stores only the pc_data).
            int count = (int)st->p_party->count;
            if (count <= 1)
            {
                return error_swap_pkmn; // can't empty the party
            }
            *box_entry = st->p_party->party[src.index].pc_data;
            for (int i = src.index; i < count - 1; i++)
            {
                st->p_party->party[i] = st->p_party->party[i + 1];
            }
            memset(&st->p_party->party[count - 1], 0, sizeof(struct pksav_gba_party_pokemon));
            st->p_party->count = (uint32_t)(count - 1);
            return error_none;
        }
        // Swap: the box mon enters the party slot, so it needs rebuilt party_data.
        struct pksav_gba_pc_pokemon box_mon = *box_entry;
        struct pksav_gba_pokemon_party_data pd;
        if (!gen3_build_party_data(&box_mon, &pd))
        {
            return error_swap_pkmn;
        }
        struct pksav_gba_party_pokemon party_mon = st->p_party->party[src.index];
        st->p_party->party[src.index].pc_data = box_mon;
        st->p_party->party[src.index].party_data = pd;
        *box_entry = party_mon.pc_data;
        return error_none;
    }

    // box -> party (withdraw to an empty slot, or swap with an occupied one).
    {
        struct pksav_gba_pc_pokemon *box_entry = &st->p_pc->boxes[src.box_num].entries[src.index];
        struct pksav_gba_pc_pokemon box_mon = *box_entry;
        struct pksav_gba_pokemon_party_data pd;
        if (!gen3_build_party_data(&box_mon, &pd))
        {
            return error_swap_pkmn; // egg / unknown species: can't build legal stats
        }
        if (!dst_occupied)
        {
            int count = (int)st->p_party->count;
            if (count >= PKSAV_GBA_PARTY_NUM_POKEMON)
            {
                return error_swap_pkmn; // party full
            }
            st->p_party->party[count].pc_data = box_mon;
            st->p_party->party[count].party_data = pd;
            st->p_party->count = (uint32_t)(count + 1);
            memset(box_entry, 0, sizeof(struct pksav_gba_pc_pokemon)); // clear box slot
            return error_none;
        }
        struct pksav_gba_party_pokemon party_mon = st->p_party->party[dst.index];
        st->p_party->party[dst.index].pc_data = box_mon;
        st->p_party->party[dst.index].party_data = pd;
        *box_entry = party_mon.pc_data;
        return error_none;
    }
}

/* Convert a raw 136-byte stored (box) PK4 into a raw 236-byte party PK4 with a
 * freshly-derived stats region. Returns false if the mon has no legal stats
 * (empty/egg/unknown species) — the caller must refuse the move. */
static bool gen4_stored_to_party_raw(const uint8_t *box_raw, uint8_t *party_raw_out)
{
    uint8_t dec[GEN4_PK4_PARTY_SIZE];
    memset(dec, 0, sizeof(dec));
    gen4_pk4_decrypt(box_raw, dec, false); // decrypt the 136-byte stored form
    if (!gen4_build_party_stats(dec))
    {
        return false;
    }
    gen4_pk4_encrypt(dec, party_raw_out, true); // re-encrypt as 236-byte party form
    return true;
}

static pksavhelper_error bills_pc_move_gen4(PokemonSave *s, struct bills_pc_slot src, struct bills_pc_slot dst)
{
    struct gen4_save *g4 = &s->save.gen4_save;
    bool src_party = src.location == BILLS_PC_LOC_PARTY;
    bool dst_party = dst.location == BILLS_PC_LOC_PARTY;

    if (!bills_pc_slot_occupied(s, src.location, src.box_num, src.index))
    {
        return error_swap_pkmn;
    }
    bool dst_occupied = bills_pc_slot_occupied(s, dst.location, dst.box_num, dst.index);

    // box <-> box: raw 136-byte relocation (swap; an empty slot is all-zero).
    if (!src_party && !dst_party)
    {
        uint8_t *a = gen4_box_slot_raw_mut(g4, src.box_num, src.index);
        uint8_t *b = gen4_box_slot_raw_mut(g4, dst.box_num, dst.index);
        if (!a || !b)
        {
            return error_swap_pkmn;
        }
        if (a == b)
        {
            return error_none;
        }
        uint8_t tmp[GEN4_PK4_STORED_SIZE];
        memcpy(tmp, a, GEN4_PK4_STORED_SIZE);
        memcpy(a, b, GEN4_PK4_STORED_SIZE);
        memcpy(b, tmp, GEN4_PK4_STORED_SIZE);
        return error_none;
    }

    // party <-> party: raw 236-byte swap. The party is contiguous, so there is
    // no empty slot to "move" into.
    if (src_party && dst_party)
    {
        if (src.index == dst.index || !dst_occupied)
        {
            return error_none;
        }
        uint8_t *a = gen4_party_slot_raw_mut(g4, src.index);
        uint8_t *b = gen4_party_slot_raw_mut(g4, dst.index);
        if (!a || !b)
        {
            return error_swap_pkmn;
        }
        uint8_t tmp[GEN4_PK4_PARTY_SIZE];
        memcpy(tmp, a, GEN4_PK4_PARTY_SIZE);
        memcpy(a, b, GEN4_PK4_PARTY_SIZE);
        memcpy(b, tmp, GEN4_PK4_PARTY_SIZE);
        return error_none;
    }

    // party -> box: the box stores only the 136-byte form (first 136 bytes of
    // the encrypted party form are exactly a valid stored form).
    if (src_party && !dst_party)
    {
        uint8_t *party_slot = gen4_party_slot_raw_mut(g4, src.index);
        uint8_t *box_slot = gen4_box_slot_raw_mut(g4, dst.box_num, dst.index);
        if (!party_slot || !box_slot)
        {
            return error_swap_pkmn;
        }
        int count = (int)gen4_party_count(g4);
        if (!dst_occupied)
        {
            // Deposit into an empty box slot and compact the party.
            if (count <= 1)
            {
                return error_swap_pkmn; // can't empty the party
            }
            memcpy(box_slot, party_slot, GEN4_PK4_STORED_SIZE);
            for (int i = src.index; i < count - 1; i++)
            {
                uint8_t *cur = gen4_party_slot_raw_mut(g4, i);
                uint8_t *nxt = gen4_party_slot_raw_mut(g4, i + 1);
                memcpy(cur, nxt, GEN4_PK4_PARTY_SIZE);
            }
            memset(gen4_party_slot_raw_mut(g4, count - 1), 0, GEN4_PK4_PARTY_SIZE);
            gen4_set_party_count(g4, (uint8_t)(count - 1));
            return error_none;
        }
        // Swap: the box mon enters the party slot (needs rebuilt stats), and the
        // party mon drops to its 136-byte stored form in the box.
        uint8_t box_mon[GEN4_PK4_STORED_SIZE];
        memcpy(box_mon, box_slot, GEN4_PK4_STORED_SIZE);
        uint8_t new_party[GEN4_PK4_PARTY_SIZE];
        if (!gen4_stored_to_party_raw(box_mon, new_party))
        {
            return error_swap_pkmn; // egg / unknown species: no legal stats
        }
        memcpy(box_slot, party_slot, GEN4_PK4_STORED_SIZE); // party -> box (drop stats)
        memcpy(party_slot, new_party, GEN4_PK4_PARTY_SIZE); // box -> party (rebuilt)
        return error_none;
    }

    // box -> party: withdraw. Needs the stats region rebuilt.
    {
        uint8_t *box_slot = gen4_box_slot_raw_mut(g4, src.box_num, src.index);
        if (!box_slot)
        {
            return error_swap_pkmn;
        }
        uint8_t box_mon[GEN4_PK4_STORED_SIZE];
        memcpy(box_mon, box_slot, GEN4_PK4_STORED_SIZE);
        uint8_t new_party[GEN4_PK4_PARTY_SIZE];
        if (!gen4_stored_to_party_raw(box_mon, new_party))
        {
            return error_swap_pkmn; // egg / unknown species: no legal stats
        }
        int count = (int)gen4_party_count(g4);
        if (!dst_occupied)
        {
            // Withdraw into an empty (appended) party slot, clearing the box slot.
            if (count >= GEN4_PARTY_MAX)
            {
                return error_swap_pkmn; // party full
            }
            memcpy(gen4_party_slot_raw_mut(g4, count), new_party, GEN4_PK4_PARTY_SIZE);
            gen4_set_party_count(g4, (uint8_t)(count + 1));
            memset(box_slot, 0, GEN4_PK4_STORED_SIZE);
            return error_none;
        }
        // Swap with an occupied party slot: party mon drops to the box.
        uint8_t *party_slot = gen4_party_slot_raw_mut(g4, dst.index);
        if (!party_slot)
        {
            return error_swap_pkmn;
        }
        memcpy(box_slot, party_slot, GEN4_PK4_STORED_SIZE); // party -> box (drop stats)
        memcpy(party_slot, new_party, GEN4_PK4_PARTY_SIZE); // box -> party (rebuilt)
        return error_none;
    }
}

pksavhelper_error bills_pc_move_pkmn(PokemonSave *pkmn_save, struct bills_pc_slot src, struct bills_pc_slot dst)
{
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        return bills_pc_move_gen1(pkmn_save, src, dst);
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_2)
    {
        return bills_pc_move_gen2(pkmn_save, src, dst);
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        return bills_pc_move_gen3(pkmn_save, src, dst);
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        return bills_pc_move_gen4(pkmn_save, src, dst);
    }
    return error_swap_pkmn;
}

// -------------------- Sorting --------------------

static bool bills_pc_view_less(enum bills_pc_sort_mode mode, const struct bills_pc_entry_view *a, const struct bills_pc_entry_view *b)
{
    switch (mode)
    {
    case BILLS_PC_SORT_DEX:
    {
        int da = a->dex ? a->dex : 999; // unknown/invalid sorts last
        int db = b->dex ? b->dex : 999;
        if (da != db)
        {
            return da < db;
        }
        return bills_pc_ci_strcmp(a->nickname, b->nickname) < 0;
    }
    case BILLS_PC_SORT_NAME:
        return bills_pc_ci_strcmp(a->nickname, b->nickname) < 0;
    case BILLS_PC_SORT_LEVEL:
        if (a->level != b->level)
        {
            return a->level > b->level; // strongest first
        }
        return bills_pc_ci_strcmp(a->nickname, b->nickname) < 0;
    case BILLS_PC_SORT_TYPE:
    {
        int ta = (a->type1 == BILLS_PC_TYPE_UNKNOWN) ? 999 : a->type1;
        int tb = (b->type1 == BILLS_PC_TYPE_UNKNOWN) ? 999 : b->type1;
        if (ta != tb)
        {
            return ta < tb;
        }
        return bills_pc_ci_strcmp(a->nickname, b->nickname) < 0;
    }
    default:
        return false;
    }
}

void bills_pc_sort_box(PokemonSave *pkmn_save, int box_num, enum bills_pc_sort_mode mode)
{
    if (mode == BILLS_PC_SORT_NONE || mode >= BILLS_PC_SORT_COUNT)
    {
        return;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        // Gen 4: gather occupied 136-byte stored mons, sort a permutation of
        // their views, and repack contiguously from the top of the box.
        struct gen4_save *g4 = &pkmn_save->save.gen4_save;
        uint8_t occupied[GEN4_BOX_SLOTS][GEN4_PK4_STORED_SIZE];
        struct bills_pc_entry_view gviews[GEN4_BOX_SLOTS];
        int gorder[GEN4_BOX_SLOTS];
        int m = 0;
        for (int i = 0; i < GEN4_BOX_SLOTS; i++)
        {
            if (!bills_pc_slot_occupied(pkmn_save, BILLS_PC_LOC_BOX, box_num, i))
            {
                continue;
            }
            memcpy(occupied[m], gen4_box_slot_raw_mut(g4, box_num, i), GEN4_PK4_STORED_SIZE);
            bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, box_num, i, &gviews[m]);
            gorder[m] = m;
            m++;
        }
        if (m <= 1)
        {
            return;
        }
        for (int i = 1; i < m; i++)
        {
            int cur = gorder[i];
            int j = i - 1;
            while (j >= 0 && bills_pc_view_less(mode, &gviews[cur], &gviews[gorder[j]]))
            {
                gorder[j + 1] = gorder[j];
                j--;
            }
            gorder[j + 1] = cur;
        }
        for (int k = 0; k < m; k++)
        {
            memcpy(gen4_box_slot_raw_mut(g4, box_num, k), occupied[gorder[k]], GEN4_PK4_STORED_SIZE);
        }
        for (int k = m; k < GEN4_BOX_SLOTS; k++)
        {
            memset(gen4_box_slot_raw_mut(g4, box_num, k), 0, GEN4_PK4_STORED_SIZE);
        }
        return;
    }
    int n = bills_pc_container_count(pkmn_save, BILLS_PC_LOC_BOX, box_num);
    if (n <= 1)
    {
        return;
    }

    // Gen 3: boxes are slot-based (gaps allowed). Gather occupied slots, sort,
    // and repack contiguously from the top of the box.
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        struct pksav_gba_pokemon_box *box = &pkmn_save->save.gba_save.pokemon_storage.p_pc->boxes[box_num];
        struct pksav_gba_pc_pokemon occupied[PKSAV_GBA_BOX_NUM_POKEMON];
        struct bills_pc_entry_view gviews[PKSAV_GBA_BOX_NUM_POKEMON];
        int gorder[PKSAV_GBA_BOX_NUM_POKEMON];
        int m = 0;
        for (int i = 0; i < PKSAV_GBA_BOX_NUM_POKEMON; i++)
        {
            if (box->entries[i].blocks.growth.species != 0)
            {
                occupied[m] = box->entries[i];
                bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, box_num, i, &gviews[m]);
                gorder[m] = m;
                m++;
            }
        }
        for (int i = 1; i < m; i++)
        {
            int cur = gorder[i];
            int j = i - 1;
            while (j >= 0 && bills_pc_view_less(mode, &gviews[cur], &gviews[gorder[j]]))
            {
                gorder[j + 1] = gorder[j];
                j--;
            }
            gorder[j + 1] = cur;
        }
        for (int k = 0; k < m; k++)
        {
            box->entries[k] = occupied[gorder[k]];
        }
        for (int k = m; k < PKSAV_GBA_BOX_NUM_POKEMON; k++)
        {
            memset(&box->entries[k], 0, sizeof(struct pksav_gba_pc_pokemon));
        }
        return;
    }

    struct bills_pc_entry_view views[PKSAV_GEN2_BOX_NUM_POKEMON];
    int order[PKSAV_GEN2_BOX_NUM_POKEMON];
    for (int i = 0; i < n; i++)
    {
        order[i] = i;
        bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, box_num, i, &views[i]);
    }
    // Stable insertion sort over the permutation (box holds at most 20).
    for (int i = 1; i < n; i++)
    {
        int cur = order[i];
        int j = i - 1;
        while (j >= 0 && bills_pc_view_less(mode, &views[cur], &views[order[j]]))
        {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = cur;
    }

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        struct pksav_gen1_pokemon_box *box = pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[box_num];
        struct pksav_gen1_pokemon_box original = *box;
        for (int k = 0; k < n; k++)
        {
            int src = order[k];
            box->species[k] = original.species[src];
            box->entries[k] = original.entries[src];
            memcpy(box->otnames[k], original.otnames[src], sizeof(box->otnames[k]));
            memcpy(box->nicknames[k], original.nicknames[src], sizeof(box->nicknames[k]));
        }
        box->species[n] = 0xFF;
    }
    else
    {
        struct pksav_gen2_pokemon_box *box = pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[box_num];
        struct pksav_gen2_pokemon_box original = *box;
        for (int k = 0; k < n; k++)
        {
            int src = order[k];
            box->species[k] = original.species[src];
            box->entries[k] = original.entries[src];
            memcpy(box->otnames[k], original.otnames[src], sizeof(box->otnames[k]));
            memcpy(box->nicknames[k], original.nicknames[src], sizeof(box->nicknames[k]));
        }
        box->species[n] = 0xFF;
    }
}

// Largest total boxed capacity across all supported gens (Gen 3: 14 boxes x 30).
#define BILLS_PC_MAX_BOXED (PKSAV_GBA_NUM_POKEMON_BOXES * PKSAV_GBA_BOX_NUM_POKEMON)

// Sort a permutation `order` (size total) by the given mode over `views`.
static void bills_pc_sort_order(int *order, const struct bills_pc_entry_view *views, int total, enum bills_pc_sort_mode mode)
{
    for (int i = 0; i < total; i++)
    {
        order[i] = i;
    }
    for (int i = 1; i < total; i++)
    {
        int cur = order[i];
        int j = i - 1;
        while (j >= 0 && bills_pc_view_less(mode, &views[cur], &views[order[j]]))
        {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = cur;
    }
}

void bills_pc_sort_all_boxes(PokemonSave *pkmn_save, enum bills_pc_sort_mode mode)
{
    if (mode == BILLS_PC_SORT_NONE || mode >= BILLS_PC_SORT_COUNT)
    {
        return;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        // Gen 4 has 18x30 slots, exceeding the shared Gen3-sized scratch below,
        // so it uses its own buffers. Gather all occupied stored mons across
        // boxes, sort, and repack contiguously filling box 0, box 1, ...
        struct gen4_save *g4 = &pkmn_save->save.gen4_save;
        enum { G4_MAX_BOXED = GEN4_NUM_BOXES * GEN4_BOX_SLOTS };
        static uint8_t g4_entries[G4_MAX_BOXED][GEN4_PK4_STORED_SIZE];
        struct bills_pc_entry_view g4_views[G4_MAX_BOXED];
        int g4_order[G4_MAX_BOXED];
        int total = 0;
        for (int b = 0; b < GEN4_NUM_BOXES; b++)
        {
            for (int s = 0; s < GEN4_BOX_SLOTS; s++)
            {
                if (!bills_pc_slot_occupied(pkmn_save, BILLS_PC_LOC_BOX, b, s))
                {
                    continue;
                }
                memcpy(g4_entries[total], gen4_box_slot_raw_mut(g4, b, s), GEN4_PK4_STORED_SIZE);
                bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, b, s, &g4_views[total]);
                total++;
            }
        }
        if (total <= 1)
        {
            return;
        }
        bills_pc_sort_order(g4_order, g4_views, total, mode);
        int idx = 0;
        for (int b = 0; b < GEN4_NUM_BOXES; b++)
        {
            for (int s = 0; s < GEN4_BOX_SLOTS; s++)
            {
                uint8_t *slot = gen4_box_slot_raw_mut(g4, b, s);
                if (idx < total)
                {
                    memcpy(slot, g4_entries[g4_order[idx++]], GEN4_PK4_STORED_SIZE);
                }
                else
                {
                    memset(slot, 0, GEN4_PK4_STORED_SIZE);
                }
            }
        }
        return;
    }
    int nboxes = bills_pc_num_boxes(pkmn_save);
    int cap = bills_pc_box_capacity(pkmn_save);

    struct bills_pc_entry_view views[BILLS_PC_MAX_BOXED];
    int order[BILLS_PC_MAX_BOXED];

    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        struct pksav_gba_pc_pokemon entries[BILLS_PC_MAX_BOXED];
        int total = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gba_pokemon_box *box = &pkmn_save->save.gba_save.pokemon_storage.p_pc->boxes[b];
            for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; s++)
            {
                if (box->entries[s].blocks.growth.species != 0)
                {
                    entries[total] = box->entries[s];
                    bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, b, s, &views[total]);
                    total++;
                }
            }
        }
        if (total <= 1)
        {
            return;
        }
        bills_pc_sort_order(order, views, total, mode);

        int idx = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gba_pokemon_box *box = &pkmn_save->save.gba_save.pokemon_storage.p_pc->boxes[b];
            for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; s++)
            {
                if (idx < total)
                {
                    box->entries[s] = entries[order[idx++]];
                }
                else
                {
                    memset(&box->entries[s], 0, sizeof(struct pksav_gba_pc_pokemon));
                }
            }
        }
        return;
    }

    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        struct pksav_gen1_pc_pokemon entries[BILLS_PC_MAX_BOXED];
        uint8_t species[BILLS_PC_MAX_BOXED];
        uint8_t otnames[BILLS_PC_MAX_BOXED][PKSAV_GEN1_POKEMON_OTNAME_STORAGE_LENGTH + 1];
        uint8_t nicknames[BILLS_PC_MAX_BOXED][PKSAV_GEN1_POKEMON_NICKNAME_LENGTH + 1];

        int total = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gen1_pokemon_box *box = pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[b];
            int c = bills_pc_container_count(pkmn_save, BILLS_PC_LOC_BOX, b);
            for (int s = 0; s < c; s++)
            {
                entries[total] = box->entries[s];
                species[total] = box->species[s];
                memcpy(otnames[total], box->otnames[s], sizeof(otnames[total]));
                memcpy(nicknames[total], box->nicknames[s], sizeof(nicknames[total]));
                bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, b, s, &views[total]);
                total++;
            }
        }
        if (total <= 1)
        {
            return;
        }
        bills_pc_sort_order(order, views, total, mode);

        int idx = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gen1_pokemon_box *box = pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[b];
            int put = total - idx;
            if (put > cap) put = cap;
            if (put < 0) put = 0;
            for (int s = 0; s < put; s++)
            {
                int k = order[idx++];
                box->species[s] = species[k];
                box->entries[s] = entries[k];
                memcpy(box->otnames[s], otnames[k], sizeof(box->otnames[s]));
                memcpy(box->nicknames[s], nicknames[k], sizeof(box->nicknames[s]));
            }
            box->count = (uint8_t)put;
            box->species[put] = 0xFF;
        }
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_2)
    {
        struct pksav_gen2_pc_pokemon entries[BILLS_PC_MAX_BOXED];
        uint8_t species[BILLS_PC_MAX_BOXED];
        uint8_t otnames[BILLS_PC_MAX_BOXED][PKSAV_GEN2_POKEMON_OTNAME_STORAGE_LENGTH + 1];
        uint8_t nicknames[BILLS_PC_MAX_BOXED][PKSAV_GEN2_POKEMON_NICKNAME_LENGTH + 1];

        int total = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gen2_pokemon_box *box = pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[b];
            int c = bills_pc_container_count(pkmn_save, BILLS_PC_LOC_BOX, b);
            for (int s = 0; s < c; s++)
            {
                entries[total] = box->entries[s];
                species[total] = box->species[s];
                memcpy(otnames[total], box->otnames[s], sizeof(otnames[total]));
                memcpy(nicknames[total], box->nicknames[s], sizeof(nicknames[total]));
                bills_pc_get_view(pkmn_save, BILLS_PC_LOC_BOX, b, s, &views[total]);
                total++;
            }
        }
        if (total <= 1)
        {
            return;
        }
        bills_pc_sort_order(order, views, total, mode);

        int idx = 0;
        for (int b = 0; b < nboxes; b++)
        {
            struct pksav_gen2_pokemon_box *box = pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[b];
            int put = total - idx;
            if (put > cap) put = cap;
            if (put < 0) put = 0;
            for (int s = 0; s < put; s++)
            {
                int k = order[idx++];
                box->species[s] = species[k];
                box->entries[s] = entries[k];
                memcpy(box->otnames[s], otnames[k], sizeof(box->otnames[s]));
                memcpy(box->nicknames[s], nicknames[k], sizeof(box->nicknames[s]));
            }
            box->count = (uint8_t)put;
            box->species[put] = 0xFF;
        }
    }
}

// -------------------- Current-box mirror sync --------------------

void bills_pc_normalize_current_box(PokemonSave *pkmn_save)
{
    int cur = bills_pc_current_box_num(pkmn_save);
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        *pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[cur] =
            *pkmn_save->save.gen1_save.pokemon_storage.p_current_box;
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_2)
    {
        *pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[cur] =
            *pkmn_save->save.gen2_save.pokemon_storage.p_current_box;
    }
}

void bills_pc_flush_current_box(PokemonSave *pkmn_save)
{
    int cur = bills_pc_current_box_num(pkmn_save);
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        *pkmn_save->save.gen1_save.pokemon_storage.p_current_box =
            *pkmn_save->save.gen1_save.pokemon_storage.pp_boxes[cur];
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_2)
    {
        *pkmn_save->save.gen2_save.pokemon_storage.p_current_box =
            *pkmn_save->save.gen2_save.pokemon_storage.pp_boxes[cur];
    }
}

// -------------------- Pokedex to-do box --------------------

struct dex_box_cand
{
    struct bills_pc_slot slot;
    uint16_t dex;
    uint8_t level;
    uint8_t copies; // copies of this species the box wants
    bool in_target;
    bool selected;
};

// Copies of `dex` the to-do box wants: one per direct evolution branch that
// still leads to an unregistered entry, or one if breeding is its only job.
static uint8_t dex_box_copies_wanted(const PokemonSave *s, uint16_t dex)
{
    struct pkmn_needed_evo tmp[PKMN_MAX_NEEDED_EVOS];
    int copies = 0;
    if (pkmn_needed_evolutions(s, dex, tmp) > 0)
    {
        uint16_t max_dex = pokedex_species_count(s);
        const struct pkmn_evo_edge *evos[PKMN_MAX_DIRECT_EVOS];
        uint8_t n = pkmn_direct_evolutions(dex, evos);
        for (uint8_t i = 0; i < n; i++)
        {
            if (evos[i]->to > max_dex)
            {
                continue;
            }
            bool seen = false, owned = false;
            pokedex_get_entry(s, evos[i]->to, &seen, &owned);
            if (!owned || pkmn_needed_evolutions(s, evos[i]->to, tmp) > 0)
            {
                copies++;
            }
        }
        if (copies == 0)
        {
            copies = 1;
        }
    }
    else if (pkmn_can_breed(s, dex) && pkmn_needed_preevolutions(s, dex, tmp) > 0)
    {
        copies = 1;
    }
    return (uint8_t)copies;
}

static int dex_box_cand_cmp(const void *pa, const void *pb)
{
    const struct dex_box_cand *a = pa, *b = pb;
    if (a->dex != b->dex)
    {
        return a->dex < b->dex ? -1 : 1;
    }
    if (a->in_target != b->in_target)
    {
        return a->in_target ? -1 : 1; // keep what is already there
    }
    if (a->level != b->level)
    {
        return a->level > b->level ? -1 : 1; // closest to evolving first
    }
    if (a->slot.box_num != b->slot.box_num)
    {
        return a->slot.box_num < b->slot.box_num ? -1 : 1;
    }
    return a->slot.index - b->slot.index;
}

// Scan every box for Pokemon with dex jobs and mark which copies the target
// box should hold. Returns the candidate count; *out_wanted is the number the
// box would take with unlimited room.
static int dex_box_select(const PokemonSave *s, int target, struct dex_box_cand *cands, int max_cands, int *out_wanted)
{
    int num_boxes = bills_pc_num_boxes(s);
    int cap = bills_pc_box_capacity(s);
    int n = 0;
    for (int b = 0; b < num_boxes && n < max_cands; b++)
    {
        for (int i = 0; i < cap && n < max_cands; i++)
        {
            if (!bills_pc_slot_occupied(s, BILLS_PC_LOC_BOX, b, i))
            {
                continue;
            }
            struct bills_pc_entry_view v;
            bills_pc_get_view(s, BILLS_PC_LOC_BOX, b, i, &v);
            if (!v.occupied || v.dex == 0)
            {
                continue;
            }
            uint8_t copies = dex_box_copies_wanted(s, v.dex);
            if (copies == 0)
            {
                continue;
            }
            cands[n].slot = (struct bills_pc_slot){BILLS_PC_LOC_BOX, b, i};
            cands[n].dex = v.dex;
            cands[n].level = v.level;
            cands[n].copies = copies;
            cands[n].in_target = (b == target);
            cands[n].selected = false;
            n++;
        }
    }
    qsort(cands, n, sizeof(cands[0]), dex_box_cand_cmp);

    int wanted = 0, selected = 0, taken = 0;
    uint16_t last_dex = 0;
    for (int i = 0; i < n; i++)
    {
        if (cands[i].dex != last_dex)
        {
            last_dex = cands[i].dex;
            taken = 0;
        }
        if (taken < cands[i].copies)
        {
            wanted++;
            taken++;
            if (selected < cap)
            {
                cands[i].selected = true;
                selected++;
            }
        }
    }
    *out_wanted = wanted;
    return n;
}

int bills_pc_fill_dex_box(PokemonSave *pkmn_save, int box_num, int *out_wanted, int *out_placed)
{
    static struct dex_box_cand cands[GEN4_NUM_BOXES * GEN4_BOX_SLOTS];
    const int max_cands = (int)(sizeof(cands) / sizeof(cands[0]));
    int cap = bills_pc_box_capacity(pkmn_save);
    int wanted = 0, placed = 0, moved = 0;
    if (box_num < 0 || box_num >= bills_pc_num_boxes(pkmn_save))
    {
        if (out_wanted) *out_wanted = 0;
        if (out_placed) *out_placed = 0;
        return 0;
    }

    // Each pass re-selects from the current layout and moves one chosen mon
    // in; selected occupants are never evicted, so every pass makes progress.
    for (int pass = 0; pass <= cap * 2; pass++)
    {
        int n = dex_box_select(pkmn_save, box_num, cands, max_cands, &wanted);
        int src = -1;
        placed = 0;
        for (int i = 0; i < n; i++)
        {
            if (cands[i].selected && cands[i].in_target)
            {
                placed++;
            }
            else if (cands[i].selected && src < 0)
            {
                src = i;
            }
        }
        if (src < 0)
        {
            break; // everything chosen is in the box
        }

        struct bills_pc_slot dst = {BILLS_PC_LOC_BOX, box_num, -1};
        for (int i = 0; i < cap; i++)
        {
            if (!bills_pc_slot_occupied(pkmn_save, BILLS_PC_LOC_BOX, box_num, i))
            {
                dst.index = i;
                break;
            }
        }
        if (dst.index < 0)
        {
            // Full: evict an occupant that is not one of the keepers.
            for (int i = 0; i < cap && dst.index < 0; i++)
            {
                bool keeper = false;
                for (int c = 0; c < n; c++)
                {
                    if (cands[c].selected && cands[c].in_target && cands[c].slot.index == i)
                    {
                        keeper = true;
                        break;
                    }
                }
                if (!keeper)
                {
                    dst.index = i;
                }
            }
        }
        if (dst.index < 0 || bills_pc_move_pkmn(pkmn_save, cands[src].slot, dst) != error_none)
        {
            break;
        }
        moved++;
    }

    // Make it exclusively the to-do box: hand any leftover occupant that is
    // not a keeper to the first free slot elsewhere (lowest box first).
    for (int pass = 0; pass < cap; pass++)
    {
        int n = dex_box_select(pkmn_save, box_num, cands, max_cands, &wanted);
        int victim = -1;
        for (int i = 0; i < cap && victim < 0; i++)
        {
            if (!bills_pc_slot_occupied(pkmn_save, BILLS_PC_LOC_BOX, box_num, i))
            {
                continue;
            }
            bool keeper = false;
            for (int c = 0; c < n; c++)
            {
                if (cands[c].selected && cands[c].in_target && cands[c].slot.index == i)
                {
                    keeper = true;
                    break;
                }
            }
            if (!keeper)
            {
                victim = i;
            }
        }
        if (victim < 0)
        {
            break;
        }
        struct bills_pc_slot dst = {BILLS_PC_LOC_BOX, -1, -1};
        int num_boxes = bills_pc_num_boxes(pkmn_save);
        for (int b = 0; b < num_boxes && dst.box_num < 0; b++)
        {
            if (b == box_num)
            {
                continue;
            }
            for (int i = 0; i < cap; i++)
            {
                if (!bills_pc_slot_occupied(pkmn_save, BILLS_PC_LOC_BOX, b, i))
                {
                    dst.box_num = b;
                    dst.index = i;
                    break;
                }
            }
        }
        if (dst.box_num < 0)
        {
            break; // every other box is full; it stays
        }
        struct bills_pc_slot src = {BILLS_PC_LOC_BOX, box_num, victim};
        if (bills_pc_move_pkmn(pkmn_save, src, dst) != error_none)
        {
            break;
        }
        moved++;
    }

    if (moved > 0)
    {
        bills_pc_sort_box(pkmn_save, box_num, BILLS_PC_SORT_DEX);
    }
    if (out_wanted) *out_wanted = wanted;
    if (out_placed) *out_placed = placed;
    return moved;
}

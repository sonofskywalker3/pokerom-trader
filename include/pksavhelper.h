#ifndef PKSAVHELPER_H
#define PKSAVHELPER_H

#include "common.h"
#include "pkmnstats.h"

#define MAX_GEN1_MOVE_INDEX 165
#define MOVE_INDEX_HM01 15
#define MOVE_INDEX_HM02 19
#define MOVE_INDEX_HM03 57
#define MOVE_INDEX_HM04 70
#define MOVE_INDEX_HM05 127
#define MOVE_INDEX_HM06 165

#define TRAINER_NAME_TEXT_MAX 7
#define PKMN_NAME_TEXT_MAX 10

struct pkmn_evolution_pair_data
{
    char species_name[PKMN_NAME_TEXT_MAX + 1];
    char evolution_name[PKMN_NAME_TEXT_MAX + 1];
    uint8_t species_index;
    uint8_t evolution_index;
    uint8_t evolution_item;
};

enum eligible_evolution_status
{
    E_EVO_STATUS_NOT_ELIGIBLE,
    E_EVO_STATUS_ELIGIBLE,
    E_EVO_STATUS_MISSING_ITEM
};

enum eligible_trade_status
{
    E_TRADE_STATUS_ELIGIBLE,
    E_TRADE_STATUS_GEN2_PKMN,
    E_TRADE_STATUS_GEN2_MOVE,
    E_TRADE_STATUS_HM_MOVE,
    E_TRADE_STATUS_MAIL
};

#define LEFTOVERS 146
#define BITTER_BERRY 83
#define BERRY 173
#define GOLD_BERRY 174

static const uint8_t trade_catch_rate_to_item[251] = {
    [25] = LEFTOVERS,
    [45] = BITTER_BERRY,
    [50] = GOLD_BERRY,
    [90] = BERRY,
    [100] = BERRY,
    [120] = BERRY,
    [135] = BERRY,
    [190] = BERRY,
    [195] = BERRY,
    [220] = BERRY,
    [250] = BERRY};

// Pokémon Evolution Pair Lookup Table
static const struct pkmn_evolution_pair_data pkmn_evolution_pairs_gen1[SI_HAUNTER + 1] = {
    [SI_KADABRA] = {.species_name = "KADABRA", .evolution_name = "ALAKAZAM", .species_index = SI_KADABRA, .evolution_index = SI_ALAKAZAM},
    [SI_MACHOKE] = {.species_name = "MACHOKE", .evolution_name = "MACHAMP", .species_index = SI_MACHOKE, .evolution_index = SI_MACHAMP},
    [SI_GRAVELER] = {.species_name = "GRAVELER", .evolution_name = "GOLEM", .species_index = SI_GRAVELER, .evolution_index = SI_GOLEM},
    [SI_HAUNTER] = {.species_name = "HAUNTER", .evolution_name = "GENGAR", .species_index = SI_HAUNTER, .evolution_index = SI_GENGAR}};

static const struct pkmn_evolution_pair_data pkmn_evolution_pairs_gen2[MAX_SPECIES_INDEX + 1] = {
    [KADABRA] = { .species_name = "KADABRA", .evolution_name = "ALAKAZAM", .species_index = KADABRA, .evolution_index = ALAKAZAM},
    [MACHOKE] = {.species_name = "MACHOKE", .evolution_name = "MACHAMP", .species_index = MACHOKE, .evolution_index = MACHAMP},
    [GRAVELER] = {.species_name = "GRAVELER", .evolution_name = "GOLEM", .species_index = GRAVELER, .evolution_index = GOLEM},
    [HAUNTER] = {.species_name = "HAUNTER", .evolution_name = "GENGAR", .species_index = HAUNTER, .evolution_index = GENGAR},
    [SCYTHER] = {.species_name = "SCYTHER", .evolution_name = "SCIZOR", .species_index = SCYTHER, .evolution_index = SCIZOR, .evolution_item = METAL_COAT},
    [POLIWHIRL] = {.species_name = "POLIWHIRL", .evolution_name = "POLITOED", .species_index = POLIWHIRL, .evolution_index = POLITOED, .evolution_item = KING_ROCK},
    [SLOWPOKE] = {.species_name = "SLOWPOKE", .evolution_name = "SLOWKING", .species_index = SLOWPOKE, .evolution_index = SLOWKING, .evolution_item = KING_ROCK},
    [ONIX] = {.species_name = "ONIX", .evolution_name = "STEELIX", .species_index = ONIX, .evolution_index = STEELIX, .evolution_item = METAL_COAT},
    [PORYGON] = {.species_name = "PORYGON", .evolution_name = "PORYGON2", .species_index = PORYGON, .evolution_index = PORYGON2, .evolution_item = UPGRADE},
    [SEADRA] = {.species_name = "SEADRA", .evolution_name = "KINGDRA", .species_index = SEADRA, .evolution_index = KINGDRA, .evolution_item = DRAGON_SCALE}};

// A normalized, gen-agnostic view of a stored Pokémon for listing/sorting/searching.
struct bills_pc_entry_view
{
    bool occupied;
    char nickname[PKMN_NAME_TEXT_MAX + 1];
    uint16_t species; // gen-native species index (Gen 1/3 internal, Gen 2 dex id)
    uint16_t dex;     // National Pokédex number
    uint8_t level;    // 0 when unknown (Gen 3 boxed mons store no level)
    uint8_t type1;    // Gen 1 type enum value, or BILLS_PC_TYPE_UNKNOWN
    uint8_t type2;
};

// Sort every PC box's contents at once: all boxed Pokémon are gathered, sorted,
// and repacked box-by-box (party untouched). Persisted like a per-box sort.
void bills_pc_sort_all_boxes(PokemonSave *pkmn_save, enum bills_pc_sort_mode mode);

// --- Bill's PC (single-save box management) ---
int bills_pc_num_boxes(const PokemonSave *pkmn_save);
int bills_pc_box_capacity(const PokemonSave *pkmn_save);
int bills_pc_party_capacity(const PokemonSave *pkmn_save);
int bills_pc_current_box_num(const PokemonSave *pkmn_save);
int bills_pc_container_count(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num);
void bills_pc_get_view(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index, struct bills_pc_entry_view *out_view);
bool bills_pc_slot_holds_mail(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index);
// Move (deposit/withdraw) or swap a Pokémon between two slots within one save.
// Returns error_none on success, or a pksavhelper_error describing why it was rejected.
pksavhelper_error bills_pc_move_pkmn(PokemonSave *pkmn_save, struct bills_pc_slot src, struct bills_pc_slot dst);
void bills_pc_sort_box(PokemonSave *pkmn_save, int box_num, enum bills_pc_sort_mode mode);
// Copy the (possibly stale) numbered current box into the live current-box buffer.
// Call once when entering Bill's PC.
void bills_pc_normalize_current_box(PokemonSave *pkmn_save);
// Copy the edited numbered current box back into the live current-box buffer.
// Call right before writing the save.
void bills_pc_flush_current_box(PokemonSave *pkmn_save);
const char *pkmn_type_name(uint8_t gen1_type_value);
void bills_pc_species_types(const PokemonSave *pkmn_save, uint16_t species, uint8_t *out_type1, uint8_t *out_type2);
// True if the given slot holds a Pokémon (handles Gen 3 boxes' non-contiguous slots).
bool bills_pc_slot_occupied(const PokemonSave *pkmn_save, enum bills_pc_location location, int box_num, int index);

int error_handler(enum pksav_error error, const char *message);
void swap_party_pkmn_at_indices(struct pksav_gen2_save *pkmn_save, uint8_t pkmn_index1, uint8_t pkmn_index2); // TODO: Update for cross-generation
pksavhelper_error swap_pkmn_at_index_between_saves(PokemonSave *player1_save, PokemonSave *player2_save, uint8_t pkmn_party_index1, uint8_t pkmn_party_index2);
pksavhelper_error swap_pkmn_at_index_between_saves_cross_gen(PokemonSave *player1_save, PokemonSave *player2_save, uint8_t pkmn_party_index1, uint8_t pkmn_party_index2);
void create_trainer(PokemonSave *pkmn_save, struct trainer_info *trainer);
pksavhelper_error update_seen_owned_pkmn(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
enum eligible_evolution_status check_trade_evolution_gen1(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
enum eligible_evolution_status check_trade_evolution_gen2(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
void evolve_party_pokemon_at_index(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
void generate_random_number_step(void);
void update_pkmn_DVs(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
void update_pkmn_stats(PokemonSave *pkmn_save, uint8_t pkmn_party_index);
void generate_rand_num_step(SaveGenerationType save_generation_type);
enum eligible_trade_status check_trade_eligibility(struct trainer_info *trainer, uint8_t pkmn_party_index);

#endif /* PKSAVHELPER_H */

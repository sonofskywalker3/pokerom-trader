/*
 * Copyright (c) 2016-2018 Nicholas Corgan (n.corgan@gmail.com)
 *
 * Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
 * or copy at http://opensource.org/licenses/MIT)
 */
#ifndef PKSAV_GBA_SAVE_H
#define PKSAV_GBA_SAVE_H

#include <pksav/config.h>
#include <pksav/error.h>

#include <pksav/common/trainer_id.h>

#include <pksav/gba/common.h>
#include <pksav/gba/daycare.h>
#include <pksav/gba/items.h>
#include <pksav/gba/map.h>
#include <pksav/gba/pokedex.h>
#include <pksav/gba/pokemon.h>
#include <pksav/gba/roamer.h>
#include <pksav/gba/time.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PKSAV_GBA_SAVE_SIZE (0x10000)

/*
 * Byte offset of the event-flag bit array within SaveBlock1, per game.
 * A flag id is a bit index into this array: byte = base + id/8, bit = id%8.
 * Sourced from the pret decompilation (SaveBlock1 `flags[]` offset):
 *   pokeruby / pokeemerald / pokefirered. Diff-confirmed against saves.
 */
#define PKSAV_GBA_RS_EVENT_FLAGS_OFFSET      (0x1220)
#define PKSAV_GBA_EMERALD_EVENT_FLAGS_OFFSET (0x1270)
#define PKSAV_GBA_FRLG_EVENT_FLAGS_OFFSET    (0x0EE0)

#define PKSAV_GBA_RIVAL_NAME_LENGTH PKSAV_GBA_TRAINER_NAME_LENGTH

#define PKSAV_GBA_SAVE_MONEY_MAX_VALUE        (999999)
#define PKSAV_GBA_SAVE_CASINO_COINS_MAX_VALUE (9999)

struct pksav_gba_options
{
    uint8_t* p_button_mode;

    uint8_t* p_text_options;

    uint8_t* p_sound_battle_options;
};

struct pksav_gba_pokemon_storage
{
    struct pksav_gba_pokemon_party* p_party;

    struct pksav_gba_pokemon_pc* p_pc;

    union pksav_gba_daycare* p_daycare;
};

struct pksav_gba_item_storage
{
    union pksav_gba_item_bag* p_bag;

    struct pksav_gba_item_pc* p_pc;

    uint16_t* p_registered_item;
};

struct pksav_gba_player_info
{
    union pksav_trainer_id* p_id;

    /*!
     * @brief A pointer to the trainer's name.
     *
     * This value should be accessed with ::pksav_text_from_gba with a num_chars
     * value of 7.
     *
     * This value should be set with ::pksav_text_to_gba with a num_chars
     * value of 7.
     */
    uint8_t* p_name;

    uint8_t* p_gender;

    uint32_t* p_money;

    struct pksav_gba_player_location_info* p_location_info;
};

/*struct pksav_gba_rs_fields
{
};

struct pksav_gba_emerald_fields
{
};*/

struct pksav_gba_frlg_fields
{
    uint8_t* p_rival_name;
};

struct pksav_gba_misc_fields
{
    uint16_t* p_casino_coins;

    struct pksav_gba_roamer* p_roamer;

    //struct pksav_gba_rs_fields rs_fields;
    //struct pksav_gba_emerald_fields emerald_fields;
    struct pksav_gba_frlg_fields frlg_fields;
};

struct pksav_gba_save
{
    enum pksav_gba_save_type save_type;

    struct pksav_gba_time* p_time_played;

    struct pksav_gba_options options;

    struct pksav_gba_item_storage item_storage;

    struct pksav_gba_pokemon_storage pokemon_storage;

    struct pksav_gba_pokedex pokedex;

    struct pksav_gba_player_info player_info;

    struct pksav_gba_misc_fields misc_fields;

    void* p_internal;
};

#ifdef __cplusplus
extern "C" {
#endif

PKSAV_API enum pksav_error pksav_gba_get_buffer_save_type(
    const uint8_t* p_buffer,
    size_t buffer_len,
    enum pksav_gba_save_type* p_save_type_out
);

PKSAV_API enum pksav_error pksav_gba_get_file_save_type(
    const char* p_filepath,
    enum pksav_gba_save_type* p_save_type_out
);

PKSAV_API enum pksav_error pksav_gba_load_save_from_buffer(
    uint8_t* p_buffer,
    size_t buffer_len,
    struct pksav_gba_save* p_gba_save_out
);

PKSAV_API enum pksav_error pksav_gba_load_save_from_file(
    const char* p_filepath,
    struct pksav_gba_save* p_gba_save_out
);

PKSAV_API enum pksav_error pksav_gba_save_save(
    const char* p_filepath,
    struct pksav_gba_save* p_gba_save
);

PKSAV_API enum pksav_error pksav_gba_free_save(
    struct pksav_gba_save* p_gba_save
);

/*!
 * @brief Read an in-game event flag from a loaded GBA save.
 *
 * `flag_id` is a bit index into the game's SaveBlock1 event-flag array (the
 * same numbering as the pret decompilation's FLAG_* constants). Returns
 * PKSAV_ERROR_INVALID_SAVE if the save type has no known flag layout or the
 * flag lies outside SaveBlock1.
 */
PKSAV_API enum pksav_error pksav_gba_save_get_flag(
    const struct pksav_gba_save* p_gba_save,
    uint16_t flag_id,
    bool* p_flag_out
);

/*!
 * @brief Set or clear an in-game event flag on a loaded GBA save.
 *
 * The change lands in the decrypted save slot; ::pksav_gba_save_save reshuffles
 * and recomputes section checksums on write. Idempotent.
 */
PKSAV_API enum pksav_error pksav_gba_save_set_flag(
    struct pksav_gba_save* p_gba_save,
    uint16_t flag_id,
    bool value
);

#ifdef __cplusplus
}
#endif

#endif /* PKSAV_GBA_SAVE_H */

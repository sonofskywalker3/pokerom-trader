/*
 * In-game "event" restorer — ticket item injection. See events.h.
 * Distributed under the MIT License (MIT).
 */
#include "events.h"
#include <pksav.h>

// Gen 2 (Crystal) key item id.
#define ITEM_GS_BALL 115
// Gen 3 key item ids (RSE/FRLG).
#define ITEM_EON_TICKET   271
#define ITEM_MYSTIC_TICKET 272
#define ITEM_AURORA_TICKET 273
#define ITEM_OLD_SEA_MAP   361

static const struct pkmn_event EVENTS[] = {
    // --- Gen 2 (Crystal only) ---
    {SAVE_GENERATION_2, (uint8_t)(1u << PKSAV_GEN2_SAVE_TYPE_CRYSTAL), ITEM_GS_BALL,
     "GS Ball", "Celebi",
     "Give the GS Ball to Kurt in Azalea Town, then use it at the Ilex Forest shrine."},

    // --- Gen 3 ---
    {SAVE_GENERATION_3, (uint8_t)((1u << PKSAV_GBA_SAVE_TYPE_RS) | (1u << PKSAV_GBA_SAVE_TYPE_EMERALD)), ITEM_EON_TICKET,
     "Eon Ticket", "Latias / Latios",
     "Board the S.S. Tidal in Lilycove City and sail to Southern Island."},

    {SAVE_GENERATION_3, (uint8_t)((1u << PKSAV_GBA_SAVE_TYPE_EMERALD) | (1u << PKSAV_GBA_SAVE_TYPE_FRLG)), ITEM_MYSTIC_TICKET,
     "Mystic Ticket", "Lugia & Ho-Oh",
     "Take the ferry to Navel Rock and reach the peak (Ho-Oh) or base (Lugia)."},

    {SAVE_GENERATION_3, (uint8_t)((1u << PKSAV_GBA_SAVE_TYPE_EMERALD) | (1u << PKSAV_GBA_SAVE_TYPE_FRLG)), ITEM_AURORA_TICKET,
     "Aurora Ticket", "Deoxys",
     "Take the ferry to Birth Island and solve the triangle puzzle."},

    {SAVE_GENERATION_3, (uint8_t)(1u << PKSAV_GBA_SAVE_TYPE_EMERALD), ITEM_OLD_SEA_MAP,
     "Old Sea Map", "Mew",
     "Board the S.S. Tidal and sail to Faraway Island."},
};

static int save_type_of(const PokemonSave *pkmn_save)
{
    if (pkmn_save->save_generation_type == SAVE_GENERATION_2)
    {
        return (int)pkmn_save->save.gen2_save.save_type;
    }
    if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        return (int)pkmn_save->save.gba_save.save_type;
    }
    return -1;
}

int events_available(const PokemonSave *pkmn_save, const struct pkmn_event **out, int max)
{
    int type = save_type_of(pkmn_save);
    if (type < 0)
    {
        return 0;
    }
    int n = 0;
    int count = (int)(sizeof(EVENTS) / sizeof(EVENTS[0]));
    for (int i = 0; i < count && n < max; i++)
    {
        if (EVENTS[i].gen != pkmn_save->save_generation_type)
        {
            continue;
        }
        if (EVENTS[i].save_type_mask & (uint8_t)(1u << type))
        {
            out[n++] = &EVENTS[i];
        }
    }
    return n;
}

// Add a Gen 3 key item to the correct item-bag variant. Returns true on success.
static bool gba_add_key_item(PokemonSave *pkmn_save, uint16_t item_id)
{
    union pksav_gba_item_bag *bag = pkmn_save->save.gba_save.item_storage.p_bag;
    struct pksav_item *keys;
    int capacity;
    switch (pkmn_save->save.gba_save.save_type)
    {
    case PKSAV_GBA_SAVE_TYPE_RS:
        keys = bag->rs.key_items;
        capacity = 20;
        break;
    case PKSAV_GBA_SAVE_TYPE_EMERALD:
        keys = bag->emerald.key_items;
        capacity = 30;
        break;
    case PKSAV_GBA_SAVE_TYPE_FRLG:
        keys = bag->frlg.key_items;
        capacity = 30;
        break;
    default:
        return false;
    }
    for (int i = 0; i < capacity; i++)
    {
        if (pksav_littleendian16(keys[i].index) == item_id)
        {
            return false; // already have it
        }
    }
    for (int i = 0; i < capacity; i++)
    {
        if (pksav_littleendian16(keys[i].index) == 0)
        {
            keys[i].index = pksav_littleendian16(item_id);
            keys[i].count = pksav_littleendian16(1);
            return true;
        }
    }
    return false; // pocket full
}

static bool gen2_add_key_item(PokemonSave *pkmn_save, uint16_t item_id)
{
    struct pksav_gen2_key_item_pocket *pocket =
        &pkmn_save->save.gen2_save.item_storage.p_item_bag->key_item_pocket;
    for (int i = 0; i < pocket->count; i++)
    {
        if (pocket->item_indices[i] == (uint8_t)item_id)
        {
            return false; // already have it
        }
    }
    if (pocket->count >= 25)
    {
        return false; // pocket effectively full
    }
    pocket->item_indices[pocket->count] = (uint8_t)item_id;
    pocket->count++;
    pocket->item_indices[pocket->count] = 0xFF; // terminator
    return true;
}

pksavhelper_error apply_event(PokemonSave *pkmn_save, const struct pkmn_event *ev)
{
    if (ev->gen != pkmn_save->save_generation_type)
    {
        return error_swap_pkmn;
    }
    bool ok = (ev->gen == SAVE_GENERATION_2)
                  ? gen2_add_key_item(pkmn_save, ev->item_id)
                  : gba_add_key_item(pkmn_save, ev->item_id);
    return ok ? error_none : error_swap_pkmn;
}

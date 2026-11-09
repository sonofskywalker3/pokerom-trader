/*
 * End-to-end test for the Events wiring: drive the real apply_event() through
 * the EVENTS table and confirm the event's flag is set on the save.
 * Build (from repo root, after building deps/pksav):
 *   gcc tests/events_apply_test.c src/events.c -I include -I deps/pksav/include \
 *       -I deps/pksav/build/include -L deps/pksav/build/lib -lpksav -o tests/events_test.exe
 */
#include "common.h"
#include "events.h"
#include <pksav.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond, msg) do { \
    if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define EMERALD_BASELINE "saves/refs/baseline/pokemon_emerald.sav"
#define FLAG_FARAWAY_ISLAND 0x8D6

static int test_old_sea_map_sets_flag(void)
{
    PokemonSave ps;
    ps.save_generation_type = SAVE_GENERATION_3;
    CHECK(pksav_gba_load_save_from_file(EMERALD_BASELINE, &ps.save.gba_save) == PKSAV_ERROR_NONE,
          "load emerald baseline");

    const struct pkmn_event* evs[16];
    int n = events_available(&ps, evs, 16);
    CHECK(n > 0, "events available on emerald");

    const struct pkmn_event* osm = NULL;
    for(int i = 0; i < n; i++)
    {
        if(strcmp(evs[i]->ticket, "Old Sea Map") == 0) osm = evs[i];
    }
    CHECK(osm != NULL, "Old Sea Map event offered on Emerald");

    bool flag = true;
    CHECK(pksav_gba_save_get_flag(&ps.save.gba_save, FLAG_FARAWAY_ISLAND, &flag) == PKSAV_ERROR_NONE,
          "read faraway flag");
    CHECK(flag == false, "faraway flag clear before apply_event");

    CHECK(apply_event(&ps, osm) == error_none, "apply_event(Old Sea Map) succeeds");

    CHECK(pksav_gba_save_get_flag(&ps.save.gba_save, FLAG_FARAWAY_ISLAND, &flag) == PKSAV_ERROR_NONE,
          "read faraway flag after");
    CHECK(flag == true, "apply_event set the faraway-island flag");

    /* idempotent: applying again still succeeds */
    CHECK(apply_event(&ps, osm) == error_none, "apply_event is idempotent");

    pksav_gba_free_save(&ps.save.gba_save);
    printf("test_old_sea_map_sets_flag PASS\n");
    return 0;
}

/* Every ticket must land in the Key Items pocket under its REAL Gen 3 item id
 * (pret: Eon 275, Mystic 370, Aurora 371, Old Sea Map 376), even when the bag
 * already holds the items whose ids the old table used (Basement Key 271 etc.). */
static int key_pocket_has(const PokemonSave *ps, uint16_t item)
{
    const union pksav_gba_item_bag *bag = ps->save.gba_save.item_storage.p_bag;
    const struct pksav_item *keys;
    int cap;
    switch (ps->save.gba_save.save_type)
    {
    case PKSAV_GBA_SAVE_TYPE_RS: keys = bag->rs.key_items; cap = 20; break;
    case PKSAV_GBA_SAVE_TYPE_EMERALD: keys = bag->emerald.key_items; cap = 30; break;
    default: keys = bag->frlg.key_items; cap = 30; break;
    }
    for (int i = 0; i < cap; i++)
        if (pksav_littleendian16(keys[i].index) == item) return 1;
    return 0;
}

static int test_ticket_item_ids(const char *path, const char *game)
{
    PokemonSave ps;
    ps.save_generation_type = SAVE_GENERATION_3;
    CHECK(pksav_gba_load_save_from_file(path, &ps.save.gba_save) == PKSAV_ERROR_NONE, "load gen3 baseline");
    const struct pkmn_event* evs[16];
    int n = events_available(&ps, evs, 16);
    int tickets = 0;
    for (int i = 0; i < n; i++)
    {
        if (evs[i]->gift != NULL) continue;
        uint16_t want = 0;
        if (strcmp(evs[i]->ticket, "Eon Ticket") == 0) want = 275;
        else if (strcmp(evs[i]->ticket, "Mystic Ticket") == 0) want = 370;
        else if (strcmp(evs[i]->ticket, "Aurora Ticket") == 0) want = 371;
        else if (strcmp(evs[i]->ticket, "Old Sea Map") == 0) want = 376;
        CHECK(want != 0 && evs[i]->item_id == want, "ticket uses the real Gen 3 item id");
        CHECK(apply_event(&ps, evs[i]) == error_none, "apply ticket");
        CHECK(key_pocket_has(&ps, want), "ticket is in the Key Items pocket");
        tickets++;
    }
    CHECK(tickets > 0, "game offers tickets");
    pksav_gba_free_save(&ps.save.gba_save);
    printf("test_ticket_item_ids(%s) PASS: %d tickets\n", game, tickets);
    return 0;
}

int main(void)
{
    if(test_old_sea_map_sets_flag()) return 1;
    if(test_ticket_item_ids(EMERALD_BASELINE, "Emerald")) return 1;
    if(test_ticket_item_ids("saves/refs/baseline/pokemon_firered.sav", "FireRed")) return 1;
    if(test_ticket_item_ids("saves/refs/baseline/pokemon_ruby.sav", "Ruby")) return 1;
    printf("ALL PASS\n");
    return 0;
}

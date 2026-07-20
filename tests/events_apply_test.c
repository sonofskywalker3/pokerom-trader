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

int main(void)
{
    if(test_old_sea_map_sets_flag()) return 1;
    printf("ALL PASS\n");
    return 0;
}

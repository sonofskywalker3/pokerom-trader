/*
 * Standalone PKSav flag-API test. Plain asserts; returns nonzero on failure.
 * Build (from repo root, after building deps/pksav):
 *   gcc tests/pksav_flag_test.c -I deps/pksav/include -I deps/pksav/build/include \
 *       -L deps/pksav/build/lib -lpksav -o tests/flag_test.exe
 *   ./tests/flag_test.exe
 */
#include <pksav.h>
#include <stdio.h>

#define CHECK(cond, msg) do { \
    if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define EMERALD_BASELINE "saves/refs/baseline/pokemon_emerald.sav"
#define EMERALD_TMP      "tests/_tmp_emerald.sav"

/* FLAG_ENABLE_SHIP_FARAWAY_ISLAND (Old Sea Map -> Mew), pokeemerald 0x860+0x76. */
#define FLAG_FARAWAY_ISLAND 0x8D6

static int test_gba_flag_roundtrip(void)
{
    struct pksav_gba_save save;
    CHECK(pksav_gba_load_save_from_file(EMERALD_BASELINE, &save) == PKSAV_ERROR_NONE,
          "load emerald baseline");
    CHECK(save.save_type == PKSAV_GBA_SAVE_TYPE_EMERALD, "baseline is Emerald");

    bool flag = true;
    CHECK(pksav_gba_save_get_flag(&save, FLAG_FARAWAY_ISLAND, &flag) == PKSAV_ERROR_NONE,
          "get flag (initial)");
    CHECK(flag == false, "faraway flag starts clear on baseline");

    CHECK(pksav_gba_save_set_flag(&save, FLAG_FARAWAY_ISLAND, true) == PKSAV_ERROR_NONE,
          "set flag");
    CHECK(pksav_gba_save_get_flag(&save, FLAG_FARAWAY_ISLAND, &flag) == PKSAV_ERROR_NONE,
          "get flag (after set)");
    CHECK(flag == true, "flag reads back set in memory");

    /* idempotent */
    CHECK(pksav_gba_save_set_flag(&save, FLAG_FARAWAY_ISLAND, true) == PKSAV_ERROR_NONE,
          "set flag again (idempotent)");

    /* persist through a real save + reload (checksums + section shuffle) */
    CHECK(pksav_gba_save_save(EMERALD_TMP, &save) == PKSAV_ERROR_NONE, "save to tmp");
    pksav_gba_free_save(&save);

    struct pksav_gba_save reloaded;
    CHECK(pksav_gba_load_save_from_file(EMERALD_TMP, &reloaded) == PKSAV_ERROR_NONE,
          "reload tmp");
    flag = false;
    CHECK(pksav_gba_save_get_flag(&reloaded, FLAG_FARAWAY_ISLAND, &flag) == PKSAV_ERROR_NONE,
          "get flag (reloaded)");
    CHECK(flag == true, "flag persisted across save+reload");
    pksav_gba_free_save(&reloaded);

    printf("test_gba_flag_roundtrip PASS\n");
    return 0;
}

#define CRYSTAL_BASELINE "saves/refs/baseline/pokemon_crystal.sav"
#define CRYSTAL_TMP      "tests/_tmp_crystal.sav"
#define GEN2_TEST_FLAG   0x10 /* arbitrary in-range event flag (mechanism test) */

static int test_gen2_flag_roundtrip(void)
{
    struct pksav_gen2_save save;
    CHECK(pksav_gen2_load_save_from_file(CRYSTAL_BASELINE, &save) == PKSAV_ERROR_NONE,
          "load crystal baseline");
    CHECK(save.save_type == PKSAV_GEN2_SAVE_TYPE_CRYSTAL, "baseline is Crystal");

    CHECK(pksav_gen2_save_set_event_flag(&save, GEN2_TEST_FLAG, true) == PKSAV_ERROR_NONE,
          "set gen2 flag");
    bool flag = false;
    CHECK(pksav_gen2_save_get_event_flag(&save, GEN2_TEST_FLAG, &flag) == PKSAV_ERROR_NONE,
          "get gen2 flag");
    CHECK(flag == true, "gen2 flag reads back set in memory");

    CHECK(pksav_gen2_save_save(CRYSTAL_TMP, &save) == PKSAV_ERROR_NONE, "save crystal tmp");
    pksav_gen2_free_save(&save);

    struct pksav_gen2_save reloaded;
    CHECK(pksav_gen2_load_save_from_file(CRYSTAL_TMP, &reloaded) == PKSAV_ERROR_NONE,
          "reload crystal tmp");
    flag = false;
    CHECK(pksav_gen2_save_get_event_flag(&reloaded, GEN2_TEST_FLAG, &flag) == PKSAV_ERROR_NONE,
          "get gen2 flag (reloaded)");
    CHECK(flag == true, "gen2 flag persisted across save+reload");
    pksav_gen2_free_save(&reloaded);

    printf("test_gen2_flag_roundtrip PASS\n");
    return 0;
}

int main(void)
{
    if(test_gba_flag_roundtrip()) return 1;
    if(test_gen2_flag_roundtrip()) return 1;
    printf("ALL PASS\n");
    return 0;
}

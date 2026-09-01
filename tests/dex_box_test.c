/*
 * Pokedex to-do box: breeding helpers + bills_pc_fill_dex_box invariants.
 *
 * Runs against the local Gen 1/2 saves in saves/ (present on the dev machine,
 * gitignored) and the optional Gen 3/4 fixtures; every case SKIPs when its
 * file is absent so a clean checkout still exits 0.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "pkmn_evolutions.h"

static int total_boxed(const PokemonSave *s)
{
    int n = 0;
    for (int b = 0; b < bills_pc_num_boxes(s); b++)
    {
        for (int i = 0; i < bills_pc_box_capacity(s); i++)
        {
            n += bills_pc_slot_occupied(s, BILLS_PC_LOC_BOX, b, i) ? 1 : 0;
        }
    }
    return n;
}

static void check_breeding_helpers(const PokemonSave *sav)
{
    bool gen1 = sav->save_generation_type == SAVE_GENERATION_1;
    uint16_t max_dex = pokedex_species_count(sav);
    struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS];

    assert(pkmn_can_breed(sav, 25) == !gen1); // Pikachu: only once the Day Care exists
    assert(!pkmn_can_breed(sav, 30));         // Nidorina
    assert(!pkmn_can_breed(sav, 31));         // Nidoqueen
    assert(!pkmn_can_breed(sav, 0));

    // Raichu <- Pikachu <- Pichu: only unowned, in-dex ancestors come back.
    uint8_t n = pkmn_needed_preevolutions(sav, 26, out);
    assert(n <= 2);
    for (uint8_t i = 0; i < n; i++)
    {
        assert(out[i].dex == 25 || out[i].dex == 172);
        assert(out[i].dex <= max_dex);
        assert(out[i].method == PKMN_EVO_BREED);
        assert(out[i].level == 0 && out[i].detail == NULL);
        bool seen = false, owned = false;
        pokedex_get_entry(sav, out[i].dex, &seen, &owned);
        assert(!owned);
    }
    if (gen1)
    {
        for (uint8_t i = 0; i < n; i++)
        {
            assert(out[i].dex != 172); // Pichu is outside the Gen 1 dex
        }
    }

    // Terminal/basic species have nothing to breed back to.
    assert(pkmn_needed_preevolutions(sav, 1, out) == 0);   // Bulbasaur
    assert(pkmn_needed_preevolutions(sav, 151, out) == 0); // Mew

    // Combined list == evolutions (+ pre-evolutions when breeding exists).
    for (uint16_t dex = 1; dex <= max_dex; dex++)
    {
        struct pkmn_needed_evo evo[PKMN_MAX_NEEDED_EVOS], all[PKMN_MAX_NEEDED_EVOS];
        uint8_t e = pkmn_needed_evolutions(sav, dex, evo);
        uint8_t a = pkmn_needed_dex_tasks(sav, dex, all);
        uint8_t p = pkmn_can_breed(sav, dex) ? pkmn_needed_preevolutions(sav, dex, out) : 0;
        uint8_t expect = e + p > PKMN_MAX_NEEDED_EVOS ? PKMN_MAX_NEEDED_EVOS : e + p;
        assert(a == expect);
        for (uint8_t i = 0; i < e; i++)
        {
            assert(all[i].dex == evo[i].dex && all[i].method == evo[i].method);
            assert(all[i].method != PKMN_EVO_BREED);
        }
        for (uint8_t i = e; i < a; i++)
        {
            assert(all[i].method == PKMN_EVO_BREED);
        }
    }
}

static int check_save(const char *path)
{
    FILE *probe = fopen(path, "rb");
    if (probe == NULL)
    {
        printf("SKIP %s (fixture absent)\n", path);
        return 0;
    }
    fclose(probe);

    PokemonSave sav;
    load_savefile_from_path(path, &sav);
    if (sav.save_generation_type == SAVE_GENERATION_NONE || sav.save_generation_type == SAVE_GENERATION_CORRUPTED)
    {
        printf("FAIL %s: could not load\n", path);
        return 1;
    }
    bills_pc_normalize_current_box(&sav);

    check_breeding_helpers(&sav);

    int cap = bills_pc_box_capacity(&sav);
    int last = bills_pc_num_boxes(&sav) - 1;
    int before = total_boxed(&sav);
    int party_before = bills_pc_container_count(&sav, BILLS_PC_LOC_PARTY, 0);

    int wanted = 0, placed = 0;
    int moved = bills_pc_fill_dex_box(&sav, last, &wanted, &placed);

    assert(total_boxed(&sav) == before);                                        // nothing lost or duplicated
    assert(bills_pc_container_count(&sav, BILLS_PC_LOC_PARTY, 0) == party_before); // party untouched
    assert(placed == (wanted < cap ? wanted : cap));

    // Everything now in the box has a job, and (given free space elsewhere,
    // which these fixtures have) nothing without a job is left in it.
    int in_box = 0;
    for (int i = 0; i < cap; i++)
    {
        if (!bills_pc_slot_occupied(&sav, BILLS_PC_LOC_BOX, last, i))
        {
            continue;
        }
        struct bills_pc_entry_view v;
        bills_pc_get_view(&sav, BILLS_PC_LOC_BOX, last, i, &v);
        struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS];
        assert(v.occupied && v.dex > 0);
        assert(pkmn_needed_dex_tasks(&sav, v.dex, out) > 0);
        in_box++;
    }
    if (before - placed <= (bills_pc_num_boxes(&sav) - 1) * cap)
    {
        assert(in_box == placed);
    }

    // Unless the box overflowed, no species outside it still has an unfilled job.
    if (wanted <= cap)
    {
        for (int b = 0; b < last; b++)
        {
            for (int i = 0; i < cap; i++)
            {
                if (!bills_pc_slot_occupied(&sav, BILLS_PC_LOC_BOX, b, i))
                {
                    continue;
                }
                struct bills_pc_entry_view v;
                bills_pc_get_view(&sav, BILLS_PC_LOC_BOX, b, i, &v);
                struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS];
                if (v.dex == 0 || pkmn_needed_dex_tasks(&sav, v.dex, out) == 0)
                {
                    continue;
                }
                // A copy of this species must already be in the to-do box.
                bool covered = false;
                for (int j = 0; j < cap && !covered; j++)
                {
                    if (!bills_pc_slot_occupied(&sav, BILLS_PC_LOC_BOX, last, j))
                    {
                        continue;
                    }
                    struct bills_pc_entry_view w;
                    bills_pc_get_view(&sav, BILLS_PC_LOC_BOX, last, j, &w);
                    covered = (w.dex == v.dex);
                }
                assert(covered);
            }
        }
    }

    // Idempotent: a second run has nothing to do.
    int wanted2 = 0, placed2 = 0;
    int moved2 = bills_pc_fill_dex_box(&sav, last, &wanted2, &placed2);
    assert(moved2 == 0 && wanted2 == wanted && placed2 == placed);

    printf("OK   %s: gen %d, box %d wanted=%d placed=%d moves=%d\n",
           path, sav.save_generation_type, last + 1, wanted, placed, moved);
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += check_save("saves/Pokemon - Blue Version.sav");
    failures += check_save("saves/Pokemon - Red LivingDex.sav");
    failures += check_save("saves/Pokemon - Crystal Version.sav");
    failures += check_save("saves/Pokemon - Gold Version.sav");
    failures += check_save("saves/refs/e_hof0.sav");
    failures += check_save("saves/gen4/platinum.sav");
    if (failures)
    {
        return 1;
    }
    printf("dex_box_test: all OK\n");
    return 0;
}

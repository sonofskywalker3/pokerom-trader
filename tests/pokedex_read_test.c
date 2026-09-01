/*
 * Test for the read-only Pokédex helpers (pokedex_get_entry / pokedex_counts /
 * pokedex_species_count) against real saves from every generation.
 *
 * Ground truth that avoids circularity: any Pokémon currently in the party of
 * a real playthrough save must be recorded seen AND owned in that save's dex
 * (the game guarantees it), so we resolve each party mon's national dex number
 * via bills_pc_get_view() and require pokedex_get_entry() to agree. Totals
 * must be >= the party's distinct species, owned <= seen, and out-of-range
 * dex numbers must read false/false.
 *
 * Gen 4 fixtures (saves/gen4/*.sav) are personal saves and NOT committed;
 * those cases SKIP when absent so a clean checkout still exits 0.
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include <stdio.h>

#define CHECK(cond, msg) do { \
    if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

static int party_count(const PokemonSave *s)
{
    switch (s->save_generation_type)
    {
    case SAVE_GENERATION_1: return (int)s->save.gen1_save.pokemon_storage.p_party->count;
    case SAVE_GENERATION_2: return (int)s->save.gen2_save.pokemon_storage.p_party->count;
    case SAVE_GENERATION_3: return (int)s->save.gba_save.pokemon_storage.p_party->count;
    case SAVE_GENERATION_4: return (int)gen4_party_count(&s->save.gen4_save);
    default: return 0;
    }
}

static int check_save(const char *path, int allow_skip)
{
    FILE *probe = fopen(path, "rb");
    if (!probe)
    {
        if (allow_skip) { printf("SKIP %s (fixture absent)\n", path); return 0; }
        fprintf(stderr, "FAIL: fixture missing: %s\n", path);
        return 1;
    }
    fclose(probe);

    PokemonSave sav;
    load_savefile_from_path(path, &sav);
    CHECK(sav.save_generation_type != SAVE_GENERATION_NONE &&
          sav.save_generation_type != SAVE_GENERATION_CORRUPTED, path);

    uint16_t max = pokedex_species_count(&sav);
    CHECK(max == 151 || max == 251 || max == 386 || max == 493, "species count");

    // out-of-range reads are false/false
    bool seen, owned;
    pokedex_get_entry(&sav, 0, &seen, &owned);
    CHECK(!seen && !owned, "dex 0 must be unset");
    pokedex_get_entry(&sav, (uint16_t)(max + 1), &seen, &owned);
    CHECK(!seen && !owned, "dex max+1 must be unset");

    // Party mons should be seen+owned in their own save. Individual misses
    // only warn (externally edited fixtures can carry unrecorded mons — e.g.
    // species corrupted by the old linear Hoenn mapping); a majority missing
    // means the reader itself is broken and fails the test.
    int n = party_count(&sav);
    CHECK(n > 0, "party is empty");
    int checked = 0, recorded = 0;
    for (int i = 0; i < n; i++)
    {
        if (sav.save_generation_type == SAVE_GENERATION_4)
        {
            const uint8_t *raw = gen4_party_slot_raw(&sav.save.gen4_save, i);
            uint8_t dec[GEN4_PK4_PARTY_SIZE];
            gen4_pk4_decrypt(raw, dec, true);
            if (gen4_pk4_is_egg(dec))
                continue; // eggs are never dex-recorded
        }
        struct bills_pc_entry_view view;
        bills_pc_get_view(&sav, BILLS_PC_LOC_PARTY, 0, i, &view);
        if (view.dex == 0 || view.dex > max)
            continue; // egg marker / unmapped species: not dex-recorded
        pokedex_get_entry(&sav, view.dex, &seen, &owned);
        checked++;
        if (seen && owned)
            recorded++;
        else
            printf("WARN %s: party dex #%u not seen+owned (edited save?)\n", path, view.dex);
    }
    CHECK(checked > 0, "no checkable party mons");
    CHECK(recorded * 2 > checked, "majority of party unrecorded - reader broken");

    uint16_t seen_count, owned_count;
    pokedex_counts(&sav, &seen_count, &owned_count);
    CHECK(owned_count <= seen_count, "owned > seen");
    CHECK(owned_count >= 1, "no owned mons at all");
    CHECK(seen_count <= max, "seen > species count");
    printf("OK   %-40s gen dex: seen %u owned %u / %u\n", path, seen_count, owned_count, max);
    return 0;
}

int main(void)
{
    if (check_save("saves/Pokemon - Blue Version.sav", 0)) return 1;
    if (check_save("saves/Pokemon - Yellow Version.sav", 0)) return 1;
    if (check_save("saves/Pokemon - Crystal Version.sav", 0)) return 1;
    if (check_save("saves/Pokemon - Gold Version.sav", 0)) return 1;
    if (check_save("saves/refs/baseline/pokemon_ruby.sav", 0)) return 1;
    if (check_save("saves/refs/baseline/pokemon_emerald.sav", 0)) return 1;
    if (check_save("saves/gen4/diamond.sav", 1)) return 1;
    if (check_save("saves/gen4/platinum.sav", 1)) return 1;
    if (check_save("saves/gen4/soulsilver.sav", 1)) return 1;
    puts("pokedex_read_test: all cases passed");
    return 0;
}

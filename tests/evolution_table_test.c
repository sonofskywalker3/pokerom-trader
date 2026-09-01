// Sanity checks for the National Dex evolution-edge table used by the
// Bill's PC "evolve for Pokedex" badges.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pkmn_evolutions.h"

int main(void)
{
    uint16_t n = pkmn_evo_edge_count();
    assert(n > 200 && n < 300);

    uint16_t prev_from = 0;
    for (uint16_t i = 0; i < n; i++)
    {
        const struct pkmn_evo_edge *e = pkmn_evo_edge_at(i);
        assert(e != NULL);
        assert(e->from >= 1 && e->from <= 493);
        assert(e->to >= 1 && e->to <= 493);
        assert(e->from != e->to);
        assert(e->from >= prev_from); // sorted so lookups can early-out
        prev_from = e->from;
        switch (e->method)
        {
        case PKMN_EVO_LEVEL:
            assert(e->level >= 7 && e->level <= 55);
            assert(e->detail == NULL);
            break;
        case PKMN_EVO_STONE:
            assert(e->detail != NULL && strstr(e->detail, "Stone") != NULL);
            assert(e->level == 0);
            break;
        case PKMN_EVO_TRADE:
            assert(e->level == 0); // detail (held item) optional
            break;
        case PKMN_EVO_SPECIAL:
            assert(e->detail != NULL && e->detail[0] != '\0');
            assert(e->level == 0);
            break;
        default:
            assert(!"unknown method");
        }
    }
    assert(pkmn_evo_edge_at(n) == NULL);

    const struct pkmn_evo_edge *evos[PKMN_MAX_DIRECT_EVOS];

    // Pidgey line: two level-up steps.
    assert(pkmn_direct_evolutions(16, evos) == 1);
    assert(evos[0]->to == 17 && evos[0]->method == PKMN_EVO_LEVEL && evos[0]->level == 18);
    assert(pkmn_direct_evolutions(17, evos) == 1);
    assert(evos[0]->to == 18 && evos[0]->method == PKMN_EVO_LEVEL && evos[0]->level == 36);
    assert(pkmn_direct_evolutions(18, evos) == 0); // Pidgeot is terminal

    // Eevee: 3 stones + 4 specials in Gen 4.
    assert(pkmn_direct_evolutions(133, evos) == 7);

    // Tyrogue: three stat-dependent specials.
    assert(pkmn_direct_evolutions(236, evos) == 3);
    for (int i = 0; i < 3; i++)
    {
        assert(evos[i]->method == PKMN_EVO_SPECIAL);
    }

    // Poliwhirl: Water Stone or trade holding King's Rock.
    assert(pkmn_direct_evolutions(61, evos) == 2);
    assert(evos[0]->to == 62 && evos[0]->method == PKMN_EVO_STONE);
    assert(evos[1]->to == 186 && evos[1]->method == PKMN_EVO_TRADE && strcmp(evos[1]->detail, "King's Rock") == 0);

    // Plain trade evolutions carry no item.
    assert(pkmn_direct_evolutions(64, evos) == 1);
    assert(evos[0]->to == 65 && evos[0]->method == PKMN_EVO_TRADE && evos[0]->detail == NULL);

    // Legendaries / terminals have no edges.
    assert(pkmn_direct_evolutions(151, evos) == 0);
    assert(pkmn_direct_evolutions(493, evos) == 0);

    // Baby edge points forward into an existing line.
    assert(pkmn_direct_evolutions(172, evos) == 1);
    assert(evos[0]->to == 25);

    printf("evolution_table_test: %u edges OK\n", n);
    return 0;
}

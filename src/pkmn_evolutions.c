#include "pkmn_evolutions.h"

// Every evolution edge for National Dex #1-493, sorted by `from`.
// Methods are the ones used in Gens 1-4; for edges that exist in more than
// one of those generations the method is the same in all of them, and edges
// whose target is outside a save's dex range are filtered at query time.

#define LV(f, t, lvl) {f, t, PKMN_EVO_LEVEL, lvl, NULL}
#define ST(f, t, stone) {f, t, PKMN_EVO_STONE, 0, stone}
#define TR(f, t) {f, t, PKMN_EVO_TRADE, 0, NULL}
#define TRI(f, t, item) {f, t, PKMN_EVO_TRADE, 0, item}
#define SP(f, t, how) {f, t, PKMN_EVO_SPECIAL, 0, how}

static const struct pkmn_evo_edge evo_edges[] = {
    // --- Kanto (#1-151) ---
    LV(1, 2, 16), LV(2, 3, 32),
    LV(4, 5, 16), LV(5, 6, 36),
    LV(7, 8, 16), LV(8, 9, 36),
    LV(10, 11, 7), LV(11, 12, 10),
    LV(13, 14, 7), LV(14, 15, 10),
    LV(16, 17, 18), LV(17, 18, 36),
    LV(19, 20, 20),
    LV(21, 22, 20),
    LV(23, 24, 22),
    ST(25, 26, "Thunder Stone"),
    LV(27, 28, 22),
    LV(29, 30, 16), ST(30, 31, "Moon Stone"),
    LV(32, 33, 16), ST(33, 34, "Moon Stone"),
    ST(35, 36, "Moon Stone"),
    ST(37, 38, "Fire Stone"),
    ST(39, 40, "Moon Stone"),
    LV(41, 42, 22), SP(42, 169, "High friendship"),
    LV(43, 44, 21), ST(44, 45, "Leaf Stone"), ST(44, 182, "Sun Stone"),
    LV(46, 47, 24),
    LV(48, 49, 31),
    LV(50, 51, 26),
    LV(52, 53, 28),
    LV(54, 55, 33),
    LV(56, 57, 28),
    ST(58, 59, "Fire Stone"),
    LV(60, 61, 25), ST(61, 62, "Water Stone"), TRI(61, 186, "King's Rock"),
    LV(63, 64, 16), TR(64, 65),
    LV(66, 67, 28), TR(67, 68),
    LV(69, 70, 21), ST(70, 71, "Leaf Stone"),
    LV(72, 73, 30),
    LV(74, 75, 25), TR(75, 76),
    LV(77, 78, 40),
    LV(79, 80, 37), TRI(79, 199, "King's Rock"),
    LV(81, 82, 30), SP(82, 462, "Level up in Mt. Coronet's magnetic field"),
    LV(84, 85, 31),
    LV(86, 87, 34),
    LV(88, 89, 38),
    ST(90, 91, "Water Stone"),
    LV(92, 93, 25), TR(93, 94),
    TRI(95, 208, "Metal Coat"),
    LV(96, 97, 26),
    LV(98, 99, 28),
    LV(100, 101, 30),
    ST(102, 103, "Leaf Stone"),
    LV(104, 105, 28),
    SP(108, 463, "Level up knowing Rollout"),
    LV(109, 110, 35),
    LV(111, 112, 42), TRI(112, 464, "Protector"),
    SP(113, 242, "High friendship"),
    SP(114, 465, "Level up knowing Ancient Power"),
    LV(116, 117, 32), TRI(117, 230, "Dragon Scale"),
    LV(118, 119, 33),
    ST(120, 121, "Water Stone"),
    TRI(123, 212, "Metal Coat"),
    TRI(125, 466, "Electirizer"),
    TRI(126, 467, "Magmarizer"),
    LV(129, 130, 20),
    ST(133, 134, "Water Stone"), ST(133, 135, "Thunder Stone"), ST(133, 136, "Fire Stone"),
    SP(133, 196, "High friendship, daytime"), SP(133, 197, "High friendship, nighttime"),
    SP(133, 470, "Level up near Moss Rock (Eterna Forest)"), SP(133, 471, "Level up near Ice Rock (Route 217)"),
    TRI(137, 233, "Up-Grade"),
    LV(138, 139, 40),
    LV(140, 141, 40),
    LV(147, 148, 30), LV(148, 149, 55),
    // --- Johto (#152-251) ---
    LV(152, 153, 16), LV(153, 154, 32),
    LV(155, 156, 14), LV(156, 157, 36),
    LV(158, 159, 18), LV(159, 160, 30),
    LV(161, 162, 15),
    LV(163, 164, 20),
    LV(165, 166, 18),
    LV(167, 168, 22),
    LV(170, 171, 27),
    SP(172, 25, "High friendship"),
    SP(173, 35, "High friendship"),
    SP(174, 39, "High friendship"),
    SP(175, 176, "High friendship"), ST(176, 468, "Shiny Stone"),
    LV(177, 178, 25),
    LV(179, 180, 15), LV(180, 181, 30),
    LV(183, 184, 18),
    LV(187, 188, 18), LV(188, 189, 27),
    SP(190, 424, "Level up knowing Double Hit"),
    ST(191, 192, "Sun Stone"),
    SP(193, 469, "Level up knowing Ancient Power"),
    LV(194, 195, 20),
    ST(198, 430, "Dusk Stone"),
    ST(200, 429, "Dusk Stone"),
    LV(204, 205, 31),
    SP(207, 472, "Level up at night holding Razor Fang"),
    LV(209, 210, 23),
    SP(215, 461, "Level up at night holding Razor Claw"),
    LV(216, 217, 30),
    LV(218, 219, 38),
    LV(220, 221, 33), SP(221, 473, "Level up knowing Ancient Power"),
    LV(223, 224, 25),
    LV(228, 229, 24),
    LV(231, 232, 25),
    TRI(233, 474, "Dubious Disc"),
    SP(236, 106, "Lv20 with Attack > Defense"),
    SP(236, 107, "Lv20 with Defense > Attack"),
    SP(236, 237, "Lv20 with Attack = Defense"),
    LV(238, 124, 30),
    LV(239, 125, 30),
    LV(240, 126, 30),
    LV(246, 247, 30), LV(247, 248, 55),
    // --- Hoenn (#252-386) ---
    LV(252, 253, 16), LV(253, 254, 36),
    LV(255, 256, 16), LV(256, 257, 36),
    LV(258, 259, 16), LV(259, 260, 36),
    LV(261, 262, 18),
    LV(263, 264, 20),
    SP(265, 266, "Lv7 (random by personality)"), SP(265, 268, "Lv7 (random by personality)"),
    LV(266, 267, 10), LV(268, 269, 10),
    LV(270, 271, 14), ST(271, 272, "Water Stone"),
    LV(273, 274, 14), ST(274, 275, "Leaf Stone"),
    LV(276, 277, 22),
    LV(278, 279, 25),
    LV(280, 281, 20), LV(281, 282, 30), ST(281, 475, "Dawn Stone (male only)"),
    LV(283, 284, 22),
    LV(285, 286, 23),
    LV(287, 288, 18), LV(288, 289, 36),
    LV(290, 291, 20), SP(290, 292, "Lv20 with an empty party slot"),
    LV(293, 294, 20), LV(294, 295, 40),
    LV(296, 297, 24),
    SP(298, 183, "High friendship"),
    SP(299, 476, "Level up in Mt. Coronet's magnetic field"),
    ST(300, 301, "Moon Stone"),
    LV(304, 305, 32), LV(305, 306, 42),
    LV(307, 308, 37),
    LV(309, 310, 26),
    ST(315, 407, "Shiny Stone"),
    LV(316, 317, 26),
    LV(318, 319, 30),
    LV(320, 321, 40),
    LV(322, 323, 33),
    LV(325, 326, 32),
    LV(328, 329, 35), LV(329, 330, 45),
    LV(331, 332, 32),
    LV(333, 334, 35),
    LV(339, 340, 30),
    LV(341, 342, 30),
    LV(343, 344, 36),
    LV(345, 346, 40),
    LV(347, 348, 40),
    SP(349, 350, "Level up with max Beauty"),
    LV(353, 354, 37),
    LV(355, 356, 37), TRI(356, 477, "Reaper Cloth"),
    LV(360, 202, 15),
    LV(361, 362, 42), ST(361, 478, "Dawn Stone (female only)"),
    LV(363, 364, 32), LV(364, 365, 44),
    TRI(366, 367, "Deep Sea Tooth"), TRI(366, 368, "Deep Sea Scale"),
    LV(371, 372, 30), LV(372, 373, 50),
    LV(374, 375, 20), LV(375, 376, 45),
    // --- Sinnoh (#387-493) ---
    LV(387, 388, 18), LV(388, 389, 32),
    LV(390, 391, 14), LV(391, 392, 36),
    LV(393, 394, 16), LV(394, 395, 36),
    LV(396, 397, 14), LV(397, 398, 34),
    LV(399, 400, 15),
    LV(401, 402, 10),
    LV(403, 404, 15), LV(404, 405, 30),
    SP(406, 315, "High friendship, daytime"),
    LV(408, 409, 30),
    LV(410, 411, 30),
    SP(412, 413, "Lv20 (female Burmy)"), SP(412, 414, "Lv20 (male Burmy)"),
    SP(415, 416, "Lv21 (female only)"),
    LV(418, 419, 26),
    LV(420, 421, 25),
    LV(422, 423, 30),
    LV(425, 426, 28),
    SP(427, 428, "High friendship"),
    LV(431, 432, 38),
    SP(433, 358, "High friendship, nighttime"),
    LV(434, 435, 34),
    LV(436, 437, 33),
    SP(438, 185, "Level up knowing Mimic"),
    SP(439, 122, "Level up knowing Mimic"),
    SP(440, 113, "Level up holding Oval Stone, daytime"),
    LV(443, 444, 24), LV(444, 445, 48),
    SP(446, 143, "High friendship"),
    SP(447, 448, "High friendship, daytime"),
    LV(449, 450, 34),
    LV(451, 452, 40),
    LV(453, 454, 37),
    LV(456, 457, 31),
    SP(458, 226, "Level up with Remoraid in party"),
    LV(459, 460, 40),
};

#define EVO_EDGE_COUNT (sizeof(evo_edges) / sizeof(evo_edges[0]))

uint16_t pkmn_evo_edge_count(void)
{
    return (uint16_t)EVO_EDGE_COUNT;
}

const struct pkmn_evo_edge *pkmn_evo_edge_at(uint16_t index)
{
    if (index >= EVO_EDGE_COUNT)
    {
        return NULL;
    }
    return &evo_edges[index];
}

uint8_t pkmn_direct_evolutions(uint16_t national_dex, const struct pkmn_evo_edge *out[PKMN_MAX_DIRECT_EVOS])
{
    uint8_t count = 0;
    for (uint16_t i = 0; i < EVO_EDGE_COUNT && count < PKMN_MAX_DIRECT_EVOS; i++)
    {
        if (evo_edges[i].from == national_dex)
        {
            out[count++] = &evo_edges[i];
        }
        else if (evo_edges[i].from > national_dex)
        {
            break; // table is sorted by `from`
        }
    }
    return count;
}

// Walk every evolution reachable from `dex` and collect the unowned ones.
static void collect_needed(const PokemonSave *pkmn_save, uint16_t dex, uint16_t max_dex,
                           struct pkmn_needed_evo *out, uint8_t *count, int depth)
{
    if (depth > 3) // longest chain is 2 evolution steps; guard regardless
    {
        return;
    }
    const struct pkmn_evo_edge *evos[PKMN_MAX_DIRECT_EVOS];
    uint8_t n = pkmn_direct_evolutions(dex, evos);
    for (uint8_t i = 0; i < n; i++)
    {
        uint16_t to = evos[i]->to;
        if (to == 0 || to > max_dex)
        {
            continue; // evolution doesn't exist in this save's generation
        }
        bool seen = false, owned = false;
        pokedex_get_entry(pkmn_save, to, &seen, &owned);
        if (!owned)
        {
            bool already = false;
            for (uint8_t j = 0; j < *count; j++)
            {
                if (out[j].dex == to)
                {
                    already = true;
                    break;
                }
            }
            if (!already && *count < PKMN_MAX_NEEDED_EVOS)
            {
                out[*count].dex = to;
                out[*count].method = evos[i]->method;
                out[*count].level = evos[i]->level;
                out[*count].detail = evos[i]->detail;
                (*count)++;
            }
        }
        collect_needed(pkmn_save, to, max_dex, out, count, depth + 1);
    }
}

uint8_t pkmn_needed_evolutions(const PokemonSave *pkmn_save, uint16_t national_dex,
                               struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS])
{
    if (pkmn_save == NULL || national_dex == 0)
    {
        return 0;
    }
    uint8_t count = 0;
    collect_needed(pkmn_save, national_dex, pokedex_species_count(pkmn_save), out, &count, 0);
    return count;
}

// -------------------- Breeding (Gen 2+) --------------------

// Babies that only hatch while a parent holds an incense (Gen 3/4).
static const struct
{
    uint16_t dex;
    const char *incense;
} breed_incense[] = {
    {298, "Sea Incense"},  // Azurill
    {360, "Lax Incense"},  // Wynaut
    {406, "Rose Incense"}, // Budew
    {433, "Pure Incense"}, // Chingling
    {438, "Rock Incense"}, // Bonsly
    {439, "Odd Incense"},  // Mime Jr.
    {440, "Luck Incense"}, // Happiny
    {446, "Full Incense"}, // Munchlax
    {458, "Wave Incense"}, // Mantyke
};

static const char *incense_for(uint16_t dex)
{
    for (size_t i = 0; i < sizeof(breed_incense) / sizeof(breed_incense[0]); i++)
    {
        if (breed_incense[i].dex == dex)
        {
            return breed_incense[i].incense;
        }
    }
    return NULL;
}

bool pkmn_can_breed(const PokemonSave *pkmn_save, uint16_t national_dex)
{
    if (pkmn_save == NULL || national_dex == 0 || pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        return false; // Gen 1 has no Day Care breeding
    }
    // Nidorina and Nidoqueen sit in the Undiscovered egg group despite evolving from Nidoran-F.
    return national_dex != 30 && national_dex != 31;
}

static void collect_preevos(const PokemonSave *pkmn_save, uint16_t dex, uint16_t max_dex,
                            struct pkmn_needed_evo *out, uint8_t *count, int depth)
{
    if (depth > 3)
    {
        return;
    }
    uint16_t n = pkmn_evo_edge_count();
    for (uint16_t i = 0; i < n; i++)
    {
        const struct pkmn_evo_edge *e = pkmn_evo_edge_at(i);
        if (e->to != dex || e->from > max_dex)
        {
            continue;
        }
        bool seen = false, owned = false;
        pokedex_get_entry(pkmn_save, e->from, &seen, &owned);
        if (!owned && *count < PKMN_MAX_NEEDED_EVOS)
        {
            out[*count].dex = e->from;
            out[*count].method = PKMN_EVO_BREED;
            out[*count].level = 0;
            out[*count].detail = incense_for(e->from);
            (*count)++;
        }
        collect_preevos(pkmn_save, e->from, max_dex, out, count, depth + 1);
    }
}

uint8_t pkmn_needed_preevolutions(const PokemonSave *pkmn_save, uint16_t national_dex,
                                  struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS])
{
    if (pkmn_save == NULL || national_dex == 0)
    {
        return 0;
    }
    uint8_t count = 0;
    collect_preevos(pkmn_save, national_dex, pokedex_species_count(pkmn_save), out, &count, 0);
    return count;
}

uint8_t pkmn_needed_dex_tasks(const PokemonSave *pkmn_save, uint16_t national_dex,
                              struct pkmn_needed_evo out[PKMN_MAX_NEEDED_EVOS])
{
    uint8_t count = pkmn_needed_evolutions(pkmn_save, national_dex, out);
    if (pkmn_can_breed(pkmn_save, national_dex) && count < PKMN_MAX_NEEDED_EVOS)
    {
        struct pkmn_needed_evo pre[PKMN_MAX_NEEDED_EVOS];
        uint8_t pre_n = pkmn_needed_preevolutions(pkmn_save, national_dex, pre);
        for (uint8_t i = 0; i < pre_n && count < PKMN_MAX_NEEDED_EVOS; i++)
        {
            out[count++] = pre[i];
        }
    }
    return count;
}

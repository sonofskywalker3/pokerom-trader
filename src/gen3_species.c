/*
 * Gen 3 species-index -> name lookup (fork addition for the Gen 3 trade module).
 * Distributed under the MIT License (MIT).
 *
 * See gen3_species.h for the internal-index -> National Dex mapping rationale.
 * Names are English National Dex names #1..386. Nidoran genders are rendered
 * "Nidoran-M"/"Nidoran-F" for ASCII terminals.
 */
#include "gen3_species.h"

static const char *const NATIONAL_DEX_NAMES[GEN3_NATIONAL_DEX_MAX + 1] = {
    /*   0 */ "?",
    /*   1 */ "Bulbasaur", "Ivysaur", "Venusaur", "Charmander", "Charmeleon",
    /*   6 */ "Charizard", "Squirtle", "Wartortle", "Blastoise", "Caterpie",
    /*  11 */ "Metapod", "Butterfree", "Weedle", "Kakuna", "Beedrill",
    /*  16 */ "Pidgey", "Pidgeotto", "Pidgeot", "Rattata", "Raticate",
    /*  21 */ "Spearow", "Fearow", "Ekans", "Arbok", "Pikachu",
    /*  26 */ "Raichu", "Sandshrew", "Sandslash", "Nidoran-F", "Nidorina",
    /*  31 */ "Nidoqueen", "Nidoran-M", "Nidorino", "Nidoking", "Clefairy",
    /*  36 */ "Clefable", "Vulpix", "Ninetales", "Jigglypuff", "Wigglytuff",
    /*  41 */ "Zubat", "Golbat", "Oddish", "Gloom", "Vileplume",
    /*  46 */ "Paras", "Parasect", "Venonat", "Venomoth", "Diglett",
    /*  51 */ "Dugtrio", "Meowth", "Persian", "Psyduck", "Golduck",
    /*  56 */ "Mankey", "Primeape", "Growlithe", "Arcanine", "Poliwag",
    /*  61 */ "Poliwhirl", "Poliwrath", "Abra", "Kadabra", "Alakazam",
    /*  66 */ "Machop", "Machoke", "Machamp", "Bellsprout", "Weepinbell",
    /*  71 */ "Victreebel", "Tentacool", "Tentacruel", "Geodude", "Graveler",
    /*  76 */ "Golem", "Ponyta", "Rapidash", "Slowpoke", "Slowbro",
    /*  81 */ "Magnemite", "Magneton", "Farfetch'd", "Doduo", "Dodrio",
    /*  86 */ "Seel", "Dewgong", "Grimer", "Muk", "Shellder",
    /*  91 */ "Cloyster", "Gastly", "Haunter", "Gengar", "Onix",
    /*  96 */ "Drowzee", "Hypno", "Krabby", "Kingler", "Voltorb",
    /* 101 */ "Electrode", "Exeggcute", "Exeggutor", "Cubone", "Marowak",
    /* 106 */ "Hitmonlee", "Hitmonchan", "Lickitung", "Koffing", "Weezing",
    /* 111 */ "Rhyhorn", "Rhydon", "Chansey", "Tangela", "Kangaskhan",
    /* 116 */ "Horsea", "Seadra", "Goldeen", "Seaking", "Staryu",
    /* 121 */ "Starmie", "Mr. Mime", "Scyther", "Jynx", "Electabuzz",
    /* 126 */ "Magmar", "Pinsir", "Tauros", "Magikarp", "Gyarados",
    /* 131 */ "Lapras", "Ditto", "Eevee", "Vaporeon", "Jolteon",
    /* 136 */ "Flareon", "Porygon", "Omanyte", "Omastar", "Kabuto",
    /* 141 */ "Kabutops", "Aerodactyl", "Snorlax", "Articuno", "Zapdos",
    /* 146 */ "Moltres", "Dratini", "Dragonair", "Dragonite", "Mewtwo",
    /* 151 */ "Mew", "Chikorita", "Bayleef", "Meganium", "Cyndaquil",
    /* 156 */ "Quilava", "Typhlosion", "Totodile", "Croconaw", "Feraligatr",
    /* 161 */ "Sentret", "Furret", "Hoothoot", "Noctowl", "Ledyba",
    /* 166 */ "Ledian", "Spinarak", "Ariados", "Crobat", "Chinchou",
    /* 171 */ "Lanturn", "Pichu", "Cleffa", "Igglybuff", "Togepi",
    /* 176 */ "Togetic", "Natu", "Xatu", "Mareep", "Flaaffy",
    /* 181 */ "Ampharos", "Bellossom", "Marill", "Azumarill", "Sudowoodo",
    /* 186 */ "Politoed", "Hoppip", "Skiploom", "Jumpluff", "Aipom",
    /* 191 */ "Sunkern", "Sunflora", "Yanma", "Wooper", "Quagsire",
    /* 196 */ "Espeon", "Umbreon", "Murkrow", "Slowking", "Misdreavus",
    /* 201 */ "Unown", "Wobbuffet", "Girafarig", "Pineco", "Forretress",
    /* 206 */ "Dunsparce", "Gligar", "Steelix", "Snubbull", "Granbull",
    /* 211 */ "Qwilfish", "Scizor", "Shuckle", "Heracross", "Sneasel",
    /* 216 */ "Teddiursa", "Ursaring", "Slugma", "Magcargo", "Swinub",
    /* 221 */ "Piloswine", "Corsola", "Remoraid", "Octillery", "Delibird",
    /* 226 */ "Mantine", "Skarmory", "Houndour", "Houndoom", "Kingdra",
    /* 231 */ "Phanpy", "Donphan", "Porygon2", "Stantler", "Smeargle",
    /* 236 */ "Tyrogue", "Hitmontop", "Smoochum", "Elekid", "Magby",
    /* 241 */ "Miltank", "Blissey", "Raikou", "Entei", "Suicune",
    /* 246 */ "Larvitar", "Pupitar", "Tyranitar", "Lugia", "Ho-Oh",
    /* 251 */ "Celebi", "Treecko", "Grovyle", "Sceptile", "Torchic",
    /* 256 */ "Combusken", "Blaziken", "Mudkip", "Marshtomp", "Swampert",
    /* 261 */ "Poochyena", "Mightyena", "Zigzagoon", "Linoone", "Wurmple",
    /* 266 */ "Silcoon", "Beautifly", "Cascoon", "Dustox", "Lotad",
    /* 271 */ "Lombre", "Ludicolo", "Seedot", "Nuzleaf", "Shiftry",
    /* 276 */ "Taillow", "Swellow", "Wingull", "Pelipper", "Ralts",
    /* 281 */ "Kirlia", "Gardevoir", "Surskit", "Masquerain", "Shroomish",
    /* 286 */ "Breloom", "Slakoth", "Vigoroth", "Slaking", "Nincada",
    /* 291 */ "Ninjask", "Shedinja", "Whismur", "Loudred", "Exploud",
    /* 296 */ "Makuhita", "Hariyama", "Azurill", "Nosepass", "Skitty",
    /* 301 */ "Delcatty", "Sableye", "Mawile", "Aron", "Lairon",
    /* 306 */ "Aggron", "Meditite", "Medicham", "Electrike", "Manectric",
    /* 311 */ "Plusle", "Minun", "Volbeat", "Illumise", "Roselia",
    /* 316 */ "Gulpin", "Swalot", "Carvanha", "Sharpedo", "Wailmer",
    /* 321 */ "Wailord", "Numel", "Camerupt", "Torkoal", "Spoink",
    /* 326 */ "Grumpig", "Spinda", "Trapinch", "Vibrava", "Flygon",
    /* 331 */ "Cacnea", "Cacturne", "Swablu", "Altaria", "Zangoose",
    /* 336 */ "Seviper", "Lunatone", "Solrock", "Barboach", "Whiscash",
    /* 341 */ "Corphish", "Crawdaunt", "Baltoy", "Claydol", "Lileep",
    /* 346 */ "Cradily", "Anorith", "Armaldo", "Feebas", "Milotic",
    /* 351 */ "Castform", "Kecleon", "Shuppet", "Banette", "Duskull",
    /* 356 */ "Dusclops", "Tropius", "Chimecho", "Absol", "Wynaut",
    /* 361 */ "Snorunt", "Glalie", "Spheal", "Sealeo", "Walrein",
    /* 366 */ "Clamperl", "Huntail", "Gorebyss", "Relicanth", "Luvdisc",
    /* 371 */ "Bagon", "Shelgon", "Salamence", "Beldum", "Metang",
    /* 376 */ "Metagross", "Regirock", "Regice", "Registeel", "Latias",
    /* 381 */ "Latios", "Kyogre", "Groudon", "Rayquaza", "Jirachi",
    /* 386 */ "Deoxys"
};

uint16_t gen3_internal_to_national(uint16_t internal_index)
{
    if (internal_index == 0) return 0;
    if (internal_index <= 251) return internal_index;         /* identity */
    if (internal_index >= 277 && internal_index <= 411)
        return (uint16_t)(internal_index - 25);               /* Hoenn block */
    return 0;                                                 /* unused/glitch */
}

uint16_t gen3_national_to_internal(uint16_t national_dex)
{
    if (national_dex < 1 || national_dex > GEN3_NATIONAL_DEX_MAX) return 0;
    if (national_dex <= 251) return national_dex;             /* identity */
    return (uint16_t)(national_dex + 25);                     /* Hoenn block */
}

const char *gen3_national_dex_name(uint16_t national_dex)
{
    if (national_dex < 1 || national_dex > GEN3_NATIONAL_DEX_MAX) return "?";
    return NATIONAL_DEX_NAMES[national_dex];
}

const char *gen3_species_name_from_internal(uint16_t internal_index)
{
    if (internal_index == 0) return "(empty)";
    uint16_t nat = gen3_internal_to_national(internal_index);
    if (nat == 0) return "?";
    return NATIONAL_DEX_NAMES[nat];
}

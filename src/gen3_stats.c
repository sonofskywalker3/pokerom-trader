/*
 * Gen 3 (GBA) party-stat recomputation. See gen3_stats.h.
 * Distributed under the MIT License (MIT).
 */
#include "gen3_stats.h"
#include "gen3_species.h"
#include "pksavhelper.h" // struct pkmn_base_stats, pkmn_base_stats_gen2
#include <pksav.h>
#include <string.h>

// Growth-rate group codes.
enum
{
    GR_MEDIUM_FAST = 0,
    GR_ERRATIC = 1,
    GR_FLUCTUATING = 2,
    GR_MEDIUM_SLOW = 3,
    GR_FAST = 4,
    GR_SLOW = 5
};

/* ===================================================================
 * DATA TABLES
 * gen3_growth_rate[dex]        : growth-rate group per National Dex #1..386
 * gen3_hoenn_base_stats[dex]   : base stats for Hoenn #252..386
 * (#1..251 base stats are reused from pkmn_base_stats_gen2.)
 * These are placeholders pending verified data; replace in place.
 * =================================================================== */
// Growth-rate group per National Dex #1..386. Codes match the GR_* enum
// (0=Medium Fast, 1=Erratic, 2=Fluctuating, 3=Medium Slow, 4=Fast, 5=Slow).
// Verified against Bulbapedia/Serebii.
static const uint8_t gen3_growth_rate[387] = {
    [0]=0,
    /* 1-8    */ [1]=3,[2]=3,[3]=3,[4]=3,[5]=3,[6]=3,[7]=3,[8]=3,
    /* 9-16   */ [9]=3,[10]=0,[11]=0,[12]=0,[13]=0,[14]=0,[15]=0,[16]=3,
    /* 17-24  */ [17]=3,[18]=3,[19]=0,[20]=0,[21]=0,[22]=0,[23]=0,[24]=0,
    /* 25-32  */ [25]=0,[26]=0,[27]=0,[28]=0,[29]=3,[30]=3,[31]=3,[32]=3,
    /* 33-40  */ [33]=3,[34]=3,[35]=4,[36]=4,[37]=0,[38]=0,[39]=4,[40]=4,
    /* 41-48  */ [41]=0,[42]=0,[43]=3,[44]=3,[45]=3,[46]=0,[47]=0,[48]=0,
    /* 49-56  */ [49]=0,[50]=0,[51]=0,[52]=0,[53]=0,[54]=0,[55]=0,[56]=0,
    /* 57-64  */ [57]=0,[58]=5,[59]=5,[60]=3,[61]=3,[62]=3,[63]=3,[64]=3,
    /* 65-72  */ [65]=3,[66]=3,[67]=3,[68]=3,[69]=3,[70]=3,[71]=3,[72]=5,
    /* 73-80  */ [73]=5,[74]=3,[75]=3,[76]=3,[77]=0,[78]=0,[79]=0,[80]=0,
    /* 81-88  */ [81]=0,[82]=0,[83]=0,[84]=0,[85]=0,[86]=0,[87]=0,[88]=0,
    /* 89-96  */ [89]=0,[90]=5,[91]=5,[92]=3,[93]=3,[94]=3,[95]=0,[96]=0,
    /* 97-104 */ [97]=0,[98]=0,[99]=0,[100]=0,[101]=0,[102]=5,[103]=5,[104]=0,
    /* 105-112*/ [105]=0,[106]=0,[107]=0,[108]=0,[109]=0,[110]=0,[111]=5,[112]=5,
    /* 113-120*/ [113]=4,[114]=0,[115]=0,[116]=0,[117]=0,[118]=0,[119]=0,[120]=5,
    /* 121-128*/ [121]=5,[122]=0,[123]=0,[124]=0,[125]=0,[126]=0,[127]=5,[128]=5,
    /* 129-136*/ [129]=5,[130]=5,[131]=5,[132]=0,[133]=0,[134]=0,[135]=0,[136]=0,
    /* 137-144*/ [137]=0,[138]=0,[139]=0,[140]=0,[141]=0,[142]=5,[143]=5,[144]=5,
    /* 145-152*/ [145]=5,[146]=5,[147]=5,[148]=5,[149]=5,[150]=5,[151]=3,[152]=3,
    /* 153-160*/ [153]=3,[154]=3,[155]=3,[156]=3,[157]=3,[158]=3,[159]=3,[160]=3,
    /* 161-168*/ [161]=0,[162]=0,[163]=0,[164]=0,[165]=4,[166]=4,[167]=4,[168]=4,
    /* 169-176*/ [169]=0,[170]=5,[171]=5,[172]=0,[173]=4,[174]=4,[175]=4,[176]=4,
    /* 177-184*/ [177]=0,[178]=0,[179]=3,[180]=3,[181]=3,[182]=3,[183]=4,[184]=4,
    /* 185-192*/ [185]=0,[186]=3,[187]=3,[188]=3,[189]=3,[190]=0,[191]=3,[192]=3,
    /* 193-200*/ [193]=0,[194]=0,[195]=0,[196]=0,[197]=0,[198]=0,[199]=0,[200]=4,
    /* 201-208*/ [201]=0,[202]=0,[203]=0,[204]=0,[205]=0,[206]=0,[207]=3,[208]=0,
    /* 209-216*/ [209]=4,[210]=4,[211]=0,[212]=0,[213]=0,[214]=5,[215]=3,[216]=0,
    /* 217-224*/ [217]=0,[218]=0,[219]=0,[220]=5,[221]=5,[222]=4,[223]=0,[224]=0,
    /* 225-232*/ [225]=0,[226]=5,[227]=0,[228]=5,[229]=5,[230]=0,[231]=0,[232]=0,
    /* 233-240*/ [233]=0,[234]=5,[235]=4,[236]=0,[237]=0,[238]=0,[239]=0,[240]=0,
    /* 241-248*/ [241]=5,[242]=4,[243]=5,[244]=5,[245]=5,[246]=5,[247]=5,[248]=5,
    /* 249-256*/ [249]=5,[250]=5,[251]=3,[252]=3,[253]=3,[254]=3,[255]=3,[256]=3,
    /* 257-264*/ [257]=3,[258]=3,[259]=3,[260]=3,[261]=0,[262]=0,[263]=0,[264]=0,
    /* 265-272*/ [265]=0,[266]=0,[267]=0,[268]=0,[269]=0,[270]=3,[271]=3,[272]=3,
    /* 273-280*/ [273]=3,[274]=3,[275]=3,[276]=3,[277]=3,[278]=0,[279]=0,[280]=5,
    /* 281-288*/ [281]=5,[282]=5,[283]=0,[284]=0,[285]=2,[286]=2,[287]=5,[288]=5,
    /* 289-296*/ [289]=5,[290]=1,[291]=1,[292]=1,[293]=3,[294]=3,[295]=3,[296]=2,
    /* 297-304*/ [297]=2,[298]=4,[299]=0,[300]=4,[301]=4,[302]=3,[303]=4,[304]=5,
    /* 305-312*/ [305]=5,[306]=5,[307]=0,[308]=0,[309]=5,[310]=5,[311]=0,[312]=0,
    /* 313-320*/ [313]=1,[314]=2,[315]=3,[316]=2,[317]=2,[318]=5,[319]=5,[320]=2,
    /* 321-328*/ [321]=2,[322]=0,[323]=0,[324]=0,[325]=4,[326]=4,[327]=4,[328]=3,
    /* 329-336*/ [329]=3,[330]=3,[331]=3,[332]=3,[333]=1,[334]=1,[335]=1,[336]=2,
    /* 337-344*/ [337]=4,[338]=4,[339]=0,[340]=0,[341]=2,[342]=2,[343]=0,[344]=0,
    /* 345-352*/ [345]=1,[346]=1,[347]=1,[348]=1,[349]=1,[350]=1,[351]=0,[352]=3,
    /* 353-360*/ [353]=4,[354]=4,[355]=4,[356]=4,[357]=5,[358]=4,[359]=3,[360]=0,
    /* 361-368*/ [361]=0,[362]=0,[363]=3,[364]=3,[365]=3,[366]=1,[367]=1,[368]=1,
    /* 369-376*/ [369]=5,[370]=4,[371]=5,[372]=5,[373]=5,[374]=5,[375]=5,[376]=5,
    /* 377-384*/ [377]=5,[378]=5,[379]=5,[380]=5,[381]=5,[382]=5,[383]=5,[384]=5,
    /* 385-386*/ [385]=5,[386]=5,
};

// Gen 3 base stats for Hoenn #252..386 (fields: max_hp/atk/def/spd=Speed/
// spatk/spdef). Gen 3-specific values (differ from later gens for Beautifly,
// Masquerain, Exploud, Volbeat, Illumise). Verified against Bulbapedia/Serebii.
static const struct pkmn_base_stats gen3_hoenn_base_stats[387] = {
    [252] = {.max_hp=40,  .atk=45,  .def=35,  .spd=70,  .spatk=65,  .spdef=55},   // Treecko
    [253] = {.max_hp=50,  .atk=65,  .def=45,  .spd=95,  .spatk=85,  .spdef=65},   // Grovyle
    [254] = {.max_hp=70,  .atk=85,  .def=65,  .spd=120, .spatk=105, .spdef=85},   // Sceptile
    [255] = {.max_hp=45,  .atk=60,  .def=40,  .spd=45,  .spatk=70,  .spdef=50},   // Torchic
    [256] = {.max_hp=60,  .atk=85,  .def=60,  .spd=55,  .spatk=85,  .spdef=60},   // Combusken
    [257] = {.max_hp=80,  .atk=120, .def=70,  .spd=80,  .spatk=110, .spdef=70},   // Blaziken
    [258] = {.max_hp=50,  .atk=70,  .def=50,  .spd=40,  .spatk=50,  .spdef=50},   // Mudkip
    [259] = {.max_hp=70,  .atk=85,  .def=70,  .spd=50,  .spatk=60,  .spdef=70},   // Marshtomp
    [260] = {.max_hp=100, .atk=110, .def=90,  .spd=60,  .spatk=85,  .spdef=90},   // Swampert
    [261] = {.max_hp=35,  .atk=55,  .def=35,  .spd=35,  .spatk=30,  .spdef=30},   // Poochyena
    [262] = {.max_hp=70,  .atk=90,  .def=70,  .spd=70,  .spatk=60,  .spdef=60},   // Mightyena
    [263] = {.max_hp=38,  .atk=30,  .def=41,  .spd=60,  .spatk=30,  .spdef=41},   // Zigzagoon
    [264] = {.max_hp=78,  .atk=70,  .def=61,  .spd=100, .spatk=50,  .spdef=61},   // Linoone
    [265] = {.max_hp=45,  .atk=45,  .def=35,  .spd=20,  .spatk=20,  .spdef=30},   // Wurmple
    [266] = {.max_hp=50,  .atk=35,  .def=55,  .spd=15,  .spatk=25,  .spdef=25},   // Silcoon
    [267] = {.max_hp=60,  .atk=70,  .def=50,  .spd=65,  .spatk=90,  .spdef=50},   // Beautifly
    [268] = {.max_hp=50,  .atk=35,  .def=55,  .spd=15,  .spatk=25,  .spdef=25},   // Cascoon
    [269] = {.max_hp=60,  .atk=50,  .def=70,  .spd=65,  .spatk=50,  .spdef=90},   // Dustox
    [270] = {.max_hp=40,  .atk=30,  .def=30,  .spd=30,  .spatk=40,  .spdef=50},   // Lotad
    [271] = {.max_hp=60,  .atk=50,  .def=50,  .spd=50,  .spatk=60,  .spdef=70},   // Lombre
    [272] = {.max_hp=80,  .atk=70,  .def=70,  .spd=70,  .spatk=90,  .spdef=100},  // Ludicolo
    [273] = {.max_hp=40,  .atk=40,  .def=50,  .spd=30,  .spatk=30,  .spdef=30},   // Seedot
    [274] = {.max_hp=70,  .atk=70,  .def=40,  .spd=60,  .spatk=60,  .spdef=40},   // Nuzleaf
    [275] = {.max_hp=90,  .atk=100, .def=60,  .spd=80,  .spatk=90,  .spdef=60},   // Shiftry
    [276] = {.max_hp=40,  .atk=55,  .def=30,  .spd=85,  .spatk=30,  .spdef=30},   // Taillow
    [277] = {.max_hp=60,  .atk=85,  .def=60,  .spd=125, .spatk=50,  .spdef=50},   // Swellow
    [278] = {.max_hp=40,  .atk=30,  .def=30,  .spd=85,  .spatk=55,  .spdef=30},   // Wingull
    [279] = {.max_hp=60,  .atk=50,  .def=100, .spd=65,  .spatk=85,  .spdef=70},   // Pelipper
    [280] = {.max_hp=28,  .atk=25,  .def=25,  .spd=40,  .spatk=45,  .spdef=35},   // Ralts
    [281] = {.max_hp=38,  .atk=35,  .def=35,  .spd=50,  .spatk=65,  .spdef=55},   // Kirlia
    [282] = {.max_hp=68,  .atk=65,  .def=65,  .spd=80,  .spatk=125, .spdef=115},  // Gardevoir
    [283] = {.max_hp=40,  .atk=30,  .def=32,  .spd=65,  .spatk=50,  .spdef=52},   // Surskit
    [284] = {.max_hp=70,  .atk=60,  .def=62,  .spd=60,  .spatk=80,  .spdef=82},   // Masquerain
    [285] = {.max_hp=60,  .atk=40,  .def=60,  .spd=35,  .spatk=40,  .spdef=60},   // Shroomish
    [286] = {.max_hp=60,  .atk=130, .def=80,  .spd=70,  .spatk=60,  .spdef=60},   // Breloom
    [287] = {.max_hp=60,  .atk=60,  .def=60,  .spd=30,  .spatk=35,  .spdef=35},   // Slakoth
    [288] = {.max_hp=80,  .atk=80,  .def=80,  .spd=90,  .spatk=55,  .spdef=55},   // Vigoroth
    [289] = {.max_hp=150, .atk=160, .def=100, .spd=100, .spatk=95,  .spdef=65},   // Slaking
    [290] = {.max_hp=31,  .atk=45,  .def=90,  .spd=40,  .spatk=30,  .spdef=30},   // Nincada
    [291] = {.max_hp=61,  .atk=90,  .def=45,  .spd=160, .spatk=50,  .spdef=50},   // Ninjask
    [292] = {.max_hp=1,   .atk=90,  .def=45,  .spd=40,  .spatk=30,  .spdef=30},   // Shedinja
    [293] = {.max_hp=64,  .atk=51,  .def=23,  .spd=28,  .spatk=51,  .spdef=23},   // Whismur
    [294] = {.max_hp=84,  .atk=71,  .def=43,  .spd=48,  .spatk=71,  .spdef=43},   // Loudred
    [295] = {.max_hp=104, .atk=91,  .def=63,  .spd=68,  .spatk=91,  .spdef=63},   // Exploud
    [296] = {.max_hp=72,  .atk=60,  .def=30,  .spd=25,  .spatk=20,  .spdef=30},   // Makuhita
    [297] = {.max_hp=144, .atk=120, .def=60,  .spd=50,  .spatk=40,  .spdef=60},   // Hariyama
    [298] = {.max_hp=50,  .atk=20,  .def=40,  .spd=20,  .spatk=20,  .spdef=40},   // Azurill
    [299] = {.max_hp=30,  .atk=45,  .def=135, .spd=30,  .spatk=45,  .spdef=90},   // Nosepass
    [300] = {.max_hp=50,  .atk=45,  .def=45,  .spd=50,  .spatk=35,  .spdef=35},   // Skitty
    [301] = {.max_hp=70,  .atk=65,  .def=65,  .spd=70,  .spatk=55,  .spdef=55},   // Delcatty
    [302] = {.max_hp=50,  .atk=75,  .def=75,  .spd=50,  .spatk=65,  .spdef=65},   // Sableye
    [303] = {.max_hp=50,  .atk=85,  .def=85,  .spd=50,  .spatk=55,  .spdef=55},   // Mawile
    [304] = {.max_hp=50,  .atk=70,  .def=100, .spd=30,  .spatk=40,  .spdef=40},   // Aron
    [305] = {.max_hp=60,  .atk=90,  .def=140, .spd=40,  .spatk=50,  .spdef=50},   // Lairon
    [306] = {.max_hp=70,  .atk=110, .def=180, .spd=50,  .spatk=60,  .spdef=60},   // Aggron
    [307] = {.max_hp=30,  .atk=40,  .def=55,  .spd=60,  .spatk=40,  .spdef=55},   // Meditite
    [308] = {.max_hp=60,  .atk=60,  .def=75,  .spd=80,  .spatk=60,  .spdef=75},   // Medicham
    [309] = {.max_hp=40,  .atk=45,  .def=40,  .spd=65,  .spatk=65,  .spdef=40},   // Electrike
    [310] = {.max_hp=70,  .atk=75,  .def=60,  .spd=105, .spatk=105, .spdef=60},   // Manectric
    [311] = {.max_hp=60,  .atk=50,  .def=40,  .spd=95,  .spatk=85,  .spdef=75},   // Plusle
    [312] = {.max_hp=60,  .atk=40,  .def=50,  .spd=95,  .spatk=75,  .spdef=85},   // Minun
    [313] = {.max_hp=65,  .atk=73,  .def=55,  .spd=85,  .spatk=47,  .spdef=75},   // Volbeat
    [314] = {.max_hp=65,  .atk=47,  .def=55,  .spd=85,  .spatk=73,  .spdef=75},   // Illumise
    [315] = {.max_hp=50,  .atk=60,  .def=45,  .spd=65,  .spatk=100, .spdef=80},   // Roselia
    [316] = {.max_hp=70,  .atk=43,  .def=53,  .spd=40,  .spatk=43,  .spdef=53},   // Gulpin
    [317] = {.max_hp=100, .atk=73,  .def=83,  .spd=55,  .spatk=73,  .spdef=83},   // Swalot
    [318] = {.max_hp=45,  .atk=90,  .def=20,  .spd=65,  .spatk=65,  .spdef=20},   // Carvanha
    [319] = {.max_hp=70,  .atk=120, .def=40,  .spd=95,  .spatk=95,  .spdef=40},   // Sharpedo
    [320] = {.max_hp=130, .atk=70,  .def=35,  .spd=60,  .spatk=70,  .spdef=35},   // Wailmer
    [321] = {.max_hp=170, .atk=90,  .def=45,  .spd=60,  .spatk=90,  .spdef=45},   // Wailord
    [322] = {.max_hp=60,  .atk=60,  .def=40,  .spd=35,  .spatk=65,  .spdef=45},   // Numel
    [323] = {.max_hp=70,  .atk=100, .def=70,  .spd=40,  .spatk=105, .spdef=75},   // Camerupt
    [324] = {.max_hp=70,  .atk=85,  .def=140, .spd=20,  .spatk=85,  .spdef=70},   // Torkoal
    [325] = {.max_hp=60,  .atk=25,  .def=35,  .spd=60,  .spatk=70,  .spdef=80},   // Spoink
    [326] = {.max_hp=80,  .atk=45,  .def=65,  .spd=80,  .spatk=90,  .spdef=110},  // Grumpig
    [327] = {.max_hp=60,  .atk=60,  .def=60,  .spd=60,  .spatk=60,  .spdef=60},   // Spinda
    [328] = {.max_hp=45,  .atk=100, .def=45,  .spd=10,  .spatk=45,  .spdef=45},   // Trapinch
    [329] = {.max_hp=50,  .atk=70,  .def=50,  .spd=70,  .spatk=50,  .spdef=50},   // Vibrava
    [330] = {.max_hp=80,  .atk=100, .def=80,  .spd=100, .spatk=80,  .spdef=80},   // Flygon
    [331] = {.max_hp=50,  .atk=85,  .def=40,  .spd=35,  .spatk=85,  .spdef=40},   // Cacnea
    [332] = {.max_hp=70,  .atk=115, .def=60,  .spd=55,  .spatk=115, .spdef=60},   // Cacturne
    [333] = {.max_hp=45,  .atk=40,  .def=60,  .spd=50,  .spatk=40,  .spdef=75},   // Swablu
    [334] = {.max_hp=75,  .atk=70,  .def=90,  .spd=80,  .spatk=70,  .spdef=105},  // Altaria
    [335] = {.max_hp=73,  .atk=115, .def=60,  .spd=90,  .spatk=60,  .spdef=60},   // Zangoose
    [336] = {.max_hp=73,  .atk=100, .def=60,  .spd=65,  .spatk=100, .spdef=60},   // Seviper
    [337] = {.max_hp=70,  .atk=55,  .def=65,  .spd=70,  .spatk=95,  .spdef=85},   // Lunatone
    [338] = {.max_hp=70,  .atk=95,  .def=85,  .spd=70,  .spatk=55,  .spdef=65},   // Solrock
    [339] = {.max_hp=50,  .atk=48,  .def=43,  .spd=60,  .spatk=46,  .spdef=41},   // Barboach
    [340] = {.max_hp=110, .atk=78,  .def=73,  .spd=60,  .spatk=76,  .spdef=71},   // Whiscash
    [341] = {.max_hp=43,  .atk=80,  .def=65,  .spd=35,  .spatk=50,  .spdef=35},   // Corphish
    [342] = {.max_hp=63,  .atk=120, .def=85,  .spd=55,  .spatk=90,  .spdef=55},   // Crawdaunt
    [343] = {.max_hp=40,  .atk=40,  .def=55,  .spd=55,  .spatk=40,  .spdef=70},   // Baltoy
    [344] = {.max_hp=60,  .atk=70,  .def=105, .spd=75,  .spatk=70,  .spdef=120},  // Claydol
    [345] = {.max_hp=66,  .atk=41,  .def=77,  .spd=23,  .spatk=61,  .spdef=87},   // Lileep
    [346] = {.max_hp=86,  .atk=81,  .def=97,  .spd=43,  .spatk=81,  .spdef=107},  // Cradily
    [347] = {.max_hp=45,  .atk=95,  .def=50,  .spd=75,  .spatk=40,  .spdef=50},   // Anorith
    [348] = {.max_hp=75,  .atk=125, .def=100, .spd=45,  .spatk=70,  .spdef=80},   // Armaldo
    [349] = {.max_hp=20,  .atk=15,  .def=20,  .spd=80,  .spatk=10,  .spdef=55},   // Feebas
    [350] = {.max_hp=95,  .atk=60,  .def=79,  .spd=81,  .spatk=100, .spdef=125},  // Milotic
    [351] = {.max_hp=70,  .atk=70,  .def=70,  .spd=70,  .spatk=70,  .spdef=70},   // Castform
    [352] = {.max_hp=60,  .atk=90,  .def=70,  .spd=40,  .spatk=60,  .spdef=120},  // Kecleon
    [353] = {.max_hp=44,  .atk=75,  .def=35,  .spd=45,  .spatk=63,  .spdef=33},   // Shuppet
    [354] = {.max_hp=64,  .atk=115, .def=65,  .spd=65,  .spatk=83,  .spdef=63},   // Banette
    [355] = {.max_hp=20,  .atk=40,  .def=90,  .spd=25,  .spatk=30,  .spdef=90},   // Duskull
    [356] = {.max_hp=40,  .atk=70,  .def=130, .spd=25,  .spatk=60,  .spdef=130},  // Dusclops
    [357] = {.max_hp=99,  .atk=68,  .def=83,  .spd=51,  .spatk=72,  .spdef=87},   // Tropius
    [358] = {.max_hp=65,  .atk=50,  .def=70,  .spd=65,  .spatk=95,  .spdef=80},   // Chimecho
    [359] = {.max_hp=65,  .atk=130, .def=60,  .spd=75,  .spatk=75,  .spdef=60},   // Absol
    [360] = {.max_hp=95,  .atk=23,  .def=48,  .spd=23,  .spatk=23,  .spdef=48},   // Wynaut
    [361] = {.max_hp=50,  .atk=50,  .def=50,  .spd=50,  .spatk=50,  .spdef=50},   // Snorunt
    [362] = {.max_hp=80,  .atk=80,  .def=80,  .spd=80,  .spatk=80,  .spdef=80},   // Glalie
    [363] = {.max_hp=70,  .atk=40,  .def=50,  .spd=25,  .spatk=55,  .spdef=50},   // Spheal
    [364] = {.max_hp=90,  .atk=60,  .def=70,  .spd=45,  .spatk=75,  .spdef=70},   // Sealeo
    [365] = {.max_hp=110, .atk=80,  .def=90,  .spd=65,  .spatk=95,  .spdef=90},   // Walrein
    [366] = {.max_hp=35,  .atk=64,  .def=85,  .spd=32,  .spatk=74,  .spdef=55},   // Clamperl
    [367] = {.max_hp=55,  .atk=104, .def=105, .spd=52,  .spatk=94,  .spdef=75},   // Huntail
    [368] = {.max_hp=55,  .atk=84,  .def=105, .spd=52,  .spatk=114, .spdef=75},   // Gorebyss
    [369] = {.max_hp=100, .atk=90,  .def=130, .spd=55,  .spatk=45,  .spdef=65},   // Relicanth
    [370] = {.max_hp=43,  .atk=30,  .def=55,  .spd=97,  .spatk=40,  .spdef=65},   // Luvdisc
    [371] = {.max_hp=45,  .atk=75,  .def=60,  .spd=50,  .spatk=40,  .spdef=30},   // Bagon
    [372] = {.max_hp=65,  .atk=95,  .def=100, .spd=50,  .spatk=60,  .spdef=50},   // Shelgon
    [373] = {.max_hp=95,  .atk=135, .def=80,  .spd=100, .spatk=110, .spdef=80},   // Salamence
    [374] = {.max_hp=40,  .atk=55,  .def=80,  .spd=30,  .spatk=35,  .spdef=60},   // Beldum
    [375] = {.max_hp=60,  .atk=75,  .def=100, .spd=50,  .spatk=55,  .spdef=80},   // Metang
    [376] = {.max_hp=80,  .atk=135, .def=130, .spd=70,  .spatk=95,  .spdef=90},   // Metagross
    [377] = {.max_hp=80,  .atk=100, .def=200, .spd=50,  .spatk=50,  .spdef=100},  // Regirock
    [378] = {.max_hp=80,  .atk=50,  .def=100, .spd=50,  .spatk=100, .spdef=200},  // Regice
    [379] = {.max_hp=80,  .atk=75,  .def=150, .spd=50,  .spatk=75,  .spdef=150},  // Registeel
    [380] = {.max_hp=80,  .atk=80,  .def=90,  .spd=110, .spatk=110, .spdef=130},  // Latias
    [381] = {.max_hp=80,  .atk=90,  .def=80,  .spd=110, .spatk=130, .spdef=110},  // Latios
    [382] = {.max_hp=100, .atk=100, .def=90,  .spd=90,  .spatk=150, .spdef=140},  // Kyogre
    [383] = {.max_hp=100, .atk=150, .def=140, .spd=90,  .spatk=100, .spdef=90},   // Groudon
    [384] = {.max_hp=105, .atk=150, .def=90,  .spd=95,  .spatk=150, .spdef=90},   // Rayquaza
    [385] = {.max_hp=100, .atk=100, .def=100, .spd=100, .spatk=100, .spdef=100},  // Jirachi
    [386] = {.max_hp=50,  .atk=150, .def=50,  .spd=150, .spatk=150, .spdef=50},   // Deoxys (Normal Forme)
};
/* =================================================================== */

static const struct pkmn_base_stats *gen3_base_for_dex(uint16_t dex)
{
    if (dex >= 1 && dex <= MEW) // #1..151 in the Gen 2 table (dex-indexed)
    {
        return &pkmn_base_stats_gen2[dex];
    }
    if (dex >= 152 && dex <= 251)
    {
        return &pkmn_base_stats_gen2[dex];
    }
    if (dex >= 252 && dex <= GEN3_NATIONAL_DEX_MAX)
    {
        return &gen3_hoenn_base_stats[dex];
    }
    return NULL;
}

uint32_t gen3_exp_for_level(int n, uint8_t growth_rate)
{
    if (n <= 1)
    {
        return 0;
    }
    long long L = n;
    long long e = 0;
    switch (growth_rate)
    {
    case GR_FAST:
        e = 4 * L * L * L / 5;
        break;
    case GR_MEDIUM_FAST:
        e = L * L * L;
        break;
    case GR_MEDIUM_SLOW:
        e = 6 * L * L * L / 5 - 15 * L * L + 100 * L - 140;
        break;
    case GR_SLOW:
        e = 5 * L * L * L / 4;
        break;
    case GR_ERRATIC:
        if (L < 50)
            e = L * L * L * (100 - L) / 50;
        else if (L < 68)
            e = L * L * L * (150 - L) / 100;
        else if (L < 98)
            e = L * L * L * ((1911 - 10 * L) / 3) / 500;
        else
            e = L * L * L * (160 - L) / 100;
        break;
    case GR_FLUCTUATING:
        if (L < 15)
            e = L * L * L * ((L + 1) / 3 + 24) / 50;
        else if (L < 36)
            e = L * L * L * (L + 14) / 50;
        else
            e = L * L * L * (L / 2 + 32) / 50;
        break;
    default:
        e = L * L * L;
        break;
    }
    if (e < 0)
    {
        e = 0;
    }
    return (uint32_t)e;
}

int gen3_level_from_exp(uint32_t exp, uint8_t growth_rate)
{
    int level = 1;
    for (int L = 2; L <= 100; L++)
    {
        if (gen3_exp_for_level(L, growth_rate) <= exp)
        {
            level = L;
        }
        else
        {
            break;
        }
    }
    return level;
}

// One stat via the Gen 3 formula. is_hp changes the +level+10 vs +5 tail.
static uint16_t gen3_calc_stat(bool is_hp, uint8_t base, uint8_t iv, uint8_t ev, int level, int nature_num, int nature_den)
{
    int common = (2 * base + iv + ev / 4) * level / 100;
    if (is_hp)
    {
        return (uint16_t)(common + level + 10);
    }
    int stat = common + 5;
    stat = stat * nature_num / nature_den; // 11/10, 9/10, or 1/1
    return (uint16_t)stat;
}

bool gen3_build_party_data(const struct pksav_gba_pc_pokemon *pc,
                           struct pksav_gba_pokemon_party_data *out)
{
    uint16_t internal = pksav_littleendian16(pc->blocks.growth.species);
    uint16_t dex = gen3_internal_to_national(internal);
    const struct pkmn_base_stats *base = gen3_base_for_dex(dex);
    if (dex == 0 || base == NULL)
    {
        return false; // egg / unknown species: cannot build legal stats
    }

    uint32_t exp = pksav_littleendian32(pc->blocks.growth.exp);
    uint8_t growth = gen3_growth_rate[dex];
    int level = gen3_level_from_exp(exp, growth);
    if (level < 1) level = 1;
    if (level > 100) level = 100;

    // IVs (5 bits each) from the misc block.
    uint32_t ivbits = pksav_littleendian32(pc->blocks.misc.iv_egg_ability);
    uint8_t iv_hp = (ivbits >> 0) & 0x1F;
    uint8_t iv_atk = (ivbits >> 5) & 0x1F;
    uint8_t iv_def = (ivbits >> 10) & 0x1F;
    uint8_t iv_spd = (ivbits >> 15) & 0x1F;
    uint8_t iv_spatk = (ivbits >> 20) & 0x1F;
    uint8_t iv_spdef = (ivbits >> 25) & 0x1F;

    // EVs from the effort block.
    const struct pksav_gba_pokemon_effort_block *ev = &pc->blocks.effort;

    // Nature (0-24) from the personality value.
    int nature = (int)(pksav_littleendian32(pc->personality) % 25);
    int raise = nature / 5; // 0=Atk 1=Def 2=Spd 3=SpAtk 4=SpDef
    int lower = nature % 5;

    // Per non-HP stat nature multiplier as num/den (avoids float).
    // stat index: 0=atk 1=def 2=spd 3=spatk 4=spdef
    int num[5], den[5];
    for (int i = 0; i < 5; i++)
    {
        num[i] = 1;
        den[i] = 1;
    }
    if (raise != lower)
    {
        num[raise] = 11; den[raise] = 10;
        num[lower] = 9;  den[lower] = 10;
    }

    memset(out, 0, sizeof(*out));
    out->level = (uint8_t)level;
    out->condition = 0;

    // Shedinja always has 1 HP.
    if (base->max_hp == 1)
    {
        out->max_hp = 1;
    }
    else
    {
        out->max_hp = gen3_calc_stat(true, base->max_hp, iv_hp, ev->ev_hp, level, 1, 1);
    }
    out->atk = gen3_calc_stat(false, base->atk, iv_atk, ev->ev_atk, level, num[0], den[0]);
    out->def = gen3_calc_stat(false, base->def, iv_def, ev->ev_def, level, num[1], den[1]);
    out->spd = gen3_calc_stat(false, base->spd, iv_spd, ev->ev_spd, level, num[2], den[2]);
    out->spatk = gen3_calc_stat(false, base->spatk, iv_spatk, ev->ev_spatk, level, num[3], den[3]);
    out->spdef = gen3_calc_stat(false, base->spdef, iv_spdef, ev->ev_spdef, level, num[4], den[4]);
    out->current_hp = out->max_hp;
    return true;
}

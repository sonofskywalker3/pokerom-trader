/*
 * Gen 2 item names and the Gen 2 -> Gen 3 held-item conversion.
 * Distributed under the MIT License (MIT).
 */
#include "gen2_items.h"
#include <stddef.h>

/* Generated from pret pokecrystal constants/item_constants.asm and pokeemerald
 * include/constants/items.h, matched by name. Only items a Gen 3 Pokemon can
 * hold are mapped (key items, Gen 2 berries, mail, Apricorns and Gen 2-only
 * items such as the Pink/Polkadot Bow have no Gen 3 equivalent and map to 0). */
static const uint16_t gen2_item_to_gen3_tbl[256] = {
    [0x01] =   1, // MASTER_BALL
    [0x02] =   2, // ULTRA_BALL
    [0x03] = 179, // BRIGHTPOWDER
    [0x04] =   3, // GREAT_BALL
    [0x05] =   4, // POKE_BALL
    [0x08] =  94, // MOON_STONE
    [0x09] =  14, // ANTIDOTE
    [0x0A] =  15, // BURN_HEAL
    [0x0B] =  16, // ICE_HEAL
    [0x0C] =  17, // AWAKENING
    [0x0D] =  18, // PARLYZ_HEAL
    [0x0E] =  19, // FULL_RESTORE
    [0x0F] =  20, // MAX_POTION
    [0x10] =  21, // HYPER_POTION
    [0x11] =  22, // SUPER_POTION
    [0x12] =  13, // POTION
    [0x13] =  85, // ESCAPE_ROPE
    [0x14] =  86, // REPEL
    [0x15] =  37, // MAX_ELIXER
    [0x16] =  95, // FIRE_STONE
    [0x17] =  96, // THUNDERSTONE
    [0x18] =  97, // WATER_STONE
    [0x1A] =  63, // HP_UP
    [0x1B] =  64, // PROTEIN
    [0x1C] =  65, // IRON
    [0x1D] =  66, // CARBOS
    [0x1E] = 222, // LUCKY_PUNCH
    [0x1F] =  67, // CALCIUM
    [0x20] =  68, // RARE_CANDY
    [0x21] =  78, // X_ACCURACY
    [0x22] =  98, // LEAF_STONE
    [0x23] = 223, // METAL_POWDER
    [0x24] = 110, // NUGGET
    [0x25] =  80, // POKE_DOLL
    [0x26] =  23, // FULL_HEAL
    [0x27] =  24, // REVIVE
    [0x28] =  25, // MAX_REVIVE
    [0x29] =  73, // GUARD_SPEC
    [0x2A] =  83, // SUPER_REPEL
    [0x2B] =  84, // MAX_REPEL
    [0x2C] =  74, // DIRE_HIT
    [0x2E] =  26, // FRESH_WATER
    [0x2F] =  27, // SODA_POP
    [0x30] =  28, // LEMONADE
    [0x31] =  75, // X_ATTACK
    [0x33] =  76, // X_DEFEND
    [0x34] =  77, // X_SPEED
    [0x39] = 182, // EXP_SHARE
    [0x3E] =  69, // PP_UP
    [0x3F] =  34, // ETHER
    [0x40] =  35, // MAX_ETHER
    [0x41] =  36, // ELIXER
    [0x48] =  29, // MOOMOO_MILK
    [0x49] = 183, // QUICK_CLAW
    [0x4C] = 203, // SOFT_SAND
    [0x4D] = 210, // SHARP_BEAK
    [0x51] = 211, // POISON_BARB
    [0x52] = 187, // KINGS_ROCK
    [0x57] = 104, // BIG_MUSHROOM
    [0x58] = 188, // SILVERPOWDER
    [0x5B] = 189, // AMULET_COIN
    [0x5E] = 190, // CLEANSE_TAG
    [0x5F] = 209, // MYSTIC_WATER
    [0x60] = 214, // TWISTEDSPOON
    [0x62] = 207, // BLACKBELT_I
    [0x66] = 206, // BLACKGLASSES
    [0x69] = 225, // STICK
    [0x6A] = 194, // SMOKE_BALL
    [0x6B] = 212, // NEVERMELTICE
    [0x6C] = 208, // MAGNET
    [0x6E] = 106, // PEARL
    [0x6F] = 107, // BIG_PEARL
    [0x70] = 195, // EVERSTONE
    [0x71] = 213, // SPELL_TAG
    [0x75] = 205, // MIRACLE_SEED
    [0x76] = 224, // THICK_CLUB
    [0x77] = 196, // FOCUS_BAND
    [0x7A] =  31, // ENERGY_ROOT
    [0x7B] =  32, // HEAL_POWDER
    [0x7C] =  33, // REVIVAL_HERB
    [0x7D] = 204, // HARD_STONE
    [0x7E] = 197, // LUCKY_EGG
    [0x83] = 108, // STARDUST
    [0x84] = 109, // STAR_PIECE
    [0x8A] = 215, // CHARCOAL
    [0x8B] =  44, // BERRY_JUICE
    [0x8C] = 198, // SCOPE_LENS
    [0x8F] = 199, // METAL_COAT
    [0x90] = 216, // DRAGON_FANG
    [0x92] = 200, // LEFTOVERS
    [0x97] = 201, // DRAGON_SCALE
    [0x9C] =  45, // SACRED_ASH
    [0xA3] = 202, // LIGHT_BALL
    [0xA9] =  93, // SUN_STONE
    [0xAC] = 218, // UP_GRADE
};

/* 256 Crystal item names, index = item id - 1 */
static const char *const gen2_item_names_tbl[] = {
    "MASTER BALL", // 0x01
    "ULTRA BALL", // 0x02
    "BRIGHTPOWDER", // 0x03
    "GREAT BALL", // 0x04
    "# BALL", // 0x05
    "TERU-SAMA", // 0x06
    "BICYCLE", // 0x07
    "MOON STONE", // 0x08
    "ANTIDOTE", // 0x09
    "BURN HEAL", // 0x0a
    "ICE HEAL", // 0x0b
    "AWAKENING", // 0x0c
    "PARLYZ HEAL", // 0x0d
    "FULL RESTORE", // 0x0e
    "MAX POTION", // 0x0f
    "HYPER POTION", // 0x10
    "SUPER POTION", // 0x11
    "POTION", // 0x12
    "ESCAPE ROPE", // 0x13
    "REPEL", // 0x14
    "MAX ELIXER", // 0x15
    "FIRE STONE", // 0x16
    "THUNDERSTONE", // 0x17
    "WATER STONE", // 0x18
    "TERU-SAMA", // 0x19
    "HP UP", // 0x1a
    "PROTEIN", // 0x1b
    "IRON", // 0x1c
    "CARBOS", // 0x1d
    "LUCKY PUNCH", // 0x1e
    "CALCIUM", // 0x1f
    "RARE CANDY", // 0x20
    "X ACCURACY", // 0x21
    "LEAF STONE", // 0x22
    "METAL POWDER", // 0x23
    "NUGGET", // 0x24
    "# DOLL", // 0x25
    "FULL HEAL", // 0x26
    "REVIVE", // 0x27
    "MAX REVIVE", // 0x28
    "GUARD SPEC.", // 0x29
    "SUPER REPEL", // 0x2a
    "MAX REPEL", // 0x2b
    "DIRE HIT", // 0x2c
    "TERU-SAMA", // 0x2d
    "FRESH WATER", // 0x2e
    "SODA POP", // 0x2f
    "LEMONADE", // 0x30
    "X ATTACK", // 0x31
    "TERU-SAMA", // 0x32
    "X DEFEND", // 0x33
    "X SPEED", // 0x34
    "X SPECIAL", // 0x35
    "COIN CASE", // 0x36
    "ITEMFINDER", // 0x37
    "TERU-SAMA", // 0x38
    "EXP.SHARE", // 0x39
    "OLD ROD", // 0x3a
    "GOOD ROD", // 0x3b
    "SILVER LEAF", // 0x3c
    "SUPER ROD", // 0x3d
    "PP UP", // 0x3e
    "ETHER", // 0x3f
    "MAX ETHER", // 0x40
    "ELIXER", // 0x41
    "RED SCALE", // 0x42
    "SECRETPOTION", // 0x43
    "S.S.TICKET", // 0x44
    "MYSTERY EGG", // 0x45
    "CLEAR BELL", // 0x46
    "SILVER WING", // 0x47
    "MOOMOO MILK", // 0x48
    "QUICK CLAW", // 0x49
    "PSNCUREBERRY", // 0x4a
    "GOLD LEAF", // 0x4b
    "SOFT SAND", // 0x4c
    "SHARP BEAK", // 0x4d
    "PRZCUREBERRY", // 0x4e
    "BURNT BERRY", // 0x4f
    "ICE BERRY", // 0x50
    "POISON BARB", // 0x51
    "KING'S ROCK", // 0x52
    "BITTER BERRY", // 0x53
    "MINT BERRY", // 0x54
    "RED APRICORN", // 0x55
    "TINYMUSHROOM", // 0x56
    "BIG MUSHROOM", // 0x57
    "SILVERPOWDER", // 0x58
    "BLU APRICORN", // 0x59
    "TERU-SAMA", // 0x5a
    "AMULET COIN", // 0x5b
    "YLW APRICORN", // 0x5c
    "GRN APRICORN", // 0x5d
    "CLEANSE TAG", // 0x5e
    "MYSTIC WATER", // 0x5f
    "TWISTEDSPOON", // 0x60
    "WHT APRICORN", // 0x61
    "BLACKBELT", // 0x62
    "BLK APRICORN", // 0x63
    "TERU-SAMA", // 0x64
    "PNK APRICORN", // 0x65
    "BLACKGLASSES", // 0x66
    "SLOWPOKETAIL", // 0x67
    "PINK BOW", // 0x68
    "STICK", // 0x69
    "SMOKE BALL", // 0x6a
    "NEVERMELTICE", // 0x6b
    "MAGNET", // 0x6c
    "MIRACLEBERRY", // 0x6d
    "PEARL", // 0x6e
    "BIG PEARL", // 0x6f
    "EVERSTONE", // 0x70
    "SPELL TAG", // 0x71
    "RAGECANDYBAR", // 0x72
    "GS BALL", // 0x73
    "BLUE CARD", // 0x74
    "MIRACLE SEED", // 0x75
    "THICK CLUB", // 0x76
    "FOCUS BAND", // 0x77
    "TERU-SAMA", // 0x78
    "ENERGYPOWDER", // 0x79
    "ENERGY ROOT", // 0x7a
    "HEAL POWDER", // 0x7b
    "REVIVAL HERB", // 0x7c
    "HARD STONE", // 0x7d
    "LUCKY EGG", // 0x7e
    "CARD KEY", // 0x7f
    "MACHINE PART", // 0x80
    "EGG TICKET", // 0x81
    "LOST ITEM", // 0x82
    "STARDUST", // 0x83
    "STAR PIECE", // 0x84
    "BASEMENT KEY", // 0x85
    "PASS", // 0x86
    "TERU-SAMA", // 0x87
    "TERU-SAMA", // 0x88
    "TERU-SAMA", // 0x89
    "CHARCOAL", // 0x8a
    "BERRY JUICE", // 0x8b
    "SCOPE LENS", // 0x8c
    "TERU-SAMA", // 0x8d
    "TERU-SAMA", // 0x8e
    "METAL COAT", // 0x8f
    "DRAGON FANG", // 0x90
    "TERU-SAMA", // 0x91
    "LEFTOVERS", // 0x92
    "TERU-SAMA", // 0x93
    "TERU-SAMA", // 0x94
    "TERU-SAMA", // 0x95
    "MYSTERYBERRY", // 0x96
    "DRAGON SCALE", // 0x97
    "BERSERK GENE", // 0x98
    "TERU-SAMA", // 0x99
    "TERU-SAMA", // 0x9a
    "TERU-SAMA", // 0x9b
    "SACRED ASH", // 0x9c
    "HEAVY BALL", // 0x9d
    "FLOWER MAIL", // 0x9e
    "LEVEL BALL", // 0x9f
    "LURE BALL", // 0xa0
    "FAST BALL", // 0xa1
    "TERU-SAMA", // 0xa2
    "LIGHT BALL", // 0xa3
    "FRIEND BALL", // 0xa4
    "MOON BALL", // 0xa5
    "LOVE BALL", // 0xa6
    "NORMAL BOX", // 0xa7
    "GORGEOUS BOX", // 0xa8
    "SUN STONE", // 0xa9
    "POLKADOT BOW", // 0xaa
    "TERU-SAMA", // 0xab
    "UP-GRADE", // 0xac
    "BERRY", // 0xad
    "GOLD BERRY", // 0xae
    "SQUIRTBOTTLE", // 0xaf
    "TERU-SAMA", // 0xb0
    "PARK BALL", // 0xb1
    "RAINBOW WING", // 0xb2
    "TERU-SAMA", // 0xb3
    "BRICK PIECE", // 0xb4
    "SURF MAIL", // 0xb5
    "LITEBLUEMAIL", // 0xb6
    "PORTRAITMAIL", // 0xb7
    "LOVELY MAIL", // 0xb8
    "EON MAIL", // 0xb9
    "MORPH MAIL", // 0xba
    "BLUESKY MAIL", // 0xbb
    "MUSIC MAIL", // 0xbc
    "MIRAGE MAIL", // 0xbd
    "TERU-SAMA", // 0xbe
    "TM01", // 0xbf
    "TM02", // 0xc0
    "TM03", // 0xc1
    "TM04", // 0xc2
    "TERU-SAMA", // 0xc3
    "TM05", // 0xc4
    "TM06", // 0xc5
    "TM07", // 0xc6
    "TM08", // 0xc7
    "TM09", // 0xc8
    "TM10", // 0xc9
    "TM11", // 0xca
    "TM12", // 0xcb
    "TM13", // 0xcc
    "TM14", // 0xcd
    "TM15", // 0xce
    "TM16", // 0xcf
    "TM17", // 0xd0
    "TM18", // 0xd1
    "TM19", // 0xd2
    "TM20", // 0xd3
    "TM21", // 0xd4
    "TM22", // 0xd5
    "TM23", // 0xd6
    "TM24", // 0xd7
    "TM25", // 0xd8
    "TM26", // 0xd9
    "TM27", // 0xda
    "TM28", // 0xdb
    "TERU-SAMA", // 0xdc
    "TM29", // 0xdd
    "TM30", // 0xde
    "TM31", // 0xdf
    "TM32", // 0xe0
    "TM33", // 0xe1
    "TM34", // 0xe2
    "TM35", // 0xe3
    "TM36", // 0xe4
    "TM37", // 0xe5
    "TM38", // 0xe6
    "TM39", // 0xe7
    "TM40", // 0xe8
    "TM41", // 0xe9
    "TM42", // 0xea
    "TM43", // 0xeb
    "TM44", // 0xec
    "TM45", // 0xed
    "TM46", // 0xee
    "TM47", // 0xef
    "TM48", // 0xf0
    "TM49", // 0xf1
    "TM50", // 0xf2
    "HM01", // 0xf3
    "HM02", // 0xf4
    "HM03", // 0xf5
    "HM04", // 0xf6
    "HM05", // 0xf7
    "HM06", // 0xf8
    "HM07", // 0xf9
    "TERU-SAMA", // 0xfa
    "TERU-SAMA", // 0xfb
    "TERU-SAMA", // 0xfc
    "TERU-SAMA", // 0xfd
    "TERU-SAMA", // 0xfe
    "TERU-SAMA", // 0xff
    "?", // 0x100
};

const char *gen2_item_name(uint8_t item)
{
    if (item == 0)
    {
        return "";
    }
    size_t n = sizeof(gen2_item_names_tbl) / sizeof(gen2_item_names_tbl[0]);
    if ((size_t)item > n)
    {
        return "?";
    }
    return gen2_item_names_tbl[item - 1];
}

uint16_t gen2_item_to_gen3(uint8_t item)
{
    return gen2_item_to_gen3_tbl[item];
}

bool gen2_item_is_berry(uint8_t item)
{
    switch (item)
    {
    case 0x4A: // PSNCUREBERRY
    case 0x4E: // PRZCUREBERRY
    case 0x4F: // BURNT_BERRY
    case 0x50: // ICE_BERRY
    case 0x53: // BITTER_BERRY
    case 0x54: // MINT_BERRY
    case 0x6D: // MIRACLEBERRY
    case 0x96: // MYSTERYBERRY
    case 0xAD: // BERRY
    case 0xAE: // GOLD_BERRY
        return true;
    default:
        return false;
    }
}

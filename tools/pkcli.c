/*
 * pkcli — headless save inspector / trader for scripted use.
 *
 * Commands:
 *   pkcli list  <save>                                    party + all boxes
 *   pkcli dex   <save>                                    dex counts + owned/seen lists
 *   pkcli move  <save> <party|box> <box#> <idx> <party|box> <box#> <idx>
 *   pkcli trade <save1> <partyIdx1> <save2> <partyIdx2>   same-gen party swap + dex update
 *   pkcli evolve <save> <partyIdx>                        trade-evolve a party mon in place
 *   pkcli denick <save>                                   reset all nicknames to species names (Gen 1/2)
 *   pkcli copy  <src> <party|box> <box#> <idx> <dst> <dstBox#>
 *                                                         one-way copy into dst box (Gen 1/Gen 2 same gen, or Gen 1 -> Gen 2 Time Capsule style)
 *
 * Mirrors the TradeScreen flow exactly: swap -> update_seen_owned_pkmn on both
 * sides -> save_savefile_to_path. File backups are the caller's job.
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen3_species.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_or_die(const char *path, PokemonSave *sav)
{
    load_savefile_from_path(path, sav);
    if (sav->save_generation_type == SAVE_GENERATION_NONE ||
        sav->save_generation_type == SAVE_GENERATION_CORRUPTED)
    {
        fprintf(stderr, "ERROR: could not load save: %s\n", path);
        return 0;
    }
    return 1;
}

static void print_entry(const PokemonSave *sav, enum bills_pc_location loc, int box, int idx)
{
    if (!bills_pc_slot_occupied(sav, loc, box, idx))
        return;
    struct bills_pc_entry_view v;
    bills_pc_get_view(sav, loc, box, idx, &v);
    if (!v.occupied)
        return;
    const char *name = v.dex ? gen3_national_dex_name(v.dex) : "?";
    if (loc == BILLS_PC_LOC_PARTY)
        printf("party    slot %2d: #%03u %-11s L%-3u si=%-3u nick=%s\n", idx, v.dex, name, v.level, v.species, v.nickname);
    else
        printf("box %2d   slot %2d: #%03u %-11s L%-3u si=%-3u nick=%s\n", box, idx, v.dex, name, v.level, v.species, v.nickname);
}

static int cmd_list(const char *path)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    struct trainer_info tr;
    create_trainer(&sav, &tr);
    printf("save: %s\ntrainer: %s  id=%u  gen=%d\n", path, tr.trainer_name, tr.trainer_id, sav.save_generation_type);
    int pc = bills_pc_party_capacity(&sav);
    for (int i = 0; i < pc; i++)
        print_entry(&sav, BILLS_PC_LOC_PARTY, 0, i);
    /* Gen 1/2 keep the live copy of the current box in a separate area;
     * the per-box bank slot for it can be stale or empty. Sync before reading. */
    bills_pc_normalize_current_box(&sav);
    int nb = bills_pc_num_boxes(&sav);
    int cap = bills_pc_box_capacity(&sav);
    for (int b = 0; b < nb; b++)
        for (int i = 0; i < cap; i++)
            print_entry(&sav, BILLS_PC_LOC_BOX, b, i);
    return 0;
}

static int cmd_dex(const char *path)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    uint16_t max = pokedex_species_count(&sav);
    uint16_t seen_count, owned_count;
    pokedex_counts(&sav, &seen_count, &owned_count);
    printf("save: %s\ndex: seen %u owned %u / %u\n", path, seen_count, owned_count, max);
    for (uint16_t d = 1; d <= max; d++)
    {
        bool seen, owned;
        pokedex_get_entry(&sav, d, &seen, &owned);
        if (owned)
            printf("OWNED #%03u %s\n", d, gen3_national_dex_name(d));
        else if (seen)
            printf("SEEN  #%03u %s\n", d, gen3_national_dex_name(d));
    }
    return 0;
}

static int cmd_elig(const char *path)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    struct trainer_info tr;
    create_trainer(&sav, &tr);
    int pc = bills_pc_party_capacity(&sav);
    for (int i = 0; i < pc; i++)
    {
        if (!bills_pc_slot_occupied(&sav, BILLS_PC_LOC_PARTY, 0, i))
            continue;
        struct bills_pc_entry_view v;
        bills_pc_get_view(&sav, BILLS_PC_LOC_PARTY, 0, i, &v);
        enum eligible_trade_status st = check_trade_eligibility(&tr, (uint8_t)i);
        const char *why =
            st == E_TRADE_STATUS_ELIGIBLE ? "ELIGIBLE" :
            st == E_TRADE_STATUS_HM_MOVE ? "BLOCKED-HM" :
            st == E_TRADE_STATUS_MAIL ? "BLOCKED-MAIL" : "BLOCKED-GEN2";
        printf("party slot %2d: #%03u %-11s %s\n", i, v.dex, gen3_national_dex_name(v.dex), why);
    }
    return 0;
}

static int parse_loc(const char *s, enum bills_pc_location *out)
{
    if (strcmp(s, "party") == 0) { *out = BILLS_PC_LOC_PARTY; return 1; }
    if (strcmp(s, "box") == 0)   { *out = BILLS_PC_LOC_BOX;   return 1; }
    return 0;
}

static int cmd_move(const char *path, char **a)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    bills_pc_normalize_current_box(&sav);
    struct bills_pc_slot src, dst;
    if (!parse_loc(a[0], &src.location) || !parse_loc(a[3], &dst.location))
    {
        fprintf(stderr, "ERROR: location must be 'party' or 'box'\n");
        return 1;
    }
    src.box_num = atoi(a[1]); src.index = atoi(a[2]);
    dst.box_num = atoi(a[4]); dst.index = atoi(a[5]);
    pksavhelper_error err = bills_pc_move_pkmn(&sav, src, dst);
    if (err != error_none)
    {
        fprintf(stderr, "ERROR: move failed (%d)\n", err);
        return 1;
    }
    bills_pc_flush_current_box(&sav);
    if (save_savefile_to_path(&sav, (char *)path) != error_none)
    {
        fprintf(stderr, "ERROR: save write failed\n");
        return 1;
    }
    printf("moved ok\n");
    return 0;
}

static int cmd_trade(const char *p1, int i1, const char *p2, int i2)
{
    PokemonSave s1, s2;
    if (!load_or_die(p1, &s1) || !load_or_die(p2, &s2)) return 1;
    if (s1.save_generation_type != s2.save_generation_type)
    {
        fprintf(stderr, "ERROR: cross-gen trade not supported by pkcli\n");
        return 1;
    }
    struct bills_pc_entry_view v1, v2;
    bills_pc_get_view(&s1, BILLS_PC_LOC_PARTY, 0, i1, &v1);
    bills_pc_get_view(&s2, BILLS_PC_LOC_PARTY, 0, i2, &v2);
    if (!v1.occupied || !v2.occupied)
    {
        fprintf(stderr, "ERROR: empty party slot (%d occ=%d, %d occ=%d)\n", i1, v1.occupied, i2, v2.occupied);
        return 1;
    }
    pksavhelper_error err = swap_pkmn_at_index_between_saves(&s1, &s2, (uint8_t)i1, (uint8_t)i2);
    if (err != error_none) { fprintf(stderr, "ERROR: swap failed (%d)\n", err); return 1; }
    err = update_seen_owned_pkmn(&s1, (uint8_t)i1);
    if (err != error_none) { fprintf(stderr, "ERROR: dex update save1 failed (%d)\n", err); return 1; }
    err = update_seen_owned_pkmn(&s2, (uint8_t)i2);
    if (err != error_none) { fprintf(stderr, "ERROR: dex update save2 failed (%d)\n", err); return 1; }
    if (save_savefile_to_path(&s1, (char *)p1) != error_none) { fprintf(stderr, "ERROR: write save1\n"); return 1; }
    if (save_savefile_to_path(&s2, (char *)p2) != error_none) { fprintf(stderr, "ERROR: write save2\n"); return 1; }
    printf("traded: %s (#%03u %s) <-> %s (#%03u %s)\n",
           p1, v1.dex, gen3_national_dex_name(v1.dex),
           p2, v2.dex, gen3_national_dex_name(v2.dex));
    return 0;
}

static int cmd_evolve(const char *path, int idx)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    struct bills_pc_entry_view v;
    bills_pc_get_view(&sav, BILLS_PC_LOC_PARTY, 0, idx, &v);
    if (!v.occupied) { fprintf(stderr, "ERROR: empty party slot %d\n", idx); return 1; }
    bool ok = false;
    if (sav.save_generation_type == SAVE_GENERATION_1) ok = check_trade_evolution_gen1(&sav, (uint8_t)idx);
    else if (sav.save_generation_type == SAVE_GENERATION_2) ok = check_trade_evolution_gen2(&sav, (uint8_t)idx);
    else if (sav.save_generation_type == SAVE_GENERATION_3) ok = check_trade_evolution_gen3(&sav, (uint8_t)idx) == E_EVO_STATUS_ELIGIBLE;
    if (!ok) { fprintf(stderr, "ERROR: slot %d (#%03u %s) is not trade-evolution eligible\n", idx, v.dex, gen3_national_dex_name(v.dex)); return 1; }
    /* Mirrors EvolveScreen: evolve -> update stats -> dex -> save */
    evolve_party_pokemon_at_index(&sav, (uint8_t)idx);
    update_pkmn_stats(&sav, (uint8_t)idx);
    update_seen_owned_pkmn(&sav, (uint8_t)idx);
    if (save_savefile_to_path(&sav, (char *)path) != error_none) { fprintf(stderr, "ERROR: write failed\n"); return 1; }
    struct bills_pc_entry_view v2;
    bills_pc_get_view(&sav, BILLS_PC_LOC_PARTY, 0, idx, &v2);
    printf("evolved: #%03u %s -> #%03u %s (L%u)\n", v.dex, gen3_national_dex_name(v.dex), v2.dex, gen3_national_dex_name(v2.dex), v2.level);
    return 0;
}

/* One-way copy of a party or boxed mon between two same-generation Gen 1 or
 * Gen 2 saves: the source file is never written. Appends into dst box, then
 * pokedex_reconcile + flush. Only the PC portion of a party mon is copied;
 * the game rebuilds party stats when it is withdrawn. */
struct copy_cont
{
    uint8_t *count;
    uint8_t *species;     /* [capacity + 1], 0xFF-terminated */
    uint8_t *entries;     /* first pc_pokemon; party entries are strided */
    size_t pc_size;
    size_t stride;
    uint8_t *otnames;
    uint8_t *nicknames;
    size_t name_len;      /* bytes per otname/nickname slot (same for both) */
    int capacity;
};

static int copy_cont_init(PokemonSave *sav, enum bills_pc_location loc, int box, struct copy_cont *c)
{
    if (sav->save_generation_type == SAVE_GENERATION_1)
    {
        c->pc_size = sizeof(struct pksav_gen1_pc_pokemon);
        c->name_len = PKSAV_GEN1_POKEMON_OTNAME_STORAGE_LENGTH + 1;
        if (loc == BILLS_PC_LOC_PARTY)
        {
            struct pksav_gen1_pokemon_party *p = sav->save.gen1_save.pokemon_storage.p_party;
            c->count = &p->count; c->species = p->species;
            c->entries = (uint8_t *)p->party; c->stride = sizeof(p->party[0]);
            c->otnames = &p->otnames[0][0]; c->nicknames = &p->nicknames[0][0];
            c->capacity = PKSAV_GEN1_PARTY_NUM_POKEMON;
        }
        else
        {
            if (box < 0 || box >= PKSAV_GEN1_NUM_POKEMON_BOXES) return 0;
            struct pksav_gen1_pokemon_box *b = sav->save.gen1_save.pokemon_storage.pp_boxes[box];
            c->count = &b->count; c->species = b->species;
            c->entries = (uint8_t *)b->entries; c->stride = sizeof(b->entries[0]);
            c->otnames = &b->otnames[0][0]; c->nicknames = &b->nicknames[0][0];
            c->capacity = PKSAV_GEN1_BOX_NUM_POKEMON;
        }
        return 1;
    }
    if (sav->save_generation_type == SAVE_GENERATION_2)
    {
        c->pc_size = sizeof(struct pksav_gen2_pc_pokemon);
        c->name_len = PKSAV_GEN2_POKEMON_OTNAME_STORAGE_LENGTH + 1;
        if (loc == BILLS_PC_LOC_PARTY)
        {
            struct pksav_gen2_pokemon_party *p = sav->save.gen2_save.pokemon_storage.p_party;
            c->count = &p->count; c->species = p->species;
            c->entries = (uint8_t *)p->party; c->stride = sizeof(p->party[0]);
            c->otnames = &p->otnames[0][0]; c->nicknames = &p->nicknames[0][0];
            c->capacity = PKSAV_GEN2_PARTY_NUM_POKEMON;
        }
        else
        {
            if (box < 0 || box >= PKSAV_GEN2_NUM_POKEMON_BOXES) return 0;
            struct pksav_gen2_pokemon_box *b = sav->save.gen2_save.pokemon_storage.pp_boxes[box];
            c->count = &b->count; c->species = b->species;
            c->entries = (uint8_t *)b->entries; c->stride = sizeof(b->entries[0]);
            c->otnames = &b->otnames[0][0]; c->nicknames = &b->nicknames[0][0];
            c->capacity = PKSAV_GEN2_BOX_NUM_POKEMON;
        }
        return 1;
    }
    return 0;
}

/* Gen 1 -> Gen 2 conversion, mirroring swap_pkmn_at_index_between_saves_cross_gen
 * (the app's Time Capsule trade): species remapped to national dex, held item
 * derived from the Gen 1 catch-rate byte exactly like the real Time Capsule,
 * friendship base, no Pokerus, unknown caught data. */
static void convert_gen1_pc_to_gen2(const struct pksav_gen1_pc_pokemon *g1, uint8_t level, struct pksav_gen2_pc_pokemon *g2)
{
    memset(g2, 0, sizeof(*g2));
    g2->species = species_gen1_to_gen2[g1->species];
    uint8_t item_override = trade_catch_rate_to_item[g1->catch_rate];
    g2->held_item = item_override ? item_override : g1->catch_rate;
    memcpy(g2->moves, g1->moves, sizeof(g2->moves));
    g2->ot_id = g1->ot_id;
    memcpy(g2->exp, g1->exp, sizeof(g2->exp));
    g2->ev_hp = g1->ev_hp; g2->ev_atk = g1->ev_atk; g2->ev_def = g1->ev_def;
    g2->ev_spd = g1->ev_spd; g2->ev_spcl = g1->ev_spcl;
    g2->iv_data = g1->iv_data;
    memcpy(g2->move_pps, g1->move_pps, sizeof(g2->move_pps));
    g2->friendship = GEN2_FRIENDSHIP_BASE;
    g2->pokerus = 0;
    g2->caught_data = 0;
    g2->level = level;
}

static int cmd_copy(const char *src_path, const char *sloc_s, int sbox, int sidx, const char *dst_path, int dbox)
{
    PokemonSave s, d;
    enum bills_pc_location sloc;
    if (!parse_loc(sloc_s, &sloc))
    {
        fprintf(stderr, "ERROR: source location must be 'party' or 'box'\n");
        return 1;
    }
    if (!load_or_die(src_path, &s) || !load_or_die(dst_path, &d)) return 1;
    bool cross = (s.save_generation_type == SAVE_GENERATION_1 && d.save_generation_type == SAVE_GENERATION_2);
    if (!cross &&
        ((s.save_generation_type != SAVE_GENERATION_1 && s.save_generation_type != SAVE_GENERATION_2) ||
         d.save_generation_type != s.save_generation_type))
    {
        fprintf(stderr, "ERROR: copy supports Gen 1 -> Gen 1, Gen 2 -> Gen 2, or Gen 1 -> Gen 2 only\n");
        return 1;
    }
    bills_pc_normalize_current_box(&s);
    bills_pc_normalize_current_box(&d);
    struct copy_cont sc, dc;
    if (!copy_cont_init(&s, sloc, sbox, &sc) || !copy_cont_init(&d, BILLS_PC_LOC_BOX, dbox, &dc))
    {
        fprintf(stderr, "ERROR: box number out of range\n");
        return 1;
    }
    if (sidx < 0 || sidx >= *sc.count)
    {
        fprintf(stderr, "ERROR: source %s %d slot %d is empty (count %u)\n", sloc_s, sbox, sidx, *sc.count);
        return 1;
    }
    if (*dc.count >= dc.capacity)
    {
        fprintf(stderr, "ERROR: destination box %d is full\n", dbox);
        return 1;
    }
    uint8_t n = *dc.count;
    /* A Gen 1 party mon's real level lives in party_data; the pc "box level"
     * byte can be stale or zero until the game deposits it. */
    uint8_t g1_level = 0;
    if (s.save_generation_type == SAVE_GENERATION_1)
    {
        const struct pksav_gen1_pc_pokemon *spc = (const struct pksav_gen1_pc_pokemon *)(sc.entries + sidx * sc.stride);
        g1_level = (sloc == BILLS_PC_LOC_PARTY)
            ? s.save.gen1_save.pokemon_storage.p_party->party[sidx].party_data.level
            : spc->level;
    }
    if (cross)
    {
        const struct pksav_gen1_pc_pokemon *spc = (const struct pksav_gen1_pc_pokemon *)(sc.entries + sidx * sc.stride);
        convert_gen1_pc_to_gen2(spc, g1_level, (struct pksav_gen2_pc_pokemon *)(dc.entries + n * dc.stride));
        dc.species[n] = species_gen1_to_gen2[sc.species[sidx]];
    }
    else
    {
        dc.species[n] = sc.species[sidx];
        memcpy(dc.entries + n * dc.stride, sc.entries + sidx * sc.stride, dc.pc_size);
        if (s.save_generation_type == SAVE_GENERATION_1)
            ((struct pksav_gen1_pc_pokemon *)(dc.entries + n * dc.stride))->level = g1_level;
    }
    memcpy(dc.otnames + n * dc.name_len, sc.otnames + sidx * sc.name_len, dc.name_len);
    memcpy(dc.nicknames + n * dc.name_len, sc.nicknames + sidx * sc.name_len, dc.name_len);
    *dc.count = (uint8_t)(n + 1);
    dc.species[*dc.count] = 0xFF;
    pokedex_reconcile(&d);
    bills_pc_flush_current_box(&d);
    if (save_savefile_to_path(&d, (char *)dst_path) != error_none)
    {
        fprintf(stderr, "ERROR: write failed\n");
        return 1;
    }
    struct bills_pc_entry_view v;
    bills_pc_get_view(&d, BILLS_PC_LOC_BOX, dbox, n, &v);
    printf("copied: #%03u %s L%u nick=%s -> %s box %d slot %d\n",
           v.dex, gen3_national_dex_name(v.dex), v.level, v.nickname, dst_path, dbox, n);
    return 0;
}

/* Encode a species name the way the game stores an un-nicknamed mon's name:
 * uppercase, Gen 1/2 charset, 0x50-terminated and 0x50-padded to 11 bytes. */
static void encode_species_name(uint16_t dex, uint8_t out[11])
{
    const char *nm = gen3_national_dex_name(dex);
    memset(out, 0x50, 11);
    int o = 0;
    for (const char *p = nm; *p && o < 10; p++)
    {
        char c = *p;
        uint8_t b;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') b = (uint8_t)(0x80 + (c - 'A'));
        else if (c >= '0' && c <= '9') b = (uint8_t)(0xF6 + (c - '0'));
        else if (c == '\'') b = 0xE0;
        else if (c == '.') b = 0xE8;
        else if (c == ' ') continue;                       /* "Mr. Mime" -> MR.MIME */
        else if (c == '-' && (dex == 29 || dex == 32)) { p++; b = (dex == 29) ? 0xF5 : 0xEF; } /* Nidoran gender glyph */
        else if (c == '-') b = 0xE3;                       /* Ho-Oh */
        else continue;
        out[o++] = b;
    }
    out[o] = 0x50;
}

/* Reset every nickname in the party and all boxes to the species name. */
static int cmd_denick(const char *path)
{
    PokemonSave sav;
    if (!load_or_die(path, &sav)) return 1;
    if (sav.save_generation_type != SAVE_GENERATION_1 && sav.save_generation_type != SAVE_GENERATION_2)
    {
        fprintf(stderr, "ERROR: denick supports Gen 1 and Gen 2 saves only\n");
        return 1;
    }
    bills_pc_normalize_current_box(&sav);
    int changed = 0;
    int nb = bills_pc_num_boxes(&sav);
    for (int b = -1; b < nb; b++)
    {
        enum bills_pc_location loc = (b < 0) ? BILLS_PC_LOC_PARTY : BILLS_PC_LOC_BOX;
        struct copy_cont c;
        if (!copy_cont_init(&sav, loc, b < 0 ? 0 : b, &c)) continue;
        for (int i = 0; i < *c.count; i++)
        {
            struct bills_pc_entry_view v;
            bills_pc_get_view(&sav, loc, b < 0 ? 0 : b, i, &v);
            if (!v.occupied || v.dex == 0) continue;
            uint8_t want[11];
            encode_species_name(v.dex, want);
            uint8_t *nick = c.nicknames + i * c.name_len;
            /* compare up to and including the terminator only */
            size_t n = 0; while (n < 10 && want[n] != 0x50) n++;
            if (memcmp(nick, want, n + 1) == 0) continue;
            printf("renamed %s %d slot %d: #%03u %s  %s -> %s\n",
                   b < 0 ? "party" : "box", b < 0 ? 0 : b, i, v.dex, gen3_national_dex_name(v.dex), v.nickname, gen3_national_dex_name(v.dex));
            memcpy(nick, want, 11);
            changed++;
        }
    }
    bills_pc_flush_current_box(&sav);
    if (changed && save_savefile_to_path(&sav, (char *)path) != error_none)
    {
        fprintf(stderr, "ERROR: write failed\n");
        return 1;
    }
    printf("denick: %d nickname(s) reset\n", changed);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "list") == 0)
        return cmd_list(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "dex") == 0)
        return cmd_dex(argv[2]);
    if (argc >= 3 && strcmp(argv[1], "elig") == 0)
        return cmd_elig(argv[2]);
    if (argc == 9 && strcmp(argv[1], "move") == 0)
        return cmd_move(argv[2], &argv[3]);
    if (argc == 8 && strcmp(argv[1], "copy") == 0)
        return cmd_copy(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), argv[6], atoi(argv[7]));
    if (argc == 7 && strcmp(argv[1], "copy") == 0) /* legacy box-only form */
        return cmd_copy(argv[2], "box", atoi(argv[3]), atoi(argv[4]), argv[5], atoi(argv[6]));
    if (argc == 3 && strcmp(argv[1], "denick") == 0)
        return cmd_denick(argv[2]);
    if (argc == 4 && strcmp(argv[1], "evolve") == 0)
        return cmd_evolve(argv[2], atoi(argv[3]));
    if (argc == 6 && strcmp(argv[1], "trade") == 0)
        return cmd_trade(argv[2], atoi(argv[3]), argv[4], atoi(argv[5]));
    fprintf(stderr,
            "usage:\n"
            "  pkcli list <save>\n"
            "  pkcli dex <save>\n"
            "  pkcli move <save> <party|box> <box#> <idx> <party|box> <box#> <idx>\n"
            "  pkcli trade <save1> <partyIdx1> <save2> <partyIdx2>\n"
            "  pkcli copy <src> <party|box> <box#> <idx> <dst> <dstBox#>   one-way; Gen 1, Gen 2, or Gen 1 -> Gen 2\n"
            "  pkcli evolve <save> <partyIdx>\n"
            "  pkcli elig <save>\n"
            "  pkcli denick <save>                     reset every nickname to the species name\n");
    return 2;
}

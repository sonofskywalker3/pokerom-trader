/*
 * Regression tests for the 2026-11-09 review fixes. Every case works on an
 * in-memory load or on tests/_tmp_bf_* copies; the fixtures themselves are
 * never written. Personal saves are gitignored, so cases SKIP when absent.
 *
 *  #1  Evolve with no / a bad party index must not write anything.
 *  #2  HG/SS saves get the box-changed mask (storage+0x12004 = 0x3FFFF).
 *  #3  FRLG Pokedex seen copy B lives at section 1 + 0x5F8.
 *  #4  Two-save writes are all-or-nothing; #7 stale files are refused.
 *  #6  Backups are a byte copy of the on-disk file, kept once per session.
 *  #8  Gen 1 -> 3 uses the party level, not the stale box level.
 *  #9  A Gen 2 egg doesn't set dex bit 253.
 *  #10 Gen 3 ability bit follows Gen 3 (not Gen 4) ability data.
 *  #11 PP Ups survive Gen 1/2 -> 3; #12 mail byte is 0xFF.
 *  #13 Gen 3 eggs aren't dex-registered.  #14/#15 trade-evo items + Everstone.
 *  #17/#18 Gen 2 -> 1 held item / Special stats.  #20 Gen 4 blank slots.
 *  #21 Stat Exp cap, Gen 1 PC keeps HP.  Torn Gen 3 slot isn't picked.
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen_transfer.h"
#include "gen3_stats.h"
#include "gen3_species.h"
#include "gen4_stats.h"
#include "gen4_save.h"
#include "gen4_pkmn.h"
#include "../deps/pksav/lib/gba/save_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return 1; } } while (0)

static const char *BLUE = "saves/Pokemon - Blue Version.sav";
static const char *CRYSTAL = "saves/Pokemon - Crystal Version.sav";
static const char *EMERALD = "saves/refs/baseline/pokemon_emerald.sav";
static const char *FIRERED = "saves/refs/baseline/pokemon_firered.sav";
static const char *HEARTGOLD = "saves/gen4/heartgold.sav";
static const char *DIAMOND = "saves/gen4/diamond.sav";

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static uint8_t *slurp(const char *p, size_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)sz);
    *n = fread(b, 1, (size_t)sz, f);
    fclose(f);
    return b;
}

static int spit(const char *p, const uint8_t *b, size_t n)
{
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    size_t w = fwrite(b, 1, n, f);
    fclose(f);
    return w == n;
}

static int copy_file(const char *src, const char *dst)
{
    size_t n;
    uint8_t *b = slurp(src, &n);
    if (!b) return 0;
    int ok = spit(dst, b, n);
    free(b);
    return ok;
}

static int same_file(const char *p, const uint8_t *b, size_t n)
{
    size_t m;
    uint8_t *c = slurp(p, &m);
    int same = c && m == n && memcmp(b, c, n) == 0;
    free(c);
    return same;
}

static void free_save(PokemonSave *s)
{
    switch (s->save_generation_type)
    {
    case SAVE_GENERATION_1: pksav_gen1_free_save(&s->save.gen1_save); break;
    case SAVE_GENERATION_2: pksav_gen2_free_save(&s->save.gen2_save); break;
    case SAVE_GENERATION_3: pksav_gba_free_save(&s->save.gba_save); break;
    case SAVE_GENERATION_4: gen4_free_save(&s->save.gen4_save); break;
    default: break;
    }
}

/* ---- #1 ---- */
static int test_evolve_bad_index(void)
{
    PokemonSave s;
    load_savefile_from_path(EMERALD, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_3, "load emerald");
    struct pksav_gba_save_internal *in = s.save.gba_save.p_internal;
    size_t n = sizeof(in->unshuffled_save_slot);
    uint8_t *before = malloc(n);
    memcpy(before, &in->unshuffled_save_slot, n);
    uint32_t count = s.save.gba_save.pokemon_storage.p_party->count;

    CHECK(!evolve_party_pokemon_at_index(&s, 255), "evolve(255) refused");
    CHECK(!evolve_party_pokemon_at_index(&s, (uint8_t)count), "evolve(empty slot) refused");
    update_pkmn_stats(&s, 255);
    CHECK(update_seen_owned_pkmn(&s, 255) != error_none, "dex update(255) refused");
    CHECK(check_trade_evolution_gen3(&s, 255) == E_EVO_STATUS_NOT_ELIGIBLE, "check(255) not eligible");
    CHECK(memcmp(before, &in->unshuffled_save_slot, n) == 0, "save buffer untouched by bad-index evolve");
    /* a non-evolving party mon (Combusken) is refused too */
    CHECK(!evolve_party_pokemon_at_index(&s, 0), "ineligible mon refused");
    CHECK(memcmp(before, &in->unshuffled_save_slot, n) == 0, "save buffer untouched by ineligible evolve");
    free(before);
    free_save(&s);

    if (exists(BLUE))
    {
        PokemonSave b;
        load_savefile_from_path(BLUE, &b);
        struct pksav_gen1_pokemon_party *p = b.save.gen1_save.pokemon_storage.p_party;
        uint8_t sp0 = p->species[0];
        CHECK(!evolve_party_pokemon_at_index(&b, 200), "gen1 evolve(200) refused");
        p->species[0] = 0xBE; /* internal index past the 148-entry pair table */
        CHECK(check_trade_evolution_gen1(&b, 0) == E_EVO_STATUS_NOT_ELIGIBLE, "gen1 index >= 148 not eligible (no OOB read)");
        p->species[0] = sp0;
        free_save(&b);
    }
    printf("evolve_bad_index PASS\n");
    return 0;
}

/* ---- #2 ---- */
static int test_hgss_box_flags(void)
{
    if (!exists(HEARTGOLD)) { printf("SKIP hgss_box_flags\n"); return 0; }
    const char *tmp = "tests/_tmp_bf_hgss.sav";
    CHECK(copy_file(HEARTGOLD, tmp), "copy hgss");
    PokemonSave s;
    load_savefile_from_path(tmp, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_4, "load hgss");
    uint32_t base = s.save.gen4_save.storage_base;
    uint32_t size = s.save.gen4_save.layout.storage_size;
    CHECK(save_savefile_to_path(&s, (char *)tmp) == error_none, "write hgss");
    free_save(&s);
    size_t n;
    uint8_t *b = slurp(tmp, &n);
    const uint8_t *f = b + base + 0x12004;
    CHECK(f[0] == 0xFF && f[1] == 0xFF && f[2] == 0x03 && f[3] == 0x00, "box-changed mask = 0x3FFFF");
    uint16_t crc = gen4_crc16_ccitt(b + base, size - 0x10);
    CHECK((b[base + size - 2] | (b[base + size - 1] << 8)) == crc, "storage CRC still valid");
    free(b);
    remove(tmp);
    printf("hgss_box_flags PASS\n");
    return 0;
}

/* ---- #3 ---- */
static int test_frlg_seen_b(void)
{
    PokemonSave s;
    load_savefile_from_path(FIRERED, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_3, "load firered");
    const struct pksav_gba_pokedex *d = &s.save.gba_save.pokedex;
    CHECK(memcmp(d->p_seenA, d->p_seenB, 49) == 0, "FRLG seen B (sec1 0x5F8) mirrors seen A");
    CHECK(memcmp(d->p_seenA, d->p_seenC, 49) == 0, "FRLG seen C mirrors seen A");
    struct pksav_gba_save_internal *in = s.save.gba_save.p_internal;
    CHECK(d->p_seenB == &in->unshuffled_save_slot.section1.data8[0x5F8], "seen B pointer at section 1 + 0x5F8");
    free_save(&s);
    printf("frlg_seen_b PASS\n");
    return 0;
}

/* ---- #4 / #7 ---- */
static int test_atomic_and_stale(void)
{
    const char *a = "tests/_tmp_bf_a.sav", *b = "tests/_tmp_bf_b.sav";
    CHECK(copy_file(EMERALD, a) && copy_file(FIRERED, b), "copy fixtures");
    PokemonSave sa, sb;
    load_savefile_from_path(a, &sa);
    load_savefile_from_path(b, &sb);
    size_t na, nb;
    uint8_t *ba = slurp(a, &na), *bb = slurp(b, &nb);

    /* Second target can't be written: neither file may change. */
    CHECK(save_two_savefiles(&sa, (char *)a, &sb, (char *)"tests/_no_such_dir/x.sav") != error_none, "pair write to a bad path fails");
    CHECK(same_file(a, ba, na), "first file untouched when the pair fails");

    /* Someone else (the emulator) writes b: refuse, leave both alone. */
    bb[0x10] ^= 0xFF;
    CHECK(spit(b, bb, nb), "external write");
    CHECK(save_file_changed_on_disk(&sb, b), "change detected");
    CHECK(save_savefile_to_path(&sb, (char *)b) == error_save_stale, "single write refused when stale");
    CHECK(save_two_savefiles(&sa, (char *)a, &sb, (char *)b) == error_save_stale, "pair write refused when one is stale");
    CHECK(same_file(a, ba, na) && same_file(b, bb, nb), "both files untouched after a stale refusal");
    free_save(&sb);

    /* Reloaded: writes go through, and our own write doesn't look stale. */
    load_savefile_from_path(b, &sb);
    CHECK(save_two_savefiles(&sa, (char *)a, &sb, (char *)b) == error_none, "pair write after reload");
    CHECK(!save_file_changed_on_disk(&sa, a) && !save_file_changed_on_disk(&sb, b), "own write re-stamped");
    CHECK(save_savefile_to_path(&sa, (char *)a) == error_none, "second write of our own file ok");
    CHECK(!exists("tests/_tmp_bf_a.sav.tmp") && !exists("tests/_tmp_bf_a.sav.old"), "no temp files left");
    PokemonSave check;
    load_savefile_from_path(a, &check);
    CHECK(check.save_generation_type == SAVE_GENERATION_3, "committed file is a valid save");
    free_save(&check);
    free_save(&sa);
    free_save(&sb);
    free(ba);
    free(bb);
    remove(a);
    remove(b);
    printf("atomic_and_stale PASS\n");
    return 0;
}

/* ---- #6 ---- */
static int test_backup_is_file_copy(void)
{
    const char *p = "tests/_tmp_bf_bak.sav", *bak = "tests/_tmp_bf_bak.sav_bak";
    remove(bak);
    CHECK(copy_file(EMERALD, p), "copy");
    size_t n;
    uint8_t *orig = slurp(p, &n);
    PokemonSave s;
    load_savefile_from_path(p, &s);
    s.save.gba_save.pokemon_storage.p_party->party[0].pc_data.blocks.growth.held_item = 1; /* in-memory edit */
    CHECK(create_backup_save(&s, (char *)p), "backup");
    CHECK(same_file(bak, orig, n), "backup = on-disk bytes, not the edited buffer");
    CHECK(save_savefile_to_path(&s, (char *)p) == error_none, "write edit");
    CHECK(create_backup_save(&s, (char *)p), "second backup");
    CHECK(same_file(bak, orig, n), "second operation keeps the pre-session backup");
    free_save(&s);
    free(orig);
    remove(p);
    remove(bak);
    printf("backup_is_file_copy PASS\n");
    return 0;
}

/* ---- #8 / #10 / #11 / #12 ---- */
static int gen1_to_emerald(uint8_t species, uint16_t ivs, uint8_t pp0, uint8_t box_level,
                           struct pksav_gba_party_pokemon *out)
{
    PokemonSave g1, em;
    load_savefile_from_path(BLUE, &g1);
    load_savefile_from_path(EMERALD, &em);
    struct pksav_gen1_pokemon_party *p = g1.save.gen1_save.pokemon_storage.p_party;
    if (species)
    {
        p->species[1] = species;
        p->party[1].pc_data.species = species;
    }
    p->party[1].pc_data.iv_data = ivs;
    p->party[1].pc_data.move_pps[0] = pp0;
    p->party[1].pc_data.level = box_level; /* stale box level */
    uint32_t c0 = em.save.gba_save.pokemon_storage.p_party->count;
    pksavhelper_error e = transfer_pkmn_to_gen3(&g1, 1, &em);
    if (e == error_none)
        *out = em.save.gba_save.pokemon_storage.p_party->party[c0];
    free_save(&g1);
    free_save(&em);
    return e == error_none;
}

static int test_gen1_to_gen3(void)
{
    if (!exists(BLUE)) { printf("SKIP gen1_to_gen3\n"); return 0; }
    PokemonSave g1;
    load_savefile_from_path(BLUE, &g1);
    struct pksav_gen1_party_pokemon art = g1.save.gen1_save.pokemon_storage.p_party->party[1];
    free_save(&g1);
    uint8_t real_level = art.party_data.level; /* Articuno L51 */

    struct pksav_gba_party_pokemon m;
    CHECK(gen1_to_emerald(0, art.pc_data.iv_data, (uint8_t)(art.pc_data.move_pps[0] | 0xC0), 1, &m), "transfer Articuno");
    CHECK(m.party_data.level == real_level, "#8 party level %u kept (got %u; box byte said 1)", real_level, m.party_data.level);
    int lv = gen3_level_from_exp(pksav_littleendian32(m.pc_data.blocks.growth.exp), gen3_growth_rate_for_dex(144));
    CHECK(lv == real_level, "#8 EXP matches the level");
    CHECK((m.pc_data.blocks.growth.pp_up & 3) == 3, "#11 3 PP Ups on move 1 carried");
    CHECK(m.party_data.pokerus_time == 0xFF, "#12 mail byte 0xFF (no mail)");

    /* #10: Pidgey / Hypno have one ability in Gen 3: the bit must stay clear
     * even when the fabricated PID is odd. Try DV sets until one gives an odd PID. */
    const uint8_t species[2] = {SI_PIDGEY, SI_HYPNO};
    for (int k = 0; k < 2; k++)
    {
        int odd_seen = 0;
        for (uint16_t ivs = 0x1111; ivs < 0x1111 + 40 && !odd_seen; ivs++)
        {
            CHECK(gen1_to_emerald(species[k], ivs, art.pc_data.move_pps[0], art.pc_data.level, &m), "transfer species %u", species[k]);
            uint32_t pid = pksav_littleendian32(m.pc_data.personality);
            uint32_t iv = pksav_littleendian32(m.pc_data.blocks.misc.iv_egg_ability);
            CHECK(!(iv >> 31), "#10 single-ability species got the ability bit");
            odd_seen = pid & 1;
        }
        CHECK(odd_seen, "found an odd PID to exercise the ability bit");
    }
    CHECK(!gen3_species_uses_ability_bit(16) && !gen3_species_uses_ability_bit(97) && !gen3_species_uses_ability_bit(133) &&
          !gen3_species_uses_ability_bit(52) && !gen3_species_uses_ability_bit(123) && !gen3_species_uses_ability_bit(137),
          "#10 Pidgey/Hypno/Eevee/Meowth/Scyther/Porygon: one Gen 3 ability");
    CHECK(gen3_species_uses_ability_bit(74) && gen3_species_uses_ability_bit(210) && gen3_species_uses_ability_bit(329) &&
          gen3_species_uses_ability_bit(330), "#10 Geodude two abilities; Granbull/Vibrava/Flygon follow PID");
    CHECK(gen3_ability_id(97, true) == 15 && gen3_ability_id(74, true) == 5 && gen3_ability_id(74, false) == 69,
          "#10 Gen 3 ability ids (Hypno Insomnia, Geodude Sturdy/Rock Head)");
    printf("gen1_to_gen3 PASS\n");
    return 0;
}

/* ---- #9 ---- */
static int test_gen2_egg_dex(void)
{
    if (!exists(CRYSTAL)) { printf("SKIP gen2_egg_dex\n"); return 0; }
    PokemonSave s;
    load_savefile_from_path(CRYSTAL, &s);
    struct pksav_gen2_save *g = &s.save.gen2_save;
    g->pokemon_storage.p_party->species[4] = 0xFD; /* egg in the species list */
    pksav_set_pokedex_bit(g->pokedex_lists.p_seen, 253, false);
    pksav_set_pokedex_bit(g->pokedex_lists.p_owned, 253, false);
    uint8_t seen_before[32], owned_before[32];
    memcpy(seen_before, g->pokedex_lists.p_seen, 32);
    memcpy(owned_before, g->pokedex_lists.p_owned, 32);
    CHECK(update_seen_owned_pkmn(&s, 4) == error_none, "egg dex update is a no-op");
    CHECK(memcmp(seen_before, g->pokedex_lists.p_seen, 32) == 0 && memcmp(owned_before, g->pokedex_lists.p_owned, 32) == 0,
          "egg sets no dex bits (no bit 253)");
    free_save(&s);
    printf("gen2_egg_dex PASS\n");
    return 0;
}

/* ---- #13 ---- */
static int test_gen3_egg_dex(void)
{
    PokemonSave s;
    load_savefile_from_path(EMERALD, &s);
    struct pksav_gba_pc_pokemon *pc = &s.save.gba_save.pokemon_storage.p_party->party[3].pc_data;
    bool seen, owned;
    pokedex_get_entry(&s, 150, &seen, &owned);
    CHECK(!owned, "fixture: Mewtwo not owned");
    pc->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(150));
    pc->blocks.misc.iv_egg_ability = pksav_littleendian32(pksav_littleendian32(pc->blocks.misc.iv_egg_ability) | PKSAV_GBA_POKEMON_EGG_MASK);
    update_seen_owned_pkmn(&s, 3);
    pokedex_reconcile(&s);
    pokedex_get_entry(&s, 150, &seen, &owned);
    CHECK(!seen && !owned, "#13 egg not registered");
    pc->blocks.misc.iv_egg_ability = pksav_littleendian32(pksav_littleendian32(pc->blocks.misc.iv_egg_ability) & ~PKSAV_GBA_POKEMON_EGG_MASK);
    pokedex_reconcile(&s);
    pokedex_get_entry(&s, 150, &seen, &owned);
    CHECK(seen && owned, "hatched mon is registered");
    free_save(&s);
    printf("gen3_egg_dex PASS\n");
    return 0;
}

/* ---- #14 / #15 ---- */
static int test_trade_evo_items(void)
{
    PokemonSave s;
    load_savefile_from_path(EMERALD, &s);
    struct pksav_gba_pc_pokemon *pc = &s.save.gba_save.pokemon_storage.p_party->party[0].pc_data;
    pc->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(95)); /* Onix */
    pc->blocks.growth.held_item = pksav_littleendian16(199);
    CHECK(check_trade_evolution_gen3(&s, 0) == E_EVO_STATUS_ELIGIBLE, "Onix + Metal Coat (199) eligible");
    pc->blocks.growth.held_item = pksav_littleendian16(233);
    CHECK(check_trade_evolution_gen3(&s, 0) == E_EVO_STATUS_MISSING_ITEM, "old id 233 is not a Metal Coat");
    pc->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(64)); /* Kadabra */
    pc->blocks.growth.held_item = pksav_littleendian16(195);
    CHECK(check_trade_evolution_gen3(&s, 0) == E_EVO_STATUS_NOT_ELIGIBLE, "Everstone (195) blocks Gen 3");
    pc->blocks.growth.species = pksav_littleendian16(gen3_national_to_internal(366)); /* Clamperl */
    pc->blocks.growth.held_item = pksav_littleendian16(193);
    CHECK(check_trade_evolution_gen3(&s, 0) == E_EVO_STATUS_ELIGIBLE, "Clamperl + DeepSeaScale (193)");
    CHECK(evolve_party_pokemon_at_index(&s, 0), "evolve Clamperl");
    CHECK(gen3_internal_to_national(pksav_littleendian16(pc->blocks.growth.species)) == 368, "-> Gorebyss");
    free_save(&s);

    if (exists(CRYSTAL))
    {
        PokemonSave c;
        load_savefile_from_path(CRYSTAL, &c);
        struct pksav_gen2_pokemon_party *p = c.save.gen2_save.pokemon_storage.p_party;
        p->species[4] = ONIX;
        p->party[4].pc_data.species = ONIX;
        p->party[4].pc_data.held_item = METAL_COAT;
        CHECK(check_trade_evolution_gen2(&c, 4) == E_EVO_STATUS_ELIGIBLE, "Gen 2 Onix + Metal Coat");
        p->species[4] = KADABRA;
        p->party[4].pc_data.species = KADABRA;
        p->party[4].pc_data.held_item = 0x70;
        CHECK(check_trade_evolution_gen2(&c, 4) == E_EVO_STATUS_NOT_ELIGIBLE, "Gen 2 Everstone blocks");
        free_save(&c);
    }
    printf("trade_evo_items PASS\n");
    return 0;
}

/* ---- #17 / #18 ---- */
static int test_gen2_to_gen1_trade(void)
{
    if (!exists(BLUE) || !exists(CRYSTAL)) { printf("SKIP gen2_to_gen1_trade\n"); return 0; }
    PokemonSave b, c;
    load_savefile_from_path(BLUE, &b);
    load_savefile_from_path(CRYSTAL, &c);
    c.save.gen2_save.pokemon_storage.p_party->party[4].pc_data.held_item = 0x52; /* Golduck holds an item */
    CHECK(swap_pkmn_at_index_between_saves_cross_gen(&b, &c, 0, 4) == error_none, "cross-gen swap");
    CHECK(b.save.gen1_save.pokemon_storage.p_party->party[0].pc_data.catch_rate == 0x52, "#17 held item kept in the catch-rate byte");
    update_pkmn_stats(&b, 0);
    update_pkmn_stats(&c, 4);
    struct pksav_gen2_party_pokemon *j = &c.save.gen2_save.pokemon_storage.p_party->party[4];
    CHECK(j->pc_data.species == 135 && j->party_data.spatk != j->party_data.spdef, "#18 Jolteon Sp.Atk/Sp.Def recomputed (110/95 base)");
    free_save(&b);
    free_save(&c);
    printf("gen2_to_gen1_trade PASS\n");
    return 0;
}

/* ---- #20 ---- */
static int test_gen4_blank_slots(void)
{
    if (!exists(DIAMOND)) { printf("SKIP gen4_blank_slots\n"); return 0; }
    PokemonSave s;
    load_savefile_from_path(DIAMOND, &s);
    struct gen4_save *g = &s.save.gen4_save;
    int eb = -1, es = -1;
    for (int b = 0; b < GEN4_NUM_BOXES && eb < 0; b++)
        for (int i = 0; i < GEN4_BOX_SLOTS; i++)
        {
            uint8_t d[GEN4_PK4_STORED_SIZE];
            gen4_pk4_decrypt(gen4_box_slot_raw(g, b, i), d, false);
            if (gen4_pk4_species(d) == 0) { eb = b; es = i; break; }
        }
    CHECK(eb >= 0, "found an empty box slot");
    uint8_t zero[GEN4_PK4_PARTY_SIZE] = {0}, blank_party[GEN4_PK4_PARTY_SIZE], blank_box[GEN4_PK4_PARTY_SIZE];
    gen4_pk4_encrypt(zero, blank_party, true);
    gen4_pk4_encrypt(zero, blank_box, false);
    int n = gen4_party_count(g);
    struct bills_pc_slot party_last = {BILLS_PC_LOC_PARTY, 0, n - 1}, box = {BILLS_PC_LOC_BOX, eb, es};
    CHECK(bills_pc_move_pkmn(&s, party_last, box) == error_none, "deposit");
    CHECK(memcmp(gen4_party_slot_raw_mut(g, n - 1), blank_party, GEN4_PK4_PARTY_SIZE) == 0, "vacated party slot = encrypted blank");
    struct bills_pc_slot party_end = {BILLS_PC_LOC_PARTY, 0, n - 1};
    CHECK(bills_pc_move_pkmn(&s, box, party_end) == error_none, "withdraw");
    CHECK(memcmp(gen4_box_slot_raw(g, eb, es), blank_box, GEN4_PK4_STORED_SIZE) == 0, "vacated box slot = encrypted blank");
    free_save(&s);
    printf("gen4_blank_slots PASS\n");
    return 0;
}

/* ---- Gen 4 block shuffle (found while verifying Pal Park with PKHeX) ----
 * Every retail box mon must decode to a real species whose ability byte is
 * one of that species' Gen 4 abilities. The old shuffle table was wrong for
 * 14 of the 24 orders, so about half of these decoded as garbage. (One
 * Platinum fixture mon is ILLEGAL in PKHeX too; allow one outlier there.) */
static int test_gen4_shuffle_retail(void)
{
    const char *saves[5] = {"saves/gen4/diamond.sav", "saves/gen4/pearl.sav", "saves/gen4/platinum.sav",
                            "saves/gen4/heartgold.sav", "saves/gen4/soulsilver.sav"};
    int checked = 0;
    for (int f = 0; f < 5; f++)
    {
        if (!exists(saves[f])) continue;
        struct gen4_save s;
        CHECK(gen4_load_save_from_file(saves[f], &s) == PKSAV_ERROR_NONE, "load %s", saves[f]);
        int bad = 0, n = 0;
        for (int b = 0; b < GEN4_NUM_BOXES; b++)
            for (int i = 0; i < GEN4_BOX_SLOTS; i++)
            {
                uint8_t d[GEN4_PK4_STORED_SIZE];
                gen4_pk4_decrypt(gen4_box_slot_raw(&s, b, i), d, false);
                if (gen4_pk4_pid(d) == 0 && gen4_pk4_species(d) == 0) continue;
                uint16_t sp = gen4_pk4_species(d);
                uint8_t ab = d[0x15];
                n++;
                if (!(sp >= 1 && sp <= 493 && (ab == gen4_ability_id(sp, 0) || ab == gen4_ability_id(sp, 1)))) bad++;
            }
        gen4_free_save(&s);
        CHECK(bad <= (f == 2 ? 1 : 0), "%s: %d of %d box mons mis-decoded", saves[f], bad, n);
        checked++;
    }
    printf("gen4_shuffle_retail PASS (%d saves)\n", checked);
    return 0;
}

/* ---- #21 ---- */
static int test_small_fixes(void)
{
    CHECK(calculate_stat(100, 100, 15, 65535) == 298, "Stat Exp sqrt capped at 255 (stat)");
    CHECK(calculate_hp(100, 100, 15, 65535) == 403, "Stat Exp sqrt capped at 255 (HP)");
    if (exists(BLUE))
    {
        PokemonSave b;
        load_savefile_from_path(BLUE, &b);
        struct pksav_gen1_pokemon_party *p = b.save.gen1_save.pokemon_storage.p_party;
        p->party[0].pc_data.current_hp = pksav_bigendian16(7);
        p->party[0].pc_data.condition = 0x08; /* poisoned */
        struct bills_pc_slot a = {BILLS_PC_LOC_PARTY, 0, 0}, c = {BILLS_PC_LOC_PARTY, 0, 1};
        CHECK(bills_pc_move_pkmn(&b, a, c) == error_none, "gen1 party swap");
        CHECK(pksav_bigendian16(p->party[1].pc_data.current_hp) == 7 && p->party[1].pc_data.condition == 0x08,
              "Gen 1 party move keeps HP and status");
        free_save(&b);
    }
    printf("small_fixes PASS\n");
    return 0;
}

/* ---- torn Gen 3 slot ---- */
static int test_torn_gen3_slot(void)
{
    size_t n;
    uint8_t *buf = slurp(FIRERED, &n);
    CHECK(buf && n >= 0x1C000, "read firered");
    /* Slot 1 is a half-written save; bump the counter of ONE of its sectors
     * past slot 0's, as a save interrupted after that sector would. */
    for (int sec = 0; sec < 14; sec++)
    {
        uint8_t *f = buf + 0xE000 + sec * 0x1000 + 0xFF4;
        if (f[0] == 0) /* section id 0 */
        {
            uint32_t idx = 381;
            memcpy(f + 8, &idx, 4);
        }
    }
    struct pksav_gba_save s;
    CHECK(pksav_gba_load_save_from_buffer(buf, n, &s) == PKSAV_ERROR_NONE, "load torn buffer");
    struct pksav_gba_save_internal *in = s.p_internal;
    CHECK(in->is_save_from_first_slot, "intact slot 0 chosen over the torn slot 1");
    pksav_gba_free_save(&s);
    free(buf);
    printf("torn_gen3_slot PASS\n");
    return 0;
}

int main(void)
{
    if (test_evolve_bad_index()) return 1;
    if (test_hgss_box_flags()) return 1;
    if (test_frlg_seen_b()) return 1;
    if (test_atomic_and_stale()) return 1;
    if (test_backup_is_file_copy()) return 1;
    if (test_gen1_to_gen3()) return 1;
    if (test_gen2_egg_dex()) return 1;
    if (test_gen3_egg_dex()) return 1;
    if (test_trade_evo_items()) return 1;
    if (test_gen2_to_gen1_trade()) return 1;
    if (test_gen4_blank_slots()) return 1;
    if (test_gen4_shuffle_retail()) return 1;
    if (test_small_fixes()) return 1;
    if (test_torn_gen3_slot()) return 1;
    printf("bugfix_test: all cases passed (or skipped)\n");
    return 0;
}

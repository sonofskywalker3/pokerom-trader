/*
 * Regression tests for the second review pass. Every case works on an
 * in-memory load, a synthetic buffer, or tests/_tmp_rv_* copies; the fixtures
 * themselves are never written.
 *
 *  - Two-file writes refuse one file named twice (it used to delete it).
 *  - A failed second commit rolls the first back; a leftover .old from an
 *    interrupted save is kept as .interrupted, not deleted.
 *  - Gen 4: a newer block copy with a bad CRC loses to an intact older one.
 *  - PKSav: exported names are padded with the terminator (0xFF / 0x50), not
 *    0xFA / 0x00; loaders reject a buffer that isn't a save; Gen 3 flag ids
 *    past the flag table are refused; Gen 3 daycare points at the daycare.
 *  - Gen 2 -> 3 items X Special / TinyMushroom / EnergyPowder carry over.
 *  - Pal Park swaps the square and triangle markings into Gen 4 order.
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen2_items.h"
#include "gen4_save.h"
#include "gen4_pkmn.h"
#include "gen4_transfer.h"
#include "../deps/pksav/lib/gba/save_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#endif

#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return 1; } } while (0)

static const char *EMERALD = "saves/refs/baseline/pokemon_emerald.sav";
static const char *FIRERED = "saves/refs/baseline/pokemon_firered.sav";

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
    return fclose(f) == 0 && w == n;
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

/* ---- two-file writes ---------------------------------------------------- */

static int test_same_file_refused(void)
{
    const char *a = "tests/_tmp_rv_same.sav";
    CHECK(copy_file(EMERALD, a), "copy fixture");
    size_t n;
    uint8_t *orig = slurp(a, &n);
    PokemonSave s1, s2;
    load_savefile_from_path(a, &s1);
    load_savefile_from_path(a, &s2);
    CHECK(save_two_savefiles(&s1, (char *)a, &s2, (char *)a) == error_save_same_file, "same path refused");
    CHECK(save_two_savefiles(&s1, (char *)a, &s2, (char *)"tests/../tests/_tmp_rv_same.sav") == error_save_same_file, "same file, different spelling refused");
    CHECK(save_two_savefiles(&s1, (char *)a, &s2, (char *)"TESTS\\_TMP_RV_SAME.SAV") == error_save_same_file, "same file, different case and slash refused");
    CHECK(same_file(a, orig, n), "file untouched");
    CHECK(!exists("tests/_tmp_rv_same.sav.tmp") && !exists("tests/_tmp_rv_same.sav.old"), "no side files");
    free_save(&s1);
    free_save(&s2);
    free(orig);
    remove(a);
    printf("same_file_refused PASS\n");
    return 0;
}

static int test_second_commit_failure_rolls_back(void)
{
#ifdef _WIN32
    const char *a = "tests/_tmp_rv_a.sav", *b = "tests/_tmp_rv_b.sav";
    CHECK(copy_file(EMERALD, a) && copy_file(FIRERED, b), "copy fixtures");
    size_t na, nb;
    uint8_t *ba = slurp(a, &na), *bb = slurp(b, &nb);
    PokemonSave sa, sb;
    load_savefile_from_path(a, &sa);
    load_savefile_from_path(b, &sb);
    /* An edit, so a successful commit of `a` would change its bytes. */
    sa.save.gba_save.pokemon_storage.p_party->party[0].pc_data.blocks.growth.held_item ^= 1;

    /* Hold b open without delete sharing (as an emulator might): the commit of
     * b can't rename it, so the already-committed a must be rolled back. */
    int fd = -1;
    CHECK(_sopen_s(&fd, b, _O_RDONLY | _O_BINARY, _SH_DENYWR, _S_IREAD) == 0, "hold b open");
    pksavhelper_error e = save_two_savefiles(&sa, (char *)a, &sb, (char *)b);
    _close(fd);
    CHECK(e == error_save_write_failed, "pair write fails cleanly (got %d)", e);
    CHECK(same_file(a, ba, na), "a rolled back to its original bytes");
    CHECK(same_file(b, bb, nb), "b untouched");
    CHECK(!exists("tests/_tmp_rv_a.sav.old") && !exists("tests/_tmp_rv_a.sav.rollback") &&
          !exists("tests/_tmp_rv_b.sav.tmp"), "no side files left");
    free_save(&sa);
    free_save(&sb);
    free(ba);
    free(bb);
    remove(a);
    remove(b);
    printf("second_commit_failure_rolls_back PASS\n");
#else
    printf("SKIP second_commit_failure_rolls_back (needs Windows share locks)\n");
#endif
    return 0;
}

static int test_leftover_old_is_kept(void)
{
    const char *a = "tests/_tmp_rv_old.sav", *old = "tests/_tmp_rv_old.sav.old";
    const char *kept = "tests/_tmp_rv_old.sav.interrupted";
    remove(kept);
    CHECK(copy_file(EMERALD, a), "copy fixture");
    const uint8_t marker[] = "pre-crash original";
    CHECK(spit(old, marker, sizeof(marker)), "plant leftover .old");
    PokemonSave s;
    load_savefile_from_path(a, &s);
    CHECK(save_savefile_to_path(&s, (char *)a) == error_none, "save");
    CHECK(same_file(kept, marker, sizeof(marker)), "leftover .old kept as .interrupted");
    CHECK(!exists(old), "no .old after a clean save");
    free_save(&s);
    remove(a);
    remove(kept);
    printf("leftover_old_is_kept PASS\n");
    return 0;
}

/* ---- Gen 4 block selection ---------------------------------------------- */

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Footer of a DP block: counter @-0x14, size @-0xC, magic @-0x8, CRC @-0x2
 * over everything but the last 0x14 bytes. */
static void dp_block(uint8_t *buf, uint32_t base, uint32_t size, uint32_t counter, int good_crc)
{
    uint8_t *end = buf + base + size;
    wr32le(end - 0x14, counter);
    wr32le(end - 0x0C, size);
    wr32le(end - 0x08, 0x20060623u);
    uint16_t crc = gen4_crc16_ccitt(buf + base, size - 0x14);
    if (!good_crc) crc ^= 0x5555;
    end[-2] = (uint8_t)crc;
    end[-1] = (uint8_t)(crc >> 8);
}

static uint8_t *synthetic_dp(int newer_general_ok, int newer_storage_ok)
{
    uint8_t *buf = calloc(1, GEN4_SAVE_SIZE);
    for (int p = 0; p < 2; p++)
    {
        uint32_t part = (uint32_t)p * GEN4_PARTITION_SIZE;
        buf[part + 0x10] = (uint8_t)(0xA0 + p); /* make the two copies differ */
        dp_block(buf, part, 0xC100, (uint32_t)(1 + p), p == 0 || newer_general_ok);
        dp_block(buf, part + 0xC100, 0x121E0, (uint32_t)(1 + p), p == 0 || newer_storage_ok);
    }
    return buf;
}

static int test_gen4_corrupt_newer_block(void)
{
    const char *p = "tests/_tmp_rv_dp.sav";
    struct gen4_save g;

    uint8_t *buf = synthetic_dp(1, 1);
    CHECK(spit(p, buf, GEN4_SAVE_SIZE), "write synthetic DP");
    CHECK(gen4_load_save_from_file(p, &g) == PKSAV_ERROR_NONE, "load synthetic DP");
    CHECK(g.general_base == GEN4_PARTITION_SIZE && g.storage_base == GEN4_PARTITION_SIZE + 0xC100,
          "both intact: newer copy wins");
    gen4_free_save(&g);
    free(buf);

    buf = synthetic_dp(0, 0);
    CHECK(spit(p, buf, GEN4_SAVE_SIZE), "write synthetic DP");
    CHECK(gen4_load_save_from_file(p, &g) == PKSAV_ERROR_NONE, "load synthetic DP");
    CHECK(g.general_base == 0 && g.storage_base == 0xC100, "newer copy with a bad CRC loses to the intact one");
    gen4_free_save(&g);
    free(buf);

    /* Saved in-game once: the games write the first save to partition 1 only. */
    buf = synthetic_dp(1, 1);
    memset(buf, 0, GEN4_PARTITION_SIZE);
    CHECK(spit(p, buf, GEN4_SAVE_SIZE), "write once-saved DP");
    CHECK(gen4_is_gen4_file(p), "once-saved file recognised");
    CHECK(gen4_load_save_from_file(p, &g) == PKSAV_ERROR_NONE, "once-saved file loads");
    CHECK(g.general_base == GEN4_PARTITION_SIZE, "uses the partition-1 copy");
    gen4_free_save(&g);
    free(buf);
    remove(p);
    printf("gen4_corrupt_newer_block PASS\n");
    return 0;
}

/* ---- PKSav -------------------------------------------------------------- */

static int test_name_padding(void)
{
    uint8_t b[10];
    pksav_gba_export_text("CELEBI", b, sizeof(b));
    for (int i = 6; i < 10; i++) CHECK(b[i] == 0xFF, "gba pad byte %d = %02X", i, b[i]);
    pksav_gen1_export_text("CELEBI", b, sizeof(b));
    for (int i = 6; i < 10; i++) CHECK(b[i] == 0x50, "gen1 pad byte %d = %02X", i, b[i]);
    pksav_gen2_export_text("CELEBI", b, sizeof(b));
    for (int i = 6; i < 10; i++) CHECK(b[i] == 0x50, "gen2 pad byte %d = %02X", i, b[i]);

    /* Full-length import: no terminator in the field. */
    uint8_t full[10];
    memset(full, 0xBB, sizeof(full)); /* "AAAAAAAAAA" */
    char out[16];
    memset(out, 0, sizeof(out));
    CHECK(pksav_gba_import_text(full, out, 10) == PKSAV_ERROR_NONE, "import full-length");
    CHECK(strncmp(out, "AAAAAAAAAA", 10) == 0, "full-length name imported");
    printf("name_padding PASS\n");
    return 0;
}

static int test_invalid_buffer_rejected(void)
{
    uint8_t *junk = malloc(0x8000);
    memset(junk, 0x5A, 0x8000); /* no checksum can match (all-zero would) */
    struct pksav_gen1_save g1;
    struct pksav_gen2_save g2;
    memset(&g1, 0, sizeof(g1));
    memset(&g2, 0, sizeof(g2));
    CHECK(pksav_gen1_load_save_from_buffer(junk, 0x8000, &g1) != PKSAV_ERROR_NONE, "gen1 junk rejected");
    CHECK(pksav_gen2_load_save_from_buffer(junk, 0x8000, &g2) != PKSAV_ERROR_NONE, "gen2 junk rejected");
    free(junk);
    printf("invalid_buffer_rejected PASS\n");
    return 0;
}

static int test_gba_flag_bounds_and_daycare(void)
{
    PokemonSave s;
    load_savefile_from_path(EMERALD, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_3, "load Emerald");
    struct pksav_gba_save *g = &s.save.gba_save;
    CHECK(pksav_gba_save_set_flag(g, 0x95F, true) == PKSAV_ERROR_NONE, "last Emerald flag ok");
    CHECK(pksav_gba_save_set_flag(g, 0x960, true) == PKSAV_ERROR_PARAM_OUT_OF_RANGE, "first var refused");
    bool v;
    CHECK(pksav_gba_save_get_flag(g, 0xFFFF, &v) == PKSAV_ERROR_PARAM_OUT_OF_RANGE, "huge id refused");

    struct pksav_gba_save_internal *in = g->p_internal;
    ptrdiff_t off = (const uint8_t *)g->pokemon_storage.p_daycare - in->unshuffled_save_slot.section4.data8;
    CHECK(off == 0x1B0, "Emerald daycare at section 4 + 0x1B0 (SaveBlock1 + 0x3030), got 0x%X", (unsigned)off);
    free_save(&s);
    printf("gba_flag_bounds_and_daycare PASS\n");
    return 0;
}

/* ---- transfers ---------------------------------------------------------- */

static int test_gen2_items(void)
{
    CHECK(gen2_item_to_gen3(0x35) == 79, "X Special");
    CHECK(gen2_item_to_gen3(0x56) == 103, "TinyMushroom");
    CHECK(gen2_item_to_gen3(0x79) == 30, "EnergyPowder");
    CHECK(gen2_item_to_gen3(0x92) == 200, "Leftovers still mapped");
    printf("gen2_items PASS\n");
    return 0;
}

static int test_pal_park_markings(void)
{
    const char *p = "tests/_tmp_rv_dp_dst.sav";
    uint8_t *buf = synthetic_dp(1, 1);
    CHECK(spit(p, buf, GEN4_SAVE_SIZE), "write synthetic DP");
    free(buf);

    PokemonSave src, dst;
    load_savefile_from_path(EMERALD, &src);
    load_savefile_from_path(p, &dst);
    CHECK(src.save_generation_type == SAVE_GENERATION_3 && dst.save_generation_type == SAVE_GENERATION_4, "load");

    struct pksav_gba_pokemon_party *party = src.save.gba_save.pokemon_storage.p_party;
    int sent = -1;
    for (int i = 0; i < (int)party->count && sent < 0; i++)
    {
        party->party[i].pc_data.markings = 0x02; /* Gen 3 square only */
        if (transfer_pkmn_gen3_to_gen4(&src, (uint8_t)i, &dst) == error_none)
            sent = i;
    }
    CHECK(sent >= 0, "some Emerald party mon can go to Gen 4");
    uint8_t dec[236];
    gen4_pk4_decrypt(gen4_party_slot_raw(&dst.save.gen4_save, gen4_party_count(&dst.save.gen4_save) - 1), dec, true);
    CHECK(dec[0x16] == 0x04, "Gen 3 square (bit 1) is Gen 4 square (bit 2), got %02X", dec[0x16]);
    free_save(&src);
    free_save(&dst);
    remove(p);
    printf("pal_park_markings PASS\n");
    return 0;
}

/* ---- trades and Bill's PC (Crystal / Emerald fixtures, in memory) ------- */

static const char *CRYSTAL = "saves/refs/baseline/pokemon_crystal.sav";

static int test_gen2_trade_keeps_symbol_names(void)
{
    PokemonSave a, b;
    load_savefile_from_path(CRYSTAL, &a);
    load_savefile_from_path(CRYSTAL, &b);
    CHECK(a.save_generation_type == SAVE_GENERATION_2, "load Crystal");
    /* "ABCDEFGHI" + male sign, and a 7-character OT ending in the female sign */
    const uint8_t nick[11] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0xEF, 0x50};
    const uint8_t ot[11] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0xF5, 0x50, 0x50, 0x50, 0x50};
    memcpy(a.save.gen2_save.pokemon_storage.p_party->nicknames[0], nick, 11);
    memcpy(a.save.gen2_save.pokemon_storage.p_party->otnames[0], ot, 11);
    CHECK(swap_pkmn_at_index_between_saves(&a, &b, 0, 1) == error_none, "trade");
    CHECK(memcmp(b.save.gen2_save.pokemon_storage.p_party->nicknames[1], nick, 11) == 0, "10-character nickname with a symbol kept");
    CHECK(memcmp(b.save.gen2_save.pokemon_storage.p_party->otnames[1], ot, 11) == 0, "OT name with a symbol kept");
    free_save(&a);
    free_save(&b);
    printf("gen2_trade_keeps_symbol_names PASS\n");
    return 0;
}

static int test_gen2_pc_party_rules(void)
{
    PokemonSave s;
    load_savefile_from_path(CRYSTAL, &s);
    struct pksav_gen2_pokemon_party *party = s.save.gen2_save.pokemon_storage.p_party;
    bills_pc_normalize_current_box(&s);
    int empty_box = -1;
    for (int b = 0; b < bills_pc_num_boxes(&s) && empty_box < 0; b++)
        if (bills_pc_container_count(&s, BILLS_PC_LOC_BOX, b) == 0) empty_box = b;
    CHECK(empty_box >= 0, "an empty box");
    /* The fixture's Mew holds Mail, which can't go in a box: take it off. */
    s.save.gen2_save.pokemon_storage.p_party_mail->party_mail[0].item_id = 0;

    /* Everyone but slot 0 has fainted: depositing slot 0 is refused. */
    for (int i = 1; i < party->count; i++) party->party[i].party_data.current_hp = 0;
    struct bills_pc_slot p0 = {BILLS_PC_LOC_PARTY, 0, 0}, box = {BILLS_PC_LOC_BOX, empty_box, 0};
    CHECK(bills_pc_move_pkmn(&s, p0, box) == error_swap_pkmn, "can't leave only fainted mons");
    /* Others are eggs instead: refused too. */
    for (int i = 1; i < party->count; i++) { party->party[i].party_data.current_hp = pksav_bigendian16(10); party->species[i] = 0xFD; }
    CHECK(bills_pc_move_pkmn(&s, p0, box) == error_swap_pkmn, "can't leave only eggs");
    struct bills_pc_entry_view v;
    bills_pc_get_view(&s, BILLS_PC_LOC_PARTY, 0, 1, &v);
    CHECK(v.is_egg && v.dex == 0, "a Gen 2 egg shows no dex number (was #253)");
    party->species[1] = party->party[1].pc_data.species; /* slot 1 can battle again */
    CHECK(bills_pc_move_pkmn(&s, p0, box) == error_none, "deposit allowed with a battler left");

    /* Reordering the party keeps HP instead of healing. */
    party->party[0].party_data.current_hp = pksav_bigendian16(1);
    struct bills_pc_slot pa = {BILLS_PC_LOC_PARTY, 0, 0}, pb = {BILLS_PC_LOC_PARTY, 0, 2};
    CHECK(bills_pc_move_pkmn(&s, pa, pb) == error_none, "party swap");
    CHECK(pksav_bigendian16(party->party[2].party_data.current_hp) == 1, "HP carried with the mon");

    /* Out-of-range slots are refused. */
    struct bills_pc_slot bad = {BILLS_PC_LOC_BOX, 0, 20}, badbox = {BILLS_PC_LOC_BOX, 14, 0};
    CHECK(bills_pc_move_pkmn(&s, p0, bad) == error_swap_pkmn && bills_pc_move_pkmn(&s, p0, badbox) == error_swap_pkmn,
          "Gen 2 box slot 20 / box 14 refused");
    free_save(&s);

    load_savefile_from_path(EMERALD, &s);
    struct bills_pc_slot e0 = {BILLS_PC_LOC_PARTY, 0, 0}, e30 = {BILLS_PC_LOC_BOX, 0, 30};
    CHECK(bills_pc_move_pkmn(&s, e0, e30) == error_swap_pkmn, "Gen 3 box slot 30 (would be box 1 slot 0) refused");
    free_save(&s);
    printf("gen2_pc_party_rules PASS\n");
    return 0;
}

int main(void)
{
    if (!exists(EMERALD) || !exists(FIRERED) || !exists(CRYSTAL))
    {
        printf("SKIP review_test (fixtures absent)\n");
        return 0;
    }
    if (test_same_file_refused()) return 1;
    if (test_second_commit_failure_rolls_back()) return 1;
    if (test_leftover_old_is_kept()) return 1;
    if (test_gen4_corrupt_newer_block()) return 1;
    if (test_name_padding()) return 1;
    if (test_invalid_buffer_rejected()) return 1;
    if (test_gba_flag_bounds_and_daycare()) return 1;
    if (test_gen2_items()) return 1;
    if (test_pal_park_markings()) return 1;
    if (test_gen2_trade_keeps_symbol_names()) return 1;
    if (test_gen2_pc_party_rules()) return 1;
    printf("review_test: all cases passed\n");
    return 0;
}

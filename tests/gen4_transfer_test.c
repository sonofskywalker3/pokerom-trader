/*
 * End-to-end test for the Gen 3 -> Gen 4 (Pal Park) transfer: drive the real
 * transfer_pkmn_gen3_to_gen4() with real saves, then re-parse the destination
 * through the Gen 4 codec and confirm every persistent field survived and the
 * Pal Park conversions were applied. Covers the party-append path (with stat
 * rebuild), the box-overflow path, and the egg/wrong-gen refusals.
 *
 * Build (from repo root, after building deps/pksav):
 *   gcc tests/gen4_transfer_test.c src/gen4_transfer.c src/gen4_save.c \
 *       src/gen4_pkmn.c src/gen4_stats.c src/gen3_species.c \
 *       -I include -I deps/pksav/include -I deps/pksav/build/include \
 *       -L deps/pksav/build/lib -lpksav -lm -o tests/gen4_transfer_test.exe
 *   ./tests/gen4_transfer_test.exe
 *
 * NOTE: the Gen 4 fixtures (saves/gen4/*.sav) are personal saves and are NOT
 * committed (gitignored). When they are absent the Gen-4 cases SKIP and the
 * test still exits 0, so a clean checkout / CI never fails on missing fixtures.
 */
#include "common.h"
#include "gen4_transfer.h"
#include "gen4_save.h"
#include "gen4_pkmn.h"
#include "gen4_stats.h"
#include "gen3_species.h"
#include <pksav.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CHECK(cond, msg) do { \
    if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define RUBY     "saves/refs/baseline/pokemon_ruby.sav"
#define EMERALD  "saves/refs/baseline/pokemon_emerald.sav"
#define FIRERED  "saves/refs/baseline/pokemon_firered.sav"

/* Gen 4 fixtures are gitignored personal saves; tests skip when they're absent. */
static int have(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Load a save straight into a PokemonSave (no GUI helpers), like the other
 * tests do. Returns 1 on failure. */
static int load_g3(const char *path, PokemonSave *ps)
{
    ps->save_generation_type = SAVE_GENERATION_3;
    CHECK(pksav_gba_load_save_from_file(path, &ps->save.gba_save) == PKSAV_ERROR_NONE, "load gen3 save");
    return 0;
}
static int load_g4(const char *path, PokemonSave *ps)
{
    ps->save_generation_type = SAVE_GENERATION_4;
    CHECK(gen4_load_save_from_file(path, &ps->save.gen4_save) == PKSAV_ERROR_NONE, "load gen4 save");
    return 0;
}

/* Fields expected to survive the transfer, read off the Gen 3 mon. */
struct expect {
    uint16_t dex; uint32_t pid; uint16_t tid, sid;
    uint8_t iv[6], ev[6]; uint16_t move[4]; char ot[16];
};
static void read_gen3(const struct pksav_gba_pc_pokemon *m, struct expect *e)
{
    e->dex = gen3_internal_to_national(pksav_littleendian16(m->blocks.growth.species));
    e->pid = pksav_littleendian32(m->personality);
    e->tid = pksav_littleendian16(m->ot_id.pid);
    e->sid = pksav_littleendian16(m->ot_id.sid);
    uint32_t iv = pksav_littleendian32(m->blocks.misc.iv_egg_ability);
    for (int i = 0; i < 6; i++) e->iv[i] = (uint8_t)((iv >> (5 * i)) & 0x1F);
    const struct pksav_gba_pokemon_effort_block *ev = &m->blocks.effort;
    e->ev[0]=ev->ev_hp; e->ev[1]=ev->ev_atk; e->ev[2]=ev->ev_def;
    e->ev[3]=ev->ev_spd; e->ev[4]=ev->ev_spatk; e->ev[5]=ev->ev_spdef;
    for (int i = 0; i < 4; i++) e->move[i] = pksav_littleendian16(m->blocks.attacks.moves[i]);
    memset(e->ot, 0, sizeof e->ot);
    pksav_gba_import_text(m->otname, e->ot, 7);
}

/* Assert a decrypted Gen 4 buffer matches the Gen 3 mon + Pal Park rules. */
static int check_pk4(const uint8_t *dec, const struct expect *e, enum gen4_game game)
{
    CHECK(gen4_pk4_species(dec) == e->dex, "species/dex preserved");
    CHECK(gen4_pk4_pid(dec) == e->pid, "PID preserved");
    CHECK(gen4_pk4_tid(dec) == e->tid, "TID preserved");
    CHECK(gen4_pk4_sid(dec) == e->sid, "SID preserved");
    for (int i = 0; i < 6; i++) CHECK(gen4_pk4_iv(dec,i) == e->iv[i], "IV preserved");
    for (int i = 0; i < 6; i++) CHECK(gen4_pk4_ev(dec,i) == e->ev[i], "EV preserved");
    for (int i = 0; i < 4; i++) CHECK(gen4_pk4_move(dec,i) == e->move[i], "move preserved");
    char ot[16] = {0}; gen4_decode_text(dec + 0x68, 8, ot);
    CHECK(strcmp(ot, e->ot) == 0, "OT name preserved");
    CHECK(gen4_pk4_stored_checksum(dec) == gen4_pk4_checksum(dec), "in-mon checksum valid");
    CHECK(gen4_pk4_is_egg(dec) == false, "result is not an egg");
    CHECK(gen4_pk4_friendship(dec) == 70, "friendship reset to 70");
    CHECK((dec[0x80] | (dec[0x81] << 8)) == 55, "met location = Pal Park (DP @0x80)");
    if (game == GEN4_GAME_PT || game == GEN4_GAME_HGSS)
        CHECK((dec[0x46] | (dec[0x47] << 8)) == 55, "met location = Pal Park (Pt/HGSS @0x46)");
    return 0;
}

/* Party has room -> mon appends to the party and gets rebuilt stats. */
static int test_party_append(const char *g3, int idx, const char *g4)
{
    if (!have(g4)) { printf("SKIP test_party_append (%s absent)\n", g4); return 0; }
    PokemonSave src, dst;
    if (load_g3(g3, &src) || load_g4(g4, &dst)) return 1;

    struct expect e;
    read_gen3(&src.save.gba_save.pokemon_storage.p_party->party[idx].pc_data, &e);
    uint32_t src_count0 = src.save.gba_save.pokemon_storage.p_party->count;

    /* Fixture parties are full; free the last slot so the mon appends. */
    gen4_set_party_count(&dst.save.gen4_save, 5);
    CHECK(transfer_pkmn_gen3_to_gen4(&src, (uint8_t)idx, &dst) == error_none, "transfer succeeds");
    CHECK(gen4_party_count(&dst.save.gen4_save) == 6, "destination party grew to 6");
    CHECK(src.save.gba_save.pokemon_storage.p_party->count == src_count0 - 1, "source mon removed");

    const uint8_t *raw = gen4_party_slot_raw(&dst.save.gen4_save, 5);
    CHECK(raw != NULL, "appended party slot present");
    uint8_t dec[GEN4_PK4_PARTY_SIZE];
    gen4_pk4_decrypt(raw, dec, true);
    if (check_pk4(dec, &e, dst.save.gen4_save.game)) return 1;
    uint8_t lvl = gen4_pk4_party_level(dec);
    CHECK(lvl >= 1 && lvl <= 100, "party stats rebuilt (level in range)");

    pksav_gba_free_save(&src.save.gba_save);
    gen4_free_save(&dst.save.gen4_save);
    printf("test_party_append(%s -> %s) PASS\n", g3, g4);
    return 0;
}

/* Party full -> mon goes to the first empty box slot. */
static int test_box_overflow(const char *g3, int idx, const char *g4)
{
    if (!have(g4)) { printf("SKIP test_box_overflow (%s absent)\n", g4); return 0; }
    PokemonSave src, dst;
    if (load_g3(g3, &src) || load_g4(g4, &dst)) return 1;

    struct expect e;
    read_gen3(&src.save.gba_save.pokemon_storage.p_party->party[idx].pc_data, &e);
    uint8_t pc0 = gen4_party_count(&dst.save.gen4_save);
    CHECK(pc0 == GEN4_PARTY_MAX, "destination party is full");

    CHECK(transfer_pkmn_gen3_to_gen4(&src, (uint8_t)idx, &dst) == error_none, "transfer succeeds");
    CHECK(gen4_party_count(&dst.save.gen4_save) == pc0, "party count unchanged (went to box)");

    uint8_t dec[GEN4_PK4_PARTY_SIZE];
    const uint8_t *found = NULL;
    for (int b = 0; b < GEN4_NUM_BOXES && !found; b++)
        for (int s = 0; s < GEN4_BOX_SLOTS; s++) {
            const uint8_t *raw = gen4_box_slot_raw(&dst.save.gen4_save, b, s);
            if (!raw) continue;
            gen4_pk4_decrypt(raw, dec, false);
            if (gen4_pk4_species(dec) != 0 && gen4_pk4_pid(dec) == e.pid) { found = raw; break; }
        }
    CHECK(found != NULL, "transferred mon found in a box");
    if (check_pk4(dec, &e, dst.save.gen4_save.game)) return 1;

    pksav_gba_free_save(&src.save.gba_save);
    gen4_free_save(&dst.save.gen4_save);
    printf("test_box_overflow(%s -> %s) PASS\n", g3, g4);
    return 0;
}

/* Eggs must be refused and the source mon left intact. */
static int test_egg_refusal(const char *g3, int idx, const char *g4)
{
    if (!have(g4)) { printf("SKIP test_egg_refusal (%s absent)\n", g4); return 0; }
    PokemonSave src, dst;
    if (load_g3(g3, &src) || load_g4(g4, &dst)) return 1;

    struct pksav_gba_pc_pokemon *m = &src.save.gba_save.pokemon_storage.p_party->party[idx].pc_data;
    uint32_t v = pksav_littleendian32(m->blocks.misc.iv_egg_ability) | PKSAV_GBA_POKEMON_EGG_MASK;
    m->blocks.misc.iv_egg_ability = pksav_littleendian32(v);            /* forge an egg */
    uint32_t src_count0 = src.save.gba_save.pokemon_storage.p_party->count;
    uint8_t  dst_pc0 = gen4_party_count(&dst.save.gen4_save);

    CHECK(transfer_pkmn_gen3_to_gen4(&src, (uint8_t)idx, &dst) == error_transfer_not_obtainable,
          "egg is refused");
    CHECK(src.save.gba_save.pokemon_storage.p_party->count == src_count0, "source mon not consumed");
    CHECK(gen4_party_count(&dst.save.gen4_save) == dst_pc0, "destination unchanged");

    pksav_gba_free_save(&src.save.gba_save);
    gen4_free_save(&dst.save.gen4_save);
    printf("test_egg_refusal PASS\n");
    return 0;
}

/* A non-Gen-4 destination must be rejected without mutating anything. */
static int test_wrong_gen(const char *g3a, const char *g3b)
{
    PokemonSave src, dst;
    if (load_g3(g3a, &src) || load_g3(g3b, &dst)) return 1;
    uint32_t src_count0 = src.save.gba_save.pokemon_storage.p_party->count;
    CHECK(transfer_pkmn_gen3_to_gen4(&src, 0, &dst) == error_swap_pkmn, "wrong-gen pairing refused");
    CHECK(src.save.gba_save.pokemon_storage.p_party->count == src_count0, "source untouched");

    pksav_gba_free_save(&src.save.gba_save);
    pksav_gba_free_save(&dst.save.gba_save);
    printf("test_wrong_gen PASS\n");
    return 0;
}

int main(void)
{
    /* party-append across all three Gen 4 formats + varied species */
    if (test_party_append(RUBY,    0, "saves/gen4/diamond.sav"))   return 1; /* DP   */
    if (test_party_append(EMERALD, 1, "saves/gen4/platinum.sav"))  return 1; /* Pt   */
    if (test_party_append(FIRERED, 2, "saves/gen4/heartgold.sav")) return 1; /* HGSS */
    /* box-overflow path */
    if (test_box_overflow(FIRERED, 4, "saves/gen4/soulsilver.sav")) return 1;
    /* refusals */
    if (test_egg_refusal(RUBY, 0, "saves/gen4/pearl.sav")) return 1;
    if (test_wrong_gen(RUBY, FIRERED)) return 1;

    printf("ALL PASS\n");
    return 0;
}

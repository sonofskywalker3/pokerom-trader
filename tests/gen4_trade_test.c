/*
 * Test for Gen 4 <-> Gen 4 trading: drive gen4_swap_party_slots() (the whole
 * trade — a PK4 travels as raw bytes) and gen4_dex_set_seen_caught() with real
 * saves, then round-trip both saves through gen4_save_save() and confirm the
 * swapped mons, the recomputed footer CRCs, and the Pokédex bits all persist.
 * Covers a same-game pair (Diamond <-> Pearl) and a cross-game pair
 * (Diamond <-> SoulSilver: DP and HGSS have different save geometry but the
 * identical PK4 party format).
 *
 * Build (from repo root, after building deps/pksav):
 *   gcc tests/gen4_trade_test.c src/gen4_save.c src/gen4_pkmn.c \
 *       -I include -I deps/pksav/include -I deps/pksav/build/include \
 *       -L deps/pksav/build/lib -lpksav -lm -o tests/gen4_trade_test.exe
 *   ./tests/gen4_trade_test.exe
 *
 * NOTE: the Gen 4 fixtures (saves/gen4/*.sav) are personal saves and are NOT
 * committed (gitignored). When they are absent the cases SKIP and the test
 * still exits 0, so a clean checkout / CI never fails on missing fixtures.
 */
#include "gen4_save.h"
#include "gen4_pkmn.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CHECK(cond, msg) do { \
    if(!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while(0)

#define DIAMOND    "saves/gen4/diamond.sav"
#define PEARL      "saves/gen4/pearl.sav"
#define SOULSILVER "saves/gen4/soulsilver.sav"

static int have(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* Read a dex bit the way the layout doc specifies (independent of the
 * implementation under test): +0x04 caught / +0x44 seen, bit dex-1 LSB-first. */
static int dex_bit(const struct gen4_save *sv, uint16_t dex, int seen)
{
    const uint8_t *block = sv->data + sv->general_base + sv->layout.dex_offset;
    int bit = dex - 1;
    return (block[(seen ? 0x44 : 0x04) + (bit >> 3)] >> (bit & 7)) & 1;
}

/* Footer CRC of the active general block, recomputed from the block bytes. */
static uint16_t general_crc_stored(const struct gen4_save *sv)
{
    const uint8_t *end = sv->data + sv->general_base + sv->layout.general_size;
    return (uint16_t)(end[-2] | (end[-1] << 8));
}
static uint16_t general_crc_computed(const struct gen4_save *sv)
{
    return gen4_crc16_ccitt(sv->data + sv->general_base,
                            sv->layout.general_size - sv->layout.crc_footer_skip);
}

static int test_trade(const char *path_a, const char *path_b,
                      const char *tmp_a, const char *tmp_b)
{
    if (!have(path_a) || !have(path_b))
    {
        printf("SKIP test_trade (%s / %s absent)\n", path_a, path_b);
        return 0;
    }
    struct gen4_save a, b;
    CHECK(gen4_load_save_from_file(path_a, &a) == PKSAV_ERROR_NONE, "load save a");
    CHECK(gen4_load_save_from_file(path_b, &b) == PKSAV_ERROR_NONE, "load save b");

    int ia = 0, ib = gen4_party_count(&b) - 1;
    CHECK(gen4_party_count(&a) >= 1 && ib >= 0, "fixture parties non-empty");

    /* Out-of-range indices must be refused without touching anything. */
    CHECK(!gen4_swap_party_slots(&a, gen4_party_count(&a), &b, ib), "oob index a refused");
    CHECK(!gen4_swap_party_slots(&a, ia, &b, -1), "oob index b refused");

    /* Capture the raw slots; a trade must move them verbatim. */
    uint8_t raw_a0[GEN4_PK4_PARTY_SIZE], raw_b0[GEN4_PK4_PARTY_SIZE];
    memcpy(raw_a0, gen4_party_slot_raw(&a, ia), GEN4_PK4_PARTY_SIZE);
    memcpy(raw_b0, gen4_party_slot_raw(&b, ib), GEN4_PK4_PARTY_SIZE);

    CHECK(gen4_swap_party_slots(&a, ia, &b, ib), "swap succeeds");
    CHECK(memcmp(gen4_party_slot_raw(&a, ia), raw_b0, GEN4_PK4_PARTY_SIZE) == 0, "a received b's mon verbatim");
    CHECK(memcmp(gen4_party_slot_raw(&b, ib), raw_a0, GEN4_PK4_PARTY_SIZE) == 0, "b received a's mon verbatim");

    /* Both received mons must still decrypt to a valid PK4. */
    uint8_t dec_a[GEN4_PK4_PARTY_SIZE], dec_b[GEN4_PK4_PARTY_SIZE];
    gen4_pk4_decrypt(gen4_party_slot_raw(&a, ia), dec_a, true);
    gen4_pk4_decrypt(gen4_party_slot_raw(&b, ib), dec_b, true);
    CHECK(gen4_pk4_stored_checksum(dec_a) == gen4_pk4_checksum(dec_a), "a's received mon checksum valid");
    CHECK(gen4_pk4_stored_checksum(dec_b) == gen4_pk4_checksum(dec_b), "b's received mon checksum valid");
    uint16_t dex_to_a = gen4_pk4_species(dec_a);
    uint16_t dex_to_b = gen4_pk4_species(dec_b);
    CHECK(dex_to_a >= 1 && dex_to_a <= GEN4_NATIONAL_DEX_MAX, "a's received species sane");
    CHECK(dex_to_b >= 1 && dex_to_b <= GEN4_NATIONAL_DEX_MAX, "b's received species sane");

    /* Register the received mons in each Pokédex. */
    gen4_dex_set_seen_caught(&a, dex_to_a);
    gen4_dex_set_seen_caught(&b, dex_to_b);
    CHECK(dex_bit(&a, dex_to_a, 0) && dex_bit(&a, dex_to_a, 1), "a's dex has seen+caught");
    CHECK(dex_bit(&b, dex_to_b, 0) && dex_bit(&b, dex_to_b, 1), "b's dex has seen+caught");

    /* Round-trip both saves and confirm everything persisted with valid CRCs. */
    CHECK(gen4_save_save(tmp_a, &a) == PKSAV_ERROR_NONE, "write save a");
    CHECK(gen4_save_save(tmp_b, &b) == PKSAV_ERROR_NONE, "write save b");
    gen4_free_save(&a);
    gen4_free_save(&b);

    struct gen4_save ra, rb;
    CHECK(gen4_load_save_from_file(tmp_a, &ra) == PKSAV_ERROR_NONE, "reload save a");
    CHECK(gen4_load_save_from_file(tmp_b, &rb) == PKSAV_ERROR_NONE, "reload save b");
    CHECK(memcmp(gen4_party_slot_raw(&ra, ia), raw_b0, GEN4_PK4_PARTY_SIZE) == 0, "a's traded mon persisted");
    CHECK(memcmp(gen4_party_slot_raw(&rb, ib), raw_a0, GEN4_PK4_PARTY_SIZE) == 0, "b's traded mon persisted");
    CHECK(general_crc_stored(&ra) == general_crc_computed(&ra), "a's general-block CRC valid");
    CHECK(general_crc_stored(&rb) == general_crc_computed(&rb), "b's general-block CRC valid");
    CHECK(dex_bit(&ra, dex_to_a, 0) && dex_bit(&ra, dex_to_a, 1), "a's dex bits persisted");
    CHECK(dex_bit(&rb, dex_to_b, 0) && dex_bit(&rb, dex_to_b, 1), "b's dex bits persisted");
    gen4_free_save(&ra);
    gen4_free_save(&rb);

    printf("test_trade(%s <-> %s) PASS\n", path_a, path_b);
    return 0;
}

/* The dex helper must refuse to write when the magic isn't present. */
static int test_dex_magic_guard(const char *path)
{
    if (!have(path)) { printf("SKIP test_dex_magic_guard (%s absent)\n", path); return 0; }
    struct gen4_save sv;
    CHECK(gen4_load_save_from_file(path, &sv) == PKSAV_ERROR_NONE, "load save");
    uint8_t *block = sv.data + sv.general_base + sv.layout.dex_offset;
    uint8_t before[0x84];
    block[0] ^= 0xFF; /* corrupt the magic */
    memcpy(before, block, sizeof before);
    gen4_dex_set_seen_caught(&sv, 25);
    CHECK(memcmp(before, block, sizeof before) == 0, "no write without the dex magic");
    gen4_free_save(&sv);
    printf("test_dex_magic_guard(%s) PASS\n", path);
    return 0;
}

int main(void)
{
    int rc = 0;
    rc |= test_trade(DIAMOND, PEARL, "tests/_tmp_g4trade_d.sav", "tests/_tmp_g4trade_p.sav");
    rc |= test_trade(DIAMOND, SOULSILVER, "tests/_tmp_g4trade_d2.sav", "tests/_tmp_g4trade_ss.sav");
    rc |= test_dex_magic_guard(PEARL);
    if (rc == 0) printf("gen4_trade_test: all cases passed (or skipped)\n");
    return rc;
}

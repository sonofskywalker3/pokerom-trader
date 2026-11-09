/*
 * Gen 3 event distribution: the US 10th Anniversary Celebi.
 *
 * 1. For every 16-bit origin seed, the built Pokemon carries the template's
 *    fixed data (PKHeX EncountersWC3: Celebi Lv70, OT "10 ANIV", ID 00010/0,
 *    AncientPower/Future Sight/Baton Pass/Perish Song, Ruby origin, event met
 *    location 255, Poke Ball, English) and its PID/IVs/OT gender are exactly
 *    what an independent re-implementation of the BACD_R_A generator gives.
 * 2. Giving it to a real Emerald save puts it in the party, registers the dex,
 *    survives save + reload with a valid per-mon checksum, and a second give
 *    is refused (once per save).
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen3_gift.h"
#include "gen3_species.h"
#include "gen3_stats.h"
#include "events.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return 1; } } while (0)

static const char *EMERALD = "saves/refs/baseline/pokemon_emerald.sav";

/* Independent reference generator (PKHeX EncounterGift3.SetPINGA for
 * Shiny.Never + restricted seed, CommonEvent3.GetRegularAntishiny,
 * ClassicEraRNG.GetSequentialIVs, GetGenderBit7). */
static void reference(uint16_t seed16, uint32_t *pid, uint32_t *iv32, int *otg)
{
    uint32_t s = seed16;
#define NEXT() (s = s * 0x41C64E6Du + 0x6073u, s >> 16)
    uint32_t a = NEXT();
    uint32_t b = NEXT();
    uint32_t p = (a << 16) | b;
    if (((10u ^ (p >> 16) ^ (p & 0xFFFF)) & 0xFFFF) < 8) p = (p + 8) & 0xFFFFFFF8u;
    uint32_t i1 = NEXT() & 0x7FFF;
    uint32_t i2 = NEXT() & 0x7FFF;
    uint32_t r = (s * 0x41C64E6Du + 0x6073u) >> 16;
#undef NEXT
    *pid = p;
    *iv32 = (i2 << 15) | i1;
    *otg = (int)(((r >> 7) & 1u) ^ 1u);
}

static uint16_t mon_checksum(const struct pksav_gba_pc_pokemon *m)
{
    const uint8_t *p = (const uint8_t *)&m->blocks;
    unsigned sum = 0;
    for (size_t i = 0; i < sizeof(m->blocks); i += 2) sum += (unsigned)(p[i] | (p[i + 1] << 8));
    return (uint16_t)sum;
}

static int test_all_seeds(void)
{
    const struct gen3_gift *g = &GEN3_GIFT_10ANIV_CELEBI;
    char ot[16], nick[16];
    for (uint32_t seed = 0; seed < 65536; seed++)
    {
        struct pksav_gba_pc_pokemon m;
        gen3_gift_build(g, (uint16_t)seed, &m);
        uint32_t pid, iv; int otg;
        reference((uint16_t)seed, &pid, &iv, &otg);
        CHECK(pksav_littleendian32(m.personality) == pid, "seed %u PID", seed);
        uint32_t ivw = pksav_littleendian32(m.blocks.misc.iv_egg_ability);
        CHECK((ivw & 0x3FFFFFFF) == iv, "seed %u IVs", seed);
        CHECK(!(ivw >> 31), "seed %u: Celebi has one ability; bit must be clear", seed);
        CHECK(!((ivw >> 30) & 1), "seed %u egg bit", seed);
        uint16_t origin = pksav_littleendian16(m.blocks.misc.origin_info);
        CHECK((origin & 0x7F) == 70, "met level");
        CHECK(((origin >> 7) & 0xF) == 2, "origin game Ruby");
        CHECK(((origin >> 11) & 0xF) == 4, "Poke Ball");
        CHECK((int)(origin >> 15) == otg, "seed %u OT gender", seed);
        CHECK(m.blocks.misc.met_location == 255, "met location");
        CHECK(m.blocks.misc.ribbons_obedience == 0, "no ribbons / not fateful");
        if (seed == 0)
        {
            CHECK(pksav_littleendian16(m.ot_id.pid) == 10 && pksav_littleendian16(m.ot_id.sid) == 0, "ID");
            CHECK(pksav_littleendian16(m.language) == 0x0202, "English + has-species flag");
            pksav_gba_import_text(m.otname, ot, 7);
            pksav_gba_import_text(m.nickname, nick, 10);
            CHECK(strcmp(ot, "10 ANIV") == 0, "OT name '%s'", ot);
            CHECK(strcmp(nick, "CELEBI") == 0, "nickname '%s'", nick);
            CHECK(pksav_littleendian16(m.blocks.growth.species) == gen3_national_to_internal(251), "species");
            CHECK(m.blocks.growth.held_item == 0, "no item");
            CHECK(m.blocks.growth.friendship == 100, "base friendship");
            CHECK(pksav_littleendian32(m.blocks.growth.exp) == gen3_exp_for_level(70, gen3_growth_rate_for_dex(251)), "exp");
            static const uint16_t mv[4] = {246, 248, 226, 195};
            static const uint8_t pp[4] = {5, 15, 40, 5};
            for (int i = 0; i < 4; i++)
            {
                CHECK(pksav_littleendian16(m.blocks.attacks.moves[i]) == mv[i], "move %d", i);
                CHECK(m.blocks.attacks.move_pps[i] == pp[i], "pp %d", i);
            }
            const uint8_t *ev = (const uint8_t *)&m.blocks.effort;
            for (size_t i = 0; i < sizeof(m.blocks.effort); i++) CHECK(ev[i] == 0, "EVs/contest must be 0");
        }
        CHECK(((10u ^ (pid >> 16) ^ (pid & 0xFFFF)) & 0xFFFF) >= 8, "seed %u shiny", seed);
    }
    printf("all_seeds PASS (65536 seeds)\n");
    return 0;
}

static int test_give(void)
{
    PokemonSave s;
    load_savefile_from_path(EMERALD, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_3, "fixture");
    uint32_t before = s.save.gba_save.pokemon_storage.p_party->count;
    CHECK(!gen3_gift_already_received(&s, &GEN3_GIFT_10ANIV_CELEBI), "fixture already has it?");
    CHECK(gen3_gift_give(&s, &GEN3_GIFT_10ANIV_CELEBI) == error_none, "give");
    CHECK(s.save.gba_save.pokemon_storage.p_party->count == before + 1, "landed in the party");
    CHECK(gen3_gift_already_received(&s, &GEN3_GIFT_10ANIV_CELEBI), "now received");
    const char *tmp = "tests/_tmp_gift.sav";
    CHECK(save_savefile_to_path(&s, (char *)tmp) == error_none, "write");
    PokemonSave r;
    load_savefile_from_path(tmp, &r);
    CHECK(r.save_generation_type == SAVE_GENERATION_3, "reload");
    struct pksav_gba_pokemon_party *party = r.save.gba_save.pokemon_storage.p_party;
    CHECK(party->count == before + 1, "party count persisted");
    for (uint32_t i = 0; i < party->count; i++)
        CHECK(pksav_littleendian16(party->party[i].pc_data.checksum) == mon_checksum(&party->party[i].pc_data),
              "party %u checksum (Bad Egg)", i);
    const struct pksav_gba_pc_pokemon *c = &party->party[party->count - 1].pc_data;
    uint32_t pid = pksav_littleendian32(c->personality);
    /* the app never hands out an anti-shiny-bumped seed */
    int found = 0;
    for (uint32_t seed = 0; seed < 65536 && !found; seed++)
    {
        uint32_t p, iv; int otg;
        reference((uint16_t)seed, &p, &iv, &otg);
        if (p == pid && iv == (pksav_littleendian32(c->blocks.misc.iv_egg_ability) & 0x3FFFFFFF))
        {
            uint32_t s0 = seed, a, b;
            s0 = s0 * 0x41C64E6Du + 0x6073u; a = s0 >> 16;
            s0 = s0 * 0x41C64E6Du + 0x6073u; b = s0 >> 16;
            CHECK(((a << 16) | b) == pid, "picked an anti-shiny-bumped seed");
            found = 1;
        }
    }
    CHECK(found, "received Celebi doesn't come from any 16-bit seed");
    bool seen, owned;
    pokedex_get_entry(&r, 251, &seen, &owned);
    CHECK(seen && owned, "dex");
    CHECK(gen3_gift_already_received(&r, &GEN3_GIFT_10ANIV_CELEBI), "received after reload");
    /* Through the Events path (what the app's screen calls): refused now. */
    const struct pkmn_event *evs[8];
    int n = events_available(&r, evs, 8), gift_row = -1;
    for (int i = 0; i < n; i++) if (evs[i]->gift == &GEN3_GIFT_10ANIV_CELEBI) gift_row = i;
    CHECK(gift_row >= 0, "Celebi row missing from Emerald's events");
    CHECK(apply_event(&r, evs[gift_row]) == error_event_already_received, "second Celebi not refused");
    remove(tmp);
    printf("give PASS (party slot %u, PID %08X)\n", party->count - 1, pid);
    return 0;
}

int main(void)
{
    if (test_all_seeds()) return 1;
    if (test_give()) return 1;
    printf("gen3_gift_test: all cases passed\n");
    return 0;
}

/*
 * Gen 2 save writing + Gen 2 -> Gen 3 forward transfer.
 *
 * 1. Writing a Gen 2 save must leave the game's backup copy identical to the
 *    primary game-data block, with both checksums valid (Gold/Silver split
 *    the backup into five pieces; Crystal keeps one copy).
 * 2. Every Pokemon in a real Crystal save is sent to a copy of the Emerald
 *    reference save. Each one must keep its gender, shininess and Unown
 *    letter, carry a held item over only when Gen 3 has it, never claim a
 *    second ability its species lacks, be registered in the Gen 3 dex, and
 *    leave the source (a move, never a copy). Eggs must be refused.
 *
 * The Gen 2 fixtures are personal saves (gitignored); cases SKIP when absent.
 */
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen_transfer.h"
#include "gen2_items.h"
#include "gen4_stats.h"
#include "gen4_transfer.h"
#include <stdio.h>
#include <string.h>

#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); return 1; } } while (0)

static const char *CRYSTAL = "saves/Pokemon - Crystal Version.sav";
static const char *GOLD = "saves/Pokemon - Gold Version.sav";
static const char *EMERALD = "saves/refs/baseline/pokemon_emerald.sav";

static int exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int read_file(const char *p, uint8_t *buf, size_t n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    return got == n;
}

static uint16_t sum(const uint8_t *b, size_t lo, size_t hi)
{
    unsigned s = 0;
    for (size_t i = lo; i <= hi; i++) s += b[i];
    return (uint16_t)s;
}

static uint16_t rd16(const uint8_t *b, size_t o) { return (uint16_t)(b[o] | (b[o + 1] << 8)); }

static int test_backup(const char *path, int crystal)
{
    if (!exists(path)) { printf("SKIP backup %s (fixture absent)\n", path); return 0; }
    PokemonSave s;
    load_savefile_from_path(path, &s);
    CHECK(s.save_generation_type == SAVE_GENERATION_2, "%s did not load as Gen 2", path);
    const char *tmp = "tests/_tmp_gen2_backup.sav";
    CHECK(save_savefile_to_path(&s, (char *)tmp) == error_none, "write %s", tmp);
    static uint8_t b[0x8000];
    CHECK(read_file(tmp, b, sizeof(b)), "read back");
    if (crystal)
    {
        CHECK(memcmp(b + 0x2009, b + 0x1209, 0x2B82 - 0x2009 + 1) == 0, "Crystal backup differs from primary");
        CHECK(rd16(b, 0x2D0D) == sum(b, 0x2009, 0x2B82), "Crystal checksum 1");
        CHECK(rd16(b, 0x1F0D) == sum(b, 0x1209, 0x1D82), "Crystal checksum 2");
    }
    else
    {
        static const size_t r[5][3] = {{0x2009, 0x222E, 0x15C7}, {0x222F, 0x23D8, 0x3D96}, {0x23D9, 0x2855, 0x0C6B},
                                       {0x2856, 0x2889, 0x7E39}, {0x288A, 0x2D68, 0x10E8}};
        for (int i = 0; i < 5; i++)
            CHECK(memcmp(b + r[i][0], b + r[i][2], r[i][1] - r[i][0] + 1) == 0, "GS backup piece %d differs", i);
        CHECK(rd16(b, 0x2D69) == sum(b, 0x2009, 0x2D68), "GS checksum 1");
        uint16_t c2 = (uint16_t)(sum(b, 0x0C6B, 0x17EC) + sum(b, 0x3D96, 0x3F3F) + sum(b, 0x7E39, 0x7E6C));
        CHECK(rd16(b, 0x7E6D) == c2, "GS checksum 2");
    }
    PokemonSave again;
    load_savefile_from_path(tmp, &again);
    CHECK(again.save_generation_type == SAVE_GENERATION_2, "rewritten %s no longer loads as Gen 2", path);
    remove(tmp);
    printf("backup(%s) PASS\n", path);
    return 0;
}

/* Expected traits, computed from the Gen 2 DVs the way the games do. */
struct traits { int gender; int shiny; int letter; };

static struct traits gb_traits(uint16_t dex, uint16_t iv_data)
{
    uint8_t dv[PKSAV_NUM_GB_IVS];
    gb_get_dvs(iv_data, dv);
    struct traits t;
    uint8_t th = pkmn_gender_threshold(dex);
    uint8_t b = (uint8_t)((dv[PKSAV_GB_IV_ATTACK] << 4) | dv[PKSAV_GB_IV_SPEED]);
    t.gender = th == 255 ? 2 : th == 0 ? 0 : th == 254 ? 1 : (b <= th);
    t.shiny = dv[PKSAV_GB_IV_DEFENSE] == 10 && dv[PKSAV_GB_IV_SPEED] == 10 && dv[PKSAV_GB_IV_SPECIAL] == 10 &&
              (dv[PKSAV_GB_IV_ATTACK] & 2);
    t.letter = dex == 201 ? (((dv[PKSAV_GB_IV_ATTACK] & 6) << 5) | ((dv[PKSAV_GB_IV_DEFENSE] & 6) << 3) |
                             ((dv[PKSAV_GB_IV_SPEED] & 6) << 1) | ((dv[PKSAV_GB_IV_SPECIAL] & 6) >> 1)) / 10
                          : -1;
    return t;
}

static struct traits gba_traits(uint16_t dex, uint32_t pid, uint16_t tid, uint16_t sid)
{
    struct traits t;
    uint8_t th = pkmn_gender_threshold(dex);
    t.gender = th == 255 ? 2 : th == 0 ? 0 : th == 254 ? 1 : ((pid & 0xFF) < th);
    t.shiny = (uint16_t)(tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
    t.letter = dex == 201 ? (int)(((((pid >> 24) & 3) << 6) | (((pid >> 16) & 3) << 4) | (((pid >> 8) & 3) << 2) | (pid & 3)) % 28) : -1;
    return t;
}

static const struct pksav_gba_pc_pokemon *find_new(PokemonSave *d, uint32_t party_before, int eb, int es)
{
    struct pksav_gba_pokemon_storage *st = &d->save.gba_save.pokemon_storage;
    if (st->p_party->count > party_before) return &st->p_party->party[st->p_party->count - 1].pc_data;
    if (eb < 0) return NULL;
    return &st->p_pc->boxes[eb].entries[es];
}

static void first_empty(PokemonSave *d, int *eb, int *es)
{
    struct pksav_gba_pokemon_storage *st = &d->save.gba_save.pokemon_storage;
    *eb = -1; *es = -1;
    for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES; b++)
        for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; s++)
            if (st->p_pc->boxes[b].entries[s].blocks.growth.species == 0) { *eb = b; *es = s; return; }
}

static int test_transfer(void)
{
    if (!exists(CRYSTAL)) { printf("SKIP transfer (fixture absent)\n"); return 0; }
    PokemonSave s, d;
    load_savefile_from_path(CRYSTAL, &s);
    load_savefile_from_path(EMERALD, &d);
    CHECK(s.save_generation_type == SAVE_GENERATION_2 && d.save_generation_type == SAVE_GENERATION_3, "fixture gens");
    bills_pc_normalize_current_box(&s);

    int sent = 0, refused = 0, shinies = 0, items_kept = 0, items_dropped = 0, unown = 0;
    int nb = bills_pc_num_boxes(&s);
    /* Party first, then every box: withdraw each boxed mon to the party and send it. */
    for (int b = -1; b < nb; b++)
    {
        for (;;)
        {
            struct pksav_gen2_pokemon_party *party = s.save.gen2_save.pokemon_storage.p_party;
            int idx;
            if (b < 0)
            {
                if (party->count <= 1)
                {
                    /* The source party may never be emptied: the lone mon is refused. */
                    if (party->count == 1 && party->species[0] != 0xFD && gen3_species_obtainable(party->party[0].pc_data.species))
                        CHECK(transfer_pkmn_to_gen3(&s, 0, &d) == error_transfer_last_pkmn && party->count == 1, "last party mon must be refused");
                    break;
                }
                idx = 0;
            }
            else
            {
                if (s.save.gen2_save.pokemon_storage.pp_boxes[b]->count == 0) break;
                struct bills_pc_slot from = {BILLS_PC_LOC_BOX, b, 0}, to = {BILLS_PC_LOC_PARTY, 0, party->count};
                CHECK(bills_pc_move_pkmn(&s, from, to) == error_none, "withdraw box %d", b);
                idx = party->count - 1;
            }
            struct pksav_gen2_party_pokemon src = party->party[idx];
            uint8_t list_species = party->species[idx];
            uint16_t dex = src.pc_data.species;
            int before = party->count;
            uint32_t dparty = d.save.gba_save.pokemon_storage.p_party->count;
            int eb, es;
            first_empty(&d, &eb, &es);
            if (dparty >= 6 && eb < 0) { printf("  (Emerald copy full after %d, at box %d)\n", sent, b); goto done; }

            pksavhelper_error e = transfer_pkmn_to_gen3(&s, (uint8_t)idx, &d);
            if (list_species == 0xFD || !gen3_species_obtainable(dex))
            {
                CHECK(e == error_transfer_not_obtainable, "#%u should be refused", dex);
                CHECK(party->count == before, "refused mon must stay");
                refused++;
                printf("  refused #%u (from box %d)\n", dex, b);
                /* It stays in the party (a lone party mon can't be
                 * deposited); later withdrawals land after it. */
                if (b < 0) goto done; /* party pass: only reached with refusals at slot 0 */
                if (party->count >= PKSAV_GEN2_PARTY_NUM_POKEMON) goto done;
                continue;
            }
            CHECK(e == error_none, "#%u transfer failed (%d)", dex, e);
            CHECK(party->count == before - 1, "#%u still in the source", dex);

            const struct pksav_gba_pc_pokemon *g = find_new(&d, dparty, eb, es);
            CHECK(g != NULL, "can't find transferred #%u", dex);
            uint32_t pid = pksav_littleendian32(g->personality);
            uint16_t tid = pksav_littleendian16(g->ot_id.pid), sid = pksav_littleendian16(g->ot_id.sid);
            struct traits want = gb_traits(dex, src.pc_data.iv_data), got = gba_traits(dex, pid, tid, sid);
            CHECK(want.gender == got.gender, "#%u gender %d -> %d", dex, want.gender, got.gender);
            CHECK(want.shiny == got.shiny, "#%u shiny %d -> %d", dex, want.shiny, got.shiny);
            CHECK(want.letter == got.letter, "Unown letter %d -> %d", want.letter, got.letter);
            uint16_t item = pksav_littleendian16(g->blocks.growth.held_item);
            CHECK(item == (src.pc_data.held_item ? gen2_item_to_gen3(src.pc_data.held_item) : 0), "#%u item", dex);
            uint32_t iv = pksav_littleendian32(g->blocks.misc.iv_egg_ability);
            if (!gen3_species_uses_ability_bit(dex)) CHECK(!(iv >> 31), "#%u claims a second ability it lacks", dex);
            else CHECK((int)(iv >> 31) == (int)(pid & 1), "#%u ability bit != PID bit 0", dex);
            CHECK(g->blocks.misc.met_location == 0xFE, "met location");
            bool seen, owned;
            pokedex_get_entry(&d, dex, &seen, &owned);
            CHECK(seen && owned, "#%u not registered in the Gen 3 dex", dex);
            sent++;
            shinies += want.shiny;
            unown += dex == 201;
            if (src.pc_data.held_item) { if (item) items_kept++; else items_dropped++; }
            if (b < 0 && party->count == 0) break;
        }
    }
done:
    CHECK(sent > 0, "nothing transferred");
    const char *ts = "tests/_tmp_g2g3_src.sav", *td = "tests/_tmp_g2g3_dst.sav";
    bills_pc_flush_current_box(&s);
    CHECK(save_savefile_to_path(&s, (char *)ts) == error_none && save_savefile_to_path(&d, (char *)td) == error_none, "write");
    PokemonSave s2, d2;
    load_savefile_from_path(ts, &s2);
    load_savefile_from_path(td, &d2);
    CHECK(s2.save_generation_type == SAVE_GENERATION_2 && d2.save_generation_type == SAVE_GENERATION_3, "reload gens");
    remove(ts); remove(td);
    printf("transfer(Crystal -> Emerald) PASS: %d sent, %d refused, %d shiny, %d Unown, items kept %d / dropped %d\n",
           sent, refused, shinies, unown, items_kept, items_dropped);
    return 0;
}

int main(void)
{
    if (test_backup(CRYSTAL, 1)) return 1;
    if (test_backup(GOLD, 0)) return 1;
    if (test_transfer()) return 1;
    printf("gen2_gen3_test: all cases passed (or skipped)\n");
    return 0;
}

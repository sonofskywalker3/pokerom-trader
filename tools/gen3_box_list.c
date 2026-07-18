/*
 * Slice 1 (Gen 3 trade module): read-only listing of a Gen 3 save's Pokemon.
 *
 * Proves Modules 1-2 end to end using PKSav:
 *   - loads a 128 KB GBA dump,
 *   - selects the ACTIVE save block correctly (the #1 footgun), and
 *   - lists party + PC box Pokemon by species/name (decrypt + GAEM deshuffle
 *     handled inside PKSav; we read the exposed decrypted fields).
 *
 * It also performs an INDEPENDENT, validity-checked slot selection and prints
 * it alongside PKSav's choice, so active-block correctness is auditable.
 *
 * Read-only: never writes to the save. Distributed under the MIT License.
 *
 * Usage: gen3_box_list <path-to-128KB-gba.sav>
 */
#include "gen3_species.h"

#include <pksav/gba/save.h>
#include <pksav/gba/text.h>
#include <pksav/math/endian.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_SIZE   0xE000u
#define SECTION_SZ  0x1000u
#define FOOTER_OFF  0x0FF4u
#define MAGIC       0x08012025u
#define NUM_SECT    14

static uint32_t le32_raw(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int slot_is_valid(const uint8_t *buf, size_t len, size_t slot_off,
                         uint32_t *save_index_out) {
    *save_index_out = (slot_off + FOOTER_OFF + 12 <= len)
                          ? le32_raw(buf + slot_off + FOOTER_OFF + 8) : 0;
    if (slot_off + SLOT_SIZE > len) return 0;
    for (int s = 0; s < NUM_SECT; ++s) {
        const uint8_t *foot = buf + slot_off + (size_t)s * SECTION_SZ + FOOTER_OFF;
        if (foot[0] > (NUM_SECT - 1) || le32_raw(foot + 4) != MAGIC) return 0;
    }
    return 1;
}

static const char *save_type_name(enum pksav_gba_save_type t) {
    switch (t) {
        case PKSAV_GBA_SAVE_TYPE_RS:      return "Ruby/Sapphire";
        case PKSAV_GBA_SAVE_TYPE_EMERALD: return "Emerald";
        case PKSAV_GBA_SAVE_TYPE_FRLG:    return "FireRed/LeafGreen";
        default:                          return "NONE/unknown";
    }
}

/* Decode a GBA-encoded name into an ASCII buffer (out must hold num_chars+1). */
static void decode_name(const uint8_t *src, char *out, size_t num_chars) {
    memset(out, 0, num_chars + 1);
    pksav_gba_import_text(src, out, num_chars);
}

/* Print one mon row from its (decrypted) pc_data. Returns 1 if non-empty. */
static int print_mon(const struct pksav_gba_pc_pokemon *mon,
                     const char *loc, int level /* -1 if unknown */) {
    uint16_t species = pksav_littleendian16(mon->blocks.growth.species);
    if (species == 0) return 0;

    char nick[PKSAV_GBA_POKEMON_NICKNAME_LENGTH + 1];
    char otname[PKSAV_GBA_POKEMON_OTNAME_LENGTH + 1];
    decode_name(mon->nickname, nick, PKSAV_GBA_POKEMON_NICKNAME_LENGTH);
    decode_name(mon->otname, otname, PKSAV_GBA_POKEMON_OTNAME_LENGTH);

    uint16_t nat = gen3_internal_to_national(species);
    uint32_t pid = pksav_littleendian32(mon->personality);
    uint16_t held = pksav_littleendian16(mon->blocks.growth.held_item);

    char lvlbuf[8];
    if (level >= 0) snprintf(lvlbuf, sizeof lvlbuf, "L%-3d", level);
    else            snprintf(lvlbuf, sizeof lvlbuf, "  - ");

    printf("  %-10s %s  #%-3u int=%-3u  %-11s  nick=\"%-10s\"  OT=%-7s  held=%u  PID=0x%08X\n",
           loc, lvlbuf, nat, species, gen3_national_dex_name(nat),
           nick, otname, held, pid);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <save.sav>\n", argv[0]); return 2; }
    const char *path = argv[1];

    /* ---- Independent active-block audit (raw buffer) ---- */
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) { fprintf(stderr, "read fail\n"); return 2; }
    fclose(f);

    printf("== Gen 3 save: %s ==\n", path);
    printf("size: %ld bytes (0x%lX)%s\n", len, len,
           (len >= 0x20000) ? "" : "  [WARNING: smaller than 128 KB]");

    uint32_t idxA = 0, idxB = 0;
    int okA = slot_is_valid(buf, (size_t)len, 0, &idxA);
    int okB = slot_is_valid(buf, (size_t)len, SLOT_SIZE, &idxB);
    int robust = (okA && okB) ? (idxA > idxB ? 0 : 1) : okA ? 0 : okB ? 1 : -1;
    printf("slot A: valid=%d save_index=%u | slot B: valid=%d save_index=%u\n",
           okA, idxA, okB, idxB);
    printf("active block (validity-checked) = %s\n",
           robust == 0 ? "A" : robust == 1 ? "B" : "NONE");
    free(buf);

    /* ---- Load via PKSav ---- */
    enum pksav_gba_save_type stype = PKSAV_GBA_SAVE_TYPE_NONE;
    pksav_gba_get_file_save_type(path, &stype);

    struct pksav_gba_save save;
    memset(&save, 0, sizeof(save));
    enum pksav_error err = pksav_gba_load_save_from_file(path, &save);
    if (err != PKSAV_ERROR_NONE) {
        printf("\nPKSav failed to load save (error=%d). Not a valid Gen 3 dump?\n", (int)err);
        return 1;
    }
    printf("PKSav loaded OK. Detected game: %s\n", save_type_name(save.save_type));

    /* ---- Party ---- */
    uint32_t party_count = save.pokemon_storage.p_party->count;
    printf("\n-- Party (%u) --\n", party_count);
    if (party_count == 0) printf("  (empty)\n");
    for (uint32_t i = 0; i < party_count && i < PKSAV_GBA_PARTY_NUM_POKEMON; ++i) {
        const struct pksav_gba_party_pokemon *pm = &save.pokemon_storage.p_party->party[i];
        print_mon(&pm->pc_data, "party", pm->party_data.level);
    }

    /* ---- PC boxes ---- */
    const struct pksav_gba_pokemon_pc *pc = save.pokemon_storage.p_pc;
    int total_box = 0;
    printf("\n-- PC Boxes (current box: %u) --\n",
           pksav_littleendian32(pc->current_box) + 1);
    for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES; ++b) {
        int box_count = 0;
        for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; ++s)
            if (pksav_littleendian16(pc->boxes[b].entries[s].blocks.growth.species) != 0)
                ++box_count;
        if (box_count == 0) continue;
        char boxlabel[24];
        snprintf(boxlabel, sizeof boxlabel, "Box%02d", b + 1);
        printf("  [%s: %d]\n", boxlabel, box_count);
        for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; ++s)
            total_box += print_mon(&pc->boxes[b].entries[s], boxlabel, -1);
    }
    if (total_box == 0) printf("  (all boxes empty)\n");

    printf("\nTotal: %u party + %d boxed Pokemon\n", party_count, total_box);
    pksav_gba_free_save(&save);
    return 0;
}

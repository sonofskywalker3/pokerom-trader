/* Minimal diagnostic: how does pksav handle a Gen 3 save, and does its
 * active-slot selection agree with an independent, validity-checked selection?
 *
 * Build (see comment at bottom). Usage: probe <path-to-128KB-gba.sav>
 */
#include <pksav/gba/save.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOT_SIZE   0xE000u
#define SECTION_SZ  0x1000u
#define FOOTER_OFF  0x0FF4u
#define MAGIC       0x08012025u
#define NUM_SECT    14

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* A slot is "present" if it holds NUM_SECT sections whose footers carry the
 * validation magic and an in-range section id. Returns save_index via out. */
static int slot_is_valid(const uint8_t *buf, size_t len, size_t slot_off,
                         uint32_t *save_index_out) {
    if (slot_off + SLOT_SIZE > len) return 0;
    int valid = 1;
    for (int s = 0; s < NUM_SECT; ++s) {
        const uint8_t *foot = buf + slot_off + (size_t)s * SECTION_SZ + FOOTER_OFF;
        uint8_t sid = foot[0];
        uint32_t validation = le32(foot + 4);
        if (sid > (NUM_SECT - 1) || validation != MAGIC) { valid = 0; break; }
    }
    /* save_index lives in the first physical section's footer (all sections in
     * a slot share it). Read it regardless so we can report it. */
    *save_index_out = le32(buf + slot_off + FOOTER_OFF + 8);
    return valid;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <save.sav>\n", argv[0]); return 2; }
    const char *path = argv[1];

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { fprintf(stderr, "read fail\n"); return 2; }
    fclose(f);

    printf("file: %s  (%ld bytes = 0x%lX)\n", path, len, len);

    /* ---- Independent, validity-checked slot selection ---- */
    uint32_t idxA = 0, idxB = 0;
    int okA = slot_is_valid(buf, (size_t)len, 0, &idxA);
    int okB = slot_is_valid(buf, (size_t)len, SLOT_SIZE, &idxB);
    printf("slot A @0x00000: valid=%d save_index=%u\n", okA, idxA);
    printf("slot B @0x0E000: valid=%d save_index=%u\n", okB, idxB);

    int robust; /* 0=A, 1=B, -1=none */
    if (okA && okB)      robust = (idxA > idxB) ? 0 : 1; /* both valid: higher counter */
    else if (okA)        robust = 0;
    else if (okB)        robust = 1;
    else                 robust = -1;
    printf("ROBUST active slot (validity-checked) = %s\n",
           robust == 0 ? "A" : robust == 1 ? "B" : "NONE");

    /* pksav's naive choice for reference */
    printf("pksav naive rule (idxA>idxB?A:B) would pick = %s\n",
           (idxA > idxB) ? "A" : "B");

    /* ---- What pksav actually does ---- */
    enum pksav_gba_save_type stype = PKSAV_GBA_SAVE_TYPE_NONE;
    enum pksav_error e1 = pksav_gba_get_file_save_type(path, &stype);
    printf("\npksav_gba_get_file_save_type -> error=%d save_type=%d "
           "(1=RS 2=E 3=FRLG 0=NONE)\n", (int)e1, (int)stype);

    struct pksav_gba_save save;
    memset(&save, 0, sizeof(save));
    enum pksav_error e2 = pksav_gba_load_save_from_file(path, &save);
    printf("pksav_gba_load_save_from_file -> error=%d\n", (int)e2);
    if (e2 == PKSAV_ERROR_NONE) {
        printf("  save_type=%d  party_count=%u\n",
               (int)save.save_type, save.pokemon_storage.p_party->count);
        int box_mons = 0;
        for (int b = 0; b < PKSAV_GBA_NUM_POKEMON_BOXES; ++b)
            for (int s = 0; s < PKSAV_GBA_BOX_NUM_POKEMON; ++s)
                if (save.pokemon_storage.p_pc->boxes[b].entries[s].blocks.growth.species != 0)
                    ++box_mons;
        printf("  non-empty box mons = %d\n", box_mons);
        pksav_gba_free_save(&save);
    }
    free(buf);
    return 0;
}

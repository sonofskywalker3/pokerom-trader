#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "pksavfilehelper.h"
#include "filehelper.h"

/************************************************************************
 * On-disk fingerprints ("stamps")
 *
 * Every loaded save buffer remembers what its file looked like (size, mtime
 * and a content hash) when it was loaded or last written by us. Before any
 * write we re-check the file: if an emulator (or another screen holding its
 * own copy) wrote it meanwhile, writing our stale buffer would silently undo
 * that progress, so the write is refused instead. Stamps are keyed by the
 * save's internal buffer pointer -- shared by struct copies of one load,
 * distinct between separate loads -- plus the path.
 ************************************************************************/
struct save_stamp
{
    const void *id;
    char path[1024];
    long size;
    long long mtime;
    uint64_t hash;
};
#define SAVE_STAMP_MAX 512
static struct save_stamp save_stamps[SAVE_STAMP_MAX];
static int save_stamp_next = 0;

static const void *save_identity(const PokemonSave *s)
{
    switch (s->save_generation_type)
    {
    case SAVE_GENERATION_1:
        return s->save.gen1_save.p_internal;
    case SAVE_GENERATION_2:
        return s->save.gen2_save.p_internal;
    case SAVE_GENERATION_3:
        return s->save.gba_save.p_internal;
    case SAVE_GENERATION_4:
        return s->save.gen4_save.data;
    default:
        return NULL;
    }
}

static bool same_path(const char *a, const char *b)
{
    for (;; a++, b++)
    {
        char ca = (*a == '\\') ? '/' : (char)tolower((unsigned char)*a);
        char cb = (*b == '\\') ? '/' : (char)tolower((unsigned char)*b);
        if (ca != cb)
            return false;
        if (ca == '\0')
            return true;
    }
}

static bool file_fingerprint(const char *path, long *size, long long *mtime, uint64_t *hash)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    uint64_t h = 1469598103934665603ULL; // FNV-1a 64
    long n = 0;
    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        for (size_t i = 0; i < got; i++)
        {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
        n += (long)got;
    }
    bool ok = !ferror(f);
    fclose(f);
    *size = n;
    *mtime = (long long)st.st_mtime;
    *hash = h;
    return ok;
}

static struct save_stamp *stamp_find(const void *id, const char *path)
{
    if (id == NULL)
        return NULL;
    for (int i = 0; i < SAVE_STAMP_MAX; i++)
    {
        if (save_stamps[i].id == id && same_path(save_stamps[i].path, path))
            return &save_stamps[i];
    }
    return NULL;
}

static void stamp_record(const PokemonSave *s, const char *path)
{
    const void *id = save_identity(s);
    if (id == NULL || strlen(path) >= sizeof(save_stamps[0].path))
        return;
    struct save_stamp *st = stamp_find(id, path);
    if (st == NULL)
    {
        st = &save_stamps[save_stamp_next];
        save_stamp_next = (save_stamp_next + 1) % SAVE_STAMP_MAX;
    }
    st->id = id;
    strcpy(st->path, path);
    if (!file_fingerprint(path, &st->size, &st->mtime, &st->hash))
        st->id = NULL;
}

static void stamp_forget(const void *id)
{
    if (id == NULL)
        return;
    for (int i = 0; i < SAVE_STAMP_MAX; i++)
    {
        if (save_stamps[i].id == id)
            save_stamps[i].id = NULL;
    }
}

bool save_file_changed_on_disk(const PokemonSave *pkmn_save, const char *path)
{
    const struct save_stamp *st = stamp_find(save_identity(pkmn_save), path);
    if (st == NULL)
        return false; // never loaded from / written to this path: nothing to protect
    long size;
    long long mtime;
    uint64_t hash;
    if (!file_fingerprint(path, &size, &mtime, &hash))
        return true; // deleted or unreadable since we loaded it
    // Content decides; a bare mtime bump with identical bytes (e.g. a copy
    // tool touching the file) is not a conflict.
    return size != st->size || hash != st->hash;
}

const char *save_error_message(pksavhelper_error err)
{
    switch (err)
    {
    case error_none:
        return "Saved";
    case error_save_stale:
        return "File changed on disk since it was loaded. Go back and reselect it to reload.";
    case error_save_write_failed:
    case error_update_save:
        return "Could not write the save file. Nothing was changed on disk.";
    case error_save_same_file:
        return "Both sides are the same save file. Nothing was changed on disk.";
    case error_save_rollback_failed:
        return "Saving failed and could not be undone. The original is kept as <save>.old.";
    default:
        return "Saving failed.";
    }
}

/**
 * @brief detects the save file generation of a save file
 * @param path the path to the save file
 * @param save_generation_type a pointer to a SaveGenerationType to store the save generation type
 * @return an enum pksav_error
 */
/* Count how many Gen 1 / Gen 2 structural invariants hold for a 32 KB save:
 * party count <= 6 with an 0xFF species-list terminator, current box count
 * within capacity with its terminator, and a 0x50-terminated player name.
 * Used only to break a tie when both generations' checksums validate. */
static int gen12_structure_score(const char *path, int gen)
{
    uint8_t buf[0x8000];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < sizeof(buf)) return 0;
    int score = 0;
    if (gen == 1)
    {
        uint8_t pc = buf[0x2F2C];
        if (pc <= 6 && buf[0x2F2D + pc] == 0xFF) score += 2;
        uint8_t bc = buf[0x30C0];
        if (bc <= 20 && buf[0x30C1 + bc] == 0xFF) score += 2;
        for (int i = 0; i < 11; i++) if (buf[0x2598 + i] == 0x50) { score += 1; break; }
    }
    else
    {
        // Crystal party at 0x2865, Gold/Silver at 0x288A; current box at 0x2D10 / 0x2D6C
        static const size_t party[2] = { 0x2865, 0x288A };
        static const size_t box[2]   = { 0x2D10, 0x2D6C };
        int best = 0;
        for (int v = 0; v < 2; v++)
        {
            int sc = 0;
            uint8_t pc = buf[party[v]];
            if (pc <= 6 && buf[party[v] + 1 + pc] == 0xFF) sc += 2;
            uint8_t bc = buf[box[v]];
            if (bc <= 20 && buf[box[v] + 1 + bc] == 0xFF) sc += 2;
            if (sc > best) best = sc;
        }
        score += best;
        for (int i = 0; i < 11; i++) if (buf[0x200B + i] == 0x50) { score += 1; break; }
    }
    return score;
}

enum pksav_error detect_savefile_generation(const char *path, SaveGenerationType *save_generation_type)
{
    enum pksav_error err = PKSAV_ERROR_NONE;

    enum pksav_gen1_save_type gen1_save_type = PKSAV_GEN1_SAVE_TYPE_NONE;
    enum pksav_gen2_save_type gen2_save_type = PKSAV_GEN2_SAVE_TYPE_NONE;
    enum pksav_gba_save_type gba_save_type = PKSAV_GBA_SAVE_TYPE_NONE;

    // Check Gen 4 (DS) first: it has the strongest, most specific signature
    // (exactly 512 KB + a 0x20060623 block-footer magic), so it can't collide
    // with the smaller GB/GBA formats or the weak Gen 2 heuristic.
    if (gen4_is_gen4_file(path))
    {
        *save_generation_type = SAVE_GENERATION_4;
        return err;
    }

    // Check Gen 3 (GBA) before Gen 2. The GBA format has a strong, specific
    // signature (0x08012025 section footer + matching security key), whereas
    // Gen 2's detection is a weaker checksum heuristic that can false-positive
    // on a 128 KB GBA save. Checking GBA first avoids misclassifying it.
    pksav_gba_get_file_save_type(path, &gba_save_type);
    if (gba_save_type != PKSAV_GBA_SAVE_TYPE_NONE)
    {
        *save_generation_type = SAVE_GENERATION_3;
        return err;
    }

    // Gen 1 vs Gen 2 can't be told apart by checksum alone:
    //  - Gen 1's signature is one 8-bit checksum over 0x2598-0x3522, a region
    //    that in a Gen 2 save holds the live current box, so any edit there
    //    gives a Gen 2 save a 1-in-256 chance of passing the Gen 1 test.
    //  - pksav's Crystal test accepts EITHER of two 16-bit checksums, and the
    //    second covers a region that is all zeros in many Gen 1 saves (zero
    //    sums to zero), so those pass the Gen 2 test.
    // Both have happened. So when both signatures match, break the tie by
    // checking which generation's party/box structures are well-formed.
    err = pksav_gen1_get_file_save_type(path, &gen1_save_type);
    pksav_gen2_get_file_save_type(path, &gen2_save_type);
    bool g1 = gen1_save_type != PKSAV_GEN1_SAVE_TYPE_NONE;
    bool g2 = gen2_save_type != PKSAV_GEN2_SAVE_TYPE_NONE;
    if (g1 && g2)
    {
        int s1 = gen12_structure_score(path, 1);
        int s2 = gen12_structure_score(path, 2);
        if (s2 > s1) g1 = false; else g2 = false;
    }
    if (g2)
    {
        *save_generation_type = SAVE_GENERATION_2;
        return err;
    }
    if (g1)
    {
        *save_generation_type = SAVE_GENERATION_1;
        return err;
    }

    SAVE_FILE_ERROR = PKSAV_ERROR_INVALID_SAVE;
    *save_generation_type = SAVE_GENERATION_CORRUPTED;
    return 0;
}
/**
 * @brief loads a save file from a path in the buffer
 * @param path the path to the save file
 * @return a PokemonSave struct
 */
void load_savefile_from_path(const char *path, PokemonSave *pkmn_save)
{
    enum pksav_error err = PKSAV_ERROR_NONE;
    // PokemonSave pkmn_save;
    pkmn_save->save_generation_type = SAVE_GENERATION_NONE;

    err = detect_savefile_generation(path, &pkmn_save->save_generation_type);
    if (err != PKSAV_ERROR_NONE)
    {
        error_handler(err, "Error detecting save file generation");
    }

    switch (pkmn_save->save_generation_type)
    {
    case SAVE_GENERATION_1:
    {
        enum pksav_gen1_save_type gen1_save_type;
        err = pksav_gen1_get_file_save_type(path, &gen1_save_type);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error getting save type");
        }
        struct pksav_gen1_save save;
        err = pksav_gen1_load_save_from_file(path, &save);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error loading save");
        }
        pkmn_save->save.gen1_save = save;
        break;
    }
    case SAVE_GENERATION_2:
    {
        enum pksav_gen2_save_type gen2_save_type;
        err = pksav_gen2_get_file_save_type(path, &gen2_save_type);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error getting save type");
        }
        struct pksav_gen2_save save;
        err = pksav_gen2_load_save_from_file(path, &save);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error loading save");
        }
        pkmn_save->save.gen2_save = save;
        break;
    }
    case SAVE_GENERATION_3:
    {
        enum pksav_gba_save_type gba_save_type;
        err = pksav_gba_get_file_save_type(path, &gba_save_type);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error getting save type");
        }
        struct pksav_gba_save save;
        err = pksav_gba_load_save_from_file(path, &save);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error loading save");
        }
        pkmn_save->save.gba_save = save;
        break;
    }
    case SAVE_GENERATION_4:
    {
        struct gen4_save save;
        err = gen4_load_save_from_file(path, &save);
        if (err != PKSAV_ERROR_NONE)
        {
            error_handler(err, "Error loading save");
        }
        pkmn_save->save.gen4_save = save;
        break;
    }
    default:
        break;
    }

    if (err == PKSAV_ERROR_NONE && save_identity(pkmn_save) != NULL)
    {
        stamp_record(pkmn_save, path);
    }
}
/**
 * @brief saves the save buffer to a path
 * @param pkmn_save a pointer to a PokemonSave struct save buffer
 * @param path the path to the save file
 */
// Gen 1 keeps a complement-of-sum checksum for each box bank (SRAM banks 2
// and 3) plus one per box; PKSav only maintains the main bank-1 checksum, so
// refresh the box ones here after every write.
static bool gen1_refresh_box_checksums(const char *path)
{
    FILE *f = fopen(path, "rb+");
    if (f == NULL)
    {
        return false;
    }
    uint8_t buf[0x8000];
    if (fread(buf, 1, sizeof(buf), f) != sizeof(buf))
    {
        fclose(f);
        return false;
    }
    for (int bank = 2; bank <= 3; bank++)
    {
        size_t base = (size_t)bank * 0x2000;
        unsigned sum = 0;
        for (size_t i = 0; i < 0x1A4C; i++)
        {
            sum += buf[base + i];
        }
        buf[base + 0x1A4C] = (uint8_t)~sum;
        for (int box = 0; box < 6; box++)
        {
            unsigned box_sum = 0;
            for (size_t i = 0; i < 0x462; i++)
            {
                box_sum += buf[base + (size_t)box * 0x462 + i];
            }
            buf[base + 0x1A4D + box] = (uint8_t)~box_sum;
        }
    }
    bool ok = fseek(f, 0x4000, SEEK_SET) == 0 && fwrite(buf + 0x4000, 1, 0x4000, f) == 0x4000;
    ok = (fclose(f) == 0) && ok;
    return ok;
}

// Serialize the buffer to `path` (no stale check, no commit step).
static pksavhelper_error write_save_raw(PokemonSave *pkmn_save, const char *path)
{
    enum pksav_error err = PKSAV_ERROR_NONE;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        err = pksav_gen1_save_save(path, &pkmn_save->save.gen1_save);
        if (err == PKSAV_ERROR_NONE && !gen1_refresh_box_checksums(path))
        {
            err = PKSAV_ERROR_FILE_IO;
        }
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_3)
    {
        err = pksav_gba_save_save(path, &pkmn_save->save.gba_save);
    }
    else if (pkmn_save->save_generation_type == SAVE_GENERATION_4)
    {
        err = gen4_save_save(path, &pkmn_save->save.gen4_save);
    }
    else
    {
        err = pksav_gen2_save_save(path, &pkmn_save->save.gen2_save);
    }

    if (err != PKSAV_ERROR_NONE)
    {
        error_handler(err, "Error saving save");
        return error_save_write_failed;
    }
    return error_none;
}

static void make_sibling_path(char *out, size_t out_size, const char *path, const char *suffix)
{
    snprintf(out, out_size, "%s%s", path, suffix);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

// Absolute form of a path (falls back to the path as given).
static void absolute_path_of(const char *p, char *out, size_t out_size)
{
#ifdef _WIN32
    if (_fullpath(out, p, out_size) == NULL)
        snprintf(out, out_size, "%s", p);
#else
    char *r = realpath(p, NULL);
    snprintf(out, out_size, "%s", r ? r : p);
    free(r);
#endif
}

static bool path_chars_equal(char x, char y)
{
#ifdef _WIN32
    x = (x == '\\') ? '/' : (char)tolower((unsigned char)x);
    y = (y == '\\') ? '/' : (char)tolower((unsigned char)y);
#endif
    return x == y;
}

// True if two existing paths are the same file on disk (hard links, junctions
// and 8.3 short names included). *known is false if either can't be opened.
static bool same_file_identity(const char *a, const char *b, bool *known)
{
#ifdef _WIN32
    HANDLE ha = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE hb = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION ia, ib;
    *known = ha != INVALID_HANDLE_VALUE && hb != INVALID_HANDLE_VALUE &&
             GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib);
    bool same = *known && ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
                ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
    if (ha != INVALID_HANDLE_VALUE)
        CloseHandle(ha);
    if (hb != INVALID_HANDLE_VALUE)
        CloseHandle(hb);
    return same;
#else
    struct stat sa, sb;
    *known = stat(a, &sa) == 0 && stat(b, &sb) == 0;
    return *known && sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

// True if `b` is `a` itself or one of the side files a write of `a` creates
// or removes (`a` + ".tmp", ".old", ".rollback", ".interrupted*"). A two-file
// write where the saves overlap like that would delete one of them: both
// sides share one .tmp/.old, or one side's cleanup removes the other save.
static bool paths_overlap(const char *a, const char *b)
{
    bool known;
    if (same_file_identity(a, b, &known))
        return true;
    char ca[4096], cb[4096];
    absolute_path_of(a, ca, sizeof(ca));
    absolute_path_of(b, cb, sizeof(cb));
    size_t i = 0;
    while (ca[i] != '\0' && cb[i] != '\0' && path_chars_equal(ca[i], cb[i]))
        i++;
    if (ca[i] == '\0' && cb[i] == '\0')
        return true; // same path (identity unknown: e.g. not created yet)
    // One is the other plus a ".suffix": a side file of it.
    return (ca[i] == '\0' && cb[i] == '.') || (cb[i] == '\0' && ca[i] == '.');
}

#define INTERRUPTED_KEEP_MAX 10

// A leftover `<save>.old` means an earlier commit was interrupted (crash or
// power loss between its steps) and may be the only copy of the pre-write
// save. Keep it as `<save>.interrupted` (or .interrupted2 ... if earlier ones
// exist) instead of deleting it. Returns false if it couldn't be kept; the
// caller must then not touch `old`.
static bool keep_interrupted_old(const char *target, const char *old)
{
    if (!file_exists(old))
        return true;
    for (int n = 1; n <= INTERRUPTED_KEEP_MAX; n++)
    {
        char kept[1120];
        char suffix[32];
        snprintf(suffix, sizeof(suffix), n == 1 ? ".interrupted" : ".interrupted%d", n);
        make_sibling_path(kept, sizeof(kept), target, suffix);
        if (file_exists(kept))
            continue;
        if (rename(old, kept) == 0)
        {
            write_to_log("Kept a leftover .old file from an interrupted save as .interrupted", E_LOG_MESSAGE_TYPE_ERROR);
            return true;
        }
        return false;
    }
    return false;
}

// Replace `target` with `tmp`: target -> old, tmp -> target. On failure the
// original is put back and false is returned (*restored says whether that
// worked). `old` is left in place on success so a caller committing several
// files can still roll back; remove() it afterwards.
static bool commit_file(const char *tmp, const char *target, const char *old, bool *had_target, bool *restored)
{
    *restored = true;
    *had_target = file_exists(target);
    if (!keep_interrupted_old(target, old) || file_exists(old))
    {
        write_to_log("Refusing to save: a leftover .old file could not be kept aside", E_LOG_MESSAGE_TYPE_ERROR);
        return false; // nothing touched
    }
    if (*had_target && rename(target, old) != 0)
    {
        return false;
    }
    if (rename(tmp, target) != 0)
    {
        if (*had_target)
        {
            *restored = rename(old, target) == 0;
        }
        return false;
    }
    return true;
}

// Undo a successful commit_file: put the old file back. The new file is moved
// aside (not deleted) until the old one is safely back, so a failed restore
// never leaves the save missing. Returns false if the old file couldn't be
// restored; `old` then still holds it.
static bool rollback_file(const char *target, const char *old, bool had_target)
{
    if (!had_target)
    {
        return remove(target) == 0;
    }
    char aside[1120];
    make_sibling_path(aside, sizeof(aside), target, ".rollback");
    remove(aside);
    if (rename(target, aside) != 0)
    {
        return false;
    }
    if (rename(old, target) != 0)
    {
        rename(aside, target);
        return false;
    }
    remove(aside);
    return true;
}

pksavhelper_error save_savefile_to_path(PokemonSave *pkmn_save, char *path)
{
    if (save_file_changed_on_disk(pkmn_save, path))
    {
        write_to_log("Refusing to save: file changed on disk since it was loaded", E_LOG_MESSAGE_TYPE_ERROR);
        return error_save_stale;
    }

    char tmp[1100], old[1100];
    make_sibling_path(tmp, sizeof(tmp), path, ".tmp");
    make_sibling_path(old, sizeof(old), path, ".old");
    remove(tmp);
    pksavhelper_error e = write_save_raw(pkmn_save, tmp);
    if (e != error_none)
    {
        remove(tmp);
        return e;
    }
    bool had_target, restored;
    if (!commit_file(tmp, path, old, &had_target, &restored))
    {
        remove(tmp);
        error_handler(PKSAV_ERROR_FILE_IO, "Error replacing save file");
        return restored ? error_save_write_failed : error_save_rollback_failed;
    }
    remove(old);
    stamp_record(pkmn_save, path);
    printf("Saved to %s\n", path);
    return error_none;
}

pksavhelper_error save_two_savefiles(PokemonSave *save1, char *path1, PokemonSave *save2, char *path2)
{
    if (paths_overlap(path1, path2))
    {
        write_to_log("Refusing to save: both sides are the same file", E_LOG_MESSAGE_TYPE_ERROR);
        return error_save_same_file;
    }
    if (save_file_changed_on_disk(save1, path1) || save_file_changed_on_disk(save2, path2))
    {
        write_to_log("Refusing to save: file changed on disk since it was loaded", E_LOG_MESSAGE_TYPE_ERROR);
        return error_save_stale;
    }

    char tmp1[1100], old1[1100], tmp2[1100], old2[1100];
    make_sibling_path(tmp1, sizeof(tmp1), path1, ".tmp");
    make_sibling_path(old1, sizeof(old1), path1, ".old");
    make_sibling_path(tmp2, sizeof(tmp2), path2, ".tmp");
    make_sibling_path(old2, sizeof(old2), path2, ".old");
    remove(tmp1);
    remove(tmp2);

    // 1. Write both to temp files; the originals are untouched so far.
    pksavhelper_error e = write_save_raw(save1, tmp1);
    if (e == error_none)
    {
        e = write_save_raw(save2, tmp2);
    }
    if (e != error_none)
    {
        remove(tmp1);
        remove(tmp2);
        return e;
    }

    // 2. Commit both, rolling the first back if the second fails.
    bool had1, had2, restored;
    if (!commit_file(tmp1, path1, old1, &had1, &restored))
    {
        remove(tmp1);
        remove(tmp2);
        error_handler(PKSAV_ERROR_FILE_IO, "Error replacing save file");
        return restored ? error_save_write_failed : error_save_rollback_failed;
    }
    if (!commit_file(tmp2, path2, old2, &had2, &restored))
    {
        bool rolled_back = rollback_file(path1, old1, had1);
        remove(tmp2);
        error_handler(PKSAV_ERROR_FILE_IO, "Error replacing save file");
        return (restored && rolled_back) ? error_save_write_failed : error_save_rollback_failed;
    }
    remove(old1);
    remove(old2);
    stamp_record(save1, path1);
    stamp_record(save2, path2);
    printf("Saved to %s and %s\n", path1, path2);
    return error_none;
}

void load_display_files(const struct save_file_data *save_file_data, PokemonSave *pkmn_saves, uint8_t *num_saves)
{
    int allocated_saves = 0;
    long save_file_size = 0;
    // Load save files once
    for (int i = 0; i < save_file_data->num_saves; i++)
    {
        // if not initialized
        if (pkmn_saves[i].save_generation_type == SAVE_GENERATION_NONE)
        {
            load_savefile_from_path(save_file_data->saves_file_path[i], &pkmn_saves[i]);
            if (pkmn_saves[i].save_generation_type != SAVE_GENERATION_NONE)
            {
                allocated_saves++;
                if (pkmn_saves[i].save_generation_type == SAVE_GENERATION_1)
                    save_file_size += sizeof(struct pksav_gen1_save);
                else if (pkmn_saves[i].save_generation_type == SAVE_GENERATION_3)
                    save_file_size += sizeof(struct pksav_gba_save);
                else if (pkmn_saves[i].save_generation_type == SAVE_GENERATION_4)
                    save_file_size += sizeof(struct gen4_save);
                else
                    save_file_size += sizeof(struct pksav_gen2_save);
            }
            else
            {
                puts("PokeromTrader: Not a pokemon save file");
            }
        }
    }
    if (allocated_saves > 0)
    {
        printf("PokeromTrader: Allocated memory (%ld bytes) for %d save files\n", save_file_size, allocated_saves);
        *num_saves = allocated_saves;
    }
}

void free_pkmn_saves(PokemonSave *pkmn_saves, uint8_t *save_file_count)
{
    if (*save_file_count == 0)
    {
        return;
    }
    uint8_t count = 0;
    printf("PokeromTrader: %u save files had been allocated\n", *save_file_count);
    for (int i = 0; i < *save_file_count; i++)
    {
        stamp_forget(save_identity(&pkmn_saves[i]));
        switch (pkmn_saves[i].save_generation_type)
        {
        case SAVE_GENERATION_1:
        {
            pksav_gen1_free_save(&pkmn_saves[i].save.gen1_save);
            count++;
            break;
        }
        case SAVE_GENERATION_2:
        {
            pksav_gen2_free_save(&pkmn_saves[i].save.gen2_save);
            count++;
            break;
        }
        case SAVE_GENERATION_3:
        {
            pksav_gba_free_save(&pkmn_saves[i].save.gba_save);
            count++;
            break;
        }
        case SAVE_GENERATION_4:
        {
            gen4_free_save(&pkmn_saves[i].save.gen4_save);
            count++;
            break;
        }
        default:
            puts("PokeromTrader: No save file to free");
            break;
        }
    }

    // reset save files to uninitialized
    for (int i = 0; i < MAX_FILE_PATH_COUNT; i++)
    {
        pkmn_saves[i].save_generation_type = SAVE_GENERATION_NONE;
    }

    *save_file_count -= count;

    printf("%d save files deallocated\n", count);

    if (*save_file_count != 0)
    {
        printf("PokeromTrader: %d save files were not deallocated\n", *save_file_count);
    }
}

// Files already backed up this session (so later operations keep the
// pre-session original instead of overwriting it with intermediate states).
#define BACKUP_SESSION_MAX 256
static char backed_up_paths[BACKUP_SESSION_MAX][1024];
static int backed_up_count = 0;

static bool copy_file_bytes(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (in == NULL)
        return false;
    FILE *out = fopen(dst, "wb");
    if (out == NULL)
    {
        fclose(in);
        return false;
    }
    bool ok = true;
    unsigned char buf[16384];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, got, out) != got)
        {
            ok = false;
            break;
        }
    }
    ok = ok && !ferror(in);
    fclose(in);
    ok = (fclose(out) == 0) && ok;
    return ok;
}

bool create_backup_save(PokemonSave *pkmn_save, char *save_path)
{
    (void)pkmn_save; // the backup is the file on disk, not the (possibly edited) buffer
    for (int i = 0; i < backed_up_count; i++)
    {
        if (same_path(backed_up_paths[i], save_path))
            return true; // pre-session original already kept
    }
    char backup_path[1100];
    make_sibling_path(backup_path, sizeof(backup_path), save_path, "_bak");
    char tmp[1110];
    make_sibling_path(tmp, sizeof(tmp), backup_path, ".tmp");
    bool ok = copy_file_bytes(save_path, tmp);
    if (ok)
    {
        remove(backup_path);
        ok = rename(tmp, backup_path) == 0;
    }
    if (!ok)
    {
        remove(tmp);
        write_to_log("Could not create backup save", E_LOG_MESSAGE_TYPE_ERROR);
        return false;
    }
    if (backed_up_count < BACKUP_SESSION_MAX && strlen(save_path) < sizeof(backed_up_paths[0]))
    {
        strcpy(backed_up_paths[backed_up_count++], save_path);
    }
    return true;
}

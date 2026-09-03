#include <stdio.h>
#include <stdbool.h>
#include "pksavfilehelper.h"

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
}
/**
 * @brief saves the save buffer to a path
 * @param pkmn_save a pointer to a PokemonSave struct save buffer
 * @param path the path to the save file
 */
// Gen 1 keeps a complement-of-sum checksum for each box bank (SRAM banks 2
// and 3) plus one per box; PKSav only maintains the main bank-1 checksum, so
// refresh the box ones here after every write.
static void gen1_refresh_box_checksums(const char *path)
{
    FILE *f = fopen(path, "rb+");
    if (f == NULL)
    {
        return;
    }
    uint8_t buf[0x8000];
    if (fread(buf, 1, sizeof(buf), f) != sizeof(buf))
    {
        fclose(f);
        return;
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
    fseek(f, 0x4000, SEEK_SET);
    fwrite(buf + 0x4000, 1, 0x4000, f);
    fclose(f);
}

pksavhelper_error save_savefile_to_path(PokemonSave *pkmn_save, char *path)
{
    enum pksav_error err = PKSAV_ERROR_NONE;
    if (pkmn_save->save_generation_type == SAVE_GENERATION_1)
    {
        err = pksav_gen1_save_save(path, &pkmn_save->save.gen1_save);
        if (err == PKSAV_ERROR_NONE)
        {
            gen1_refresh_box_checksums(path);
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
    }
    else
    {
        printf("Saved to %s\n", path);
    }

    if (err != PKSAV_ERROR_NONE)
    {
        return error_update_save;
    }
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

void create_backup_save(PokemonSave *pkmn_save, char* save_path)
{
    char backup_path[1004] = "\0";
    strcpy(backup_path, save_path);
    strcat(backup_path, "_bak");
    save_savefile_to_path(pkmn_save, backup_path);
}

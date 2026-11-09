#include <stdio.h>
#include <string.h>
#include "pksavhelper.h"

static enum pksav_error SAVE_FILE_ERROR = PKSAV_ERROR_NONE;

enum pksav_error detect_savefile_generation(const char *path, SaveGenerationType *save_generation_type);
void load_savefile_from_path(const char *path, PokemonSave *pkmn_save);
pksavhelper_error save_savefile_to_path(PokemonSave *pkmn_save, char *path);
void load_display_files(const struct save_file_data *save_file_data, PokemonSave *pkmn_saves, uint8_t *num_saves);
void free_pkmn_saves(PokemonSave *pkmn_saves, uint8_t *save_file_count);
void free_trade_saves(void);
void free_evolve_saves(void);
// Drop the cached saves of the two-file (Trade/Transfer) or single-file
// (Boxes/Evolve/Events/Pokedex) select screens so they are re-read from disk
// on the next frame, and show `message` there. Use after a failed or refused
// write: the in-memory copies can no longer be trusted.
void trade_saves_force_reload(const char *message);
void single_saves_force_reload(const char *message);
// Back up the ON-DISK file (a byte copy, independent of the in-memory buffer)
// to "<path>_bak", once per app session per file, so the backup is always the
// pre-session original. Returns false if the copy failed.
bool create_backup_save(PokemonSave *pkmn_save, char* save_path);
// True if `path` no longer matches what was loaded into / last written from
// this save buffer (an emulator or another screen wrote it meanwhile).
bool save_file_changed_on_disk(const PokemonSave *pkmn_save, const char *path);
// Write two saves as one unit: both are written to temp files first and only
// committed (renamed over the originals) if both writes succeed; if a commit
// step fails, the already-committed file is rolled back. On any failure the
// on-disk files are unchanged, but the in-memory saves may not match them any
// more -- reload them from disk. save1 is committed first: for a transfer pass
// the RECEIVING save as save1, so a crash between the two commits leaves the
// mon in both saves rather than in neither. Refuses (error_save_same_file) if
// both paths name the same file.
pksavhelper_error save_two_savefiles(PokemonSave *save1, char *path1, PokemonSave *save2, char *path2);
// Human-readable reason for a save-write error.
const char *save_error_message(pksavhelper_error err);

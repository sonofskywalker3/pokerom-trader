#include <stdio.h>
#include <string.h>
#include "raylibhelper.h"
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen_transfer.h"
#include "gen4_transfer.h"

// Can this source mon be sent up to the destination generation?
static bool transfer_species_allowed(uint16_t dex, SaveGenerationType dest_gen)
{
    if (dest_gen == SAVE_GENERATION_4)
    {
        return dex >= 1 && dex <= 386; // any Gen 3 species Pal Parks (eggs refused later)
    }
    return gen3_species_obtainable(dex); // Gen 1/2 -> Gen 3
}

// One-way forward transfer: move a party Pokémon up a generation. Destination is
// the higher-generation save (Gen 1/2 -> Gen 3, or Gen 3 -> Gen 4 via Pal Park).
void draw_transfer(PokemonSave *save_player1, PokemonSave *save_player2, char *player1_save_path, char *player2_save_path, struct trainer_info *trainer1, struct trainer_info *trainer2, GameScreen *current_screen)
{
    // The higher generation is the destination; the lower is the source.
    PokemonSave *dest, *source;
    char *dest_path, *source_path;
    struct trainer_info *dest_tr, *source_tr;
    if (save_player1->save_generation_type > save_player2->save_generation_type)
    {
        dest = save_player1; dest_path = player1_save_path; dest_tr = trainer1;
        source = save_player2; source_path = player2_save_path; source_tr = trainer2;
    }
    else
    {
        dest = save_player2; dest_path = player2_save_path; dest_tr = trainer2;
        source = save_player1; source_path = player1_save_path; source_tr = trainer1;
    }
    SaveGenerationType dest_gen = dest->save_generation_type;
    bool to_gen4 = (dest_gen == SAVE_GENERATION_4);

    static int selected = -1;
    static bool show_saving_icon = false;
    static bool show_ok_toast = false;
    static bool show_err_toast = false;
    static const char *err_text = "Can't transfer that one";
    bool force_reload = false;
    const char *reload_reason = NULL;

    int party_count = bills_pc_container_count(source, BILLS_PC_LOC_PARTY, 0);
    if (selected >= party_count)
    {
        selected = -1;
    }

    begin_virtual_frame();
    ClearBackground(RED);
    draw_background_grid();

    shadow_text("Transfer  (one-way, up a generation)", 40, 12, 20, WHITE);
    char sname[15] = "\0", dname[15] = "\0";
    create_trainer_name_str(source_tr, sname);
    create_trainer_name_str(dest_tr, dname);
    shadow_text(TextFormat("%s  ->  %s", sname, dname), 40, 40, 18, WHITE);

    // Source party list (left)
    Rectangle list_rec = (Rectangle){40, 80, 260, 300};
    DrawRectangle(list_rec.x - 4, list_rec.y - 4, list_rec.width + 8, list_rec.height + 8, (Color){0, 0, 0, 90});
    for (int i = 0; i < party_count; i++)
    {
        struct bills_pc_entry_view v;
        bills_pc_get_view(source, BILLS_PC_LOC_PARTY, 0, i, &v);
        bool ok = transfer_species_allowed(v.dex, dest_gen);
        Rectangle row = (Rectangle){list_rec.x, list_rec.y + i * 30, list_rec.width, 28};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        if (selected == i)
        {
            DrawRectangleRec(row, COLOR_PKMN_YELLOW);
        }
        else if (hovered)
        {
            DrawRectangleRec(row, (Color){255, 255, 255, 60});
        }
        Color tc = selected == i ? BLACK : (ok ? WHITE : (Color){200, 200, 200, 130});
        DrawText(v.nickname, (int)row.x + 8, (int)row.y + 4, 20, tc);
        // Right-align the level, and put the "can't transfer" mark left of it
        // (it used to be drawn on top of a three-digit level).
        int right_x = (int)(row.x + row.width) - 6;
        if (v.level > 0)
        {
            const char *lv = TextFormat("Lv%u", v.level);
            right_x -= MeasureText(lv, 20);
            DrawText(lv, right_x, (int)row.y + 4, 20, tc);
        }
        if (!ok)
        {
            DrawText("x", right_x - 18, (int)row.y + 4, 20, (Color){255, 120, 120, 255});
        }
        if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            selected = (selected == i) ? -1 : i;
        }
    }
    if (party_count == 0)
    {
        shadow_text("Source party is empty.", (int)list_rec.x + 10, (int)list_rec.y + 20, 20, WHITE);
    }

    // Detail panel (right)
    Rectangle info_rec = (Rectangle){320, 80, SCREEN_WIDTH - 360, 300};
    DrawRectangle(info_rec.x - 4, info_rec.y - 4, info_rec.width + 8, info_rec.height + 8, (Color){0, 0, 0, 110});
    int dest_party = bills_pc_container_count(dest, BILLS_PC_LOC_PARTY, 0);
    shadow_text(TextFormat("Destination party: %d/6", dest_party), (int)info_rec.x + 10, (int)info_rec.y + 10, 18, WHITE);
    shadow_text("(full party -> first free box slot)", (int)info_rec.x + 10, (int)info_rec.y + 34, 14, WHITE);

    bool can_transfer = false;
    if (selected >= 0 && selected < party_count)
    {
        struct bills_pc_entry_view v;
        bills_pc_get_view(source, BILLS_PC_LOC_PARTY, 0, selected, &v);
        bool ok = transfer_species_allowed(v.dex, dest_gen);
        shadow_text(v.nickname, (int)info_rec.x + 10, (int)info_rec.y + 74, 22, WHITE);
        shadow_text(TextFormat("National Dex #%u", v.dex), (int)info_rec.x + 10, (int)info_rec.y + 104, 16, WHITE);
        if (ok)
        {
            shadow_text(to_gen4 ? "Pal Park to Gen 4 - keeps IVs," : "Obtainable in Gen 3 - OK to",
                        (int)info_rec.x + 10, (int)info_rec.y + 140, 16, (Color){200, 255, 200, 255});
            shadow_text(to_gen4 ? "moves, OT; met at Pal Park." : "transfer (rebuilds stats, keeps OT).",
                        (int)info_rec.x + 10, (int)info_rec.y + 162, 16, (Color){200, 255, 200, 255});
            can_transfer = true;
        }
        else if (to_gen4)
        {
            shadow_text("Not a Gen 3 species", (int)info_rec.x + 10, (int)info_rec.y + 140, 16, (Color){255, 180, 180, 255});
            shadow_text("(cannot Pal Park). Blocked.", (int)info_rec.x + 10, (int)info_rec.y + 162, 16, (Color){255, 180, 180, 255});
        }
        else
        {
            shadow_text("No legal Gen 3 home", (int)info_rec.x + 10, (int)info_rec.y + 140, 16, (Color){255, 180, 180, 255});
            shadow_text("(event-only: Mew / Celebi).", (int)info_rec.x + 10, (int)info_rec.y + 162, 16, (Color){255, 180, 180, 255});
            shadow_text("Transfer blocked.", (int)info_rec.x + 10, (int)info_rec.y + 184, 16, (Color){255, 180, 180, 255});
        }
    }
    else if (party_count > 0)
    {
        shadow_text("Pick a Pokemon to send up.", (int)info_rec.x + 10, (int)info_rec.y + 74, 18, WHITE);
    }

    // Bottom bar
    const Rectangle bottom_bar_rec = (Rectangle){0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 100};
    DrawRectangleRec(bottom_bar_rec, WHITE);
    DrawLineEx((Vector2){bottom_bar_rec.x, bottom_bar_rec.y}, (Vector2){bottom_bar_rec.width, bottom_bar_rec.y}, 15, BLACK);

    const Rectangle back_button_rec = (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    bool back_hover = CheckCollisionPointRec(GetMousePosition(), back_button_rec);
    DrawText("< Back", (int)back_button_rec.x + 15, (int)back_button_rec.y + 10, 20, back_hover ? LIGHTGRAY : BLACK);

    const Rectangle xfer_button_rec = (Rectangle){NEXT_BUTTON_X - 30, NEXT_BUTTON_Y + 8, BUTTON_WIDTH + 30, BUTTON_HEIGHT};
    bool xfer_hover = CheckCollisionPointRec(GetMousePosition(), xfer_button_rec);
    DrawText("Transfer >", (int)xfer_button_rec.x + 5, (int)xfer_button_rec.y + 10, 20, can_transfer ? (xfer_hover ? LIGHTGRAY : BLACK) : LIGHTGRAY);

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        if (back_hover)
        {
            selected = -1;
            *current_screen = SCREEN_FILE_SELECT;
        }
        else if (xfer_hover && can_transfer)
        {
            if (save_file_changed_on_disk(source, source_path) || save_file_changed_on_disk(dest, dest_path))
            {
                force_reload = true;
                reload_reason = "A save changed on disk since it was loaded. Reloaded; nothing was transferred.";
            }
            else if (!create_backup_save(source, source_path) || !create_backup_save(dest, dest_path))
            {
                err_text = "Couldn't back up the saves; nothing was transferred";
                show_err_toast = true;
            }
            else
            {
                pksavhelper_error e = to_gen4
                                          ? transfer_pkmn_gen3_to_gen4(source, (uint8_t)selected, dest)
                                          : transfer_pkmn_to_gen3(source, (uint8_t)selected, dest);
                if (e == error_none)
                {
                    // Both files go to temp files and are committed together:
                    // a failure can't lose the mon (or leave it in both saves).
                    // The destination is committed first, so even a crash
                    // between the two commits can't lose it.
                    pksavhelper_error w = save_two_savefiles(dest, dest_path, source, source_path);
                    if (w == error_none)
                    {
                        create_trainer(source, source_tr);
                        create_trainer(dest, dest_tr);
                        selected = -1;
                        show_saving_icon = true;
                        show_ok_toast = true;
                    }
                    else
                    {
                        force_reload = true;
                        reload_reason = w == error_save_stale
                                            ? "A save changed on disk since it was loaded. Reloaded; nothing was transferred."
                                            : "Saving failed; nothing was transferred. Saves reloaded from disk.";
                    }
                }
                else
                {
                    // The transfer functions refuse before touching either save.
                    switch (e)
                    {
                    case error_transfer_last_pkmn:
                        err_text = "Can't send the last Pokemon in the party";
                        break;
                    case error_transfer_hm_move:
                        err_text = "Pal Park refuses Pokemon that know an HM";
                        break;
                    case error_transfer_no_space:
                        err_text = "No room in the destination party or boxes";
                        break;
                    default:
                        err_text = "Can't transfer that one";
                        break;
                    }
                    show_err_toast = true;
                }
            }
        }
    }

    if (show_saving_icon)
    {
        show_saving_icon = draw_save_icon(SCREEN_WIDTH - 50, 12, show_saving_icon);
    }
    if (show_ok_toast)
    {
        show_ok_toast = !draw_toast_message(to_gen4 ? "Transferred up to Gen 4!" : "Transferred up to Gen 3!", TOAST_SHORT, TOAST_SUCCESS);
    }
    if (show_err_toast)
    {
        show_err_toast = !draw_toast_message((char *)err_text, TOAST_SHORT, TOAST_ERROR);
    }

    end_virtual_frame();

    if (force_reload)
    {
        selected = -1;
        show_saving_icon = false;
        show_ok_toast = false;
        show_err_toast = false;
        trainer1->trainer_id = 0;
        trainer2->trainer_id = 0;
        save_player1->save_generation_type = SAVE_GENERATION_NONE;
        save_player2->save_generation_type = SAVE_GENERATION_NONE;
        trade_saves_force_reload(reload_reason);
        *current_screen = SCREEN_FILE_SELECT;
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        selected = -1;
        *current_screen = SCREEN_FILE_SELECT;
    }
}

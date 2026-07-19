#include <stdio.h>
#include <string.h>
#include "raylibhelper.h"
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen_transfer.h"

// One-way forward transfer: move a Gen 1/2 party Pokémon up into a Gen 3 save.
void draw_transfer(PokemonSave *save_player1, PokemonSave *save_player2, char *player1_save_path, char *player2_save_path, struct trainer_info *trainer1, struct trainer_info *trainer2, GameScreen *current_screen)
{
    // One save is Gen 3 (destination), the other is Gen 1/2 (source).
    PokemonSave *dest, *source;
    char *dest_path, *source_path;
    struct trainer_info *dest_tr, *source_tr;
    if (save_player1->save_generation_type == SAVE_GENERATION_3)
    {
        dest = save_player1; dest_path = player1_save_path; dest_tr = trainer1;
        source = save_player2; source_path = player2_save_path; source_tr = trainer2;
    }
    else
    {
        dest = save_player2; dest_path = player2_save_path; dest_tr = trainer2;
        source = save_player1; source_path = player1_save_path; source_tr = trainer1;
    }

    static int selected = -1;
    static bool show_saving_icon = false;
    static bool show_ok_toast = false;
    static bool show_err_toast = false;

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
        bool ok = gen3_species_obtainable(v.dex);
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
        if (v.level > 0)
        {
            DrawText(TextFormat("Lv%u", v.level), (int)(row.x + row.width) - 60, (int)row.y + 4, 20, tc);
        }
        if (!ok)
        {
            DrawText("x", (int)(row.x + row.width) - 20, (int)row.y + 4, 20, (Color){255, 120, 120, 255});
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
        bool ok = gen3_species_obtainable(v.dex);
        shadow_text(v.nickname, (int)info_rec.x + 10, (int)info_rec.y + 74, 22, WHITE);
        shadow_text(TextFormat("National Dex #%u", v.dex), (int)info_rec.x + 10, (int)info_rec.y + 104, 16, WHITE);
        if (ok)
        {
            shadow_text("Obtainable in Gen 3 - OK to", (int)info_rec.x + 10, (int)info_rec.y + 140, 16, (Color){200, 255, 200, 255});
            shadow_text("transfer (rebuilds stats, keeps OT).", (int)info_rec.x + 10, (int)info_rec.y + 162, 16, (Color){200, 255, 200, 255});
            can_transfer = true;
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
            create_backup_save(source, source_path);
            create_backup_save(dest, dest_path);
            pksavhelper_error e = transfer_pkmn_to_gen3(source, (uint8_t)selected, dest);
            if (e == error_none)
            {
                save_savefile_to_path(source, source_path);
                save_savefile_to_path(dest, dest_path);
                create_trainer(source, source_tr);
                create_trainer(dest, dest_tr);
                selected = -1;
                show_saving_icon = true;
                show_ok_toast = true;
            }
            else
            {
                show_err_toast = true;
            }
        }
    }

    if (show_saving_icon)
    {
        show_saving_icon = draw_save_icon(SCREEN_WIDTH - 50, 12, show_saving_icon);
    }
    if (show_ok_toast)
    {
        show_ok_toast = !draw_toast_message("Transferred up to Gen 3!", TOAST_SHORT, TOAST_SUCCESS);
    }
    if (show_err_toast)
    {
        show_err_toast = !draw_toast_message("Can't transfer that one", TOAST_SHORT, TOAST_ERROR);
    }

    end_virtual_frame();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        selected = -1;
        *current_screen = SCREEN_FILE_SELECT;
    }
}

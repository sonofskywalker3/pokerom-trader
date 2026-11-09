#include "raylibhelper.h"
#include "pksavfilehelper.h"

void draw_trade(PokemonSave *save_player1, PokemonSave *save_player2, char *player1_save_path, char *player2_save_path, struct TrainerSelection trainerSelection[2], struct trainer_info *trainer1, struct trainer_info *trainer2, bool *is_same_generation, GameScreen *current_screen, Texture2D *trade_texture)
{
    // Update
    int selected_index_trainer1 = trainerSelection[0].pkmn_party_index;
    int selected_index_trainer2 = trainerSelection[1].pkmn_party_index;
    static enum ui_selections {
        BUTTON_NONE = -1,
        BUTTON_TRADE,
        BUTTON_BACK,
    } ui_selection = BUTTON_NONE;
    static bool show_trade_toast = false;
    static bool show_saving_icon = false;
    bool force_reload = false;
    const char *reload_reason = NULL;

    begin_virtual_frame();
    ClearBackground(RED);
    draw_background_grid();

    DrawTextureEx(*trade_texture, (Vector2){50, 10}, 0, 0.8, WHITE);

    bool is_valid_trade[2] = {true, true};
    draw_trainer_info(trainer1, 25, (SCREEN_HEIGHT - 100) / 2 - 75, trainerSelection, *is_same_generation, is_valid_trade);
    draw_trainer_info(trainer2, SCREEN_WIDTH - 178, (SCREEN_HEIGHT - 100) / 2 - 75, trainerSelection, *is_same_generation, is_valid_trade);
    bool can_submit_trade = trainerSelection[0].pkmn_party_index != -1 && trainerSelection[1].pkmn_party_index != -1 && is_valid_trade[0] == true && is_valid_trade[1] == true;

    // Bottom bar
    const Rectangle bottom_bar_rec = (Rectangle){0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 100};
    DrawRectangleRec(bottom_bar_rec, WHITE);
    DrawLineEx((Vector2){bottom_bar_rec.x, bottom_bar_rec.y}, (Vector2){bottom_bar_rec.width, bottom_bar_rec.y}, 15, BLACK);

    const Rectangle trade_button_rec = (Rectangle){NEXT_BUTTON_X - 15, NEXT_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    DrawText("Trade !", trade_button_rec.x + 15, trade_button_rec.y + 10, 20, can_submit_trade ? ui_selection == BUTTON_TRADE ? LIGHTGRAY : BLACK : LIGHTGRAY);

    const Rectangle back_button_rec = (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    DrawText("< Back", back_button_rec.x + 15, back_button_rec.y + 10, 20, ui_selection == BUTTON_BACK ? LIGHTGRAY : BLACK);

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){NEXT_BUTTON_X - 15, NEXT_BUTTON_Y - 30, BUTTON_WIDTH, BUTTON_HEIGHT}))
        {
            ui_selection = BUTTON_TRADE;
        }
        else if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y - 30, BUTTON_WIDTH, BUTTON_HEIGHT}))
        {
            ui_selection = BUTTON_BACK;
        }
        else
        {
            ui_selection = BUTTON_NONE;
        }
    }

    static pksavhelper_error trade_error = error_none;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && ui_selection != BUTTON_NONE)
    {
        switch (ui_selection)
        {
        case BUTTON_TRADE:
            if (can_submit_trade && CheckCollisionPointRec(GetMousePosition(), (Rectangle){NEXT_BUTTON_X - 15, NEXT_BUTTON_Y - 30, BUTTON_WIDTH, BUTTON_HEIGHT}))
            {
                // Reset Trading state
                trainerSelection[0].pkmn_party_index = -1;
                trainerSelection[1].pkmn_party_index = -1;

                if (show_trade_toast)
                {
                    reset_toast_message();
                }

                // Refuse before touching anything if a file changed on disk
                // (e.g. the emulator saved) since it was loaded.
                if (save_file_changed_on_disk(save_player1, player1_save_path) || save_file_changed_on_disk(save_player2, player2_save_path))
                {
                    force_reload = true;
                    reload_reason = "A save changed on disk since it was loaded. Reloaded; nothing was traded.";
                    break;
                }

                // create backup (a copy of the on-disk files) before trading
                if (!create_backup_save(save_player1, player1_save_path) || !create_backup_save(save_player2, player2_save_path))
                {
                    trade_error = error_save_write_failed;
                    show_trade_toast = true;
                    break;
                }

                if (*is_same_generation)
                {
                    trade_error = swap_pkmn_at_index_between_saves(save_player1, save_player2, selected_index_trainer1, selected_index_trainer2);
                }
                else
                {
                    trade_error = swap_pkmn_at_index_between_saves_cross_gen(save_player1, save_player2, selected_index_trainer1, selected_index_trainer2);
                    if (trade_error == error_none)
                    {
                        // Gen 1 has one Special, Gen 2 split it: recompute the
                        // traded mons' stats for their new generation.
                        update_pkmn_stats(save_player1, selected_index_trainer1);
                        update_pkmn_stats(save_player2, selected_index_trainer2);
                    }
                }
                if (trade_error != error_none)
                {
                    // A half-done swap leaves the buffers inconsistent: reload.
                    force_reload = true;
                    reload_reason = "Trade failed; nothing was saved. Saves reloaded from disk.";
                    break;
                }
                pksavhelper_error dex1 = update_seen_owned_pkmn(save_player1, selected_index_trainer1);
                pksavhelper_error dex2 = update_seen_owned_pkmn(save_player2, selected_index_trainer2);
                // Also heal any historical dex gaps while we're writing dex bits
                pokedex_reconcile(save_player1);
                pokedex_reconcile(save_player2);
                // Both files are written to temp files and only committed
                // together, so a failure can't leave one side traded (a clone
                // or a lost mon).
                trade_error = save_two_savefiles(save_player1, player1_save_path, save_player2, player2_save_path);
                if (trade_error != error_none)
                {
                    force_reload = true;
                    reload_reason = trade_error == error_save_stale
                                        ? "A save changed on disk since it was loaded. Reloaded; nothing was traded."
                                        : "Saving failed; nothing was traded. Saves reloaded from disk.";
                    break;
                }
                if (dex1 != error_none || dex2 != error_none)
                {
                    trade_error = error_update_pokedex;
                }
                show_saving_icon = true;

                // Recreate trainer info with updated saves
                create_trainer(save_player1, trainer1);
                create_trainer(save_player2, trainer2);

                show_trade_toast = true;
            }
            else
            {
                ui_selection = BUTTON_NONE;
            }
            break;
        case BUTTON_BACK:
            if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y - 30, BUTTON_WIDTH, BUTTON_HEIGHT}))
            {
                trainer1->trainer_id = 0;
                trainer2->trainer_id = 0;
                trainerSelection[0].pkmn_party_index = -1;
                trainerSelection[1].pkmn_party_index = -1;
                reset_details_panel();
                reset_toast_message();
                show_trade_toast = false;
                *current_screen = SCREEN_FILE_SELECT;

                ui_selection = BUTTON_NONE;
            }
            break;
        default:
            break;
        }
        ui_selection = BUTTON_NONE;
    }

    // Draw Toast with status message
    if (show_trade_toast)
    {
        const char *toast_message;
        switch (trade_error)
        {
        case error_none:
            toast_message = "Trade successful!";
            break;
        case error_update_pokedex:
            toast_message = "Traded, but the Pokedex update failed";
            break;
        case error_swap_pkmn:
            toast_message = "Pokemon trade failed!";
            break;
        default:
            toast_message = save_error_message(trade_error);
            break;
        }

        show_trade_toast = !draw_toast_message((char *)toast_message, TOAST_SHORT, trade_error == error_none ? TOAST_INFO : TOAST_ERROR);
    }
    else
    {
        trade_error = error_none;
    }
    if (show_saving_icon)
    {
        show_saving_icon = draw_save_icon(SCREEN_WIDTH - 50, 10, show_saving_icon);
    }

    end_virtual_frame();

    if (force_reload)
    {
        trainer1->trainer_id = 0;
        trainer2->trainer_id = 0;
        trainerSelection[0].pkmn_party_index = -1;
        trainerSelection[1].pkmn_party_index = -1;
        reset_details_panel();
        show_trade_toast = false;
        show_saving_icon = false;
        trade_error = error_none;
        ui_selection = BUTTON_NONE;
        save_player1->save_generation_type = SAVE_GENERATION_NONE;
        save_player2->save_generation_type = SAVE_GENERATION_NONE;
        trade_saves_force_reload(reload_reason);
        *current_screen = SCREEN_FILE_SELECT;
        return;
    }

    // ESCAPE nav back to file select
    if (IsKeyPressed(KEY_ESCAPE))
    {
        trainer1->trainer_id = 0;
        trainer2->trainer_id = 0;
        trainerSelection[0].pkmn_party_index = -1;
        trainerSelection[1].pkmn_party_index = -1;
        reset_details_panel();
        reset_toast_message();
        show_trade_toast = false;
        *current_screen = SCREEN_FILE_SELECT;

        ui_selection = BUTTON_NONE;
    }
}

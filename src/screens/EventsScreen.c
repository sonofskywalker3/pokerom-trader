#include <stdio.h>
#include <string.h>
#include "raylibhelper.h"
#include "pksavfilehelper.h"
#include "events.h"

// Draw word-wrapped text within a max width.
static void draw_wrapped(const char *text, int x, int y, int max_w, int font, Color color)
{
    char line[192] = "";
    char word[96];
    int ty = y;
    const char *p = text;
    while (*p)
    {
        int wl = 0;
        while (*p && *p != ' ' && wl < (int)sizeof(word) - 1)
        {
            word[wl++] = *p++;
        }
        word[wl] = '\0';
        while (*p == ' ')
        {
            p++;
        }
        char test[288];
        if (line[0])
        {
            snprintf(test, sizeof(test), "%s %s", line, word);
        }
        else
        {
            snprintf(test, sizeof(test), "%s", word);
        }
        if (line[0] && MeasureText(test, font) > max_w)
        {
            shadow_text(line, x, ty, font, color);
            ty += font + 6;
            snprintf(line, sizeof(line), "%s", word);
        }
        else
        {
            snprintf(line, sizeof(line), "%s", test);
        }
    }
    if (line[0])
    {
        shadow_text(line, x, ty, font, color);
    }
}

void draw_events(PokemonSave *pkmn_save, char *save_path, struct trainer_info *trainer, GameScreen *current_screen)
{
    static bool needs_init = true;
    static int selected = -1;
    static bool show_saving_icon = false;
    static bool show_applied_toast = false;
    static bool show_error_toast = false;

    if (needs_init)
    {
        selected = -1;
        needs_init = false;
    }

    const struct pkmn_event *events[8];
    int n = events_available(pkmn_save, events, 8);
    if (selected >= n)
    {
        selected = -1;
    }

    begin_virtual_frame();
    ClearBackground(RED);
    draw_background_grid();

    shadow_text("Events", 50, 12, 24, WHITE);
    char trainer_name[15] = "\0";
    create_trainer_name_str(trainer, trainer_name);
    char trainer_id[11] = "\0";
    create_trainer_id_str(trainer, trainer_id);
    shadow_text(trainer_name, 50, 44, 20, WHITE);
    shadow_text(trainer_id, 50, 68, 20, WHITE);

    // List panel
    Rectangle list_rec = (Rectangle){40, 100, 260, 280};
    DrawRectangle(list_rec.x - 4, list_rec.y - 4, list_rec.width + 8, list_rec.height + 8, (Color){0, 0, 0, 90});

    if (n == 0)
    {
        shadow_text("No re-issuable events", (int)list_rec.x + 10, (int)list_rec.y + 20, 20, WHITE);
        shadow_text("for this game.", (int)list_rec.x + 10, (int)list_rec.y + 46, 20, WHITE);
    }

    for (int i = 0; i < n; i++)
    {
        Rectangle row = (Rectangle){list_rec.x, list_rec.y + i * 46, list_rec.width, 42};
        bool hovered = CheckCollisionPointRec(GetMousePosition(), row);
        if (selected == i)
        {
            DrawRectangleRec(row, COLOR_PKMN_YELLOW);
        }
        else if (hovered)
        {
            DrawRectangleRec(row, (Color){255, 255, 255, 60});
        }
        Color tc = selected == i ? BLACK : WHITE;
        DrawText(events[i]->ticket, (int)row.x + 8, (int)row.y + 3, 20, tc);
        DrawText(TextFormat("-> %s", events[i]->pokemon), (int)row.x + 8, (int)row.y + 23, 15, tc);
        if (hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            selected = (selected == i) ? -1 : i;
        }
    }

    // Detail panel
    Rectangle info_rec = (Rectangle){320, 100, SCREEN_WIDTH - 360, 280};
    DrawRectangle(info_rec.x - 4, info_rec.y - 4, info_rec.width + 8, info_rec.height + 8, (Color){0, 0, 0, 110});
    if (selected >= 0 && selected < n)
    {
        const struct pkmn_event *ev = events[selected];
        shadow_text(ev->ticket, (int)info_rec.x + 10, (int)info_rec.y + 12, 22, COLOR_PKMN_YELLOW);
        shadow_text(TextFormat("Unlocks: %s", ev->pokemon), (int)info_rec.x + 10, (int)info_rec.y + 46, 20, WHITE);
        shadow_text("After adding the ticket, in-game:", (int)info_rec.x + 10, (int)info_rec.y + 84, 16, WHITE);
        draw_wrapped(ev->location, (int)info_rec.x + 10, (int)info_rec.y + 108, (int)info_rec.width - 20, 18, WHITE);
        shadow_text("The game creates a legit Pokemon.", (int)info_rec.x + 10, (int)info_rec.y + 240, 14, (Color){200, 255, 200, 255});
    }
    else if (n > 0)
    {
        shadow_text("Select an event to re-issue", (int)info_rec.x + 10, (int)info_rec.y + 20, 18, WHITE);
        shadow_text("its ticket.", (int)info_rec.x + 10, (int)info_rec.y + 44, 18, WHITE);
    }

    // Bottom bar
    const Rectangle bottom_bar_rec = (Rectangle){0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 100};
    DrawRectangleRec(bottom_bar_rec, WHITE);
    DrawLineEx((Vector2){bottom_bar_rec.x, bottom_bar_rec.y}, (Vector2){bottom_bar_rec.width, bottom_bar_rec.y}, 15, BLACK);

    const Rectangle back_button_rec = (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    bool back_hover = CheckCollisionPointRec(GetMousePosition(), back_button_rec);
    DrawText("< Back", (int)back_button_rec.x + 15, (int)back_button_rec.y + 10, 20, back_hover ? LIGHTGRAY : BLACK);

    bool can_apply = selected >= 0 && selected < n;
    const Rectangle apply_button_rec = (Rectangle){NEXT_BUTTON_X - 25, NEXT_BUTTON_Y + 8, BUTTON_WIDTH + 20, BUTTON_HEIGHT};
    bool apply_hover = CheckCollisionPointRec(GetMousePosition(), apply_button_rec);
    DrawText("Add Ticket", (int)apply_button_rec.x + 5, (int)apply_button_rec.y + 10, 20, can_apply ? (apply_hover ? LIGHTGRAY : BLACK) : LIGHTGRAY);

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        if (back_hover)
        {
            needs_init = true;
            *current_screen = SCREEN_EVENTS_FILE_SELECT;
        }
        else if (apply_hover && can_apply)
        {
            create_backup_save(pkmn_save, save_path);
            pksavhelper_error e = apply_event(pkmn_save, events[selected]);
            if (e == error_none)
            {
                save_savefile_to_path(pkmn_save, save_path);
                create_trainer(pkmn_save, trainer);
                show_saving_icon = true;
                show_applied_toast = true;
            }
            else
            {
                show_error_toast = true; // already have it / no room
            }
        }
    }

    if (show_saving_icon)
    {
        show_saving_icon = draw_save_icon(SCREEN_WIDTH - 50, 44, show_saving_icon);
    }
    if (show_applied_toast)
    {
        show_applied_toast = !draw_toast_message("Ticket added! Now play to the spot.", TOAST_MEDIUM, TOAST_SUCCESS);
    }
    if (show_error_toast)
    {
        show_error_toast = !draw_toast_message("Already have that ticket", TOAST_SHORT, TOAST_ERROR);
    }

    end_virtual_frame();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        needs_init = true;
        *current_screen = SCREEN_EVENTS_FILE_SELECT;
    }
}

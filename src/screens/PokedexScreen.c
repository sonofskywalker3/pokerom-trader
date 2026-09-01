#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "raylibhelper.h"
#include "pksavhelper.h"
#include "gen4_stats.h" // gen4_species_name — national names, covers every gen's range

// Pokédex viewer — strictly read-only. Every entry shows its name: grey when
// unseen, black when seen, with a Poké Ball icon when owned. Dex bits are only
// ever written by real trades/transfers, never from this screen.

#define DEX_PANEL_X 30
#define DEX_PANEL_Y 46
#define DEX_PANEL_W (SCREEN_WIDTH - 60)
#define DEX_PANEL_H 372
#define DEX_ROW_H 24
#define DEX_COLS 2
#define DEX_COL_W (DEX_PANEL_W / DEX_COLS)

static bool dex_ci_contains(const char *haystack, const char *needle)
{
    if (needle[0] == '\0')
        return true;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++)
    {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return true;
    }
    return false;
}

static bool dex_entry_matches(uint16_t dex, const char *query)
{
    if (query[0] == '\0')
        return true;
    if (dex_ci_contains(gen4_species_name(dex), query))
        return true;
    char num[8];
    snprintf(num, sizeof(num), "%u", dex);
    return dex_ci_contains(num, query);
}

// Tiny vector Poké Ball: red cap, white base, band, center button.
static void draw_pokeball_icon(int cx, int cy, int r)
{
    DrawCircleSector((Vector2){(float)cx, (float)cy}, (float)r, 180, 360, 16, RED);
    DrawCircleSector((Vector2){(float)cx, (float)cy}, (float)r, 0, 180, 16, RAYWHITE);
    DrawRectangle(cx - r, cy - 1, 2 * r + 1, 2, BLACK);
    DrawCircleLines(cx, cy, (float)r, BLACK);
    DrawCircle(cx, cy, (float)(r / 3 + 1), RAYWHITE);
    DrawCircleLines(cx, cy, (float)(r / 3 + 1), BLACK);
}

void draw_pokedex(PokemonSave *pkmn_save, struct TrainerSelection *trainerSelection, GameScreen *current_screen)
{
    static bool needs_init = true;
    static int scroll_row = 0;
    static char query[24] = "\0";
    static int query_len = 0;
    static bool search_active = false;
    static uint16_t seen_total = 0, owned_total = 0;

    uint16_t max = pokedex_species_count(pkmn_save);

    if (needs_init)
    {
        scroll_row = 0;
        query[0] = '\0';
        query_len = 0;
        search_active = false;
        pokedex_reconcile(pkmn_save); // heal dex gaps for mons actually present
        pokedex_counts(pkmn_save, &seen_total, &owned_total);
        needs_init = false;
    }

    // Search text entry
    if (search_active)
    {
        int ch = GetCharPressed();
        while (ch > 0)
        {
            if (ch >= 32 && ch <= 125 && query_len < (int)sizeof(query) - 1)
            {
                query[query_len++] = (char)ch;
                query[query_len] = '\0';
                scroll_row = 0;
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && query_len > 0)
        {
            query[--query_len] = '\0';
            scroll_row = 0;
        }
    }

    // Build the filtered entry list (dex numbers)
    static uint16_t filtered[493];
    int num_filtered = 0;
    for (uint16_t dex = 1; dex <= max; dex++)
    {
        if (dex_entry_matches(dex, query))
            filtered[num_filtered++] = dex;
    }

    int visible_rows = DEX_PANEL_H / DEX_ROW_H;
    int total_rows = (num_filtered + DEX_COLS - 1) / DEX_COLS;
    int max_scroll = total_rows > visible_rows ? total_rows - visible_rows : 0;

    float wheel = GetMouseWheelMove();
    if (wheel != 0)
    {
        scroll_row -= (int)(wheel * 3);
    }
    if (scroll_row > max_scroll)
        scroll_row = max_scroll;
    if (scroll_row < 0)
        scroll_row = 0;

    begin_virtual_frame();
    ClearBackground(RED);
    draw_background_grid();

    // Title + totals
    shadow_text("Pokedex", DEX_PANEL_X, 10, 22, WHITE);
    shadow_text(TextFormat("Seen %u   Owned %u / %u", seen_total, owned_total, max),
                DEX_PANEL_X + 130, 14, 18, WHITE);

    // Search box
    Rectangle search_rec = (Rectangle){520, 10, 190, 26};
    DrawRectangleRec(search_rec, search_active ? WHITE : (Color){0, 0, 0, 120});
    DrawRectangleLinesEx(search_rec, 2, BLACK);
    if (query_len > 0)
    {
        DrawText(query, (int)search_rec.x + 6, (int)search_rec.y + 5, 18, search_active ? BLACK : WHITE);
    }
    else
    {
        DrawText("Search name/number", (int)search_rec.x + 6, (int)search_rec.y + 7, 14,
                 search_active ? (Color){120, 120, 120, 255} : (Color){220, 220, 220, 200});
    }
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        search_active = CheckCollisionPointRec(GetMousePosition(), search_rec);
    }

    // List panel
    Rectangle panel = (Rectangle){DEX_PANEL_X, DEX_PANEL_Y, DEX_PANEL_W, DEX_PANEL_H};
    DrawRectangleRounded(panel, 0.03f, 6, RAYWHITE);
    DrawRectangleRoundedLines(panel, 0.03f, 6, 2.0f, BLACK);

    const Color unseen_grey = (Color){150, 150, 150, 255};
    for (int r = 0; r < visible_rows; r++)
    {
        for (int c = 0; c < DEX_COLS; c++)
        {
            int idx = (scroll_row + r) * DEX_COLS + c;
            if (idx >= num_filtered)
                break;
            uint16_t dex = filtered[idx];
            bool seen, owned;
            pokedex_get_entry(pkmn_save, dex, &seen, &owned);

            int x = DEX_PANEL_X + 14 + c * DEX_COL_W;
            int y = DEX_PANEL_Y + 8 + r * DEX_ROW_H;
            Color color = seen ? BLACK : unseen_grey;
            DrawText(TextFormat("#%03u", dex), x, y, 18, color);
            if (owned)
            {
                draw_pokeball_icon(x + 62, y + 8, 7);
            }
            DrawText(gen4_species_name(dex), x + 78, y, 18, color);
        }
    }

    // Scrollbar
    if (max_scroll > 0)
    {
        float track_h = DEX_PANEL_H - 8;
        float thumb_h = track_h * visible_rows / total_rows;
        float thumb_y = DEX_PANEL_Y + 4 + (track_h - thumb_h) * scroll_row / max_scroll;
        DrawRectangle(DEX_PANEL_X + DEX_PANEL_W - 8, DEX_PANEL_Y + 4, 4, (int)track_h, (Color){0, 0, 0, 40});
        DrawRectangle(DEX_PANEL_X + DEX_PANEL_W - 8, (int)thumb_y, 4, (int)thumb_h, (Color){0, 0, 0, 140});
    }

    // Back button
    const Rectangle back_button_rec = (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    bool back_hover = CheckCollisionPointRec(GetMousePosition(), back_button_rec);
    DrawText("< Back", (int)back_button_rec.x + 15, (int)back_button_rec.y + 10, 20, back_hover ? LIGHTGRAY : BLACK);

    end_virtual_frame();

    if ((IsMouseButtonReleased(MOUSE_LEFT_BUTTON) && back_hover) || IsKeyPressed(KEY_ESCAPE))
    {
        needs_init = true;
        trainerSelection->pkmn_party_index = -1;
        *current_screen = SCREEN_POKEDEX_FILE_SELECT;
    }
}

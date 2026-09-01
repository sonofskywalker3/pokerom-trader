#include <ctype.h>
#include <stdio.h>
#include "raylibhelper.h"
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "gen4_stats.h" /* gen4_species_name: National Dex -> name, all gens */
#include "pkmn_evolutions.h"

// Bill's PC — browse the party and all PC boxes, deposit / withdraw / swap
// Pokémon, sort a box, and search by name / type / level.

#define BILLS_PARTY_X 40
#define BILLS_PARTY_Y 64
#define BILLS_PARTY_ROW_H 24
#define BILLS_PARTY_W 230
#define BILLS_PARTY_FONT 18

#define BILLS_BOX_X 300
#define BILLS_BOX_Y 64
#define BILLS_BOX_ROW_H 17
#define BILLS_BOX_W 220
#define BILLS_BOX_FONT 14

#define BILLS_INFO_X 545
#define BILLS_INFO_Y 64
#define BILLS_INFO_W 220
#define BILLS_INFO_H 320

// Tooltip for the evolve-method badges: filled while drawing slots, rendered
// on top of everything at the end of the frame.
static struct
{
    bool active;
    char lines[PKMN_MAX_NEEDED_EVOS][80];
    int line_count;
    int width;   // widest line, px
    int font;
    Vector2 anchor; // top-center of the hovered icon
    int below_y;    // y to fall back to when there's no room above
} evo_tip;

// Fill the tooltip with this method's missing evolutions.
static void evo_tip_fill(int method, const struct pkmn_needed_evo *needed, uint8_t n,
                         Vector2 anchor, int below_y)
{
    evo_tip.active = true;
    evo_tip.anchor = anchor;
    evo_tip.below_y = below_y;
    evo_tip.font = 14;
    evo_tip.width = 0;
    evo_tip.line_count = 0;
    for (uint8_t i = 0; i < n && evo_tip.line_count < PKMN_MAX_NEEDED_EVOS; i++)
    {
        if (needed[i].method != method)
        {
            continue;
        }
        char *line = evo_tip.lines[evo_tip.line_count];
        const char *name = gen4_species_name(needed[i].dex);
        switch (method)
        {
        case PKMN_EVO_LEVEL:
            snprintf(line, sizeof(evo_tip.lines[0]), "%s  Lv%u", name, needed[i].level);
            break;
        case PKMN_EVO_STONE:
            snprintf(line, sizeof(evo_tip.lines[0]), "%s  %s", name, needed[i].detail);
            break;
        case PKMN_EVO_TRADE:
            if (needed[i].detail != NULL)
            {
                snprintf(line, sizeof(evo_tip.lines[0]), "%s  Trade holding %s", name, needed[i].detail);
            }
            else
            {
                snprintf(line, sizeof(evo_tip.lines[0]), "%s  Trade", name);
            }
            break;
        case PKMN_EVO_BREED:
            if (needed[i].detail != NULL)
            {
                snprintf(line, sizeof(evo_tip.lines[0]), "%s  Breed holding %s", name, needed[i].detail);
            }
            else
            {
                snprintf(line, sizeof(evo_tip.lines[0]), "%s  Breed", name);
            }
            break;
        default:
            snprintf(line, sizeof(evo_tip.lines[0]), "%s  %s", name, needed[i].detail ? needed[i].detail : "Special");
            break;
        }
        int w = MeasureText(line, evo_tip.font);
        if (w > evo_tip.width)
        {
            evo_tip.width = w;
        }
        evo_tip.line_count++;
    }
}

// Render the pending tooltip near its icon; called last so it draws on top.
static void evo_tip_draw(void)
{
    if (!evo_tip.active || evo_tip.line_count == 0)
    {
        return;
    }
    int line_h = evo_tip.font + 4;
    int h = evo_tip.line_count * line_h + 8;
    int w = evo_tip.width + 12;
    int x = (int)evo_tip.anchor.x - w / 2;
    if (x < 4)
    {
        x = 4;
    }
    if (x + w > SCREEN_WIDTH - 4)
    {
        x = SCREEN_WIDTH - 4 - w;
    }
    int y = (int)evo_tip.anchor.y - h - 4;
    if (y < 4)
    {
        y = evo_tip.below_y + 4;
    }
    DrawRectangle(x, y, w, h, (Color){20, 20, 30, 235});
    DrawRectangleLines(x, y, w, h, (Color){255, 205, 90, 255});
    for (int i = 0; i < evo_tip.line_count; i++)
    {
        DrawText(evo_tip.lines[i], x + 6, y + 5 + i * line_h, evo_tip.font, RAYWHITE);
    }
}

// Convert a string to lowercase into out (bounded).
static void bills_to_lower(const char *in, char *out, int out_size)
{
    int i = 0;
    for (; in[i] != '\0' && i < out_size - 1; i++)
    {
        out[i] = (char)tolower((unsigned char)in[i]);
    }
    out[i] = '\0';
}

static bool bills_ci_contains(const char *haystack, const char *needle)
{
    if (needle[0] == '\0')
    {
        return true;
    }
    char h[64];
    char n[64];
    bills_to_lower(haystack, h, sizeof(h));
    bills_to_lower(needle, n, sizeof(n));
    return strstr(h, n) != NULL;
}

// Gen-agnostic species display name (National Dex based; "?" when unknown).
static const char *bills_species_name(const struct bills_pc_entry_view *view)
{
    return gen4_species_name(view->dex);
}

// True when the mon's nickname differs from its species name.
static bool bills_is_nicknamed(const struct bills_pc_entry_view *view)
{
    char nick[64];
    char species[64];
    bills_to_lower(view->nickname, nick, sizeof(nick));
    bills_to_lower(bills_species_name(view), species, sizeof(species));
    return strcmp(nick, species) != 0;
}

// Does a stored Pokémon match the current search query (name / type / level)?
static bool bills_entry_matches(const struct bills_pc_entry_view *view, const char *query)
{
    if (query[0] == '\0' || !view->occupied)
    {
        return query[0] == '\0';
    }
    if (bills_ci_contains(view->nickname, query))
    {
        return true;
    }
    if (bills_ci_contains(bills_species_name(view), query))
    {
        return true;
    }
    if (view->type1 != BILLS_PC_TYPE_UNKNOWN && bills_ci_contains(pkmn_type_name(view->type1), query))
    {
        return true;
    }
    if (view->type2 != BILLS_PC_TYPE_UNKNOWN && bills_ci_contains(pkmn_type_name(view->type2), query))
    {
        return true;
    }
    char level_str[8];
    snprintf(level_str, sizeof(level_str), "%u", view->level);
    if (bills_ci_contains(level_str, query))
    {
        return true;
    }
    return false;
}

static bool bills_slot_eq(struct bills_pc_slot a, struct bills_pc_slot b)
{
    if (a.location != b.location || a.index != b.index)
    {
        return false;
    }
    if (a.location == BILLS_PC_LOC_BOX && a.box_num != b.box_num)
    {
        return false;
    }
    return true;
}

static const char *bills_sort_label(enum bills_pc_sort_mode mode)
{
    switch (mode)
    {
    case BILLS_PC_SORT_DEX:   return "Sort: Dex";
    case BILLS_PC_SORT_NAME:  return "Sort: Name";
    case BILLS_PC_SORT_LEVEL: return "Sort: Level";
    case BILLS_PC_SORT_TYPE:  return "Sort: Type";
    default:                  return "Sort: --";
    }
}

// Draw a single storage row and report interaction.
// Returns true if this row was clicked this frame.
static bool bills_draw_slot(PokemonSave *pkmn_save, struct bills_pc_slot slot, Rectangle rect,
                            int font_size, bool is_pending, const char *query,
                            struct bills_pc_entry_view *out_hovered, bool *out_has_hover)
{
    struct bills_pc_entry_view view;
    bills_pc_get_view(pkmn_save, slot.location, slot.box_num, slot.index, &view);

    bool hovered = CheckCollisionPointRec(GetMousePosition(), rect);
    bool searching = query[0] != '\0';
    bool matches = bills_entry_matches(&view, query);

    if (is_pending)
    {
        DrawRectangleRec(rect, COLOR_PKMN_YELLOW);
    }
    else if (hovered)
    {
        DrawRectangleRec(rect, (Color){255, 255, 255, 60});
    }
    else if (searching && view.occupied && matches)
    {
        DrawRectangleRec(rect, (Color){62, 185, 94, 90});
    }

    Color text_color = WHITE;
    if (is_pending)
    {
        text_color = BLACK;
    }
    else if (searching && view.occupied && !matches)
    {
        text_color = (Color){200, 200, 200, 120};
    }

    if (view.occupied)
    {
        DrawText(view.nickname, (int)rect.x + 6, (int)rect.y + 2, font_size, text_color);
        int right_x = (int)(rect.x + rect.width) - 6;
        if (view.level > 0) // Gen 3 boxed mons store no level; skip the label
        {
            const char *lv = TextFormat("Lv%u", view.level);
            right_x -= MeasureText(lv, font_size);
            DrawText(lv, right_x, (int)rect.y + 2, font_size, text_color);
            right_x -= 8;
        }
        if (view.dex > 0)
        {
            // Pokedex to-do badges: one icon per method that still leads to
            // an unregistered entry (evolve, or breed a pre-evolution in
            // Gen 2+); hover an icon for the details.
            struct pkmn_needed_evo needed[PKMN_MAX_NEEDED_EVOS];
            uint8_t needed_n = pkmn_needed_dex_tasks(pkmn_save, view.dex, needed);
            if (needed_n > 0)
            {
                bool has_method[PKMN_EVO_METHOD_COUNT] = {false};
                for (uint8_t i = 0; i < needed_n; i++)
                {
                    has_method[needed[i].method] = true;
                }
                int icon_h = font_size - 5;
                int icon_y = (int)rect.y + 2 + (font_size - icon_h) / 2;
                for (int m = PKMN_EVO_METHOD_COUNT - 1; m >= 0; m--)
                {
                    if (!has_method[m])
                    {
                        continue;
                    }
                    right_x -= icon_h;
                    draw_evo_method_icon(m, right_x, icon_y, icon_h, text_color.a);
                    Rectangle icon_rec = {(float)(right_x - 2), rect.y, (float)(icon_h + 4), rect.height};
                    if (CheckCollisionPointRec(GetMousePosition(), icon_rec))
                    {
                        evo_tip_fill(m, needed, needed_n,
                                     (Vector2){right_x + icon_h / 2.0f, rect.y},
                                     (int)(rect.y + rect.height));
                    }
                    right_x -= 5;
                }
                right_x -= 3;
            }
        }
        if (bills_is_nicknamed(&view))
        {
            // Nicknamed mon: fit the species name in (dimmed) so the row still
            // says what the Pokémon actually is; skipped if it would collide.
            const char *species = bills_species_name(&view);
            int species_font = font_size >= 16 ? font_size - 4 : 10;
            int species_w = MeasureText(species, species_font);
            int nick_end = (int)rect.x + 6 + MeasureText(view.nickname, font_size);
            if (right_x - species_w > nick_end + 8)
            {
                Color species_color = text_color;
                if (species_color.a > 160)
                {
                    species_color.a = 160;
                }
                DrawText(species, right_x - species_w, (int)rect.y + 2 + (font_size - species_font), species_font, species_color);
            }
        }
        if (hovered && out_hovered)
        {
            *out_hovered = view;
            *out_has_hover = true;
        }
    }
    else
    {
        DrawText("----", (int)rect.x + 6, (int)rect.y + 2, font_size, (Color){255, 255, 255, 90});
    }

    return hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

static void bills_reset_state(PokemonSave *pkmn_save, int *box_view, bool *has_pending,
                              enum bills_pc_sort_mode *sort_mode, char *query, int *query_len,
                              bool *search_active, bool *is_dirty)
{
    bills_pc_normalize_current_box(pkmn_save);
    pokedex_reconcile(pkmn_save); // heal dex gaps for mons actually present
    *box_view = bills_pc_current_box_num(pkmn_save);
    *has_pending = false;
    *sort_mode = BILLS_PC_SORT_NONE;
    query[0] = '\0';
    *query_len = 0;
    *search_active = false;
    *is_dirty = false;
}

void draw_bills_pc(PokemonSave *pkmn_save, char *save_path, struct trainer_info *trainer, struct TrainerSelection *trainerSelection, GameScreen *current_screen)
{
    static bool needs_init = true;
    static int box_view = 0;
    static struct bills_pc_slot pending;
    static bool has_pending = false;
    static enum bills_pc_sort_mode sort_mode = BILLS_PC_SORT_NONE;
    static bool global_sort = false;
    static char query[32] = "\0";
    static int query_len = 0;
    static bool search_active = false;
    static bool is_dirty = false;
    static bool show_saving_icon = false;
    static bool show_saved_toast = false;
    static bool show_error_toast = false;
    static bool show_dex_toast = false;
    static enum E_TOAST_TYPE dex_toast_type = TOAST_SUCCESS;
    static char dex_toast[64] = "\0";

    if (needs_init)
    {
        bills_reset_state(pkmn_save, &box_view, &has_pending, &sort_mode, query, &query_len, &search_active, &is_dirty);
        needs_init = false;
    }

    int num_boxes = bills_pc_num_boxes(pkmn_save);
    int box_capacity = bills_pc_box_capacity(pkmn_save);
    int party_capacity = bills_pc_party_capacity(pkmn_save);
    if (box_view < 0 || box_view >= num_boxes)
    {
        box_view = 0;
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
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && query_len > 0)
        {
            query[--query_len] = '\0';
        }
    }

    begin_virtual_frame();
    ClearBackground(RED);
    draw_background_grid();
    evo_tip.active = false; // re-armed by whichever badge the mouse is over

    // Title
    shadow_text("Boxes", BILLS_PARTY_X, 10, 22, WHITE);

    // --- Search box ---
    Rectangle search_rec = (Rectangle){250, 10, 190, 26};
    DrawRectangleRec(search_rec, search_active ? WHITE : (Color){0, 0, 0, 120});
    DrawRectangleLinesEx(search_rec, 2, BLACK);
    Color query_color = search_active ? BLACK : WHITE;
    if (query_len > 0)
    {
        DrawText(query, (int)search_rec.x + 6, (int)search_rec.y + 5, 18, query_color);
    }
    else
    {
        DrawText("Search name/type/level", (int)search_rec.x + 6, (int)search_rec.y + 7, 14,
                 search_active ? (Color){120, 120, 120, 255} : (Color){220, 220, 220, 200});
    }

    // --- Sort button ---
    Rectangle sort_rec = (Rectangle){450, 10, 150, 26};
    bool sort_hover = CheckCollisionPointRec(GetMousePosition(), sort_rec);
    DrawRectangleRec(sort_rec, sort_hover ? COLOR_PKMN_DARKYELLOW : (Color){0, 0, 0, 120});
    DrawRectangleLinesEx(sort_rec, 2, BLACK);
    DrawText(bills_sort_label(sort_mode), (int)sort_rec.x + 8, (int)sort_rec.y + 5, 16, WHITE);

    // --- Global sort checkbox ---
    Rectangle global_rec = (Rectangle){612, 13, 20, 20};
    bool global_hover = CheckCollisionPointRec(GetMousePosition(), global_rec);
    DrawRectangleRec(global_rec, global_hover ? COLOR_PKMN_DARKYELLOW : (Color){0, 0, 0, 120});
    DrawRectangleLinesEx(global_rec, 2, BLACK);
    if (global_sort)
    {
        // check mark
        DrawRectangle((int)global_rec.x + 4, (int)global_rec.y + 4, 12, 12, COLOR_PKMN_YELLOW);
    }
    DrawText("Global", (int)global_rec.x + 26, (int)global_rec.y + 2, 16, WHITE);

    // --- Column headers ---
    int party_count = bills_pc_container_count(pkmn_save, BILLS_PC_LOC_PARTY, 0);
    int viewed_box_count = bills_pc_container_count(pkmn_save, BILLS_PC_LOC_BOX, box_view);
    shadow_text(TextFormat("PARTY  %d/%d", party_count, party_capacity), BILLS_PARTY_X, BILLS_PARTY_Y - 22, 18, WHITE);

    // Box header with < > arrows
    Rectangle box_prev = (Rectangle){BILLS_BOX_X, BILLS_BOX_Y - 26, 24, 22};
    Rectangle box_next = (Rectangle){BILLS_BOX_X + BILLS_BOX_W - 24, BILLS_BOX_Y - 26, 24, 22};
    bool prev_hover = CheckCollisionPointRec(GetMousePosition(), box_prev);
    bool next_hover = CheckCollisionPointRec(GetMousePosition(), box_next);
    DrawText("<", (int)box_prev.x + 6, (int)box_prev.y, 22, prev_hover ? COLOR_PKMN_YELLOW : WHITE);
    DrawText(">", (int)box_next.x + 6, (int)box_next.y, 22, next_hover ? COLOR_PKMN_YELLOW : WHITE);
    shadow_text(TextFormat("BOX %d  %d/%d", box_view + 1, viewed_box_count, box_capacity),
                BILLS_BOX_X + 34, BILLS_BOX_Y - 24, 18, WHITE);

    // Box grid layout: single column for Gen 1/2 (20 slots), two columns for
    // Gen 3 (30 slots) so everything fits the fixed-height panel.
    int box_cols = box_capacity > 20 ? 2 : 1;
    int box_rows = (box_capacity + box_cols - 1) / box_cols;
    int box_col_w = (BILLS_BOX_W - 8) / box_cols;

    // Panels
    DrawRectangle(BILLS_PARTY_X - 4, BILLS_PARTY_Y - 2, BILLS_PARTY_W, party_capacity * BILLS_PARTY_ROW_H + 6, (Color){0, 0, 0, 90});
    DrawRectangle(BILLS_BOX_X - 4, BILLS_BOX_Y - 2, BILLS_BOX_W, box_rows * BILLS_BOX_ROW_H + 6, (Color){0, 0, 0, 90});

    bool clicked = false;
    struct bills_pc_slot clicked_slot = {0};
    struct bills_pc_entry_view hovered_view = {0};
    bool has_hover = false;

    // --- Party rows ---
    for (int i = 0; i < party_capacity; i++)
    {
        struct bills_pc_slot slot = {BILLS_PC_LOC_PARTY, 0, i};
        Rectangle rect = (Rectangle){BILLS_PARTY_X, BILLS_PARTY_Y + i * BILLS_PARTY_ROW_H, BILLS_PARTY_W - 8, BILLS_PARTY_ROW_H - 2};
        bool is_pending = has_pending && bills_slot_eq(pending, slot);
        if (bills_draw_slot(pkmn_save, slot, rect, BILLS_PARTY_FONT, is_pending, query, &hovered_view, &has_hover))
        {
            clicked = true;
            clicked_slot = slot;
        }
    }

    // --- Box slots (single or two columns) ---
    for (int i = 0; i < box_capacity; i++)
    {
        struct bills_pc_slot slot = {BILLS_PC_LOC_BOX, box_view, i};
        int col = i / box_rows;
        int row = i % box_rows;
        Rectangle rect = (Rectangle){BILLS_BOX_X + col * box_col_w, BILLS_BOX_Y + row * BILLS_BOX_ROW_H, box_col_w - 4, BILLS_BOX_ROW_H - 1};
        bool is_pending = has_pending && bills_slot_eq(pending, slot);
        if (bills_draw_slot(pkmn_save, slot, rect, BILLS_BOX_FONT, is_pending, query, &hovered_view, &has_hover))
        {
            clicked = true;
            clicked_slot = slot;
        }
    }

    // --- Info panel (pending selection or hovered mon) ---
    DrawRectangle(BILLS_INFO_X - 4, BILLS_INFO_Y - 2, BILLS_INFO_W, BILLS_INFO_H, (Color){0, 0, 0, 110});
    struct bills_pc_entry_view info_view;
    bool have_info = false;
    if (has_pending)
    {
        bills_pc_get_view(pkmn_save, pending.location, pending.box_num, pending.index, &info_view);
        have_info = info_view.occupied;
        shadow_text("SELECTED", BILLS_INFO_X + 6, BILLS_INFO_Y + 4, 16, COLOR_PKMN_YELLOW);
    }
    else if (has_hover)
    {
        info_view = hovered_view;
        have_info = true;
        shadow_text("INFO", BILLS_INFO_X + 6, BILLS_INFO_Y + 4, 16, WHITE);
    }
    if (have_info)
    {
        shadow_text(info_view.nickname, BILLS_INFO_X + 6, BILLS_INFO_Y + 30, 20, WHITE);
        // Species + dex line: tells what a nicknamed mon actually is.
        if (info_view.dex > 0)
        {
            shadow_text(TextFormat("#%03u %s", info_view.dex, bills_species_name(&info_view)), BILLS_INFO_X + 6, BILLS_INFO_Y + 54, 14, (Color){220, 220, 220, 255});
        }
        shadow_text(TextFormat("Level %u", info_view.level), BILLS_INFO_X + 6, BILLS_INFO_Y + 76, 18, WHITE);
        const char *t1 = info_view.type1 == BILLS_PC_TYPE_UNKNOWN ? "?" : pkmn_type_name(info_view.type1);
        if (info_view.type2 != BILLS_PC_TYPE_UNKNOWN && info_view.type2 != info_view.type1)
        {
            shadow_text(TextFormat("Type: %s/%s", t1, pkmn_type_name(info_view.type2)), BILLS_INFO_X + 6, BILLS_INFO_Y + 102, 16, WHITE);
        }
        else
        {
            shadow_text(TextFormat("Type: %s", t1), BILLS_INFO_X + 6, BILLS_INFO_Y + 102, 16, WHITE);
        }
        // Dex entries this Pokemon can still register (evolve / breed), with the method.
        if (info_view.dex > 0)
        {
            struct pkmn_needed_evo needed[PKMN_MAX_NEEDED_EVOS];
            uint8_t needed_n = pkmn_needed_dex_tasks(pkmn_save, info_view.dex, needed);
            if (needed_n > 0)
            {
                shadow_text("POKEDEX TO-DO:", BILLS_INFO_X + 6, BILLS_INFO_Y + 128, 14, (Color){255, 205, 90, 255});
                int shown = needed_n <= 3 ? needed_n : 2;
                for (int i = 0; i < shown; i++)
                {
                    const char *how;
                    switch (needed[i].method)
                    {
                    case PKMN_EVO_LEVEL: how = TextFormat("Lv%u", needed[i].level); break;
                    case PKMN_EVO_STONE: how = needed[i].detail; break;
                    case PKMN_EVO_TRADE: how = "Trade"; break;
                    case PKMN_EVO_BREED: how = needed[i].detail ? TextFormat("Breed, %s", needed[i].detail) : "Breed"; break;
                    default: how = "Special"; break;
                    }
                    draw_evo_method_icon(needed[i].method, BILLS_INFO_X + 6, BILLS_INFO_Y + 147 + i * 17, 10, 255);
                    shadow_text(TextFormat("%s (%s)", gen4_species_name(needed[i].dex), how),
                                BILLS_INFO_X + 22, BILLS_INFO_Y + 146 + i * 17, 12, WHITE);
                }
                if (needed_n > shown)
                {
                    shadow_text(TextFormat("+%d more (hover row icons)", needed_n - shown),
                                BILLS_INFO_X + 22, BILLS_INFO_Y + 146 + shown * 17, 12, (Color){200, 200, 200, 255});
                }
            }
        }
    }
    // Instructions
    if (has_pending)
    {
        shadow_text("Click a slot to", BILLS_INFO_X + 6, BILLS_INFO_Y + 200, 14, WHITE);
        shadow_text("move/swap here,", BILLS_INFO_X + 6, BILLS_INFO_Y + 220, 14, WHITE);
        shadow_text("or click it again", BILLS_INFO_X + 6, BILLS_INFO_Y + 240, 14, WHITE);
        shadow_text("to cancel.", BILLS_INFO_X + 6, BILLS_INFO_Y + 260, 14, WHITE);
    }
    else
    {
        shadow_text("Click a Pokemon", BILLS_INFO_X + 6, BILLS_INFO_Y + 220, 14, WHITE);
        shadow_text("to pick it up.", BILLS_INFO_X + 6, BILLS_INFO_Y + 240, 14, WHITE);
    }

    // --- Bottom bar ---
    const Rectangle bottom_bar_rec = (Rectangle){0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 100};
    DrawRectangleRec(bottom_bar_rec, WHITE);
    DrawLineEx((Vector2){bottom_bar_rec.x, bottom_bar_rec.y}, (Vector2){bottom_bar_rec.width, bottom_bar_rec.y}, 15, BLACK);

    const Rectangle back_button_rec = (Rectangle){BACK_BUTTON_X - 15, BACK_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    bool back_hover = CheckCollisionPointRec(GetMousePosition(), back_button_rec);
    DrawText("< Back", (int)back_button_rec.x + 15, (int)back_button_rec.y + 10, 20, back_hover ? LIGHTGRAY : BLACK);

    // Pokedex to-do box: pulls every mon with an evolve/breed job into the last box.
    const int dex_box = num_boxes - 1;
    const Rectangle dex_button_rec = (Rectangle){SCREEN_WIDTH / 2 - 100, NEXT_BUTTON_Y + 8, 200, BUTTON_HEIGHT};
    bool dex_hover = CheckCollisionPointRec(GetMousePosition(), dex_button_rec);
    {
        const char *dex_label = TextFormat("Dex To-Do -> Box %d", dex_box + 1);
        int label_w = MeasureText(dex_label, 18);
        int icon_h = 14;
        int start_x = (int)dex_button_rec.x + ((int)dex_button_rec.width - (label_w + icon_h + 6)) / 2;
        draw_evo_method_icon(PKMN_EVO_BREED, start_x, (int)dex_button_rec.y + 12, icon_h, 255);
        DrawText(dex_label, start_x + icon_h + 6, (int)dex_button_rec.y + 11, 18, dex_hover ? LIGHTGRAY : BLACK);
    }

    const Rectangle save_button_rec = (Rectangle){NEXT_BUTTON_X - 15, NEXT_BUTTON_Y + 8, BUTTON_WIDTH, BUTTON_HEIGHT};
    bool save_hover = CheckCollisionPointRec(GetMousePosition(), save_button_rec);
    DrawText(is_dirty ? "Save *" : "Save", (int)save_button_rec.x + 15, (int)save_button_rec.y + 10, 20,
             is_dirty ? (save_hover ? LIGHTGRAY : BLACK) : LIGHTGRAY);

    // ---------------- Input handling ----------------
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        // Focus / unfocus search box
        search_active = CheckCollisionPointRec(GetMousePosition(), search_rec);

        if (global_hover)
        {
            global_sort = !global_sort;
        }
        else if (sort_hover)
        {
            sort_mode = (enum bills_pc_sort_mode)((sort_mode + 1) % BILLS_PC_SORT_COUNT);
            if (sort_mode != BILLS_PC_SORT_NONE)
            {
                if (global_sort)
                {
                    bills_pc_sort_all_boxes(pkmn_save, sort_mode);
                }
                else
                {
                    bills_pc_sort_box(pkmn_save, box_view, sort_mode);
                }
                is_dirty = true;
                has_pending = false;
            }
        }
        else if (prev_hover)
        {
            box_view = (box_view - 1 + num_boxes) % num_boxes;
        }
        else if (next_hover)
        {
            box_view = (box_view + 1) % num_boxes;
        }
    }

    if (clicked)
    {
        struct bills_pc_entry_view cv;
        bills_pc_get_view(pkmn_save, clicked_slot.location, clicked_slot.box_num, clicked_slot.index, &cv);
        if (!has_pending)
        {
            if (cv.occupied)
            {
                pending = clicked_slot;
                has_pending = true;
            }
        }
        else if (bills_slot_eq(pending, clicked_slot))
        {
            has_pending = false; // cancel
        }
        else
        {
            pksavhelper_error result = bills_pc_move_pkmn(pkmn_save, pending, clicked_slot);
            if (result == error_none)
            {
                is_dirty = true;
                create_trainer(pkmn_save, trainer); // keep party display fresh
            }
            else
            {
                show_error_toast = true;
            }
            has_pending = false;
        }
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        if (back_hover)
        {
            has_pending = false;
            needs_init = true;
            trainerSelection->pkmn_party_index = -1;
            // Unsaved box moves mutate the file-select screen's cached save
            // in place; drop the cache so it reloads clean from disk.
            if (is_dirty)
                free_evolve_saves();
            *current_screen = SCREEN_BILLS_PC_FILE_SELECT;
        }
        else if (dex_hover)
        {
            int wanted = 0, placed = 0;
            int moved = bills_pc_fill_dex_box(pkmn_save, dex_box, &wanted, &placed);
            has_pending = false;
            box_view = dex_box;
            if (moved > 0)
            {
                is_dirty = true;
            }
            if (wanted == 0)
            {
                snprintf(dex_toast, sizeof(dex_toast), "Nothing left to evolve%s",
                         pkmn_save->save_generation_type == SAVE_GENERATION_1 ? "" : " or breed");
                dex_toast_type = TOAST_INFO;
            }
            else if (placed < wanted)
            {
                snprintf(dex_toast, sizeof(dex_toast), "Box %d full: %d of %d to-dos", dex_box + 1, placed, wanted);
                dex_toast_type = TOAST_ERROR;
            }
            else
            {
                snprintf(dex_toast, sizeof(dex_toast), "Box %d: %d Pokedex to-do%s", dex_box + 1, placed, placed == 1 ? "" : "s");
                dex_toast_type = TOAST_SUCCESS;
            }
            show_dex_toast = true;
        }
        else if (save_hover && is_dirty)
        {
            create_backup_save(pkmn_save, save_path);
            bills_pc_flush_current_box(pkmn_save);
            save_savefile_to_path(pkmn_save, save_path);
            create_trainer(pkmn_save, trainer);
            is_dirty = false;
            show_saving_icon = true;
            show_saved_toast = true;
        }
    }

    if (show_saving_icon)
    {
        show_saving_icon = draw_save_icon(SCREEN_WIDTH - 50, 44, show_saving_icon);
    }
    if (show_saved_toast)
    {
        show_saved_toast = !draw_toast_message("Saved!", TOAST_SHORT, TOAST_SUCCESS);
    }
    if (show_error_toast)
    {
        show_error_toast = !draw_toast_message("Can't move there", TOAST_SHORT, TOAST_ERROR);
    }
    if (show_dex_toast)
    {
        show_dex_toast = !draw_toast_message(dex_toast, TOAST_MEDIUM, dex_toast_type);
    }

    evo_tip_draw();

    end_virtual_frame();

    if (IsKeyPressed(KEY_ESCAPE))
    {
        has_pending = false;
        needs_init = true;
        trainerSelection->pkmn_party_index = -1;
        if (is_dirty)
            free_evolve_saves();
        *current_screen = SCREEN_BILLS_PC_FILE_SELECT;
    }
}

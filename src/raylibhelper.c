#include "raylibhelper.h"
#include "filehelper.h"
#include "pksavhelper.h"
#include "pksavfilehelper.h"
#include "textures.h"
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

// Render target the current frame draws into (SCREEN_WIDTH x g_virtual_height),
// then scaled to the actual window in end_virtual_frame().
static RenderTexture2D g_render_target;
static int g_target_height = SCREEN_HEIGHT_BASE;

// Current virtual canvas height. Fixed screens use SCREEN_HEIGHT_BASE; list
// screens grow it so a taller window shows more rows. Reported via SCREEN_HEIGHT.
static int g_virtual_height = SCREEN_HEIGHT_BASE;
// Whether the current screen wants a height that grows with the window.
static bool g_dynamic_viewport = false;

int virtual_screen_height(void)
{
    return g_virtual_height;
}

// Set by the main loop before dispatching a screen.
void set_dynamic_viewport(bool dynamic)
{
    g_dynamic_viewport = dynamic;
}

// Compute the draw scale + top-left offset for the current window/mode, and
// update g_virtual_height. Returns the scale.
static float compute_viewport(float *out_offset_x, float *out_offset_y)
{
    float win_w = (float)GetScreenWidth();
    float win_h = (float)GetScreenHeight();
    float scale;
    float offset_x;
    float offset_y;

    if (g_dynamic_viewport)
    {
        // Fit width; the extra vertical space becomes more canvas (more rows).
        float scale_w = win_w / SCREEN_WIDTH;
        int vh = (int)(win_h / scale_w + 0.5f);
        if (vh >= SCREEN_HEIGHT_BASE)
        {
            // Tall/normal window: fill it, no letterboxing.
            if (vh > 4000) vh = 4000;
            g_virtual_height = vh;
            scale = scale_w;
            offset_x = 0.0f;
            offset_y = 0.0f;
        }
        else
        {
            // Wide, short window: fit height instead, letterbox the sides.
            g_virtual_height = SCREEN_HEIGHT_BASE;
            scale = win_h / SCREEN_HEIGHT_BASE;
            offset_x = (win_w - SCREEN_WIDTH * scale) * 0.5f;
            offset_y = 0.0f;
        }
    }
    else
    {
        // Fixed 800x480 canvas, uniformly scaled and letterboxed.
        g_virtual_height = SCREEN_HEIGHT_BASE;
        float sx = win_w / SCREEN_WIDTH;
        float sy = win_h / SCREEN_HEIGHT_BASE;
        scale = sx < sy ? sx : sy;
        offset_x = (win_w - SCREEN_WIDTH * scale) * 0.5f;
        offset_y = (win_h - SCREEN_HEIGHT_BASE * scale) * 0.5f;
    }

    if (out_offset_x) *out_offset_x = offset_x;
    if (out_offset_y) *out_offset_y = offset_y;
    return scale;
}

void begin_virtual_frame(void)
{
    float offset_x, offset_y;
    float scale = compute_viewport(&offset_x, &offset_y);

    // Grow/shrink the render target if the virtual height changed.
    if (g_virtual_height != g_target_height)
    {
        UnloadRenderTexture(g_render_target);
        g_render_target = LoadRenderTexture(SCREEN_WIDTH, g_virtual_height);
        SetTextureFilter(g_render_target.texture, TEXTURE_FILTER_BILINEAR);
        g_target_height = g_virtual_height;
    }

    // Map real mouse coords back into virtual space so hit-testing is unchanged.
    SetMouseOffset((int)(-offset_x), (int)(-offset_y));
    SetMouseScale(1.0f / scale, 1.0f / scale);

    BeginTextureMode(g_render_target);
}

void end_virtual_frame(void)
{
    EndTextureMode();

    float offset_x, offset_y;
    float scale = compute_viewport(&offset_x, &offset_y);

    BeginDrawing();
    ClearBackground(BLACK);
    // Source is flipped vertically because render textures are bottom-up.
    Rectangle src = {0.0f, 0.0f, (float)SCREEN_WIDTH, -(float)g_virtual_height};
    Rectangle dst = {offset_x, offset_y, SCREEN_WIDTH * scale, g_virtual_height * scale};
    DrawTexturePro(g_render_target.texture, src, dst, (Vector2){0.0f, 0.0f}, 0.0f, WHITE);
    EndDrawing();
}

void draw_background_grid(void)
{
    int line_count_v = SCREEN_HEIGHT / 10;
    int line_count_h = SCREEN_WIDTH / 10;

    // draw line grid color_pkmn_red
    for (int i = 0; i < line_count_v; i++)
    {
        DrawRectangle(0, i * 10, SCREEN_WIDTH, 1, COLOR_PKMN_RED);
    }
    for (int i = 0; i < line_count_h; i++)
    {
        DrawRectangle(i * 10, 0, 1, SCREEN_HEIGHT, COLOR_PKMN_RED);
    }
}

// Draws a button with the pokemon nickname
void draw_pkmn_button(Rectangle rect, int index, char *pokemon_nickname, bool selected)
{
    shadow_text(pokemon_nickname, rect.x + 10, rect.y + 6, 20, selected ? LIGHTGRAY : WHITE);
}

// Concantenate the trainer's name and id into a string for Raylib to draw
void create_trainer_name_str(const struct trainer_info *trainer, char *trainer_name)
{
    strcpy(trainer_name, "NAME/");
    strcat(trainer_name, trainer->trainer_name);
}

// Concantenate the trainer's id into a string for Raylib to draw
void create_trainer_id_str(const struct trainer_info *trainer, char *trainer_id)
{
    char id_str[6];
    strcpy(trainer_id, "IDNo ");
    snprintf(id_str, sizeof(id_str), "%05u", trainer->trainer_id);
    strcat(trainer_id, id_str);
}

void handle_list_scroll(int *y_offset, const int num_saves, const int corrupted_count, int *mouses_down_index, bool *is_moving_scroll, int *banner_position_offset)
{
    const uint8_t box_height = 93;
    // Rows that fit the current (possibly taller) canvas, so the scroll range
    // stays correct when a taller window shows more of the list at once.
    int num_visible = (SCREEN_HEIGHT - 100) / box_height;
    if (num_visible < 1)
    {
        num_visible = 1;
    }
    const int height = num_saves * box_height - 60 * corrupted_count;

    const int min_offset = -height + (num_visible * box_height) + (corrupted_count * 25);
    const int max_offset = 75;

    // Mouse wheel — the primary way to scroll.
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        *y_offset += (int)(wheel * 40);
        *mouses_down_index = -1;
        *is_moving_scroll = true;
    }
    // Arrow keys (held) scroll at a usable speed.
    if (IsKeyDown(KEY_UP))
    {
        *y_offset += 8;
        *mouses_down_index = -1;
        *is_moving_scroll = true;
    }
    if (IsKeyDown(KEY_DOWN))
    {
        *y_offset -= 8;
        *mouses_down_index = -1;
        *is_moving_scroll = true;
    }
    // Click-and-drag still works.
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
    {
        float mouse_delta = GetMouseDelta().y;
        if (mouse_delta >= 0.5f || mouse_delta <= -0.5f)
        {
            *y_offset += (int)mouse_delta;
            *mouses_down_index = -1;
            *is_moving_scroll = true;
        }
    }

    // Clamp the scroll offset (min first so a short list snaps to the top).
    if (*y_offset < min_offset)
    {
        *y_offset = min_offset;
    }
    if (*y_offset > max_offset)
    {
        *y_offset = max_offset;
    }
    // The top banner follows the list as it scrolls past the top.
    *banner_position_offset = *y_offset < 50 ? *y_offset - 50 : 0;

    const int min_y = (height + (num_visible * box_height) + (corrupted_count * 25)) - 75;
    const int max_y = min_y + 75 - (-height + (num_visible * box_height) + (corrupted_count * 25));
    const uint8_t t_max = 50;
    const uint8_t t_min = 0;
    const uint8_t t_rate = 5;
    static uint8_t transparency = 0;

    // scroll position indicator
    float scroll_position = min_y + 75 - *y_offset;
    scroll_position = (scroll_position - min_y) / (max_y - min_y);
    scroll_position = scroll_position < 0 ? 0 : scroll_position;
    scroll_position = scroll_position > 1 ? 1 : scroll_position;
    if (*is_moving_scroll)
    {
        transparency += t_rate;
        if (transparency > t_max)
        {
            transparency = t_max;
        }
    }
    else if (transparency > t_min)
    {
        transparency -= t_rate;
        if (transparency < t_min)
        {
            transparency = t_min;
        }
    }
    if (height > SCREEN_HEIGHT - 100 - 2 * (SCREEN_HEIGHT / 16))
    {
        DrawRectangleGradientV(SCREEN_WIDTH - 20, 50, 20, SCREEN_HEIGHT / 16, (Color){255, 255, 255, 0}, (Color){255, 255, 255, transparency});
        DrawRectangle(SCREEN_WIDTH - 20, 50 + SCREEN_HEIGHT / 16, 20, SCREEN_HEIGHT - 100 - 2 * (SCREEN_HEIGHT / 16), (Color){255, 255, 255, transparency});
        DrawRectangleGradientV(SCREEN_WIDTH - 20, SCREEN_HEIGHT - 50 - SCREEN_HEIGHT / 16, 20, SCREEN_HEIGHT / 16, (Color){255, 255, 255, transparency}, (Color){255, 255, 255, 0});
        draw_pokeball_scroll(scroll_position, (float)transparency * 5.1f);
    }
}

void update_selected_indexes_with_selection(int *selected_saves_index, int *mouses_down_index, bool *is_moving_scroll)
{
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        if (*mouses_down_index != -1 && !*is_moving_scroll)
        {
            if (selected_saves_index[0] == *mouses_down_index)
            {
                selected_saves_index[0] = -1;
            }
            else if (selected_saves_index[1] == *mouses_down_index)
            {
                selected_saves_index[1] = -1;
            }
            else if (selected_saves_index[0] == -1)
            {
                selected_saves_index[0] = *mouses_down_index;
            }
            else if (selected_saves_index[1] == -1)
            {
                selected_saves_index[1] = *mouses_down_index;
            }
        }
        *mouses_down_index = -1;
        *is_moving_scroll = false;
    }
}

void draw_no_save_files(char *save_path)
{
    ClearBackground(RED);
    DrawRectangle(20, 150, SCREEN_WIDTH - 40, SCREEN_HEIGHT - 300, (Color){0, 0, 0, 50});
    if (no_dir_err)
    {
        Vector2 text_center = SCREEN_CENTER("Save folder doesn't exist!", 20);
        shadow_text("Save folder doesn't exist!", text_center.x, text_center.y, 20, WHITE);
    }
    else
    {
        shadow_text("No save files found in save folder", 190, 200, 20, WHITE);
    }
    shadow_text(TextFormat("%s", save_path), SCREEN_CENTER(save_path, 20).x, 230, 20, WHITE);

    char *text = "Add save files to the save folder or drag and drop save files here!";
    shadow_text(text, SCREEN_CENTER(text, 20).x, 300, 20, WHITE);
}

void draw_top_banner(const char *text, const int *banner_position_offset)
{
    int text_width = MeasureText(text, 20);
    DrawRectangle(0, *banner_position_offset - 10, SCREEN_WIDTH, 50, WHITE);
    DrawLineEx((Vector2){0, *banner_position_offset + 45}, (Vector2){SCREEN_WIDTH, *banner_position_offset + 45}, 15, BLACK);
    DrawText(text, SCREEN_WIDTH / 2 - text_width / 2, 10 + *banner_position_offset, 20, BLACK);
}

void draw_raylib_screen_loop(
    struct save_file_data *save_file_data,
    struct trainer_info *trainer1,
    struct trainer_info *trainer2,
    struct TrainerSelection trainerSelection[2],
    char *player1_save_path,
    char *player2_save_path,
    PokemonSave *pkmn_save_player1,
    PokemonSave *pkmn_save_player2)
{
    InitWindow(SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, "Pokerom Trader");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetWindowMinSize(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetTargetFPS(60);
    SetExitKey(0);

    // Fixed-resolution target that every screen renders into, then gets scaled
    // to the current (resizable) window in end_virtual_frame().
    g_render_target = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetTextureFilter(g_render_target.texture, TEXTURE_FILTER_BILINEAR);
    GameScreen current_screen = SCREEN_MAIN_MENU;
    static bool is_same_generation = true;
    static bool should_close_window = false;
    bool is_build_prerelease = strcmp(PROJECT_VERSION_TYPE, "prerelease") == 0;
    Texture2D textures[T_COUNT] = {
        [0 ... T_COUNT - 1] = {
            .id = 0}};
    const struct texture_data *texture_data[6] = {
        &evolve_data,
        &logo_data,
        &quit_data,
        &settings_data,
        &trade_data,
        &boxes_data};

    // while textures are loading
    int texture_load_loop_limit = 3;
    int texture_loop_count = 0;
    while (texture_loop_count <= texture_load_loop_limit && (textures[0].id == 0 || textures[1].id == 0 || textures[2].id == 0 || textures[3].id == 0 || textures[4].id == 0 || textures[T_BOXES].id == 0))
    {
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("Loading Textures...", 20, 20, 20, WHITE);
        EndDrawing();

        for (uint8_t i = T_EVOLVE; i < T_CONSOLE_0; i++)
        {
            if (textures[i].id == 0)
            {
                Image img = {0};
                img.format = FORMAT;
                img.height = texture_data[i]->height;
                img.width = texture_data[i]->width;
                img.data = texture_data[i]->data;
                img.mipmaps = 1;

                textures[i] = LoadTextureFromImage(img);
            }
        }

        for (int i = T_CONSOLE_0; i < T_POKEBALL_0; i++)
        {
            if (textures[i].id == 0)
            {
                Image console = {0};
                console.format = FORMAT;
                console.height = CONSOLE_HEIGHT;
                console.width = CONSOLE_WIDTH;
                console.data = console_data[i - T_CONSOLE_0];
                console.mipmaps = 1;

                textures[i] = LoadTextureFromImage(console);
            }
        }

        for (int i = T_POKEBALL_0; i < T_COUNT; i++)
        {
            if (textures[i].id == 0)
            {
                Image pokeball = {0};
                pokeball.format = FORMAT;
                pokeball.height = BALL_HEIGHT;
                pokeball.width = BALL_WIDTH;
                pokeball.data = ball_data[i - T_POKEBALL_0];
                pokeball.mipmaps = 1;

                textures[i] = LoadTextureFromImage(pokeball);
            }
        }

        texture_loop_count++;
    }

    while (!should_close_window && !WindowShouldClose())
    {
        // The save-file lists grow their canvas with the window (taller window =
        // more rows); every other screen uses the fixed 800x480 layout.
        set_dynamic_viewport(current_screen == SCREEN_FILE_SELECT ||
                             current_screen == SCREEN_BILLS_PC_FILE_SELECT ||
                             current_screen == SCREEN_EVOLVE_FILE_SELECT ||
                             current_screen == SCREEN_EVENTS_FILE_SELECT ||
                             current_screen == SCREEN_POKEDEX_FILE_SELECT);

        switch (current_screen)
        {
        case SCREEN_FILE_SELECT:
            draw_file_select(save_file_data, player1_save_path, player2_save_path, trainer1, trainer2, trainerSelection, pkmn_save_player1, pkmn_save_player2, &current_screen, &is_same_generation);
            break;
        case SCREEN_TRADE:
            draw_trade(pkmn_save_player1, pkmn_save_player2, player1_save_path, player2_save_path, trainerSelection, trainer1, trainer2, &is_same_generation, &current_screen, &textures[T_TRADE]);
            break;
        case SCREEN_TRANSFER:
            draw_transfer(pkmn_save_player1, pkmn_save_player2, player1_save_path, player2_save_path, trainer1, trainer2, &current_screen);
            break;
        case SCREEN_MAIN_MENU:
            draw_main_menu(save_file_data, &current_screen, &should_close_window, textures);
            break;
        case SCREEN_SETTINGS:
            draw_settings(save_file_data, &current_screen, &textures[T_SETTINGS]);
            break;
        case SCREEN_FILE_EDIT:
            draw_change_dir(save_file_data, &current_screen, &textures[T_SETTINGS]);
            break;
        case SCREEN_BILLS_PC_FILE_SELECT:
            draw_file_select_single(save_file_data, pkmn_save_player1, player1_save_path, trainer1, &trainerSelection[0], SINGLE_PLAYER_MENU_TYPE_BILLS_PC, &current_screen);
            break;
        case SCREEN_BILLS_PC:
            draw_bills_pc(pkmn_save_player1, player1_save_path, trainer1, &trainerSelection[0], &current_screen);
            break;
        case SCREEN_POKEDEX_FILE_SELECT:
            draw_file_select_single(save_file_data, pkmn_save_player1, player1_save_path, trainer1, &trainerSelection[0], SINGLE_PLAYER_MENU_TYPE_POKEDEX, &current_screen);
            break;
        case SCREEN_POKEDEX:
            draw_pokedex(pkmn_save_player1, &trainerSelection[0], &current_screen);
            break;
        case SCREEN_EVOLVE_FILE_SELECT:
            draw_file_select_single(save_file_data, pkmn_save_player1, player1_save_path, trainer1, &trainerSelection[0], SINGLE_PLAYER_MENU_TYPE_EVOLVE, &current_screen);
            break;
        case SCREEN_EVOLVE:
            draw_evolve(pkmn_save_player1, player1_save_path, trainer1, &current_screen, &textures[T_EVOLVE]);
            break;
        case SCREEN_EVENTS_FILE_SELECT:
            draw_file_select_single(save_file_data, pkmn_save_player1, player1_save_path, trainer1, &trainerSelection[0], SINGLE_PLAYER_MENU_TYPE_EVENTS, &current_screen);
            break;
        case SCREEN_EVENTS:
            draw_events(pkmn_save_player1, player1_save_path, trainer1, &current_screen);
            break;
        case SCREEN_ABOUT:
            draw_about(&current_screen, is_build_prerelease);
            break;
        case SCREEN_LEGAL:
            draw_legal(&current_screen);
            break;
        default:
            begin_virtual_frame();
            ClearBackground(BACKGROUND_COLOR);
            DrawText("Something went wrong", SCREEN_CENTER("Something went wrong", 20).x, SCREEN_CENTER("Something went wrong", 20).y, 20, BLACK);
            DrawText("Press ESC key to exit!", SCREEN_CENTER("Press ESC key to exit!", 20).x, SCREEN_CENTER("Press ESC key to exit!", 20).x + 50, 20, BLACK);
            end_virtual_frame();
            // Escape key to close window
            if (IsKeyPressed(KEY_ESCAPE))
            {
                should_close_window = true;
            }
            break;
        }
    }

    for (uint8_t i = 0; i < T_COUNT; i++)
    {
        UnloadTexture(textures[i]);
    }

    UnloadRenderTexture(g_render_target);
    CloseWindow();
}

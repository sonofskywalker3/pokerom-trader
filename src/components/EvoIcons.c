#include "raylibhelper.h"
#include "pkmn_evolutions.h"

// Tiny vector icons marking HOW a Pokemon evolves toward an unregistered
// dex entry: rare candy (level), stone, trade arrows, "?" for special
// methods, or an egg for a pre-evolution it can breed. Drawn inside an h-by-h box whose top-left corner is (x, y).

static Color evo_tint(Color c, unsigned char alpha)
{
    c.a = alpha;
    return c;
}

void draw_evo_method_icon(int method, int x, int y, int h, unsigned char alpha)
{
    float cx = x + h / 2.0f;
    float cy = y + h / 2.0f;
    switch (method)
    {
    case PKMN_EVO_LEVEL: // rare candy: blue-wrapped diamond with a shine
        DrawPoly((Vector2){cx, cy}, 4, h / 2.0f, 0, evo_tint((Color){120, 170, 255, 255}, alpha));
        DrawPolyLines((Vector2){cx, cy}, 4, h / 2.0f, 0, evo_tint((Color){30, 60, 140, 255}, alpha));
        DrawCircle((int)cx, (int)cy, h / 6.0f, evo_tint(RAYWHITE, alpha));
        break;
    case PKMN_EVO_STONE: // evolution stone: teal hexagon
        DrawPoly((Vector2){cx, cy}, 6, h / 2.0f, 0, evo_tint((Color){95, 215, 170, 255}, alpha));
        DrawPolyLines((Vector2){cx, cy}, 6, h / 2.0f, 0, evo_tint((Color){20, 90, 65, 255}, alpha));
        break;
    case PKMN_EVO_TRADE: // two-headed left/right arrow
    {
        Color c = evo_tint((Color){255, 205, 90, 255}, alpha);
        float mid = cy;
        float head = h * 0.38f;
        DrawRectangleRec((Rectangle){x + head - 1, mid - 1, h - 2 * head + 2, 3}, c);
        DrawTriangle((Vector2){(float)x, mid}, (Vector2){x + head, mid + head}, (Vector2){x + head, mid - head}, c);
        DrawTriangle((Vector2){(float)(x + h), mid}, (Vector2){x + h - head, mid - head}, (Vector2){x + h - head, mid + head}, c);
        break;
    }
    case PKMN_EVO_BREED: // egg: cream oval with green spots
    {
        Color shell = evo_tint((Color){250, 240, 210, 255}, alpha);
        Color rim = evo_tint((Color){120, 100, 60, 255}, alpha);
        Color spot = evo_tint((Color){110, 190, 110, 255}, alpha);
        DrawEllipse((int)cx, (int)cy, h * 0.36f, h * 0.5f, shell);
        DrawEllipseLines((int)cx, (int)cy, h * 0.36f, h * 0.5f, rim);
        DrawCircle((int)(cx - h * 0.12f), (int)(cy - h * 0.15f), h * 0.09f, spot);
        DrawCircle((int)(cx + h * 0.12f), (int)(cy + h * 0.1f), h * 0.09f, spot);
        break;
    }
    case PKMN_EVO_SPECIAL: // circled question mark
    default:
    {
        Color c = evo_tint((Color){235, 235, 245, 255}, alpha);
        DrawCircleLines((int)cx, (int)cy, h / 2.0f, c);
        int q_size = h > 10 ? h - 2 : 10; // default raylib font bottoms out at 10
        int q_w = MeasureText("?", q_size);
        DrawText("?", (int)(cx - q_w / 2.0f), (int)(cy - q_size / 2.0f), q_size, c);
        break;
    }
    }
}

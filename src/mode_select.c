#include "mode_select.h"

#define MS_SCREEN_W   800
#define MS_SCREEN_H   520

#define MS_TILE_W     460
#define MS_TILE_H      80
#define MS_TILE_GAP    20
#define MS_FIRST_Y    140

#define MS_TILE_COUNT GAME_MODE_COUNT  /* = 3 */

/* "Retour" button — same proportions as the JOUER/OPTIONS buttons on the
 * main menu (300x50 button texture, 200 wide here so it sits at the
 * bottom-left without dominating the screen).                            */
#define MS_BACK_W      200
#define MS_BACK_H       50
#define MS_BACK_X       40
#define MS_BACK_Y      (MS_SCREEN_H - MS_BACK_H - 40)

static SDL_Rect s_rects[MS_TILE_COUNT];
static SDL_Rect s_back_rect;
static int      s_hovered  = -1;
static int      s_back_hovered = 0;
static int      s_selected =  0;

GameMode g_game_mode = MODE_CAMPAIGN;

typedef struct { const char *name; const char *desc; } ModeLabel;

static const ModeLabel kModeLabels[MS_TILE_COUNT] = {
    [MODE_CAMPAIGN] = {"CAMPAIGN", "1 player - live the story"},
    [MODE_COOP]     = {"CO-OP",    "2 players - beat the game together"},
    [MODE_VERSUS]   = {"VERSUS",   "2 players - face off"},
};

static void ms_compute_rects(void)
{
    int x0 = (MS_SCREEN_W - MS_TILE_W) / 2;
    for (int i = 0; i < MS_TILE_COUNT; i++) {
        s_rects[i] = (SDL_Rect){
            x0,
            MS_FIRST_Y + i * (MS_TILE_H + MS_TILE_GAP),
            MS_TILE_W,
            MS_TILE_H
        };
    }
    s_back_rect = (SDL_Rect){MS_BACK_X, MS_BACK_Y, MS_BACK_W, MS_BACK_H};
}

void mode_select_init(SDL_Renderer *r)
{
    (void)r;            /* no textures owned */
    ms_compute_rects();
}

void mode_select_free(void)
{
    /* no-op */
}

void mode_select_enter(void)
{
    s_hovered  = -1;
    s_selected = (int)g_game_mode;
    if (s_selected < 0 || s_selected >= MS_TILE_COUNT) s_selected = 0;
}

void mode_select_handle_motion(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    s_hovered = -1;
    s_back_hovered = SDL_PointInRect(&p, &s_back_rect) ? 1 : 0;
    for (int i = 0; i < MS_TILE_COUNT; i++)
        if (SDL_PointInRect(&p, &s_rects[i])) { s_hovered = i; break; }
    if (s_hovered >= 0) s_selected = s_hovered;
}

ModeSelectAction mode_select_handle_click(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    if (SDL_PointInRect(&p, &s_back_rect)) {
        return MS_ACTION_CANCEL;
    }
    for (int i = 0; i < MS_TILE_COUNT; i++) {
        if (SDL_PointInRect(&p, &s_rects[i])) {
            s_selected  = i;
            g_game_mode = (GameMode)i;
            return MS_ACTION_CONFIRM;
        }
    }
    return MS_ACTION_NONE;
}

ModeSelectAction mode_select_handle_key(SDL_Keycode k)
{
    if (k == SDLK_UP   || k == SDLK_w)
        s_selected = (s_selected + MS_TILE_COUNT - 1) % MS_TILE_COUNT;
    else if (k == SDLK_DOWN || k == SDLK_s)
        s_selected = (s_selected + 1) % MS_TILE_COUNT;
    else if (k == SDLK_RETURN || k == SDLK_SPACE) {
        g_game_mode = (GameMode)s_selected;
        return MS_ACTION_CONFIRM;
    } else if (k == SDLK_ESCAPE) {
        return MS_ACTION_CANCEL;
    }
    return MS_ACTION_NONE;
}

/* Same letterbox/vignette pass weapon_select and skin_select use, so
 * the submenu chain reads as one continuous screen flow.              */
static void ms_paint_backdrop(SDL_Renderer *r, SDL_Texture *bg)
{
    if (bg) {
        SDL_RenderCopy(r, bg, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(r, 12, 14, 22, 255);
        SDL_RenderClear(r);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
    SDL_Rect left  = {0,                 0, 40, MS_SCREEN_H};
    SDL_Rect right = {MS_SCREEN_W - 40,  0, 40, MS_SCREEN_H};
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_Rect top    = {0, 0, MS_SCREEN_W, 6};
    SDL_Rect bottom = {0, MS_SCREEN_H - 6, MS_SCREEN_W, 6};
    SDL_RenderFillRect(r, &top);
    SDL_RenderFillRect(r, &bottom);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void ms_draw_tile(SDL_Renderer *r,
                         UIFontPack   *fonts,
                         UITextCache  *cache,
                         int idx, int is_selected, int is_hovered)
{
    SDL_Rect rect = s_rects[idx];

    /* Dark interior + warm glow on the cursor tile.                    */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 14, 16, 24, 230);
    SDL_RenderFillRect(r, &rect);
    if (is_selected) {
        SDL_SetRenderDrawColor(r, 235, 180, 60, 36);
        SDL_Rect glow = {rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16};
        SDL_RenderFillRect(r, &glow);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Name on the left half, description right beneath it.             */
    SDL_Color name_c = is_selected
                     ? (SDL_Color){255, 220, 100, 255}
                     : (SDL_Color){200, 205, 220, 255};
    SDL_Color desc_c = is_selected
                     ? (SDL_Color){220, 225, 240, 220}
                     : (SDL_Color){150, 160, 180, 200};

    int pad_x  = 24;
    int name_y = rect.y + 12;
    int desc_y = rect.y + 46;
    ui_draw_text(cache, r, fonts->bebasHeader,
                 kModeLabels[idx].name, name_c,
                 rect.x + pad_x, name_y);
    ui_draw_text(cache, r, fonts->mono,
                 kModeLabels[idx].desc, desc_c,
                 rect.x + pad_x, desc_y);

    /* Border: bright yellow on cursor, white on hover-only, grey idle. */
    if (is_selected) {
        SDL_SetRenderDrawColor(r, 235, 180, 60, 255);
        for (int t = 0; t < 3; t++) {
            SDL_Rect b = {rect.x - t, rect.y - t, rect.w + 2*t, rect.h + 2*t};
            SDL_RenderDrawRect(r, &b);
        }
    } else if (is_hovered) {
        SDL_SetRenderDrawColor(r, 210, 215, 240, 255);
        for (int t = 0; t < 2; t++) {
            SDL_Rect b = {rect.x - t, rect.y - t, rect.w + 2*t, rect.h + 2*t};
            SDL_RenderDrawRect(r, &b);
        }
    } else {
        SDL_SetRenderDrawColor(r, 60, 65, 85, 255);
        SDL_RenderDrawRect(r, &rect);
    }
}

/* Render the back button using the same texture+color-mod look as the
 * UIButton path that the main menu uses.  Mirrors ui_draw_one() so the
 * style stays consistent without exposing UIButtonState plumbing.       */
static void ms_draw_back_button(SDL_Renderer *r,
                                UIFontPack   *fonts,
                                UITextCache  *cache,
                                SDL_Texture  *button_tex)
{
    SDL_Rect dst = s_back_rect;
    int hot = s_back_hovered;

    if (button_tex) {
        Uint8 mr = hot ? 255 : 200;
        Uint8 mg = hot ? 190 : 150;
        Uint8 mb = hot ?  70 :  50;
        Uint8 ma = hot ? 235 : 195;
        SDL_SetTextureColorMod(button_tex, mr, mg, mb);
        SDL_SetTextureAlphaMod(button_tex, ma);
        SDL_RenderCopy(r, button_tex, NULL, &dst);
        SDL_SetTextureColorMod(button_tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(button_tex, 255);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color fc = hot ? (SDL_Color){255, 190, 70, 230}
                           : (SDL_Color){200, 150, 50, 195};
        SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, fc.a);
        SDL_RenderFillRect(r, &dst);
        SDL_SetRenderDrawColor(r, 180, 130, 40, 255);
        SDL_RenderDrawRect(r, &dst);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    SDL_Color txt = hot ? (SDL_Color){255, 240, 180, 255}
                        : (SDL_Color){232, 213, 163, 255};
    ui_draw_text_in_rect(cache, r, fonts->bebasHeader, "BACK", txt, dst);
}

void mode_select_render(SDL_Renderer *r,
                        UIFontPack   *fonts,
                        UITextCache  *cache,
                        SDL_Texture  *bg,
                        SDL_Texture  *button_tex)
{
    ms_paint_backdrop(r, bg);

    SDL_Color titre = {255, 215,   0, 255};
    SDL_Color hint  = {180, 190, 210, 230};

    ui_draw_text_centered(cache, r, fonts->bebasTitle,
                          "GAME MODE", titre,
                          MS_SCREEN_W/2, 60);

    for (int i = 0; i < MS_TILE_COUNT; i++) {
        ms_draw_tile(r, fonts, cache, i,
                     s_selected == i,
                     s_hovered  == i && s_selected != i);
    }

    ms_draw_back_button(r, fonts, cache, button_tex);

    ui_draw_text_centered(cache, r, fonts->mono,
        "[MOUSE] Click  [^/v] Navigate  "
        "[ENTER] Confirm  [ESC] Back",
        hint, MS_SCREEN_W/2, MS_SCREEN_H - 24);
}

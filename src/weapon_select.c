#include "weapon_select.h"
#include "two_player.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>

/* W_ECRAN / H_ECRAN are 800 / 520 in this project; we hard-code those
 * coordinates the same way the other menu layouts do.                  */
#define WS_SCREEN_W       800
#define WS_SCREEN_H       520

#define WS_BTN_W          260
#define WS_BTN_H          320
#define WS_BTN_GAP         60
#define WS_BTN_Y          130

/* Two buttons, axe = 0, sword = 1.                                    */
#define WS_BTN_COUNT       2

/* Back button — matches main-menu button proportions (300x50 texture).
 * 200x50 sits cleanly at the bottom-left.                              */
#define WS_BACK_W         200
#define WS_BACK_H          50
#define WS_BACK_X          40
#define WS_BACK_Y         (WS_SCREEN_H - WS_BACK_H - 40)

static SDL_Rect    s_rects[WS_BTN_COUNT];
static SDL_Rect    s_back_rect;
static SDL_Texture *s_icons[WS_BTN_COUNT];
static int         s_hovered      = -1;
static int         s_back_hovered =  0;
static int         s_selected     =  0;
static int         s_loaded       =  0;
static int         s_target_slot  =  0;

/* Two-line trade-off blurb shown under each weapon, kept short so each
 * line fits comfortably inside the 260px wide button without overlap.  */
static const char *ws_description_line1(int i)
{
    return (i == 0) ? "Slower but"  : "Faster but";
}
static const char *ws_description_line2(int i)
{
    return (i == 0) ? "more damage" : "less damage";
}

/* Map button index 0/1 to the WEAPON_* enum (kept explicit so reordering
 * is safe).                                                            */
static Weapon ws_index_to_weapon(int i)
{
    return (i == 0) ? WEAPON_AXE : WEAPON_SWORD;
}

static const char *ws_label(int i)
{
    return (i == 0) ? "AXE" : "SWORD";
}

/* Path chain, tried in order.  First file that loads wins.            */
static const char *ws_icon_paths[] = {
    "assets/ui/weapon_button_%s.png",          /* bespoke UI slot      */
    "spritesheets assets/%s.png",              /* artist drop location */
    "assets/sprites/characters/ellie/%s/preview.png",  /* portrait fb  */
};

static SDL_Texture *ws_load_icon(SDL_Renderer *r, int idx)
{
    const char *weapon_dir = (idx == 0) ? "axe" : "sword";
    char path[256];

    for (size_t i = 0; i < sizeof(ws_icon_paths)/sizeof(ws_icon_paths[0]); i++) {
        snprintf(path, sizeof(path), ws_icon_paths[i], weapon_dir);
        SDL_Texture *t = IMG_LoadTexture(r, path);
        if (t) return t;
    }
    fprintf(stderr, "[weapon_select] no icon found for '%s' (tried "
                    "assets/ui/, spritesheets assets/, portrait fallback)\n",
            weapon_dir);
    return NULL;
}

void weapon_select_init(SDL_Renderer *r)
{
    /* Static layout: two buttons centered horizontally. */
    int total = WS_BTN_W * 2 + WS_BTN_GAP;
    int x0    = (WS_SCREEN_W - total) / 2;
    s_rects[0] = (SDL_Rect){x0,                          WS_BTN_Y, WS_BTN_W, WS_BTN_H};
    s_rects[1] = (SDL_Rect){x0 + WS_BTN_W + WS_BTN_GAP,  WS_BTN_Y, WS_BTN_W, WS_BTN_H};
    s_back_rect = (SDL_Rect){WS_BACK_X, WS_BACK_Y, WS_BACK_W, WS_BACK_H};

    if (s_loaded) return;
    for (int i = 0; i < WS_BTN_COUNT; i++)
        s_icons[i] = ws_load_icon(r, i);
    s_loaded = 1;
}

void weapon_select_free(void)
{
    for (int i = 0; i < WS_BTN_COUNT; i++) {
        if (s_icons[i]) SDL_DestroyTexture(s_icons[i]);
        s_icons[i] = NULL;
    }
    s_loaded = 0;
}

void weapon_select_enter(int target_slot)
{
    if (target_slot < 0 || target_slot >= MAX_PLAYERS) target_slot = 0;
    s_target_slot = target_slot;
    s_hovered     = -1;
    s_selected    = (player_cfg[s_target_slot].weapon == WEAPON_SWORD) ? 1 : 0;
}

void weapon_select_handle_motion(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    s_hovered = -1;
    s_back_hovered = SDL_PointInRect(&p, &s_back_rect) ? 1 : 0;
    for (int i = 0; i < WS_BTN_COUNT; i++) {
        if (SDL_PointInRect(&p, &s_rects[i])) { s_hovered = i; break; }
    }
    if (s_hovered >= 0) s_selected = s_hovered;
}

WeaponSelectAction weapon_select_handle_click(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    if (SDL_PointInRect(&p, &s_back_rect)) {
        return WS_ACTION_CANCEL;
    }
    for (int i = 0; i < WS_BTN_COUNT; i++) {
        if (SDL_PointInRect(&p, &s_rects[i])) {
            s_selected = i;
            player_cfg[s_target_slot].weapon = ws_index_to_weapon(i);
            return WS_ACTION_CONFIRM;
        }
    }
    return WS_ACTION_NONE;
}

WeaponSelectAction weapon_select_handle_key(SDL_Keycode k)
{
    if (k == SDLK_LEFT || k == SDLK_a)
        s_selected = (s_selected + WS_BTN_COUNT - 1) % WS_BTN_COUNT;
    else if (k == SDLK_RIGHT || k == SDLK_d)
        s_selected = (s_selected + 1) % WS_BTN_COUNT;
    else if (k == SDLK_RETURN || k == SDLK_SPACE) {
        player_cfg[s_target_slot].weapon = ws_index_to_weapon(s_selected);
        return WS_ACTION_CONFIRM;
    } else if (k == SDLK_ESCAPE) {
        return WS_ACTION_CANCEL;
    }
    return WS_ACTION_NONE;
}

/* Letterbox/vignette pass — matches the menu/save screens' dark
 * cinematic look so the new screen blends in even without a bespoke
 * background image.                                                  */
static void ws_paint_backdrop(SDL_Renderer *r, SDL_Texture *bg)
{
    if (bg) {
        SDL_RenderCopy(r, bg, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(r, 12, 14, 22, 255);
        SDL_RenderClear(r);
    }
    /* Vignette darken sides. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
    SDL_Rect left  = {0,                       0, 60,  WS_SCREEN_H};
    SDL_Rect right = {WS_SCREEN_W - 60,        0, 60,  WS_SCREEN_H};
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    /* Top/bottom letterbox. */
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_Rect top    = {0, 0, WS_SCREEN_W, 6};
    SDL_Rect bottom = {0, WS_SCREEN_H - 6, WS_SCREEN_W, 6};
    SDL_RenderFillRect(r, &top);
    SDL_RenderFillRect(r, &bottom);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Fit (sw, sh) into rect preserving aspect (letterbox inside).         */
static SDL_Rect ws_fit(SDL_Rect rect, int sw, int sh)
{
    if (sw <= 0 || sh <= 0) return rect;
    float ra = (float)rect.w / (float)rect.h;
    float ia = (float)sw     / (float)sh;
    SDL_Rect out = rect;
    if (ia > ra) {
        out.w = rect.w;
        out.h = (int)(rect.w / ia + 0.5f);
    } else {
        out.h = rect.h;
        out.w = (int)(rect.h * ia + 0.5f);
    }
    out.x = rect.x + (rect.w - out.w) / 2;
    out.y = rect.y + (rect.h - out.h) / 2;
    return out;
}

static void ws_draw_button(SDL_Renderer *r,
                           UIFontPack   *fonts,
                           UITextCache  *cache,
                           int idx,
                           int is_selected,
                           int is_hovered)
{
    SDL_Rect    rect = s_rects[idx];
    SDL_Texture *tex = s_icons[idx];

    /* Dark interior panel.                                             */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 14, 16, 24, 230);
    SDL_RenderFillRect(r, &rect);

    /* Selection glow (warm yellow tied to the main menu's accent).     */
    if (is_selected) {
        SDL_SetRenderDrawColor(r, 235, 180, 60, 36);
        SDL_Rect glow = {rect.x - 10, rect.y - 10, rect.w + 20, rect.h + 20};
        SDL_RenderFillRect(r, &glow);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Icon — fitted, slightly dimmed when not selected.  Reserves
     * ~120px at the bottom of the tile for label + two-line description
     * so the text never overlaps the icon.                             */
    if (tex) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
        SDL_Rect inner = {rect.x + 14, rect.y + 18,
                          rect.w - 28, rect.h - 120};
        SDL_Rect fit   = ws_fit(inner, tw, th);
        if (!is_selected) SDL_SetTextureAlphaMod(tex, 150);
        SDL_RenderCopy(r, tex, NULL, &fit);
        if (!is_selected) SDL_SetTextureAlphaMod(tex, 255);
    } else {
        /* Procedural placeholder: a diagonal stroke + label.           */
        SDL_SetRenderDrawColor(r,
            is_selected ? 235 : 80,
            is_selected ? 180 : 90,
            is_selected ?  60 : 110, 255);
        SDL_Rect bar = {rect.x + rect.w/2 - 6, rect.y + 30,
                        12,                    rect.h - 150};
        SDL_RenderFillRect(r, &bar);
    }

    /* Label band, then two-line description directly under it.  Spacing
     * (label at h-90, line1 at h-52, line2 at h-30) keeps each line
     * readable with a small gap between them.                          */
    SDL_Color label = is_selected
                    ? (SDL_Color){255, 220, 100, 255}
                    : (SDL_Color){200, 205, 220, 255};
    ui_draw_text_centered(cache, r, fonts->bebasHeader,
                          ws_label(idx), label,
                          rect.x + rect.w/2,
                          rect.y + rect.h - 90);

    SDL_Color desc_c = is_selected
                     ? (SDL_Color){220, 225, 240, 230}
                     : (SDL_Color){150, 160, 180, 200};
    ui_draw_text_centered(cache, r, fonts->mono,
                          ws_description_line1(idx), desc_c,
                          rect.x + rect.w/2,
                          rect.y + rect.h - 52);
    ui_draw_text_centered(cache, r, fonts->mono,
                          ws_description_line2(idx), desc_c,
                          rect.x + rect.w/2,
                          rect.y + rect.h - 30);

    /* Outline: thick yellow when selected, thin grey otherwise, with a
     * lighter outline when hovered (but not yet clicked).              */
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

static void ws_draw_back_button(SDL_Renderer *r,
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

void weapon_select_render(SDL_Renderer *r,
                          UIFontPack   *fonts,
                          UITextCache  *cache,
                          SDL_Texture  *bg,
                          SDL_Texture  *button_tex)
{
    ws_paint_backdrop(r, bg);

    SDL_Color titre = {255, 215,   0, 255};   /* matches existing menus  */
    SDL_Color sous  = {220, 222, 232, 255};
    SDL_Color hint  = {180, 190, 210, 230};

    char header[48];
    snprintf(header, sizeof(header),
             "PLAYER %d - CHOOSE YOUR WEAPON", s_target_slot + 1);
    ui_draw_text_centered(cache, r, fonts->bebasTitle,
                          header, titre, WS_SCREEN_W/2, 60);

    ui_draw_text_centered(cache, r, fonts->story,
                          "What will be your weapon ?", sous,
                          WS_SCREEN_W/2, 100);

    for (int i = 0; i < WS_BTN_COUNT; i++) {
        ws_draw_button(r, fonts, cache, i,
                       /* selected: keyboard cursor              */
                       s_selected == i,
                       /* hovered: mouse over (but not selected) */
                       s_hovered  == i && s_selected != i);
    }

    ws_draw_back_button(r, fonts, cache, button_tex);

    ui_draw_text_centered(cache, r, fonts->mono,
        "[MOUSE] Click  [<-/->] Navigate  "
        "[ENTER] Confirm  [ESC] Back",
        hint, WS_SCREEN_W/2, WS_SCREEN_H - 24);
}

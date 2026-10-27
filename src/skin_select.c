#include "skin_select.h"
#include "weapon_select.h"
#include "two_player.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>

#define SS_SCREEN_W   800
#define SS_SCREEN_H   520

#define SS_TILE_W     160
#define SS_TILE_H     260   /* +40 so the label has clearance under the portrait */
#define SS_TILE_GAP    20
#define SS_TILE_Y     130

#define SS_TILE_COUNT  SKIN_COUNT   /* 4 in current build */

/* Back button — same metrics as the main-menu buttons, bottom-left.  */
#define SS_BACK_W      200
#define SS_BACK_H       50
#define SS_BACK_X       40
#define SS_BACK_Y      (SS_SCREEN_H - SS_BACK_H - 40)

static SDL_Rect    s_rects[SS_TILE_COUNT];
static SDL_Rect    s_back_rect;
/* One live animator per tile (loaded with the slot's weapon).  The
 * focused tile swings its attack; the rest idle.                        */
static CharAnim    s_anims[SS_TILE_COUNT];
static int         s_hovered      = -1;
static int         s_back_hovered =  0;
static int         s_selected     =  0;
static int         s_init         =  0;
static int         s_target_slot  =  0;

/* Skins shown left-to-right.  Order matches the enum so SKIN_COUNT
 * additions automatically appear in the strip.                      */
static const Skin kSkinOrder[SS_TILE_COUNT] = {
    SKIN_CURTIS, SKIN_CURTIS1, SKIN_ELLIE, SKIN_NAMGOONG,
};

static const char *ss_display_label(Skin s)
{
    switch (s) {
        case SKIN_CURTIS:   return "CURTIS";
        case SKIN_CURTIS1:  return "CURTIS 1";
        case SKIN_ELLIE:    return "ELLIE";
        case SKIN_NAMGOONG: return "NAMGOONG";
        default:            return "?";
    }
}

static void ss_compute_rects(void)
{
    int total = SS_TILE_W * SS_TILE_COUNT + SS_TILE_GAP * (SS_TILE_COUNT - 1);
    int x0    = (SS_SCREEN_W - total) / 2;
    for (int i = 0; i < SS_TILE_COUNT; i++) {
        s_rects[i] = (SDL_Rect){
            x0 + i * (SS_TILE_W + SS_TILE_GAP),
            SS_TILE_Y,
            SS_TILE_W, SS_TILE_H
        };
    }
    s_back_rect = (SDL_Rect){SS_BACK_X, SS_BACK_Y, SS_BACK_W, SS_BACK_H};
}

static void ss_free_anims(void)
{
    for (int i = 0; i < SS_TILE_COUNT; i++)
        char_anim_free(&s_anims[i]);
}

static void ss_reload_anims(SDL_Renderer *r)
{
    /* Load every tile with the slot's just-picked weapon so the swing
     * shows the right axe/sword, and park them all on IDLE.            */
    Weapon w = player_cfg[s_target_slot].weapon;
    for (int i = 0; i < SS_TILE_COUNT; i++) {
        if (!char_anim_load(&s_anims[i], r, kSkinOrder[i], w))
            fprintf(stderr, "[skin_select] anim load failed: %s/%s\n",
                    char_skin_name(kSkinOrder[i]), char_weapon_name(w));
        char_anim_set(&s_anims[i], ANIM_IDLE);
    }
}

void skin_select_init(SDL_Renderer *r)
{
    ss_compute_rects();
    if (!s_init) {
        ss_reload_anims(r);
        s_init = 1;
    }
}

void skin_select_free(void)
{
    ss_free_anims();
    s_init = 0;
}

void skin_select_enter(SDL_Renderer *r, int target_slot)
{
    if (target_slot < 0 || target_slot >= MAX_PLAYERS) target_slot = 0;
    s_target_slot = target_slot;
    s_hovered     = -1;
    /* Land the cursor on the slot's current pick. */
    s_selected = 0;
    for (int i = 0; i < SS_TILE_COUNT; i++)
        if (kSkinOrder[i] == player_cfg[s_target_slot].skin) {
            s_selected = i; break;
        }
    /* Animations mirror the slot's just-picked weapon. */
    ss_reload_anims(r);
}

/* Drive the per-tile animators.  The focused tile loops its attack
 * swing (re-arming the one-shot each time it finishes); the others
 * fall back to a calm idle.                                            */
void skin_select_update(float dt)
{
    for (int i = 0; i < SS_TILE_COUNT; i++) {
        CharAnim *ca = &s_anims[i];
        if (!ca->loaded) continue;

        if (i == s_selected) {
            if (ca->current != ANIM_ATTACK || char_anim_is_finished(ca)) {
                ca->is_locked = 0;                 /* allow the replay   */
                char_anim_set(ca, ANIM_ATTACK);
            }
        } else if (ca->current != ANIM_IDLE) {
            ca->is_locked = 0;                     /* drop a held swing  */
            char_anim_set(ca, ANIM_IDLE);
        }

        char_anim_update(ca, dt);
    }
}

void skin_select_handle_motion(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    s_hovered = -1;
    s_back_hovered = SDL_PointInRect(&p, &s_back_rect) ? 1 : 0;
    for (int i = 0; i < SS_TILE_COUNT; i++)
        if (SDL_PointInRect(&p, &s_rects[i])) { s_hovered = i; break; }
    if (s_hovered >= 0) s_selected = s_hovered;
}

SkinSelectAction skin_select_handle_click(int lx, int ly)
{
    SDL_Point p = {lx, ly};
    if (SDL_PointInRect(&p, &s_back_rect)) {
        return SS_ACTION_CANCEL;
    }
    for (int i = 0; i < SS_TILE_COUNT; i++) {
        if (SDL_PointInRect(&p, &s_rects[i])) {
            s_selected = i;
            player_cfg[s_target_slot].skin = kSkinOrder[i];
            return SS_ACTION_CONFIRM;
        }
    }
    return SS_ACTION_NONE;
}

SkinSelectAction skin_select_handle_key(SDL_Keycode k)
{
    if (k == SDLK_LEFT || k == SDLK_a)
        s_selected = (s_selected + SS_TILE_COUNT - 1) % SS_TILE_COUNT;
    else if (k == SDLK_RIGHT || k == SDLK_d)
        s_selected = (s_selected + 1) % SS_TILE_COUNT;
    else if (k == SDLK_RETURN || k == SDLK_SPACE) {
        player_cfg[s_target_slot].skin = kSkinOrder[s_selected];
        return SS_ACTION_CONFIRM;
    } else if (k == SDLK_ESCAPE) {
        return SS_ACTION_CANCEL;
    }
    return SS_ACTION_NONE;
}

/* Letterbox/vignette to match weapon_select.                          */
static void ss_paint_backdrop(SDL_Renderer *r, SDL_Texture *bg)
{
    if (bg) {
        SDL_RenderCopy(r, bg, NULL, NULL);
    } else {
        SDL_SetRenderDrawColor(r, 12, 14, 22, 255);
        SDL_RenderClear(r);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 110);
    SDL_Rect left  = {0, 0, 40, SS_SCREEN_H};
    SDL_Rect right = {SS_SCREEN_W - 40, 0, 40, SS_SCREEN_H};
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_Rect top    = {0, 0, SS_SCREEN_W, 6};
    SDL_Rect bottom = {0, SS_SCREEN_H - 6, SS_SCREEN_W, 6};
    SDL_RenderFillRect(r, &top);
    SDL_RenderFillRect(r, &bottom);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void ss_draw_tile(SDL_Renderer *r,
                         UIFontPack   *fonts,
                         UITextCache  *cache,
                         int idx,
                         int is_selected,
                         int is_hovered)
{
    SDL_Rect    rect = s_rects[idx];

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 14, 16, 24, 230);
    SDL_RenderFillRect(r, &rect);
    if (is_selected) {
        SDL_SetRenderDrawColor(r, 235, 180, 60, 36);
        SDL_Rect glow = {rect.x - 8, rect.y - 8, rect.w + 16, rect.h + 16};
        SDL_RenderFillRect(r, &glow);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Live character: feet anchored just above the label band, scaled to
     * sit inside the tile.  The focused tile is mid-swing (driven by
     * skin_select_update); the rest idle.                               */
    {
        int center_x = rect.x + rect.w / 2;
        int foot_y   = rect.y + rect.h - 72;
        int disp_h   = rect.h - 96;
        char_anim_render(&s_anims[idx], r, center_x, foot_y, disp_h,
                         SDL_FLIP_NONE);

        if (!is_selected) {
            /* Dim the unfocused portraits, matching the old alpha look. */
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 14, 16, 24, 120);
            SDL_Rect dim = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 48};
            SDL_RenderFillRect(r, &dim);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
    }

    SDL_Color label = is_selected
                    ? (SDL_Color){255, 220, 100, 255}
                    : (SDL_Color){200, 205, 220, 255};
    /* Label top is 44px from the tile's bottom edge — bebasHeader
     * runs ~30px tall, leaving comfortable clearance.            */
    ui_draw_text_centered(cache, r, fonts->bebasHeader,
                          ss_display_label(kSkinOrder[idx]), label,
                          rect.x + rect.w/2,
                          rect.y + rect.h - 44);

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

static void ss_draw_back_button(SDL_Renderer *r,
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

void skin_select_render(SDL_Renderer *r,
                        UIFontPack   *fonts,
                        UITextCache  *cache,
                        SDL_Texture  *bg,
                        SDL_Texture  *button_tex)
{
    ss_paint_backdrop(r, bg);

    SDL_Color titre = {255, 215,   0, 255};
    SDL_Color sous  = {220, 222, 232, 255};
    SDL_Color hint  = {180, 190, 210, 230};

    char header[64];
    snprintf(header, sizeof(header),
             "PLAYER %d - CHOOSE YOUR CHARACTER", s_target_slot + 1);
    ui_draw_text_centered(cache, r, fonts->bebasTitle,
                          header, titre, SS_SCREEN_W/2, 60);

    char sub[64];
    snprintf(sub, sizeof(sub), "Weapon : %s",
             player_cfg[s_target_slot].weapon == WEAPON_AXE
                 ? "Axe" : "Sword");
    ui_draw_text_centered(cache, r, fonts->story,
                          sub, sous, SS_SCREEN_W/2, 100);

    for (int i = 0; i < SS_TILE_COUNT; i++) {
        ss_draw_tile(r, fonts, cache, i,
                     s_selected == i,
                     s_hovered  == i && s_selected != i);
    }

    ss_draw_back_button(r, fonts, cache, button_tex);

    ui_draw_text_centered(cache, r, fonts->mono,
        "[MOUSE] Click  [<-/->] Navigate  "
        "[ENTER] Confirm  [ESC] Back",
        hint, SS_SCREEN_W/2, SS_SCREEN_H - 24);
}

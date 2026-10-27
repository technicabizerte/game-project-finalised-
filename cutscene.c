#include "cutscene.h"
#include "background.h"   /* W_ECRAN, H_ECRAN, MAX_WAGONS */
#include "fade.h"

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Asset path for the wide train shot used in panel 2.  Defined here so it
 * is easy to repoint (e.g. swap a higher-res panorama later).            */
#define CUTSCENE_TRAIN_PATH  "assets/intro/train_cutscene.png"

/* Wagon names mirror the inline HUD list in main.c - kept here so the
 * cutscene stays self-contained.                                        */
static const char *CUT_WAGON_NOMS[MAX_WAGONS] = {
    "Wagon Pauvres",
    "Wagon Industrie",
    "Wagon Aquarium",
    "Wagon Jardin",
    "Wagon Luxe"
};

/* Panel 1 - lore text crawl */
static const char *CUT_LORE_LINES[3] = {
    "2031. La Terre est gelee.",
    "Les derniers survivants vivent a bord du Snowpiercer.",
    "Tu es Curtis. Tu vis en queue de train."
};

/* Panel durations (seconds). */
#define CUT_LORE_DUR        4.0f
#define CUT_TRAIN_DUR       4.0f
#define CUT_TITLECARD_DUR   2.0f
#define CUT_FADEIN_DUR      1.0f
#define CUT_SKIP_FADEIN_DUR 0.3f

/* Panel 2 visual constants. */
#define CUT_TRAIN_SCROLL_PX_S   80.0f
static const SDL_Color CUT_BLUE_TINT = {20, 40, 70, 90};

/* The fade overlay used to live here; it now lives in fade.c so the
 * outro can share the same static alpha state.                         */

/* ---------------------------------------------------------------------
 * Snow particle seeding for panel 2.
 * --------------------------------------------------------------------- */
static void seed_snow(CutsceneState *c)
{
    for (int i = 0; i < CUT_SNOW_COUNT; i++) {
        c->snow[i].x     = (float)(rand() % W_ECRAN);
        c->snow[i].y     = (float)(rand() % H_ECRAN);
        c->snow[i].layer = i % 3;
    }
}

static void update_snow(CutsceneState *c, float dt)
{
    static const float vy[3] = {  60.0f, 100.0f, 160.0f };
    static const float vx[3] = { -20.0f, -35.0f, -55.0f };
    for (int i = 0; i < CUT_SNOW_COUNT; i++) {
        int L = c->snow[i].layer;
        c->snow[i].x += vx[L] * dt;
        c->snow[i].y += vy[L] * dt;
        if (c->snow[i].y >= H_ECRAN) {
            c->snow[i].y = -2.0f;
            c->snow[i].x = (float)(rand() % W_ECRAN);
        }
        if (c->snow[i].x < -4.0f) c->snow[i].x += W_ECRAN + 8.0f;
    }
}

static void render_snow(CutsceneState *c, SDL_Renderer *r)
{
    static const int sz[3] = {2, 3, 4};
    static const Uint8 br[3] = {180, 220, 255};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < CUT_SNOW_COUNT; i++) {
        int L = c->snow[i].layer;
        SDL_SetRenderDrawColor(r, br[L], br[L], br[L], 230);
        SDL_Rect d = {(int)c->snow[i].x, (int)c->snow[i].y, sz[L], sz[L]};
        SDL_RenderFillRect(r, &d);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* ---------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */
void cutscene_start(CutsceneState *c, SDL_Renderer *r, int wagonIndex)
{
    memset(c, 0, sizeof(*c));
    c->panel      = CUT_PANEL_LORE;
    c->panelTime  = 0.0f;
    c->wagonIndex = (wagonIndex < 0) ? 0
                  : (wagonIndex >= MAX_WAGONS ? MAX_WAGONS - 1 : wagonIndex);

    c->trainTex = IMG_LoadTexture(r, CUTSCENE_TRAIN_PATH);
    if (c->trainTex) {
        SDL_QueryTexture(c->trainTex, NULL, NULL,
                         &c->trainTexW, &c->trainTexH);
    } else {
        fprintf(stderr, "[cutscene] %s missing - using fallback backdrop\n",
                CUTSCENE_TRAIN_PATH);
        c->trainTexW = c->trainTexH = 0;
    }
    c->trainScrollX = 0.0f;

    seed_snow(c);

    /* start opaque-black and fade in into panel 1 */
    fade_reset();
    fade_from_black(0.6f);
}

void cutscene_skip(CutsceneState *c)
{
    if (c->skipping || c->panel == CUT_PANEL_DONE) return;
    c->skipping  = 1;
    c->panel     = CUT_PANEL_FADEIN;
    c->panelTime = 0.0f;
    fade_from_black(CUT_SKIP_FADEIN_DUR);
}

void cutscene_free(CutsceneState *c)
{
    if (c->trainTex) {
        SDL_DestroyTexture(c->trainTex);
        c->trainTex = NULL;
    }
    c->trainTexW = c->trainTexH = 0;
}

/* ---------------------------------------------------------------------
 * Panel advance helper - called by update when a panel's clock runs out.
 * --------------------------------------------------------------------- */
static void advance_panel(CutsceneState *c)
{
    c->panelTime = 0.0f;
    switch (c->panel) {
        case CUT_PANEL_LORE:
            c->panel = CUT_PANEL_TRAIN;
            fade_from_black(0.5f);
            break;
        case CUT_PANEL_TRAIN:
            c->panel = CUT_PANEL_TITLECARD;
            fade_from_black(0.4f);
            break;
        case CUT_PANEL_TITLECARD:
            c->panel = CUT_PANEL_FADEIN;
            fade_from_black(CUT_FADEIN_DUR);
            break;
        case CUT_PANEL_FADEIN:
            c->panel = CUT_PANEL_DONE;
            break;
        case CUT_PANEL_DONE: break;
    }
}

int cutscene_update(CutsceneState *c, float dt)
{
    if (c->panel == CUT_PANEL_DONE) return 1;

    c->panelTime += dt;
    fade_update(dt);

    switch (c->panel) {
        case CUT_PANEL_LORE: {
            /* fade-to-black during the last 0.4s of the panel */
            if (c->panelTime >= CUT_LORE_DUR - 0.4f && !fade_is_active()) {
                fade_to_black(0.4f);
            }
            if (c->panelTime >= CUT_LORE_DUR) advance_panel(c);
            break;
        }
        case CUT_PANEL_TRAIN: {
            c->trainScrollX += CUT_TRAIN_SCROLL_PX_S * dt;
            update_snow(c, dt);
            if (c->panelTime >= CUT_TRAIN_DUR - 0.5f && !fade_is_active()) {
                fade_to_black(0.5f);
            }
            if (c->panelTime >= CUT_TRAIN_DUR) advance_panel(c);
            break;
        }
        case CUT_PANEL_TITLECARD: {
            /* in: 0.5s, hold: 1.0s, out: 0.5s */
            if (c->panelTime >= CUT_TITLECARD_DUR - 0.5f && !fade_is_active()) {
                fade_to_black(0.5f);
            }
            if (c->panelTime >= CUT_TITLECARD_DUR) advance_panel(c);
            break;
        }
        case CUT_PANEL_FADEIN: {
            float dur = c->skipping ? CUT_SKIP_FADEIN_DUR : CUT_FADEIN_DUR;
            if (c->panelTime >= dur) advance_panel(c);
            break;
        }
        case CUT_PANEL_DONE: return 1;
    }

    return (c->panel == CUT_PANEL_DONE) ? 1 : 0;
}

/* ---------------------------------------------------------------------
 * Panel renderers
 * --------------------------------------------------------------------- */
static void clear_black(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
}

/* Line N appears at panelTime = N * gap and fades in over fadeDur. */
static Uint8 line_alpha(float panelTime, float startAt, float fadeDur)
{
    if (panelTime < startAt) return 0;
    float t = (panelTime - startAt) / fadeDur;
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return 255;
    return (Uint8)(t * 255.0f);
}

static void render_lore(CutsceneState *c, SDL_Renderer *r,
                        UIFontPack *fonts, UITextCache *cache)
{
    clear_black(r);

    SDL_Color cream = {235, 230, 215, 255};
    TTF_Font *font  = fonts->story ? fonts->story : fonts->fallback;
    if (!font) return;

    const float startTimes[3] = {0.0f, 0.8f, 1.6f};
    const int   ys[3]         = {H_ECRAN/2 - 50, H_ECRAN/2, H_ECRAN/2 + 50};
    const float fadeDur       = 0.6f;

    for (int i = 0; i < 3; i++) {
        Uint8 a = line_alpha(c->panelTime, startTimes[i], fadeDur);
        if (a == 0) continue;

        int tw, th;
        SDL_Texture *t = ui_text_get(cache, r, font, CUT_LORE_LINES[i],
                                     cream, &tw, &th);
        if (!t) continue;
        SDL_SetTextureAlphaMod(t, a);
        SDL_Rect dst = {W_ECRAN/2 - tw/2, ys[i] - th/2, tw, th};
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_SetTextureAlphaMod(t, 255);
    }
}

static void render_train(CutsceneState *c, SDL_Renderer *r)
{
    /* Train shot - scroll-loop the source rect across the texture.   */
    if (c->trainTex && c->trainTexW > 0 && c->trainTexH > 0) {
        float scroll = c->trainScrollX;
        if (c->trainTexW > 0)
            scroll = fmodf(scroll, (float)c->trainTexW);
        if (scroll < 0) scroll += c->trainTexW;

        int sx = (int)scroll;
        int remaining = c->trainTexW - sx;

        if (c->trainTexW >= W_ECRAN) {
            if (remaining >= W_ECRAN) {
                SDL_Rect src = {sx, 0, W_ECRAN, c->trainTexH};
                SDL_Rect dst = {0,  0, W_ECRAN, H_ECRAN};
                SDL_RenderCopy(r, c->trainTex, &src, &dst);
            } else {
                SDL_Rect src1 = {sx, 0, remaining, c->trainTexH};
                SDL_Rect dst1 = {0,  0, remaining, H_ECRAN};
                SDL_RenderCopy(r, c->trainTex, &src1, &dst1);
                SDL_Rect src2 = {0, 0, W_ECRAN - remaining, c->trainTexH};
                SDL_Rect dst2 = {remaining, 0, W_ECRAN - remaining, H_ECRAN};
                SDL_RenderCopy(r, c->trainTex, &src2, &dst2);
            }
        } else {
            /* Image narrower than screen - stretch full-screen.       */
            SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
            SDL_RenderCopy(r, c->trainTex, NULL, &full);
        }
    } else {
        /* Asset missing fallback - vertical gradient sky.            */
        for (int i = 0; i < H_ECRAN; i++) {
            float t = (float)i / (float)H_ECRAN;
            Uint8 rc = (Uint8)(10 + 25 * (1.0f - t));
            Uint8 gc = (Uint8)(20 + 40 * (1.0f - t));
            Uint8 bc = (Uint8)(45 + 70 * (1.0f - t));
            SDL_SetRenderDrawColor(r, rc, gc, bc, 255);
            SDL_RenderDrawLine(r, 0, i, W_ECRAN - 1, i);
        }
    }

    render_snow(c, r);

    /* Cold blue tint overlay. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, CUT_BLUE_TINT.r, CUT_BLUE_TINT.g,
                              CUT_BLUE_TINT.b, CUT_BLUE_TINT.a);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void render_titlecard(CutsceneState *c, SDL_Renderer *r,
                             UIFontPack *fonts, UITextCache *cache)
{
    clear_black(r);

    /* Local fade: ramp 0->255 over 0.5s, hold, then the shared overlay
     * fades it back out at the tail of the panel.                    */
    float t = c->panelTime / 0.5f;
    if (t > 1.0f) t = 1.0f;
    Uint8 a = (Uint8)(t * 255.0f);

    SDL_Color jaune = {255, 215,   0, 255};
    SDL_Color cream = {235, 230, 215, 255};

    TTF_Font *bigF = fonts->bebasTitle ? fonts->bebasTitle : fonts->fallback;
    TTF_Font *subF = fonts->story      ? fonts->story      : fonts->fallback;

    char top[32];
    snprintf(top, sizeof(top), "Wagon %d / %d",
             c->wagonIndex + 1, MAX_WAGONS);

    const char *sub = CUT_WAGON_NOMS[c->wagonIndex];

    if (bigF) {
        int tw, th;
        SDL_Texture *tex = ui_text_get(cache, r, bigF, top, jaune, &tw, &th);
        if (tex) {
            SDL_SetTextureAlphaMod(tex, a);
            SDL_Rect dst = {W_ECRAN/2 - tw/2, H_ECRAN/2 - th, tw, th};
            SDL_RenderCopy(r, tex, NULL, &dst);
            SDL_SetTextureAlphaMod(tex, 255);
        }
    }
    if (subF) {
        int tw, th;
        SDL_Texture *tex = ui_text_get(cache, r, subF, sub, cream, &tw, &th);
        if (tex) {
            SDL_SetTextureAlphaMod(tex, a);
            SDL_Rect dst = {W_ECRAN/2 - tw/2, H_ECRAN/2 + 20, tw, th};
            SDL_RenderCopy(r, tex, NULL, &dst);
            SDL_SetTextureAlphaMod(tex, 255);
        }
    }
}

void cutscene_render(CutsceneState *c, SDL_Renderer *r,
                     UIFontPack *fonts, UITextCache *cache)
{
    switch (c->panel) {
        case CUT_PANEL_LORE:      render_lore     (c, r, fonts, cache); break;
        case CUT_PANEL_TRAIN:     render_train    (c, r);                break;
        case CUT_PANEL_TITLECARD: render_titlecard(c, r, fonts, cache); break;
        case CUT_PANEL_FADEIN:    /* world is rendered by main.c */     break;
        case CUT_PANEL_DONE:      break;
    }
    fade_render(r);
}

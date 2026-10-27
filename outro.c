#include "outro.h"
#include "background.h"   /* W_ECRAN, H_ECRAN */
#include "fade.h"

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Asset paths.  Both intro and outro pull from assets/intro/ for the
 * shared backdrops; the post-win still lives under assets/outro/.   */
#define OUTRO_METAL_PATH   "assets/intro/metal_bg.png"
#define OUTRO_TRAIN_PATH   "assets/intro/train_cutscene.png"
#define OUTRO_END_BG_PATH  "assets/outro/end_bg.png"

/* Phase durations (seconds). */
#define OUTRO_IMPACT_DUR     1.5f
#define OUTRO_NARRATION_DUR  4.0f
#define OUTRO_TRAIN_DUR      3.0f

/* Phase 1 timing: flash 0..0.3, hold 0.3..1.0, fade-to-black 1.0..1.5 */
#define OUTRO_FLASH_DUR      0.3f
#define OUTRO_FREEZE_END     1.0f
#define OUTRO_IMPACT_FADE    0.5f

/* Phase 3 scrolls the train shot at half intro speed (40 px/s). */
#define OUTRO_TRAIN_SCROLL_PX_S 40.0f

/* Cold blue tint - matches intro panel 2 exactly. */
static const SDL_Color OUTRO_BLUE_TINT = {20, 40, 70, 90};

/* Narration lines for phase 2.  Line 3 is filled in at runtime with score. */
static const char *OUTRO_NARRATION_LINES[2] = {
    "Le Snowpiercer continue sa course.",
    "Mais le voyage est loin d'etre termine."
};

/* Win-screen button layout (matches the pause-menu style: centered pair). */
#define OUTRO_BTN_W   240
#define OUTRO_BTN_H   50
#define OUTRO_BTN_GAP 40
#define OUTRO_BTN_Y   (H_ECRAN - 90)

static void outro_button_layout(UIButton out[2])
{
    int totalW = 2 * OUTRO_BTN_W + OUTRO_BTN_GAP;
    int startX = (W_ECRAN - totalW) / 2;
    out[0] = (UIButton){{startX,                              OUTRO_BTN_Y,
                         OUTRO_BTN_W, OUTRO_BTN_H},          "REJOUER",        0, 1};
    out[1] = (UIButton){{startX + OUTRO_BTN_W + OUTRO_BTN_GAP, OUTRO_BTN_Y,
                         OUTRO_BTN_W, OUTRO_BTN_H},          "MENU PRINCIPAL", 1, 1};
}

/* ---------------------------------------------------------------------
 * Snow particles (same constants as intro panel 2).
 * --------------------------------------------------------------------- */
static void outro_seed_snow(OutroState *o)
{
    for (int i = 0; i < OUTRO_SNOW_COUNT; i++) {
        o->snow[i].x     = (float)(rand() % W_ECRAN);
        o->snow[i].y     = (float)(rand() % H_ECRAN);
        o->snow[i].layer = i % 3;
    }
}

static void outro_update_snow(OutroState *o, float dt)
{
    static const float vy[3] = {  60.0f, 100.0f, 160.0f };
    static const float vx[3] = { -20.0f, -35.0f, -55.0f };
    for (int i = 0; i < OUTRO_SNOW_COUNT; i++) {
        int L = o->snow[i].layer;
        o->snow[i].x += vx[L] * dt;
        o->snow[i].y += vy[L] * dt;
        if (o->snow[i].y >= H_ECRAN) {
            o->snow[i].y = -2.0f;
            o->snow[i].x = (float)(rand() % W_ECRAN);
        }
        if (o->snow[i].x < -4.0f) o->snow[i].x += W_ECRAN + 8.0f;
    }
}

static void outro_render_snow(OutroState *o, SDL_Renderer *r)
{
    static const int   sz[3] = {2, 3, 4};
    static const Uint8 br[3] = {180, 220, 255};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < OUTRO_SNOW_COUNT; i++) {
        int L = o->snow[i].layer;
        SDL_SetRenderDrawColor(r, br[L], br[L], br[L], 230);
        SDL_Rect d = {(int)o->snow[i].x, (int)o->snow[i].y, sz[L], sz[L]};
        SDL_RenderFillRect(r, &d);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* ---------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */
void outro_start(OutroState *o, SDL_Renderer *r,
                 int finalScore, int finalTimeSec, int enemiesKilled)
{
    memset(o, 0, sizeof(*o));
    o->phase         = OUTRO_PHASE_IMPACT;
    o->phaseTime     = 0.0f;
    o->finalScore    = finalScore;
    o->finalTimeSec  = finalTimeSec;
    o->enemiesKilled = enemiesKilled;
    o->flashAlpha    = 255.0f;
    o->winSelected   = 0;

    o->metalTex = IMG_LoadTexture(r, OUTRO_METAL_PATH);
    if (!o->metalTex)
        fprintf(stderr, "[outro] %s missing - using fallback bg\n",
                OUTRO_METAL_PATH);

    o->trainTex = IMG_LoadTexture(r, OUTRO_TRAIN_PATH);
    if (o->trainTex)
        SDL_QueryTexture(o->trainTex, NULL, NULL, &o->trainTexW, &o->trainTexH);
    else
        fprintf(stderr, "[outro] %s missing - using fallback bg\n",
                OUTRO_TRAIN_PATH);

    o->endBgTex = IMG_LoadTexture(r, OUTRO_END_BG_PATH);
    if (!o->endBgTex)
        fprintf(stderr, "[outro] %s missing - using fallback bg\n",
                OUTRO_END_BG_PATH);

    outro_seed_snow(o);

    /* Reset the shared fade overlay: phase 1 starts with a clean alpha. */
    fade_reset();
}

void outro_free(OutroState *o)
{
    if (o->metalTex) { SDL_DestroyTexture(o->metalTex); o->metalTex = NULL; }
    if (o->trainTex) { SDL_DestroyTexture(o->trainTex); o->trainTex = NULL; }
    if (o->endBgTex) { SDL_DestroyTexture(o->endBgTex); o->endBgTex = NULL; }
    o->trainTexW = o->trainTexH = 0;
}

int outro_should_render_world(const OutroState *o)
{
    return o && o->phase == OUTRO_PHASE_IMPACT;
}

/* ---------------------------------------------------------------------
 * Phase advancement
 * --------------------------------------------------------------------- */
static void outro_advance(OutroState *o)
{
    o->phaseTime = 0.0f;
    switch (o->phase) {
        case OUTRO_PHASE_IMPACT:
            o->phase = OUTRO_PHASE_NARRATION;
            fade_from_black(0.5f);
            break;
        case OUTRO_PHASE_NARRATION:
            o->phase = OUTRO_PHASE_TRAIN;
            fade_from_black(0.5f);
            break;
        case OUTRO_PHASE_TRAIN:
            o->phase = OUTRO_PHASE_WIN;
            fade_from_black(0.6f);
            break;
        case OUTRO_PHASE_WIN:
        case OUTRO_PHASE_DONE:
            break;
    }
}

/* Skip narration / train and jump straight to the win screen, fading
 * out the current panel so the transition isn't a hard cut.            */
static void outro_skip_to_win(OutroState *o)
{
    if (o->phase != OUTRO_PHASE_NARRATION && o->phase != OUTRO_PHASE_TRAIN)
        return;
    o->phase     = OUTRO_PHASE_WIN;
    o->phaseTime = 0.0f;
    fade_from_black(0.3f);
}

/* ---------------------------------------------------------------------
 * Update
 * --------------------------------------------------------------------- */
OutroAction outro_update(OutroState *o, float dt)
{
    o->phaseTime += dt;
    fade_update(dt);

    switch (o->phase) {
        case OUTRO_PHASE_IMPACT: {
            /* white flash decays over the first OUTRO_FLASH_DUR seconds */
            if (o->phaseTime <= OUTRO_FLASH_DUR) {
                float t = o->phaseTime / OUTRO_FLASH_DUR;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                o->flashAlpha = 255.0f * (1.0f - t);
            } else {
                o->flashAlpha = 0.0f;
            }
            /* fade-to-black during the last OUTRO_IMPACT_FADE seconds */
            if (o->phaseTime >= OUTRO_FREEZE_END && !fade_is_active())
                fade_to_black(OUTRO_IMPACT_FADE);
            if (o->phaseTime >= OUTRO_IMPACT_DUR) outro_advance(o);
            break;
        }
        case OUTRO_PHASE_NARRATION:
            if (o->phaseTime >= OUTRO_NARRATION_DUR - 0.5f && !fade_is_active())
                fade_to_black(0.5f);
            if (o->phaseTime >= OUTRO_NARRATION_DUR) outro_advance(o);
            break;

        case OUTRO_PHASE_TRAIN:
            o->trainScrollX += OUTRO_TRAIN_SCROLL_PX_S * dt;
            outro_update_snow(o, dt);
            if (o->phaseTime >= OUTRO_TRAIN_DUR - 0.5f && !fade_is_active())
                fade_to_black(0.5f);
            if (o->phaseTime >= OUTRO_TRAIN_DUR) outro_advance(o);
            break;

        case OUTRO_PHASE_WIN:
        case OUTRO_PHASE_DONE:
            break;
    }
    return OUTRO_ACTION_NONE;
}

/* ---------------------------------------------------------------------
 * Input
 * --------------------------------------------------------------------- */
OutroAction outro_handle_key(OutroState *o, SDL_Keycode key)
{
    /* Phase 1 is intentionally NEVER skippable - the impact needs to land. */
    if (o->phase == OUTRO_PHASE_IMPACT) return OUTRO_ACTION_NONE;

    if (o->phase == OUTRO_PHASE_NARRATION || o->phase == OUTRO_PHASE_TRAIN) {
        if (key == SDLK_SPACE || key == SDLK_RETURN) {
            outro_skip_to_win(o);
            return OUTRO_ACTION_CLICK;
        }
        return OUTRO_ACTION_NONE;
    }

    if (o->phase == OUTRO_PHASE_WIN) {
        if (key == SDLK_LEFT  || key == SDLK_a || key == SDLK_q) {
            if (o->winSelected != 0) { o->winSelected = 0; return OUTRO_ACTION_CLICK; }
        }
        if (key == SDLK_RIGHT || key == SDLK_d) {
            if (o->winSelected != 1) { o->winSelected = 1; return OUTRO_ACTION_CLICK; }
        }
        if (key == SDLK_UP    || key == SDLK_w ||
            key == SDLK_DOWN  || key == SDLK_s) {
            o->winSelected = !o->winSelected;
            return OUTRO_ACTION_CLICK;
        }
        if (key == SDLK_RETURN) {
            return (o->winSelected == 0) ? OUTRO_ACTION_REPLAY
                                         : OUTRO_ACTION_MENU;
        }
        if (key == SDLK_ESCAPE) {
            return OUTRO_ACTION_MENU;
        }
    }
    return OUTRO_ACTION_NONE;
}

OutroAction outro_handle_mouse_motion(OutroState *o, int lx, int ly)
{
    if (o->phase != OUTRO_PHASE_WIN) return OUTRO_ACTION_NONE;
    UIButton btns[2]; outro_button_layout(btns);
    SDL_Point p = {lx, ly};
    for (int i = 0; i < 2; i++) {
        if (SDL_PointInRect(&p, &btns[i].rect)) {
            if (o->winSelected != i) { o->winSelected = i; return OUTRO_ACTION_CLICK; }
            return OUTRO_ACTION_NONE;
        }
    }
    return OUTRO_ACTION_NONE;
}

OutroAction outro_handle_mouse_click(OutroState *o, int lx, int ly)
{
    if (o->phase != OUTRO_PHASE_WIN) return OUTRO_ACTION_NONE;
    UIButton btns[2]; outro_button_layout(btns);
    SDL_Point p = {lx, ly};
    for (int i = 0; i < 2; i++) {
        if (SDL_PointInRect(&p, &btns[i].rect)) {
            o->winSelected = i;
            return (i == 0) ? OUTRO_ACTION_REPLAY : OUTRO_ACTION_MENU;
        }
    }
    return OUTRO_ACTION_NONE;
}

/* ---------------------------------------------------------------------
 * Render helpers
 * --------------------------------------------------------------------- */
static void clear_color(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B)
{
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
}

static void draw_full_tex(SDL_Renderer *r, SDL_Texture *t)
{
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderCopy(r, t, NULL, &full);
}

/* Fade-in alpha for a line that starts at startAt and ramps over fadeDur. */
static Uint8 line_alpha(float panelTime, float startAt, float fadeDur)
{
    if (panelTime < startAt) return 0;
    float t = (panelTime - startAt) / fadeDur;
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return 255;
    return (Uint8)(t * 255.0f);
}

/* ---------------------------------------------------------------------
 * Phase renderers
 * --------------------------------------------------------------------- */
static void render_impact(OutroState *o, SDL_Renderer *r)
{
    /* The frozen wagon is already on the buffer (drawn by main.c).
     * We just stack the decaying white flash on top.                  */
    if (o->flashAlpha > 0.5f) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 255, 255, 255, (Uint8)o->flashAlpha);
        SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
        SDL_RenderFillRect(r, &full);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}

static void render_narration(OutroState *o, SDL_Renderer *r,
                             UIFontPack *fonts, UITextCache *cache)
{
    /* Dark metal bg (or a fallback charcoal gradient).                */
    if (o->metalTex) {
        draw_full_tex(r, o->metalTex);
    } else {
        for (int i = 0; i < H_ECRAN; i++) {
            float t = (float)i / (float)H_ECRAN;
            Uint8 g = (Uint8)(22 + 18 * (1.0f - t));
            SDL_SetRenderDrawColor(r, g, g, (Uint8)(g + 6), 255);
            SDL_RenderDrawLine(r, 0, i, W_ECRAN - 1, i);
        }
    }
    /* Dim the bg slightly so text stays readable. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 130);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    SDL_Color cream = {235, 230, 215, 255};
    TTF_Font *font  = fonts->story ? fonts->story : fonts->fallback;
    if (!font) return;

    const float startTimes[3] = {0.0f, 0.8f, 1.6f};
    const int   ys[3]         = {H_ECRAN/2 - 50, H_ECRAN/2, H_ECRAN/2 + 50};
    const float fadeDur       = 0.6f;

    char scoreLine[64];
    snprintf(scoreLine, sizeof(scoreLine),
             "Score final : %d", o->finalScore);
    const char *lines[3] = {
        OUTRO_NARRATION_LINES[0],
        OUTRO_NARRATION_LINES[1],
        scoreLine
    };

    for (int i = 0; i < 3; i++) {
        Uint8 a = line_alpha(o->phaseTime, startTimes[i], fadeDur);
        if (a == 0) continue;
        int tw, th;
        SDL_Texture *t = ui_text_get(cache, r, font, lines[i],
                                     cream, &tw, &th);
        if (!t) continue;
        SDL_SetTextureAlphaMod(t, a);
        SDL_Rect dst = {W_ECRAN/2 - tw/2, ys[i] - th/2, tw, th};
        SDL_RenderCopy(r, t, NULL, &dst);
        SDL_SetTextureAlphaMod(t, 255);
    }
}

static void render_train_shot(OutroState *o, SDL_Renderer *r)
{
    if (o->trainTex && o->trainTexW > 0 && o->trainTexH > 0) {
        float scroll = fmodf(o->trainScrollX, (float)o->trainTexW);
        if (scroll < 0) scroll += o->trainTexW;
        int sx = (int)scroll;
        int remaining = o->trainTexW - sx;

        if (o->trainTexW >= W_ECRAN) {
            if (remaining >= W_ECRAN) {
                SDL_Rect src = {sx, 0, W_ECRAN, o->trainTexH};
                SDL_Rect dst = {0,  0, W_ECRAN, H_ECRAN};
                SDL_RenderCopy(r, o->trainTex, &src, &dst);
            } else {
                SDL_Rect src1 = {sx, 0, remaining, o->trainTexH};
                SDL_Rect dst1 = {0,  0, remaining, H_ECRAN};
                SDL_RenderCopy(r, o->trainTex, &src1, &dst1);
                SDL_Rect src2 = {0, 0, W_ECRAN - remaining, o->trainTexH};
                SDL_Rect dst2 = {remaining, 0, W_ECRAN - remaining, H_ECRAN};
                SDL_RenderCopy(r, o->trainTex, &src2, &dst2);
            }
        } else {
            draw_full_tex(r, o->trainTex);
        }
    } else {
        for (int i = 0; i < H_ECRAN; i++) {
            float t = (float)i / (float)H_ECRAN;
            Uint8 rc = (Uint8)(10 + 25 * (1.0f - t));
            Uint8 gc = (Uint8)(20 + 40 * (1.0f - t));
            Uint8 bc = (Uint8)(45 + 70 * (1.0f - t));
            SDL_SetRenderDrawColor(r, rc, gc, bc, 255);
            SDL_RenderDrawLine(r, 0, i, W_ECRAN - 1, i);
        }
    }

    outro_render_snow(o, r);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, OUTRO_BLUE_TINT.r, OUTRO_BLUE_TINT.g,
                              OUTRO_BLUE_TINT.b, OUTRO_BLUE_TINT.a);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void render_win(OutroState *o, SDL_Renderer *r,
                       UIFontPack *fonts, UITextCache *cache, UIAssets *assets)
{
    /* end_bg.png full screen (with fallback). */
    if (o->endBgTex) {
        draw_full_tex(r, o->endBgTex);
    } else {
        for (int i = 0; i < H_ECRAN; i++) {
            float t = (float)i / (float)H_ECRAN;
            Uint8 rc = (Uint8)(8  + 30 * (1.0f - t));
            Uint8 gc = (Uint8)(12 + 35 * (1.0f - t));
            Uint8 bc = (Uint8)(22 + 60 * (1.0f - t));
            SDL_SetRenderDrawColor(r, rc, gc, bc, 255);
            SDL_RenderDrawLine(r, 0, i, W_ECRAN - 1, i);
        }
    }

    /* Dark semi-transparent center panel. */
    SDL_Rect panel = {W_ECRAN/2 - 320, 50, 640, H_ECRAN - 160};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 100, 180, 255, 200);
    SDL_RenderDrawRect(r, &panel);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Match existing ETAT_ENDING palette so the visual language stays
     * consistent with the rest of the menu / HUD.                      */
    SDL_Color jauneE = {255, 215,   0, 255};
    SDL_Color blancE = {235, 235, 235, 255};
    SDL_Color grisE  = {180, 190, 210, 255};

    /* 1) Game title small at top. */
    if (fonts->novaLabel) {
        ui_draw_text_centered(cache, r, fonts->novaLabel,
            "SNOWPIERCER", grisE, W_ECRAN/2, 70);
    }

    /* 2) Large centered win text. */
    ui_draw_text_centered(cache, r,
        fonts->bebasTitle ? fonts->bebasTitle : fonts->fallback,
        "VOUS AVEZ SURVECU", jauneE, W_ECRAN/2, 130);

    /* 3) Stats - reuse the ETAT_ENDING line format. */
    char buf[64];
    snprintf(buf, sizeof(buf), "Score final : %d", o->finalScore);
    ui_draw_text_centered(cache, r, fonts->story, buf, blancE, W_ECRAN/2, 215);

    snprintf(buf, sizeof(buf), "Temps total : %02d:%02d",
             o->finalTimeSec / 60, o->finalTimeSec % 60);
    ui_draw_text_centered(cache, r, fonts->story, buf, blancE, W_ECRAN/2, 250);

    snprintf(buf, sizeof(buf), "Ennemis vaincus : %d", o->enemiesKilled);
    ui_draw_text_centered(cache, r, fonts->story, buf, blancE, W_ECRAN/2, 285);

    /* 4) Buttons - reuse the project's button widget. */
    UIButton btns[2]; outro_button_layout(btns);
    UIButtonState dummy = {0};
    dummy.hoveredId   = o->winSelected;
    dummy.pressedId   = -1;
    dummy.activatedId = -1;
    ui_button_draw_all(r, assets->buttonTex, btns, 2,
                       &dummy, o->winSelected, cache, fonts->novaButton);

    ui_draw_text_centered(cache, r, fonts->mono,
        "[</>] Naviguer  [ENTREE] Valider  [ECHAP] Menu",
        grisE, W_ECRAN/2, H_ECRAN - 25);
}

void outro_render(OutroState *o, SDL_Renderer *r,
                  UIFontPack *fonts, UITextCache *cache, UIAssets *assets)
{
    switch (o->phase) {
        case OUTRO_PHASE_IMPACT:    render_impact   (o, r);                       break;
        case OUTRO_PHASE_NARRATION: render_narration(o, r, fonts, cache);         break;
        case OUTRO_PHASE_TRAIN:     render_train_shot(o, r);                       break;
        case OUTRO_PHASE_WIN:       render_win      (o, r, fonts, cache, assets); break;
        case OUTRO_PHASE_DONE:                                                    break;
    }
    /* The shared black fade overlays everything but the persistent win
     * screen - on the win screen we want the panel fully readable.    */
    if (o->phase != OUTRO_PHASE_WIN || fade_is_active())
        fade_render(r);
}

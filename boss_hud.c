/* ----------------------------------------------------------------------------
 * boss_hud — implementation.
 *
 * Layout (centered top, 40% screen width):
 *
 *   ┌──────────────────────────────────────┐
 *   │  [PORTRAIT]  BOSS NAME               │
 *   │              ████████████░░░░░░░░░░  │
 *   │              |seg1|seg2|seg3|seg4|seg5|
 *   └──────────────────────────────────────┘
 *
 * Slide in: lerp slide_offset_y from -panel_h to 0 over 0.5s when show().
 * Slide out: same lerp reversed on hide(), but first holds 0.5s at 0 HP
 *            for the full-bar white flash before sliding up.
 *
 * Segment-break detection: each frame compares floor(current_hp_visual /
 * (max_hp / 5)) to the previous frame's value; on a downward step the
 * corresponding segment's flash timer is armed, a screen shake fires, and
 * a spark burst is queued at the break point's world position (top of
 * the HUD bar at the segment boundary).
 *
 * Communication: the public API is read-only against the boss HP (the
 * actual_boss_hp parameter is the only HP path in/out).  boss_anim is
 * not touched — both modules are driven by the same caller through
 * matching sync-point trigger pairs.
 * -------------------------------------------------------------------------- */

#include "boss_hud.h"
#include "boss_anim.h"       /* boss_anim_set / get_state for segment-break rage */

#include "background.h"      /* W_ECRAN / H_ECRAN */
#include "gameplay.h"        /* gameplay_trigger_shake, SHAKE_HIT_AMP    */
#include "effects.h"         /* effects_spawn_sparks                      */

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- tuning -------------------------------------------------------------- */
#define BH_PANEL_W_PCT          0.40f
#define BH_PANEL_H              78
#define BH_PANEL_TOP_Y          12
#define BH_PANEL_PADDING        10
#define BH_PORTRAIT_INSET       8
#define BH_NAME_FONT_HEIGHT     24
#define BH_BAR_HEIGHT           18

#define BH_SLIDE_DURATION       0.50f
#define BH_HP_LERP_RATE         6.0f      /* per second; higher = snappier  */
#define BH_SEG_FLASH_DURATION   0.30f
#define BH_NAME_PULSE_DURATION  0.30f
#define BH_NAME_PULSE_PEAK      1.20f
#define BH_BORDER_FLASH_PERIOD  0.50f
#define BH_DEATH_HOLD           0.50f     /* white-flash hold before slide-up */
#define BH_DEATH_SLIDE          0.50f     /* slide-out duration after death  */

/* ---- singleton ---------------------------------------------------------- */
static BossHUD g_hud;

/* ---- hitstop ------------------------------------------------------------ */
/* Tiny module-static timer; main.c gates the world-sim block on the
 * accessor below and ticks the timer every frame outside that gate. */
static float g_hitstop_timer = 0.0f;

void hitstop_trigger(float duration)
{
    if (duration <= 0.0f) return;
    /* Take the longer of the new request and what's already running so
     * a back-to-back trigger doesn't shorten an in-flight freeze. */
    if (duration > g_hitstop_timer) g_hitstop_timer = duration;
}

int hitstop_active(void)
{
    return (g_hitstop_timer > 0.0f) ? 1 : 0;
}

void hitstop_tick(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    g_hitstop_timer -= dt;
    if (g_hitstop_timer < 0.0f) g_hitstop_timer = 0.0f;
}

/* ---- portrait sampling --------------------------------------------------- */
/* Crop a BOSS_HUD_PORTRAIT_SIZE x BOSS_HUD_PORTRAIT_SIZE square from
 * Row 1 Frame 1 of the supplied sheet, into a freshly created
 * SDL_TEXTUREACCESS_TARGET texture.  Returns NULL on any failure (caller
 * falls back to a solid accent-color rect). */
static SDL_Texture *bh_sample_portrait(SDL_Renderer *r, SDL_Texture *sheet)
{
    if (!r || !sheet) return NULL;

    int sw = 0, sh = 0;
    if (SDL_QueryTexture(sheet, NULL, NULL, &sw, &sh) != 0) return NULL;
    if (sw <= 0 || sh <= 0) return NULL;

    /* Row 1 Frame 1 = top-left cell.  Cell size is sheet_w / max_cols x
     * sheet_h / 7 rows, matching boss_anim's layout.  Max cols = 6
     * (CHARGE row) from the spec.  We crop the centered 48x48 region of
     * that cell so the portrait shows the boss's torso/face, not the full
     * frame at small scale. */
    const int max_cols = 6;
    const int rows     = 7;
    int cell_w = sw / max_cols;
    int cell_h = sh / rows;
    if (cell_w <= 0 || cell_h <= 0) return NULL;

    SDL_Texture *out = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         BOSS_HUD_PORTRAIT_SIZE,
                                         BOSS_HUD_PORTRAIT_SIZE);
    if (!out) {
        fprintf(stderr, "[boss_hud] portrait CreateTexture failed: %s\n",
                SDL_GetError());
        return NULL;
    }
    SDL_SetTextureBlendMode(out, SDL_BLENDMODE_BLEND);

    /* Source = the first cell of row 1, centered crop. */
    int src_size = (cell_w < cell_h) ? cell_w : cell_h;
    int src_x    = (cell_w - src_size) / 2;
    int src_y    = (cell_h - src_size) / 2;
    SDL_Rect src = { src_x, src_y, src_size, src_size };
    SDL_Rect dst = { 0, 0, BOSS_HUD_PORTRAIT_SIZE, BOSS_HUD_PORTRAIT_SIZE };

    SDL_Texture *prev_target = SDL_GetRenderTarget(r);
    if (SDL_SetRenderTarget(r, out) != 0) {
        fprintf(stderr, "[boss_hud] SetRenderTarget(portrait): %s\n",
                SDL_GetError());
        SDL_DestroyTexture(out);
        return NULL;
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, sheet, &src, &dst);
    SDL_SetRenderTarget(r, prev_target);

    return out;
}

/* ---- lifecycle ----------------------------------------------------------- */
void boss_hud_init(SDL_Renderer *r,
                   UIFontPack *fonts, UITextCache *cache,
                   GameplayState *gp,  EffectsSystem *fx,
                   SDL_Texture *boss_sheet,
                   int boss_max_hp,
                   const char *boss_name)
{
    memset(&g_hud, 0, sizeof(g_hud));

    /* Palette mirrors the gameplay HUD: amber accent {235,180,60},
     * red {220,60,60}, deep-red bar fill (180,30,30) with (220,60,60)
     * top-edge highlight, dark grey (40,40,45) empties, panel bg
     * (0,0,0,160), stagger flash red (200,40,40). */
    SDL_Color accent       = {235, 180,  60, 255};
    SDL_Color panel_bg     = {  0,   0,   0, 160};
    SDL_Color bar_fill     = {180,  30,  30, 255};
    SDL_Color bar_fill_hi  = {220,  60,  60, 255};
    SDL_Color bar_empty    = { 40,  40,  45, 255};
    SDL_Color stagger_red  = {200,  40,  40, 255};
    SDL_Color name_c       = {235, 180,  60, 255};

    g_hud.accent      = accent;
    g_hud.panel_bg    = panel_bg;
    g_hud.bar_fill    = bar_fill;
    g_hud.bar_fill_hi = bar_fill_hi;
    g_hud.bar_empty   = bar_empty;
    g_hud.stagger_red = stagger_red;
    g_hud.name_color  = name_c;

    g_hud.renderer  = r;
    g_hud.fonts     = fonts;
    g_hud.cache     = cache;
    g_hud.gp        = gp;
    g_hud.fx        = fx;
    g_hud.boss_name = boss_name ? boss_name : "WILFORD'S ENFORCER";

    g_hud.max_hp            = boss_max_hp > 0 ? boss_max_hp : 1;
    g_hud.last_hp           = g_hud.max_hp;
    g_hud.current_hp_visual = (float)g_hud.max_hp;
    g_hud.segments_broken   = 0;

    g_hud.is_visible    = 0;
    g_hud.sliding_out   = 0;
    g_hud.slide_offset_y = -(float)(BH_PANEL_H + BH_PANEL_TOP_Y + 20);
    g_hud.slide_target_y = g_hud.slide_offset_y;

    g_hud.portrait    = bh_sample_portrait(r, boss_sheet);
    g_hud.portrait_ok = (g_hud.portrait != NULL);
}

void boss_hud_free(void)
{
    if (g_hud.portrait) SDL_DestroyTexture(g_hud.portrait);
    g_hud.portrait    = NULL;
    g_hud.portrait_ok = 0;
}

void boss_hud_reset(int boss_max_hp)
{
    /* Keep externally-bound pointers + portrait; reset live state. */
    SDL_Texture   *keepPortrait = g_hud.portrait;
    int            keepPortraitOk = g_hud.portrait_ok;
    SDL_Renderer  *kr  = g_hud.renderer;
    UIFontPack    *kf  = g_hud.fonts;
    UITextCache   *kc  = g_hud.cache;
    GameplayState *kgp = g_hud.gp;
    EffectsSystem *kfx = g_hud.fx;
    const char    *kn  = g_hud.boss_name;
    SDL_Color a = g_hud.accent, p = g_hud.panel_bg, bf = g_hud.bar_fill,
              bh = g_hud.bar_fill_hi, be = g_hud.bar_empty,
              sr = g_hud.stagger_red, nc = g_hud.name_color;

    memset(&g_hud, 0, sizeof(g_hud));
    g_hud.portrait    = keepPortrait;
    g_hud.portrait_ok = keepPortraitOk;
    g_hud.renderer    = kr; g_hud.fonts = kf; g_hud.cache = kc;
    g_hud.gp          = kgp; g_hud.fx   = kfx; g_hud.boss_name = kn;
    g_hud.accent      = a; g_hud.panel_bg = p; g_hud.bar_fill = bf;
    g_hud.bar_fill_hi = bh; g_hud.bar_empty = be;
    g_hud.stagger_red = sr; g_hud.name_color = nc;

    g_hud.max_hp            = boss_max_hp > 0 ? boss_max_hp : 1;
    g_hud.last_hp           = g_hud.max_hp;
    g_hud.current_hp_visual = (float)g_hud.max_hp;
    g_hud.slide_offset_y    = -(float)(BH_PANEL_H + BH_PANEL_TOP_Y + 20);
    g_hud.slide_target_y    = g_hud.slide_offset_y;
}

/* ---- show/hide ---------------------------------------------------------- */
void boss_hud_show(void)
{
    if (g_hud.is_visible && !g_hud.sliding_out) return;
    g_hud.is_visible     = 1;
    g_hud.sliding_out    = 0;
    g_hud.slide_target_y = 0.0f;            /* rest position */
}

void boss_hud_hide(void)
{
    /* Start the death sequence: full-bar white flash for BH_DEATH_HOLD,
     * then slide back up.  The flag drives boss_hud_render to paint the
     * bar white during the hold.  Callers should let the existing boss
     * death sequence run AFTER the slide-out finishes. */
    if (!g_hud.is_visible) return;
    if (g_hud.dying)       return;
    g_hud.dying          = 1;
    g_hud.dying_timer    = 0.0f;
    g_hud.sliding_out    = 0;     /* held during death flash, slides after */
}

int boss_hud_is_visible(void) { return g_hud.is_visible; }

int boss_hud_is_finished_hiding(void)
{
    return g_hud.dying && g_hud.sliding_out &&
           (g_hud.slide_offset_y <=
            -(float)(BH_PANEL_H + BH_PANEL_TOP_Y + 4));
}

/* ---- sync-point triggers ----------------------------------------------- */
void boss_hud_pulse_name(void)
{
    if (g_hud.name_pulse_active) return;
    g_hud.name_pulse_active = 1;
    g_hud.name_pulse_timer  = 0.0f;
}

void boss_hud_flash_border(void)
{
    if (g_hud.border_flash_active) return;
    g_hud.border_flash_active = 1;
    g_hud.border_flash_timer  = 0.0f;
}

/* ---- update ------------------------------------------------------------- */
void boss_hud_update(float dt, int actual_boss_hp)
{
    if (!g_hud.is_visible) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;

    /* Slide: lerp the offset toward the target.  BH_SLIDE_DURATION is the
     * total time, so step = (target - current) * (dt / DURATION) clamped
     * to never overshoot. */
    if (!g_hud.dying || g_hud.sliding_out) {
        float diff = g_hud.slide_target_y - g_hud.slide_offset_y;
        float step = diff * (dt / BH_SLIDE_DURATION);
        if (fabsf(step) > fabsf(diff)) step = diff;
        g_hud.slide_offset_y += step;
    }

    /* Lerp the visual HP toward the actual HP.  We never let the visual
     * dip below 0 or above max. */
    if (actual_boss_hp < 0)             actual_boss_hp = 0;
    if (actual_boss_hp > g_hud.max_hp)  actual_boss_hp = g_hud.max_hp;

    float target = (float)actual_boss_hp;
    float diff   = target - g_hud.current_hp_visual;
    /* Exponential decay: visual approaches target.  Frame-rate independent. */
    g_hud.current_hp_visual += diff * (1.0f - expf(-BH_HP_LERP_RATE * dt));
    if (g_hud.current_hp_visual < 0.0f)              g_hud.current_hp_visual = 0.0f;
    if (g_hud.current_hp_visual > (float)g_hud.max_hp)
        g_hud.current_hp_visual = (float)g_hud.max_hp;

    /* Segment-break detection.  Each segment = max_hp / 5.  Count broken
     * segments as floor((max_hp - current_visual) / seg_size).  When that
     * crosses upward by 1, fire the white flash + screen shake + spark
     * burst for the just-broken segment. */
    float seg_size  = (float)g_hud.max_hp / (float)BOSS_HUD_NUM_SEGMENTS;
    if (seg_size <= 0.0f) seg_size = 1.0f;
    int   new_broken = (int)floorf(((float)g_hud.max_hp - g_hud.current_hp_visual) / seg_size);
    if (new_broken > BOSS_HUD_NUM_SEGMENTS) new_broken = BOSS_HUD_NUM_SEGMENTS;

    while (g_hud.segments_broken < new_broken) {
        int idx = g_hud.segments_broken;
        if (idx >= 0 && idx < BOSS_HUD_NUM_SEGMENTS)
            g_hud.segment_flash_timers[idx] = BH_SEG_FLASH_DURATION;
        g_hud.segments_broken++;

        /* Reuse the existing screen shake. */
        if (g_hud.gp) gameplay_trigger_shake(g_hud.gp, SHAKE_HIT_AMP, 0.18f);

        /* Combat-feel: freeze the world for 0.6s so the break lands hard.
         * The shake above keeps ticking (main.c runs gameplay_update_shake
         * outside the hitstop gate). */
        hitstop_trigger(0.6f);

        /* Segment break feedback is just VFX (hitstop + shake + sparks +
         * segment flash) here.  The boss's *animation* response to each
         * threshold (HURT / RAGE / GROUND_SLAM) is driven by main.c
         * through the boss_ai_event_* hooks so it isn't duplicated. */

        /* Spark burst at the broken segment's bar position.  We synthesize
         * a world-space point from the segment center on the panel; the
         * existing effects_spawn_sparks paints them in screen-space
         * coords so we just pass the screen-space center directly.  The
         * spark system uses (x, y) as initial positions and adds random
         * velocities, so the burst reads as "particles erupting from the
         * broken section of the bar" regardless of camera. */
        if (g_hud.fx) {
            int panel_w = (int)((float)W_ECRAN * BH_PANEL_W_PCT);
            int panel_x = (W_ECRAN - panel_w) / 2;
            int bar_x   = panel_x + BH_PORTRAIT_INSET + BOSS_HUD_PORTRAIT_SIZE
                          + BH_PANEL_PADDING;
            int bar_w   = panel_w - (bar_x - panel_x) - BH_PANEL_PADDING;
            int seg_w   = bar_w / BOSS_HUD_NUM_SEGMENTS;
            /* Break point = the right edge of the just-broken segment
             * (counting from the right side of the bar). */
            int break_seg_from_right = idx;     /* 0 = rightmost segment */
            int break_x = bar_x + bar_w - (break_seg_from_right + 1) * seg_w
                          + seg_w / 2;
            int break_y = BH_PANEL_TOP_Y + BH_PANEL_PADDING
                          + BH_NAME_FONT_HEIGHT + BH_BAR_HEIGHT / 2;
            effects_spawn_sparks(g_hud.fx, (float)break_x, (float)break_y, 8);
        }
    }

    /* Advance segment flash timers. */
    for (int i = 0; i < BOSS_HUD_NUM_SEGMENTS; i++) {
        if (g_hud.segment_flash_timers[i] > 0.0f) {
            g_hud.segment_flash_timers[i] -= dt;
            if (g_hud.segment_flash_timers[i] < 0.0f)
                g_hud.segment_flash_timers[i] = 0.0f;
        }
    }

    /* Name pulse. */
    if (g_hud.name_pulse_active) {
        g_hud.name_pulse_timer += dt;
        if (g_hud.name_pulse_timer >= BH_NAME_PULSE_DURATION) {
            g_hud.name_pulse_active = 0;
            g_hud.name_pulse_timer  = 0.0f;
        }
    }

    /* Border flash cycle.  Once armed, runs forever (stagger is permanent). */
    if (g_hud.border_flash_active)
        g_hud.border_flash_timer += dt;

    /* Death sequence: hold for BH_DEATH_HOLD (full-bar white flash drawn
     * by render), then start sliding out. */
    if (g_hud.dying) {
        g_hud.dying_timer += dt;
        if (g_hud.dying_timer >= BH_DEATH_HOLD && !g_hud.sliding_out) {
            g_hud.sliding_out    = 1;
            g_hud.slide_target_y = -(float)(BH_PANEL_H + BH_PANEL_TOP_Y + 20);
        }
    }

    g_hud.last_hp = actual_boss_hp;
}

/* ---- render -------------------------------------------------------------- */
static void bh_draw_filled(SDL_Renderer *r, SDL_Rect rc, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}

static void bh_draw_outline(SDL_Renderer *r, SDL_Rect rc, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rc);
}

void boss_hud_render(void)
{
    if (!g_hud.is_visible)  return;
    if (!g_hud.renderer)    return;

    /* Hide entirely once the slide-out has retreated off-screen. */
    if (g_hud.dying && g_hud.sliding_out &&
        g_hud.slide_offset_y <= -(float)(BH_PANEL_H + BH_PANEL_TOP_Y + 4))
    {
        g_hud.is_visible = 0;
        return;
    }

    SDL_Renderer *r = g_hud.renderer;

    int panel_w = (int)((float)W_ECRAN * BH_PANEL_W_PCT);
    if (panel_w < 280) panel_w = 280;
    int panel_x = (W_ECRAN - panel_w) / 2;
    int panel_y = BH_PANEL_TOP_Y + (int)g_hud.slide_offset_y;

    /* Background panel. */
    SDL_Rect bg = { panel_x, panel_y, panel_w, BH_PANEL_H };
    bh_draw_filled(r, bg, g_hud.panel_bg);

    /* Border — flashes between accent and stagger_red during stagger. */
    SDL_Color border = g_hud.accent;
    if (g_hud.border_flash_active) {
        /* 0.5s cycle, half on accent, half on stagger_red. */
        float t = fmodf(g_hud.border_flash_timer, BH_BORDER_FLASH_PERIOD);
        if (t > BH_BORDER_FLASH_PERIOD * 0.5f) border = g_hud.stagger_red;
    }
    bh_draw_outline(r, bg, border);

    /* Portrait box. */
    SDL_Rect prt = { panel_x + BH_PORTRAIT_INSET,
                     panel_y + BH_PORTRAIT_INSET,
                     BOSS_HUD_PORTRAIT_SIZE, BOSS_HUD_PORTRAIT_SIZE };
    if (g_hud.portrait_ok && g_hud.portrait) {
        SDL_RenderCopy(r, g_hud.portrait, NULL, &prt);
    } else {
        bh_draw_filled(r, prt, g_hud.accent);
    }
    bh_draw_outline(r, prt, g_hud.accent);

    /* Boss name above the bar.  Pulse scales it from 1.0 to 1.2x and back
     * over BH_NAME_PULSE_DURATION using a sine envelope. */
    float scale = 1.0f;
    if (g_hud.name_pulse_active) {
        float t   = g_hud.name_pulse_timer / BH_NAME_PULSE_DURATION;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        /* sin(pi * t) peaks at 1.0 in the middle, 0 at the ends */
        float envelope = sinf((float)M_PI * t);
        scale = 1.0f + (BH_NAME_PULSE_PEAK - 1.0f) * envelope;
    }

    if (g_hud.fonts && g_hud.cache) {
        TTF_Font *f = g_hud.fonts->bebasHeader
                        ? g_hud.fonts->bebasHeader
                        : (g_hud.fonts->bebasTitle
                            ? g_hud.fonts->bebasTitle
                            : g_hud.fonts->fallback);
        if (f) {
            int tw = 0, th = 0;
            SDL_Texture *tex = ui_text_get(g_hud.cache, r, f,
                                           g_hud.boss_name,
                                           g_hud.name_color, &tw, &th);
            if (tex) {
                int dw = (int)((float)tw * scale);
                int dh = (int)((float)th * scale);
                int dx = prt.x + prt.w + BH_PANEL_PADDING;
                int dy = panel_y + BH_PORTRAIT_INSET
                       + (BH_NAME_FONT_HEIGHT - dh) / 2;
                if (dy < panel_y + BH_PORTRAIT_INSET - 4)
                    dy = panel_y + BH_PORTRAIT_INSET - 4;
                SDL_Rect dst = { dx, dy, dw, dh };
                SDL_RenderCopy(r, tex, NULL, &dst);
            }
        }
    }

    /* HP bar.  bar_w = panel_w minus the portrait + paddings. */
    int bar_x = prt.x + prt.w + BH_PANEL_PADDING;
    int bar_y = panel_y + BH_PORTRAIT_INSET + BH_NAME_FONT_HEIGHT + 6;
    int bar_w = panel_w - (bar_x - panel_x) - BH_PANEL_PADDING;
    if (bar_w < 40) bar_w = 40;
    int bar_h = BH_BAR_HEIGHT;

    /* Death flash: paint the entire bar white during the BH_DEATH_HOLD
     * window before the slide-out begins. */
    int death_white = (g_hud.dying && !g_hud.sliding_out &&
                       g_hud.dying_timer < BH_DEATH_HOLD);

    /* Background = bar_empty (dark grey).  Each segment is painted
     * individually so segment-flash white overrides correctly. */
    int seg_w = bar_w / BOSS_HUD_NUM_SEGMENTS;

    /* Bar fill width based on lerped visual HP. */
    float fill_pct = (g_hud.max_hp > 0)
                     ? (g_hud.current_hp_visual / (float)g_hud.max_hp) : 0.0f;
    if (fill_pct < 0.0f) fill_pct = 0.0f;
    if (fill_pct > 1.0f) fill_pct = 1.0f;
    int fill_px = (int)((float)bar_w * fill_pct);

    /* Paint each segment.
     * Segment i covers x in [bar_x + i*seg_w, bar_x + (i+1)*seg_w).
     * It's:
     *  - white if death_white OR its flash timer > 0
     *  - bar_empty if i >= (BOSS_HUD_NUM_SEGMENTS - segments_broken)
     *  - bar_fill if fully filled
     *  - partially filled if the fill_px crosses through it
     */
    for (int i = 0; i < BOSS_HUD_NUM_SEGMENTS; i++) {
        int sx  = bar_x + i * seg_w;
        int sw  = (i == BOSS_HUD_NUM_SEGMENTS - 1) ? (bar_x + bar_w - sx) : seg_w;
        SDL_Rect seg = { sx, bar_y, sw, bar_h };

        /* Permanently dark segments at the right end. */
        int from_right_idx = BOSS_HUD_NUM_SEGMENTS - 1 - i;
        int permanently_broken = (from_right_idx < g_hud.segments_broken);

        /* Compute the filled portion of THIS segment from the lerped HP. */
        int seg_start_px = i * seg_w;
        int seg_end_px   = seg_start_px + sw;
        int filled_in_seg = 0;
        if (fill_px > seg_start_px)
            filled_in_seg = (fill_px < seg_end_px ? fill_px - seg_start_px : sw);

        if (death_white) {
            SDL_Color white = {255, 255, 255, 255};
            bh_draw_filled(r, seg, white);
        }
        else if (g_hud.segment_flash_timers[i] > 0.0f) {
            float a01 = g_hud.segment_flash_timers[i] / BH_SEG_FLASH_DURATION;
            if (a01 > 1.0f) a01 = 1.0f;
            SDL_Color white = {255, 255, 255, (Uint8)(255.0f * a01)};
            /* Draw the underlying color first (broken-dark or partial fill)
             * then overlay the white flash. */
            bh_draw_filled(r, seg, g_hud.bar_empty);
            if (filled_in_seg > 0 && !permanently_broken) {
                SDL_Rect f = { sx, bar_y, filled_in_seg, bar_h };
                bh_draw_filled(r, f, g_hud.bar_fill);
            }
            bh_draw_filled(r, seg, white);
        }
        else if (permanently_broken) {
            bh_draw_filled(r, seg, g_hud.bar_empty);
        }
        else {
            bh_draw_filled(r, seg, g_hud.bar_empty);
            if (filled_in_seg > 0) {
                SDL_Rect f = { sx, bar_y, filled_in_seg, bar_h };
                bh_draw_filled(r, f, g_hud.bar_fill);
                /* Highlight line along the top edge. */
                SDL_SetRenderDrawColor(r, g_hud.bar_fill_hi.r,
                                          g_hud.bar_fill_hi.g,
                                          g_hud.bar_fill_hi.b, 255);
                SDL_RenderDrawLine(r, sx, bar_y, sx + filled_in_seg - 1, bar_y);
            }
        }
    }

    /* Segment dividers — thin dark lines between segments.  Drawn last so
     * they sit on top of the fill / flash. */
    SDL_SetRenderDrawColor(r, 8, 8, 12, 230);
    for (int i = 1; i < BOSS_HUD_NUM_SEGMENTS; i++) {
        int dx = bar_x + i * seg_w;
        SDL_RenderDrawLine(r, dx, bar_y, dx, bar_y + bar_h - 1);
    }

    /* Outer bar border for definition. */
    SDL_Rect br = { bar_x, bar_y, bar_w, bar_h };
    bh_draw_outline(r, br, border);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

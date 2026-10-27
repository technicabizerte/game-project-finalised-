/* ----------------------------------------------------------------------------
 * boss_anim — implementation.
 *
 * Per-animation singleton.  Each enum value has its own SDL_Texture, its own
 * grid (cols x rows), its own frame_count cap, frame_time, play_mode, and an
 * optional y_offset for animations whose source frames put the character
 * higher in the cell than ground-level (e.g., GROUND_SLAM airborne frames).
 *
 * Sheets land at module init; freed at boss_anim_free.  IDLE has no sheet —
 * its render path falls back to the last frame of whichever animation last
 * played, so the boss never blanks out between states.
 * -------------------------------------------------------------------------- */

#include "boss_anim.h"
#include "ui.h"
#include <SDL2/SDL_image.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- per-animation sheet metadata --------------------------------------- */

typedef struct {
    const char  *path;          /* NULL for IDLE (no sheet)                  */
    int          cols;          /* grid columns; auto-resolved from size     */
    int          rows;          /* grid rows; auto-resolved from size        */
    int          frame_count;   /* logical frame count (caps grid)           */
    float        frame_time;    /* seconds per frame                          */
    BossPlayMode play_mode;
    int          y_offset_px;   /* added to dst.y at render (positive = down)*/
} BossAnimMeta;

#define SHEET(name) "spritesheets assets/final boss/final boss-" name ".png"

static BossAnimMeta s_meta[BOSS_STATE_COUNT] = {
    /* BOSS_IDLE  ─ substitute, no sheet                                    */
    [BOSS_IDLE]        = { NULL,                             0, 0,  0, 0.20f, BOSS_PLAY_LOOP,         0 },
    /* BOSS_RUN   ─ 3584x3584 → 7x7=49 frames                                */
    [BOSS_RUN]         = { SHEET("run"),                     7, 7, 49, 0.07f, BOSS_PLAY_LOOP,         0 },
    /* BOSS_ATTACK ─ 4096x4096 → 8x8=64 frames                                */
    [BOSS_ATTACK]      = { SHEET("attack"),                  8, 8, 64, 0.06f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_KICK ─ 1280x1280 → 5x5=25 frames                                 */
    [BOSS_KICK]        = { SHEET("kick"),                    5, 5, 25, 0.06f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_GROUND_SLAM ─ 1280x1280 → 5x5=25; airborne frames pushed down    */
    [BOSS_GROUND_SLAM] = { SHEET("jumps_and_does_ground_slam"),
                                                              5, 5, 25, 0.08f, BOSS_PLAY_ONCE_HOLD,   28 },
    /* BOSS_HIT_REACT ─ 1280x1280 → 5x5=25                                   */
    [BOSS_HIT_REACT]   = { SHEET("hit_react"),               5, 5, 25, 0.07f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_HURT ─ 1280x1280 → 5x5=25                                        */
    [BOSS_HURT]        = { SHEET("hurt"),                    5, 5, 25, 0.09f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_RAGE ─ 1280x1280 → 5x5=25, one-shot power-up                     */
    [BOSS_RAGE]        = { SHEET("rage"),                    5, 5, 25, 0.07f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_POINT ─ 1280x1280 → 5x5=25, pre-fight taunt                      */
    [BOSS_POINT]       = { SHEET("point"),                   5, 5, 25, 0.09f, BOSS_PLAY_ONCE_HOLD,    0 },
    /* BOSS_DEATH ─ 1280x1280 → 5x5=25, locks forever                        */
    [BOSS_DEATH]       = { SHEET("death"),                   5, 5, 25, 0.09f, BOSS_PLAY_LOCK_FOREVER, 0 },
    /* BOSS_VICTORY ─ 1280x1280 → 5x5=25, hold last frame                    */
    [BOSS_VICTORY]     = { SHEET("victory"),                 5, 5, 25, 0.08f, BOSS_PLAY_ONCE_HOLD,    0 },
};

/* Per-state runtime overrides.  Phase 2 dials these for ATTACK / KICK +15%
 * speed.  1.0 means "use the meta frame_time as-is". */
static float s_frame_time_scale[BOSS_STATE_COUNT] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

#undef SHEET

/* ---- singleton state ---------------------------------------------------- */

typedef struct {
    BossState    state;
    BossState    last_completed_state; /* drives IDLE substitute             */
    int          last_completed_frame; /* last frame to hold for IDLE        */
    int          frame_index;
    float        frame_timer;
    float        elapsed;              /* seconds since state began           */
    bool         finished;             /* one-shot reached last frame this state */
    bool         just_finished_flag;   /* edge tracker: read-and-clear        */
    BossState    just_finished_which;
} BossAnim;

static BossAnim       g_ba;
static SDL_Renderer  *g_ba_r        = NULL;
static SDL_Texture   *s_tex[BOSS_STATE_COUNT];

/* ---- grid auto-resolve (mirror of src/char_anim.c) ---------------------- */
static int resolve_grid(int side, int fallback)
{
    static const int candidates[] = { 5, 7, 8 };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        int n = candidates[i];
        if (side > 0 && side % n == 0) return n;
    }
    return fallback > 0 ? fallback : 1;
}

/* ---- load one sheet ----------------------------------------------------- */
static SDL_Texture *load_boss_sheet(SDL_Renderer *r, const char *path,
                                    int *out_cols, int *out_rows)
{
    if (!r || !path) return NULL;
    SDL_Texture *t = ui_loadTexture(r, path);
    if (!t) {
        fprintf(stderr, "[boss_anim] load '%s' failed\n", path);
        return NULL;
    }
    int w = 0, h = 0;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    if (out_cols) *out_cols = resolve_grid(w, *out_cols);
    if (out_rows) *out_rows = resolve_grid(h, *out_rows);
    return t;
}

/* ---- lifecycle ---------------------------------------------------------- */
void boss_anim_init(SDL_Renderer *r)
{
    memset(&g_ba, 0, sizeof(g_ba));
    g_ba.state                = BOSS_IDLE;
    g_ba.last_completed_state = BOSS_IDLE;
    g_ba.last_completed_frame = 0;
    g_ba_r                    = r;

    for (int i = 0; i < BOSS_STATE_COUNT; i++) {
        s_tex[i] = NULL;
        s_frame_time_scale[i] = 1.0f;
    }

    /* Load every animation that has a sheet. */
    for (int i = 0; i < BOSS_STATE_COUNT; i++) {
        if (!s_meta[i].path) continue;
        s_tex[i] = load_boss_sheet(r, s_meta[i].path,
                                   &s_meta[i].cols, &s_meta[i].rows);
        /* Cap frame_count at cols*rows in case the meta was over-set. */
        int cap = s_meta[i].cols * s_meta[i].rows;
        if (s_meta[i].frame_count > cap) s_meta[i].frame_count = cap;
    }

    /* Seed the IDLE substitute with a default "standing" pose so the boss
     * is visible from the moment the player enters the room — before the
     * BOSS_POINT taunt fires, before boss_intro even hands off.  We pick
     * the first frame of POINT (a standing/pointing pose) as the seed;
     * fall back to RUN, then ATTACK if POINT's sheet failed to load. */
    BossState seed = BOSS_STATE_COUNT;
    if      (s_tex[BOSS_POINT])  seed = BOSS_POINT;
    else if (s_tex[BOSS_RUN])    seed = BOSS_RUN;
    else if (s_tex[BOSS_ATTACK]) seed = BOSS_ATTACK;
    if (seed != BOSS_STATE_COUNT) {
        g_ba.last_completed_state = seed;
        g_ba.last_completed_frame = 0;
    }
}

void boss_anim_free(void)
{
    for (int i = 0; i < BOSS_STATE_COUNT; i++) {
        if (s_tex[i]) {
            SDL_DestroyTexture(s_tex[i]);
            s_tex[i] = NULL;
        }
    }
    memset(&g_ba, 0, sizeof(g_ba));
}

void boss_anim_reset(void)
{
    /* Keep loaded textures; reset state machine only. */
    g_ba.state                = BOSS_IDLE;
    /* Re-seed the IDLE substitute so the boss is visible immediately on
     * entry to the room (mirror of boss_anim_init).                    */
    BossState seed = BOSS_STATE_COUNT;
    if      (s_tex[BOSS_POINT])  seed = BOSS_POINT;
    else if (s_tex[BOSS_RUN])    seed = BOSS_RUN;
    else if (s_tex[BOSS_ATTACK]) seed = BOSS_ATTACK;
    g_ba.last_completed_state = (seed != BOSS_STATE_COUNT) ? seed : BOSS_IDLE;
    g_ba.last_completed_frame = 0;
    g_ba.frame_index          = 0;
    g_ba.frame_timer          = 0.0f;
    g_ba.elapsed              = 0.0f;
    g_ba.finished             = false;
    g_ba.just_finished_flag   = false;
    for (int i = 0; i < BOSS_STATE_COUNT; i++)
        s_frame_time_scale[i] = 1.0f;
}

/* ---- helpers ------------------------------------------------------------ */
static void ba_start(BossState s)
{
    g_ba.state        = s;
    g_ba.frame_index  = 0;
    g_ba.frame_timer  = 0.0f;
    g_ba.elapsed      = 0.0f;
    g_ba.finished     = false;
}

/* ---- per-frame ---------------------------------------------------------- */
void boss_anim_update(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;

    g_ba.elapsed += dt;

    BossState st = g_ba.state;

    /* IDLE has no sheet — nothing to advance.  Render falls back to the
     * last-completed-state's last frame.                                  */
    if (st == BOSS_IDLE) return;

    int nframes = s_meta[st].frame_count;
    if (nframes <= 0) nframes = 1;
    float dur = s_meta[st].frame_time * s_frame_time_scale[st];
    if (dur <= 0.0f) dur = 0.05f;

    /* DEATH and VICTORY hold on the last frame once reached. */
    if (g_ba.finished) {
        /* nothing — already at last frame */
        return;
    }

    g_ba.frame_timer += dt;
    while (g_ba.frame_timer >= dur) {
        g_ba.frame_timer -= dur;
        g_ba.frame_index++;
        if (g_ba.frame_index >= nframes) {
            /* End of animation. */
            g_ba.frame_index = nframes - 1;
            g_ba.finished    = true;

            /* Edge marker for one-shots so AI code can detect completion
             * with boss_anim_just_finished(st).                          */
            g_ba.just_finished_flag  = true;
            g_ba.just_finished_which = st;

            /* Remember for IDLE substitute. */
            g_ba.last_completed_state = st;
            g_ba.last_completed_frame = g_ba.frame_index;

            /* LOOP rolls back to frame 0; ONCE_HOLD/LOCK_FOREVER stay put.*/
            if (s_meta[st].play_mode == BOSS_PLAY_LOOP) {
                g_ba.frame_index = 0;
                g_ba.finished    = false;
            }
            break;
        }
    }
}

/* ---- state change ------------------------------------------------------- */
static bool ba_can_replace(BossState wanted, bool force)
{
    BossState cur = g_ba.state;

    /* LOCK_FOREVER: nothing replaces it. */
    if (s_meta[cur].play_mode == BOSS_PLAY_LOCK_FOREVER) {
        if (cur == wanted) return false;
        return false;
    }

    /* IDLE / finished one-shot / LOOP: free to swap. */
    if (cur == BOSS_IDLE) return true;
    if (s_meta[cur].play_mode == BOSS_PLAY_LOOP) return true;
    if (g_ba.finished) return true;     /* one-shot done; release           */

    /* ONCE_HOLD still playing — only force, HIT_REACT, HURT, or DEATH may
     * preempt.  DEATH always wins.                                        */
    if (wanted == BOSS_DEATH) return true;
    if (force) return true;
    return false;
}

void boss_anim_set(BossState s)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return;
    if (!ba_can_replace(s, false)) return;
    if (g_ba.state == s && !g_ba.finished) return;
    ba_start(s);
}

void boss_anim_set_force(BossState s)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return;
    if (!ba_can_replace(s, true)) return;
    ba_start(s);
}

/* ---- render ------------------------------------------------------------- */
void boss_anim_render(float world_x, float world_y,
                      int cam_ox, int direction,
                      int hbw, int hbh)
{
    if (!g_ba_r) return;

    /* Choose which (sheet, cols, frame, y_offset) to display.            */
    BossState renderState = g_ba.state;
    int       renderFrame = g_ba.frame_index;
    if (renderState == BOSS_IDLE) {
        /* Substitute: hold last frame of whatever played last. */
        renderState = g_ba.last_completed_state;
        renderFrame = g_ba.last_completed_frame;
        if (renderState == BOSS_IDLE) {
            /* Nothing has ever played yet — draw a silhouette. */
            int dx = (int)world_x + cam_ox + hbw / 2 - BOSS_DISPLAY_W / 2;
            int dy = (int)world_y + hbh - BOSS_DISPLAY_H;
            SDL_Rect r = { dx, dy, BOSS_DISPLAY_W, BOSS_DISPLAY_H };
            SDL_SetRenderDrawColor(g_ba_r, 80, 20, 20, 255);
            SDL_RenderFillRect(g_ba_r, &r);
            SDL_SetRenderDrawColor(g_ba_r, 235, 180, 60, 255);
            SDL_RenderDrawRect(g_ba_r, &r);
            return;
        }
    }

    SDL_Texture *tex = s_tex[renderState];
    int   cols       = s_meta[renderState].cols > 0 ? s_meta[renderState].cols : 1;
    int   rows       = s_meta[renderState].rows > 0 ? s_meta[renderState].rows : 1;
    int   tex_w = 0, tex_h = 0;
    if (tex) SDL_QueryTexture(tex, NULL, NULL, &tex_w, &tex_h);
    int   fw = (tex_w > 0 && cols > 0) ? tex_w / cols : 256;
    int   fh = (tex_h > 0 && rows > 0) ? tex_h / rows : 256;
    int   col = renderFrame % cols;
    int   row = renderFrame / cols;
    SDL_Rect src = { col * fw, row * fh, fw, fh };

    int  dw = BOSS_DISPLAY_W;
    int  dh = BOSS_DISPLAY_H;
    int  feet_x = (int)world_x + hbw / 2;
    int  feet_y = (int)world_y + hbh;
    /* Compensate for the transparent padding below the character's feet
     * inside each source cell — without this the boss floats above the
     * floor by the padding amount.  Per-anim y_offset_px is layered on
     * top for cases like GROUND_SLAM's airborne frames. */
    int  foot_pad = (int)((float)dh * BOSS_FOOT_PAD_FRAC);
    SDL_Rect dst = {
        feet_x + cam_ox - dw / 2,
        feet_y - dh + foot_pad + s_meta[renderState].y_offset_px,
        dw, dh
    };

    if (!tex) {
        /* Sheet missing — paint a silhouette placeholder. */
        SDL_SetRenderDrawColor(g_ba_r, 80, 20, 20, 255);
        SDL_RenderFillRect(g_ba_r, &dst);
        SDL_SetRenderDrawColor(g_ba_r, 235, 180, 60, 255);
        SDL_RenderDrawRect(g_ba_r, &dst);
        return;
    }

    SDL_RendererFlip flip = (direction == -1) ? SDL_FLIP_HORIZONTAL
                                              : SDL_FLIP_NONE;
    SDL_RenderCopyEx(g_ba_r, tex, &src, &dst, 0.0, NULL, flip);
}

/* ---- queries ------------------------------------------------------------ */
BossState     boss_anim_get_state    (void) { return g_ba.state;       }
bool          boss_anim_is_locked    (void)
{
    BossPlayMode m = s_meta[g_ba.state].play_mode;
    if (m == BOSS_PLAY_LOCK_FOREVER) return true;
    if (m == BOSS_PLAY_ONCE_HOLD)    return !g_ba.finished;
    return false;
}
bool          boss_anim_is_finished  (void) { return g_ba.finished;    }
int           boss_anim_current_frame(void) { return g_ba.frame_index; }
int           boss_anim_frame_count  (BossState s)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return 0;
    return s_meta[s].frame_count;
}
float         boss_anim_frame_time   (BossState s)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return 0.0f;
    return s_meta[s].frame_time * s_frame_time_scale[s];
}
BossPlayMode  boss_anim_play_mode    (BossState s)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return BOSS_PLAY_LOOP;
    return s_meta[s].play_mode;
}
float         boss_anim_elapsed      (void) { return g_ba.elapsed;     }

bool boss_anim_just_finished(BossState s)
{
    if (!g_ba.just_finished_flag) return false;
    if (g_ba.just_finished_which != s) return false;
    g_ba.just_finished_flag = false;
    return true;
}

void boss_anim_set_frame_time_scale(BossState s, float scale)
{
    if (s < 0 || s >= BOSS_STATE_COUNT) return;
    if (scale <= 0.0f) scale = 1.0f;
    s_frame_time_scale[s] = scale;
}

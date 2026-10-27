#include "boss_cutscene.h"
#include "boss_intro.h"
#include "npc_dialogue.h"

#include <string.h>

/* ----------------------------------------------------------------------
 * Tunables.  Spec exposes scale, fps, and dialogue frame as constants.
 * -------------------------------------------------------------------- */
#define CUTSCENE_SHEET_PATH   "assets/cutscene/boss_cutscene-cutscene.png"
#define CUTSCENE_COLS         7
#define CUTSCENE_ROWS         7
#define CUTSCENE_FRAME_PX     512
#define CUTSCENE_LAST_FRAME   48    /* 7*7 = 49 frames, indices 0..48     */

#define CUTSCENE_FPS          10.0f /* slower = more dramatic             */
#define CUTSCENE_SCALE_PCT    0.80f /* frame height fills 80% of window   */
#define CUTSCENE_FOOT_PCT     0.80f /* foot anchor at 80% of screen height*/

#define DIALOGUE_FRAME        28    /* start of the candlelit act         */

#define BC_BOSS_NAME          "WILFORD'S ENFORCER"
#define BC_BOSS_LINE          "You shall not pass."

#define BC_SHAKE_AMP          12.0f
#define BC_SHAKE_DUR          0.45f

/* ----------------------------------------------------------------------
 * Module state.  Mirrors the boss_intro singleton pattern.
 * -------------------------------------------------------------------- */
typedef enum {
    BC_IDLE = 0,
    BC_ACTIVE,
    BC_HANDOFF                    /* terminal; boss_intro now owns control */
} BCStage;

typedef struct {
    BCStage      stage;
    bool         played;          /* one-shot, persists across deaths      */
    float        elapsed;
    int          frame_idx;       /* clamped to [0, CUTSCENE_LAST_FRAME]   */
    bool         shake_fired;
    bool         dialogue_fired;
    NPCDialogue  dlg;             /* fed to dialogue_cinematic_start       */
} BCState;

static BCState             g_bc;
static SDL_Texture        *g_sheet    = NULL;
static SDL_Renderer       *g_renderer = NULL;
static UIFontPack         *g_fonts    = NULL;
static UITextCache        *g_cache    = NULL;
static Background         *g_bg       = NULL;
static GameplayState      *g_gp       = NULL;
static AudioSystem        *g_audio    = NULL;
static DialogueCinematic  *g_cin      = NULL;

/* ----------------------------------------------------------------------
 * Lifecycle.
 * -------------------------------------------------------------------- */
void boss_cutscene_init(SDL_Renderer *renderer, UIFontPack *fonts,
                        UITextCache *cache, Background *bg,
                        GameplayState *gp, AudioSystem *audio,
                        DialogueCinematic *cin)
{
    memset(&g_bc, 0, sizeof(g_bc));
    g_renderer = renderer;
    g_fonts    = fonts;
    g_cache    = cache;
    g_bg       = bg;
    g_gp       = gp;
    g_audio    = audio;
    g_cin      = cin;

    /* Sheet is lazy-loaded on first start() so a process that never
     * reaches wagon 5 doesn't pay the disk read.                       */
    g_sheet = NULL;

    /* Seed the dialogue once; dialogue_cinematic_start later opens it
     * (which preserves response[]) so the typewriter fires immediately
     * instead of stalling on the "waiting on LLM" placeholder.         */
    npc_dialogue_init(&g_bc.dlg, BC_BOSS_NAME, "");
    snprintf(g_bc.dlg.response, sizeof(g_bc.dlg.response), "%s", BC_BOSS_LINE);
}

bool boss_cutscene_should_start(void)
{
    return !g_bc.played && g_bc.stage == BC_IDLE;
}

bool boss_cutscene_active(void)
{
    return g_bc.stage == BC_ACTIVE;
}

bool boss_cutscene_freezes_world(void)
{
    /* Freeze for the entire ACTIVE span.  HANDOFF returns false so
     * boss_intro can take over and pin the gate itself.                */
    return g_bc.stage == BC_ACTIVE;
}

void boss_cutscene_start(void)
{
    if (g_bc.played || g_bc.stage != BC_IDLE) return;

    if (!g_sheet && g_renderer)
        g_sheet = ui_loadTexture(g_renderer, CUTSCENE_SHEET_PATH);

    g_bc.stage          = BC_ACTIVE;
    g_bc.elapsed        = 0.0f;
    g_bc.frame_idx      = 0;
    g_bc.shake_fired    = false;
    g_bc.dialogue_fired = false;
    g_bc.played         = true;

    /* Pin the player-input gate the same way boss_intro does.  Released
     * at the handoff so boss_intro can re-assert it.                   */
    if (g_bg) g_bg->transitionSlide.actif = 1;
}

/* ----------------------------------------------------------------------
 * Handoff: stop the cutscene, close any open dialogue, kick boss_intro.
 * -------------------------------------------------------------------- */
static void boss_cutscene_handoff(void)
{
    if (g_bc.stage == BC_HANDOFF) return;
    g_bc.stage = BC_HANDOFF;

    /* Slide the dialogue chrome out (no-op if never opened or already
     * closing).  boss_intro then opens its own pass through the
     * cinematic for the Stage-3 reveal.                                */
    if (g_cin) dialogue_cinematic_close(g_cin);

    /* Release the input gate so boss_intro_start can pin it again.    */
    if (g_bg) g_bg->transitionSlide.actif = 0;

    if (boss_intro_should_start()) boss_intro_start();
}

/* ----------------------------------------------------------------------
 * Update — drives the frame index by wall-clock dt, fires one-shots,
 * and triggers the handoff when frame 48 reached + dialogue dismissed.
 * -------------------------------------------------------------------- */
void boss_cutscene_update(float dt)
{
    if (g_bc.stage != BC_ACTIVE) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;
    g_bc.elapsed += dt;

    int idx = (int)(g_bc.elapsed * CUTSCENE_FPS);
    if (idx < 0) idx = 0;
    if (idx > CUTSCENE_LAST_FRAME) idx = CUTSCENE_LAST_FRAME;
    g_bc.frame_idx = idx;

    /* Frame 0 — screen shake pulse (and a thud if audio is up). */
    if (!g_bc.shake_fired) {
        if (g_gp) gameplay_trigger_shake(g_gp, BC_SHAKE_AMP, BC_SHAKE_DUR);
        if (g_audio) audio_play_sfx(g_audio, SFX_HIT_IMPACT);
        g_bc.shake_fired = true;
    }

    /* Frame DIALOGUE_FRAME — open the cinematic on the pre-seeded line. */
    if (!g_bc.dialogue_fired && g_bc.frame_idx >= DIALOGUE_FRAME) {
        if (g_cin) {
            /* Re-seed every time we fire — defensive, in case a prior
             * cinematic left response[] dirty.                          */
            snprintf(g_bc.dlg.response, sizeof(g_bc.dlg.response),
                     "%s", BC_BOSS_LINE);
            float cx = (float)(W_ECRAN / 2);
            float cy = (float)(H_ECRAN / 2);
            dialogue_cinematic_start(g_cin, &g_bc.dlg, cx, cy, cx, cy);
        }
        g_bc.dialogue_fired = true;
    }

    /* Completion: last frame reached AND cinematic returned to IDLE
     * (the user dismissed and the EXIT slide finished).  We let the
     * sprite hold on frame 48 in the meantime.                         */
    if (g_bc.frame_idx >= CUTSCENE_LAST_FRAME) {
        bool dlg_done;
        if (!g_bc.dialogue_fired) dlg_done = false;        /* arrived before frame 28 — shouldn't happen */
        else if (!g_cin)          dlg_done = true;
        else                      dlg_done = (g_cin->stage == CIN_IDLE);
        if (dlg_done) boss_cutscene_handoff();
    }
}

/* ----------------------------------------------------------------------
 * Skip — Space or Enter at any time during ACTIVE.  Bypasses the
 * remaining frames + any in-flight dialogue.
 * -------------------------------------------------------------------- */
bool boss_cutscene_handle_skip_key(SDL_Keycode k)
{
    if (g_bc.stage != BC_ACTIVE) return false;
    if (k != SDLK_SPACE && k != SDLK_RETURN) return false;
    boss_cutscene_handoff();
    return true;
}

/* ----------------------------------------------------------------------
 * Render — clear black, draw the current frame centered+foot-anchored,
 * vignette on top, skip hint in the bottom-right corner.
 *
 * Called from main.c instead of the world+HUD render block while the
 * cutscene is active; the dialogue chrome (letterbox + typewriter)
 * paints on top of us from the existing main.c chrome path.
 * -------------------------------------------------------------------- */
void boss_cutscene_render(void)
{
    if (g_bc.stage != BC_ACTIVE) return;
    if (!g_renderer) return;

    /* 1 — pure black background. */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(g_renderer, &full);

    /* 2 — sprite centered horizontally, foot-anchored low. */
    if (g_sheet) {
        int dst_h = (int)((float)H_ECRAN * CUTSCENE_SCALE_PCT);
        int dst_w = dst_h;                              /* square frame */
        int dst_x = (W_ECRAN - dst_w) / 2;
        int foot_y = (int)((float)H_ECRAN * CUTSCENE_FOOT_PCT);
        int dst_y  = foot_y - dst_h;

        int col = g_bc.frame_idx % CUTSCENE_COLS;
        int row = g_bc.frame_idx / CUTSCENE_COLS;
        SDL_Rect src = {col * CUTSCENE_FRAME_PX,
                        row * CUTSCENE_FRAME_PX,
                        CUTSCENE_FRAME_PX,
                        CUTSCENE_FRAME_PX};
        SDL_Rect dst = {dst_x, dst_y, dst_w, dst_h};
        SDL_RenderCopy(g_renderer, g_sheet, &src, &dst);
    }

    /* 3 — vignette.  Reuse the dialogue-cinematic 4-ring vignette via a
     *      tiny fake-cinematic struct so we don't duplicate the loop. */
    {
        DialogueCinematic fake;
        memset(&fake, 0, sizeof(fake));
        fake.stage    = CIN_ACTIVE;
        fake.progress = 1.0f;
        dialogue_cinematic_render_vignette(&fake, g_renderer);
    }

    /* 4 — skip hint (matches the intro/outro corner-hint style). */
    if (g_fonts && g_cache) {
        TTF_Font *fHint = g_fonts->monoSmall ? g_fonts->monoSmall
                                              : g_fonts->fallback;
        if (fHint) {
            SDL_Color hint = {200, 200, 200, 200};
            ui_draw_text(g_cache, g_renderer, fHint,
                         "Press Space to skip", hint,
                         W_ECRAN - 200, H_ECRAN - 22);
        }
    }
}

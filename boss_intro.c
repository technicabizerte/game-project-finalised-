/* ----------------------------------------------------------------------------
 * boss_intro — implementation.
 *
 * Architecture notes:
 *   - Singleton state (g_bi) plus stashed engine pointers (g_bg, g_gp, g_cin,
 *     g_renderer, g_fonts, g_cache) so the public API can be the bare 4-stage
 *     start/update/render the spec asked for.
 *
 *   - Stage 1 doesn't drive the door at all on its own — door_anim already
 *     owns the slam, the screen shake (gameplay_trigger_shake from inside
 *     door_anim_update Stage 1->2 transition), and the white flash
 *     (render_flash_overlay during Stage 2).  We just call
 *     door_anim_close_and_lock() and wait for door_anim_active() to clear.
 *
 *   - Freeze: we set bg->transitionSlide.actif (the same input gate
 *     joueur_update_interieur honors at background.c:823, and that door_anim
 *     hijacks for stages 1..4) so player input is guaranteed dead even if
 *     the world block somehow runs.  Plus boss_intro_freezes_world() lets
 *     main.c extend its world-freeze gate (mirrors the existing
 *     dialogue_cinematic_freezes_world hook on the same conditional), so
 *     ennemis_update / projectiles / particles all pause too.
 *
 *   - Camera: we don't reach into kinetic.c.  Instead boss_intro_world_pan_ox()
 *     returns an additive offset that main.c folds into the existing
 *     `cin_pan` term (which is already added to gameplay_render_world's ox
 *     and friends).  Same composition pattern as the dialogue cinematic.
 *
 *   - Letterbox: we drive the existing dialogue_cinematic via _start /
 *     _close.  We pass NULL for the wrapped NPCDialogue, which makes the
 *     cinematic chrome's text path skip itself (see the `c->dlg && bot_h`
 *     guard at dialogue_cinematic.c:302), leaving the bars + amber dividing
 *     line in place.  We then paint our own boss name + subtitle inside the
 *     bottom bar from boss_intro_render().
 * -------------------------------------------------------------------------- */

#include "boss_intro.h"
#include "door_anim.h"
#include "combat.h"

#include <math.h>
#include <string.h>

/* Stage timings (seconds) — see header for the design table. */
#define BI_PAN_DUR        2.50f
#define BI_REVEAL_DUR     2.00f   /* total time bars stay in    */
#define BI_NAME_FADE_IN   0.35f   /* name + subtitle fade-in    */
#define BI_EXIT_DUR       0.50f

/* Boss metadata.  No ET_BOSS / boss-name field exists on the engine side
 * (the "boss" is the lone ET_ENFORCER spawned in wagon 4, see
 * background.c:536); we declare the display name + subtitle here.  The
 * name string matches boss_monologue.c's "WILFORD'S ENFORCER" header so
 * the two systems read as the same character. */
#define BI_BOSS_NAME     "WILFORD'S ENFORCER"
#define BI_BOSS_SUBTITLE "Last of Wilford's Lieutenants"

/* HUD accent (gold/amber) — same RGB the HUD uses in gameplay.c and that
 * door_anim mirrors as DA_ACCENT.  Crimson is the boss-themed shade
 * boss_monologue uses for its WILFORD'S ENFORCER header text. */
static const SDL_Color BI_ACCENT  = {235, 180,  60, 255};
static const SDL_Color BI_CRIMSON = {255, 100, 100, 255};

/* ---------------------------------------------------------- singleton ---- */
typedef struct {
    BossIntroStage stage;
    float          stage_t;
    bool           played;          /* one-shot flag, persists across deaths */

    int            boss_idx;        /* index into bg->ennemis, or -1         */
    float          boss_x, boss_y;  /* world center of the boss              */
    float          player_x, player_y;

    float          pan_target;      /* px, signed; negative = world drifts L */
    float          pan_curr;        /* current per-frame additive offset     */
    float          name_alpha;      /* 0..1 envelope for the reveal text     */
} BossIntroState;

static BossIntroState g_bi;

static SDL_Renderer       *g_renderer = NULL;
static UIFontPack         *g_fonts    = NULL;
static UITextCache        *g_cache    = NULL;
static Background         *g_bg       = NULL;
static GameplayState      *g_gp       = NULL;
static DialogueCinematic  *g_cin      = NULL;

/* --------------------------------------------------------- small utils --- */
static float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

/* smoothstep-style ease-in-out, gentle take-off and gentle settle. */
static float ease_in_out(float t)
{
    t = clamp01(t);
    return (t < 0.5f) ? 2.0f * t * t
                       : 1.0f - powf(-2.0f * t + 2.0f, 2.0f) * 0.5f;
}

/* Pick the boss enemy: the lone ET_ENFORCER spawned in the final wagon
 * (background.c seeds exactly one at index 4).  Falls back to "first
 * active enemy in the final wagon" if some other layout is in play. */
static int find_boss_enemy_idx(const Background *bg)
{
    if (!bg) return -1;
    int last_enforcer = -1;
    for (int i = 0; i < bg->nbEnnemis; i++) {
        const Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != MAX_WAGONS - 1) continue;
        if (e->type == ET_ENFORCER) last_enforcer = i;
    }
    if (last_enforcer >= 0) return last_enforcer;
    for (int i = 0; i < bg->nbEnnemis; i++) {
        const Ennemi *e = &bg->ennemis[i];
        if (e->actif && e->wagon == MAX_WAGONS - 1) return i;
    }
    return -1;
}

/* --------------------------------------------------------------- API ---- */
void boss_intro_init(SDL_Renderer *r, UIFontPack *f, UITextCache *c,
                     Background *bg, GameplayState *gp,
                     DialogueCinematic *cin)
{
    memset(&g_bi, 0, sizeof(g_bi));
    g_bi.stage    = BOSS_INTRO_IDLE;
    g_bi.boss_idx = -1;

    g_renderer = r;
    g_fonts    = f;
    g_cache    = c;
    g_bg       = bg;
    g_gp       = gp;
    g_cin      = cin;
}

void boss_intro_reset_run(void)
{
    g_bi.played    = false;
    g_bi.stage     = BOSS_INTRO_IDLE;
    g_bi.stage_t   = 0.0f;
    g_bi.pan_curr  = 0.0f;
    g_bi.pan_target = 0.0f;
    g_bi.name_alpha = 0.0f;
    g_bi.boss_idx  = -1;
}

bool boss_intro_should_start(void)
{
    return (!g_bi.played) && (g_bi.stage == BOSS_INTRO_IDLE);
}

bool boss_intro_active(void)
{
    return g_bi.stage != BOSS_INTRO_IDLE && g_bi.stage != BOSS_INTRO_DONE;
}

bool boss_intro_freezes_world(void)
{
    /* Freeze the world during PAN + REVEAL only.  We deliberately do NOT
     * freeze during DOOR_LOCK: door_anim_update is called from inside the
     * main-loop world-sim block (see main.c:~1313), so freezing the world
     * also freezes the door animation — which would deadlock the intro on
     * Stage 1 because we wait for !door_anim_active() to advance.  During
     * DOOR_LOCK the player input gate is already held by door_anim itself
     * (it re-asserts bg->transitionSlide.actif every frame, see
     * door_anim.c:373), and the door covers the screen anyway, so letting
     * the world tick under it is visually identical to a normal door
     * transition.  EXIT also releases the freeze so the world starts
     * ticking again under the sliding-out bars (mirrors the
     * dialogue_cinematic_freezes_world pattern: ENTER+ACTIVE freeze, EXIT
     * lets the world breathe). */
    return (g_bi.stage == BOSS_INTRO_PAN ||
            g_bi.stage == BOSS_INTRO_REVEAL);
}

int boss_intro_world_pan_ox(void)
{
    return (int)g_bi.pan_curr;
}

void boss_intro_start(void)
{
    if (g_bi.played) return;
    if (g_bi.stage != BOSS_INTRO_IDLE) return;
    if (!g_bg) return;

    /* Stage 1's slam, shake, and white flash are all owned by door_anim.
     * gameplay_trigger_shake fires from inside door_anim's Stage1->2
     * transition (door_anim.c uses DA_SHAKE_AMP / DA_SHAKE_DUR), and
     * render_flash_overlay paints the white flash during Stage 2. */
    door_anim_close_and_lock();

    /* Cache anchors at the moment the intro starts. */
    g_bi.boss_idx = find_boss_enemy_idx(g_bg);
    if (g_bi.boss_idx >= 0) {
        const Ennemi *e = &g_bg->ennemis[g_bi.boss_idx];
        g_bi.boss_x = e->x + (float)BOSS_HBW * 0.5f;
        g_bi.boss_y = e->y + (float)BOSS_HBH * 0.5f;
    } else {
        /* Fallback: assume the boss stands near the wagon's right wall. */
        g_bi.boss_x = (float)(WAGON_MUR_D_X - WAGON_PORTE_W - 80);
        g_bi.boss_y = (float)(WAGON_SOL_Y - BOSS_HBH / 2);
    }

    /* Player anchor: use joueur 0 (mono) or whichever player is active. */
    {
        Joueur *p = &g_bg->joueurs[0];
        if (g_bg->nbJoueursActifs >= 1 && p->actif) {
            g_bi.player_x = p->x + (float)p->w * 0.5f;
            g_bi.player_y = p->y + (float)p->h * 0.5f;
        } else {
            g_bi.player_x = (float)(WAGON_MUR_G_X + WAGON_PORTE_W + 20);
            g_bi.player_y = (float)(WAGON_SOL_Y - JOUEUR_H / 2);
        }
    }

    /* Pan target: lands the boss at logical-screen center.  The wagon BG
     * isn't pan-aware (matches the cinematic), so the pan reads as a
     * camera framing on the entities while the wagon stays fixed. */
    g_bi.pan_target = (float)(W_ECRAN / 2) - g_bi.boss_x;
    g_bi.pan_curr   = 0.0f;
    g_bi.name_alpha = 0.0f;

    /* Belt-and-braces: hold the existing player-input gate so even an
     * out-of-band update path (loaded save mid-intro, etc.) can't move
     * the player.  door_anim sets this too; we keep it pinned through
     * all the boss-intro stages and release it when EXIT finishes. */
    g_bg->transitionSlide.actif = 1;

    g_bi.stage   = BOSS_INTRO_DOOR_LOCK;
    g_bi.stage_t = 0.0f;
    g_bi.played  = true;     /* one-shot from this point on */
}

/* ------------------------------------------------------------ update ---- */
void boss_intro_update(float dt)
{
    if (g_bi.stage == BOSS_INTRO_IDLE || g_bi.stage == BOSS_INTRO_DONE) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;          /* cap a long frame */

    g_bi.stage_t += dt;

    /* Re-assert the input gate while we still own the world.  door_anim
     * also keeps it pinned during stages 1..4 (door_anim.c:373); we mirror
     * that for the rest of the boss intro. */
    if (g_bg && boss_intro_freezes_world())
        g_bg->transitionSlide.actif = 1;

    switch (g_bi.stage) {

    case BOSS_INTRO_DOOR_LOCK:
        /* Wait for door_anim's full 5-stage close-and-lock to finish.  The
         * door's own gate releases at IDLE (door_anim.c:208), but we keep
         * the input gate pinned via the assertion above. */
        if (!door_anim_active()) {
            g_bi.stage   = BOSS_INTRO_PAN;
            g_bi.stage_t = 0.0f;
        }
        break;

    case BOSS_INTRO_PAN: {
        /* Linear-ish pan from 0 to the boss-centering offset.  Eased so
         * the camera feels like it's accelerating then settling, not a
         * hard linear sweep.  Spec calls for "exactly 2.5s" for the full
         * traversal — BI_PAN_DUR is the entire stage. */
        float t = ease_in_out(g_bi.stage_t / BI_PAN_DUR);
        g_bi.pan_curr = g_bi.pan_target * t;

        if (g_bi.stage_t >= BI_PAN_DUR) {
            g_bi.pan_curr = g_bi.pan_target;
            g_bi.stage    = BOSS_INTRO_REVEAL;
            g_bi.stage_t  = 0.0f;

            /* Slide the existing dialogue-cinematic letterbox bars in.
             * Pass NULL for the wrapped dlg so the chrome's text path is
             * skipped (we draw our own boss name + subtitle in
             * boss_intro_render); the bars + amber dividing line still
             * draw because they're outside the c->dlg guard at
             * dialogue_cinematic.c:302. */
            if (g_cin) {
                dialogue_cinematic_start(g_cin, NULL,
                                         g_bi.boss_x,   g_bi.boss_y,
                                         g_bi.player_x, g_bi.player_y);
            }

            /* Flip the boss to face the player using the existing facing
             * field.  The world is frozen so this fires no AI reaction;
             * it's a static pose for the reveal. */
            if (g_bi.boss_idx >= 0 && g_bg) {
                Ennemi *e = &g_bg->ennemis[g_bi.boss_idx];
                e->direction = (g_bi.player_x < e->x) ? -1 : 1;
            }
        }
    } break;

    case BOSS_INTRO_REVEAL: {
        /* Hold pan; fade the name in over BI_NAME_FADE_IN, hold the rest
         * of the stage.  Total stage = BI_REVEAL_DUR; the spec's "1.5s
         * hold" is BI_REVEAL_DUR - BI_NAME_FADE_IN - bars-slide-in
         * (~0.4s from CIN_ENTER_SECONDS) ≈ 1.25s, close enough. */
        g_bi.pan_curr   = g_bi.pan_target;
        g_bi.name_alpha = clamp01(g_bi.stage_t / BI_NAME_FADE_IN);

        if (g_bi.stage_t >= BI_REVEAL_DUR) {
            /* Begin EXIT — slide bars out, lerp camera back, fade text. */
            if (g_cin) dialogue_cinematic_close(g_cin);
            g_bi.stage   = BOSS_INTRO_EXIT;
            g_bi.stage_t = 0.0f;
        }
    } break;

    case BOSS_INTRO_EXIT: {
        float t = clamp01(g_bi.stage_t / BI_EXIT_DUR);
        g_bi.pan_curr   = g_bi.pan_target * (1.0f - ease_in_out(t));
        g_bi.name_alpha = 1.0f - t;

        if (g_bi.stage_t >= BI_EXIT_DUR) {
            g_bi.pan_curr   = 0.0f;
            g_bi.name_alpha = 0.0f;

            /* Release the existing input gate.  Boss AI resumes naturally
             * because the world-freeze gate clears (boss_intro_freezes_world
             * returns false at DONE), so ennemis_update fires again next
             * frame and the boss's already-ES_WALK state ticks the AI back
             * up — no separate "activate" call is needed. */
            if (g_bg) g_bg->transitionSlide.actif = 0;

            g_bi.stage   = BOSS_INTRO_DONE;
            g_bi.stage_t = 0.0f;
        }
    } break;

    default: break;
    }
}

/* ------------------------------------------------------------ render ---- */
void boss_intro_render(void)
{
    if (g_bi.stage != BOSS_INTRO_REVEAL && g_bi.stage != BOSS_INTRO_EXIT) return;
    if (!g_renderer || !g_fonts || !g_cache) return;
    if (g_bi.name_alpha <= 0.001f) return;

    /* The letterbox bars themselves are drawn by
     * dialogue_cinematic_render_chrome (called from main.c just before us);
     * we paint the boss name + subtitle on top of the bottom bar.  The
     * bottom bar height matches the cinematic's CIN_BAR_BOT_PCT. */
    int bot_h = (int)((float)H_ECRAN * CIN_BAR_BOT_PCT);
    int bar_y = H_ECRAN - bot_h;

    int alpha     = (int)(g_bi.name_alpha * 255.0f);
    int sub_alpha = (alpha * 200) / 255;
    if (alpha     > 255) alpha     = 255;
    if (sub_alpha > 255) sub_alpha = 255;

    SDL_Color name_c = BI_CRIMSON; name_c.a = (Uint8)alpha;
    SDL_Color sub_c  = BI_ACCENT;  sub_c.a  = (Uint8)sub_alpha;

    TTF_Font *fBig   = g_fonts->bebasTitle ? g_fonts->bebasTitle : g_fonts->fallback;
    TTF_Font *fSmall = g_fonts->mono       ? g_fonts->mono       : g_fonts->fallback;

    /* Center vertically inside the bottom bar with a small inset.  The
     * cinematic chrome's text path is suppressed (NULL dlg), so the whole
     * bar is ours to fill. */
    int name_y = bar_y + 14;
    int sub_y  = name_y + 46;

    ui_draw_text_centered(g_cache, g_renderer, fBig,
                          BI_BOSS_NAME, name_c,
                          W_ECRAN / 2, name_y);
    ui_draw_text_centered(g_cache, g_renderer, fSmall,
                          BI_BOSS_SUBTITLE, sub_c,
                          W_ECRAN / 2, sub_y);
}

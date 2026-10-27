/* ----------------------------------------------------------------------------
 * door_anim — implementation.
 *
 * Architecture notes:
 *   - Singleton state (g_da) plus pointers to engine bits (g_bg/g_gp/g_audio/
 *     g_fonts/g_cache) stashed at init() time, so the public API can match
 *     the spec's bare signatures: door_anim_open(), door_anim_update(dt),
 *     door_anim_render().
 *
 *   - Input gate: we hijack bg->transitionSlide.actif (already checked by
 *     joueur_update_interieur — background.c:831) so the rest of the engine
 *     doesn't have to learn about the door.  We DON'T let transitionSlide
 *     fire its own wagon-swap underneath us though: every update() we clamp
 *     transitionSlide.offset back to 0, which both (a) keeps the kinetic
 *     camera centered on the active wagon (camX = transDir * offset *
 *     slotW / KIN_SCR_W = 0) and (b) prevents the offset>=W_ECRAN branch in
 *     transitionSlide_update from firing the legacy player-reposition path.
 *     We do the wagon swap ourselves at Stage 2.
 *
 *   - Render order: door_anim_render() draws after the world / kinetic
 *     composite, before any HUD chrome — so the steam burst spawned via the
 *     existing bg->particles field renders UNDER the door (it leaks through
 *     the splitting seam in stages 4–5, which is exactly the look we want).
 * -------------------------------------------------------------------------- */

#include "door_anim.h"

#include "weaponfx.h"    /* ParticleField + Particle, for the white steam burst */

#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Stage timings (seconds) — see header for the design table. */
#define DA_STAGE1_DUR          0.30f
#define DA_STAGE2_DUR          0.08f   /* same as the flash; doubles as a
                                          tiny "hold" so the slam is felt */
#define DA_STAGE3_DUR          1.20f
#define DA_STAGE3_LOCKED_DUR   1.00f
#define DA_STAGE4_DUR          0.40f
#define DA_STAGE5_DUR          0.30f

#define DA_TITLE_FADE_IN       0.30f
#define DA_TITLE_FADE_OUT      0.20f
/* hold = DA_STAGE3_DUR - (fade_in + fade_out) = 0.70s, per spec */

#define DA_FLASH_DUR           0.08f
#define DA_FLASH_ALPHA         180

#define DA_SHAKE_INTENSITY     0.60f   /* spec units; mapped to engine units */
#define DA_SHAKE_DUR           0.20f

#define DA_LOCKED_PULSE_HZ     4.0f

/* The screen-shake API takes amplitude in pixel units; map the spec's "0.6"
 * intensity to a sensible engine value.  SHAKE_HAZARD_AMP is 9.0f and feels
 * heavy, SHAKE_HIT_AMP is 6.0f for normal hits — 0.6 * SHAKE_HAZARD_AMP =
 * 5.4f reads as a satisfying door-slam thunk without being earthquake-y. */
#define DA_SHAKE_AMP           (DA_SHAKE_INTENSITY * SHAKE_HAZARD_AMP)

/* HUD accent (gold/amber) — same RGB the HUD uses in gameplay.c. */
static const SDL_Color DA_ACCENT      = {235, 180,  60, 255};
static const SDL_Color DA_ACCENT_SOFT = {220, 175,  90, 255};

static const char *DA_WAGON_NAME[5] = {
    "Wagon Pauvres", "Wagon Industrie", "Wagon Aquarium",
    "Wagon Jardin",  "Wagon Luxe"
};

/* ---------------------------------------------------------- singleton ---- */
static DoorAnim         g_da;
static SDL_Renderer    *g_renderer = NULL;
static UIFontPack    *g_fonts   = NULL;
static UITextCache   *g_cache   = NULL;
static Background    *g_bg      = NULL;
static GameplayState *g_gp      = NULL;
static AudioSystem   *g_audio   = NULL;     /* reserved for future SFX */

static SDL_Texture *g_tex_side  = NULL;
static SDL_Texture *g_tex_front = NULL;
static SDL_Texture *g_tex_frame = NULL;
static int          g_side_w = 0,  g_side_h = 0;
static int          g_front_w = 0, g_front_h = 0;
static int          g_frame_w = 0, g_frame_h = 0;

/* Per-anim transient state (not on the public struct since the spec keeps
 * that lean — these are pure implementation detail). */
static int          g_direction      = 1;   /* +1 forward, -1 backward     */
static int          g_swapped        = 0;   /* 1 once Stage 2 has swapped  */
static int          g_shake_fired    = 0;   /* 1 once Stage 1 fired shake  */
static int          g_steam_fired    = 0;   /* 1 once Stage 4 spawned steam*/
static int          g_target_wagon   = 0;   /* destination wagon index     */
static float        g_flash_timer    = 0.0f;
static float        g_lock_pulse_t   = 0.0f;

/* --------------------------------------------------------- small utils --- */
static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float ease_in_cubic(float t)  { t = clamp01(t); return t * t * t; }
static float ease_out_cubic(float t) { t = clamp01(t); float u = 1.0f - t; return 1.0f - u * u * u; }

static SDL_Texture *load_tex(SDL_Renderer *r, const char *path,
                              int *out_w, int *out_h)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        fprintf(stderr, "[door_anim] IMG_Load '%s': %s\n", path, IMG_GetError());
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return NULL;
    }
    if (out_w) *out_w = s->w;
    if (out_h) *out_h = s->h;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* Reuse the existing particle field for a quick burst — recolor freshly
 * spawned particles to white so they read as steam.  particles_burst()
 * appends 8-13 particles to f->pool, all newly marked actif=1; we walk the
 * pool tail-to-head and recolor / retarget the ones we just added. */
static void spawn_steam_burst(float wx, float wy)
{
    if (!g_bg) return;
    ParticleField *f = &g_bg->particles;
    int before = f->nb;
    particles_burst(f, wx, wy);
    int after = f->nb;
    /* Recolor & bias upward/outward.  particles_burst's vy already has a
     * -1.0f bias (and gravity), so the trajectory is naturally "burst up
     * then settle" — we just nudge it a bit harder so it reads as steam. */
    for (int i = before; i < after && i < PARTICLES_MAX; i++) {
        Particle *p = &f->pool[i];
        if (!p->actif) continue;
        p->r = 245; p->g = 245; p->b = 250;
        p->vy -= 0.6f;          /* extra upward kick */
        p->vx *= 1.20f;         /* a bit more outward spread */
        if (p->size < 3) p->size = 3;
    }
    /* If particles_burst hadn't found room (f->nb stalled), don't worry —
     * the visual is "a small burst", not a guaranteed count. */
    (void)after;
}

/* Stage 2 wagon-swap.  Replicates transitionSlide_update's player-reposition
 * + blockTransitionLeft side effects without firing the slide animation. */
static void perform_wagon_swap(void)
{
    if (!g_bg) return;
    if (g_swapped) return;

    g_bg->wagonActif = g_target_wagon;

    /* Forward swaps lock the left-transition (matches the legacy slide). */
    if (g_direction == 1) g_bg->blockTransitionLeft = 1;

    /* Reposition each active player to the entry side of the new wagon —
     * same offsets the legacy slide used. */
    for (int p = 0; p < g_bg->nbJoueursActifs; p++) {
        if (!g_bg->joueurs[p].actif) continue;
        if (g_direction == 1)
            g_bg->joueurs[p].x = (float)(WAGON_MUR_G_X + WAGON_PORTE_W + 6 + p * 30);
        else
            g_bg->joueurs[p].x = (float)(WAGON_MUR_D_X - WAGON_PORTE_W
                                          - JOUEUR_W - 6 - p * 30);
        g_bg->joueurs[p].y    = (float)(WAGON_SOL_Y - JOUEUR_H);
        g_bg->joueurs[p].velX = 0.0f;
        g_bg->joueurs[p].velY = 0.0f;
        g_bg->joueurs[p].auSol = 1;
    }

    /* Reset the hijacked transitionSlide bookkeeping so kinetic doesn't
     * keep displaying a half-slid camera once we're past Stage 2. */
    g_bg->transitionSlide.offset     = 0.0f;
    g_bg->transitionSlide.wagonCible = g_bg->wagonActif;
    g_bg->transitionSlide.direction  = g_direction;

    g_swapped = 1;
}

/* Build the title strings for Stage 3 from the freshly-swapped wagon. */
static void build_title_for_current_wagon(char *line1, size_t n1,
                                           char *line2, size_t n2)
{
    int w = g_bg ? g_bg->wagonActif : 0;
    if (w < 0) w = 0;
    if (w >= 5) w = 4;
    snprintf(line1, n1, "Wagon %d / 5", w + 1);
    snprintf(line2, n2, "%s", DA_WAGON_NAME[w]);
}

/* ------------------------------------------------------- state moves --- */
static void stage_enter(int s)
{
    g_da.stage = s;
    g_da.timer = 0.0f;

    switch (s) {
    case DOOR_STAGE_IDLE:
        g_da.duration = 0.0f;
        g_da.left_x   = 0.0f;
        g_da.right_x  = (float)W_ECRAN;
        g_da.alpha    = 0;
        /* Release the input gate fully and reset color/alpha mods. */
        if (g_bg) g_bg->transitionSlide.actif = 0;
        if (g_tex_front) {
            SDL_SetTextureColorMod(g_tex_front, 255, 255, 255);
            SDL_SetTextureAlphaMod(g_tex_front, 255);
        }
        if (g_tex_frame) SDL_SetTextureAlphaMod(g_tex_frame, 255);
        if (g_tex_side)  SDL_SetTextureAlphaMod(g_tex_side,  255);
        break;

    case DOOR_STAGE1_SIDES:
        g_da.duration = DA_STAGE1_DUR;
        g_da.left_x   = (float)(-g_side_w);
        g_da.right_x  = (float)W_ECRAN;
        g_shake_fired = 0;
        break;

    case DOOR_STAGE2_SLAM:
        g_da.duration = DA_STAGE2_DUR;
        g_flash_timer = 0.0f;
        /* Wagon data swap happens here, with the screen fully covered. */
        perform_wagon_swap();
        /* Also fire the slam shake here in case Stage 1 was extremely short
         * (e.g., dt spike); harmless if already triggered. */
        if (!g_shake_fired && g_gp) {
            gameplay_trigger_shake(g_gp, DA_SHAKE_AMP, DA_SHAKE_DUR);
            g_shake_fired = 1;
        }
        break;

    case DOOR_STAGE3_TITLE:
        g_da.duration = g_da.is_locked_variant
                        ? DA_STAGE3_LOCKED_DUR : DA_STAGE3_DUR;
        g_lock_pulse_t = 0.0f;
        break;

    case DOOR_STAGE4_OPEN:
        g_da.duration = DA_STAGE4_DUR;
        /* Halves start at their natural positions (covering each screen
         * half), eased outward over the stage. */
        g_da.left_x   = 0.0f;
        g_da.right_x  = (float)(W_ECRAN / 2);
        g_steam_fired = 0;
        /* Reset any color mod left over from the locked-variant pulse so
         * the door reads as its normal color while splitting. */
        if (g_tex_front) SDL_SetTextureColorMod(g_tex_front, 255, 255, 255);
        /* Input gate releases at the end of Stage 4 (see exit below); we
         * cannot clear it here without also leaving the input alive for
         * Stage 4 itself.  Cleared on Stage 5 entry. */
        break;

    case DOOR_STAGE5_FADE:
        g_da.duration = DA_STAGE5_DUR;
        g_da.alpha    = 255;
        /* Player input resumes the moment Stage 4 ended — release the
         * existing input-gate flag now. */
        if (g_bg) g_bg->transitionSlide.actif = 0;
        if (g_tex_frame) SDL_SetTextureAlphaMod(g_tex_frame, 255);
        break;
    }
}

static void start_anim(int locked_variant)
{
    if (!g_bg) return;

    /* Honor the existing mono-mode kronole door gate the same way
     * transitionSlide_declencher did, so this can be a drop-in replacement
     * for that call. */
    if (g_bg->blockTransitionRight) return;

    /* Refuse to retrigger while running. */
    if (g_da.stage != DOOR_STAGE_IDLE) return;

    g_direction    = 1;                       /* spec is forward-only */
    g_target_wagon = g_bg->wagonActif + g_direction;
    if (g_target_wagon < 0 || g_target_wagon >= MAX_WAGONS) return;

    g_swapped              = 0;
    g_shake_fired          = 0;
    g_steam_fired          = 0;
    g_flash_timer          = 0.0f;
    g_lock_pulse_t         = 0.0f;
    g_da.is_locked_variant = locked_variant ? 1 : 0;

    /* Hijack the existing input gate.  joueur_update_interieur returns at
     * background.c:831 whenever transitionSlide.actif is set, which is
     * exactly the gate the spec asked us to reuse. */
    g_bg->transitionSlide.actif      = 1;
    g_bg->transitionSlide.direction  = g_direction;
    g_bg->transitionSlide.wagonCible = g_target_wagon;
    g_bg->transitionSlide.offset     = 0.0f;

    stage_enter(DOOR_STAGE1_SIDES);
}

/* --------------------------------------------------------------- API --- */
int door_anim_init(SDL_Renderer  *renderer,
                   UIFontPack    *fonts,
                   UITextCache   *cache,
                   Background    *bg,
                   GameplayState *gp,
                   AudioSystem   *audio)
{
    memset(&g_da, 0, sizeof(g_da));
    g_renderer = renderer;
    g_fonts    = fonts;
    g_cache    = cache;
    g_bg       = bg;
    g_gp       = gp;
    g_audio    = audio;

    g_tex_side  = load_tex(renderer, "assets/door/door_side.png",  &g_side_w,  &g_side_h);
    g_tex_front = load_tex(renderer, "assets/door/door_front.png", &g_front_w, &g_front_h);
    g_tex_frame = load_tex(renderer, "assets/door/door_frame.png", &g_frame_w, &g_frame_h);

    /* Reasonable side-strip width fallback if the asset failed to load,
     * so the rest of the math doesn't divide by zero. */
    if (g_side_w  <= 0) g_side_w  = 96;
    if (g_side_h  <= 0) g_side_h  = H_ECRAN;
    if (g_front_w <= 0) g_front_w = W_ECRAN;
    if (g_front_h <= 0) g_front_h = H_ECRAN;
    if (g_frame_w <= 0) g_frame_w = W_ECRAN;
    if (g_frame_h <= 0) g_frame_h = H_ECRAN;

    stage_enter(DOOR_STAGE_IDLE);
    return (g_tex_side && g_tex_front && g_tex_frame) ? 0 : -1;
}

void door_anim_free(void)
{
    if (g_tex_side)  SDL_DestroyTexture(g_tex_side);
    if (g_tex_front) SDL_DestroyTexture(g_tex_front);
    if (g_tex_frame) SDL_DestroyTexture(g_tex_frame);
    g_tex_side = g_tex_front = g_tex_frame = NULL;
    memset(&g_da, 0, sizeof(g_da));
}

void door_anim_open(void)            { start_anim(0); }
void door_anim_close_and_lock(void)  { start_anim(1); }

int door_anim_active(void)
{
    return g_da.stage != DOOR_STAGE_IDLE;
}

int door_anim_blocks_input(void)
{
    return (g_da.stage >= DOOR_STAGE1_SIDES && g_da.stage <= DOOR_STAGE4_OPEN);
}

/* ------------------------------------------------------------ update --- */
void door_anim_update(float dt)
{
    if (g_da.stage == DOOR_STAGE_IDLE) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;          /* cap a long frame */

    /* Keep the hijacked transitionSlide offset pinned to 0.  The main loop
     * already called transitionSlide_update() this frame (which advanced
     * offset by +30); reset it here, before kinetic reads it for camera
     * pan and before it can drift across the W_ECRAN swap threshold. */
    if (g_bg) {
        g_bg->transitionSlide.offset = 0.0f;
        /* If something else cleared actif on us mid-anim (e.g., a save
         * load), reassert the gate — but only while we still need it. */
        if (door_anim_blocks_input()) g_bg->transitionSlide.actif = 1;
    }

    g_da.timer += dt;

    switch (g_da.stage) {
    case DOOR_STAGE1_SIDES: {
        /* Cubic ease-in slide toward the screen center. */
        float t = ease_in_cubic(g_da.timer / g_da.duration);
        g_da.left_x  = -(float)g_side_w + t * (float)g_side_w;
        g_da.right_x = (float)W_ECRAN  - t * (float)g_side_w;
        if (g_da.timer >= g_da.duration) {
            /* "On the frame they both reach their target: screen shake" — fire
             * the shake before transitioning so the slam carries into Stage 2. */
            if (!g_shake_fired && g_gp) {
                gameplay_trigger_shake(g_gp, DA_SHAKE_AMP, DA_SHAKE_DUR);
                g_shake_fired = 1;
            }
            stage_enter(DOOR_STAGE2_SLAM);
        }
    } break;

    case DOOR_STAGE2_SLAM: {
        g_flash_timer += dt;
        if (g_da.timer >= g_da.duration) stage_enter(DOOR_STAGE3_TITLE);
    } break;

    case DOOR_STAGE3_TITLE:
        g_lock_pulse_t += dt;
        if (g_da.timer >= g_da.duration) stage_enter(DOOR_STAGE4_OPEN);
        break;

    case DOOR_STAGE4_OPEN: {
        float t   = ease_out_cubic(g_da.timer / g_da.duration);
        float off = t * (float)(W_ECRAN / 2);
        g_da.left_x  = -off;
        g_da.right_x = (float)(W_ECRAN / 2) + off;
        if (!g_steam_fired) {
            spawn_steam_burst((float)(W_ECRAN / 2), (float)(H_ECRAN / 2));
            g_steam_fired = 1;
        }
        if (g_da.timer >= g_da.duration) stage_enter(DOOR_STAGE5_FADE);
    } break;

    case DOOR_STAGE5_FADE: {
        float t = clamp01(g_da.timer / g_da.duration);
        g_da.alpha = (int)((1.0f - t) * 255.0f);
        if (g_da.alpha < 0)   g_da.alpha = 0;
        if (g_da.alpha > 255) g_da.alpha = 255;
        if (g_da.timer >= g_da.duration) stage_enter(DOOR_STAGE_IDLE);
    } break;

    default: break;
    }
}

/* ------------------------------------------------------------ render --- */
static void draw_full(SDL_Texture *t)
{
    if (!t || !g_renderer) return;
    SDL_Rect dst = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderCopy(g_renderer, t, NULL, &dst);
}

static void render_sides(void)
{
    if (!g_tex_side || !g_renderer) return;
    SDL_Rect dst_l = { (int)g_da.left_x,  0, g_side_w, H_ECRAN };
    SDL_Rect dst_r = { (int)g_da.right_x, 0, g_side_w, H_ECRAN };
    SDL_RenderCopy(g_renderer, g_tex_side, NULL, &dst_l);
    /* Right copy is the same strip, mirrored, so the "edge" reads correctly
     * coming in from the right.  SDL_RenderCopyEx with FLIP_HORIZONTAL. */
    SDL_RenderCopyEx(g_renderer, g_tex_side, NULL, &dst_r,
                     0.0, NULL, SDL_FLIP_HORIZONTAL);
}

static void render_flash_overlay(void)
{
    if (g_flash_timer >= DA_FLASH_DUR) return;
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    /* Optional: ease the flash alpha down so it doesn't pop off. */
    float k = clamp01(1.0f - (g_flash_timer / DA_FLASH_DUR));
    int   a = (int)(DA_FLASH_ALPHA * k);
    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, (Uint8)a);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(g_renderer, &full);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static void render_title(void)
{
    if (!g_fonts || !g_cache || !g_renderer) return;

    /* Alpha envelope: in 0.30s, hold 0.70s, out 0.20s — sums to STAGE3_DUR. */
    float t = g_da.timer;
    float a01;
    if      (t <  DA_TITLE_FADE_IN)
        a01 = t / DA_TITLE_FADE_IN;
    else if (t < (DA_STAGE3_DUR - DA_TITLE_FADE_OUT))
        a01 = 1.0f;
    else
        a01 = clamp01((DA_STAGE3_DUR - t) / DA_TITLE_FADE_OUT);
    a01 = clamp01(a01);
    int a = (int)(a01 * 255.0f);

    char line1[64], line2[64];
    build_title_for_current_wagon(line1, sizeof(line1),
                                   line2, sizeof(line2));

    SDL_Color c1 = DA_ACCENT;       c1.a = (Uint8)a;
    SDL_Color c2 = DA_ACCENT_SOFT;  c2.a = (Uint8)a;

    TTF_Font *fBig   = g_fonts->bebasTitle ? g_fonts->bebasTitle : g_fonts->fallback;
    TTF_Font *fSmall = g_fonts->mono       ? g_fonts->mono       : g_fonts->fallback;

    int big_y   = H_ECRAN / 2 - 36;
    int small_y = H_ECRAN / 2 + 22;

    ui_draw_text_centered(g_cache, g_renderer, fBig,   line1, c1, W_ECRAN/2, big_y);
    ui_draw_text_centered(g_cache, g_renderer, fSmall, line2, c2, W_ECRAN/2, small_y);
}

static void render_locked_pulse_color(void)
{
    /* 4 Hz cycle between (255,40,40) and (80,20,20). */
    float phase = 0.5f + 0.5f * sinf(2.0f * (float)M_PI * DA_LOCKED_PULSE_HZ * g_lock_pulse_t);
    Uint8 r = (Uint8)(80 + (255 - 80) * phase);
    Uint8 g = (Uint8)(20 + ( 40 -  20) * phase);
    Uint8 b = (Uint8)(20 + ( 40 -  20) * phase);
    if (g_tex_front) SDL_SetTextureColorMod(g_tex_front, r, g, b);
}

static void render_split_halves(void)
{
    if (!g_tex_front || !g_renderer) return;

    /* The two halves come from the source texture's left/right 50% — letting
     * the original art's seam align naturally. */
    int half_src_w = g_front_w / 2;
    int half_dst_w = W_ECRAN  / 2;

    SDL_Rect src_l = { 0,          0, half_src_w, g_front_h };
    SDL_Rect src_r = { half_src_w, 0, g_front_w - half_src_w, g_front_h };

    SDL_Rect dst_l = { (int)g_da.left_x,  0, half_dst_w, H_ECRAN };
    SDL_Rect dst_r = { (int)g_da.right_x, 0, W_ECRAN - half_dst_w, H_ECRAN };

    SDL_RenderCopy(g_renderer, g_tex_front, &src_l, &dst_l);
    SDL_RenderCopy(g_renderer, g_tex_front, &src_r, &dst_r);
}

void door_anim_render(void)
{
    if (!g_renderer)               return;
    if (g_da.stage == DOOR_STAGE_IDLE) return;

    switch (g_da.stage) {

    case DOOR_STAGE1_SIDES:
        /* Game world stays visible between the two side strips. */
        render_sides();
        break;

    case DOOR_STAGE2_SLAM:
        /* Front panel slammed in, fills screen.  Then the white flash. */
        if (g_tex_front) {
            SDL_SetTextureColorMod(g_tex_front, 255, 255, 255);
            SDL_SetTextureAlphaMod(g_tex_front, 255);
            draw_full(g_tex_front);
        }
        render_flash_overlay();
        break;

    case DOOR_STAGE3_TITLE:
        if (g_da.is_locked_variant) {
            /* Locked variant: pulse the front panel red, no title text. */
            render_locked_pulse_color();
            if (g_tex_front) draw_full(g_tex_front);
            /* Reset color mod here in case render() is the last call this
             * frame — Stage 4 also resets on entry, this is belt+braces. */
        } else {
            /* Normal: static front + animated title overlay. */
            if (g_tex_front) {
                SDL_SetTextureColorMod(g_tex_front, 255, 255, 255);
                draw_full(g_tex_front);
            }
            render_title();
        }
        break;

    case DOOR_STAGE4_OPEN:
        /* Frame underneath first, then the splitting halves on top. */
        if (g_tex_frame) {
            SDL_SetTextureAlphaMod(g_tex_frame, 255);
            draw_full(g_tex_frame);
        }
        render_split_halves();
        break;

    case DOOR_STAGE5_FADE:
        if (g_tex_frame && g_da.alpha > 0) {
            SDL_SetTextureAlphaMod(g_tex_frame, (Uint8)g_da.alpha);
            draw_full(g_tex_frame);
        }
        /* When alpha hits 0 we render nothing — the new wagon is fully
         * visible underneath, exactly as the spec describes. */
        break;

    default: break;
    }
}

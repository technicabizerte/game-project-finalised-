#include "char_anim.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>

/* ---------- string tables --------------------------------------------- */

static const char *kSkinNames[SKIN_COUNT] = {
    [SKIN_CURTIS]   = "curtis",
    [SKIN_CURTIS1]  = "curtis1",
    [SKIN_ELLIE]    = "ellie",
    [SKIN_NAMGOONG] = "namgoong",
};

static const char *kWeaponNames[WEAPON_COUNT] = {
    [WEAPON_AXE]   = "axe",
    [WEAPON_SWORD] = "sword",
};

static const char *kAnimNames[ANIM_COUNT] = {
    [ANIM_IDLE]      = "idle",
    [ANIM_RUN]       = "run",
    [ANIM_JUMP]      = "jump",
    [ANIM_ATTACK]    = "attack",
    [ANIM_HIT_REACT] = "hit_react",
    [ANIM_DEATH]     = "death",
    [ANIM_GUN]       = "gun",
};

const char *char_skin_name  (Skin     s) { return (s < SKIN_COUNT)   ? kSkinNames[s]   : "?"; }
const char *char_weapon_name(Weapon   w) { return (w < WEAPON_COUNT) ? kWeaponNames[w] : "?"; }
const char *char_anim_name  (AnimType a) { return (a < ANIM_COUNT)   ? kAnimNames[a]   : "?"; }

/* ---------- per-anim metadata ----------------------------------------- */

const AnimMeta CHAR_ANIM_META[ANIM_COUNT] = {
    [ANIM_IDLE]      = {5, 5, 25, 0.10f, PLAY_LOOP},
    [ANIM_RUN]       = {5, 5, 25, 0.07f, PLAY_LOOP},
    /* JUMP is LOOP (was ONCE_HOLD with 2 s lifetime).  ONCE_HOLD locked
     * char_anim for the full 2 s, which was longer than any actual jump
     * arc — so attacks were blocked + the sprite stuck on the last
     * jump/land frame after touchdown.  LOOP doesn't lock, attacks /
     * IDLE / RUN can swap in on landing, and the airborne branch's
     * per-frame char_anim_set(ANIM_JUMP) is a no-op while looping.
     * 0.045 s × 25 ≈ 1.1 s per cycle — faster + smoother landing.    */
    [ANIM_JUMP]      = {5, 5, 25, 0.045f, PLAY_LOOP},
    /* ATTACK plays the first 18 frames of the 5x5 sheet — windup +
     * impact (frame 14) + a couple of recovery frames.  The trailing
     * return-to-idle frames are dropped so the swing feels snappier;
     * actual per-frame timing is overridden per-weapon below.       */
    [ANIM_ATTACK]    = {5, 5, 18, 0.04f, PLAY_ONCE_HOLD},
    /* HIT_REACT plays only the first ~half of the sheet at double speed
     * so getting hit doesn't lock attacks/shoot for ~1.75s.  12 * 0.03 =
     * 0.36s — long enough to read the stagger, short enough to recover. */
    [ANIM_HIT_REACT] = {5, 5, 12, 0.030f, PLAY_ONCE_HOLD},
    [ANIM_DEATH]     = {5, 5, 25, 0.09f, PLAY_LOCK_FOREVER},
    [ANIM_GUN]       = {5, 5, 25, 0.06f, PLAY_ONCE_HOLD},
};

/* Sword = faster + lighter; axe = slower + heavier.  attack_frame_time
 * is applied to the loaded ATTACK track so the swing visibly takes
 * longer when wielding the axe.                                       */
/* Per-weapon swing speed: axe is still heavier than sword (sword swings
 * ~1.5x faster) but both are well under a second now.
 *   sword: 18 frames * 0.020s = 0.36s total, impact at frame 14 = 0.28s
 *   axe  : 18 frames * 0.030s = 0.54s total, impact at frame 14 = 0.42s
 * Cooldowns are tuned to be at most the swing duration so chain attacks
 * are gated by the anim itself, not by an artificial wait afterwards.  */
const WeaponStats CHAR_WEAPON_STATS[WEAPON_COUNT] = {
    [WEAPON_AXE]   = {.melee_damage = 50, .melee_cooldown_ms = 500,
                      .melee_range = 60, .attack_frame_time = 0.030f},
    [WEAPON_SWORD] = {.melee_damage = 25, .melee_cooldown_ms = 300,
                      .melee_range = 50, .attack_frame_time = 0.020f},
};

/* ---------- loader helpers -------------------------------------------- */

/* Pick the smallest cols in {5, 7, 8} that divides side evenly.
 * Our inventory has 1280, 3584, 4096 px sheets which map unambiguously
 * (1280→5, 3584→7, 4096→8) in this order.                              */
static int resolve_grid(int side, int fallback)
{
    static const int candidates[] = {5, 7, 8};
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        int c = candidates[i];
        if (side > 0 && side % c == 0) return c;
    }
    return fallback;
}

static void track_clear(CharAnimTrack *t)
{
    if (!t) return;
    if (t->texture) SDL_DestroyTexture(t->texture);
    memset(t, 0, sizeof(*t));
}

static void track_load(CharAnimTrack *t,
                       SDL_Renderer  *r,
                       const char    *path,
                       const AnimMeta meta)
{
    track_clear(t);

    SDL_Texture *tex = IMG_LoadTexture(r, path);
    if (!tex) {
        fprintf(stderr, "[char_anim] failed to load '%s': %s\n",
                path, IMG_GetError());
        t->frame_time = meta.frame_time;
        t->mode       = meta.mode;
        return;
    }

    int w = 0, h = 0;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);

    /* Auto-resolve cols/rows from texture size (square frames assumed).
     * If autodetect fails we fall back to the meta's typical grid.    */
    int cols = resolve_grid(w, meta.cols);
    int rows = resolve_grid(h, meta.rows);

    t->texture     = tex;
    t->cols        = cols;
    t->rows        = rows;
    t->frame_w     = w / cols;
    t->frame_h     = h / rows;
    t->frame_count = cols * rows;
    if (meta.frame_count > 0 && meta.frame_count < t->frame_count &&
        cols == meta.cols && rows == meta.rows) {
        /* Honor an explicit smaller cap only when the texture matches
         * the meta's grid exactly; otherwise the cap is meaningless. */
        t->frame_count = meta.frame_count;
    }
    t->frame_time = meta.frame_time > 0.0f ? meta.frame_time : 0.1f;
    t->mode       = meta.mode;
    /* current_frame/timer/finished left zeroed */
}

/* ---------- lifecycle ------------------------------------------------- */

bool char_anim_load(CharAnim     *ca,
                    SDL_Renderer *r,
                    Skin          skin,
                    Weapon        weapon)
{
    if (!ca || !r) return false;
    if (skin >= SKIN_COUNT || weapon >= WEAPON_COUNT) return false;

    /* free previous textures first */
    char_anim_free(ca);

    ca->skin    = skin;
    ca->weapon  = weapon;
    ca->current = ANIM_IDLE;
    ca->previous = ANIM_IDLE;

    const char *s = kSkinNames[skin];
    const char *w = kWeaponNames[weapon];

    for (int i = 0; i < ANIM_COUNT; i++) {
        char path[256];
        snprintf(path, sizeof(path),
                 "assets/sprites/characters/%s/%s/%s.png",
                 s, w, kAnimNames[i]);
        track_load(&ca->tracks[i], r, path, CHAR_ANIM_META[i]);
    }

    /* Per-weapon swing-speed override.  Axe holds longer per frame so
     * the swing reads as heavier; sword cuts faster.                  */
    float aft = CHAR_WEAPON_STATS[weapon].attack_frame_time;
    if (aft > 0.0f) ca->tracks[ANIM_ATTACK].frame_time = aft;

    {
        char path[256];
        snprintf(path, sizeof(path),
                 "assets/sprites/characters/%s/%s/preview.png", s, w);
        ca->preview = IMG_LoadTexture(r, path);
        if (!ca->preview) {
            fprintf(stderr, "[char_anim] preview missing for %s/%s: %s\n",
                    s, w, IMG_GetError());
        }
    }

    ca->loaded = (ca->tracks[ANIM_IDLE].texture != NULL);
    return ca->loaded;
}

void char_anim_free(CharAnim *ca)
{
    if (!ca) return;
    for (int i = 0; i < ANIM_COUNT; i++)
        track_clear(&ca->tracks[i]);
    if (ca->preview) {
        SDL_DestroyTexture(ca->preview);
        ca->preview = NULL;
    }
    ca->loaded    = 0;
    ca->is_locked = 0;
    ca->current   = ANIM_IDLE;
    ca->previous  = ANIM_IDLE;
}

/* ---------- state machine --------------------------------------------- */

static int locks_on_start(AnimType a)
{
    /* Any one-shot variant (ONCE_HOLD or LOCK_FOREVER) holds the lock
     * while it plays.  LOOP anims never lock.                          */
    PlayMode m = CHAR_ANIM_META[a].mode;
    return (m == PLAY_ONCE_HOLD || m == PLAY_LOCK_FOREVER);
}

static void track_reset(CharAnimTrack *t)
{
    if (!t) return;
    t->current_frame = 0;
    t->timer         = 0.0f;
    t->finished      = 0;
}

static void track_tick(CharAnimTrack *t, float dt)
{
    if (!t || !t->texture || t->frame_count <= 0) return;
    if (t->finished && t->mode != PLAY_LOOP) return;

    t->timer += dt;
    while (t->timer >= t->frame_time) {
        t->timer -= t->frame_time;
        t->current_frame++;
        if (t->current_frame >= t->frame_count) {
            if (t->mode == PLAY_LOOP) {
                t->current_frame = 0;
            } else {
                t->current_frame = t->frame_count - 1;
                t->finished      = 1;
                t->timer         = 0.0f;
                return;
            }
        }
    }
}

void char_anim_set(CharAnim *ca, AnimType a)
{
    if (!ca || a >= ANIM_COUNT) return;

    /* Lock semantics:
     *   - DEATH is permanent once it owns the slot.
     *   - DEATH always overrides.
     *   - HIT_REACT can interrupt ATTACK (mid-swing stagger).
     *   - ATTACK can interrupt HIT_REACT — lets the player swing back
     *     out of stagger instead of being chain-locked by multiple
     *     enemies.                                                     */
    if (ca->is_locked) {
        if (ca->current == ANIM_DEATH) return;
        int can_override = 0;
        if (a == ANIM_DEATH) can_override = 1;
        else if (a == ANIM_HIT_REACT && ca->current == ANIM_ATTACK)
            can_override = 1;
        else if (a == ANIM_ATTACK && ca->current == ANIM_HIT_REACT)
            can_override = 1;
        if (!can_override) return;
    }

    /* Re-entering a looping state is a no-op — don't reset the timer. */
    if (ca->current == a && ca->tracks[a].mode == PLAY_LOOP) return;

    /* HIT_REACT remembers where it came from so it can auto-return.  */
    if (a == ANIM_HIT_REACT)
        ca->previous = (ca->current == ANIM_HIT_REACT) ? ca->previous
                                                      : ca->current;

    ca->current   = a;
    ca->is_locked = locks_on_start(a);
    track_reset(&ca->tracks[a]);
}

void char_anim_update(CharAnim *ca, float dt)
{
    if (!ca) return;
    CharAnimTrack *t = &ca->tracks[ca->current];
    track_tick(t, dt);
    if (!t->finished) return;

    if (ca->current == ANIM_DEATH) {
        ca->is_locked = 1;          /* permanent */
        return;
    }

    /* One-shot finished — drop the lock. */
    ca->is_locked = 0;

    /* HIT_REACT auto-returns to the prior state.                     */
    if (ca->current == ANIM_HIT_REACT) {
        AnimType back = ca->previous;
        if (back == ANIM_HIT_REACT || back == ANIM_DEATH) back = ANIM_IDLE;
        ca->current = back;
        track_reset(&ca->tracks[ca->current]);
    }
    /* ATTACK and GUN auto-return to IDLE once the visible swing /
     * fire animation finishes — otherwise the character sits frozen
     * on the last frame until the next state transition.  JUMP is
     * deliberately excluded: it's held intentionally for the airborne
     * arc and re-set every airborne frame by the caller.            */
    else if (ca->current == ANIM_ATTACK || ca->current == ANIM_GUN) {
        ca->current = ANIM_IDLE;
        track_reset(&ca->tracks[ANIM_IDLE]);
    }
}

bool char_anim_is_locked  (const CharAnim *ca) { return ca ? !!ca->is_locked : false; }
bool char_anim_is_finished(const CharAnim *ca) { return ca ? !!ca->tracks[ca->current].finished : false; }
int  char_anim_current_frame(const CharAnim *ca) { return ca ? ca->tracks[ca->current].current_frame : 0; }

bool char_anim_attack_at_impact(const CharAnim *ca)
{
    if (!ca) return false;
    if (ca->current != ANIM_ATTACK) return false;
    return ca->tracks[ANIM_ATTACK].current_frame >= CHAR_ATTACK_IMPACT_FRAME;
}

bool char_anim_gun_at_fire(const CharAnim *ca)
{
    if (!ca) return false;
    if (ca->current != ANIM_GUN) return false;
    return ca->tracks[ANIM_GUN].current_frame >= CHAR_GUN_FIRE_FRAME;
}

/* ---------- rendering ------------------------------------------------- */

static SDL_Rect track_src_rect(const CharAnimTrack *t)
{
    SDL_Rect r = {0, 0, 0, 0};
    if (!t || t->cols <= 0) return r;
    int col = t->current_frame % t->cols;
    int row = t->current_frame / t->cols;
    r.x = col * t->frame_w;
    r.y = row * t->frame_h;
    r.w = t->frame_w;
    r.h = t->frame_h;
    return r;
}

void char_anim_render(const CharAnim   *ca,
                      SDL_Renderer     *r,
                      int               center_x,
                      int               foot_y,
                      int               display_h,
                      SDL_RendererFlip  flip)
{
    if (!ca || !r) return;
    const CharAnimTrack *t = &ca->tracks[ca->current];
    if (!t->texture) return;

    /* Preserve frame aspect; height fixed, width derived.              */
    SDL_Rect src = track_src_rect(t);
    int dh = display_h > 0 ? display_h : CHAR_DISPLAY_H;
    int dw = (t->frame_h > 0)
           ? (int)((float)dh * (float)t->frame_w / (float)t->frame_h + 0.5f)
           : dh;

    /* Shift dst.y down by the foot-padding fraction so the visible
     * boots land on foot_y instead of floating above it.              */
    int foot_shift = (int)((float)dh * CHAR_FOOT_PADDING_FRAC + 0.5f);
    SDL_Rect dst = {
        center_x - dw / 2,
        foot_y   - dh + foot_shift,
        dw,
        dh
    };
    SDL_RenderCopyEx(r, t->texture, &src, &dst, 0, NULL, flip);
}

bool char_anim_current_src(const CharAnim *ca,
                           SDL_Texture   **out_tex,
                           SDL_Rect       *out_src)
{
    if (!ca || !out_tex || !out_src) return false;
    const CharAnimTrack *t = &ca->tracks[ca->current];
    if (!t->texture) return false;
    *out_tex = t->texture;
    *out_src = track_src_rect(t);
    return true;
}

#ifndef EFFECTS_H
#define EFFECTS_H

/* ---------------------------------------------------------------------------
 * effects.h -- four visual upgrades, in a single self-contained subsystem:
 *
 *   1. death explosion : 3x3 sprite shatter + 20 additive sparks + warm flash
 *   2. hit feedback    : 4-blink white additive overlay + impulse knockback
 *                        offset + 200 ms input/AI stun gate
 *   3. grounded shadow : 4 stacked semi-transparent rects under every character
 *   4. flickering torch: per-torch warm point light, built into a screen-sized
 *                        light-map texture and composited with BLENDMODE_MOD
 *
 * Design notes:
 *   - all pools are fixed-size; no malloc per frame
 *   - knockback is a render-side offset, *not* a position mutation, so we
 *     don't disturb the existing physics or the savegame struct layouts
 *   - hit-blink redraws the sprite additively; we never touch the sprite's
 *     own ColorMod outside our own scope
 *   - the lightmap is built once per frame in effects_prepare_lightmap()
 *     and composited (BLENDMODE_MOD) on top of the wagon scene via
 *     effects_composite_lightmap()
 * ------------------------------------------------------------------------- */

#include <SDL2/SDL.h>
#include "background.h"   /* anon-struct typedefs: Background, Ennemi, ... */

#define EFFECTS_MAX_DEATH_PARTICLES   64
#define EFFECTS_MAX_TORCHES           24
#define EFFECTS_TORCH_STEPS           12
#define EFFECTS_MAX_SLASHES           16
#define EFFECTS_MAX_IMPACTS            8
#define EFFECTS_MAX_SHOCKWAVES         6

#define EFFECTS_HIT_FLASH_MS         240
#define EFFECTS_HIT_BLINK_PERIOD_MS   60   /* 4 on/off cycles in 240 ms */
#define EFFECTS_STUN_MS              200

#define EFFECTS_KB_VX                  5.0f
#define EFFECTS_KB_VY                 -3.0f
#define EFFECTS_KB_FRICTION            0.82f
#define EFFECTS_KB_GRAVITY             0.6f

#define EFFECTS_DEATH_FLASH_MS        80
#define EFFECTS_DEATH_SPARK_COUNT     20

#define EFFECTS_TORCH_BASE_RADIUS    160.0f
#define EFFECTS_TORCH_RADIUS_AMP      20.0f   /* sin sweep, +/- 20 px        */
#define EFFECTS_TORCH_RADIUS_NOISE     8.0f   /* per-frame jitter, +/-       */

typedef enum {
    EFX_ENT_PLAYER = 0,
    EFX_ENT_ENEMY  = 1
} EfxEntityKind;

/* Slash-arc visual styling.  Add a new enum value + per-kind branch in
 * effects_render_slashes to support new weapons (wrench planned).      */
typedef enum {
    SLASH_SWORD = 0,
    SLASH_AXE   = 1,
    SLASH_KIND_COUNT
} SlashKind;

typedef struct {
    int    actif;
    int    isChunk;       /* 1 = textured chunk, 0 = additive spark    */
    float  x, y;          /* WORLD coords (pre-ox)                     */
    float  vx, vy;        /* px / frame                                */
    float  rot, vrot;     /* degrees                                   */
    float  life;          /* seconds remaining                         */
    float  lifeMax;
    /* chunk fields */
    SDL_Texture *tex;
    SDL_Rect     src;
    int          flip;
    int          drawW, drawH;
    /* spark fields */
    Uint8 r, g, b;
    int   size;
} DeathParticle;

typedef struct {
    int   actif;
    int   wagon;
    float x, y;           /* WORLD coords                              */
    float baseRadius;
    float phase;          /* sine-wave phase, radians                  */
} Torch;

typedef struct {
    int    actif;
    float  vx, vy;
    float  ox, oy;        /* accumulated visual offset (px)            */
    Uint32 stunUntilMs;
} Knockback;

typedef struct {
    int    actif;
    Uint8  r, g, b;
    float  life;
    float  lifeMax;
} ScreenFlash;

typedef struct {
    int       actif;
    SlashKind kind;
    float     x, y;          /* WORLD coords — arc pivot                  */
    int       dirSign;       /* +1 right, -1 left                         */
    float     life;
    float     lifeMax;
    float     tilt;          /* random angle offset, degrees (visual)     */
} SlashFX;

typedef struct {
    int    actif;
    float  x, y;             /* WORLD coords — contact point              */
    float  life, lifeMax;
    float  startSize, endSize;
    Uint8  r, g, b;
} ImpactFlash;

typedef struct {
    int    actif;
    float  x, y;             /* WORLD coords — ring centre                */
    float  life, lifeMax;
    float  startRadius, endRadius;
    Uint8  r, g, b;
    int    thickness;
} Shockwave;

typedef struct {
    DeathParticle deathPool[EFFECTS_MAX_DEATH_PARTICLES];

    Torch torches[EFFECTS_MAX_TORCHES];
    int   nbTorches;

    int   playerHitFlashMs[2];
    int   enemyHitFlashMs [MAX_ENNEMIS];
    Uint32 playerHitTick[2];          /* SDL_GetTicks when hit started */
    Uint32 enemyHitTick [MAX_ENNEMIS];

    Knockback kbPlayer[2];
    Knockback kbEnemy [MAX_ENNEMIS];

    ScreenFlash flash;

    SlashFX slashes[EFFECTS_MAX_SLASHES];
    ImpactFlash impacts[EFFECTS_MAX_IMPACTS];
    Shockwave   shocks [EFFECTS_MAX_SHOCKWAVES];

    SDL_Texture *lightMap;            /* W_ECRAN x H_ECRAN, RT-capable */
    int          lightMapOk;

    unsigned int rng;
} EffectsSystem;

/* ----- lifecycle ----------------------------------------------------- */
void effects_init  (EffectsSystem *fx, SDL_Renderer *r);
void effects_free  (EffectsSystem *fx);
void effects_update(EffectsSystem *fx, float dt);

/* Global hook so the 4 enemy-death sites can find the active instance
 * without having to thread an EffectsSystem* through every signature. */
void           effects_bind_global(EffectsSystem *fx);
EffectsSystem *effects_global     (void);

/* ----- torches ------------------------------------------------------- */
int  effects_add_torch          (EffectsSystem *fx, int wagon,
                                  float x, float y, float baseRadius);
void effects_clear_torches      (EffectsSystem *fx);
void effects_seed_default_torches(EffectsSystem *fx); /* 2 per wagon */

/* ----- hit / knockback ---------------------------------------------- */
/* dirSign is the direction of the push (+1 = pushed right, -1 = left).
 * Pass 0 to leave the entity in place (rare). */
void effects_on_hit(EffectsSystem *fx, EfxEntityKind kind, int idx, int dirSign);

int  effects_is_stunned      (const EffectsSystem *fx, EfxEntityKind kind, int idx);
void effects_get_kb_offset   (const EffectsSystem *fx, EfxEntityKind kind, int idx,
                              int *outOX, int *outOY);
int  effects_hit_flash_alpha (const EffectsSystem *fx, EfxEntityKind kind, int idx);

/* ----- death --------------------------------------------------------- */
/* hitDirSign = direction of the killing blow (+1 right, -1 left).  The
 * upgraded death effect biases chunks/sparks in that direction so the
 * death reads as a directional gib, not a radial puff.                  */
void effects_spawn_death          (EffectsSystem *fx,
                                    SDL_Texture *sheet, SDL_Rect src,
                                    int dx, int dy, int dw, int dh,
                                    int flip, int hitDirSign);
void effects_spawn_death_for_enemy(EffectsSystem *fx, const Ennemi *e,
                                    int hitDirSign);

/* Toned-down kill effect for PvP rounds — flash + sparks + small ring,
 * NO full gib.  Caller positions the burst at the loser's chest.       */
void effects_spawn_ko_finisher(EffectsSystem *fx,
                               float x, float y, int hitDirSign);

/* Standalone shockwave ring.  Used internally by the death paths and
 * by callers who want a one-off ring without the rest of the death
 * package (e.g. dash entrance, big enemy slam).                        */
void effects_spawn_shockwave(EffectsSystem *fx, float x, float y,
                             float startRadius, float endRadius,
                             float lifeSeconds,
                             Uint8 r, Uint8 g, Uint8 b,
                             int thickness);

/* ----- screen flash + sparks ---------------------------------------- */
void effects_spawn_sparks   (EffectsSystem *fx, float x, float y, int count);
void effects_trigger_flash  (EffectsSystem *fx, Uint8 r, Uint8 g, Uint8 b, int ms);

/* ----- slash arcs --------------------------------------------------- */
/* Spawn a one-shot additive crescent at (x, y) facing dirSign (+1/-1).
 * Caller picks the SlashKind (per-weapon styling) and the pivot — for a
 * humanoid swing, the pivot should land roughly at chest height in
 * front of the swinger.                                                */
void effects_spawn_slash(EffectsSystem *fx, SlashKind kind,
                         float x, float y, int dirSign);

/* ----- impact feedback (sparks + starburst flash) ------------------- */
/* Fire on every successful hit, at the world-space contact point.
 * Scales with damage: crit-tier (>= DMG_NUM_CRIT_THRESHOLD) gets more
 * sparks and a larger flash.  Does NOT trigger hitstop — caller hooks
 * hitstop_trigger() separately for player-attack sites.                */
void effects_spawn_impact(EffectsSystem *fx,
                          float x, float y,
                          int dirSign, int damage);

/* ----- stateless shadow helper -------------------------------------- */
void effects_draw_shadow(SDL_Renderer *r, int cx, int floor_y, int width);

/* ----- render passes (in call-order) -------------------------------- */
/* shadows: draw between the wagon floor and the entities                */
void effects_render_shadows      (EffectsSystem *fx, Background *bg,
                                   SDL_Renderer *r, int wagon, int ox);
/* hit-blink: draw after the entities (additive white overlay)           */
void effects_render_hit_blink    (EffectsSystem *fx, Background *bg,
                                   SDL_Renderer *r, int ox);
/* death chunks + sparks: draw after the entities and after the blink    */
void effects_render_world        (EffectsSystem *fx, SDL_Renderer *r, int ox);

/* lightmap: prepare (off-target) once per frame, then composite over
 * the current render target wherever the wagon is presented              */
void effects_prepare_lightmap    (EffectsSystem *fx, SDL_Renderer *r, int wagon);
void effects_composite_lightmap  (EffectsSystem *fx, SDL_Renderer *r,
                                   int dstX, int dstY, int dstW, int dstH);

/* full-screen warm flash: draw after the world composite, before the HUD */
void effects_render_flash        (EffectsSystem *fx, SDL_Renderer *r);

#endif

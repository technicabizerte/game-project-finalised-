#include "effects.h"
#include "background.h"
#include "combat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Tunables that don't need to live in the header.
 * ========================================================================= */
#define CHUNK_GRAVITY_PER_FRAME   0.35f
#define CHUNK_VX_FRICTION         0.99f
#define SPARK_GRAVITY_PER_FRAME   0.22f
#define SPARK_VX_FRICTION         0.96f

#define CHUNK_LIFE_MIN_S          0.65f
#define CHUNK_LIFE_RANGE_S        0.45f
#define SPARK_LIFE_MIN_S          0.30f
#define SPARK_LIFE_RANGE_S        0.35f

/* MOD blend = dst * src / 255.  Setting ambient ~160 darkens unlit areas
 * to roughly 63% brightness with a slight cool cast; torch contributions
 * additively push the lightmap toward 255 where they reach, restoring (or
 * nearly so) full brightness in the lit halo. */
#define LIGHT_AMBIENT_R          165
#define LIGHT_AMBIENT_G          158
#define LIGHT_AMBIENT_B          180

#define TORCH_R                   240
#define TORCH_G                   200
#define TORCH_B                   140

/* ===========================================================================
 * Global pointer, optional.  Lets us reach the active EffectsSystem from
 * the four enemy-death sites without having to thread it through every
 * signature in combat.c / gameplay.c / background.c.
 * ========================================================================= */
static EffectsSystem *s_global = NULL;
void           effects_bind_global(EffectsSystem *fx) { s_global = fx; }
EffectsSystem *effects_global     (void)              { return s_global; }

/* ===========================================================================
 * PRNG matching the weaponfx style (cheap LCG, no malloc).
 * ========================================================================= */
static unsigned int prng_next(EffectsSystem *fx)
{
    fx->rng = fx->rng * 1664525u + 1013904223u;
    return fx->rng >> 16;
}

/* [0..1) */
static float frand(EffectsSystem *fx)
{
    return (float)(prng_next(fx) & 0x7fff) / 32768.0f;
}

/* [-1..1) */
static float frand_centered(EffectsSystem *fx)
{
    return frand(fx) * 2.0f - 1.0f;
}

/* ===========================================================================
 * Death-particle pool: free-slot scan.
 * ========================================================================= */
static DeathParticle *get_free_particle(EffectsSystem *fx)
{
    for (int i = 0; i < EFFECTS_MAX_DEATH_PARTICLES; i++) {
        DeathParticle *p = &fx->deathPool[i];
        if (!p->actif || p->life <= 0.0f) return p;
    }
    return NULL;
}

/* ===========================================================================
 * Lifecycle
 * ========================================================================= */
void effects_init(EffectsSystem *fx, SDL_Renderer *r)
{
    memset(fx, 0, sizeof(*fx));
    fx->rng = 0xc0ffee42u ^ (unsigned int)SDL_GetTicks();

    fx->lightMap = SDL_CreateTexture(r,
                                     SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_TARGET,
                                     W_ECRAN, H_ECRAN);
    fx->lightMapOk = (fx->lightMap != NULL);
    if (fx->lightMap) {
        SDL_SetTextureBlendMode(fx->lightMap, SDL_BLENDMODE_MOD);
    } else {
        SDL_Log("[effects] light map RT unavailable: %s", SDL_GetError());
    }
}

void effects_free(EffectsSystem *fx)
{
    if (!fx) return;
    if (fx->lightMap) {
        SDL_DestroyTexture(fx->lightMap);
        fx->lightMap = NULL;
    }
    if (s_global == fx) s_global = NULL;
}

/* ===========================================================================
 * Torches
 * ========================================================================= */
int effects_add_torch(EffectsSystem *fx, int wagon, float x, float y, float baseRadius)
{
    if (fx->nbTorches >= EFFECTS_MAX_TORCHES) return -1;
    Torch *t = &fx->torches[fx->nbTorches];
    t->actif      = 1;
    t->wagon      = wagon;
    t->x          = x;
    t->y          = y;
    t->baseRadius = (baseRadius > 0.0f) ? baseRadius : EFFECTS_TORCH_BASE_RADIUS;
    t->phase      = frand(fx) * (float)(2.0 * M_PI);
    fx->nbTorches++;
    return fx->nbTorches - 1;
}

void effects_clear_torches(EffectsSystem *fx)
{
    for (int i = 0; i < EFFECTS_MAX_TORCHES; i++) fx->torches[i].actif = 0;
    fx->nbTorches = 0;
}

void effects_seed_default_torches(EffectsSystem *fx)
{
    /* Two warm-tungsten torches per wagon along the upper third of the
     * interior, far enough from the doors that walking through doesn't
     * carry the light with you. */
    effects_clear_torches(fx);
    for (int w = 0; w < MAX_WAGONS; w++) {
        effects_add_torch(fx, w, (float)(WAGON_MUR_G_X + 130),
                          (float)(WAGON_PLAF_Y + 70), EFFECTS_TORCH_BASE_RADIUS);
        effects_add_torch(fx, w, (float)(WAGON_MUR_D_X - 130),
                          (float)(WAGON_PLAF_Y + 70), EFFECTS_TORCH_BASE_RADIUS);
    }
}

/* ===========================================================================
 * Hit + knockback
 * ========================================================================= */
static void kb_apply(Knockback *kb, int dirSign)
{
    kb->actif       = 1;
    kb->vx          = EFFECTS_KB_VX * (float)((dirSign >= 0) ? 1 : -1);
    if (dirSign == 0) kb->vx = 0.0f;
    kb->vy          = EFFECTS_KB_VY;
    kb->ox          = 0.0f;
    kb->oy          = 0.0f;
    kb->stunUntilMs = SDL_GetTicks() + EFFECTS_STUN_MS;
}

void effects_on_hit(EffectsSystem *fx, EfxEntityKind kind, int idx, int dirSign)
{
    if (!fx) return;

    if (kind == EFX_ENT_PLAYER) {
        if (idx < 0 || idx >= 2) return;
        fx->playerHitFlashMs[idx] = EFFECTS_HIT_FLASH_MS;
        fx->playerHitTick[idx]    = SDL_GetTicks();
        kb_apply(&fx->kbPlayer[idx], dirSign);
    } else {
        if (idx < 0 || idx >= MAX_ENNEMIS) return;
        fx->enemyHitFlashMs[idx] = EFFECTS_HIT_FLASH_MS;
        fx->enemyHitTick[idx]    = SDL_GetTicks();
        kb_apply(&fx->kbEnemy[idx], dirSign);
    }
}

int effects_is_stunned(const EffectsSystem *fx, EfxEntityKind kind, int idx)
{
    if (!fx) return 0;
    const Knockback *kb;
    if (kind == EFX_ENT_PLAYER) {
        if (idx < 0 || idx >= 2) return 0;
        kb = &fx->kbPlayer[idx];
    } else {
        if (idx < 0 || idx >= MAX_ENNEMIS) return 0;
        kb = &fx->kbEnemy[idx];
    }
    if (!kb->actif) return 0;
    return (SDL_GetTicks() < kb->stunUntilMs) ? 1 : 0;
}

void effects_get_kb_offset(const EffectsSystem *fx, EfxEntityKind kind, int idx,
                           int *outOX, int *outOY)
{
    if (outOX) *outOX = 0;
    if (outOY) *outOY = 0;
    if (!fx) return;
    const Knockback *kb;
    if (kind == EFX_ENT_PLAYER) {
        if (idx < 0 || idx >= 2) return;
        kb = &fx->kbPlayer[idx];
    } else {
        if (idx < 0 || idx >= MAX_ENNEMIS) return;
        kb = &fx->kbEnemy[idx];
    }
    if (!kb->actif) return;
    if (outOX) *outOX = (int)kb->ox;
    if (outOY) *outOY = (int)kb->oy;
}

int effects_hit_flash_alpha(const EffectsSystem *fx, EfxEntityKind kind, int idx)
{
    if (!fx) return 0;
    int ms;
    Uint32 tick;
    if (kind == EFX_ENT_PLAYER) {
        if (idx < 0 || idx >= 2) return 0;
        ms   = fx->playerHitFlashMs[idx];
        tick = fx->playerHitTick[idx];
    } else {
        if (idx < 0 || idx >= MAX_ENNEMIS) return 0;
        ms   = fx->enemyHitFlashMs[idx];
        tick = fx->enemyHitTick[idx];
    }
    if (ms <= 0) return 0;

    /* 4 blink-on phases inside EFFECTS_HIT_FLASH_MS.  We're "on" during
     * the odd-numbered EFFECTS_HIT_BLINK_PERIOD_MS slices since hit. */
    Uint32 elapsed = SDL_GetTicks() - tick;
    int phase = (int)(elapsed / EFFECTS_HIT_BLINK_PERIOD_MS);
    if ((phase & 1) != 0) return 0;

    /* Fade toward the end so the last blink is the dimmest. */
    float t = (float)ms / (float)EFFECTS_HIT_FLASH_MS;     /* 1 -> 0 */
    if (t < 0.0f) t = 0.0f;
    int a = (int)(255.0f * (0.55f + 0.45f * t));
    if (a > 255) a = 255;
    if (a < 0)   a = 0;
    return a;
}

/* ===========================================================================
 * Shockwave ring pool — additive expanding circle outline.
 * ========================================================================= */
static Shockwave *get_free_shock(EffectsSystem *fx)
{
    for (int i = 0; i < EFFECTS_MAX_SHOCKWAVES; i++) {
        Shockwave *s = &fx->shocks[i];
        if (!s->actif) return s;
    }
    Shockwave *oldest = &fx->shocks[0];
    for (int i = 1; i < EFFECTS_MAX_SHOCKWAVES; i++)
        if (fx->shocks[i].life < oldest->life) oldest = &fx->shocks[i];
    return oldest;
}

void effects_spawn_shockwave(EffectsSystem *fx, float x, float y,
                             float startRadius, float endRadius,
                             float lifeSeconds,
                             Uint8 r, Uint8 g, Uint8 b,
                             int thickness)
{
    if (!fx) return;
    Shockwave *s = get_free_shock(fx);
    s->actif       = 1;
    s->x           = x;
    s->y           = y;
    s->life        = lifeSeconds;
    s->lifeMax     = lifeSeconds;
    s->startRadius = startRadius;
    s->endRadius   = endRadius;
    s->r = r; s->g = g; s->b = b;
    s->thickness   = thickness > 0 ? thickness : 1;
}

/* ===========================================================================
 * Death explosion — Dead Cells-style layered burst.  Layered in render
 * order:
 *   1. one-frame full-WHITE silhouette (isChunk=2, lives ~0.05 s)
 *   2. 3×3 textured chunks (existing — biased in hit direction)
 *   3. expanding shockwave ring (~0.20 s)
 *   4. 30 bright sparks biased in hit direction
 *   5. 4 longer-lived ember particles
 *   6. warm screen flash (existing)
 * Caller hooks the kill-tier hitstop / shake at the call site.
 * ========================================================================= */
static void spawn_death_sparks_dir(EffectsSystem *fx, float x, float y,
                                   int count, int dirSign)
{
    if (!fx || count <= 0) return;
    int dir = (dirSign < 0) ? -1 : 1;
    for (int n = 0; n < count; n++) {
        DeathParticle *p = get_free_particle(fx);
        if (!p) return;

        p->actif   = 1;
        p->isChunk = 0;
        p->x       = x;
        p->y       = y;

        /* Forward cone ±55° from `dir` axis, tilted up.                */
        float angBase = (dir == 1) ? 0.0f : 180.0f;
        float angDeg  = angBase + frand_centered(fx) * 55.0f - 25.0f;
        float ang     = angDeg * (float)M_PI / 180.0f;
        float spd     = 3.0f + frand(fx) * 4.0f;
        p->vx = cosf(ang) * spd;
        p->vy = sinf(ang) * spd - 0.8f;

        /* Brighter mix: white-hot center, warm rim.                    */
        if ((prng_next(fx) & 3) == 0) {
            p->r = 255; p->g = 255; p->b = 230;
        } else {
            p->r = 255; p->g = 210; p->b = 110;
        }
        p->size    = 5 + (int)(prng_next(fx) % 4);   /* was 2-4, now 5-8 */
        p->lifeMax = SPARK_LIFE_MIN_S + frand(fx) * SPARK_LIFE_RANGE_S;
        p->life    = p->lifeMax;
        p->rot     = 0.0f;
        p->vrot    = 0.0f;
        p->tex     = NULL;
        p->flip    = 0;
    }
}

static void spawn_embers(EffectsSystem *fx, float x, float y,
                         int count, int dirSign)
{
    int dir = (dirSign < 0) ? -1 : 1;
    for (int n = 0; n < count; n++) {
        DeathParticle *p = get_free_particle(fx);
        if (!p) return;
        p->actif   = 1;
        p->isChunk = 0;
        p->x       = x;
        p->y       = y;
        float angBase = (dir == 1) ? 0.0f : 180.0f;
        float angDeg  = angBase + frand_centered(fx) * 80.0f - 30.0f;
        float ang     = angDeg * (float)M_PI / 180.0f;
        float spd     = 1.2f + frand(fx) * 1.6f;
        p->vx = cosf(ang) * spd;
        p->vy = sinf(ang) * spd - 1.5f;
        /* Warm ember tones, large + long life. */
        p->r = 255; p->g = 160; p->b = 60;
        p->size    = 8 + (int)(prng_next(fx) % 4);   /* was 4-6, now 8-11 */
        p->lifeMax = 0.85f + frand(fx) * 0.55f;
        p->life    = p->lifeMax;
        p->rot = 0.0f; p->vrot = 0.0f; p->tex = NULL; p->flip = 0;
    }
}

void effects_spawn_death(EffectsSystem *fx,
                         SDL_Texture *sheet, SDL_Rect src,
                         int dx, int dy, int dw, int dh,
                         int flip, int hitDirSign)
{
    if (!fx) return;
    int dir = (hitDirSign < 0) ? -1 : (hitDirSign > 0 ? 1 : 0);

    int srcW = src.w / 3;
    int srcH = src.h / 3;
    int dstW = dw / 3;
    int dstH = dh / 3;

    /* center of the sprite in WORLD coords */
    float cxw = (float)dx + (float)dw * 0.5f;
    float cyw = (float)dy + (float)dh * 0.5f;

    /* ── Layer 1: white silhouette flash (full sprite, isChunk=2).
     * Lives ~3 frames; render path tints additive white over the
     * chunks so the kill reads as a bright pop first.                  */
    {
        DeathParticle *p = get_free_particle(fx);
        if (p) {
            p->actif   = 1;
            p->isChunk = 2;
            p->tex     = sheet;
            p->src     = src;
            p->drawW   = dw;
            p->drawH   = dh;
            p->flip    = flip;
            p->x       = (float)dx;
            p->y       = (float)dy;
            p->vx = 0.0f; p->vy = 0.0f;
            p->rot = 0.0f; p->vrot = 0.0f;
            p->lifeMax = 0.05f;
            p->life    = p->lifeMax;
        }
    }

    /* ── Layer 2: 3×3 textured chunks, velocities biased toward dir.   */
    for (int cy = 0; cy < 3; cy++) {
        for (int cx = 0; cx < 3; cx++) {
            DeathParticle *p = get_free_particle(fx);
            if (!p) goto extras;

            p->actif   = 1;
            p->isChunk = 1;
            p->tex     = sheet;
            p->src.x   = src.x + cx * srcW;
            p->src.y   = src.y + cy * srcH;
            p->src.w   = srcW;
            p->src.h   = srcH;
            p->flip    = flip;
            p->drawW   = dstW;
            p->drawH   = dstH;

            p->x = (float)dx + (float)cx * (float)dstW;
            p->y = (float)dy + (float)cy * (float)dstH;

            /* Radial offset + directional kick.  Strong push in `dir`
             * so the gib reads as the body being knocked that way.    */
            float ux  = ((float)cx - 1.0f) + frand_centered(fx) * 0.4f;
            float uy  = ((float)cy - 1.0f) + frand_centered(fx) * 0.4f - 0.55f;
            float len = sqrtf(ux*ux + uy*uy);
            if (len < 0.1f) len = 0.1f;
            float spd = 2.4f + frand(fx) * 2.6f;
            float bias = (float)dir * 2.6f;       /* directional kick */
            p->vx = ux / len * spd + bias;
            p->vy = uy / len * spd;

            p->rot     = 0.0f;
            p->vrot    = frand_centered(fx) * 6.0f;
            p->lifeMax = CHUNK_LIFE_MIN_S + frand(fx) * CHUNK_LIFE_RANGE_S;
            p->life    = p->lifeMax;
        }
    }
extras:
    /* ── Layer 3: shockwave ring.                                      */
    effects_spawn_shockwave(fx, cxw, cyw,
                            10.0f, 140.0f, 0.28f,   /* was 6→64, 0.20 */
                            255, 240, 200,
                            4);                     /* was 2 */
    /* ── Layer 4: brighter / more numerous additive sparks (biased).   */
    spawn_death_sparks_dir(fx, cxw, cyw,
                           EFFECTS_DEATH_SPARK_COUNT + 12, dir);
    /* ── Layer 5: a few large longer-lived embers.                     */
    spawn_embers(fx, cxw, cyw, 4, dir);
    /* ── Layer 6: existing warm screen flash.                          */
    effects_trigger_flash(fx, 255, 220, 180, EFFECTS_DEATH_FLASH_MS);
}

void effects_spawn_death_for_enemy(EffectsSystem *fx, const Ennemi *e,
                                   int hitDirSign)
{
    if (!fx || !e || !e->anim.texture) return;

    /* Mirror combat.c::ennemis_afficher so the chunks line up exactly with
     * where the sprite was just drawn. */
    int dw, dh, hw, hh;
    switch (e->type) {
        case ET_GUARD:    dw=GUARD_DRAW_W;    dh=GUARD_DRAW_H;
                          hw=GUARD_HBW;       hh=GUARD_HBH;       break;
        case ET_WORKER:   dw=WORKER_DRAW_W;   dh=WORKER_DRAW_H;
                          hw=WORKER_HBW;      hh=WORKER_HBH;      break;
        case ET_ENFORCER:
        default:          dw=ENFORCER_DRAW_W; dh=ENFORCER_DRAW_H;
                          hw=ENFORCER_HBW;    hh=ENFORCER_HBH;    break;
    }

    SDL_Rect src = animation_get_src_rect(&e->anim);
    int dx = (int)e->x + hw/2 - dw/2;
    int dy = (int)e->y + hh - dh;
    int flip = (e->direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

    effects_spawn_death(fx, e->anim.texture, src, dx, dy, dw, dh, flip,
                        hitDirSign);
}

/* ===========================================================================
 * PvP KO finisher — toned-down death (flash + sparks + small ring, no gib).
 * Wired by versus.c at the round-losing hit, gated by VERSUS_KO_FINISHER.
 * ========================================================================= */
void effects_spawn_ko_finisher(EffectsSystem *fx,
                               float x, float y, int hitDirSign)
{
    if (!fx) return;
    int dir = (hitDirSign < 0) ? -1 : 1;
    /* Bright sparks + embers, no chunks, smaller flash. */
    spawn_death_sparks_dir(fx, x, y, 18, dir);
    spawn_embers          (fx, x, y, 3,  dir);
    effects_spawn_shockwave(fx, x, y,
                            8.0f, 90.0f, 0.24f,    /* was 4→40, 0.18 */
                            255, 230, 180, 3);     /* was 2 */
    effects_trigger_flash(fx, 255, 200, 130, 80);
}

/* ===========================================================================
 * Sparks + flash (also used externally)
 * ========================================================================= */
void effects_spawn_sparks(EffectsSystem *fx, float x, float y, int count)
{
    if (!fx || count <= 0) return;
    for (int n = 0; n < count; n++) {
        DeathParticle *p = get_free_particle(fx);
        if (!p) return;

        p->actif   = 1;
        p->isChunk = 0;
        p->x       = x;
        p->y       = y;

        /* upper hemisphere bias: angle in [-160, -20] deg from +x (i.e.
         * mostly upward).  Cosine for x, sine for y -- screen y is down,
         * so negative sine = up. */
        float angDeg = -160.0f + frand(fx) * 140.0f;
        float ang    = angDeg * (float)M_PI / 180.0f;
        float spd    = 2.0f + frand(fx) * 4.0f;
        p->vx = cosf(ang) * spd;
        p->vy = sinf(ang) * spd;

        /* orange / white mix */
        if ((prng_next(fx) & 3) == 0) {
            p->r = 255; p->g = 255; p->b = 230;   /* white-hot */
        } else {
            p->r = 255; p->g = 200; p->b = 90;    /* warm orange */
        }
        p->size    = 1 + (int)(prng_next(fx) % 3);
        p->lifeMax = SPARK_LIFE_MIN_S + frand(fx) * SPARK_LIFE_RANGE_S;
        p->life    = p->lifeMax;
        p->rot     = 0.0f;
        p->vrot    = 0.0f;
        p->tex     = NULL;
        p->flip    = 0;
    }
}

void effects_trigger_flash(EffectsSystem *fx, Uint8 r, Uint8 g, Uint8 b, int ms)
{
    if (!fx) return;
    fx->flash.actif   = 1;
    fx->flash.r       = r;
    fx->flash.g       = g;
    fx->flash.b       = b;
    fx->flash.lifeMax = (float)ms / 1000.0f;
    fx->flash.life    = fx->flash.lifeMax;
}

/* ===========================================================================
 * Impact flash pool — additive starburst at the contact point.  Lives
 * inside a pooled fixed-size array so a hit-storm never allocates.
 * ========================================================================= */
static ImpactFlash *get_free_impact(EffectsSystem *fx)
{
    for (int i = 0; i < EFFECTS_MAX_IMPACTS; i++) {
        ImpactFlash *p = &fx->impacts[i];
        if (!p->actif) return p;
    }
    /* Oldest-wins overwrite so chained hits don't drop on the floor. */
    ImpactFlash *oldest = &fx->impacts[0];
    for (int i = 1; i < EFFECTS_MAX_IMPACTS; i++)
        if (fx->impacts[i].life < oldest->life) oldest = &fx->impacts[i];
    return oldest;
}

void effects_spawn_impact(EffectsSystem *fx, float x, float y,
                          int dirSign, int damage)
{
    if (!fx) return;
    int dir = (dirSign < 0) ? -1 : 1;
    int crit = (damage >= 50);          /* matches DMG_NUM_CRIT_THRESHOLD */

    /* 1) Direction-biased sparks reusing the DeathParticle pool.       */
    int count = crit ? 12 : 8;
    for (int n = 0; n < count; n++) {
        DeathParticle *p = get_free_particle(fx);
        if (!p) break;

        p->actif   = 1;
        p->isChunk = 0;
        p->x       = x;
        p->y       = y;

        /* Forward cone: angle clustered ±50 deg from `dir` axis, with
         * a slight upward lift so sparks "spit" off the impact.        */
        float angBaseDeg = (dir == 1) ? 0.0f : 180.0f;
        float angDeg     = angBaseDeg + frand_centered(fx) * 50.0f
                                       - 18.0f;     /* tilt upward */
        float ang        = angDeg * (float)M_PI / 180.0f;
        float spd        = 2.5f + frand(fx) * 4.5f;
        p->vx = cosf(ang) * spd;
        p->vy = sinf(ang) * spd - 0.4f;             /* extra upward kick */

        /* Crits trend white-hot; normal hits stay warm orange.         */
        if (crit || (prng_next(fx) & 3) == 0) {
            p->r = 255; p->g = 245; p->b = 200;
        } else {
            p->r = 255; p->g = 190; p->b = 80;
        }
        p->size    = 5 + (int)(prng_next(fx) % 4);    /* was 2-4, now 5-8 */
        p->lifeMax = 0.30f + frand(fx) * 0.22f;
        p->life    = p->lifeMax;
        p->rot     = 0.0f;
        p->vrot    = 0.0f;
        p->tex     = NULL;
        p->flip    = 0;
    }

    /* 2) Starburst flash — single pooled instance with scale-up + fade. */
    ImpactFlash *fl = get_free_impact(fx);
    fl->actif   = 1;
    fl->x       = x;
    fl->y       = y;
    fl->life    = 0.09f;
    fl->lifeMax = fl->life;
    fl->startSize = crit ? 18.0f : 12.0f;          /* was 10/6  */
    fl->endSize   = crit ? 72.0f : 50.0f;          /* was 36/22 */
    if (crit) { fl->r = 255; fl->g = 240; fl->b = 200; }
    else      { fl->r = 255; fl->g = 220; fl->b = 130; }
}

/* ===========================================================================
 * Slash arc pool — additive procedural crescents, one per swing impact.
 * Free-slot scan with oldest-wins overwrite so a flurry of swings never
 * drops on the floor.
 * ========================================================================= */
void effects_spawn_slash(EffectsSystem *fx, SlashKind kind,
                         float x, float y, int dirSign)
{
    if (!fx) return;
    SlashFX *s = NULL;
    for (int i = 0; i < EFFECTS_MAX_SLASHES; i++) {
        if (!fx->slashes[i].actif) { s = &fx->slashes[i]; break; }
    }
    if (!s) {
        s = &fx->slashes[0];
        for (int i = 1; i < EFFECTS_MAX_SLASHES; i++)
            if (fx->slashes[i].life < s->life) s = &fx->slashes[i];
    }
    /* Per-kind lifetime: axe is heavier/slower so its arc lingers a
     * touch longer than the snappier sword.                          */
    float lifeS = (kind == SLASH_AXE) ? 0.14f : 0.10f;
    s->actif   = 1;
    s->kind    = kind;
    s->x       = x;
    s->y       = y;
    s->dirSign = (dirSign < 0) ? -1 : 1;
    s->life    = lifeS;
    s->lifeMax = lifeS;
    s->tilt    = frand_centered(fx) * 8.0f;   /* +/- 8 deg variety */
}

/* ===========================================================================
 * Per-frame integration
 * ========================================================================= */
static void update_knockback(Knockback *kb, float dt)
{
    if (!kb->actif) return;
    /* px/frame physics: assume ~60 fps step.  We scale by (dt*60) so the
     * physics stays roughly identical at any reasonable frame rate. */
    float steps = dt * 60.0f;
    if (steps > 4.0f) steps = 4.0f;        /* clamp on long stalls */
    kb->vx *= powf(EFFECTS_KB_FRICTION, steps);
    kb->vy += EFFECTS_KB_GRAVITY * steps;
    kb->ox += kb->vx * steps;
    kb->oy += kb->vy * steps;

    /* settle: once the entity is back near zero offset and stun expired,
     * release the slot. */
    if (SDL_GetTicks() >= kb->stunUntilMs) {
        kb->ox *= 0.85f;
        kb->oy *= 0.85f;
        if (fabsf(kb->ox) < 0.5f && fabsf(kb->oy) < 0.5f &&
            fabsf(kb->vx) < 0.3f && fabsf(kb->vy) < 0.3f) {
            kb->actif = 0;
            kb->ox = kb->oy = kb->vx = kb->vy = 0.0f;
        }
    }
}

void effects_update(EffectsSystem *fx, float dt)
{
    if (!fx) return;

    /* death-particle pool */
    for (int i = 0; i < EFFECTS_MAX_DEATH_PARTICLES; i++) {
        DeathParticle *p = &fx->deathPool[i];
        if (!p->actif) continue;

        if (p->isChunk) {
            p->vy += CHUNK_GRAVITY_PER_FRAME;
            p->vx *= CHUNK_VX_FRICTION;
            p->rot += p->vrot;
        } else {
            p->vy += SPARK_GRAVITY_PER_FRAME;
            p->vx *= SPARK_VX_FRICTION;
        }
        p->x += p->vx;
        p->y += p->vy;
        p->life -= dt;
        if (p->life <= 0.0f) p->actif = 0;
    }

    /* hit-flash timers */
    int dms = (int)(dt * 1000.0f);
    if (dms < 1) dms = 1;
    for (int i = 0; i < 2; i++)
        if (fx->playerHitFlashMs[i] > 0)
            fx->playerHitFlashMs[i] -= dms;
    for (int i = 0; i < MAX_ENNEMIS; i++)
        if (fx->enemyHitFlashMs[i] > 0)
            fx->enemyHitFlashMs[i] -= dms;

    /* knockbacks */
    for (int i = 0; i < 2; i++)            update_knockback(&fx->kbPlayer[i], dt);
    for (int i = 0; i < MAX_ENNEMIS; i++)  update_knockback(&fx->kbEnemy[i],  dt);

    /* screen flash */
    if (fx->flash.actif) {
        fx->flash.life -= dt;
        if (fx->flash.life <= 0.0f) fx->flash.actif = 0;
    }

    /* slash arcs: simple lifetime decay, no physics */
    for (int i = 0; i < EFFECTS_MAX_SLASHES; i++) {
        SlashFX *s = &fx->slashes[i];
        if (!s->actif) continue;
        s->life -= dt;
        if (s->life <= 0.0f) s->actif = 0;
    }

    /* impact flashes: lifetime decay (size + alpha read at render). */
    for (int i = 0; i < EFFECTS_MAX_IMPACTS; i++) {
        ImpactFlash *p = &fx->impacts[i];
        if (!p->actif) continue;
        p->life -= dt;
        if (p->life <= 0.0f) p->actif = 0;
    }

    /* shockwave rings: lifetime decay only. */
    for (int i = 0; i < EFFECTS_MAX_SHOCKWAVES; i++) {
        Shockwave *s = &fx->shocks[i];
        if (!s->actif) continue;
        s->life -= dt;
        if (s->life <= 0.0f) s->actif = 0;
    }

    /* torch phases drift slowly for a low-frequency wobble */
    for (int i = 0; i < fx->nbTorches; i++) {
        Torch *t = &fx->torches[i];
        if (!t->actif) continue;
        t->phase += dt * 3.2f;   /* ~0.5 Hz */
    }
}

/* ===========================================================================
 * Shadow primitive: 4 stacked dark rects, flat ellipse silhouette.
 * ========================================================================= */
void effects_draw_shadow(SDL_Renderer *r, int cx, int floor_y, int width)
{
    if (width < 6) width = 6;
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 80);

    const int steps[4][2] = {
        /* {w_delta, h} from widest at the bottom up to thin top */
        {  0, 4 },
        {  4, 3 },
        {  8, 2 },
        { 12, 1 },
    };
    int y = floor_y;
    for (int i = 0; i < 4; i++) {
        int w = width - steps[i][0];
        int h = steps[i][1];
        if (w < 2) continue;
        SDL_Rect rc = { cx - w / 2, y - h, w, h };
        SDL_RenderFillRect(r, &rc);
        y -= h;
    }
    SDL_SetRenderDrawBlendMode(r, prev);
}

/* ===========================================================================
 * Shadow render pass: iterates the wagon's entities.
 * ========================================================================= */
void effects_render_shadows(EffectsSystem *fx, Background *bg,
                            SDL_Renderer *r, int wagon, int ox)
{
    (void)fx;
    int floor_y = WAGON_SOL_Y - 2;

    /* enemies first (drawn under, regardless of stacking) */
    for (int i = 0; i < bg->nbEnnemis; i++) {
        const Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != wagon || e->etat == ES_DEAD) continue;
        int hw = ennemi_hbw(e);
        /* shadow padding bumped 16 -> 18 to scale with the 1.15x larger
         * character hitboxes (see combat.h). */
        int width = hw + 18;
        int cx = (int)e->x + hw / 2 + ox;
        effects_draw_shadow(r, cx, floor_y, width);
    }

    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        const Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        int cx = (int)j->x + JOUEUR_W / 2 + ox;
        /* shadow padding bumped 14 -> 16 to scale with the 1.15x larger
         * JOUEUR_W (see background.h). */
        effects_draw_shadow(r, cx, floor_y, JOUEUR_W + 16);
    }
}

/* ===========================================================================
 * Hit-blink render pass: additive bright "ghost" sprite redraw.
 * ========================================================================= */
void effects_render_hit_blink(EffectsSystem *fx, Background *bg,
                              SDL_Renderer *r, int ox)
{
    if (!fx) return;

    /* enemies */
    for (int i = 0; i < bg->nbEnnemis; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != bg->wagonActif) continue;
        int a = effects_hit_flash_alpha(fx, EFX_ENT_ENEMY, i);
        if (a <= 0 || !e->anim.texture) continue;

        int dw, dh, hw, hh;
        switch (e->type) {
            case ET_GUARD:    dw=GUARD_DRAW_W;    dh=GUARD_DRAW_H;
                              hw=GUARD_HBW;       hh=GUARD_HBH;       break;
            case ET_WORKER:   dw=WORKER_DRAW_W;   dh=WORKER_DRAW_H;
                              hw=WORKER_HBW;      hh=WORKER_HBH;      break;
            case ET_ENFORCER:
            default:          dw=ENFORCER_DRAW_W; dh=ENFORCER_DRAW_H;
                              hw=ENFORCER_HBW;    hh=ENFORCER_HBH;    break;
        }
        int kbx = 0, kby = 0;
        effects_get_kb_offset(fx, EFX_ENT_ENEMY, i, &kbx, &kby);

        SDL_Rect src = animation_get_src_rect(&e->anim);
        SDL_Rect dst = { (int)e->x + ox + hw/2 - dw/2 + kbx,
                         (int)e->y + hh - dh + kby,
                         dw, dh };
        SDL_RendererFlip flip = (e->direction == -1) ? SDL_FLIP_HORIZONTAL
                                                     : SDL_FLIP_NONE;

        SDL_BlendMode prevB;
        SDL_GetTextureBlendMode(e->anim.texture, &prevB);
        Uint8 cr, cg, cb, ca;
        SDL_GetTextureColorMod(e->anim.texture, &cr, &cg, &cb);
        SDL_GetTextureAlphaMod(e->anim.texture, &ca);

        SDL_SetTextureBlendMode(e->anim.texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod (e->anim.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod (e->anim.texture, (Uint8)a);
        SDL_RenderCopyEx(r, e->anim.texture, &src, &dst, 0, NULL, flip);

        SDL_SetTextureBlendMode(e->anim.texture, prevB);
        SDL_SetTextureColorMod (e->anim.texture, cr, cg, cb);
        SDL_SetTextureAlphaMod (e->anim.texture, ca);
    }

    /* players (mirror joueur_afficher_interieur_idx) */
    for (int idx = 0; idx < bg->nbJoueursActifs; idx++) {
        Joueur *j = &bg->joueurs[idx];
        if (!j->actif) continue;
        int a = effects_hit_flash_alpha(fx, EFX_ENT_PLAYER, idx);
        if (a <= 0) continue;

        int kbx = 0, kby = 0;
        effects_get_kb_offset(fx, EFX_ENT_PLAYER, idx, &kbx, &kby);

        /* Mirror joueur_afficher_interieur_idx: use the character animator's
         * current sheet+frame so the additive blink stays aligned with the
         * visible sprite (matching aspect-preserving foot-anchored draw). */
        if (!j->ca.loaded) continue;
        SDL_Texture *useTex = NULL;
        SDL_Rect     useSrc;
        if (!char_anim_current_src(&j->ca, &useTex, &useSrc)) continue;

        int dh = PLAYER_DRAW_H;
        int dw = (useSrc.h > 0)
               ? (int)((float)dh * (float)useSrc.w / (float)useSrc.h + 0.5f)
               : dh;
        int foot_x   = (int)j->x + ox + JOUEUR_W/2 + kbx;
        int foot_y   = (int)j->y + JOUEUR_H + kby;
        int foot_sh  = (int)((float)dh * CHAR_FOOT_PADDING_FRAC + 0.5f);
        SDL_Rect dst = { foot_x - dw/2, foot_y - dh + foot_sh, dw, dh };
        SDL_RendererFlip flip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL
                                                     : SDL_FLIP_NONE;

        SDL_BlendMode prevB;
        SDL_GetTextureBlendMode(useTex, &prevB);
        Uint8 cr, cg, cb, ca;
        SDL_GetTextureColorMod(useTex, &cr, &cg, &cb);
        SDL_GetTextureAlphaMod(useTex, &ca);

        SDL_SetTextureBlendMode(useTex, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod (useTex, 255, 255, 255);
        SDL_SetTextureAlphaMod (useTex, (Uint8)a);
        SDL_RenderCopyEx(r, useTex, &useSrc, &dst, 0, NULL, flip);

        SDL_SetTextureBlendMode(useTex, prevB);
        SDL_SetTextureColorMod (useTex, cr, cg, cb);
        SDL_SetTextureAlphaMod (useTex, ca);
    }
}

/* ===========================================================================
 * World render: death chunks + sparks.
 * ========================================================================= */
void effects_render_world(EffectsSystem *fx, SDL_Renderer *r, int ox)
{
    if (!fx) return;

    /* chunks (normal blend, alpha-faded) — only isChunk == 1 */
    for (int i = 0; i < EFFECTS_MAX_DEATH_PARTICLES; i++) {
        DeathParticle *p = &fx->deathPool[i];
        if (!p->actif || p->isChunk != 1 || !p->tex) continue;

        float tlife = (p->lifeMax > 0.0f) ? (p->life / p->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;
        Uint8 a = (Uint8)(255.0f * tlife);

        SDL_Rect dst = { (int)p->x + ox, (int)p->y, p->drawW, p->drawH };

        SDL_BlendMode prevB;
        SDL_GetTextureBlendMode(p->tex, &prevB);
        Uint8 ca;
        SDL_GetTextureAlphaMod(p->tex, &ca);

        SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod (p->tex, a);
        SDL_RenderCopyEx(r, p->tex, &p->src, &dst, (double)p->rot,
                         NULL, (SDL_RendererFlip)p->flip);

        SDL_SetTextureBlendMode(p->tex, prevB);
        SDL_SetTextureAlphaMod (p->tex, ca);
    }

    /* white-flash silhouettes (isChunk == 2) — additive bleach over
     * the chunks for the first ~50 ms of death.  Renders AFTER chunks
     * so the bleached pop hides the gib.                              */
    for (int i = 0; i < EFFECTS_MAX_DEATH_PARTICLES; i++) {
        DeathParticle *p = &fx->deathPool[i];
        if (!p->actif || p->isChunk != 2 || !p->tex) continue;

        float tlife = (p->lifeMax > 0.0f) ? (p->life / p->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;
        /* Stay near-full alpha for the bulk of the life, then fade
         * fast at the end so the flash reads as crisp.                */
        float a_norm = tlife;
        if (a_norm > 0.30f) a_norm = 1.0f;
        else                a_norm = a_norm / 0.30f;
        Uint8 a = (Uint8)(255.0f * a_norm);

        SDL_Rect dst = { (int)p->x + ox, (int)p->y, p->drawW, p->drawH };

        SDL_BlendMode prevB;
        SDL_GetTextureBlendMode(p->tex, &prevB);
        Uint8 cr, cg, cb, ca;
        SDL_GetTextureColorMod(p->tex, &cr, &cg, &cb);
        SDL_GetTextureAlphaMod(p->tex, &ca);

        SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_ADD);
        SDL_SetTextureColorMod (p->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod (p->tex, a);
        SDL_RenderCopyEx(r, p->tex, &p->src, &dst, 0,
                         NULL, (SDL_RendererFlip)p->flip);

        SDL_SetTextureBlendMode(p->tex, prevB);
        SDL_SetTextureColorMod (p->tex, cr, cg, cb);
        SDL_SetTextureAlphaMod (p->tex, ca);
    }

    /* sparks (additive) */
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    for (int i = 0; i < EFFECTS_MAX_DEATH_PARTICLES; i++) {
        DeathParticle *p = &fx->deathPool[i];
        if (!p->actif || p->isChunk) continue;
        float tlife = (p->lifeMax > 0.0f) ? (p->life / p->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;
        Uint8 a = (Uint8)(255.0f * tlife);
        int s = p->size;
        SDL_Rect rc = { (int)p->x + ox - s/2, (int)p->y - s/2, s, s };
        SDL_SetRenderDrawColor(r, p->r, p->g, p->b, a);
        SDL_RenderFillRect(r, &rc);
    }

    /* slash arcs (additive procedural crescents) — fan of N tapered
     * rays from the pivot, each with thickness for body, tinted per
     * weapon, length grows slightly as the arc fades.  No texture
     * dependency so the effect ships without an art asset.           */
    for (int i = 0; i < EFFECTS_MAX_SLASHES; i++) {
        SlashFX *s = &fx->slashes[i];
        if (!s->actif) continue;

        float tlife = (s->lifeMax > 0.0f) ? (s->life / s->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;

        Uint8 R, G, B;
        float baseLen, halfAng;
        int   thick;
        if (s->kind == SLASH_AXE) {
            R = 255; G = 170; B =  70;
            baseLen = 92.0f;
            halfAng = 58.0f;
            thick   = 10;
        } else {  /* SLASH_SWORD */
            R = 180; G = 240; B = 255;
            baseLen = 76.0f;
            halfAng = 38.0f;
            thick   = 5;
        }
        float grow = 1.0f + (1.0f - tlife) * 0.30f;     /* expands as fades */
        baseLen   *= grow;
        Uint8 alpha = (Uint8)(tlife * 255.0f);

        SDL_SetRenderDrawColor(r, R, G, B, alpha);

        float cx = s->x + (float)ox;
        float cy = s->y;
        float dir = (s->dirSign < 0) ? -1.0f : 1.0f;

        const int N = 18;
        for (int k = 0; k < N; k++) {
            float u    = (float)k / (float)(N - 1);
            float taper = sinf(u * (float)M_PI);          /* fat in middle */
            float L    = baseLen * taper;
            float ang  = -halfAng + u * (2.0f * halfAng) + s->tilt;
            float rad  = ang * (float)M_PI / 180.0f;
            float ex   = cx + dir * cosf(rad) * L;
            float ey   = cy +       sinf(rad) * L * 0.55f;
            int half_t = thick / 2;
            for (int td = -half_t; td <= half_t; td++) {
                SDL_RenderDrawLine(r, (int)cx,     (int)cy + td,
                                       (int)ex,     (int)ey + td);
            }
        }
    }

    /* impact starbursts (additive): scaling 4-axis cross + bright
     * center square.  Scales from startSize to endSize across its
     * lifetime; alpha fades to zero so the burst pops then dies.   */
    for (int i = 0; i < EFFECTS_MAX_IMPACTS; i++) {
        ImpactFlash *p = &fx->impacts[i];
        if (!p->actif) continue;

        float tlife = (p->lifeMax > 0.0f) ? (p->life / p->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;
        float grow = 1.0f - tlife;                 /* 0 → 1 over life */
        float sz   = p->startSize + (p->endSize - p->startSize) * grow;
        Uint8 a    = (Uint8)(255.0f * tlife);

        int cx = (int)p->x + ox;
        int cy = (int)p->y;
        int rx = (int)sz;

        SDL_SetRenderDrawColor(r, p->r, p->g, p->b, a);
        /* 4 axis-aligned rays */
        for (int k = -1; k <= 1; k++) {
            SDL_RenderDrawLine(r, cx - rx, cy + k, cx + rx, cy + k);
            SDL_RenderDrawLine(r, cx + k, cy - rx, cx + k, cy + rx);
        }
        /* 2 diagonal rays at 70% length */
        int rxd = (int)(sz * 0.70f);
        SDL_RenderDrawLine(r, cx - rxd, cy - rxd, cx + rxd, cy + rxd);
        SDL_RenderDrawLine(r, cx - rxd, cy + rxd, cx + rxd, cy - rxd);

        /* Bright center square */
        int core = (int)(sz * 0.35f);
        if (core < 2) core = 2;
        SDL_Rect cc = { cx - core/2, cy - core/2, core, core };
        SDL_SetRenderDrawColor(r, 255, 250, 230, a);
        SDL_RenderFillRect(r, &cc);
    }

    /* shockwave rings (additive): N-segment circle outline, radius
     * lerps from start to end across lifetime, alpha fades.            */
    for (int i = 0; i < EFFECTS_MAX_SHOCKWAVES; i++) {
        Shockwave *s = &fx->shocks[i];
        if (!s->actif) continue;

        float tlife = (s->lifeMax > 0.0f) ? (s->life / s->lifeMax) : 0.0f;
        if (tlife < 0.0f) tlife = 0.0f;
        if (tlife > 1.0f) tlife = 1.0f;
        float grow   = 1.0f - tlife;                /* 0 → 1 */
        float radius = s->startRadius +
                       (s->endRadius - s->startRadius) * grow;
        Uint8 a = (Uint8)(255.0f * tlife);

        SDL_SetRenderDrawColor(r, s->r, s->g, s->b, a);
        int cx = (int)s->x + ox;
        int cy = (int)s->y;

        /* Approximate circle outline with N=32 line segments. */
        const int N = 32;
        float prevx = (float)cx + radius;
        float prevy = (float)cy;
        for (int k = 1; k <= N; k++) {
            float ang = (float)k * 2.0f * (float)M_PI / (float)N;
            float xv  = (float)cx + cosf(ang) * radius;
            float yv  = (float)cy + sinf(ang) * radius;
            /* draw with `thickness` parallel offset strokes */
            for (int td = 0; td < s->thickness; td++) {
                SDL_RenderDrawLine(r, (int)prevx, (int)prevy + td,
                                       (int)xv,    (int)yv    + td);
            }
            prevx = xv;
            prevy = yv;
        }
    }

    SDL_SetRenderDrawBlendMode(r, prev);
}

/* ===========================================================================
 * Lightmap (torches)
 * ========================================================================= */
void effects_prepare_lightmap(EffectsSystem *fx, SDL_Renderer *r, int wagon)
{
    if (!fx || !fx->lightMapOk || !fx->lightMap) return;

    SDL_Texture *prev = SDL_GetRenderTarget(r);
    if (SDL_SetRenderTarget(r, fx->lightMap) != 0) return;

    /* Ambient darkness baseline (becomes the "moonlight" the world keeps
     * in the absence of any torches). */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, LIGHT_AMBIENT_R, LIGHT_AMBIENT_G, LIGHT_AMBIENT_B, 255);
    SDL_RenderClear(r);

    /* Each torch is a stack of EFFECTS_TORCH_STEPS concentric filled
     * rects, brighter toward the centre, drawn additively. */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    for (int i = 0; i < fx->nbTorches; i++) {
        Torch *t = &fx->torches[i];
        if (!t->actif || t->wagon != wagon) continue;

        float sinTerm   = sinf(t->phase);
        float noise     = frand_centered(fx) * EFFECTS_TORCH_RADIUS_NOISE;
        float radius    = t->baseRadius
                          + sinTerm * EFFECTS_TORCH_RADIUS_AMP
                          + noise;
        if (radius < 40.0f) radius = 40.0f;

        for (int s = 0; s < EFFECTS_TORCH_STEPS; s++) {
            /* tt: 0 outermost (faint), 1 innermost (hot) */
            float tt = (float)(s + 1) / (float)EFFECTS_TORCH_STEPS;
            float rectHalf = radius * (1.0f - (float)s / (float)EFFECTS_TORCH_STEPS);
            if (rectHalf < 2.0f) rectHalf = 2.0f;

            /* quadratic falloff -> bright tight core */
            float intensity = tt * tt;
            Uint8 rr = (Uint8)((float)TORCH_R * intensity);
            Uint8 gg = (Uint8)((float)TORCH_G * intensity);
            Uint8 bb = (Uint8)((float)TORCH_B * intensity);

            SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
            SDL_Rect rc = { (int)(t->x - rectHalf),
                            (int)(t->y - rectHalf),
                            (int)(rectHalf * 2.0f),
                            (int)(rectHalf * 2.0f) };
            SDL_RenderFillRect(r, &rc);
        }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(r, prev);
}

void effects_composite_lightmap(EffectsSystem *fx, SDL_Renderer *r,
                                int dstX, int dstY, int dstW, int dstH)
{
    if (!fx || !fx->lightMapOk || !fx->lightMap) return;
    SDL_Rect dst = { dstX, dstY, dstW, dstH };
    /* lightMap's blend mode was set to MOD at init -> multiplies. */
    SDL_RenderCopy(r, fx->lightMap, NULL, &dst);
}

/* ===========================================================================
 * Full-screen warm flash (death moment).
 * ========================================================================= */
void effects_render_flash(EffectsSystem *fx, SDL_Renderer *r)
{
    if (!fx || !fx->flash.actif) return;
    float t = (fx->flash.lifeMax > 0.0f) ? (fx->flash.life / fx->flash.lifeMax)
                                         : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* peaks at 220 alpha at t=1, fades to 0 at t=0 */
    Uint8 a = (Uint8)(220.0f * t);

    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, fx->flash.r, fx->flash.g, fx->flash.b, a);
    SDL_Rect full = { 0, 0, W_ECRAN, H_ECRAN };
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, prev);
}

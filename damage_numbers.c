#include "damage_numbers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- *
 *  Floating damage numbers.
 *
 *  One pool of DMG_NUM_POOL_SIZE slots, each owning a baked text texture.
 *  Slots are scanned linearly on spawn -- the pool is small enough that the
 *  cost is irrelevant compared to the SDL_ttf glyph render that follows.
 *
 *  Render order: numbers live in world space and are drawn alongside enemy
 *  taunts, so the caller passes the same ox offset used for the wagon RT.
 * ------------------------------------------------------------------------- */

static DamageNumber  g_pool[DMG_NUM_POOL_SIZE];
static TTF_Font     *g_font     = NULL;
static SDL_Renderer *g_renderer = NULL;

/* Tracks the previous spawn so a rapid burst at the same spot can fan out
 * vertically/horizontally instead of stacking into one illegible blob. */
static float  g_last_x       = -1e9f;
static float  g_last_y       = -1e9f;
static Uint32 g_last_tick    = 0;
static int    g_stack_count  = 0;

#define STACK_RADIUS_PX 40
#define STACK_WINDOW_MS 150

static float frand_pm(float amp)
{
    /* uniform in [-amp, +amp] */
    return ((float)rand() / (float)RAND_MAX) * 2.0f * amp - amp;
}

static void slot_release(DamageNumber *n)
{
    if (n->tex) { SDL_DestroyTexture(n->tex); n->tex = NULL; }
    n->active = 0;
}

static SDL_Color color_for_type(DamageNumberType t)
{
    switch (t) {
        case DMG_NUM_CRIT:   return (SDL_Color){255, 220,  60, 255};
        case DMG_NUM_PLAYER: return (SDL_Color){255,  80,  80, 255};
        case DMG_NUM_HEAL:   return (SDL_Color){ 80, 255, 100, 255};
        case DMG_NUM_NORMAL:
        default:             return (SDL_Color){255, 255, 255, 255};
    }
}

void damage_numbers_init(TTF_Font *font, SDL_Renderer *renderer)
{
    g_font     = font;
    g_renderer = renderer;
    memset(g_pool, 0, sizeof(g_pool));
    g_last_x = -1e9f; g_last_y = -1e9f;
    g_last_tick = 0;  g_stack_count = 0;
}

void damage_numbers_shutdown(void)
{
    for (int i = 0; i < DMG_NUM_POOL_SIZE; i++) slot_release(&g_pool[i]);
    g_font = NULL;
    g_renderer = NULL;
}

void damage_numbers_reset(void)
{
    for (int i = 0; i < DMG_NUM_POOL_SIZE; i++) slot_release(&g_pool[i]);
    g_stack_count = 0;
    g_last_tick   = 0;
}

void damage_numbers_update(float dt)
{
    for (int i = 0; i < DMG_NUM_POOL_SIZE; i++) {
        DamageNumber *n = &g_pool[i];
        if (!n->active) continue;

        n->life += dt;
        if (n->life >= n->max_life) { slot_release(n); continue; }

        /* upward drift (vy is negative -> y decreases on screen) */
        n->y += n->vy * dt;

        /* scale punch: 1.4 -> 1.0 over the first 0.1s, then 1.0 -> 0.6 over
         * the remainder.  Crit numbers get a static 1.5x boost on top. */
        float s;
        if (n->life < 0.1f) {
            float k = n->life / 0.1f;
            s = 1.4f + (1.0f - 1.4f) * k;
        } else {
            float k = (n->life - 0.1f) / (n->max_life - 0.1f);
            if (k > 1.0f) k = 1.0f;
            s = 1.0f + (0.6f - 1.0f) * k;
        }
        if (n->type == DMG_NUM_CRIT) s *= 1.5f;
        n->scale = s;
    }
}

void damage_numbers_render(SDL_Renderer *r, int ox)
{
    if (!r) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (int i = 0; i < DMG_NUM_POOL_SIZE; i++) {
        DamageNumber *n = &g_pool[i];
        if (!n->active || !n->tex) continue;

        /* horizontal wobble for crits (small sine, phase-offset per slot) */
        float dx = 0.0f;
        if (n->type == DMG_NUM_CRIT)
            dx = sinf(n->life * 8.0f + n->wobble_phase) * 4.0f;

        /* alpha fade: stays 255 for the first ~30% of life, then linear to 0 */
        float t01     = n->life / n->max_life;
        float fade_t  = (t01 - 0.30f) / 0.70f;
        if (fade_t < 0.0f) fade_t = 0.0f;
        if (fade_t > 1.0f) fade_t = 1.0f;
        Uint8 alpha = (Uint8)(255.0f * (1.0f - fade_t));

        int w = (int)((float)n->tex_w * n->scale);
        int h = (int)((float)n->tex_h * n->scale);

        SDL_Rect dst;
        dst.x = (int)(n->x + dx + ox) - w / 2;
        dst.y = (int)(n->y) - h / 2;
        dst.w = w;
        dst.h = h;

        SDL_SetTextureAlphaMod(n->tex, alpha);
        SDL_RenderCopy(r, n->tex, NULL, &dst);
    }
}

void spawn_damage_number(float x, float y, int value, DamageNumberType type)
{
    if (!g_font || !g_renderer) return;
    /* a "0" damage pop on a damage type would just be noise; the heal case
     * still wants to surface a +0 only if a healer ever passes one (rare),
     * so we silence both for consistency. */
    if (value <= 0) return;

    /* find a free slot (or recycle the oldest active one if everything's
     * busy -- shouldn't happen in practice with a 32-slot pool & 0.8s life,
     * but lets a long burst of bullets degrade gracefully instead of silent
     * dropping). */
    DamageNumber *n     = NULL;
    DamageNumber *oldest = NULL;
    float oldest_life   = -1.0f;
    for (int i = 0; i < DMG_NUM_POOL_SIZE; i++) {
        if (!g_pool[i].active) { n = &g_pool[i]; break; }
        if (g_pool[i].life > oldest_life) {
            oldest_life = g_pool[i].life;
            oldest      = &g_pool[i];
        }
    }
    if (!n) { n = oldest; slot_release(n); }
    if (!n) return;

    /* fan out repeated hits on the same spot so they're each readable */
    Uint32 now = SDL_GetTicks();
    float  dx_last = x - g_last_x;
    float  dy_last = y - g_last_y;
    int    same_spot = (dx_last*dx_last + dy_last*dy_last
                        < (float)(STACK_RADIUS_PX*STACK_RADIUS_PX));
    if (same_spot && (now - g_last_tick) < STACK_WINDOW_MS) {
        g_stack_count++;
    } else {
        g_stack_count = 0;
    }
    /* alternating left/right fan, climbing slightly each step */
    float fan_sign = (g_stack_count & 1) ? -1.0f : 1.0f;
    float fan_x    = fan_sign * (float)((g_stack_count + 1) / 2) * 7.0f;
    float fan_y    = -(float)g_stack_count * 4.0f;
    g_last_x = x; g_last_y = y; g_last_tick = now;

    /* build the displayed string -- HEAL prefixes a '+' */
    char buf[16];
    if (type == DMG_NUM_HEAL) snprintf(buf, sizeof(buf), "+%d", value);
    else                      snprintf(buf, sizeof(buf), "%d",  value);

    SDL_Color col = color_for_type(type);
    SDL_Surface *s = TTF_RenderUTF8_Blended(g_font, buf, col);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_renderer, s);
    int tw = s->w, th = s->h;
    SDL_FreeSurface(s);
    if (!t) return;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);

    /* slight random horizontal jitter so simultaneous shots don't perfectly
     * overlap even when the spawn point matches to the pixel */
    float jitter_x = frand_pm(5.0f);

    n->active       = 1;
    n->x            = x + jitter_x + fan_x;
    n->y            = y + fan_y;
    n->spawn_x      = n->x;
    n->vy           = -DMG_NUM_RISE_SPEED;
    n->life         = 0.0f;
    n->max_life     = DMG_NUM_LIFE_SECONDS;
    n->value        = value;
    n->type         = type;
    n->scale        = 1.4f;
    n->wobble_phase = ((float)rand() / (float)RAND_MAX) * 6.2831853f;
    n->tex          = t;
    n->tex_w        = tw;
    n->tex_h        = th;
}

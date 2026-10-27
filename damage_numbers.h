#ifndef DAMAGE_NUMBERS_H
#define DAMAGE_NUMBERS_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

/* Floating combat-feedback numbers that pop out of an entity when it takes
 * damage (or gets healed).  Drawn in world space, after world geometry but
 * before the HUD, so they ride along with the camera/RT ox offset like the
 * enemy-taunt bubbles do. */

typedef enum {
    DMG_NUM_NORMAL = 0,  /* white   -- bullets, dash, contact, hazards */
    DMG_NUM_CRIT,        /* yellow  -- damage >= DMG_NUM_CRIT_THRESHOLD */
    DMG_NUM_PLAYER,      /* red     -- player took damage */
    DMG_NUM_HEAL         /* green   -- health pickup, '+' prefix */
} DamageNumberType;

#define DMG_NUM_POOL_SIZE        32
#define DMG_NUM_CRIT_THRESHOLD   30
#define DMG_NUM_LIFE_SECONDS     0.80f
#define DMG_NUM_RISE_SPEED       40.0f   /* px/s, upward (drawn with -vy) */

typedef struct {
    int    active;
    float  x, y;          /* world-space position, top-left of glyph block */
    float  vy;            /* upward drift (negative in screen y)           */
    float  spawn_x;       /* anchor for crit horizontal wobble             */
    float  life;          /* seconds elapsed since spawn                   */
    float  max_life;      /* lifespan in seconds                           */
    int    value;
    DamageNumberType type;
    float  scale;         /* current scale, animated each frame            */
    float  wobble_phase;  /* random offset so stacked crits don't sync     */
    SDL_Texture *tex;     /* baked TTF surface for this slot's number      */
    int    tex_w, tex_h;
} DamageNumber;

/* Initialise the pool.  Both arguments are borrowed (no ownership taken).
 * Safe to pass a NULL font -- spawns will fall back silently (no draw). */
void damage_numbers_init(TTF_Font *font, SDL_Renderer *renderer);

/* Tear down the pool; releases any baked SDL_Textures still alive. */
void damage_numbers_shutdown(void);

/* Drop every active number (e.g. on level restart or wagon transition). */
void damage_numbers_reset(void);

/* Advance positions/scales/life; recycles slots whose life ran out. */
void damage_numbers_update(float dt);

/* Render every active number, shifted by world->screen offset `ox`. */
void damage_numbers_render(SDL_Renderer *r, int ox);

/* Allocate a slot from the pool and start a number rising from (x, y).
 * (x, y) is world-space; the renderer adds the camera ox.  For value <= 0
 * on damage types this is a no-op (we don't show "0" pops); for HEAL the
 * absolute value is shown with a '+' prefix. */
void spawn_damage_number(float x, float y, int value, DamageNumberType type);

#endif

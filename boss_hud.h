#ifndef BOSS_HUD_H
#define BOSS_HUD_H

/* ---------------------------------------------------------------------------
 * boss_hud — top-center boss panel that slides in when the boss intro
 * finishes and slides out on boss death.  Five-segment HP bar with a lerped
 * fill (so individual hits feel impactful), segment-break feedback (white
 * flash + spark burst + screen shake), a name pulse synced with the rage
 * trigger at 50% HP, and a border flash synced with the stagger trigger at
 * 15% HP.  At 0 HP the whole bar flashes white, holds 0.5s, then slides up.
 *
 * The portrait is a 48x48 crop sampled from Row 1 Frame 1 of the boss sprite
 * sheet at boot, into a dedicated SDL_TEXTUREACCESS_TARGET texture.  If that
 * fails (e.g., sheet not loaded), the portrait falls back to a solid rect
 * in the accent color.
 *
 * Communication contract:
 *   - boss_hud and boss_anim share NO state directly.  They both read the
 *     boss entity's HP through their own APIs (boss_hud_update takes the
 *     actual HP; boss_anim is driven by the same caller through the two
 *     sync-point trigger calls).
 *   - boss_hud_update NEVER writes the boss HP.  It only tracks the
 *     lerped visual value internally.
 * ------------------------------------------------------------------------- */

/* The engine structs (EffectsSystem) are anonymous typedefs, so we can't
 * tag-forward-declare them — pull the engine headers in directly.  Same
 * pattern as boss_intro.h. */
#include <SDL2/SDL.h>
#include "ui.h"
#include "gameplay.h"
#include "effects.h"

#define BOSS_HUD_NUM_SEGMENTS  5
#define BOSS_HUD_PORTRAIT_SIZE 48

typedef struct {
    float        current_hp_visual;   /* lerped display value (in HP units) */
    int          max_hp;
    int          last_hp;             /* last raw HP we saw — for fall edge */
    int          segments_broken;     /* how many segments are permanently dark */
    float        segment_flash_timers[BOSS_HUD_NUM_SEGMENTS];

    /* slide / visibility */
    int          is_visible;
    int          sliding_out;
    float        slide_offset_y;      /* px above panel rest position; >0 hides */
    float        slide_target_y;

    /* sync-point effects */
    float        name_pulse_timer;    /* counts up over PULSE_DURATION */
    int          name_pulse_active;
    float        border_flash_timer;  /* counts up, modulo cycle period */
    int          border_flash_active;

    /* 0 HP fade-and-flash */
    int          dying;
    float        dying_timer;

    /* artwork */
    SDL_Texture *portrait;
    int          portrait_ok;

    /* accent color (taken from the existing HUD/door_anim palette) */
    SDL_Color    accent;
    SDL_Color    panel_bg;
    SDL_Color    bar_fill;
    SDL_Color    bar_fill_hi;
    SDL_Color    bar_empty;
    SDL_Color    stagger_red;
    SDL_Color    name_color;

    /* dependencies stashed at init time */
    SDL_Renderer  *renderer;
    UIFontPack    *fonts;
    UITextCache   *cache;
    GameplayState *gp;          /* for screen shake on segment break */
    EffectsSystem *fx;          /* for spark burst on segment break */
    const char    *boss_name;
} BossHUD;

/* Lifecycle */
void boss_hud_init (SDL_Renderer *r,
                    UIFontPack *fonts, UITextCache *cache,
                    GameplayState *gp,  EffectsSystem *fx,
                    SDL_Texture *boss_sheet,
                    int boss_max_hp,
                    const char *boss_name);
void boss_hud_free (void);
void boss_hud_reset(int boss_max_hp);

/* Show/hide hooks */
void boss_hud_show (void);
void boss_hud_hide (void);            /* triggers slide-out + fade-flash */
int  boss_hud_is_visible(void);
int  boss_hud_is_finished_hiding(void);

/* Per-frame */
void boss_hud_update(float dt, int actual_boss_hp);
void boss_hud_render(void);

/* Sync points — call simultaneously with the matching boss_anim_* trigger. */
void boss_hud_pulse_name   (void);    /* fires once at HP <= 50% */
void boss_hud_flash_border (void);    /* fires once at HP <= 15% */

/* ---------------------------------------------------------------------------
 * Hitstop — short global freeze used as combat-feel feedback when a boss HP
 * segment breaks.  Lives in boss_hud (the only trigger site) so we keep the
 * "no new files" contract; main.c gates the world-sim block on
 * hitstop_active() and decrements with hitstop_tick(dt) every frame from
 * OUTSIDE the gate so the timer always counts down even while frozen.
 *
 *   hitstop_trigger(duration) — request a freeze of `duration` seconds
 *                               (does not shorten an already-longer one)
 *   hitstop_active()          — returns 1 while frozen, 0 otherwise
 *   hitstop_tick(dt)          — decrement the timer; must be called every
 *                               frame outside the world-sim gate
 * ------------------------------------------------------------------------- */
void hitstop_trigger (float duration);
int  hitstop_active  (void);
void hitstop_tick    (float dt);

#endif

#ifndef DOOR_ANIM_H
#define DOOR_ANIM_H

#include <SDL2/SDL.h>

/* The engine's structs are all anonymous typedefs (typedef struct { ... }
 * Name;), so we can't tag-forward-declare them — include the engine headers
 * directly.  This still leaves all behavior contained in door_anim.{c,h}; the
 * includes are just type plumbing. */
#include "background.h"
#include "gameplay.h"
#include "audio.h"
#include "ui.h"

/* ----------------------------------------------------------------------------
 * door_anim — 5-stage cinematic that plays on every wagon transition.
 *
 *   Stage 1 (0.30s) — door_side.png slides in from both screen edges (cubic
 *                     ease-in), then an impact shake when the strips meet.
 *   Stage 2 (~0s)   — instant swap to a full-screen door_front.png + white
 *                     flash.  The wagon-swap (wagonActif advance, player
 *                     reposition, blockTransitionLeft) happens here while the
 *                     screen is fully covered.
 *   Stage 3 (1.20s) — "Wagon X / 5" + wagon name fade in/hold/out on the door
 *                     panel.  (Boss-locked variant skips this and instead
 *                     pulses the door red for 1.00s.)
 *   Stage 4 (0.40s) — door_front.png splits left/right (cubic ease-out)
 *                     revealing door_frame.png underneath.  Steam burst from
 *                     the seam.
 *   Stage 5 (0.30s) — door_frame.png alpha-fades out; gameplay underneath is
 *                     now fully visible and player input has resumed.        */

typedef enum {
    DOOR_STAGE_IDLE   = 0,
    DOOR_STAGE1_SIDES = 1,
    DOOR_STAGE2_SLAM  = 2,
    DOOR_STAGE3_TITLE = 3,   /* or locked-pulse, see is_locked_variant       */
    DOOR_STAGE4_OPEN  = 4,
    DOOR_STAGE5_FADE  = 5
} DoorAnimStage;

/* Singleton-backed state.  The instance lives in door_anim.c; the struct is
 * public per the spec so callers (and debuggers) can introspect it. */
typedef struct {
    int    stage;             /* DoorAnimStage                              */
    float  timer;             /* seconds elapsed within current stage       */
    float  duration;          /* current stage duration (seconds)           */
    float  left_x;            /* x of left panel  — stages 1 & 4            */
    float  right_x;           /* x of right panel — stages 1 & 4            */
    int    alpha;             /* 0..255, used by stage-5 frame fade         */
    int    is_locked_variant; /* 1 = close-and-lock (red pulse)             */
} DoorAnim;

/* Lifecycle.  init() loads the three assets/door/*.png textures once. */
int  door_anim_init(SDL_Renderer  *renderer,
                    UIFontPack    *fonts,
                    UITextCache   *text_cache,
                    Background    *bg,
                    GameplayState *gp,
                    AudioSystem   *audio);
void door_anim_free(void);

/* Triggers.  door_anim_open() is the normal forward transition; the boss
 * variant uses door_anim_close_and_lock() which swaps the wagon AND pulses
 * the door red instead of showing the title card. */
void door_anim_open(void);
void door_anim_close_and_lock(void);

/* Per-frame.  update() is called from the world-sim block (it relies on a
 * regular dt); render() draws on top of the world but underneath the HUD. */
void door_anim_update(float dt);
void door_anim_render(void);

/* Status — used by the wiring layer if you want to suppress, e.g., enemy
 * shouts while the door is on-screen.  The input gate itself reuses the
 * existing bg->transitionSlide.actif mechanism (see door_anim.c). */
int  door_anim_active(void);          /* 1 while not IDLE                  */
int  door_anim_blocks_input(void);    /* 1 during stages 1..4              */

#endif

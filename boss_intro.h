#ifndef BOSS_INTRO_H
#define BOSS_INTRO_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* The engine structs are anonymous typedefs (typedef struct { ... } Name;)
 * so we can't tag-forward-declare them — pull the engine headers in
 * directly.  All behavior still lives in boss_intro.{c,h}; the includes
 * are just type plumbing, identical to door_anim's setup. */
#include "background.h"
#include "gameplay.h"
#include "ui.h"
#include "dialogue_cinematic.h"

/* ---------------------------------------------------------------------------
 * boss_intro — 4-stage cinematic that plays once per run, the first time the
 * player crosses into the final wagon.  Reuses door_anim, dialogue_cinematic
 * (letterbox bars), the kinetic camera-pan additive offset, the screen shake,
 * and the screen flash entirely; nothing here is reimplemented.
 *
 *   Stage 1 — DOOR_LOCK : door_anim_close_and_lock() runs to completion;
 *                         player input + entity sim are frozen via the
 *                         existing input gate (transitionSlide.actif) and
 *                         the cinematic's freeze pattern.
 *   Stage 2 — PAN  (2.5s): camera lerps from the player toward the boss
 *                          across the wagon (additive world-pan_ox, picked
 *                          up by the existing main-loop render path).
 *   Stage 3 — REVEAL (2.0s): dialogue_cinematic_start() slides the bars in,
 *                            boss flips to face the player, name + subtitle
 *                            fade in inside the bottom letterbox bar.
 *   Stage 4 — EXIT (0.5s): dialogue_cinematic_close() slides bars out,
 *                          camera lerps back, input gate releases, boss
 *                          AI resumes naturally as ennemis_update unfreezes.
 *
 * One-shot: a static `played` flag survives death/reload (it is intentionally
 * NOT reset on gameplay_reset_for_run); only a fresh process re-arms it.   */

typedef enum {
    BOSS_INTRO_IDLE = 0,
    BOSS_INTRO_DOOR_LOCK,
    BOSS_INTRO_PAN,
    BOSS_INTRO_REVEAL,
    BOSS_INTRO_EXIT,
    BOSS_INTRO_DONE
} BossIntroStage;

/* Lifecycle.  init() captures the engine pointers used at update/render time
 * (matches the door_anim_init pattern). */
void boss_intro_init(SDL_Renderer       *renderer,
                     UIFontPack         *fonts,
                     UITextCache        *cache,
                     Background         *bg,
                     GameplayState      *gp,
                     DialogueCinematic  *cin);

/* Re-arm the one-shot flag.  Provided for completeness; the spec asks for
 * dying / reloading to NOT replay the intro, so this is intentionally NOT
 * called from any of the gameplay reset paths in main.c. */
void boss_intro_reset_run(void);

/* Trigger gate — true iff the intro hasn't played yet AND no stage is in
 * progress.  Cheap, no side effects, safe to poll. */
bool boss_intro_should_start(void);

/* Begin Stage 1.  Internal guard checks should_start() and bails otherwise,
 * so the call site can be a one-line conditional (see background.c). */
void boss_intro_start(void);

/* Per-frame.  update() is called from outside the world-freeze gate so the
 * stage clock keeps running while the world is frozen.  render() is drawn
 * after the cinematic chrome so the boss name / subtitle land on top of
 * the letterbox bars. */
void boss_intro_update(float dt);
void boss_intro_render(void);

/* Status queries — wired by main.c to (a) extend the world-freeze gate,
 * (b) add to the existing additive pan_ox passed to entity render paths. */
bool boss_intro_active(void);
bool boss_intro_freezes_world(void);
int  boss_intro_world_pan_ox(void);

#endif

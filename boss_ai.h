#ifndef BOSS_AI_H
#define BOSS_AI_H

/* ---------------------------------------------------------------------------
 * boss_ai — final-boss decision/attack-pool/phase machine.  Sits above
 * boss_anim (which only owns sprite playback) and below main.c (which gates
 * the world simulation around the cinematic + death sequences).
 *
 * Three-phase attack pool (HP-driven):
 *   Phase 1 (HP > 50%)            BOSS_ATTACK[+KICK] / KICK alone /
 *                                 RUN charge → ATTACK / GROUND_SLAM (12s cd)
 *   Phase 2 (HP <= 50%)           Same pool, ATTACK/KICK 15% faster, shorter
 *                                 charge, GS cd 8s, HIT_REACT plays half.
 *   Phase 3 (HP <= 15%)           HURT serves as idle.  ATTACK/KICK
 *                                 (alternating per anti-spam) + GS (5s cd).
 *                                 No RUN windup.
 *
 * Anti-spam: the last attack used (ATTACK, KICK, or GROUND_SLAM) is excluded
 * from the next pick.  Never repeats consecutively.
 *
 * Segment events 80/60/40/50/20:
 *   80/60/40 → force HURT, no new attack until HURT finishes + hitstop ends.
 *   50       → force HURT, then RAGE (locked), then Phase 2 activates.
 *   20       → force GROUND_SLAM (heavy), then Phase 3 activates.
 *   0        → boss death sequence (see boss_ai_request_death).
 *
 * Pre-fight TAUNT (BOSS_POINT): played after boss_intro on EVERY fight
 * attempt (including retries with intro skipped).  AI is suspended until
 * BOSS_POINT finishes, then begins Phase 1.
 *
 * Player death / boss death: dedicated sub-FSM that freezes inputs around
 * the BOSS_VICTORY / BOSS_DEATH play-to-completion.  main.c reads the
 * boss_ai_world_frozen() flag to gate the world simulation.
 * ------------------------------------------------------------------------- */

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "combat.h"
#include "background.h"
#include "gameplay.h"
#include "audio.h"

/* Boss HP multiplier — applied on top of ENEMY_HP_BASE_MULT.  Used by
 * the boss-wagon init in main.c, the HUD reset, the AI's max_hp cache,
 * AND by gameplay's scale_enemy_for_level so the actual entity HP and
 * the segment-event thresholds line up.                                */
#define BOSS_HP_MULT             3.0f

/* Public AI top-level states.  Mostly exposed for diagnostics; main.c uses
 * the dedicated query helpers below to drive its render/freeze logic.    */
typedef enum {
    BAI_IDLE = 0,         /* before the fight starts (boss intro running)   */
    BAI_TAUNT,            /* BOSS_POINT playing, AI suspended               */
    BAI_DECIDING,         /* pick next attack from the current-phase pool   */
    BAI_RUN_CHARGE,       /* BOSS_RUN toward player up to a max duration    */
    BAI_ATTACKING,        /* BOSS_ATTACK / BOSS_KICK / BOSS_GROUND_SLAM     */
    BAI_HIT_RECOVER,      /* BOSS_HIT_REACT brief, returns to deciding      */
    BAI_SEG_HURT,         /* segment 80/60/40 — locked in HURT              */
    BAI_SEG_RAGE,         /* segment 50  — HURT → RAGE then Phase 2 begins  */
    BAI_SEG_SLAM,         /* segment 20  — GROUND_SLAM then Phase 3 begins  */
    BAI_DYING,            /* BOSS_DEATH lock-forever, awaiting completion   */
    BAI_VICTORY,          /* BOSS_VICTORY one-shot after player death       */
    BAI_DONE              /* terminal — main.c hands off to outro / loss   */
} BossAIState;

/* Lifecycle --------------------------------------------------------------- */
void boss_ai_init  (Background *bg, GameplayState *gp, AudioSystem *audio);
void boss_ai_reset (void);   /* re-arm for a fresh boss encounter           */

/* Boot the fight: called once boss_intro has fully completed (or the intro
 * was skipped because already-played).  Plays BOSS_POINT, suspends AI until
 * it finishes, then enters Phase 1.                                       */
void boss_ai_arm_taunt(void);

/* Per-frame.  Drives the AI state machine and the boss entity's
 * position/HP/animation.  Pass dt seconds.                                */
void boss_ai_update(float dt);

/* Segment-event hooks — main.c calls once per threshold crossing.        */
void boss_ai_event_hurt_segment(int segment_idx);   /* 80/60/40 → idx 0/1/2 */
void boss_ai_event_rage_50     (void);
void boss_ai_event_slam_20     (void);

/* Death triggers.  Boss death = caller saw boss HP <= 0; player death =
 * caller saw all players HP <= 0.  Each transitions the AI into the
 * respective lock-forever finishing animation.                            */
void boss_ai_request_death    (void);   /* boss died                       */
void boss_ai_request_victory  (void);   /* player(s) died                  */

/* Hit hook — call ONCE per damage event the boss takes (any source).
 * Drives the brief HIT_REACT when the boss is in the deciding window;
 * does not interrupt locked attacks or segment animations.               */
void boss_ai_on_hit(void);

/* Queries (no side effects) --------------------------------------------- */
bool          boss_ai_world_frozen   (void); /* freeze world sim?         */
bool          boss_ai_input_frozen   (void); /* freeze player input?      */
bool          boss_ai_should_outro   (void); /* boss death finished, win  */
bool          boss_ai_should_gameover(void); /* victory anim finished, loss */
BossAIState   boss_ai_state          (void);
int           boss_ai_phase          (void); /* 1 / 2 / 3                 */
bool          boss_ai_taunt_active   (void); /* BOSS_POINT playing        */

/* For boss_anim_render's foot anchor — exposes the boss entity index in
 * bg->ennemis.  -1 if no boss is currently spawned.                      */
int           boss_ai_boss_idx       (void);

#endif

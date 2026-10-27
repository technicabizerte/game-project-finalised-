#ifndef VERSUS_H
#define VERSUS_H

#include <SDL2/SDL.h>
#include "background.h"
#include "gameplay.h"
#include "ui.h"

/* ─── Versus tunables — one-line switches ─────────────────────────────
 * Toggle these to change the ruleset.  Defaults: best-of-3, HP rounds. */

/* Best-of-N matches: first player to reach this many round wins.       */
#define VERSUS_TARGET_WINS   2          /* 2 → best of 3.  Set to 1 for first-to-one. */

/* Single-life variant.  1 → match ends the first time anyone hits 0 HP
 * (effectively first-to-one regardless of VERSUS_TARGET_WINS).         */
#define VERSUS_SINGLE_LIFE   0

/* Round time limit (seconds).  >0 enables: the round ends after this
 * many seconds and the player with more HP wins (tie = draw).  0 means
 * unlimited time — round only ends on a KO.                            */
#define VERSUS_ROUND_TIME_S  0

/* Y coordinate of the arena floor — where the feet of the hitbox rest.
 * Tuned to sit on the painted grating in versus_arena.png (1376×768,
 * stretched edge-to-edge into the 1024×576 canvas).  The diamond-mesh
 * grating top sits ~81% down the source (y≈625/768) → screen y≈470.   */
#define VERSUS_FLOOR_Y       470

/* Versus character draw scale (multiplied against PLAYER_DRAW_H).
 * Hitbox stays at JOUEUR_W/H so combat ranges don't drift; the bigger
 * sprite is purely visual.  1.02 → 148 × 1.02 ≈ 151 px sprite, ~20%
 * larger than 0.85, sits well against the new arena art without
 * eating the whole stage.                                              */
#define VERSUS_PLAYER_SCALE  1.02f

/* PvP-only max HP.  Doubled vs. campaign so rounds last longer with
 * the same per-hit damage values.                                     */
#define VERSUS_MAX_HP        200

/* Toned-down KO finisher: on the round-losing hit, spawn a flash +
 * spark burst + small shockwave on the loser (no full gib).  Set to 0
 * to play the regular hit + ANIM_DEATH alone.                          */
#define VERSUS_KO_FINISHER   1

typedef enum {
    VS_PHASE_INTRO = 0,     /* "ROUND N" banner, world frozen           */
    VS_PHASE_FIGHT,         /* live combat                              */
    VS_PHASE_ROUND_END,     /* winner banner, world frozen              */
    VS_PHASE_MATCH_END,     /* results screen + Rematch / Menu          */
} VersusPhase;

typedef struct {
    VersusPhase phase;
    float       phase_timer;        /* seconds since this phase entered */

    int   round_idx;                /* 1-based current round number      */
    int   score[2];                 /* round wins per player             */
    int   last_winner;              /* 0 = P1, 1 = P2, -1 = draw         */
    float fight_elapsed;            /* seconds since FIGHT started       */

    /* Results-screen cursor.  0 = REMATCH, 1 = MENU.                    */
    int   ui_cursor;
    int   ui_hover;                 /* -1 = none                         */

    /* Output flags: main.c reads after versus_update() / handlers.     */
    int   request_rematch;
    int   request_exit_to_menu;
} VersusState;

/* Start a fresh match.  Resets score, positions, HP; runs through
 * joueur_init_interieur so each player's char_anim sheets reflect their
 * picked (skin, weapon).                                                */
void versus_begin_match(VersusState *vs, Background *bg, GameplayState *gp);

/* World + phase tick.  Advances player physics + bullets only during
 * VS_PHASE_FIGHT; otherwise just counts down the phase timer.          */
void versus_update(VersusState *vs, Background *bg, GameplayState *gp, float dt);

/* Renders the wagon backdrop, both players, bullets, and effect FX.
 * Takes the SDL_Window so the renderer can size the background to the
 * real screen via SDL_GetWindowSize — no logical-size letterbox.      */
void versus_render_world(VersusState *vs, SDL_Renderer *r, SDL_Window *win,
                         Background *bg, GameplayState *gp);

/* HUD: HP bars + round pips + phase banner / match-end overlay.
 * Uses SDL_GetWindowSize(win) so widget positions track the actual
 * screen dimensions instead of a hardcoded logical canvas.            */
void versus_render_hud(VersusState *vs, SDL_Renderer *r, SDL_Window *win,
                       UIFontPack *fonts, UITextCache *cache,
                       Background *bg);

void versus_handle_motion(VersusState *vs, int lx, int ly);
void versus_handle_click (VersusState *vs, int lx, int ly);
void versus_handle_key   (VersusState *vs, SDL_Keycode k);

/* PvP melee damage path called from background.c::apply_melee_impact
 * when g_game_mode == MODE_VERSUS.  Same visual feedback as enemy hits
 * (damage number + hit-fx + knockback) but the target is the other
 * player.  Returns immediately if the swing is out of range.          */
void versus_apply_melee(Background *bg, int swinger_idx);

/* PvP dash trigger.  Mirrors gameplay_handle_dash_input but stays
 * inside the versus rules (no namgoong gate, no enemy targets).  Reads
 * + writes gp->dash[idx] so the existing trail-spawn / state machine
 * is reused.                                                          */
void versus_handle_dash_input(struct GameplayState *gp,
                              Background *bg, int idx);

/* Per-frame dash step.  Advances the active dash, spawns trails, runs
 * PvP contact damage against the other player, decays trails.         */
void versus_step_dash(struct GameplayState *gp, Background *bg, float dt);

#endif /* VERSUS_H */

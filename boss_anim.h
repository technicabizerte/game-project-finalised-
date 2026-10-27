#ifndef BOSS_ANIM_H
#define BOSS_ANIM_H

/* ---------------------------------------------------------------------------
 * boss_anim — final-boss state machine driven by separate per-animation PNG
 * sheets under spritesheets assets/final boss/.  Each animation is its own
 * texture with its own grid and frame count; the metadata table maps each
 * state to (cols, rows, frame_count, frame_time, play_mode, y_offset).
 *
 * Grid sizing convention (matches src/char_anim.c::resolve_grid):
 *   1280x1280 → 5x5  (256x256 frames)
 *   3584x3584 → 7x7  (512x512 frames)
 *   4096x4096 → 8x8  (512x512 frames)
 *
 * IDLE is a substitute — there is no idle sheet.  Each call to render while
 * the boss is in BOSS_IDLE picks up the LAST FRAME of whatever previous
 * animation finished (a "freeze on last frame" idle), so transitions don't
 * pop.  TODO: swap the idle substitute for the real sheet at boss_anim.c::
 * load path when art ships.
 *
 * BOSS_POINT is the pre-fight taunt: opening animation only, locked until
 * complete, plays on every fight attempt including retries.  Not a rage
 * trigger.  See boss_intro.c hand-off + boss_ai.c::boss_ai_arm_taunt.
 * ------------------------------------------------------------------------- */

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
    BOSS_IDLE = 0,        /* substitute — hold last frame of previous anim */
    BOSS_RUN,
    BOSS_ATTACK,
    BOSS_KICK,
    BOSS_GROUND_SLAM,
    BOSS_HIT_REACT,
    BOSS_HURT,
    BOSS_RAGE,
    BOSS_POINT,
    BOSS_DEATH,
    BOSS_VICTORY,
    BOSS_STATE_COUNT
} BossState;

typedef enum {
    BOSS_PLAY_LOOP = 0,
    BOSS_PLAY_ONCE_HOLD,
    BOSS_PLAY_LOCK_FOREVER
} BossPlayMode;

/* World-render target footprint.  Foot-anchored — the character's feet sit
 * on (world_y + hbh); the sprite scales up from there.  Exposed as a tunable
 * so the final on-screen boss size can be dialled in one place.            */
#define BOSS_DISPLAY_H            220
#define BOSS_DISPLAY_W            220
/* Transparent bottom padding inside each source cell, as a fraction of
 * the destination height.  Shifts dst.y DOWN so the character's visible
 * feet sit on the floor instead of floating above it.                    */
#define BOSS_FOOT_PAD_FRAC        0.18f

/* Lifecycle ---------------------------------------------------------------- */
void boss_anim_init (SDL_Renderer *r);
void boss_anim_free (void);
void boss_anim_reset(void);

/* Per-frame --------------------------------------------------------------- */
void boss_anim_update(float dt);

/* State change.  Honors locking rules:
 *   - LOCK_FOREVER: nothing can replace; calls are no-ops.
 *   - ONCE_HOLD locked: nothing can replace until the animation completes
 *     OR the caller passes force=true.
 *   - LOOP: free to change.
 *
 * HIT_REACT and HURT bypass the lock when the existing one-shot is in
 * ONCE_HOLD (mid-play) and force is true — see boss_anim_set_force.       */
void boss_anim_set      (BossState s);
void boss_anim_set_force(BossState s);

/* Render at the boss's world position.  cam_ox is the camera offset added
 * to world coords; direction = +1 face right, -1 face left.               */
void boss_anim_render(float world_x, float world_y,
                      int cam_ox, int direction,
                      int hbw, int hbh);

/* State queries --------------------------------------------------------- */
BossState     boss_anim_get_state         (void);
bool          boss_anim_is_locked         (void);
bool          boss_anim_is_finished       (void);   /* one-shot reached last frame */
bool          boss_anim_just_finished     (BossState s); /* edge-trigger one-shot   */
int           boss_anim_current_frame     (void);
int           boss_anim_frame_count       (BossState s);
float         boss_anim_frame_time        (BossState s);
BossPlayMode  boss_anim_play_mode         (BossState s);
float         boss_anim_elapsed           (void);   /* seconds since current state began */

/* Frame-time override for a single state — used by Phase 2 to speed
 * ATTACK and KICK by 15% without rewriting the metadata table.           */
void          boss_anim_set_frame_time_scale(BossState s, float scale);

#endif

#ifndef CHAR_ANIM_H
#define CHAR_ANIM_H

#include <SDL2/SDL.h>
#include <stdbool.h>

/* ---------- enums ----------------------------------------------------- */

typedef enum {
    WEAPON_AXE = 0,
    WEAPON_SWORD,
    WEAPON_COUNT
} Weapon;

typedef enum {
    SKIN_CURTIS = 0,
    SKIN_CURTIS1,
    SKIN_ELLIE,
    SKIN_NAMGOONG,
    SKIN_COUNT
} Skin;
/* To drop curtis1, remove SKIN_CURTIS1 (it sits between CURTIS and ELLIE)
 * and the slot collapses; SKIN_COUNT updates automatically.            */

typedef enum {
    ANIM_IDLE = 0,
    ANIM_RUN,
    ANIM_JUMP,
    ANIM_ATTACK,
    ANIM_HIT_REACT,
    ANIM_DEATH,
    ANIM_GUN,
    ANIM_COUNT
} AnimType;

typedef enum {
    PLAY_LOOP = 0,        /* runs forever, never finishes                */
    PLAY_ONCE_HOLD,       /* plays once, holds last frame, then unlocks  */
    PLAY_LOCK_FOREVER     /* plays once, holds last frame, never unlocks */
} PlayMode;

/* ---------- per-anim metadata ----------------------------------------- */

typedef struct {
    int      cols, rows;
    int      frame_count;   /* cap; loader uses min(meta, cols*rows)      */
    float    frame_time;    /* seconds per frame                          */
    PlayMode mode;
} AnimMeta;

/* Defaults for the 5x5 norm. The loader auto-resolves cols/rows from the
 * texture (1280→5, 3584→7, 4096→8); see char_anim.c.                    */
extern const AnimMeta CHAR_ANIM_META[ANIM_COUNT];

/* Frame index (5x5 grid) at which a melee swing visually connects.
 * Same value the legacy ellie animator used.                            */
#define CHAR_ATTACK_IMPACT_FRAME 14

/* Frame index (5x5 grid) at which the muzzle flash reads on ANIM_GUN.
 * Tuned visually against the per-skin gun sheets — adjust here if a
 * future sheet shows the flash earlier/later.                          */
#define CHAR_GUN_FIRE_FRAME      8

/* Combat stats per weapon — sword is faster + lighter, axe heavier.
 * `attack_frame_time` overrides ANIM_ATTACK's per-frame duration at
 * load time, so swing speed agrees with the cooldown gate.            */
typedef struct {
    int   melee_damage;
    int   melee_cooldown_ms;
    int   melee_range;
    float attack_frame_time;
} WeaponStats;

extern const WeaponStats CHAR_WEAPON_STATS[WEAPON_COUNT];

/* Default on-screen height for rendered characters, in pixels.          */
#define CHAR_DISPLAY_H 100

/* Per-frame empty space below the feet, as a fraction of display_h.
 * The art has ~14% padding at the bottom of each frame so the visible
 * boots end well above the frame's bottom edge; the renderer shifts
 * dst.y down by this fraction so the feet land on the foot_y anchor.   */
#define CHAR_FOOT_PADDING_FRAC 0.14f

/* ---------- runtime state --------------------------------------------- */

typedef struct {
    SDL_Texture *texture;
    int          cols, rows;     /* resolved per texture                 */
    int          frame_w;
    int          frame_h;
    int          frame_count;    /* min(meta.frame_count, cols*rows)     */
    int          current_frame;
    float        timer;
    float        frame_time;
    PlayMode     mode;
    int          finished;       /* 1 once a non-loop anim hit its end   */
} CharAnimTrack;

typedef struct {
    Skin           skin;
    Weapon         weapon;
    SDL_Texture   *preview;
    CharAnimTrack  tracks[ANIM_COUNT];
    AnimType       current;
    AnimType       previous;
    int            is_locked;    /* 1 while a locked one-shot plays      */
    int            loaded;
} CharAnim;

/* ---------- path-derivation helpers ----------------------------------- */

const char *char_skin_name  (Skin   s);
const char *char_weapon_name(Weapon w);
const char *char_anim_name  (AnimType a);

/* ---------- lifecycle -------------------------------------------------- */

/* Load all 7 anim sheets + preview.png for (skin, weapon).
 * If `ca` already holds textures, they are freed first (no leaks).
 * Returns true if at least IDLE loaded; missing optional sheets are
 * logged but do not fail the call.                                      */
bool char_anim_load(CharAnim     *ca,
                    SDL_Renderer *r,
                    Skin          skin,
                    Weapon        weapon);

void char_anim_free(CharAnim *ca);

/* ---------- state machine --------------------------------------------- */

/* Request a transition.  Honors the lock rules:
 *   - while locked, only DEATH (always wins) or HIT_REACT-interrupting-
 *     ATTACK pass through;
 *   - re-entering the same looping state is a no-op (does not reset).  */
void char_anim_set   (CharAnim *ca, AnimType a);
void char_anim_update(CharAnim *ca, float dt);

bool char_anim_is_locked  (const CharAnim *ca);
bool char_anim_is_finished(const CharAnim *ca);
int  char_anim_current_frame(const CharAnim *ca);

/* True iff the current anim is ATTACK and its playhead has reached the
 * impact frame.  Used to fire melee damage on the visible swing.       */
bool char_anim_attack_at_impact(const CharAnim *ca);

/* True iff the current anim is GUN and its playhead has reached the
 * fire frame (muzzle flash).  Used to spawn the projectile then.      */
bool char_anim_gun_at_fire(const CharAnim *ca);

/* ---------- rendering ------------------------------------------------- */

/* Foot-anchored draw: feet land on (center_x, foot_y).                 */
void char_anim_render(const CharAnim     *ca,
                      SDL_Renderer       *r,
                      int                 center_x,
                      int                 foot_y,
                      int                 display_h,
                      SDL_RendererFlip    flip);

/* Effects-friendly: hand back the current sheet and source rect for
 * external blits (e.g. additive kinetic blink).  Returns false if no
 * texture is loaded for the current anim.                              */
bool char_anim_current_src(const CharAnim *ca,
                           SDL_Texture   **out_tex,
                           SDL_Rect       *out_src);

#endif /* CHAR_ANIM_H */

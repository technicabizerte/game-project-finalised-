#ifndef DIALOGUE_CINEMATIC_H
#define DIALOGUE_CINEMATIC_H

#include <SDL2/SDL.h>
#include <stdbool.h>

#include "ui.h"
#include "npc_dialogue.h"

/* Cinematic-presentation wrapper around the existing NPCDialogue chatbot.
 *
 * The cinematic owns:
 *   - a 4-state machine (IDLE / ENTER / ACTIVE / EXIT)
 *   - top and bottom letterbox bars, animated in & out
 *   - a "look-at" world camera shift toward the midpoint between player & NPC
 *   - a screen-zoom envelope (1.00 -> 1.15 -> 1.00) applied via RenderSetScale
 *   - a vignette darkening pass
 *   - a typewriter cursor over NPCDialogue::response
 *   - a "speaker is talking" 2px sine bob that the player/NPC draw code can
 *     query and add to their y position
 *
 * It deliberately does NOT take ownership of the dialogue data: NPCDialogue
 * is still poked by the existing input loop & LLM callback.  We just delay
 * the visible reveal of `dlg->response` and decorate the screen.
 */

typedef enum {
    CIN_IDLE = 0,
    CIN_ENTER,
    CIN_ACTIVE,
    CIN_EXIT
} CinematicStage;

#define CIN_ENTER_SECONDS     0.40f
#define CIN_EXIT_SECONDS      0.30f
/* spec asked for ~12% bars on both edges; we keep the top at 12% but grow
 * the bottom to fit speaker-name + typewriter-line + player-input + hint
 * without the text colliding with the world edge. */
#define CIN_BAR_TOP_PCT       0.12f
#define CIN_BAR_BOT_PCT       0.24f
#define CIN_ZOOM_PEAK         1.15f
#define CIN_VIGNETTE_PEAK     90      /* alpha 0..255 at full active     */
#define CIN_TYPE_CHARS_PER_S  40.0f
#define CIN_BOB_AMPLITUDE_PX  2.0f
#define CIN_BOB_HZ            1.6f

typedef struct {
    CinematicStage stage;
    float          stage_time;     /* seconds in current stage           */
    float          progress;       /* 0..1, smoothed envelope            */

    /* anchor points captured at start(), in WORLD coordinates */
    float          npc_x, npc_y;
    float          player_x, player_y;

    /* typewriter -- watches NPCDialogue::response & reveals char by char */
    int            type_char_index;
    float          type_char_timer;
    bool           type_is_complete;
    /* hash of the response we're currently revealing; if dlg->response
     * changes (LLM reply arrived, or player got a new line) we reset    */
    unsigned long  type_response_hash;

    /* pointer to the wrapped dialogue (borrowed, not owned)             */
    NPCDialogue   *dlg;
} DialogueCinematic;

/* ------------------------------------------------------------------------- */
/* lifecycle / state                                                         */

void dialogue_cinematic_init(DialogueCinematic *c);

/* Trigger the ENTER stage.  Captures speaker/listener anchors and opens
 * the wrapped NPCDialogue so the existing input/LLM flow keeps working. */
void dialogue_cinematic_start(DialogueCinematic *c, NPCDialogue *dlg,
                               float npc_x,    float npc_y,
                               float player_x, float player_y);

/* Begin the EXIT stage; call when the player presses Esc or otherwise
 * dismisses the dialogue.  Safe to call from any stage.                 */
void dialogue_cinematic_close(DialogueCinematic *c);

/* Per-frame advance.  Drives the stage transitions, the zoom envelope,
 * the letterbox slide, and the typewriter cursor.                       */
void dialogue_cinematic_update(DialogueCinematic *c, float dt);

/* Returns true whenever the world simulation should be frozen.  Use this
 * to gate physics, AI, projectiles, effects, etc. in the main loop.     */
bool dialogue_cinematic_freezes_world(const DialogueCinematic *c);

/* True iff stage != IDLE.  Use to suppress non-essential HUD pieces.    */
bool dialogue_cinematic_is_active(const DialogueCinematic *c);

/* ------------------------------------------------------------------------- */
/* render-time queries (called by main render path & by gameplay/namgoong)   */

/* Zoom factor in [1.0, CIN_ZOOM_PEAK].  Apply via SDL_RenderSetScale     */
/* before the world draw block, then reset to 1.0 before HUD/letterbox.   */
float dialogue_cinematic_zoom(const DialogueCinematic *c);

/* Extra world-x shift to add to the existing render `ox` so the speaker
 * mid-point sits near logical-screen center while we zoom in.            */
int   dialogue_cinematic_world_pan_ox(const DialogueCinematic *c);

/* Y-offset for the speaker's draw position (the bob).  Caller picks
 * which entity is the speaker; the cinematic only owns the curve.        */
int   dialogue_cinematic_speaker_bob_offset(const DialogueCinematic *c);

/* Draw the vignette (called BEFORE the letterbox so bars stay solid).   */
void  dialogue_cinematic_render_vignette(const DialogueCinematic *c,
                                          SDL_Renderer *r);

/* Draw the letterbox bars + speaker name + typewriter text + chevrons.  */
void  dialogue_cinematic_render_chrome(const DialogueCinematic *c,
                                        SDL_Renderer *r,
                                        UIFontPack *fonts,
                                        UITextCache *cache);

/* Intercept a key while the cinematic is active.  Returns true if the
 * cinematic consumed the key (e.g., the player pressed Enter to skip the
 * typewriter to fully-revealed); returns false to let the underlying
 * NPCDialogue keydown handler run as usual.                              */
bool  dialogue_cinematic_handle_key(DialogueCinematic *c, SDL_Keycode key);

/* Module-local global so render code that doesn't carry a DialogueCinematic
 * pointer (e.g. gameplay_render_world drawing the NPC) can still read the
 * speaker-bob offset.  Mirror of the effects_global() pattern.            */
void                dialogue_cinematic_bind_global(DialogueCinematic *c);
DialogueCinematic * dialogue_cinematic_global(void);

#endif

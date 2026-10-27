#ifndef OUTRO_H
#define OUTRO_H

#include <SDL2/SDL.h>
#include "ui.h"

/* Outro cutscene played the instant the final wagon's boss dies.
 *
 * Phase 1: white flash + frozen scene + fade to black (1.5 s, NEVER skippable)
 * Phase 2: dark metal bg, 3 narration lines fade in (4 s, SPACE/RETURN skips)
 * Phase 3: train shot scrolling at half intro speed + cold tint (3 s, skippable)
 * Phase 4: full win screen with stats + Rejouer / Menu principal buttons
 *
 * Phase 4 REPLACES the existing ETAT_ENDING render path: ETAT_ENDING is
 * left untouched in main.c (per "do not modify existing win screen logic")
 * but the new win flow exits via outro -> ETAT_OUTRO -> action codes below. */

typedef enum {
    OUTRO_PHASE_IMPACT    = 0,
    OUTRO_PHASE_NARRATION = 1,
    OUTRO_PHASE_TRAIN     = 2,
    OUTRO_PHASE_WIN       = 3,
    OUTRO_PHASE_DONE      = 4
} OutroPhase;

/* Returned by input handlers / outro_update to tell main.c what to do. */
typedef enum {
    OUTRO_ACTION_NONE   = 0,
    OUTRO_ACTION_CLICK  = 1,   /* selection moved on a button - play SFX  */
    OUTRO_ACTION_REPLAY = 2,   /* Rejouer:        restart from beginning  */
    OUTRO_ACTION_MENU   = 3    /* Menu principal: return to ETAT_MENU     */
} OutroAction;

#define OUTRO_SNOW_COUNT 60

typedef struct {
    float x, y;
    int   layer;
} OutroSnow;

typedef struct {
    OutroPhase    phase;
    float         phaseTime;

    /* stats captured at boss death */
    int           finalScore;
    int           finalTimeSec;
    int           enemiesKilled;

    /* phase 1 white flash: 255 -> 0 over 0.3 s */
    float         flashAlpha;

    /* assets */
    SDL_Texture  *metalTex;
    SDL_Texture  *trainTex;
    int           trainTexW, trainTexH;
    SDL_Texture  *endBgTex;

    /* phase 3 scroll */
    float         trainScrollX;
    OutroSnow     snow[OUTRO_SNOW_COUNT];

    /* phase 4 win-screen selection: 0 = Rejouer, 1 = Menu principal */
    int           winSelected;
} OutroState;

void        outro_start (OutroState *o, SDL_Renderer *r,
                         int finalScore, int finalTimeSec, int enemiesKilled);
OutroAction outro_handle_key (OutroState *o, SDL_Keycode key);
OutroAction outro_handle_mouse_motion(OutroState *o, int lx, int ly);
OutroAction outro_handle_mouse_click (OutroState *o, int lx, int ly);
OutroAction outro_update(OutroState *o, float dt);  /* polls for completion  */
void        outro_render(OutroState *o, SDL_Renderer *r,
                         UIFontPack *fonts, UITextCache *cache,
                         UIAssets *assets);
void        outro_free  (OutroState *o);

/* main.c queries this to know when to keep rendering the frozen wagon
 * world (only true during the impact phase).                             */
int         outro_should_render_world(const OutroState *o);

#endif

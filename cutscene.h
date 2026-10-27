#ifndef CUTSCENE_H
#define CUTSCENE_H

#include <SDL2/SDL.h>
#include "ui.h"

/* Intro cutscene played between ETAT_SKIN_SELECT (new game) and ETAT_JEU.
 * Auto-advances through 4 panels; SPACE/RETURN skips straight to a quick
 * fade-in.  All state is owned by CutsceneState - main.c just calls
 * cutscene_update(dt) until it returns 1, then transitions to gameplay. */

typedef enum {
    CUT_PANEL_LORE      = 0,   /* Pure black + 3 lore lines fade in   */
    CUT_PANEL_TRAIN     = 1,   /* Scrolling train shot + snow + tint  */
    CUT_PANEL_TITLECARD = 2,   /* "Wagon 1 / 5" + car name            */
    CUT_PANEL_FADEIN    = 3,   /* Final fade from black into gameplay */
    CUT_PANEL_DONE      = 4
} CutscenePanel;

#define CUT_SNOW_COUNT 60

typedef struct {
    float x, y;
    int   layer;     /* 0 = slow/small, 1 = mid, 2 = fast/big */
} CutsceneSnow;

typedef struct {
    CutscenePanel panel;
    float         panelTime;     /* seconds since current panel began   */
    int           wagonIndex;    /* 0..MAX_WAGONS-1, drives title card  */

    SDL_Texture  *trainTex;
    int           trainTexW, trainTexH;
    float         trainScrollX;

    CutsceneSnow  snow[CUT_SNOW_COUNT];

    int           skipping;      /* 1 once SPACE/RETURN was pressed     */
} CutsceneState;

void cutscene_start (CutsceneState *c, SDL_Renderer *r, int wagonIndex);
void cutscene_skip  (CutsceneState *c);
int  cutscene_update(CutsceneState *c, float dt);   /* returns 1 = done */
void cutscene_render(CutsceneState *c, SDL_Renderer *r,
                     UIFontPack *fonts, UITextCache *cache);
void cutscene_free  (CutsceneState *c);

#endif

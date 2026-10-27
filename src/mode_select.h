#ifndef MODE_SELECT_H
#define MODE_SELECT_H

#include <SDL2/SDL.h>
#include "ui.h"

/* High-level game mode picked from the new game-mode menu.  Distinct
 * from background.h's MODE_MONO/MODE_MULTI which is the low-level
 * "active player count" flag passed to gameplay_reset_for_run.        */
typedef enum {
    MODE_CAMPAIGN = 0,   /* 1 player, full story                       */
    MODE_COOP,           /* 2 players, beat the campaign together      */
    MODE_VERSUS,         /* 2 players, face off                        */
    GAME_MODE_COUNT
} GameMode;

extern GameMode g_game_mode;

typedef enum {
    MS_ACTION_NONE = 0,
    MS_ACTION_CONFIRM,   /* g_game_mode updated; caller routes accordingly */
    MS_ACTION_CANCEL,    /* Escape — caller decides where back              */
} ModeSelectAction;

void mode_select_init(SDL_Renderer *r);
void mode_select_free(void);
void mode_select_enter(void);   /* reset cursor on entry  */

void              mode_select_handle_motion(int logical_x, int logical_y);
ModeSelectAction  mode_select_handle_click (int logical_x, int logical_y);
ModeSelectAction  mode_select_handle_key   (SDL_Keycode k);

void mode_select_render(SDL_Renderer *r,
                        UIFontPack   *fonts,
                        UITextCache  *cache,
                        SDL_Texture  *bg,
                        SDL_Texture  *button_tex);

#endif /* MODE_SELECT_H */

#ifndef SKIN_SELECT_H
#define SKIN_SELECT_H

#include <SDL2/SDL.h>
#include "char_anim.h"
#include "ui.h"

/* The skin picked on the last confirm is written into
 * player_cfg[target_slot].skin (see src/two_player.h).               */

typedef enum {
    SS_ACTION_NONE = 0,
    SS_ACTION_CONFIRM,   /* player_cfg[target_slot].skin updated         */
    SS_ACTION_CANCEL,    /* user pressed Escape                           */
} SkinSelectAction;

void skin_select_init(SDL_Renderer *r);
void skin_select_free(void);

/* (Re)load preview textures with the slot's currently-selected weapon
 * and bind subsequent confirms to that slot.  Call once when entering
 * the screen.                                                          */
void skin_select_enter(SDL_Renderer *r, int target_slot);

void              skin_select_handle_motion(int logical_x, int logical_y);
SkinSelectAction  skin_select_handle_click (int logical_x, int logical_y);
SkinSelectAction  skin_select_handle_key   (SDL_Keycode k);

/* Advance the per-tile character animations by `dt` seconds.  Call once
 * per frame while the screen is up, before skin_select_render.  The
 * focused tile loops its attack swing; the rest idle.                   */
void skin_select_update(float dt);

void skin_select_render(SDL_Renderer *r,
                        UIFontPack   *fonts,
                        UITextCache  *cache,
                        SDL_Texture  *bg,
                        SDL_Texture  *button_tex);

#endif /* SKIN_SELECT_H */

#ifndef WEAPON_SELECT_H
#define WEAPON_SELECT_H

#include <SDL2/SDL.h>
#include "char_anim.h"
#include "ui.h"

/* The weapon picked on the last confirm is written into
 * player_cfg[target_slot].weapon (see src/two_player.h).              */

typedef enum {
    WS_ACTION_NONE = 0,
    WS_ACTION_CONFIRM,   /* player_cfg[target_slot].weapon updated      */
    WS_ACTION_CANCEL,    /* user pressed Escape                         */
} WeaponSelectAction;

/* Load icon textures (or fall back to portrait art).  Idempotent.     */
void weapon_select_init(SDL_Renderer *r);
void weapon_select_free(void);

/* Reset cursor/hover state when re-entering the screen and bind the
 * confirm action to a specific player slot (0 = campaign / P1, 1 = P2). */
void weapon_select_enter(int target_slot);

void               weapon_select_handle_motion(int logical_x, int logical_y);
WeaponSelectAction weapon_select_handle_click (int logical_x, int logical_y);
WeaponSelectAction weapon_select_handle_key   (SDL_Keycode k);

void weapon_select_render(SDL_Renderer *r,
                          UIFontPack   *fonts,
                          UITextCache  *cache,
                          SDL_Texture  *bg,
                          SDL_Texture  *button_tex);

#endif /* WEAPON_SELECT_H */

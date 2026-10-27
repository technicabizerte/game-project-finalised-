#ifndef TWO_PLAYER_H
#define TWO_PLAYER_H

#include <SDL2/SDL.h>
#include "char_anim.h"
#include "ui.h"

/* Shared two-player foundation: per-player selected character + per-player
 * input, plus a tiny HP-bar widget both co-op and versus HUDs can reuse.   */

#define MAX_PLAYERS 2

typedef struct {
    Skin   skin;
    Weapon weapon;
} PlayerCfg;

/* Per-player selection.  Slot 0 doubles as the campaign character; slot 1
 * is only populated when the user picks CO-OP or VERSUS.                  */
extern PlayerCfg player_cfg[MAX_PLAYERS];

typedef struct {
    int gauche, droite;   /* held: move L / R                              */
    int sauter;           /* held: jump                                    */
    int shoot;            /* one-shot edge for the frame                   */
    int melee;            /* one-shot edge for the frame                   */
} PlayerInput;

extern PlayerInput player_input[MAX_PLAYERS];

void two_player_init(void);

/* Fill player_input[0..1] from the keysHeld array and the one-shot edges
 * that main.c already tracks for shoot/melee.  Keep this the single
 * source of truth so co-op gameplay and versus arena both read the same
 * struct.                                                                */
void two_player_poll(const int *keysHeld,
                     int p1_shoot, int p1_melee,
                     int p2_shoot, int p2_melee);

/* HP bar that can be parked anywhere (left for P1, right for P2).
 * `accent` colors the fill + outline + label; `back` colors the empty
 * portion.  Caller supplies the same UIFontPack / UITextCache it uses
 * elsewhere so the bar stays consistent with the rest of the HUD.       */
void two_player_render_hp_bar(SDL_Renderer *r,
                              SDL_Rect      rect,
                              int           hp,
                              int           max_hp,
                              SDL_Color     accent,
                              SDL_Color     back,
                              const char   *label,
                              UIFontPack   *fonts,
                              UITextCache  *cache);

#endif /* TWO_PLAYER_H */

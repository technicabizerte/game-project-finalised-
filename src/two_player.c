#include "two_player.h"

/* Defaults applied to slot 0 (campaign) and slot 1 (joins on CO-OP /
 * VERSUS); the selects overwrite these once the user confirms.          */
PlayerCfg player_cfg[MAX_PLAYERS] = {
    [0] = { .skin = SKIN_ELLIE,  .weapon = WEAPON_SWORD },
    [1] = { .skin = SKIN_CURTIS, .weapon = WEAPON_AXE   },
};

PlayerInput player_input[MAX_PLAYERS];

void two_player_init(void)
{
    /* Reserved for future shared resources (HP-bar textures, gamepad
     * setup).  Defaults for player_cfg[] are already populated by the
     * static initializer above; reset the input snapshot just in case.  */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        PlayerInput zero = (PlayerInput){0};
        player_input[i] = zero;
    }
}

void two_player_poll(const int *keysHeld,
                     int p1_shoot, int p1_melee,
                     int p2_shoot, int p2_melee)
{
    if (!keysHeld) return;

    /* P1: ZQSD + SPACE/Z + F (shoot) + E (melee). */
    player_input[0].gauche = keysHeld[SDLK_q     & 0x1FF];
    player_input[0].droite = keysHeld[SDLK_d     & 0x1FF];
    player_input[0].sauter = keysHeld[SDLK_SPACE & 0x1FF] ||
                             keysHeld[SDLK_z     & 0x1FF];
    player_input[0].shoot  = p1_shoot;
    player_input[0].melee  = p1_melee;

    /* P2: arrows + UP-to-jump + K (shoot) + L (melee). */
    player_input[1].gauche = keysHeld[SDLK_LEFT  & 0x1FF];
    player_input[1].droite = keysHeld[SDLK_RIGHT & 0x1FF];
    player_input[1].sauter = keysHeld[SDLK_UP    & 0x1FF];
    player_input[1].shoot  = p2_shoot;
    player_input[1].melee  = p2_melee;
}

void two_player_render_hp_bar(SDL_Renderer *r,
                              SDL_Rect      rect,
                              int           hp,
                              int           max_hp,
                              SDL_Color     accent,
                              SDL_Color     back,
                              const char   *label,
                              UIFontPack   *fonts,
                              UITextCache  *cache)
{
    if (!r) return;
    if (max_hp <= 0) max_hp = 1;
    if (hp < 0)      hp = 0;
    if (hp > max_hp) hp = max_hp;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, back.r, back.g, back.b, 220);
    SDL_RenderFillRect(r, &rect);

    int fillW = (rect.w * hp) / max_hp;
    SDL_Rect fill = { rect.x, rect.y, fillW, rect.h };
    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
    SDL_RenderFillRect(r, &fill);

    SDL_SetRenderDrawColor(r, accent.r, accent.g, accent.b, 255);
    SDL_RenderDrawRect(r, &rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (label && fonts && cache && fonts->mono)
        ui_draw_text(cache, r, fonts->mono, label,
                     accent, rect.x, rect.y - 14);
}

#ifndef ENEMY_TAUNTS_H
#define ENEMY_TAUNTS_H

#include <SDL2/SDL.h>
#include "background.h"
#include "ui.h"
#include "llm_queue.h"

void enemy_taunts_init(void);

void enemy_taunts_update(Background *bg, Uint32 now_tick, LLMQueue *q);

void enemy_taunts_render(Background *bg, SDL_Renderer *r, int ox,
                          UIFontPack *fonts, UITextCache *cache);

#endif

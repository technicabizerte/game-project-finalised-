#ifndef WIRE_PUZZLE_H
#define WIRE_PUZZLE_H

#include <SDL2/SDL.h>
#include "ui.h"
#include "audio.h"
#include "gameplay.h"

void run_wire_puzzle(SDL_Renderer *renderer, SDL_Window *window,
                     int *score_bonus, int current_wagon,
                     UIFontPack *fonts, UITextCache *cache,
                     UIAssets *assets, AudioSystem *audio,
                     GameplayState *gp);

#endif

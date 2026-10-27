#ifndef INTERLUDE_MANAGER_H
#define INTERLUDE_MANAGER_H

#include <SDL2/SDL.h>
#include "ui.h"
#include "audio.h"
#include "gameplay.h"

typedef enum {
    INTERLUDE_NONE = 0,
    INTERLUDE_WIRE,
    INTERLUDE_PUZZLE,
    INTERLUDE_QUIZ
} InterludeType;

InterludeType get_interlude_type(int from_wagon);

void interlude_run(InterludeType type, SDL_Renderer *renderer, SDL_Window *window,
                   int *score_bonus, int current_wagon,
                   UIFontPack *fonts, UITextCache *cache,
                   UIAssets *assets, AudioSystem *audio,
                   GameplayState *gp);

#endif

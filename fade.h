#ifndef FADE_H
#define FADE_H

#include <SDL2/SDL.h>

/* Shared black-fade overlay used by the intro cutscene and the outro.
 *
 * Internally a single static alpha (0..255) is driven each frame by
 * fade_update().  Both intro and outro never run simultaneously, so a
 * module-global is the simplest fit and matches the "static alpha
 * value updated each frame" contract spelled out in the intro spec. */

void  fade_reset       (void);
void  fade_to_black    (float duration);
void  fade_from_black  (float duration);
void  fade_update      (float dt);
void  fade_render      (SDL_Renderer *r);
float fade_alpha       (void);
int   fade_is_active   (void);

#endif

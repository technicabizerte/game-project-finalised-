#ifndef HISTOIRE_H
#define HISTOIRE_H

#include <SDL2/SDL.h>

/* Show the Histoire screen (Play / Back buttons) and play video.mp4
 * via ffplay when Play is pressed. Returns 1 if the user requested to
 * quit the whole game (SDL_QUIT), 0 to return to the main menu. */
int histoire_screen_run(SDL_Window *window, SDL_Renderer *renderer,
                        const char *video_path);

#endif

#include <SDL2/SDL.h>
#include <stdio.h>

int enigme_run(SDL_Window *window, SDL_Renderer *renderer);

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *w = SDL_CreateWindow(
        "Snowpiercer - Enigme",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1000, 600,
        SDL_WINDOW_SHOWN
    );

    if(!w){
        printf("CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *r = SDL_CreateRenderer(
        w, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if(!r){
        printf("CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        SDL_Quit();
        return 1;
    }

    enigme_run(w, r);

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
}


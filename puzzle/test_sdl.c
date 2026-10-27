
#include <SDL2/SDL.h>
#include <stdio.h>

int main(void)
{
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;

    printf("Testing SDL2 installation...\n");

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        printf("ERROR: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("SDL2 initialized successfully!\n");

    win = SDL_CreateWindow("SDL2 Test",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           400, 300, SDL_WINDOW_SHOWN);
    if (!win)
    {
        printf("ERROR: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    printf("Window created successfully!\n");

    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
    {
        printf("ERROR: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    printf("Renderer created successfully!\n");

    SDL_SetRenderDrawColor(ren, 50, 100, 150, 255);
    SDL_RenderClear(ren);

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_Rect rect = {150, 100, 100, 100};
    SDL_RenderFillRect(ren, &rect);

    SDL_RenderPresent(ren);

    printf("Test pattern displayed. Close window to exit.\n");

    SDL_Event e;
    int running = 1;
    while (running)
    {
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = 0;
        }
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    printf("SDL2 test completed successfully!\n");
    return 0;
}

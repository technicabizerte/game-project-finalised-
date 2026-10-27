#include "histoire.h"

#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>

#define SCR_W 800
#define SCR_H 520

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *s,
                      int cx, int cy, SDL_Color c)
{
    if (!f || !s) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = {cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

static void draw_button(SDL_Renderer *r, TTF_Font *f, const SDL_Rect *rc,
                        const char *label, int hovered)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (hovered) SDL_SetRenderDrawColor(r, 200, 70, 70, 230);
    else         SDL_SetRenderDrawColor(r, 40, 50, 70, 210);
    SDL_RenderFillRect(r, rc);
    SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
    SDL_RenderDrawRect(r, rc);
    if (hovered) {
        SDL_Rect inner = {rc->x + 2, rc->y + 2, rc->w - 4, rc->h - 4};
        SDL_RenderDrawRect(r, &inner);
    }
    draw_text(r, f, label, rc->x + rc->w / 2, rc->y + rc->h / 2,
              (SDL_Color){255, 255, 255, 255});
}

/* Launch ffplay as a child process and wait for it to exit.
 * ffplay built-in controls: F=fullscreen, ESC/Q=quit, SPACE=pause,
 * double-click=fullscreen, close-window=quit. */
static int play_with_ffplay(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "[histoire] video file not found: %s (%s)\n",
                path, strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[histoire] fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execlp("ffplay",
               "ffplay",
               "-autoexit",
               "-window_title", "Histoire",
               "-x", "960",
               "-y", "540",
               path,
               (char *)NULL);
        fprintf(stderr, "[histoire] ffplay exec failed: %s\n", strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "[histoire] waitpid failed: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

int histoire_screen_run(SDL_Window *window, SDL_Renderer *renderer,
                        const char *video_path)
{
    (void)window;

    TTF_Font *titleFont = TTF_OpenFont("arial.ttf", 42);
    TTF_Font *btnFont   = TTF_OpenFont("arial.ttf", 22);
    TTF_Font *infoFont  = TTF_OpenFont("arial.ttf", 16);

    SDL_Rect playBtn   = {SCR_W / 2 - 160, 260, 320, 60};
    SDL_Rect returnBtn = {SCR_W / 2 - 160, 340, 320, 60};

    /* Pause menu music while user is on this screen so it doesn't
     * fight with ffplay's audio. Resume on exit. */
    int wasPlaying = Mix_PlayingMusic();
    if (wasPlaying) Mix_PauseMusic();

    int running    = 1;
    int returnFlag = 0;
    int hoverPlay = 0, hoverRet = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0; returnFlag = 1;
            }
            else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                SDL_Point p = {mx, my};
                hoverPlay = SDL_PointInRect(&p, &playBtn);
                hoverRet  = SDL_PointInRect(&p, &returnBtn);
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN &&
                     e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                SDL_Point p = {mx, my};
                if (SDL_PointInRect(&p, &playBtn)) {
                    play_with_ffplay(video_path);
                }
                else if (SDL_PointInRect(&p, &returnBtn)) {
                    running = 0;
                }
            }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) {
                    running = 0;
                }
                else if (k == SDLK_RETURN || k == SDLK_SPACE) {
                    play_with_ffplay(video_path);
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 8, 10, 20, 255);
        SDL_RenderClear(renderer);

        /* subtle frame */
        SDL_SetRenderDrawColor(renderer, 60, 80, 110, 255);
        SDL_Rect frame = {30, 30, SCR_W - 60, SCR_H - 60};
        SDL_RenderDrawRect(renderer, &frame);

        draw_text(renderer, titleFont, "HISTOIRE",
                  SCR_W / 2, 110, (SDL_Color){240, 230, 200, 255});
        draw_text(renderer, infoFont,
                  "Cliquez pour lire la video. Pendant la lecture :",
                  SCR_W / 2, 180, (SDL_Color){200, 200, 210, 255});
        draw_text(renderer, infoFont,
                  "F = plein ecran    ESC = retour",
                  SCR_W / 2, 205, (SDL_Color){180, 200, 230, 255});

        draw_button(renderer, btnFont, &playBtn,   "JOUER LA VIDEO", hoverPlay);
        draw_button(renderer, btnFont, &returnBtn, "RETOUR AU MENU", hoverRet);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (titleFont) TTF_CloseFont(titleFont);
    if (btnFont)   TTF_CloseFont(btnFont);
    if (infoFont)  TTF_CloseFont(infoFont);

    if (wasPlaying) Mix_ResumeMusic();

    return returnFlag;
}

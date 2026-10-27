#include "highscore_screen.h"
#include "high score menu /leaderboard.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static SDL_Texture *load_tex(SDL_Renderer *r, const char *p)
{
    SDL_Surface *s = IMG_Load(p);
    if (!s) { fprintf(stderr, "[hs] '%s': %s\n", p, IMG_GetError()); return NULL; }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(s);
    return t;
}

int highscore_screen_run(SDL_Window *window, SDL_Renderer *renderer)
{
    (void)window;

    char saved_cwd[1024];
    if (!getcwd(saved_cwd, sizeof(saved_cwd))) saved_cwd[0] = '\0';

    /* leaderboard button assets live in quizz/enigme/assets — load before chdir */
    SDL_Texture *retourNorm  = load_tex(renderer, "quizz/enigme/assets/retour1.png");
    SDL_Texture *retourHov   = load_tex(renderer, "quizz/enigme/assets/hretour.png");
    SDL_Texture *quitterNorm = load_tex(renderer, "quizz/enigme/assets/quitter.png");
    SDL_Texture *quitterHov  = load_tex(renderer, "quizz/enigme/assets/hquitter.png");

    if (chdir("high score menu ") != 0) {
        fprintf(stderr, "[hs] cannot chdir into high score menu \n");
        if (retourNorm)  SDL_DestroyTexture(retourNorm);
        if (retourHov)   SDL_DestroyTexture(retourHov);
        if (quitterNorm) SDL_DestroyTexture(quitterNorm);
        if (quitterHov)  SDL_DestroyTexture(quitterHov);
        return 0;
    }

    SDL_Texture *lbBgTex = load_tex(renderer, "background4.png");

    TTF_Font *font      = TTF_OpenFont("arial.ttf", 24);
    TTF_Font *smallFont = TTF_OpenFont("arial.ttf", 18);
    if (!font || !smallFont) {
        fprintf(stderr, "[hs] fonts missing\n");
        if (font) TTF_CloseFont(font);
        if (smallFont) TTF_CloseFont(smallFont);
        if (lbBgTex) SDL_DestroyTexture(lbBgTex);
        if (retourNorm)  SDL_DestroyTexture(retourNorm);
        if (retourHov)   SDL_DestroyTexture(retourHov);
        if (quitterNorm) SDL_DestroyTexture(quitterNorm);
        if (quitterHov)  SDL_DestroyTexture(quitterHov);
        if (saved_cwd[0]) chdir(saved_cwd);
        return 0;
    }

    Mix_Music *victoryMusic = Mix_LoadMUS("music.mp3");
    Mix_Chunk *hoverSound   = Mix_LoadWAV("hover.wav");

    /* load + sort players, then build podium name/score textures */
    Player allPlayers[100];
    int playerCount = loadPlayers(allPlayers);
    sortPlayers(allPlayers, playerCount);

    SDL_Texture *lbTex[3]      = {NULL, NULL, NULL};
    SDL_Texture *lbScoreTex[3] = {NULL, NULL, NULL};
    for (int i = 0; i < 3 && i < playerCount; i++) {
        SDL_Surface *ns = TTF_RenderText_Solid(font, allPlayers[i].name,
                            (SDL_Color){0, 0, 50, 255});
        lbTex[i] = ns ? SDL_CreateTextureFromSurface(renderer, ns) : NULL;
        if (ns) SDL_FreeSurface(ns);
        char st[24];
        snprintf(st, sizeof(st), "%d", allPlayers[i].score);
        SDL_Surface *ss = TTF_RenderText_Solid(smallFont, st,
                            (SDL_Color){20, 20, 60, 255});
        lbScoreTex[i] = ss ? SDL_CreateTextureFromSurface(renderer, ss) : NULL;
        if (ss) SDL_FreeSurface(ss);
    }

    Mix_HaltMusic();
    if (victoryMusic) Mix_PlayMusic(victoryMusic, -1);

    SDL_Rect retourBtn  = {140, 446, 220, 64};
    SDL_Rect quitterBtn = {440, 446, 220, 64};
    int retourHovered = 0, quitterHovered = 0;

    int running    = 1;
    int returnFlag = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = 0; returnFlag = 1; }
            else if (e.type == SDL_MOUSEMOTION) {
                int mx = e.motion.x, my = e.motion.y;
                int prevR = retourHovered, prevQ = quitterHovered;
                retourHovered = (mx >= retourBtn.x && mx <= retourBtn.x + retourBtn.w &&
                                 my >= retourBtn.y && my <= retourBtn.y + retourBtn.h);
                quitterHovered = (mx >= quitterBtn.x && mx <= quitterBtn.x + quitterBtn.w &&
                                  my >= quitterBtn.y && my <= quitterBtn.y + quitterBtn.h);
                if (hoverSound && retourHovered && !prevR)
                    Mix_PlayChannel(-1, hoverSound, 0);
                if (hoverSound && quitterHovered && !prevQ)
                    Mix_PlayChannel(-1, hoverSound, 0);
            }
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
            }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int mx = e.button.x, my = e.button.y;
                if (mx >= retourBtn.x && mx <= retourBtn.x + retourBtn.w &&
                    my >= retourBtn.y && my <= retourBtn.y + retourBtn.h) {
                    running = 0;            /* returnFlag stays 0 -> back to main menu */
                }
                if (mx >= quitterBtn.x && mx <= quitterBtn.x + quitterBtn.w &&
                    my >= quitterBtn.y && my <= quitterBtn.y + quitterBtn.h) {
                    running = 0; returnFlag = 1;   /* quit the whole game */
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        if (lbBgTex) SDL_RenderCopy(renderer, lbBgTex, NULL, NULL);

        int slots[3][2] = {{396, 238}, {274, 250}, {515, 255}};
        for (int i = 0; i < 3; i++) {
            if (lbTex[i] && playerCount > i) {
                SDL_Rect nr = {0, slots[i][1], 0, 0};
                SDL_QueryTexture(lbTex[i], NULL, NULL, &nr.w, &nr.h);
                nr.x = slots[i][0] - nr.w / 2;
                SDL_RenderCopy(renderer, lbTex[i], NULL, &nr);
                if (lbScoreTex[i]) {
                    SDL_Rect sr = {0, slots[i][1] + 26, 0, 0};
                    SDL_QueryTexture(lbScoreTex[i], NULL, NULL, &sr.w, &sr.h);
                    sr.x = slots[i][0] - sr.w / 2;
                    SDL_RenderCopy(renderer, lbScoreTex[i], NULL, &sr);
                }
            }
        }

        SDL_Texture *rt = retourHovered  ? (retourHov  ? retourHov  : retourNorm)
                                         : retourNorm;
        SDL_Texture *qt = quitterHovered ? (quitterHov ? quitterHov : quitterNorm)
                                         : quitterNorm;
        if (rt) SDL_RenderCopy(renderer, rt, NULL, &retourBtn);
        else {
            SDL_SetRenderDrawColor(renderer, 70, 80, 110, 255);
            SDL_RenderFillRect(renderer, &retourBtn);
        }
        if (qt) SDL_RenderCopy(renderer, qt, NULL, &quitterBtn);
        else {
            SDL_SetRenderDrawColor(renderer, 110, 50, 50, 255);
            SDL_RenderFillRect(renderer, &quitterBtn);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    for (int i = 0; i < 3; i++) {
        if (lbTex[i])      SDL_DestroyTexture(lbTex[i]);
        if (lbScoreTex[i]) SDL_DestroyTexture(lbScoreTex[i]);
    }
    if (hoverSound)   Mix_FreeChunk(hoverSound);
    if (victoryMusic) { Mix_HaltMusic(); Mix_FreeMusic(victoryMusic); }
    TTF_CloseFont(font);
    TTF_CloseFont(smallFont);
    if (lbBgTex)     SDL_DestroyTexture(lbBgTex);
    if (retourNorm)  SDL_DestroyTexture(retourNorm);
    if (retourHov)   SDL_DestroyTexture(retourHov);
    if (quitterNorm) SDL_DestroyTexture(quitterNorm);
    if (quitterHov)  SDL_DestroyTexture(quitterHov);

    if (saved_cwd[0]) chdir(saved_cwd);

    return returnFlag;
}

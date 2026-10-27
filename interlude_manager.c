#include "interlude_manager.h"
#include "wire_puzzle.h"
#include "background.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

extern int run_enigme2_with_renderer(SDL_Renderer *renderer, SDL_Window *window);
extern int enigme_run(SDL_Window *window, SDL_Renderer *renderer);

InterludeType get_interlude_type(int from_wagon)
{
    /* from_wagon is the destination wagon (0-indexed internally).
     * Maps to user-facing wagon (from_wagon + 1):
     *   from_wagon = 1 → entering user-wagon 2
     *   from_wagon = 2 → entering user-wagon 3 ← QUIZ moved here
     *   from_wagon = 3 → entering user-wagon 4 ← PUZZLE stays
     *   from_wagon = 4 → entering user-wagon 5 (boss) — no interlude
     *                    so nothing fires between the player and the
     *                    boss cinematic.                              */
    switch (from_wagon) {
        case 1: return INTERLUDE_WIRE;
        case 2: return INTERLUDE_QUIZ;
        case 3: return INTERLUDE_PUZZLE;
        case 4: return INTERLUDE_NONE;
        default: return INTERLUDE_NONE;
    }
}

/* Dialog screen shown right before each interlude minigame starts.
 * Freezes the wagon scene under a dim overlay and prompts the player
 * to confirm before the actual challenge takes over. */
static void show_interlude_prompt(SDL_Renderer *renderer,
                                  InterludeType type,
                                  UIFontPack *fonts,
                                  UITextCache *cache)
{
    if (!renderer || type == INTERLUDE_NONE) return;

    const char *title = "CHALLENGE";
    switch (type) {
        case INTERLUDE_WIRE:   title = "WIRE PUZZLE";   break;
        case INTERLUDE_PUZZLE: title = "JIGSAW PUZZLE"; break;
        case INTERLUDE_QUIZ:   title = "QUIZ";          break;
        default: break;
    }
    const char *line1 = "To reach the next wagon,";
    const char *line2 = "solve this challenge first.";
    const char *hint  = "Press any key or click to start";

    int w = W_ECRAN, h = H_ECRAN;
    SDL_RenderGetLogicalSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) { w = W_ECRAN; h = H_ECRAN; }

    /* Snapshot the current backbuffer (wagon scene) so we can keep
     * showing it dimmed behind the dialog. If readback fails (some
     * drivers refuse), we just fall back to a black background.
     *
     * IMPORTANT: SDL_RenderReadPixels reads at the renderer's *physical*
     * output size, NOT the logical size. If the window is fullscreen or
     * resized, allocating at logical size overflows the surface buffer
     * and corrupts the heap (observed crashing later inside SDL2). Always
     * allocate at the actual output size. */
    SDL_Texture *snapshot = NULL;
    int phys_w = w, phys_h = h;
    SDL_GetRendererOutputSize(renderer, &phys_w, &phys_h);
    if (phys_w <= 0 || phys_h <= 0) { phys_w = w; phys_h = h; }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, phys_w, phys_h, 32,
                                                      SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGBA32,
                                 surf->pixels, surf->pitch) == 0) {
            snapshot = SDL_CreateTextureFromSurface(renderer, surf);
        }
        SDL_FreeSurface(surf);
    }

    SDL_Rect box;
    box.w = (int)(w * 0.62f);
    box.h = (int)(h * 0.45f);
    box.x = (w - box.w) / 2;
    box.y = (h - box.h) / 2;

    SDL_Color colTitle = { 240, 220, 120, 255 };
    SDL_Color colBody  = { 230, 230, 230, 255 };
    SDL_Color colHint  = { 200, 200, 200, 255 };

    TTF_Font *fTitle = fonts ? (fonts->bebasTitle ? fonts->bebasTitle : fonts->fallback) : NULL;
    TTF_Font *fBody  = fonts ? (fonts->novaLabel  ? fonts->novaLabel  : fonts->fallback) : NULL;
    TTF_Font *fHint  = fonts ? (fonts->monoSmall  ? fonts->monoSmall  : fonts->fallback) : NULL;

    int done = 0;
    Uint32 startTick = SDL_GetTicks();
    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    done = 1;
                    break;
                case SDL_KEYDOWN:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_JOYBUTTONDOWN:
                    /* Eat a 250 ms grace window so an input still held
                     * from the wagon scene doesn't dismiss instantly. */
                    if (SDL_GetTicks() - startTick > 250) done = 1;
                    break;
                default: break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (snapshot) SDL_RenderCopy(renderer, snapshot, NULL, NULL);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        SDL_Rect full = { 0, 0, w, h };
        SDL_RenderFillRect(renderer, &full);

        SDL_SetRenderDrawColor(renderer, 18, 22, 30, 235);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderDrawColor(renderer, 200, 180, 90, 255);
        SDL_RenderDrawRect(renderer, &box);
        SDL_Rect inner = { box.x + 2, box.y + 2, box.w - 4, box.h - 4 };
        SDL_RenderDrawRect(renderer, &inner);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        if (cache) {
            int cx = box.x + box.w / 2;
            int yCursor = box.y + (int)(box.h * 0.14f);
            if (fTitle) {
                ui_draw_text_centered(cache, renderer, fTitle, title,
                                      colTitle, cx, yCursor);
                yCursor += (int)(box.h * 0.32f);
            }
            if (fBody) {
                ui_draw_text_centered(cache, renderer, fBody, line1,
                                      colBody, cx, yCursor);
                yCursor += (int)(box.h * 0.14f);
                ui_draw_text_centered(cache, renderer, fBody, line2,
                                      colBody, cx, yCursor);
            }
            if (fHint) {
                int hintY = box.y + box.h - (int)(box.h * 0.18f);
                ui_draw_text_centered(cache, renderer, fHint, hint,
                                      colHint, cx, hintY);
            }
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (snapshot) SDL_DestroyTexture(snapshot);
}

static void run_puzzle_wrapped(SDL_Renderer *renderer, SDL_Window *window,
                                int *score_bonus, int current_wagon,
                                GameplayState *gp)
{
    char saved_cwd[1024];
    if (!getcwd(saved_cwd, sizeof(saved_cwd))) saved_cwd[0] = '\0';
    Uint32 startTick = SDL_GetTicks();

    if (chdir("puzzle") == 0) {

        SDL_RenderSetLogicalSize(renderer, 960, 480);
        run_enigme2_with_renderer(renderer, window);
        SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
        if (saved_cwd[0]) chdir(saved_cwd);
    } else {
        fprintf(stderr, "[interlude] cannot chdir to puzzle\n");
    }

    Uint32 dur = (SDL_GetTicks() - startTick) / 1000;
    int bonus = 1000 - (int)dur * 10;
    if (bonus < 100) bonus = 100;
    if (score_bonus) *score_bonus = bonus;
    if (gp) gp->totalInterludeScore += bonus;
    (void)current_wagon;
}

static void run_quiz_wrapped(SDL_Renderer *renderer, SDL_Window *window,
                              int *score_bonus, int current_wagon,
                              GameplayState *gp)
{
    char saved_cwd[1024];
    if (!getcwd(saved_cwd, sizeof(saved_cwd))) saved_cwd[0] = '\0';
    Uint32 startTick = SDL_GetTicks();

    if (chdir("quizz/enigme") == 0) {
        SDL_RenderSetLogicalSize(renderer, 1000, 600);
        enigme_run(window, renderer);
        SDL_RenderSetLogicalSize(renderer, W_ECRAN, H_ECRAN);
        if (saved_cwd[0]) chdir(saved_cwd);
    } else {
        fprintf(stderr, "[interlude] cannot chdir to quizz/enigme\n");
    }

    Uint32 dur = (SDL_GetTicks() - startTick) / 1000;
    int bonus = 1200 - (int)dur * 10;
    if (bonus < 150) bonus = 150;
    if (score_bonus) *score_bonus = bonus;
    if (gp) gp->totalInterludeScore += bonus;
    (void)current_wagon;
}

void interlude_run(InterludeType type, SDL_Renderer *renderer, SDL_Window *window,
                   int *score_bonus, int current_wagon,
                   UIFontPack *fonts, UITextCache *cache,
                   UIAssets *assets, AudioSystem *audio,
                   GameplayState *gp)
{
    int dummy = 0;
    if (!score_bonus) score_bonus = &dummy;
    *score_bonus = 0;

    if (type != INTERLUDE_NONE)
        show_interlude_prompt(renderer, type, fonts, cache);

    switch (type) {
        case INTERLUDE_WIRE:
            run_wire_puzzle(renderer, window, score_bonus, current_wagon,
                            fonts, cache, assets, audio, gp);
            break;
        case INTERLUDE_PUZZLE:
            run_puzzle_wrapped(renderer, window, score_bonus, current_wagon, gp);
            break;
        case INTERLUDE_QUIZ:
            run_quiz_wrapped(renderer, window, score_bonus, current_wagon, gp);
            break;
        case INTERLUDE_NONE:
        default:
            break;
    }
}

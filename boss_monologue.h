#ifndef BOSS_MONOLOGUE_H
#define BOSS_MONOLOGUE_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "background.h"
#include "gameplay.h"
#include "ui.h"
#include "llm_queue.h"

#define BOSS_MONO_TEXT_MAX  360
#define BOSS_MONO_WAGONS    5

typedef struct {
    bool   fired_for_wagon[BOSS_MONO_WAGONS];

    bool   pending;
    bool   showing;
    int    show_wagon;
    char   text[BOSS_MONO_TEXT_MAX];

    Uint32 show_started_tick;
    Uint32 request_tick;
} BossMonologue;

void boss_monologue_init(BossMonologue *bm);

void boss_monologue_reset_run(BossMonologue *bm);

void boss_monologue_update(BossMonologue *bm,
                            Background *bg,
                            const struct GameplayState *gp,
                            Uint32 game_start_tick,
                            Uint32 now_tick,
                            LLMQueue *q);

void boss_monologue_render(const BossMonologue *bm, SDL_Renderer *r,
                            UIFontPack *fonts, UITextCache *cache);

#endif

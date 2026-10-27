#ifndef BARKER_H
#define BARKER_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include "ui.h"
#include "llm_queue.h"

#define BARKER_MAX_WAGONS  5
#define BARKER_TEXT_MAX    220

typedef struct {
    char   text[BARKER_TEXT_MAX];
    bool   ready;
    bool   pending;
    bool   used_fallback;
} BarkerCacheEntry;

typedef struct {
    BarkerCacheEntry cache[BARKER_MAX_WAGONS];

    int    last_seen_wagon;
    int    show_wagon;
    Uint32 entered_tick;
    Uint32 show_started_tick;
    bool   showing;
    bool   awaiting;
} WagonBarker;

void wagon_barker_init(WagonBarker *b);

void wagon_barker_on_enter_wagon(WagonBarker *b, int wagon, LLMQueue *q);

void wagon_barker_update(WagonBarker *b, int current_wagon, Uint32 now,
                          LLMQueue *q);

void wagon_barker_render(const WagonBarker *b, SDL_Renderer *r,
                          UIFontPack *fonts, UITextCache *cache);

#endif

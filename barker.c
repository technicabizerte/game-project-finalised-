#include "barker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BARKER_SHOW_MS         6500u
#define BARKER_FALLBACK_MS     4500u
#define BARKER_FADE_MS         350u

typedef struct {
    const char *name;
    const char *theme;
    const char *fallback;
} WagonBarkerInfo;

static const WagonBarkerInfo WAGON_INFO[BARKER_MAX_WAGONS] = {
    { "TAIL CARS",
      "the squalid tail of the train where the poor live",
      "Tail-enders. Maintain assigned positions. Wilford's mercy is finite." },
    { "INDUSTRIAL CAR",
      "a roaring factory wagon producing protein and tools",
      "Workers of the Engine: meet your daily quota or freeze in the snow." },
    { "AQUARIUM CAR",
      "a glittering wagon of preserved fish and sushi for the elite",
      "First-class guests: your fresh sashimi service begins shortly." },
    { "GREENHOUSE CAR",
      "a humid garden wagon, the only green that remains in the world",
      "Behold the Eternal Engine's gift: green leaves, in a world of white." },
    { "LUXURY CAR",
      "a champagne-and-velvet wagon for the wealthiest passengers",
      "Premium passengers, your evening of refined indulgence awaits." }
};

static void clean_response(char *s)
{
    if (!s) return;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        memmove(s, s + 1, strlen(s));
    }
    size_t L = strlen(s);
    while (L > 0 && (s[L-1] == ' ' || s[L-1] == '\n' ||
                     s[L-1] == '\r' || s[L-1] == '\t')) {
        s[--L] = 0;
    }
    if (L >= 2 && s[0] == '"' && s[L-1] == '"') {
        memmove(s, s + 1, L - 2);
        s[L - 2] = 0;
    }
    static const char *prefixes[] = {
        "ANNOUNCEMENT:", "Announcement:", "announcement:",
        "PA:", "Pa:", "pa:",
        "BARKER:", "Barker:", "barker:",
        "SAY:", "Say:", "say:",
        "ANSWER:", "Answer:", "answer:",
        NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t p = strlen(prefixes[i]);
        if (strncmp(s, prefixes[i], p) == 0) {
            memmove(s, s + p, strlen(s + p) + 1);
            while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
            break;
        }
    }
    L = strlen(s);
    if (L > 0) {
        char *nl = strchr(s, '\n');
        if (nl) *nl = 0;
    }
}

typedef struct {
    WagonBarker *barker;
    int          wagon;
} BarkerCallbackCtx;

static BarkerCallbackCtx g_ctx[BARKER_MAX_WAGONS];

static void on_barker_response(bool ok, const char *response, void *userdata)
{
    BarkerCallbackCtx *ctx = (BarkerCallbackCtx*)userdata;
    if (!ctx || !ctx->barker) return;
    int w = ctx->wagon;
    if (w < 0 || w >= BARKER_MAX_WAGONS) return;
    BarkerCacheEntry *e = &ctx->barker->cache[w];
    e->pending = false;
    if (ok && response && response[0]) {
        char tmp[BARKER_TEXT_MAX];
        strncpy(tmp, response, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        clean_response(tmp);
        if (tmp[0]) {
            strncpy(e->text, tmp, sizeof(e->text) - 1);
            e->text[sizeof(e->text) - 1] = 0;
            e->ready = true;
        }
    }
}

static void build_prompt(int wagon, char *out, size_t cap)
{
    const WagonBarkerInfo *w = &WAGON_INFO[wagon];
    snprintf(out, cap,
        "<start_of_turn>user\n"
        "You write 1-line public-address announcements for the Snowpiercer train, "
        "where survivors of a frozen Earth live in a perpetual-motion train run by Mr. Wilford. "
        "Reply in English, in ONE short sentence (max 18 words). "
        "No preamble, no quotes, no asterisks, no labels. Speak in the voice of the train's PA system.\n\n"
        "Wagon: %s.\nDescription: %s.\n\n"
        "Write the announcement for that wagon.<end_of_turn>\n"
        "<start_of_turn>model\n",
        w->name, w->theme);
}

void wagon_barker_init(WagonBarker *b)
{
    if (!b) return;
    memset(b, 0, sizeof(*b));
    b->last_seen_wagon = -1;
    b->show_wagon      = -1;
    b->showing         = false;
    b->awaiting        = false;
}

static void try_request(WagonBarker *b, int wagon, LLMQueue *q)
{
    if (!b || !q) return;
    if (wagon < 0 || wagon >= BARKER_MAX_WAGONS) return;
    BarkerCacheEntry *e = &b->cache[wagon];
    if (e->ready || e->pending) return;

    char prompt[1024];
    build_prompt(wagon, prompt, sizeof(prompt));

    g_ctx[wagon].barker = b;
    g_ctx[wagon].wagon  = wagon;

    if (llm_queue_submit(q, LLM_PRIO_LOW, prompt, 60, 0.85f,
                          on_barker_response, &g_ctx[wagon])) {
        e->pending = true;
    }
}

void wagon_barker_on_enter_wagon(WagonBarker *b, int wagon, LLMQueue *q)
{
    if (!b) return;
    if (wagon < 0 || wagon >= BARKER_MAX_WAGONS) return;

    b->show_wagon         = wagon;
    b->entered_tick       = SDL_GetTicks();
    b->show_started_tick  = 0;
    b->showing            = false;
    b->awaiting           = true;
    b->last_seen_wagon    = wagon;

    BarkerCacheEntry *e = &b->cache[wagon];
    e->used_fallback = false;

    try_request(b, wagon, q);
}

void wagon_barker_update(WagonBarker *b, int current_wagon, Uint32 now,
                          LLMQueue *q)
{
    if (!b) return;
    if (current_wagon != b->last_seen_wagon &&
        current_wagon >= 0 && current_wagon < BARKER_MAX_WAGONS) {
        wagon_barker_on_enter_wagon(b, current_wagon, q);
    }

    if (b->awaiting && b->show_wagon >= 0 && b->show_wagon < BARKER_MAX_WAGONS) {
        BarkerCacheEntry *e = &b->cache[b->show_wagon];
        if (e->ready) {
            b->showing           = true;
            b->awaiting          = false;
            b->show_started_tick = now;
        } else if (now - b->entered_tick >= BARKER_FALLBACK_MS) {
            strncpy(e->text, WAGON_INFO[b->show_wagon].fallback,
                    sizeof(e->text) - 1);
            e->text[sizeof(e->text) - 1] = 0;
            e->ready          = true;
            e->used_fallback  = true;
            b->showing        = true;
            b->awaiting       = false;
            b->show_started_tick = now;
        }
    }

    if (b->showing && now - b->show_started_tick >= BARKER_SHOW_MS) {
        b->showing  = false;
        b->awaiting = false;
    }
}

static int compute_alpha(Uint32 elapsed)
{
    if (elapsed < BARKER_FADE_MS) {
        return (int)((elapsed * 255u) / BARKER_FADE_MS);
    }
    if (elapsed > BARKER_SHOW_MS - BARKER_FADE_MS) {
        Uint32 left = BARKER_SHOW_MS - elapsed;
        return (int)((left * 255u) / BARKER_FADE_MS);
    }
    return 255;
}

void wagon_barker_render(const WagonBarker *b, SDL_Renderer *r,
                          UIFontPack *fonts, UITextCache *cache)
{
    if (!b || !r || !fonts) return;
    if (!b->showing) return;
    if (b->show_wagon < 0 || b->show_wagon >= BARKER_MAX_WAGONS) return;

    const BarkerCacheEntry *e = &b->cache[b->show_wagon];
    if (!e->ready || !e->text[0]) return;

    Uint32 now     = SDL_GetTicks();
    Uint32 elapsed = now - b->show_started_tick;
    int alpha = compute_alpha(elapsed);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    int box_w = 720;
    int box_h = 70;
    int box_x = 40;
    int box_y = 12;
    SDL_Rect box = { box_x, box_y, box_w, box_h };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 18, 14, 6, (Uint8)((alpha * 220) / 255));
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 150, 50, (Uint8)alpha);
    SDL_RenderDrawRect(r, &box);
    SDL_Rect inner = { box.x + 1, box.y + 1, box.w - 2, box.h - 2 };
    SDL_SetRenderDrawColor(r, 90, 60, 20, (Uint8)alpha);
    SDL_RenderDrawRect(r, &inner);

    int strip_w = 6;
    SDL_Rect lstrip = { box.x + 8, box.y + 8, strip_w, box.h - 16 };
    SDL_Rect rstrip = { box.x + box.w - 8 - strip_w, box.y + 8, strip_w, box.h - 16 };
    SDL_SetRenderDrawColor(r, 220, 80, 40, (Uint8)alpha);
    SDL_RenderFillRect(r, &lstrip);
    SDL_RenderFillRect(r, &rstrip);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    TTF_Font *fLabel = fonts->novaLabel ? fonts->novaLabel : fonts->fallback;
    TTF_Font *fBody  = fonts->story     ? fonts->story     : fonts->fallback;

    SDL_Color amber = {255, 200, 90, (Uint8)alpha};
    SDL_Color cream = {235, 225, 200, (Uint8)alpha};

    char header[64];
    snprintf(header, sizeof(header), "::: PA --- %s :::",
             WAGON_INFO[b->show_wagon].name);
    ui_draw_text_centered((UITextCache*)cache, r, fLabel, header, amber,
                          box.x + box.w / 2, box.y + 6);

    int tw = 0, th = 0;
    TTF_SizeUTF8(fBody, e->text, &tw, &th);
    int max_w = box_w - 60;
    if (tw <= max_w) {
        ui_draw_text_centered((UITextCache*)cache, r, fBody, e->text, cream,
                              box.x + box.w / 2, box.y + 32);
    } else {
        char line1[BARKER_TEXT_MAX];
        char line2[BARKER_TEXT_MAX];
        line1[0] = 0; line2[0] = 0;
        int len  = (int)strlen(e->text);
        int mid  = len / 2;
        int split = mid;
        for (int i = mid; i < len; i++) {
            if (e->text[i] == ' ') { split = i; break; }
        }
        if (split <= 0 || split >= len) split = mid;
        memcpy(line1, e->text, split);
        line1[split] = 0;
        const char *src2 = e->text + split;
        while (*src2 == ' ') src2++;
        strncpy(line2, src2, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = 0;
        ui_draw_text_centered((UITextCache*)cache, r, fBody, line1, cream,
                              box.x + box.w / 2, box.y + 26);
        ui_draw_text_centered((UITextCache*)cache, r, fBody, line2, cream,
                              box.x + box.w / 2, box.y + 46);
    }
}

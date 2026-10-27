#include "ui.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FONT_DIR "assets/fonts "

static TTF_Font *try_open(const char *path, int size)
{
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) fprintf(stderr, "[ui] TTF_OpenFont '%s' @%d: %s\n", path, size, TTF_GetError());
    return f;
}

int ui_fonts_load(UIFontPack *pack)
{
    memset(pack, 0, sizeof(*pack));

    const char *fallbacks[] = {
        "arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        NULL
    };
    for (int i = 0; fallbacks[i] && !pack->fallback; i++)
        pack->fallback = TTF_OpenFont(fallbacks[i], 18);

    pack->bebasTitle  = try_open(FONT_DIR "/Bebas_Neue/BebasNeue-Regular.ttf",      48);
    pack->bebasHeader = try_open(FONT_DIR "/Bebas_Neue/BebasNeue-Regular.ttf",      28);
    pack->novaButton  = try_open(FONT_DIR "/Nova_Square/NovaSquare-Regular.ttf",    20);
    pack->novaLabel   = try_open(FONT_DIR "/Nova_Square/NovaSquare-Regular.ttf",    16);
    pack->mono        = try_open(FONT_DIR "/Share_Tech_Mono/ShareTechMono-Regular.ttf", 18);
    pack->monoLarge   = try_open(FONT_DIR "/Share_Tech_Mono/ShareTechMono-Regular.ttf", 34);
    pack->monoSmall   = try_open(FONT_DIR "/Share_Tech_Mono/ShareTechMono-Regular.ttf", 12);
    pack->story       = try_open(FONT_DIR "/Special_Elite/SpecialElite-Regular.ttf", 20);
    pack->storySmall  = try_open(FONT_DIR "/Special_Elite/SpecialElite-Regular.ttf", 16);

    TTF_Font **slots[] = {&pack->bebasTitle, &pack->bebasHeader, &pack->novaButton,
                           &pack->novaLabel, &pack->mono,        &pack->monoLarge,
                           &pack->monoSmall, &pack->story,       &pack->storySmall};
    for (size_t i = 0; i < sizeof(slots)/sizeof(slots[0]); i++)
        if (!*slots[i]) *slots[i] = pack->fallback;

    return pack->fallback ? 0 : -1;
}

void ui_fonts_free(UIFontPack *pack)
{

    TTF_Font *unique[16]; int nu = 0;
    TTF_Font *list[] = {pack->bebasTitle, pack->bebasHeader, pack->novaButton,
                         pack->novaLabel, pack->mono,         pack->monoLarge,
                         pack->monoSmall, pack->story,        pack->storySmall,
                         pack->fallback};
    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++) {
        if (!list[i]) continue;
        int dup = 0;
        for (int j = 0; j < nu; j++) if (unique[j] == list[i]) { dup = 1; break; }
        if (!dup) { unique[nu++] = list[i]; TTF_CloseFont(list[i]); }
    }
    memset(pack, 0, sizeof(*pack));
}

SDL_Texture *ui_loadTexture(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) { fprintf(stderr, "[ui] IMG_Load '%s': %s\n", path, IMG_GetError()); return NULL; }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

static SDL_Texture *load_trimmed_alpha(SDL_Renderer *r, const char *path)
{
    SDL_Surface *raw = IMG_Load(path);
    if (!raw) { fprintf(stderr, "[ui] IMG_Load '%s': %s\n", path, IMG_GetError()); return NULL; }

    SDL_Surface *s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(raw);
    if (!s) return NULL;

    int minX = s->w, minY = s->h, maxX = -1, maxY = -1;
    SDL_LockSurface(s);
    Uint32 *pixels = (Uint32*)s->pixels;
    int pitch = s->pitch / 4;
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            Uint8 a = (Uint8)((pixels[y * pitch + x] >> 24) & 0xFF);
            if (a > 16) {
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
            }
        }
    }
    SDL_UnlockSurface(s);

    SDL_Surface *out = s;
    if (maxX >= minX && maxY >= minY) {
        SDL_Rect bb = { minX, minY, maxX - minX + 1, maxY - minY + 1 };
        SDL_Surface *trim = SDL_CreateRGBSurfaceWithFormat(0, bb.w, bb.h, 32, SDL_PIXELFORMAT_RGBA32);
        if (trim) {
            SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
            SDL_BlitSurface(s, &bb, trim, NULL);
            out = trim;
            SDL_FreeSurface(s);
        }
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, out);
    SDL_FreeSurface(out);
    return t;
}

int ui_assets_load(UIAssets *a, SDL_Renderer *r)
{
    memset(a, 0, sizeof(*a));
    a->buttonTex = load_trimmed_alpha(r, "assets/background assets/button.png");
    a->bgMenu    = ui_loadTexture(r, "assets/background assets/background menu.png");
    a->bgOptions = ui_loadTexture(r, "assets/background assets/background options.png");
    a->bgSave    = ui_loadTexture(r, "assets/background assets/save menu background.png");
    a->bgConfig  = ui_loadTexture(r, "assets/background assets/background configuration joueur.png");
    a->logoTex   = ui_loadTexture(r, "snowpiercer logo.png");
    a->titreTex  = ui_loadTexture(r, "titre jeu.png");
    return a->buttonTex ? 0 : -1;
}

void ui_assets_free(UIAssets *a)
{
    if (a->buttonTex) SDL_DestroyTexture(a->buttonTex);
    if (a->bgMenu)    SDL_DestroyTexture(a->bgMenu);
    if (a->bgOptions) SDL_DestroyTexture(a->bgOptions);
    if (a->bgSave)    SDL_DestroyTexture(a->bgSave);
    if (a->bgConfig)  SDL_DestroyTexture(a->bgConfig);
    if (a->logoTex)   SDL_DestroyTexture(a->logoTex);
    if (a->titreTex)  SDL_DestroyTexture(a->titreTex);
    memset(a, 0, sizeof(*a));
}

void ui_text_cache_init(UITextCache *c) { c->nb = 0; memset(c->entries, 0, sizeof(c->entries)); }

void ui_text_cache_clear(UITextCache *c)
{
    for (int i = 0; i < c->nb; i++)
        if (c->entries[i].tex) SDL_DestroyTexture(c->entries[i].tex);
    c->nb = 0;
    memset(c->entries, 0, sizeof(c->entries));
}

static void ui_cache_key(char *out, size_t cap, TTF_Font *font,
                          const char *text, SDL_Color color)
{
    snprintf(out, cap, "%p|%02x%02x%02x%02x|%.96s",
             (void*)font, color.r, color.g, color.b, color.a, text);
}

SDL_Texture *ui_text_get(UITextCache *c, SDL_Renderer *r,
                          TTF_Font *font, const char *text,
                          SDL_Color color, int *outW, int *outH)
{
    if (!font || !text) { if (outW) *outW = 0; if (outH) *outH = 0; return NULL; }

    char key[128];
    ui_cache_key(key, sizeof(key), font, text, color);

    for (int i = 0; i < c->nb; i++) {
        if (strcmp(c->entries[i].key, key) == 0) {
            if (outW) *outW = c->entries[i].w;
            if (outH) *outH = c->entries[i].h;
            return c->entries[i].tex;
        }
    }

    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, color);
    if (!s) { if (outW) *outW = 0; if (outH) *outH = 0; return NULL; }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    int w = s->w, h = s->h;
    SDL_FreeSurface(s);
    if (!t) { if (outW) *outW = 0; if (outH) *outH = 0; return NULL; }

    if (c->nb >= UI_TEXT_CACHE_MAX) {

        if (c->entries[0].tex) SDL_DestroyTexture(c->entries[0].tex);
        memmove(&c->entries[0], &c->entries[1],
                (UI_TEXT_CACHE_MAX - 1) * sizeof(c->entries[0]));
        c->nb = UI_TEXT_CACHE_MAX - 1;
    }
    UICachedText *e = &c->entries[c->nb++];
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    e->tex = t; e->w = w; e->h = h;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return t;
}

void ui_draw_text(UITextCache *c, SDL_Renderer *r,
                   TTF_Font *font, const char *text,
                   SDL_Color color, int x, int y)
{
    int w, h;
    SDL_Texture *t = ui_text_get(c, r, font, text, color, &w, &h);
    if (!t) return;
    SDL_Rect d = {x, y, w, h};
    SDL_RenderCopy(r, t, NULL, &d);
}

void ui_draw_text_centered(UITextCache *c, SDL_Renderer *r,
                            TTF_Font *font, const char *text,
                            SDL_Color color, int cx, int y)
{
    int w, h;
    SDL_Texture *t = ui_text_get(c, r, font, text, color, &w, &h);
    if (!t) return;
    SDL_Rect d = {cx - w/2, y, w, h};
    SDL_RenderCopy(r, t, NULL, &d);
}

void ui_draw_text_in_rect(UITextCache *c, SDL_Renderer *r,
                           TTF_Font *font, const char *text,
                           SDL_Color color, SDL_Rect rect)
{
    int w, h;
    SDL_Texture *t = ui_text_get(c, r, font, text, color, &w, &h);
    if (!t) return;
    SDL_Rect d = {rect.x + (rect.w - w) / 2,
                  rect.y + (rect.h - h) / 2, w, h};
    SDL_RenderCopy(r, t, NULL, &d);
}

void ui_button_state_reset(UIButtonState *s)
{
    s->hoveredId      = -1;
    s->pressedId      = -1;
    s->pressStartTick = 0;
    s->activatedId    = -1;
}

void ui_mouse_logical(SDL_Renderer *r, int wx, int wy, int *lx, int *ly)
{
    /* Manual reverse-transform using the SAME viewport + scale SDL
     * reports via RenderGetViewport / RenderGetScale — which is what
     * the rendering pipeline uses to draw widgets.  We can't trust
     * SDL_RenderWindowToLogical here because on this Linux/X11 VM it
     * uses a different internal viewport (the "ideal" desktop-letter
     * box) than what RenderGetViewport reports (the actual drawable
     * letterbox), so its translated clicks land ~7 logical pixels off
     * from the painted button positions.  Math:
     *     logical = (window - viewport.{x,y}) / scale
     * Falls back to identity if SDL returns zero scale.               */
    SDL_Rect vp = { 0, 0, 0, 0 };
    SDL_RenderGetViewport(r, &vp);
    float sx = 1.0f, sy = 1.0f;
    SDL_RenderGetScale(r, &sx, &sy);

    float fx, fy;
    if (sx > 0.001f && sy > 0.001f) {
        fx = ((float)wx - (float)vp.x) / sx;
        fy = ((float)wy - (float)vp.y) / sy;
    } else {
        fx = (float)wx;
        fy = (float)wy;
    }
    if (lx) *lx = (int)fx;
    if (ly) *ly = (int)fy;
}

static int pointInRect(int x, int y, SDL_Rect r)
{
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

int ui_button_hit(const UIButton *btns, int nb, int lx, int ly)
{
    for (int i = 0; i < nb; i++)
        if (btns[i].enabled && pointInRect(lx, ly, btns[i].rect))
            return btns[i].id;
    return -1;
}

void ui_button_handle_motion(UIButtonState *s, const UIButton *btns, int nb,
                              int lx, int ly)
{
    s->hoveredId = ui_button_hit(btns, nb, lx, ly);
}

void ui_button_handle_click(UIButtonState *s, const UIButton *btns, int nb,
                             int lx, int ly)
{
    int id = ui_button_hit(btns, nb, lx, ly);
    if (id >= 0) {
        s->pressedId      = id;
        s->pressStartTick = SDL_GetTicks();
        s->activatedId    = id;
    } else {
        s->activatedId = -1;
    }
}

static void ui_draw_one(SDL_Renderer *r, SDL_Texture *tex, const UIButton *b,
                        int isHover, int isActive, int isPressed,
                        UITextCache *cache, TTF_Font *font)
{
    SDL_Rect dst = b->rect;
    int hot = (isHover || isActive);

    if (tex) {
        Uint8 mr, mg, mb, ma;
        if (!b->enabled)    { mr = 100; mg =  85; mb =  50; ma = 150; }
        else if (isPressed) { mr = 160; mg = 110; mb =  30; ma = 235; }
        else if (hot)       { mr = 255; mg = 190; mb =  70; ma = 235; }
        else                { mr = 200; mg = 150; mb =  50; ma = 195; }
        SDL_SetTextureColorMod(tex, mr, mg, mb);
        SDL_SetTextureAlphaMod(tex, ma);
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    } else {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_Color fc;
        if (!b->enabled)    fc = (SDL_Color){ 60,  45,  20, 160};
        else if (isPressed) fc = (SDL_Color){160, 110,  30, 235};
        else if (hot)       fc = (SDL_Color){255, 190,  70, 230};
        else                fc = (SDL_Color){200, 150,  50, 195};
        SDL_SetRenderDrawColor(r, fc.r, fc.g, fc.b, fc.a);
        SDL_RenderFillRect(r, &dst);
        SDL_SetRenderDrawColor(r, 180, 130, 40, 255);
        SDL_RenderDrawRect(r, &dst);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }

    if (b->label && font) {
        SDL_Color txt = hot ? (SDL_Color){255, 240, 180, 255}
                            : (SDL_Color){232, 213, 163, 255};
        if (!b->enabled) txt = (SDL_Color){150, 130,  90, 220};
        ui_draw_text_in_rect(cache, r, font, b->label, txt, dst);
    }
}

void ui_button_draw_all(SDL_Renderer *r, SDL_Texture *buttonTex,
                         const UIButton *btns, int nb,
                         const UIButtonState *state, int activeId,
                         UITextCache *cache, TTF_Font *font)
{
    Uint32 now = SDL_GetTicks();
    int pressedStillActive =
        (state->pressedId >= 0 &&
         (int)(now - state->pressStartTick) < UI_PRESS_FLASH_MS);

    for (int i = 0; i < nb; i++) {
        int isHover    = (state->hoveredId == btns[i].id);
        int isActive   = (activeId          == btns[i].id);
        int isPressed  = pressedStillActive && (state->pressedId == btns[i].id);
        ui_draw_one(r, buttonTex, &btns[i], isHover, isActive, isPressed,
                    cache, font);
    }
}

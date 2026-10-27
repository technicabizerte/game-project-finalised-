#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

typedef struct {
    TTF_Font *bebasTitle;
    TTF_Font *bebasHeader;
    TTF_Font *novaButton;
    TTF_Font *novaLabel;
    TTF_Font *mono;
    TTF_Font *monoLarge;
    TTF_Font *monoSmall;
    TTF_Font *story;
    TTF_Font *storySmall;
    TTF_Font *fallback;
} UIFontPack;

int  ui_fonts_load(UIFontPack *pack);
void ui_fonts_free(UIFontPack *pack);

SDL_Texture *ui_loadTexture(SDL_Renderer *r, const char *path);

typedef struct {
    SDL_Texture *buttonTex;
    SDL_Texture *bgMenu;
    SDL_Texture *bgOptions;
    SDL_Texture *bgSave;
    SDL_Texture *bgConfig;
    SDL_Texture *logoTex;
    SDL_Texture *titreTex;
} UIAssets;

int  ui_assets_load(UIAssets *a, SDL_Renderer *r);
void ui_assets_free(UIAssets *a);

#define UI_TEXT_CACHE_MAX 96

typedef struct {
    char          key[128];
    SDL_Texture  *tex;
    int           w, h;
} UICachedText;

typedef struct {
    UICachedText  entries[UI_TEXT_CACHE_MAX];
    int           nb;
} UITextCache;

void         ui_text_cache_init(UITextCache *c);
void         ui_text_cache_clear(UITextCache *c);

SDL_Texture *ui_text_get(UITextCache *c, SDL_Renderer *r,
                         TTF_Font *font, const char *text,
                         SDL_Color color, int *outW, int *outH);
void         ui_draw_text(UITextCache *c, SDL_Renderer *r,
                          TTF_Font *font, const char *text,
                          SDL_Color color, int x, int y);
void         ui_draw_text_centered(UITextCache *c, SDL_Renderer *r,
                                   TTF_Font *font, const char *text,
                                   SDL_Color color, int cx, int y);
void         ui_draw_text_in_rect(UITextCache *c, SDL_Renderer *r,
                                  TTF_Font *font, const char *text,
                                  SDL_Color color, SDL_Rect rect);

#define UI_PRESS_FLASH_MS 100

typedef struct {
    SDL_Rect     rect;
    const char  *label;
    int          id;
    int          enabled;
} UIButton;

typedef struct {
    int          hoveredId;
    int          pressedId;
    Uint32       pressStartTick;
    int          activatedId;
} UIButtonState;

void ui_button_state_reset(UIButtonState *s);

void ui_mouse_logical(SDL_Renderer *r, int wx, int wy, int *lx, int *ly);

int  ui_button_hit(const UIButton *btns, int nb, int lx, int ly);

void ui_button_handle_motion(UIButtonState *s, const UIButton *btns, int nb,
                              int logicalX, int logicalY);

void ui_button_handle_click(UIButtonState *s, const UIButton *btns, int nb,
                             int logicalX, int logicalY);

void ui_button_draw_all(SDL_Renderer *r, SDL_Texture *buttonTex,
                        const UIButton *btns, int nb,
                        const UIButtonState *state, int activeId,
                        UITextCache *cache, TTF_Font *labelFont);

#endif

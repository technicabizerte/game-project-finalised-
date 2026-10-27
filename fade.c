#include "fade.h"
#include "background.h"   /* W_ECRAN / H_ECRAN */

static float s_alpha = 0.0f;   /* 0 = transparent, 255 = opaque black */
static int   s_dir   = 0;      /* +1 = to black, -1 = from black, 0 = idle */
static float s_speed = 0.0f;   /* alpha units per second */

void fade_reset(void)
{
    s_alpha = 0.0f;
    s_dir   = 0;
    s_speed = 0.0f;
}

void fade_to_black(float duration)
{
    if (duration <= 0.0f) { s_alpha = 255.0f; s_dir = 0; return; }
    s_dir   = 1;
    s_speed = 255.0f / duration;
}

void fade_from_black(float duration)
{
    s_alpha = 255.0f;
    if (duration <= 0.0f) { s_alpha = 0.0f; s_dir = 0; return; }
    s_dir   = -1;
    s_speed = 255.0f / duration;
}

void fade_update(float dt)
{
    if (s_dir == 1) {
        s_alpha += s_speed * dt;
        if (s_alpha >= 255.0f) { s_alpha = 255.0f; s_dir = 0; }
    } else if (s_dir == -1) {
        s_alpha -= s_speed * dt;
        if (s_alpha <= 0.0f) { s_alpha = 0.0f; s_dir = 0; }
    }
}

void fade_render(SDL_Renderer *r)
{
    if (s_alpha <= 0.5f) return;
    Uint8 a = (Uint8)(s_alpha > 255.0f ? 255.0f : s_alpha);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, a);
    SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

float fade_alpha(void)   { return s_alpha; }
int   fade_is_active(void) { return s_dir != 0 || s_alpha > 0.5f; }

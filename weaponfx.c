#include "weaponfx.h"
#include <math.h>
#include <string.h>

#define SWING_FPS 28.0f

void weapon_swing_init(WeaponSwing *w)
{
    memset(w, 0, sizeof(*w));
    w->length = 46.0f;
}

void weapon_swing_start(WeaponSwing *w, float pivotX, float pivotY, int direction)
{
    w->active     = 1;
    w->direction  = direction < 0 ? -1 : 1;
    w->frame      = 0;
    w->frameTimer = 0.0f;
    w->pivotX     = pivotX;
    w->pivotY     = pivotY;
    w->trailCount = 0;
    for (int i = 0; i < WEAPON_TRAIL_FRAMES; i++) w->trailAngle[i] = 0.0f;
}

void weapon_swing_update(WeaponSwing *w, float dt)
{
    if (!w->active) return;
    w->frameTimer += dt;
    float frameDur = 1.0f / SWING_FPS;
    while (w->frameTimer >= frameDur) {
        w->frameTimer -= frameDur;

        for (int i = WEAPON_TRAIL_FRAMES - 1; i > 0; i--)
            w->trailAngle[i] = w->trailAngle[i - 1];

        float tstep = (float)w->frame / (float)(WEAPON_SWING_FRAMES - 1);
        w->trailAngle[0] = tstep;
        if (w->trailCount < WEAPON_TRAIL_FRAMES) w->trailCount++;

        w->frame++;
        if (w->frame >= WEAPON_SWING_FRAMES) {
            w->active     = 0;
            w->frame      = 0;
            w->frameTimer = 0.0f;
            return;
        }
    }
}

static float swing_angle_for_t(float t, int direction)
{
    float a = -60.0f + t * 120.0f;
    return (direction < 0) ? -a : a;
}

void weapon_swing_render(const WeaponSwing *w, SDL_Renderer *r, int ox)
{
    if (!w->active) return;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    const Uint8 alphas[WEAPON_TRAIL_FRAMES] = {180, 120, 60};
    for (int i = 0; i < w->trailCount; i++) {
        float t    = w->trailAngle[i];
        float deg  = swing_angle_for_t(t, w->direction);
        float rad  = deg * (float)M_PI / 180.0f;

        float bx = w->pivotX + (float)ox;
        float by = w->pivotY;

        float tx = bx + cosf(rad) * w->length;
        float ty = by + sinf(rad) * w->length * 0.6f;

        SDL_SetRenderDrawColor(r, 255, 220, 140, alphas[i]);
        SDL_RenderDrawLine(r, (int)bx, (int)by, (int)tx, (int)ty);

        SDL_RenderDrawLine(r, (int)bx, (int)(by + 1), (int)tx, (int)(ty + 1));
        SDL_RenderDrawLine(r, (int)bx, (int)(by - 1), (int)tx, (int)(ty - 1));
    }

    float tnow = (float)w->frame / (float)(WEAPON_SWING_FRAMES - 1);
    float deg  = swing_angle_for_t(tnow, w->direction);
    float rad  = deg * (float)M_PI / 180.0f;
    float bx   = w->pivotX + (float)ox;
    float by   = w->pivotY;
    float tx   = bx + cosf(rad) * w->length;
    float ty   = by + sinf(rad) * w->length * 0.6f;

    SDL_SetRenderDrawColor(r, 255, 235, 180, 235);
    for (int dx = -2; dx <= 2; dx++) {
        SDL_RenderDrawLine(r, (int)bx + dx, (int)by, (int)tx + dx, (int)ty);
    }

    SDL_SetRenderDrawColor(r, 255, 255, 200, 255);
    SDL_Rect spark = {(int)tx - 2, (int)ty - 2, 5, 5};
    SDL_RenderFillRect(r, &spark);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void particles_init(ParticleField *f)
{
    memset(f, 0, sizeof(*f));
}

static unsigned int g_particle_seed = 0x13579bdfu;

static unsigned int prng_next(void)
{
    g_particle_seed = g_particle_seed * 1664525u + 1013904223u;
    return g_particle_seed >> 16;
}

void particles_burst(ParticleField *f, float x, float y)
{
    g_particle_seed ^= (unsigned int)SDL_GetTicks();
    int count = 8 + (int)(prng_next() % 5);
    Uint32 now = SDL_GetTicks();

    for (int n = 0; n < count; n++) {
        Particle *p = NULL;
        for (int i = 0; i < PARTICLES_MAX; i++) {
            if (!f->pool[i].actif) { p = &f->pool[i]; if (i + 1 > f->nb) f->nb = i + 1; break; }
        }
        if (!p) return;

        float ang = (float)(prng_next() % 360) * (float)M_PI / 180.0f;
        float spd = 2.0f + (float)(prng_next() % 30) / 10.0f;
        p->x  = x;
        p->y  = y;
        p->vx = cosf(ang) * spd;
        p->vy = sinf(ang) * spd - 1.0f;
        p->spawnTick = now;

        if (prng_next() & 1) {
            p->r = 255; p->g = 190; p->b = 90;
        } else {
            p->r = 235; p->g = 80;  p->b = 60;
        }
        p->size  = 2 + (int)(prng_next() % 3);
        p->actif = 1;
    }
}

void particles_update(ParticleField *f, float dt)
{
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < f->nb; i++) {
        Particle *p = &f->pool[i];
        if (!p->actif) continue;
        if ((int)(now - p->spawnTick) >= BURST_PARTICLE_TTL_MS) {
            p->actif = 0; continue;
        }
        p->x  += p->vx;
        p->y  += p->vy;
        p->vy += 0.18f;
        p->vx *= 0.98f;
        (void)dt;
    }

    while (f->nb > 0 && !f->pool[f->nb - 1].actif) f->nb--;
}

void particles_render(const ParticleField *f, SDL_Renderer *r, int ox)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < f->nb; i++) {
        const Particle *p = &f->pool[i];
        if (!p->actif) continue;
        int age = (int)(now - p->spawnTick);
        int remaining = BURST_PARTICLE_TTL_MS - age;
        if (remaining < 0) continue;
        Uint8 a = (Uint8)(255 * remaining / BURST_PARTICLE_TTL_MS);
        SDL_SetRenderDrawColor(r, p->r, p->g, p->b, a);
        SDL_Rect d = {(int)p->x + ox - p->size/2,
                      (int)p->y - p->size/2,
                      p->size, p->size};
        SDL_RenderFillRect(r, &d);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

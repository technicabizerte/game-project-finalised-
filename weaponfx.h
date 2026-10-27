#ifndef WEAPONFX_H
#define WEAPONFX_H

#include <SDL2/SDL.h>

#define WEAPON_SWING_FRAMES 6
#define WEAPON_TRAIL_FRAMES 3

typedef struct {
    int     active;
    int     direction;
    int     frame;
    float   frameTimer;
    float   pivotX, pivotY;
    float   length;

    float   trailAngle[WEAPON_TRAIL_FRAMES];
    int     trailCount;
} WeaponSwing;

void weapon_swing_init(WeaponSwing *w);
void weapon_swing_start(WeaponSwing *w, float pivotX, float pivotY,
                        int direction);
void weapon_swing_update(WeaponSwing *w, float dt);

void weapon_swing_render(const WeaponSwing *w, SDL_Renderer *r, int ox);

#define PARTICLES_MAX       96
#define BURST_PARTICLE_TTL_MS 300

typedef struct {
    float   x, y;
    float   vx, vy;
    Uint32  spawnTick;
    Uint8   r, g, b;
    int     size;
    int     actif;
} Particle;

typedef struct {
    Particle   pool[PARTICLES_MAX];
    int        nb;
} ParticleField;

void particles_init(ParticleField *f);

void particles_burst(ParticleField *f, float x, float y);
void particles_update(ParticleField *f, float dt);
void particles_render(const ParticleField *f, SDL_Renderer *r, int ox);

#endif

#ifndef COMBAT_H
#define COMBAT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "weaponfx.h"

#define PLAYER_COLS   6
#define PLAYER_ROWS   7
#define GUARD_COLS    6
#define GUARD_ROWS    3
#define WORKER_COLS   6
#define WORKER_ROWS   3
#define ENFORCER_COLS 6
#define ENFORCER_ROWS 3
#define HITFX_COLS    4
#define HITFX_ROWS    1

/* Character sprite + hitbox sizes — all scaled by 1.15x vs. baseline.
 * Baselines (kept here for easy tweaking if 1.15 needs adjustment):
 *   PLAYER_DRAW   48 x 64    GUARD_DRAW    48 x 64    GUARD_HB    36 x 60
 *   WORKER_DRAW   40 x 56    WORKER_HB     30 x 52
 *   ENFORCER_DRAW 64 x 80    ENFORCER_HB   52 x 72
 * Renderers anchor sprite bottom-center to the hitbox bottom-center
 * (see joueur_afficher_interieur_idx and ennemis_afficher), so growing
 * both draw + hitbox by the same factor keeps the character's feet on
 * the floor automatically. */
/* Bumped to match the heaviest non-boss enemy (ENFORCER_DRAW 110 x 148)
 * so the player reads at the same silhouette as the biggest grunts.
 * Hitbox JOUEUR_W/H in background.h stays at 38 x 63 for forgiving
 * collisions; the larger draw rect is purely visual via the foot-
 * anchored char_anim_render path.                                      */
#define PLAYER_DRAW_W   110
#define PLAYER_DRAW_H   148

/* Enemy DRAW sizes bumped to roughly match PLAYER_DRAW_W/H (94 x 125)
 * so enemies read at the same silhouette as Ellie/Curtis on screen.
 * Hitboxes (HBW/HBH) are intentionally kept smaller — same pattern as
 * the player: forgiving collisions under a larger visible sprite.
 * Sprites are foot-anchored in ennemis_afficher so the new height
 * extends UPWARD from the floor.                                        */
#define GUARD_DRAW_W     94   /* matches player */
#define GUARD_DRAW_H    125   /* matches player */
#define GUARD_HBW        41
#define GUARD_HBH        69

#define WORKER_DRAW_W    88
#define WORKER_DRAW_H   118
#define WORKER_HBW       35
#define WORKER_HBH       60

#define ENFORCER_DRAW_W 110   /* a bit larger — heavy enemy */
#define ENFORCER_DRAW_H 148
#define ENFORCER_HBW     60
#define ENFORCER_HBH     83

/* Final boss reuses ET_ENFORCER but renders at BOSS_DISPLAY_W/H (220x220
 * — see boss_anim.h).  Its hitbox is intentionally larger than a normal
 * enforcer so attacks/contact register against the much bigger sprite. */
#define BOSS_HBW         100
#define BOSS_HBH         150

#define HITFX_DRAW_W     48
#define HITFX_DRAW_H     48

#define BULLET_DRAW_W    16
#define BULLET_DRAW_H     6
#define MUZZLE_DRAW_W    32
#define MUZZLE_DRAW_H    24
#define BULLET_SPEED     9.0f

#define ANIM_P_IDLE   0
#define ANIM_P_WALK   1
#define ANIM_P_JUMP   2
#define ANIM_P_FALL   3
#define ANIM_P_SHOOT  4
#define ANIM_P_MELEE  5
#define ANIM_P_HIT    6

static const int   PLAYER_NFRAMES[7]  = {4,    6,    2,   2,    3,    4,    4   };
static const float PLAYER_ANIM_FPS[7] = {4.0f, 10.0f,8.0f,8.0f,12.0f,12.0f,6.0f};
static const int   PLAYER_ANIM_LOOP[7]= {1,    1,    0,   0,    0,    0,    0   };

#define ANIM_E_WALK   0
#define ANIM_E_ATTACK 1
#define ANIM_E_DEATH  2

static const int GUARD_NFRAMES[3]    = {6, 4, 4};
static const int WORKER_NFRAMES[3]   = {6, 4, 4};
static const int ENFORCER_NFRAMES[3] = {6, 6, 4};

static const float ENEMY_ANIM_FPS[3] = {10.0f, 10.0f, 6.0f};

typedef enum {
    PS_IDLE = 0, PS_WALK, PS_JUMP, PS_FALL,
    PS_SHOOT, PS_MELEE, PS_HIT, PS_DEAD
} EtatJoueur;

typedef enum { ET_GUARD = 0, ET_WORKER, ET_ENFORCER } TypeEnnemi;
typedef enum { ES_WALK = 0,  ES_ATTACK, ES_DEAD      } EtatEnnemi;

typedef struct {
    SDL_Texture *texture;
    int          frame_w;
    int          frame_h;
    int          current_row;
    int          current_frame;
    int          frame_count;
    float        frame_timer;
    float        fps;
    int          loop;
    int          finished;
} AnimationSystem;

void     animation_init(AnimationSystem *anim, SDL_Texture *tex,
                        int num_cols, int num_rows,
                        int row, int frame_count, float fps, int loop);
void     animation_update(AnimationSystem *anim, float dt);
SDL_Rect animation_get_src_rect(const AnimationSystem *anim);
void     animation_set_row(AnimationSystem *anim,
                            int row, int frame_count, float fps, int loop);

#define GUARD_HP       60
#define WORKER_HP      50
#define ENFORCER_HP   120
#define SHOOT_DAMAGE   20
#define MELEE_DAMAGE   35
#define GUARD_DMG      15
#define WORKER_DMG     10
#define ENFORCER_DMG   25

#define MAX_BALLES 24

typedef struct {
    float x, y, velX;
    int   actif;
    int   wagon;
} Balle;

#define MAX_HIT_FX 8

typedef struct {
    float         x, y;
    int           actif;
    AnimationSystem anim;
} HitEffect;

#define MAX_ENNEMIS 12

typedef struct {
    float         x, y, velY;
    int           auSol, actif, hp;
    TypeEnnemi    type;
    EtatEnnemi    etat;
    int           direction;
    int           wagon;
    AnimationSystem anim;
    Uint32        attackCooldownMs;
    Uint32        lastAttackTick;
    int           hitFlashMs;
} Ennemi;

/* Boss / hitbox helpers — centralise the "is this the final boss" check
 * and the per-type hitbox lookup so collision sites stay consistent. */
int ennemi_is_boss(const Ennemi *e);
int ennemi_hbw    (const Ennemi *e);
int ennemi_hbh    (const Ennemi *e);

SDL_Texture *combat_loadTexture(SDL_Renderer *r, const char *path);

void balles_update(Balle *balles, int *nb,
                   Ennemi *ennemis, int *nbEnnemis,
                   int *joueurHp, EtatJoueur *joueurEtat, AnimationSystem *joueurAnim,
                   float joueurX, float joueurY, int joueurW, int joueurH);

void balles_update_fx(Balle *balles, int *nb,
                      Ennemi *ennemis, int *nbEnnemis,
                      ParticleField *fx, HitEffect *hfx, int *nbHfx,
                      SDL_Texture *hitFxSheet);
void balles_afficher(Balle *balles, int nb, SDL_Renderer *r, SDL_Texture *bulletTex, int ox);

void hitfx_spawn(HitEffect *fx, int *nbFx, float x, float y, SDL_Texture *sheet);
void hitfx_update(HitEffect *fx, int *nb, float dt);
void hitfx_afficher(HitEffect *fx, int nb, SDL_Renderer *r, int ox);

void ennemis_spawn(Ennemi *ennemis, int *nb, TypeEnnemi type,
                   float x, float y, int wagon, SDL_Texture *sheet);

void ennemis_afficher(Ennemi *ennemis, int nb, int wagonActif, SDL_Renderer *r, int ox);

#endif

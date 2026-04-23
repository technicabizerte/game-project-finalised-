#ifndef COMBAT_H
#define COMBAT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

/* ══════════════════════════════════════
   SPRITESHEET LAYOUT  (cols × rows)
   ══════════════════════════════════════ */
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

/* ══════════════════════════════════════
   DRAW (display) SIZES  — pixels on screen
   ══════════════════════════════════════ */
#define PLAYER_DRAW_W    48
#define PLAYER_DRAW_H    64

#define GUARD_DRAW_W     48
#define GUARD_DRAW_H     64
#define GUARD_HBW        36
#define GUARD_HBH        60

#define WORKER_DRAW_W    40
#define WORKER_DRAW_H    56
#define WORKER_HBW       30
#define WORKER_HBH       52

#define ENFORCER_DRAW_W  64
#define ENFORCER_DRAW_H  80
#define ENFORCER_HBW     52
#define ENFORCER_HBH     72

#define HITFX_DRAW_W     48
#define HITFX_DRAW_H     48

#define BULLET_DRAW_W    16
#define BULLET_DRAW_H     6
#define MUZZLE_DRAW_W    32
#define MUZZLE_DRAW_H    24
#define BULLET_SPEED     9.0f

/* ══════════════════════════════════════
   PLAYER ANIMATION ROWS
   ══════════════════════════════════════ */
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

/* ══════════════════════════════════════
   ENEMY ANIMATION ROWS
   ══════════════════════════════════════ */
#define ANIM_E_WALK   0
#define ANIM_E_ATTACK 1
#define ANIM_E_DEATH  2

static const int GUARD_NFRAMES[3]    = {6, 4, 4};
static const int WORKER_NFRAMES[3]   = {6, 4, 4};
static const int ENFORCER_NFRAMES[3] = {6, 6, 4};

static const float ENEMY_ANIM_FPS[3] = {10.0f, 10.0f, 6.0f};

/* ══════════════════════════════════════
   STATES
   ══════════════════════════════════════ */
typedef enum {
    PS_IDLE = 0, PS_WALK, PS_JUMP, PS_FALL,
    PS_SHOOT, PS_MELEE, PS_HIT, PS_DEAD
} EtatJoueur;

typedef enum { ET_GUARD = 0, ET_WORKER, ET_ENFORCER } TypeEnnemi;
typedef enum { ES_WALK = 0,  ES_ATTACK, ES_DEAD      } EtatEnnemi;

/* ══════════════════════════════════════
   ANIMATION SYSTEM
   ══════════════════════════════════════ */
typedef struct {
    SDL_Texture *texture;       /* spritesheet this animation belongs to */
    int          frame_w;       /* source frame width  (pixels in sheet) */
    int          frame_h;       /* source frame height (pixels in sheet) */
    int          current_row;
    int          current_frame;
    int          frame_count;   /* active frames in this row */
    float        frame_timer;   /* accumulated seconds since last advance */
    float        fps;
    int          loop;          /* 1 = loop, 0 = play once */
    int          finished;      /* 1 after play-once completes */
} AnimationSystem;

/* ── functions ── */
void     animation_init(AnimationSystem *anim, SDL_Texture *tex,
                        int num_cols, int num_rows,
                        int row, int frame_count, float fps, int loop);
void     animation_update(AnimationSystem *anim, float dt);
SDL_Rect animation_get_src_rect(const AnimationSystem *anim);
void     animation_set_row(AnimationSystem *anim,
                            int row, int frame_count, float fps, int loop);

/* ══════════════════════════════════════
   DAMAGE VALUES
   ══════════════════════════════════════ */
#define GUARD_HP       50
#define WORKER_HP      30
#define ENFORCER_HP   150
#define SHOOT_DAMAGE   20
#define MELEE_DAMAGE   35
#define GUARD_DMG      15
#define WORKER_DMG     10
#define ENFORCER_DMG   25

/* ══════════════════════════════════════
   BULLET
   ══════════════════════════════════════ */
#define MAX_BALLES 24

typedef struct {
    float x, y, velX;
    int   actif;
} Balle;

/* ══════════════════════════════════════
   HIT EFFECT
   ══════════════════════════════════════ */
#define MAX_HIT_FX 8

typedef struct {
    float         x, y;
    int           actif;
    AnimationSystem anim;
} HitEffect;

/* ══════════════════════════════════════
   ENEMY
   ══════════════════════════════════════ */
#define MAX_ENNEMIS 12

typedef struct {
    float         x, y, velY;
    int           auSol, actif, hp;
    TypeEnnemi    type;
    EtatEnnemi    etat;
    int           direction;    /* -1 = faces left */
    int           wagon;
    AnimationSystem anim;
    Uint32        attackCooldownMs;
    Uint32        lastAttackTick;
    int           hitFlashMs;
} Ennemi;

/* ══════════════════════════════════════
   PROTOTYPES
   ══════════════════════════════════════ */
SDL_Texture *combat_loadTexture(SDL_Renderer *r, const char *path);

void balles_update(Balle *balles, int *nb,
                   Ennemi *ennemis, int *nbEnnemis,
                   int *joueurHp, EtatJoueur *joueurEtat, AnimationSystem *joueurAnim,
                   float joueurX, float joueurY, int joueurW, int joueurH);
void balles_afficher(Balle *balles, int nb, SDL_Renderer *r, SDL_Texture *bulletTex, int ox);

void hitfx_spawn(HitEffect *fx, int *nbFx, float x, float y, SDL_Texture *sheet);
void hitfx_update(HitEffect *fx, int *nb, float dt);
void hitfx_afficher(HitEffect *fx, int nb, SDL_Renderer *r, int ox);

void ennemis_spawn(Ennemi *ennemis, int *nb, TypeEnnemi type,
                   float x, float y, int wagon, SDL_Texture *sheet);
/* ennemis_update declared in background.h (needs Joueur type) */
void ennemis_afficher(Ennemi *ennemis, int nb, int wagonActif, SDL_Renderer *r, int ox);

#endif

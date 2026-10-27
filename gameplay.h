#ifndef GAMEPLAY_H
#define GAMEPLAY_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "combat.h"
#include "background.h"
#include "ui.h"
#include "audio.h"

#define AMMO_START                12
#define AMMO_PICKUP_AMOUNT        6
#define AMMO_PICKUPS_MAX          3
#define HEALTH_PICKUPS_MAX        2
#define HEALTH_PICKUP_PERCENT     25
#define KRONOLE_MIN_PER_LEVEL     3
#define KRONOLE_MAX_PER_LEVEL     5
#define KRONOLE_DOOR_COST         3

#define ENEMY_HP_BASE_MULT       1.00f
#define ENEMY_DMG_MULT           1.25f
#define ENEMY_SPD_MULT           1.20f
#define ENEMY_DETECT_RANGE       300
#define ENEMY_ATTACK_COOLDOWN_MS 800u
#define PLAYER_MELEE_MULT        0.80f
#define PLAYER_INVULN_MS         500u
#define ENEMY_CONTACT_DMG        5

#define MAX_HAZARDS_PER_WAGON    4
#define HAZARD_BASE_DMG          12
#define HAZARD_HIT_COOLDOWN_MS   600u

#define SHAKE_HIT_AMP            6.0f
#define SHAKE_HAZARD_AMP         9.0f
#define SHAKE_DECAY_PER_SEC      24.0f

#define RANGED_DETECT_RANGE      400
#define RANGED_FIRE_COOLDOWN_MS  2500u
#define RANGED_PROJECTILE_SPEED  6.0f
#define RANGED_PROJECTILE_DMG_PCT 15

#define DASH_DURATION_MS         200u
#define DASH_COOLDOWN_MS         1500u
#define DASH_SPEED_MULT          3.0f
#define DASH_ENEMY_DMG_PCT       10
#define DASH_TRAIL_COUNT         3

#define WIRE_TIMER_LV1_MS        30000u
#define WIRE_TIMER_LV2_MS        25000u
#define WIRE_TIMER_LV3_MS        20000u
#define WIRE_MAX_ATTEMPTS        3
#define WIRE_MAX_WIRES           5

#define DOOR_UNLOCK_DURATION_MS  8000u

#define HITBOX_W_MULT_PCT        120
#define HITBOX_H_MULT_PCT        115

#define LIVES_MAX_PER_PLAYER      3
#define LIFE_BREAK_FRAMES         6
#define LIFE_BREAK_ANIM_MS        600u
#define LIFE_INVULN_MS            1500u

typedef struct {
    int   actif;
    float x, y;
    int   wagon;
    int   collected;
} ItemPickup;

typedef struct {
    int    actif;
    float  x, y;            /* top-left in world coords                       */
    float  velX;
    int    wagon;
    int    sourceEnemyIdx;
    int    w, h;            /* render + collision size                        */
    int    is_fireball;     /* boss variant: bigger sprite, animated sheet    */
    Uint32 spawnTick;       /* SDL_GetTicks at spawn — drives fireball anim   */
} EnemyProjectile;

/* Fireball variant fired by the final boss in place of the regular bullet.
 * The sheet is a 5x5 grid of square frames (25 frames total, looping). */
#define BOSS_FIREBALL_W          48
#define BOSS_FIREBALL_H          48
#define BOSS_FIREBALL_COLS        5
#define BOSS_FIREBALL_ROWS        5
#define BOSS_FIREBALL_FRAME_COUNT (BOSS_FIREBALL_COLS * BOSS_FIREBALL_ROWS)
#define BOSS_FIREBALL_FPS        20
/* Push the fireball spawn point down from the hitbox center so it leaves
 * around the boss's hand/waist instead of mid-chest.  Positive = lower. */
#define BOSS_FIREBALL_SPAWN_Y_OFFSET 30

typedef struct {
    int    active;
    Uint32 startTick;
    int    direction;
} DashState;

typedef struct {
    float x, y;
    int   alpha;
    int   direction;
    int   playerIdx;
} DashTrail;

#define MAX_ENEMY_PROJECTILES 12
#define MAX_DASH_TRAILS       16
#define MAX_PICKUPS_PER_KIND  8
#define MAX_KRONOLE_PER_LVL   30
#define MAX_SPARKS            32

typedef enum {
    DOOR_IDLE = 0,
    DOOR_UNLOCKING,
    DOOR_OPEN,
    DOOR_LOCKED_NOT_ENOUGH
} DoorState;

typedef struct {
    int    actif;
    Uint32 startTick;
    Uint32 pausedFor;
    int    paused;
    DoorState state;
    int    targetWagon;
    int    enemiesSpawned;
    int    nbEnemiesAtUnlockStart;
    char   message[128];
    Uint32 messageStartTick;
} LockedDoor;

typedef struct {
    int    actif;
    int    revivable;
    int    revivedThisLevel;
    float  deathX, deathY;
    int    deathWagon;
    int    deathDirection;
} RevivalSlot;

typedef struct {
    int    activeSolo;
    float  x, y;
    int    direction;
    int    hp;
    int    maxHp;
    int    wagon;
    int    working;
    int    moving;             /* 1 while chasing the player, 0 when idle */
    Uint32 farewellTick;       /* SDL_GetTicks() at wagon 4→5 door open;
                                  drives the "you go without me" bubble  */
    AnimationSystem walkAnim;
    AnimationSystem workAnim;
} NamgoongNPC;

typedef struct {
    float x, y;
    float vx, vy;
    int   alpha;
    int   life;
    int   maxLife;
} Spark;

typedef struct {
    int    actif;
    int    wagon;
    SDL_Rect rect;
    int    damage;
    Uint32 lastHitTick[NB_JOUEURS_MAX];
    int    spriteRows;
    int    spriteCols;
    int    framesAnim;
    float  animFps;
    float  animTimer;
    int    animFrame;
} Hazard;

typedef struct GameplayState {
    int   ammo[NB_JOUEURS_MAX];

    DashState  dash[NB_JOUEURS_MAX];
    Uint32     dashLastEndTick[NB_JOUEURS_MAX];
    DashTrail  trails[MAX_DASH_TRAILS];
    int        nbTrails;

    Uint32     invulnUntil[NB_JOUEURS_MAX];
    int        debugHitbox;

    ItemPickup ammoPickups[MAX_PICKUPS_PER_KIND];
    int        nbAmmoPickups;
    ItemPickup healthPickups[MAX_PICKUPS_PER_KIND];
    int        nbHealthPickups;
    ItemPickup kronolePickups[MAX_KRONOLE_PER_LVL];
    int        nbKronolePickups;

    int        kronoleCount;

    EnemyProjectile projectiles[MAX_ENEMY_PROJECTILES];
    int             nbProjectiles;
    int        rangedEnemyIndices[MAX_ENNEMIS];
    int        nbRangedEnemies;
    Uint32     rangedLastFireTick[MAX_ENNEMIS];

    NamgoongNPC namgoong;
    LockedDoor  door;
    RevivalSlot revival[NB_JOUEURS_MAX];
    int         permanentlyDead[NB_JOUEURS_MAX];

    int        levelLoaded;
    int        wagonInitialized[5];
    int        enemiesKilled;
    int        totalInterludeScore;

    int        gameMode;
    int        gameEndedFlag;

    SDL_Texture *tex_iconAmmo;
    SDL_Texture *tex_iconDash;
    SDL_Texture *tex_pickupAmmo;
    SDL_Texture *tex_pickupHealth;
    SDL_Texture *tex_pickupKronole;
    SDL_Texture *tex_enemyRanged;
    SDL_Texture *tex_enemyProjectile;
    SDL_Texture *tex_bossFireball;
    SDL_Texture *tex_doorLocked;
    SDL_Texture *tex_playerDowned;
    SDL_Texture *tex_namgoong;       /* "working at a panel" anim (4-frame strip) */
    SDL_Texture *tex_namgoong_run;   /* run sheet used while chasing the player    */
    SDL_Texture *tex_namgoong_idle;  /* idle sheet shown when he stops             */
    SDL_Texture *tex_wirePanel;
    SDL_Texture *tex_spark;

    SDL_Texture *tex_heartFull;
    SDL_Texture *tex_heartEmpty;
    SDL_Texture *tex_heartBreakSheet;

    int    lives[NB_JOUEURS_MAX];
    int    lifeBreakIdx[NB_JOUEURS_MAX];
    Uint32 lifeBreakStartTick[NB_JOUEURS_MAX];

    SDL_Texture *tex_hazardWagon[5];
    Hazard       hazards[MAX_HAZARDS_PER_WAGON * 5];
    int          nbHazards;

    float        shakeAmp;
    float        shakeTimeLeft;
    int          shakeOffsetX;
    int          shakeOffsetY;

    Uint32      saveToastUntil;
} GameplayState;

void gameplay_trigger_shake(GameplayState *gp, float amp, float seconds);
void gameplay_update_shake(GameplayState *gp, float dt);

void gameplay_init(GameplayState *gp);
void gameplay_load_assets(GameplayState *gp, SDL_Renderer *r);
void gameplay_free_assets(GameplayState *gp);

void gameplay_reset_for_run(GameplayState *gp, int mode);

void gameplay_init_wagon(GameplayState *gp, Background *bg, int wagon);

void gameplay_update(GameplayState *gp, Background *bg,
                     AudioSystem *audio, float dt);

void gameplay_handle_dash_input(GameplayState *gp, Background *bg,
                                AudioSystem *audio, int idx);

int  gameplay_handle_player_shoot(GameplayState *gp, int idx, AudioSystem *audio);

void gameplay_handle_revive_request(GameplayState *gp, Background *bg,
                                    AudioSystem *audio, int reviverIdx);

void gameplay_handle_door_interact(GameplayState *gp, Background *bg,
                                   AudioSystem *audio);

void gameplay_render_world(GameplayState *gp, SDL_Renderer *r,
                           Background *bg, int ox);

void gameplay_render_hud(GameplayState *gp, SDL_Renderer *r,
                         Background *bg,
                         UIFontPack *fonts,
                         UITextCache *cache,
                         UIAssets *assets,
                         int scoreJoueur);

int  gameplay_check_advance(GameplayState *gp, Background *bg);

int  gameplay_anyone_alive(Background *bg, GameplayState *gp);

#endif

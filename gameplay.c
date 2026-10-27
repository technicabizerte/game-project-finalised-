#include "gameplay.h"
#include "background.h"
#include "effects.h"
#include "ui.h"
#include "audio.h"
#include "damage_numbers.h"
#include "dialogue_cinematic.h"
#include "boss_anim.h"
#include "boss_ai.h"
#include "boss_hud.h"
#include "two_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static SDL_Texture *gp_load(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) { fprintf(stderr, "[gp] '%s': %s\n", path, IMG_GetError()); return NULL; }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

static int g_prevWagon = -1;

void gameplay_init(GameplayState *gp)
{
    memset(gp, 0, sizeof(*gp));
    gp->gameMode = MODE_MONO;
}

void gameplay_load_assets(GameplayState *gp, SDL_Renderer *r)
{
    gp->tex_iconAmmo        = gp_load(r, "assets/background assets/icon_ammo.png");
    gp->tex_iconDash        = gp_load(r, "assets/background assets/icon_dash.png");
    gp->tex_pickupAmmo      = gp_load(r, "assets/spritesheets/pickup_ammo.png");
    gp->tex_pickupHealth    = gp_load(r, "assets/spritesheets/pickup_health.png");
    gp->tex_pickupKronole   = gp_load(r, "assets/spritesheets/pickup_kronole.png");
    gp->tex_enemyRanged     = gp_load(r, "assets/spritesheets/enemy_ranged.png");
    gp->tex_enemyProjectile = gp_load(r, "assets/spritesheets/enemy_projectile.png");
    gp->tex_bossFireball    = gp_load(r, "assets/spritesheets/boss_fireball.png");
    gp->tex_doorLocked      = gp_load(r, "assets/spritesheets/door_locked.png");
    gp->tex_playerDowned    = gp_load(r, "assets/spritesheets/player_downed.png");
    gp->tex_namgoong        = gp_load(r, "assets/spritesheets/namgoong_working.png");
    /* Walking namgoong: reuse his player-skin sheets (5x5 grid).          */
    gp->tex_namgoong_run    = gp_load(r, "assets/sprites/characters/namgoong/sword/run.png");
    gp->tex_namgoong_idle   = gp_load(r, "assets/sprites/characters/namgoong/sword/idle.png");
    gp->tex_wirePanel       = gp_load(r, "assets/background assets/wire_panel.png");
    gp->tex_spark           = gp_load(r, "assets/spritesheets/spark.png");

    gp->tex_hazardWagon[0]  = gp_load(r, "assets/hazards/hazard_poor.png");
    gp->tex_hazardWagon[1]  = gp_load(r, "assets/hazards/hazard_industry.png");
    gp->tex_hazardWagon[2]  = gp_load(r, "assets/hazards/hazard_aquarium.png");
    gp->tex_hazardWagon[3]  = gp_load(r, "assets/hazards/hazard_garden.png");
    gp->tex_hazardWagon[4]  = gp_load(r, "assets/hazards/hazard_luxury.png");

    gp->tex_heartFull        = gp_load(r, "assets/heart_full.png");
    gp->tex_heartEmpty       = gp_load(r, "assets/heart_empty.png");
    gp->tex_heartBreakSheet  = gp_load(r, "assets/heart_break_sheet.png");
}

void gameplay_free_assets(GameplayState *gp)
{
    SDL_Texture *list[] = {
        gp->tex_iconAmmo, gp->tex_iconDash, gp->tex_pickupAmmo, gp->tex_pickupHealth,
        gp->tex_pickupKronole, gp->tex_enemyRanged, gp->tex_enemyProjectile,
        gp->tex_bossFireball,
        gp->tex_doorLocked, gp->tex_playerDowned, gp->tex_namgoong,
        gp->tex_namgoong_run, gp->tex_namgoong_idle,
        gp->tex_wirePanel, gp->tex_spark,
        gp->tex_heartFull, gp->tex_heartEmpty, gp->tex_heartBreakSheet
    };
    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++)
        if (list[i]) SDL_DestroyTexture(list[i]);
    gp->tex_iconAmmo = gp->tex_iconDash = gp->tex_pickupAmmo = gp->tex_pickupHealth = NULL;
    gp->tex_pickupKronole = gp->tex_enemyRanged = gp->tex_enemyProjectile = NULL;
    gp->tex_bossFireball = NULL;
    gp->tex_doorLocked = gp->tex_playerDowned = gp->tex_namgoong = NULL;
    gp->tex_namgoong_run = gp->tex_namgoong_idle = NULL;
    gp->tex_wirePanel = gp->tex_spark = NULL;
    gp->tex_heartFull = gp->tex_heartEmpty = gp->tex_heartBreakSheet = NULL;
    for (int i = 0; i < 5; i++) {
        if (gp->tex_hazardWagon[i]) SDL_DestroyTexture(gp->tex_hazardWagon[i]);
        gp->tex_hazardWagon[i] = NULL;
    }
}

void gameplay_reset_for_run(GameplayState *gp, int mode)
{
    SDL_Texture *keep[18] = {
        gp->tex_iconAmmo, gp->tex_iconDash, gp->tex_pickupAmmo, gp->tex_pickupHealth,
        gp->tex_pickupKronole, gp->tex_enemyRanged, gp->tex_enemyProjectile,
        gp->tex_bossFireball,
        gp->tex_doorLocked, gp->tex_playerDowned, gp->tex_namgoong,
        gp->tex_namgoong_run, gp->tex_namgoong_idle,
        gp->tex_wirePanel, gp->tex_spark,
        gp->tex_heartFull, gp->tex_heartEmpty, gp->tex_heartBreakSheet
    };
    SDL_Texture *keepHaz[5];
    for (int i = 0; i < 5; i++) keepHaz[i] = gp->tex_hazardWagon[i];
    int debug = gp->debugHitbox;
    int total = gp->totalInterludeScore;

    memset(gp, 0, sizeof(*gp));

    gp->tex_iconAmmo        = keep[0];
    gp->tex_iconDash        = keep[1];
    gp->tex_pickupAmmo      = keep[2];
    gp->tex_pickupHealth    = keep[3];
    gp->tex_pickupKronole   = keep[4];
    gp->tex_enemyRanged     = keep[5];
    gp->tex_enemyProjectile = keep[6];
    gp->tex_bossFireball    = keep[7];
    gp->tex_doorLocked      = keep[8];
    gp->tex_playerDowned    = keep[9];
    gp->tex_namgoong        = keep[10];
    gp->tex_namgoong_run    = keep[11];
    gp->tex_namgoong_idle   = keep[12];
    gp->tex_wirePanel       = keep[13];
    gp->tex_spark           = keep[14];
    gp->tex_heartFull       = keep[15];
    gp->tex_heartEmpty      = keep[16];
    gp->tex_heartBreakSheet = keep[17];
    for (int i = 0; i < 5; i++) gp->tex_hazardWagon[i] = keepHaz[i];

    gp->debugHitbox = debug;
    gp->totalInterludeScore = total;
    gp->gameMode = mode;

    for (int i = 0; i < NB_JOUEURS_MAX; i++) {
        gp->ammo[i] = AMMO_START;
        gp->dashLastEndTick[i] = 0;
        gp->invulnUntil[i] = 0;
        gp->permanentlyDead[i] = 0;
        gp->lives[i] = LIVES_MAX_PER_PLAYER;
        gp->lifeBreakIdx[i] = -1;
        gp->lifeBreakStartTick[i] = 0;
    }

    gp->namgoong.activeSolo = (mode == MODE_MONO) ? 1 : 0;
    gp->namgoong.hp = 100;
    gp->namgoong.maxHp = 100;

    g_prevWagon = -1;
}

static int rand_range(int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + rand() % (hi - lo + 1);
}

static void scale_enemy_for_level(Ennemi *e, int wagon)
{
    float lvlMult = 1.0f;
    if (wagon == 1) lvlMult = 1.20f;
    else if (wagon >= 2) lvlMult = 1.40f;
    /* Final boss gets a stronger multiplier so the fight has weight. */
    if (e->type == ET_ENFORCER && wagon == MAX_WAGONS - 1)
        lvlMult = BOSS_HP_MULT;
    int baseHp = 0;
    if (e->type == ET_GUARD)        baseHp = GUARD_HP;
    else if (e->type == ET_WORKER)  baseHp = WORKER_HP;
    else                            baseHp = ENFORCER_HP;
    int newMax = (int)((float)baseHp * ENEMY_HP_BASE_MULT * lvlMult);
    e->hp = newMax;
    e->attackCooldownMs = ENEMY_ATTACK_COOLDOWN_MS;
}

void gameplay_trigger_shake(GameplayState *gp, float amp, float seconds)
{
    if (!gp) return;
    if (amp > gp->shakeAmp)            gp->shakeAmp      = amp;
    if (seconds > gp->shakeTimeLeft)   gp->shakeTimeLeft = seconds;
}

void gameplay_update_shake(GameplayState *gp, float dt)
{
    if (!gp) return;
    if (gp->shakeTimeLeft <= 0.0f) {
        gp->shakeAmp        = 0.0f;
        gp->shakeOffsetX    = 0;
        gp->shakeOffsetY    = 0;
        gp->shakeTimeLeft   = 0.0f;
        return;
    }
    gp->shakeTimeLeft -= dt;
    gp->shakeAmp      -= SHAKE_DECAY_PER_SEC * dt;
    if (gp->shakeAmp < 0.0f)        gp->shakeAmp = 0.0f;
    if (gp->shakeTimeLeft <= 0.0f)  gp->shakeAmp = 0.0f;
    int amp = (int)gp->shakeAmp;
    if (amp <= 0) {
        gp->shakeOffsetX = gp->shakeOffsetY = 0;
        return;
    }
    int span = amp * 2 + 1;
    gp->shakeOffsetX = (rand() % span) - amp;
    gp->shakeOffsetY = (rand() % span) - amp;
}

static void hazards_spawn_for_wagon(GameplayState *gp, int wagon)
{
    if (wagon < 0 || wagon >= 5) return;
    /* Boss wagon is a clean arena — no hazards, no obstacles. */
    if (wagon == MAX_WAGONS - 1) return;
    StyleWagon *dummy = NULL; (void)dummy;

    int nbExisting = 0;
    for (int i = 0; i < gp->nbHazards; i++)
        if (gp->hazards[i].actif && gp->hazards[i].wagon == wagon) nbExisting++;
    if (nbExisting > 0) return;

    int target = (wagon == 0) ? 1 : (wagon < 3) ? 2 : 3;
    if (target > MAX_HAZARDS_PER_WAGON) target = MAX_HAZARDS_PER_WAGON;
    int dmg = HAZARD_BASE_DMG + wagon * 3;

    int hzW = 56, hzH = 38;
    int leftBound  = WAGON_MUR_G_X + WAGON_PORTE_W + 36;
    int rightBound = WAGON_MUR_D_X - WAGON_PORTE_W - 36 - hzW;
    if (rightBound < leftBound) return;
    int span = rightBound - leftBound;
    int slot = span / target;

    for (int k = 0; k < target; k++) {
        if (gp->nbHazards >= (int)(sizeof(gp->hazards)/sizeof(gp->hazards[0]))) break;
        Hazard *h = &gp->hazards[gp->nbHazards++];
        memset(h, 0, sizeof(*h));
        h->actif      = 1;
        h->wagon      = wagon;
        h->damage     = dmg;
        h->spriteCols = 4;
        h->spriteRows = 1;
        h->framesAnim = 4;
        h->animFps    = 8.0f;
        int jitter = rand_range(-12, 12);
        h->rect.x = leftBound + slot * k + slot / 2 - hzW / 2 + jitter;
        h->rect.y = WAGON_SOL_Y - hzH;
        h->rect.w = hzW;
        h->rect.h = hzH;
    }
}

void gameplay_init_wagon(GameplayState *gp, Background *bg, int wagon)
{
    if (wagon < 0 || wagon >= 5) return;
    if (gp->wagonInitialized[wagon]) return;
    gp->wagonInitialized[wagon] = 1;

    hazards_spawn_for_wagon(gp, wagon);

    StyleWagon *s = &bg->stylesWagons[wagon];

    int ammoSpawned = 0, healthSpawned = 0;
    if (gp->nbAmmoPickups < AMMO_PICKUPS_MAX && s->obstacles.nb >= 1) {
        for (int i = 0; i < s->obstacles.nb && ammoSpawned < 1; i++) {
            if (gp->nbAmmoPickups >= MAX_PICKUPS_PER_KIND) break;
            SDL_Rect *r = &s->obstacles.rects[i];
            if (r->h < 18) continue;
            ItemPickup *p = &gp->ammoPickups[gp->nbAmmoPickups++];
            p->actif = 1;
            p->collected = 0;
            p->x = (float)(r->x + r->w/2 - 10);
            p->y = (float)(r->y - 22);
            p->wagon = wagon;
            ammoSpawned++;
        }
    }
    if (gp->nbHealthPickups < HEALTH_PICKUPS_MAX && s->obstacles.nb >= 2) {
        for (int i = s->obstacles.nb - 1; i >= 0 && healthSpawned < 1; i--) {
            if (gp->nbHealthPickups >= MAX_PICKUPS_PER_KIND) break;
            SDL_Rect *r = &s->obstacles.rects[i];
            if (r->h < 18) continue;
            ItemPickup *p = &gp->healthPickups[gp->nbHealthPickups++];
            p->actif = 1;
            p->collected = 0;
            p->x = (float)(r->x + r->w/2 - 10);
            p->y = (float)(r->y - 22);
            p->wagon = wagon;
            healthSpawned++;
        }
    }

    /* No kronole in the boss arena — the door is already unlocked by the
     * time we get here and the wagon is a clean fight space. */
    if (gp->gameMode == MODE_MONO && wagon != MAX_WAGONS - 1) {
        int target = rand_range(KRONOLE_MIN_PER_LEVEL, KRONOLE_MAX_PER_LEVEL);
        for (int k = 0; k < target && gp->nbKronolePickups < MAX_KRONOLE_PER_LVL; k++) {
            ItemPickup *p = &gp->kronolePickups[gp->nbKronolePickups++];
            p->actif = 1;
            p->collected = 0;
            p->wagon = wagon;
            p->x = (float)rand_range(WAGON_MUR_G_X + 60,
                                      WAGON_MUR_D_X - WAGON_PORTE_W - 60);
            p->y = (float)(WAGON_SOL_Y - 22);
        }
    }

    int rangedTarget = (wagon >= 2) ? 2 : 1;
    int spawnedRanged = 0;
    for (int i = 0; i < bg->nbEnnemis && spawnedRanged < rangedTarget; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != wagon) continue;
        if (e->etat == ES_DEAD) continue;
        int already = 0;
        for (int k = 0; k < gp->nbRangedEnemies; k++)
            if (gp->rangedEnemyIndices[k] == i) { already = 1; break; }
        if (already) continue;
        if (gp->nbRangedEnemies < MAX_ENNEMIS) {
            gp->rangedEnemyIndices[gp->nbRangedEnemies++] = i;
            spawnedRanged++;
        }
    }

    for (int i = 0; i < bg->nbEnnemis; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->wagon != wagon) continue;
        if (e->etat == ES_DEAD) continue;
        scale_enemy_for_level(e, wagon);
    }
}

int gameplay_handle_player_shoot(GameplayState *gp, int idx, AudioSystem *audio)
{
    if (idx < 0 || idx >= NB_JOUEURS_MAX) return 0;
    if (gp->ammo[idx] <= 0) {
        if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
        return 0;
    }
    return 1;
}

void gameplay_handle_dash_input(GameplayState *gp, Background *bg,
                                AudioSystem *audio, int idx)
{
    if (idx < 0 || idx >= NB_JOUEURS_MAX) return;
    Joueur *j = &bg->joueurs[idx];
    if (!j->actif || j->hp <= 0) return;
    if (gp->dash[idx].active) return;
    Uint32 now = SDL_GetTicks();
    if (now - gp->dashLastEndTick[idx] < DASH_COOLDOWN_MS &&
        gp->dashLastEndTick[idx] != 0) return;
    if (idx == 1 && gp->namgoong.activeSolo) return;
    gp->dash[idx].active = 1;
    gp->dash[idx].startTick = now;
    gp->dash[idx].direction = j->direction;
    if (audio) audio_play_sfx(audio, SFX_JUMP);
}

static int rect_intersect(SDL_Rect a, SDL_Rect b)
{
    return SDL_HasIntersection(&a, &b);
}

static SDL_Rect player_rect(const Joueur *j)
{
    SDL_Rect r = {(int)j->x, (int)j->y, j->w, j->h};
    return r;
}

static void clamp_player_in_wagon(Joueur *j, Background *bg, int widx)
{
    int leftStop  = WAGON_MUR_G_X + (widx > 0 ? WAGON_PORTE_W : 0);
    int rightStop = WAGON_MUR_D_X - (widx < MAX_WAGONS-1 ? WAGON_PORTE_W : 0) - j->w;
    if (j->x < (float)leftStop)  j->x = (float)leftStop;
    if (j->x > (float)rightStop) j->x = (float)rightStop;
    (void)bg;
}

static void apply_dash_movement(GameplayState *gp, Background *bg, int idx, float dt)
{
    if (!gp->dash[idx].active) return;
    Joueur *j = &bg->joueurs[idx];
    Uint32 now = SDL_GetTicks();
    if (now - gp->dash[idx].startTick >= DASH_DURATION_MS) {
        gp->dash[idx].active = 0;
        gp->dashLastEndTick[idx] = now;
        return;
    }
    float speed = VITESSE_JOUEUR * DASH_SPEED_MULT;
    float advance = (float)gp->dash[idx].direction * speed * (dt * 60.0f);
    j->x += advance;
    j->velX = (float)gp->dash[idx].direction * speed;
    clamp_player_in_wagon(j, bg, bg->wagonActif);

    if (gp->nbTrails < MAX_DASH_TRAILS) {
        DashTrail *t = &gp->trails[gp->nbTrails++];
        t->x = j->x;
        t->y = j->y;
        t->alpha = 180;
        t->direction = j->direction;
        t->playerIdx = idx;
    }

    int hbw = (j->w > 0) ? j->w : JOUEUR_W;
    int hbh = (j->h > 0) ? j->h : JOUEUR_H;
    SDL_Rect jr = {(int)j->x, (int)j->y, hbw, hbh};
    for (int e = 0; e < bg->nbEnnemis; e++) {
        Ennemi *en = &bg->ennemis[e];
        if (!en->actif || en->wagon != bg->wagonActif || en->etat == ES_DEAD) continue;
        int ehw = ennemi_hbw(en);
        int ehh = ennemi_hbh(en);
        SDL_Rect er = {(int)en->x, (int)en->y, ehw, ehh};
        if (SDL_HasIntersection(&jr, &er)) {
            int max = (en->type == ET_GUARD) ? GUARD_HP : (en->type == ET_WORKER) ? WORKER_HP : ENFORCER_HP;
            int dmg = (max * DASH_ENEMY_DMG_PCT) / 100;
            if (dmg < 5) dmg = 5;
            int _is_boss = ennemi_is_boss(en);
            en->hp -= dmg;
            en->hitFlashMs = 120;
            if (_is_boss) boss_ai_on_hit();
            {
                DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                                     ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
                spawn_damage_number(en->x + ehw/2.0f, en->y + ehh/3.0f, dmg, t);
            }
            {
                EffectsSystem *efx = effects_global();
                /* dash dir: gp->dash[idx].direction matches the player's
                 * facing.  +1 = pushed right, -1 = pushed left. */
                int dirSign = gp->dash[idx].direction;
                if (efx) {
                    effects_on_hit(efx, EFX_ENT_ENEMY, e, dirSign);
                    effects_spawn_impact(efx, en->x + ehw/2.0f,
                                              en->y + ehh/3.0f,
                                              dirSign, dmg);
                }
                hitstop_trigger(dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
            }
            if (en->hp <= 0) {
                EffectsSystem *efx = effects_global();
                int kdir = gp->dash[idx].direction;
                if (efx) effects_spawn_death_for_enemy(efx, en, kdir);
                hitstop_trigger(0.12f);
                gameplay_trigger_shake(gp, SHAKE_HIT_AMP * 0.6f, 0.12f);
                int nf = (en->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                         (en->type == ET_GUARD)    ? GUARD_NFRAMES[2]    : WORKER_NFRAMES[2];
                en->etat = ES_DEAD;
                animation_set_row(&en->anim, ANIM_E_DEATH, nf, ENEMY_ANIM_FPS[ANIM_E_DEATH], 0);
                gp->enemiesKilled++;
            }
        }
    }
}

static void update_dash_trails(GameplayState *gp, float dt)
{
    int writeIdx = 0;
    int decay = (int)(dt * 600.0f);
    if (decay < 1) decay = 1;
    for (int i = 0; i < gp->nbTrails; i++) {
        gp->trails[i].alpha -= decay;
        if (gp->trails[i].alpha > 0) {
            if (writeIdx != i) gp->trails[writeIdx] = gp->trails[i];
            writeIdx++;
        }
    }
    gp->nbTrails = writeIdx;
}

static void check_pickups(GameplayState *gp, Background *bg, AudioSystem *audio)
{
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        SDL_Rect jr = player_rect(j);

        for (int i = 0; i < gp->nbAmmoPickups; i++) {
            ItemPickup *pk = &gp->ammoPickups[i];
            if (!pk->actif || pk->collected || pk->wagon != bg->wagonActif) continue;
            SDL_Rect pr = {(int)pk->x, (int)pk->y, 22, 22};
            if (rect_intersect(jr, pr)) {
                pk->collected = 1;
                pk->actif = 0;
                gp->ammo[p] += AMMO_PICKUP_AMOUNT;
                if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
            }
        }
        for (int i = 0; i < gp->nbHealthPickups; i++) {
            ItemPickup *pk = &gp->healthPickups[i];
            if (!pk->actif || pk->collected || pk->wagon != bg->wagonActif) continue;
            SDL_Rect pr = {(int)pk->x, (int)pk->y, 22, 22};
            if (rect_intersect(jr, pr)) {
                pk->collected = 1;
                pk->actif = 0;
                int gain = (JOUEUR_MAX_HP * HEALTH_PICKUP_PERCENT) / 100;
                int hpBefore = j->hp;
                j->hp += gain;
                if (j->hp > JOUEUR_MAX_HP) j->hp = JOUEUR_MAX_HP;
                spawn_damage_number(j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                                    j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                                    j->hp - hpBefore, DMG_NUM_HEAL);
                if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
            }
        }
        if (gp->gameMode == MODE_MONO && p == 0) {
            for (int i = 0; i < gp->nbKronolePickups; i++) {
                ItemPickup *pk = &gp->kronolePickups[i];
                if (!pk->actif || pk->collected || pk->wagon != bg->wagonActif) continue;
                SDL_Rect pr = {(int)pk->x, (int)pk->y, 18, 22};
                if (rect_intersect(jr, pr)) {
                    pk->collected = 1;
                    pk->actif = 0;
                    gp->kronoleCount++;
                    if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
                }
            }
        }
    }
}

static int find_closest_player(Background *bg, float ex, float ey, int *outIdx)
{
    float best = 1e9f;
    int idx = -1;
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        float dx = (j->x + j->w/2.0f) - ex;
        float dy = (j->y + j->h/2.0f) - ey;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d < best) { best = d; idx = p; }
    }
    if (outIdx) *outIdx = idx;
    return (idx >= 0);
}

static void update_ranged_enemies(GameplayState *gp, Background *bg, AudioSystem *audio)
{
    Uint32 now = SDL_GetTicks();
    for (int k = 0; k < gp->nbRangedEnemies; k++) {
        int idx = gp->rangedEnemyIndices[k];
        if (idx < 0 || idx >= bg->nbEnnemis) continue;
        Ennemi *e = &bg->ennemis[idx];
        if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) continue;
        int targetIdx = -1;
        if (!find_closest_player(bg, e->x, e->y, &targetIdx)) continue;
        Joueur *t = &bg->joueurs[targetIdx];
        /* Aim from the shooter's hitbox center so the projectile leaves
         * mid-body instead of near the top — matters for the boss whose
         * 150px-tall hitbox left bullets emerging up by the head. */
        float ecx = e->x + ennemi_hbw(e) / 2.0f;
        float ecy = e->y + ennemi_hbh(e) / 2.0f;
        float dx = (t->x + t->w/2.0f) - ecx;
        float dy = (t->y + t->h/2.0f) - ecy;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist > (float)RANGED_DETECT_RANGE) continue;
        if (now - gp->rangedLastFireTick[k] < RANGED_FIRE_COOLDOWN_MS &&
            gp->rangedLastFireTick[k] != 0) continue;
        if (gp->nbProjectiles >= MAX_ENEMY_PROJECTILES) continue;

        EnemyProjectile *pr = &gp->projectiles[gp->nbProjectiles++];
        pr->actif = 1;
        pr->is_fireball = ennemi_is_boss(e);
        pr->w = pr->is_fireball ? BOSS_FIREBALL_W : 16;
        pr->h = pr->is_fireball ? BOSS_FIREBALL_H : 8;
        /* Anchor so the sprite is centred on the shooter's hitbox centre.
         * Fireballs drop a bit lower so they leave from the boss's
         * hand/waist rather than dead center. */
        float spawn_y = ecy + (pr->is_fireball ? BOSS_FIREBALL_SPAWN_Y_OFFSET : 0);
        pr->x = ecx - pr->w / 2.0f;
        pr->y = spawn_y - pr->h / 2.0f;
        float sign = (dx >= 0) ? 1.0f : -1.0f;
        pr->velX = sign * RANGED_PROJECTILE_SPEED;
        pr->wagon = bg->wagonActif;
        pr->sourceEnemyIdx = idx;
        pr->spawnTick = now;
        e->direction = (sign > 0) ? 1 : -1;
        gp->rangedLastFireTick[k] = now;
        if (audio) audio_play_sfx(audio, SFX_ATTACK);
    }
}

static void update_projectiles(GameplayState *gp, Background *bg, AudioSystem *audio)
{
    int writeIdx = 0;
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < gp->nbProjectiles; i++) {
        EnemyProjectile *pr = &gp->projectiles[i];
        if (!pr->actif) continue;
        if (pr->wagon != bg->wagonActif) {
            if (writeIdx != i) gp->projectiles[writeIdx] = *pr;
            writeIdx++;
            continue;
        }
        pr->x += pr->velX;
        if (pr->x < -20 || pr->x > W_ECRAN + 20) { pr->actif = 0; continue; }

        int pr_w = (pr->w > 0) ? pr->w : 16;
        int pr_h = (pr->h > 0) ? pr->h : 8;
        SDL_Rect prRect = {(int)pr->x, (int)pr->y, pr_w, pr_h};

        StyleWagon *s = &bg->stylesWagons[bg->wagonActif];
        for (int o = 0; o < s->obstacles.nb; o++) {
            if (rect_intersect(prRect, s->obstacles.rects[o])) {
                pr->actif = 0;
                break;
            }
        }
        if (!pr->actif) continue;

        for (int p = 0; p < bg->nbJoueursActifs; p++) {
            Joueur *j = &bg->joueurs[p];
            if (!j->actif || j->hp <= 0) continue;
            if (gp->dash[p].active) continue;
            if (now < gp->invulnUntil[p]) continue;
            SDL_Rect jr = player_rect(j);
            if (rect_intersect(prRect, jr)) {
                int dmg = (JOUEUR_MAX_HP * RANGED_PROJECTILE_DMG_PCT) / 100;
                j->hp -= dmg;
                if (j->hp < 0) j->hp = 0;
                spawn_damage_number(j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                                    j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                                    dmg, DMG_NUM_PLAYER);
                gp->invulnUntil[p] = now + PLAYER_INVULN_MS;
                if (j->hp == 0) {
                    j->etat = PS_DEAD;
                    char_anim_set(&j->ca, ANIM_DEATH);
                } else if (j->etat != PS_HIT) {
                    animation_set_row(&j->anim, ANIM_P_HIT,
                        PLAYER_NFRAMES[ANIM_P_HIT], PLAYER_ANIM_FPS[ANIM_P_HIT], 0);
                    j->etat = PS_HIT;
                    char_anim_set(&j->ca, ANIM_HIT_REACT);
                }
                {
                    EffectsSystem *efx = effects_global();
                    int dirSign = (pr->velX >= 0.0f) ? 1 : -1;
                    if (efx) {
                        effects_on_hit(efx, EFX_ENT_PLAYER, p, dirSign);
                        effects_spawn_impact(efx,
                            j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                            j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                            dirSign, dmg);
                    }
                }
                if (audio) audio_play_sfx(audio, SFX_PLAYER_HURT);
                gameplay_trigger_shake(gp, SHAKE_HIT_AMP, 0.22f);
                pr->actif = 0;
                break;
            }
        }
        if (pr->actif) {
            if (writeIdx != i) gp->projectiles[writeIdx] = *pr;
            writeIdx++;
        }
    }
    gp->nbProjectiles = writeIdx;
}

static void update_hazards(GameplayState *gp, Background *bg, AudioSystem *audio, float dt)
{
    Uint32 now = SDL_GetTicks();
    for (int h = 0; h < gp->nbHazards; h++) {
        Hazard *hz = &gp->hazards[h];
        if (!hz->actif) continue;
        if (hz->framesAnim > 1) {
            hz->animTimer += dt;
            float dur = (hz->animFps > 0.0f) ? (1.0f / hz->animFps) : 0.125f;
            while (hz->animTimer >= dur) {
                hz->animTimer -= dur;
                hz->animFrame = (hz->animFrame + 1) % hz->framesAnim;
            }
        }
        if (hz->wagon != bg->wagonActif) continue;
        for (int p = 0; p < bg->nbJoueursActifs; p++) {
            Joueur *j = &bg->joueurs[p];
            if (!j->actif || j->hp <= 0) continue;
            if (gp->dash[p].active) continue;
            if (now < gp->invulnUntil[p]) continue;
            if (now - hz->lastHitTick[p] < HAZARD_HIT_COOLDOWN_MS) continue;
            SDL_Rect jr = player_rect(j);
            if (!rect_intersect(jr, hz->rect)) continue;

            j->hp -= hz->damage;
            if (j->hp < 0) j->hp = 0;
            spawn_damage_number(j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                                j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                                hz->damage, DMG_NUM_PLAYER);
            gp->invulnUntil[p]  = now + PLAYER_INVULN_MS;
            hz->lastHitTick[p]  = now;
            gameplay_trigger_shake(gp, SHAKE_HAZARD_AMP, 0.30f);
            if (audio) audio_play_sfx(audio, SFX_PLAYER_HURT);

            if (j->hp == 0) {
                j->etat = PS_DEAD;
                char_anim_set(&j->ca, ANIM_DEATH);
            } else if (j->etat != PS_HIT) {
                animation_set_row(&j->anim, ANIM_P_HIT,
                    PLAYER_NFRAMES[ANIM_P_HIT], PLAYER_ANIM_FPS[ANIM_P_HIT], 0);
                j->etat = PS_HIT;
                char_anim_set(&j->ca, ANIM_HIT_REACT);
            }
            {
                /* push away from hazard's centre */
                EffectsSystem *efx = effects_global();
                int hcx = hz->rect.x + hz->rect.w / 2;
                int dirSign = ((int)j->x + JOUEUR_W/2 >= hcx) ? 1 : -1;
                if (efx) {
                    effects_on_hit(efx, EFX_ENT_PLAYER, p, dirSign);
                    effects_spawn_impact(efx,
                        j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                        j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                        dirSign, hz->damage);
                }
            }
        }
    }
}

static void update_enemy_contact_damage(GameplayState *gp, Background *bg, AudioSystem *audio)
{
    Uint32 now = SDL_GetTicks();
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        if (gp->dash[p].active) continue;
        if (now < gp->invulnUntil[p]) continue;
        SDL_Rect jr = player_rect(j);
        for (int e = 0; e < bg->nbEnnemis; e++) {
            Ennemi *en = &bg->ennemis[e];
            if (!en->actif || en->wagon != bg->wagonActif || en->etat == ES_DEAD) continue;
            int hbw = ennemi_hbw(en);
            int hbh = ennemi_hbh(en);
            SDL_Rect er = {(int)en->x, (int)en->y, hbw, hbh};
            if (rect_intersect(jr, er)) {
                j->hp -= ENEMY_CONTACT_DMG;
                if (j->hp < 0) j->hp = 0;
                spawn_damage_number(j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                                    j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                                    ENEMY_CONTACT_DMG, DMG_NUM_PLAYER);
                gp->invulnUntil[p] = now + PLAYER_INVULN_MS;
                if (j->hp == 0) {
                    j->etat = PS_DEAD;
                    char_anim_set(&j->ca, ANIM_DEATH);
                } else {
                    char_anim_set(&j->ca, ANIM_HIT_REACT);
                }
                {
                    EffectsSystem *efx = effects_global();
                    int dirSign = ((int)j->x >= (int)en->x) ? 1 : -1;
                    if (efx) {
                        effects_on_hit(efx, EFX_ENT_PLAYER, p, dirSign);
                        effects_spawn_impact(efx,
                            j->x + (j->w > 0 ? j->w : JOUEUR_W) / 2.0f,
                            j->y + (j->h > 0 ? j->h : JOUEUR_H) / 3.0f,
                            dirSign, ENEMY_CONTACT_DMG);
                    }
                }
                if (audio) audio_play_sfx(audio, SFX_PLAYER_HURT);
                gameplay_trigger_shake(gp, SHAKE_HIT_AMP, 0.22f);
                break;
            }
        }
    }
}

static int count_enemies_alive_in_wagon(Background *bg, int wagon)
{
    int n = 0;
    for (int i = 0; i < bg->nbEnnemis; i++)
        if (bg->ennemis[i].actif && bg->ennemis[i].wagon == wagon &&
            bg->ennemis[i].etat != ES_DEAD) n++;
    return n;
}

void gameplay_handle_revive_request(GameplayState *gp, Background *bg,
                                    AudioSystem *audio, int reviverIdx)
{
    if (gp->gameMode != MODE_MULTI) return;
    if (reviverIdx < 0 || reviverIdx >= NB_JOUEURS_MAX) return;
    Joueur *r = &bg->joueurs[reviverIdx];
    if (!r->actif || r->hp <= 0) return;
    int targetIdx = reviverIdx ^ 1;
    if (targetIdx >= NB_JOUEURS_MAX) return;
    if (!gp->revival[targetIdx].actif || !gp->revival[targetIdx].revivable) return;
    if (gp->revival[targetIdx].deathWagon != bg->wagonActif) return;

    Joueur *d = &bg->joueurs[targetIdx];
    float dx = (r->x + r->w/2.0f) - (gp->revival[targetIdx].deathX + JOUEUR_W/2.0f);
    float dy = (r->y + r->h/2.0f) - (gp->revival[targetIdx].deathY + JOUEUR_H/2.0f);
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist > 60.0f) return;

    d->hp = JOUEUR_MAX_HP * 30 / 100;
    d->x = gp->revival[targetIdx].deathX;
    d->y = gp->revival[targetIdx].deathY;
    d->velX = d->velY = 0;
    d->etat = PS_IDLE;
    animation_set_row(&d->anim, ANIM_P_IDLE,
        PLAYER_NFRAMES[ANIM_P_IDLE], PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
    d->ca.is_locked = 0;
    char_anim_set(&d->ca, ANIM_IDLE);
    gp->revival[targetIdx].revivable = 0;
    gp->revival[targetIdx].revivedThisLevel = 1;
    gp->revival[targetIdx].actif = 0;
    if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
}

void gameplay_handle_door_interact(GameplayState *gp, Background *bg,
                                   AudioSystem *audio)
{
    if (gp->gameMode != MODE_MONO) return;
    if (bg->wagonActif >= MAX_WAGONS - 1) return;

    Joueur *j = &bg->joueurs[0];
    if (!j->actif || j->hp <= 0) return;
    int rightStop = WAGON_MUR_D_X - WAGON_PORTE_W;
    if (j->x + j->w < (float)(rightStop - 60)) return;

    if (gp->door.state == DOOR_UNLOCKING) return;
    /* Already unlocked: pressing E next to an open door used to fall
     * through, spend kronole, and reset state to DOOR_UNLOCKING — i.e.
     * re-lock a door namgoong already opened.  Make a 2nd press a no-op. */
    if (gp->door.state == DOOR_OPEN) return;

    if (gp->kronoleCount >= KRONOLE_DOOR_COST) {
        gp->kronoleCount -= KRONOLE_DOOR_COST;
        gp->door.actif = 1;
        gp->door.state = DOOR_UNLOCKING;
        gp->door.startTick = SDL_GetTicks();
        gp->door.pausedFor = 0;
        gp->door.paused = 0;
        gp->door.targetWagon = bg->wagonActif + 1;
        gp->door.enemiesSpawned = 1;
        gp->door.nbEnemiesAtUnlockStart = count_enemies_alive_in_wagon(bg, bg->wagonActif);

        gp->namgoong.working = 1;
        /* Stand flush against the locked door so the work-animation's
         * outstretched hand actually grips the panel.  The door asset's
         * LEFT edge sits at WAGON_MUR_D_X - WAGON_PORTE_W (see the
         * tex_doorLocked render in gameplay_render_world); align
         * namgoong's drawn right edge to that point with a small overlap
         * so the hand visibly reaches INTO the door across all 4 work
         * frames.  Was -70 (a 15px gap of empty air between sprite and
         * door), which made him look like he was fiddling with nothing. */
        #define NAMGOONG_DOOR_HAND_OVERLAP 10
        gp->namgoong.x = (float)(WAGON_MUR_D_X - WAGON_PORTE_W
                                  - PLAYER_DRAW_W + NAMGOONG_DOOR_HAND_OVERLAP);
        gp->namgoong.y = (float)(WAGON_SOL_Y - PLAYER_DRAW_H);
        gp->namgoong.wagon = bg->wagonActif;
        gp->namgoong.direction = 1;

        int spawned = 0;
        int target = 2 + (rand() % 3);
        SDL_Texture *gsheet = bg->enemySheets[ET_GUARD];
        SDL_Texture *wsheet = bg->enemySheets[ET_WORKER];
        for (int s = 0; s < target && bg->nbEnnemis < MAX_ENNEMIS; s++) {
            TypeEnnemi tp = (s % 2 == 0) ? ET_GUARD : ET_WORKER;
            float ex = (float)(WAGON_MUR_G_X + 40 + s * 60);
            float ey = (float)(WAGON_SOL_Y - (tp == ET_GUARD ? GUARD_HBH : WORKER_HBH));
            ennemis_spawn(bg->ennemis, &bg->nbEnnemis, tp, ex, ey,
                          bg->wagonActif, tp == ET_GUARD ? gsheet : wsheet);
            if (bg->nbEnnemis > 0)
                scale_enemy_for_level(&bg->ennemis[bg->nbEnnemis - 1], bg->wagonActif);
            spawned++;
        }

        snprintf(gp->door.message, sizeof(gp->door.message),
            "Deverrouillage en cours...");
        gp->door.messageStartTick = SDL_GetTicks();
        if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
    } else {
        gp->door.state = DOOR_LOCKED_NOT_ENOUGH;
        snprintf(gp->door.message, sizeof(gp->door.message),
            "J'ai besoin de Kronole... trouve m'en d'abord.");
        gp->door.messageStartTick = SDL_GetTicks();
    }
}

static void update_door(GameplayState *gp, Background *bg)
{
    if (gp->door.state != DOOR_UNLOCKING) return;
    Uint32 now = SDL_GetTicks();
    int alive = count_enemies_alive_in_wagon(bg, bg->wagonActif);
    if (alive > 0) {
        if (!gp->door.paused) {
            gp->door.paused = 1;
            gp->door.pausedFor = now;
        }
    } else {
        if (gp->door.paused) {
            gp->door.startTick += (now - gp->door.pausedFor);
            gp->door.paused = 0;
        }
    }
    Uint32 elapsed = now - gp->door.startTick;
    if (gp->door.paused) elapsed = (gp->door.pausedFor - gp->door.startTick);
    if (elapsed >= DOOR_UNLOCK_DURATION_MS && !gp->door.paused) {
        gp->door.state = DOOR_OPEN;
        gp->namgoong.working = 0;
        /* If this was the wagon 4 → wagon 5 door, arm the farewell
         * bubble.  Namgoong stops following past this point (see
         * update_namgoong), and the bubble plays over his head for
         * a few seconds so the player reads the goodbye line. */
        if (bg->wagonActif == MAX_WAGONS - 2)
            gp->namgoong.farewellTick = SDL_GetTicks();
    }
}

static void update_namgoong(GameplayState *gp, Background *bg, float dt)
{
    if (!gp->namgoong.activeSolo) return;
    if (gp->namgoong.hp <= 0) return;
    if (gp->namgoong.working) {
        animation_update(&gp->namgoong.workAnim, dt);
        return;
    }
    Joueur *j = &bg->joueurs[0];
    if (!j->actif) return;
    /* Namgoong stays behind once the player crosses into the boss
     * wagon — he holds the rest, per the farewell bubble.  He simply
     * stops updating his own wagon tracking and his render gate
     * (namgoong.wagon == bg->wagonActif) becomes false. */
    if (bg->wagonActif >= MAX_WAGONS - 1) {
        gp->namgoong.moving = 0;
        animation_update(&gp->namgoong.walkAnim, dt);
        return;
    }
    gp->namgoong.wagon = bg->wagonActif;
    float targetX = j->x - 50.0f;
    if (j->direction == -1) targetX = j->x + j->w + 20.0f;
    float dx = targetX - gp->namgoong.x;
    if (dx > 2.0f)      { gp->namgoong.x += 2.0f; gp->namgoong.direction = 1;  gp->namgoong.moving = 1; }
    else if (dx < -2.0f){ gp->namgoong.x -= 2.0f; gp->namgoong.direction = -1; gp->namgoong.moving = 1; }
    else                                                                     { gp->namgoong.moving = 0; }
    gp->namgoong.y = (float)(WAGON_SOL_Y - PLAYER_DRAW_H);
    if (gp->namgoong.x < (float)WAGON_MUR_G_X) gp->namgoong.x = (float)WAGON_MUR_G_X;
    if (gp->namgoong.x > (float)(WAGON_MUR_D_X - JOUEUR_W))
        gp->namgoong.x = (float)(WAGON_MUR_D_X - JOUEUR_W);
    animation_update(&gp->namgoong.walkAnim, dt);

    Uint32 now = SDL_GetTicks();
    for (int e = 0; e < bg->nbEnnemis; e++) {
        Ennemi *en = &bg->ennemis[e];
        if (!en->actif || en->wagon != bg->wagonActif || en->etat == ES_DEAD) continue;
        SDL_Rect nr = {(int)gp->namgoong.x, (int)gp->namgoong.y, JOUEUR_W, JOUEUR_H};
        int hbw = ennemi_hbw(en);
        int hbh = ennemi_hbh(en);
        SDL_Rect er = {(int)en->x, (int)en->y, hbw, hbh};
        if (rect_intersect(nr, er)) {
            if (now - gp->dashLastEndTick[1] > 1000) {
                gp->namgoong.hp -= 1;
                gp->dashLastEndTick[1] = now;
                spawn_damage_number(gp->namgoong.x + JOUEUR_W/2.0f,
                                    gp->namgoong.y + JOUEUR_H/3.0f,
                                    1, DMG_NUM_PLAYER);
            }
        }
    }
}

static void check_lives_consumption(GameplayState *gp, Background *bg,
                                     AudioSystem *audio)
{
    Uint32 now = SDL_GetTicks();
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif) continue;
        if (j->hp > 0) continue;
        if (gp->permanentlyDead[p]) continue;
        if (gp->lives[p] <= 0) continue;

        gp->lives[p]--;
        gp->lifeBreakIdx[p]       = gp->lives[p];
        gp->lifeBreakStartTick[p] = now;

        j->hp   = JOUEUR_MAX_HP;
        j->etat = PS_IDLE;
        animation_set_row(&j->anim, ANIM_P_IDLE,
                          PLAYER_NFRAMES[ANIM_P_IDLE],
                          PLAYER_ANIM_FPS[ANIM_P_IDLE], 1);
        j->ca.is_locked = 0;
        char_anim_set(&j->ca, ANIM_IDLE);
        gp->invulnUntil[p] = now + LIFE_INVULN_MS;

        gp->revival[p].actif     = 0;
        gp->revival[p].revivable = 0;

        gameplay_trigger_shake(gp, SHAKE_HAZARD_AMP * 1.2f, 0.40f);
        if (audio) audio_play_sfx(audio, SFX_PLAYER_HURT);
    }
}

static void update_life_break_anim(GameplayState *gp, Uint32 now)
{
    for (int p = 0; p < NB_JOUEURS_MAX; p++) {
        if (gp->lifeBreakIdx[p] < 0) continue;
        if (now - gp->lifeBreakStartTick[p] >= LIFE_BREAK_ANIM_MS) {
            gp->lifeBreakIdx[p] = -1;
        }
    }
}

static void check_player_deaths(GameplayState *gp, Background *bg)
{
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (j->hp > 0) {
            if (gp->revival[p].actif && gp->revival[p].revivable == 0) {
                gp->revival[p].actif = 0;
            }
            continue;
        }
        if (gp->revival[p].actif) continue;
        gp->revival[p].actif = 1;
        gp->revival[p].deathX = j->x;
        gp->revival[p].deathY = j->y;
        gp->revival[p].deathDirection = j->direction;
        gp->revival[p].deathWagon = bg->wagonActif;
        if (gp->gameMode == MODE_MULTI && !gp->revival[p].revivedThisLevel) {
            gp->revival[p].revivable = 1;
        } else {
            gp->revival[p].revivable = 0;
            gp->permanentlyDead[p] = 1;
        }
    }
}

static void on_wagon_change(GameplayState *gp, Background *bg)
{
    for (int p = 0; p < NB_JOUEURS_MAX; p++) {
        gp->revival[p].revivedThisLevel = 0;
        if (gp->revival[p].revivable && bg->joueurs[p].hp <= 0) {
            gp->permanentlyDead[p] = 1;
            gp->revival[p].revivable = 0;
            gp->revival[p].actif = 0;
        }
    }
    gp->door.state = DOOR_IDLE;
    gp->door.actif = 0;
}

void gameplay_update(GameplayState *gp, Background *bg, AudioSystem *audio, float dt)
{
    if (g_prevWagon != bg->wagonActif) {
        if (g_prevWagon >= 0) on_wagon_change(gp, bg);
        g_prevWagon = bg->wagonActif;
    }

    gameplay_init_wagon(gp, bg, bg->wagonActif);

    for (int p = 0; p < bg->nbJoueursActifs; p++)
        apply_dash_movement(gp, bg, p, dt);

    update_dash_trails(gp, dt);
    check_pickups(gp, bg, audio);
    update_ranged_enemies(gp, bg, audio);
    update_projectiles(gp, bg, audio);
    update_enemy_contact_damage(gp, bg, audio);
    update_hazards(gp, bg, audio, dt);
    update_door(gp, bg);
    update_namgoong(gp, bg, dt);
    check_lives_consumption(gp, bg, audio);
    check_player_deaths(gp, bg);
    update_life_break_anim(gp, SDL_GetTicks());
    gameplay_update_shake(gp, dt);

    int alive = 0;
    for (int i = 0; i < bg->nbEnnemis; i++)
        if (bg->ennemis[i].actif && bg->ennemis[i].etat != ES_DEAD) alive++;
    int killed = bg->nbEnnemis - alive;
    if (killed > gp->enemiesKilled) gp->enemiesKilled = killed;
}

int gameplay_check_advance(GameplayState *gp, Background *bg)
{
    if (bg->wagonActif >= MAX_WAGONS - 1) return 0;
    int rightStop = WAGON_MUR_D_X - WAGON_PORTE_W;
    int triggered = 0;
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        if (j->x + j->w >= (float)rightStop) {
            triggered = 1;
            break;
        }
    }
    if (!triggered) return 0;
    if (gp->gameMode == MODE_MONO) {
        if (gp->door.state != DOOR_OPEN) return 0;
        gp->door.state = DOOR_IDLE;
        gp->door.actif = 0;
    }
    return 1;
}

int gameplay_anyone_alive(Background *bg, GameplayState *gp)
{
    int aliveAny = 0;
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        if (gp->permanentlyDead[p]) continue;
        if (bg->joueurs[p].hp > 0) { aliveAny = 1; break; }
        if (gp->revival[p].revivable) { aliveAny = 1; break; }
    }
    return aliveAny;
}

void gameplay_render_world(GameplayState *gp, SDL_Renderer *r,
                           Background *bg, int ox)
{
    Uint32 now = SDL_GetTicks();

    if (gp->gameMode == MODE_MONO && bg->wagonActif < MAX_WAGONS - 1) {
        int dx = ox + WAGON_MUR_D_X - WAGON_PORTE_W;
        int dy = WAGON_SOL_Y - 112;
        SDL_Rect ddst = {dx, dy, WAGON_PORTE_W + 2, 112};
        if (gp->tex_doorLocked && gp->door.state != DOOR_OPEN) {
            SDL_RenderCopy(r, gp->tex_doorLocked, NULL, &ddst);
        }
    }

    SDL_Texture *hzTex = (bg->wagonActif >= 0 && bg->wagonActif < 5)
                         ? gp->tex_hazardWagon[bg->wagonActif] : NULL;
    for (int i = 0; i < gp->nbHazards; i++) {
        Hazard *hz = &gp->hazards[i];
        if (!hz->actif || hz->wagon != bg->wagonActif) continue;
        SDL_Rect dst = {hz->rect.x + ox, hz->rect.y, hz->rect.w, hz->rect.h};
        if (hzTex) {
            int tw = 0, th = 0;
            SDL_QueryTexture(hzTex, NULL, NULL, &tw, &th);
            int cols = (hz->spriteCols > 0) ? hz->spriteCols : 1;
            int rows = (hz->spriteRows > 0) ? hz->spriteRows : 1;
            int frameW = tw / cols;
            int frameH = th / rows;
            SDL_Rect src = {hz->animFrame * frameW, 0, frameW, frameH};
            SDL_RenderCopy(r, hzTex, &src, &dst);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            Uint8 pulse = (Uint8)(160 + (int)(sinf((float)SDL_GetTicks() * 0.012f + i) * 60.0f));
            SDL_SetRenderDrawColor(r, 220, 60, 60, pulse);
            SDL_RenderFillRect(r, &dst);
            SDL_SetRenderDrawColor(r, 255, 200, 80, 220);
            SDL_RenderDrawRect(r, &dst);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
    }

    Uint32 nowMs = SDL_GetTicks();
    for (int i = 0; i < gp->nbAmmoPickups; i++) {
        ItemPickup *p = &gp->ammoPickups[i];
        if (!p->actif || p->collected || p->wagon != bg->wagonActif) continue;
        int hover = (int)(sinf((float)nowMs * 0.004f + i * 1.3f) * 5.0f);
        SDL_Rect dst = {(int)p->x + ox - 11, (int)p->y - hover - 11, 44, 44};
        if (gp->tex_pickupAmmo) SDL_RenderCopy(r, gp->tex_pickupAmmo, NULL, &dst);
        else { SDL_SetRenderDrawColor(r, 230, 200, 80, 255); SDL_RenderFillRect(r, &dst); }
    }
    for (int i = 0; i < gp->nbHealthPickups; i++) {
        ItemPickup *p = &gp->healthPickups[i];
        if (!p->actif || p->collected || p->wagon != bg->wagonActif) continue;
        int hover = (int)(sinf((float)nowMs * 0.004f + i * 1.7f + 0.5f) * 5.0f);
        SDL_Rect dst = {(int)p->x + ox - 11, (int)p->y - hover - 11, 44, 44};
        if (gp->tex_pickupHealth) SDL_RenderCopy(r, gp->tex_pickupHealth, NULL, &dst);
        else { SDL_SetRenderDrawColor(r, 220, 60, 60, 255); SDL_RenderFillRect(r, &dst); }
    }
    if (gp->gameMode == MODE_MONO) {
        for (int i = 0; i < gp->nbKronolePickups; i++) {
            ItemPickup *p = &gp->kronolePickups[i];
            if (!p->actif || p->collected || p->wagon != bg->wagonActif) continue;
            int hover = (int)(sinf((float)nowMs * 0.005f + i * 2.1f) * 6.0f);
            SDL_Rect dst = {(int)p->x + ox - 7, (int)p->y - hover - 7, 36, 48};
            if (gp->tex_pickupKronole) SDL_RenderCopy(r, gp->tex_pickupKronole, NULL, &dst);
            else { SDL_SetRenderDrawColor(r, 245, 220, 90, 255); SDL_RenderFillRect(r, &dst); }
        }
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < gp->nbTrails; i++) {
        DashTrail *t = &gp->trails[i];
        SDL_Rect dst = {(int)t->x + ox, (int)t->y, JOUEUR_W, JOUEUR_H};
        SDL_SetRenderDrawColor(r, 255, 200, 80, (Uint8)(t->alpha > 200 ? 200 : t->alpha));
        SDL_RenderFillRect(r, &dst);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    for (int i = 0; i < gp->nbProjectiles; i++) {
        EnemyProjectile *pr = &gp->projectiles[i];
        if (!pr->actif || pr->wagon != bg->wagonActif) continue;
        int pr_w = (pr->w > 0) ? pr->w : 16;
        int pr_h = (pr->h > 0) ? pr->h : 8;
        SDL_Rect dst = {(int)pr->x + ox, (int)pr->y, pr_w, pr_h};
        SDL_Texture *tex = pr->is_fireball ? gp->tex_bossFireball
                                           : gp->tex_enemyProjectile;
        if (tex) {
            /* Regular bullet sheet faces right; the boss fireball sheet
             * faces left, so flip its sign convention. */
            SDL_RendererFlip f;
            if (pr->is_fireball)
                f = (pr->velX < 0) ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL;
            else
                f = (pr->velX < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            if (pr->is_fireball) {
                /* Animate through the 5x5 sheet by elapsed-time / frame_ms.
                 * Frame size is read from the texture so a different cell
                 * size still works as long as the grid is 5x5. */
                int tw = 0, th = 0;
                SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
                int fw = (tw > 0) ? tw / BOSS_FIREBALL_COLS : BOSS_FIREBALL_W;
                int fh = (th > 0) ? th / BOSS_FIREBALL_ROWS : BOSS_FIREBALL_H;
                Uint32 elapsed = now - pr->spawnTick;
                Uint32 frame_ms = (Uint32)(1000 / BOSS_FIREBALL_FPS);
                if (frame_ms == 0) frame_ms = 1;
                int frame = (int)((elapsed / frame_ms) % BOSS_FIREBALL_FRAME_COUNT);
                SDL_Rect src = { (frame % BOSS_FIREBALL_COLS) * fw,
                                 (frame / BOSS_FIREBALL_COLS) * fh,
                                 fw, fh };
                SDL_RenderCopyEx(r, tex, &src, &dst, 0, NULL, f);
            } else {
                SDL_RenderCopyEx(r, tex, NULL, &dst, 0, NULL, f);
            }
        } else {
            /* Placeholder visual until boss_fireball.png ships: an orange
             * square with a yellow core so the fireball is still readable. */
            if (pr->is_fireball) {
                SDL_SetRenderDrawColor(r, 240, 120, 30, 255);
                SDL_RenderFillRect(r, &dst);
                SDL_Rect core = { dst.x + pr_w/4, dst.y + pr_h/4,
                                  pr_w/2, pr_h/2 };
                SDL_SetRenderDrawColor(r, 255, 220, 80, 255);
                SDL_RenderFillRect(r, &core);
            } else {
                SDL_SetRenderDrawColor(r, 220, 100, 100, 255);
                SDL_RenderFillRect(r, &dst);
            }
        }
    }

    /* Player invulnerability blink: char_anim now owns the player render
     * path so the blink lives in effects.c (effects_hit_flash_alpha).
     * Nothing to alpha-mod here.                                       */
    (void)bg; (void)gp; (void)now;

    if (gp->gameMode == MODE_MULTI) {
        for (int p = 0; p < NB_JOUEURS_MAX; p++) {
            if (gp->revival[p].revivable && gp->revival[p].deathWagon == bg->wagonActif) {
                SDL_Rect dst = {(int)gp->revival[p].deathX + ox,
                                (int)gp->revival[p].deathY, JOUEUR_W, JOUEUR_H};
                if (gp->tex_playerDowned)
                    SDL_RenderCopy(r, gp->tex_playerDowned, NULL, &dst);
                else {
                    SDL_SetRenderDrawColor(r, 80, 80, 90, 255);
                    SDL_RenderFillRect(r, &dst);
                }
            }
        }
    }

    if (gp->gameMode == MODE_MONO && gp->namgoong.activeSolo &&
        gp->namgoong.wagon == bg->wagonActif && gp->namgoong.hp > 0) {
        SDL_Texture *sheet = NULL;
        SDL_Rect src = {0, 0, JOUEUR_W, JOUEUR_H};
        /* Door-opening (working) sprite stays at the pre-bump size — when
         * he sits down at the door the larger player-size rect made him
         * loom over the door.  Following/running sprite uses the bigger
         * PLAYER_DRAW size so he scales with Ellie/Curtis on screen.    */
        int draw_w = PLAYER_DRAW_W;
        int draw_h = PLAYER_DRAW_H;
        int working_sit = 0;
        if (gp->namgoong.working && gp->tex_namgoong) {
            sheet = gp->tex_namgoong;
            int tw, th; SDL_QueryTexture(sheet, NULL, NULL, &tw, &th);
            int frame = (int)((now / 200) % 4);
            int cellW = tw / 4;
            src = (SDL_Rect){frame * cellW, 0, cellW, th};
            draw_w = 94;
            draw_h = 125;
            working_sit = 1;
        } else {
            /* Following-player namgoong: pick the run sheet while
             * chasing, idle sheet when stopped near the player.       */
            int use_idle = (!gp->namgoong.moving) && gp->tex_namgoong_idle;
            sheet = use_idle ? gp->tex_namgoong_idle : gp->tex_namgoong_run;
            if (sheet) {
                int tw, th; SDL_QueryTexture(sheet, NULL, NULL, &tw, &th);
                int cols = 5, rows = 5;
                int cellW = tw / cols, cellH = th / rows;
                /* Slower frame timing than before — run was 40 ms per
                 * frame (25 fps); now 70 ms (~14 fps) so the stride
                 * reads naturally.  Idle ticks at 100 ms.            */
                int frame_ms = use_idle ? 100 : 70;
                int frame = (int)((now / frame_ms) % (cols * rows));
                int col   = frame % cols;
                int row   = frame / cols;
                src = (SDL_Rect){col * cellW, row * cellH, cellW, cellH};
            }
        }
        /* During the dialogue cinematic the NPC is the "speaker"; add a
         * subtle 2px sine bob to signal he's talking.  The non-speaking
         * player gets no bob. */
        int bob = dialogue_cinematic_speaker_bob_offset(
                      dialogue_cinematic_global());
        /* Same foot-padding shift the player render applies: the art has
         * empty pixels at the bottom of each frame, so without this he
         * floats above the floor.                                       */
        int n_foot_shift = (int)((float)draw_h * CHAR_FOOT_PADDING_FRAC + 0.5f);
        /* When sitting, anchor the render foot to the wagon floor
         * directly so the smaller rect doesn't inherit the bigger
         * namgoong.y (which is sized for PLAYER_DRAW_H).               */
        int dst_y = working_sit
                  ? (WAGON_SOL_Y - draw_h + n_foot_shift + bob)
                  : ((int)gp->namgoong.y + bob + n_foot_shift);
        SDL_Rect dst = {(int)gp->namgoong.x + ox, dst_y, draw_w, draw_h};
        if (sheet) {
            SDL_SetTextureColorMod(sheet, 200, 255, 200);
            SDL_RendererFlip flip = (gp->namgoong.direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(r, sheet, &src, &dst, 0, NULL, flip);
            SDL_SetTextureColorMod(sheet, 255, 255, 255);
        }
    }

    if (gp->debugHitbox) {
        SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
        for (int p = 0; p < bg->nbJoueursActifs; p++) {
            Joueur *j = &bg->joueurs[p];
            if (!j->actif) continue;
            SDL_Rect jr = {(int)j->x + ox, (int)j->y, j->w, j->h};
            SDL_RenderDrawRect(r, &jr);
        }
        SDL_SetRenderDrawColor(r, 0, 255, 255, 255);
        for (int e = 0; e < bg->nbEnnemis; e++) {
            Ennemi *en = &bg->ennemis[e];
            if (!en->actif || en->wagon != bg->wagonActif || en->etat == ES_DEAD) continue;
            int hbw = ennemi_hbw(en);
            int hbh = ennemi_hbh(en);
            SDL_Rect er = {(int)en->x + ox, (int)en->y, hbw, hbh};
            SDL_RenderDrawRect(r, &er);
        }
    }
}

static void render_lives_row(GameplayState *gp, SDL_Renderer *r,
                              UIFontPack *fonts, UITextCache *cache,
                              int player, int rowX, int rowY,
                              const char *label)
{
    const int heartW = 28;
    const int heartH = 28;
    const int gap    = 4;

    if (label) {
        SDL_Color amber = {235, 180, 60, 255};
        TTF_Font *f = fonts->monoSmall ? fonts->monoSmall :
                      (fonts->mono ? fonts->mono : fonts->fallback);
        if (f) ui_draw_text(cache, r, f, label, amber, rowX, rowY - 14);
    }

    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < LIVES_MAX_PER_PLAYER; i++) {
        SDL_Rect dst = { rowX + i * (heartW + gap), rowY, heartW, heartH };

        int breaking = (gp->lifeBreakIdx[player] == i &&
                         now - gp->lifeBreakStartTick[player] < LIFE_BREAK_ANIM_MS);
        int full     = (i < gp->lives[player]);

        if (breaking && gp->tex_heartBreakSheet) {
            Uint32 elapsed = now - gp->lifeBreakStartTick[player];

            int sheet_w = 0, sheet_h = 0;
            SDL_QueryTexture(gp->tex_heartBreakSheet, NULL, NULL,
                              &sheet_w, &sheet_h);
            int frame_count = (sheet_h > 0) ? (sheet_w / sheet_h) : LIFE_BREAK_FRAMES;
            if (frame_count < 1) frame_count = LIFE_BREAK_FRAMES;
            int frame_w = sheet_w / frame_count;
            int frame_h = sheet_h;

            int frame = (int)((elapsed * (Uint32)frame_count) / LIFE_BREAK_ANIM_MS);
            if (frame >= frame_count) frame = frame_count - 1;

            SDL_Rect src = { frame * frame_w, 0, frame_w, frame_h };
            int wobble = ((elapsed / 40) % 2) ? 1 : -1;
            SDL_Rect wob = { dst.x + wobble, dst.y + wobble, dst.w, dst.h };
            SDL_RenderCopy(r, gp->tex_heartBreakSheet, &src, &wob);
            continue;
        }

        if (full && gp->tex_heartFull) {
            int   phase  = (int)((now / 80u) % 13u) - 6;
            float pulse  = 1.0f + 0.04f * (float)phase / 6.0f;
            int   pw     = (int)((float)heartW * pulse);
            int   ph     = (int)((float)heartH * pulse);
            if (pw < 1) pw = 1; if (pw > heartW * 4) pw = heartW * 4;
            if (ph < 1) ph = 1; if (ph > heartH * 4) ph = heartH * 4;
            SDL_Rect ps = { dst.x + (heartW - pw) / 2,
                            dst.y + (heartH - ph) / 2, pw, ph };
            SDL_RenderCopy(r, gp->tex_heartFull, NULL, &ps);
        } else if (!full && gp->tex_heartEmpty) {
            SDL_SetTextureAlphaMod(gp->tex_heartEmpty, 200);
            SDL_RenderCopy(r, gp->tex_heartEmpty, NULL, &dst);
            SDL_SetTextureAlphaMod(gp->tex_heartEmpty, 255);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, full ? 220 : 80,
                                       full ? 50  : 80,
                                       full ? 80  : 80,
                                       full ? 255 : 180);
            SDL_RenderFillRect(r, &dst);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderDrawRect(r, &dst);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        }
    }
}

void gameplay_render_hud(GameplayState *gp, SDL_Renderer *r,
                         Background *bg,
                         UIFontPack *fonts,
                         UITextCache *cache,
                         UIAssets *assets,
                         int scoreJoueur)
{
    (void)assets;
    SDL_Color amber  = {235, 180,  60, 255};
    SDL_Color rouge  = {220,  60,  60, 255};
    SDL_Color blanc  = {235, 235, 235, 255};
    SDL_Color cyan   = { 90, 200, 230, 255};

    Uint32 now = SDL_GetTicks();
    int hudTopY = H_ECRAN - 88;

    int hudW = 600;
    int hudX = (W_ECRAN - hudW) / 2;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 5, 8, 18, 200);
    SDL_Rect hudHi = {hudX, hudTopY, hudW, 44};
    SDL_RenderFillRect(r, &hudHi);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    if (gp->gameMode == MODE_MULTI && bg->nbJoueursActifs >= 2) {
        render_lives_row(gp, r, fonts, cache, 0, 15, 38, "P1");
        render_lives_row(gp, r, fonts, cache, 1, 15, 78, "P2");

        /* Two-player HP bars: P1 top-left, P2 top-right (mirrors the
         * camera-side convention used by the split-screen view).      */
        SDL_Color amberAccent = {235, 180,  60, 255};
        SDL_Color cyanAccent  = { 90, 200, 230, 255};
        SDL_Color barBack     = { 20,  20,  28, 255};
        SDL_Rect p1Bar = { 15, 18, 200, 10 };
        SDL_Rect p2Bar = { W_ECRAN - 215, 18, 200, 10 };
        two_player_render_hp_bar(r, p1Bar,
            bg->joueurs[0].hp, JOUEUR_MAX_HP,
            amberAccent, barBack, "P1", fonts, cache);
        two_player_render_hp_bar(r, p2Bar,
            bg->joueurs[1].hp, JOUEUR_MAX_HP,
            cyanAccent,  barBack, "P2", fonts, cache);
    } else {
        render_lives_row(gp, r, fonts, cache, 0, 15, 38, NULL);
    }

    int iconY = hudTopY + 8;
    int colMun  = hudX + 12;
    int colDash = hudX + 180;
    int colKron = hudX + 320;
    int colNam  = hudX + hudW - 160;

    if (gp->tex_iconAmmo) {
        SDL_Rect dst = {colMun, iconY, 18, 18};
        SDL_RenderCopy(r, gp->tex_iconAmmo, NULL, &dst);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Munitions : %d", gp->ammo[0]);
    ui_draw_text(cache, r, fonts->mono, buf, amber, colMun + 24, iconY);

    Uint32 sinceDash = (gp->dashLastEndTick[0] == 0) ? DASH_COOLDOWN_MS :
                      (now - gp->dashLastEndTick[0]);
    int dashReady = (sinceDash >= DASH_COOLDOWN_MS) ? 1 : 0;
    if (gp->tex_iconDash) {
        SDL_Rect dst = {colDash, iconY, 18, 18};
        if (!dashReady) SDL_SetTextureColorMod(gp->tex_iconDash, 90, 90, 110);
        SDL_RenderCopy(r, gp->tex_iconDash, NULL, &dst);
        SDL_SetTextureColorMod(gp->tex_iconDash, 255, 255, 255);
    }
    if (dashReady)
        ui_draw_text(cache, r, fonts->mono, "Dash", cyan, colDash + 24, iconY);
    else {
        snprintf(buf, sizeof(buf), "%.1fs", (float)(DASH_COOLDOWN_MS - sinceDash) / 1000.0f);
        ui_draw_text(cache, r, fonts->mono, buf, blanc, colDash + 24, iconY);
    }

    if (gp->gameMode == MODE_MONO) {
        if (gp->tex_pickupKronole) {
            SDL_Rect dst = {colKron, iconY, 16, 20};
            SDL_RenderCopy(r, gp->tex_pickupKronole, NULL, &dst);
        }
        snprintf(buf, sizeof(buf), "Kronole : %d", gp->kronoleCount);
        ui_draw_text(cache, r, fonts->mono, buf, amber, colKron + 22, iconY);
    }

    if (gp->gameMode == MODE_MONO && gp->namgoong.activeSolo) {
        int barW = 140;
        int filled = (gp->namgoong.hp * barW) / gp->namgoong.maxHp;
        int bx = colNam;
        int by = iconY + 4;
        SDL_SetRenderDrawColor(r, 30, 50, 30, 255);
        SDL_Rect bgB = {bx, by, barW, 8};
        SDL_RenderFillRect(r, &bgB);
        SDL_SetRenderDrawColor(r, 80, 200, 110, 255);
        SDL_Rect fgB = {bx, by, filled, 8};
        SDL_RenderFillRect(r, &fgB);
        ui_draw_text(cache, r, fonts->storySmall ? fonts->storySmall : fonts->mono,
                     "Namgoong", blanc, bx, by - 14);
    }

    if (gp->gameMode == MODE_MONO && gp->door.state == DOOR_UNLOCKING) {
        int barW = 280;
        Uint32 elapsed = now - gp->door.startTick;
        if (gp->door.paused)
            elapsed = (gp->door.pausedFor - gp->door.startTick);
        if (elapsed > DOOR_UNLOCK_DURATION_MS) elapsed = DOOR_UNLOCK_DURATION_MS;
        int filled = (int)((float)elapsed / (float)DOOR_UNLOCK_DURATION_MS * (float)barW);
        int bx = (W_ECRAN - barW) / 2;
        int by = 12;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
        SDL_Rect bgD = {bx - 4, by - 18, barW + 8, 32};
        SDL_RenderFillRect(r, &bgD);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 30, 30, 50, 255);
        SDL_Rect bgB = {bx, by, barW, 10};
        SDL_RenderFillRect(r, &bgB);
        SDL_SetRenderDrawColor(r, 235, 180, 60, 255);
        SDL_Rect fgB = {bx, by, filled, 10};
        SDL_RenderFillRect(r, &fgB);
        ui_draw_text_centered(cache, r, fonts->storySmall ? fonts->storySmall : fonts->mono,
            gp->door.paused ? "Deverrouillage... (eliminez les ennemis)" : "Deverrouillage...",
            blanc, W_ECRAN/2, by - 16);
    }

    if (gp->door.state == DOOR_LOCKED_NOT_ENOUGH &&
        now - gp->door.messageStartTick < 2500) {
        ui_draw_text_centered(cache, r,
            fonts->story ? fonts->story : fonts->mono,
            gp->door.message, rouge, W_ECRAN/2, H_ECRAN/2 - 30);
    }

    if (gp->gameMode == MODE_MULTI) {
        for (int p = 0; p < NB_JOUEURS_MAX; p++) {
            if (gp->revival[p].revivable && gp->revival[p].deathWagon == bg->wagonActif) {
                int rx = (int)gp->revival[p].deathX + JOUEUR_W/2;
                int ry = (int)gp->revival[p].deathY - 22;
                int otherIdx = p ^ 1;
                Joueur *o = &bg->joueurs[otherIdx];
                if (o->hp <= 0) continue;
                float dx = (o->x + o->w/2.0f) - (gp->revival[p].deathX + JOUEUR_W/2.0f);
                float dy = (o->y + o->h/2.0f) - (gp->revival[p].deathY + JOUEUR_H/2.0f);
                float d = sqrtf(dx*dx + dy*dy);
                if (d < 80.0f) {
                    ui_draw_text_centered(cache, r,
                        fonts->storySmall ? fonts->storySmall : fonts->mono,
                        "Appuyer sur E pour ranimer", amber, rx, ry);
                }
            }
        }
    }

    if (gp->gameMode == MODE_MONO && gp->kronoleCount >= KRONOLE_DOOR_COST &&
        gp->door.state != DOOR_UNLOCKING && gp->door.state != DOOR_OPEN &&
        bg->wagonActif < MAX_WAGONS - 1) {
        Joueur *j = &bg->joueurs[0];
        int rightStop = WAGON_MUR_D_X - WAGON_PORTE_W;
        if (j->hp > 0 && j->x + j->w >= (float)(rightStop - 60)) {
            ui_draw_text_centered(cache, r,
                fonts->storySmall ? fonts->storySmall : fonts->mono,
                "Appuyer sur E pour payer Namgoong",
                amber, W_ECRAN/2, H_ECRAN/2 - 30);
        }
    }

    if (gp->ammo[0] == 0) {
        ui_draw_text(cache, r, fonts->mono,
            "Munitions epuisees - melee uniquement",
            rouge, 12, hudTopY - 16);
    }

    if (gp->saveToastUntil > now) {
        Uint32 remain = gp->saveToastUntil - now;
        int alpha = (remain < 400u) ? (int)((remain * 220u) / 400u) : 220;
        if (alpha < 0) alpha = 0; if (alpha > 220) alpha = 220;
        const int boxW = 240, boxH = 40;
        int bx = (W_ECRAN - boxW) / 2;
        int by = 60;
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 10, 30, 12, alpha);
        SDL_Rect box = {bx, by, boxW, boxH};
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 80, 200, 110, alpha);
        SDL_RenderDrawRect(r, &box);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_Color verT = {200, 240, 200, 255};
        ui_draw_text_centered(cache, r,
            fonts->story ? fonts->story : fonts->mono,
            "Partie sauvegardee", verT,
            W_ECRAN / 2, by + boxH / 2 - 10);
    }

    (void)scoreJoueur;
}

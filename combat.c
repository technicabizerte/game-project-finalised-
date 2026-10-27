#include "combat.h"
#include "background.h"
#include "effects.h"
#include "damage_numbers.h"
#include "boss_anim.h"
#include "boss_ai.h"
#include "boss_hud.h"               /* hitstop_trigger on player attacks  */
#include <stdio.h>

void animation_init(AnimationSystem *anim, SDL_Texture *tex,
                    int num_cols, int num_rows,
                    int row, int frame_count, float fps, int loop)
{
    int w = 0, h = 0;
    if (tex) SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    anim->texture       = tex;
    anim->frame_w       = (num_cols > 0 && w > 0) ? w / num_cols : 48;
    anim->frame_h       = (num_rows > 0 && h > 0) ? h / num_rows : 64;
    anim->current_row   = row;
    anim->current_frame = 0;
    anim->frame_count   = frame_count;
    anim->frame_timer   = 0.0f;
    anim->fps           = (fps > 0.0f) ? fps : 1.0f;
    anim->loop          = loop;
    anim->finished      = 0;
}

void animation_update(AnimationSystem *anim, float dt)
{
    if (anim->finished && !anim->loop) return;
    anim->frame_timer += dt;
    float frame_duration = 1.0f / anim->fps;
    while (anim->frame_timer >= frame_duration) {
        anim->frame_timer -= frame_duration;
        anim->current_frame++;
        if (anim->current_frame >= anim->frame_count) {
            if (anim->loop) {
                anim->current_frame = 0;
            } else {
                anim->current_frame = anim->frame_count - 1;
                anim->finished      = 1;
                anim->frame_timer   = 0.0f;
                return;
            }
        }
    }
}

SDL_Rect animation_get_src_rect(const AnimationSystem *anim)
{
    SDL_Rect r;
    r.x = anim->current_frame * anim->frame_w;
    r.y = anim->current_row   * anim->frame_h;
    r.w = anim->frame_w;
    r.h = anim->frame_h;
    return r;
}

void animation_set_row(AnimationSystem *anim,
                        int row, int frame_count, float fps, int loop)
{
    if (anim->current_row == row) return;
    anim->current_row   = row;
    anim->current_frame = 0;
    anim->frame_count   = frame_count;
    anim->fps           = (fps > 0.0f) ? fps : 1.0f;
    anim->frame_timer   = 0.0f;
    anim->loop          = loop;
    anim->finished      = 0;
}

SDL_Texture *combat_loadTexture(SDL_Renderer *r, const char *path)
{
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) {
        fprintf(stderr, "[combat] load '%s': %s\n", path, IMG_GetError());
        return NULL;
    }
    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 255, 0, 255));
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return t;
}

void balles_update(Balle *balles, int *nb,
                   Ennemi *ennemis, int *nbEnnemis,
                   int *joueurHp, EtatJoueur *joueurEtat, AnimationSystem *joueurAnim,
                   float joueurX, float joueurY, int joueurW, int joueurH)
{
    (void)joueurHp; (void)joueurEtat; (void)joueurAnim;
    (void)joueurX;  (void)joueurY;    (void)joueurW; (void)joueurH;

    for (int i = 0; i < *nb; i++) {
        Balle *b = &balles[i];
        if (!b->actif) continue;

        b->x += b->velX;
        if (b->x < WAGON_MUR_G_X || b->x > WAGON_MUR_D_X) { b->actif = 0; continue; }

        SDL_Rect br = {(int)b->x, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
        for (int j = 0; j < *nbEnnemis; j++) {
            Ennemi *e = &ennemis[j];
            if (!e->actif || e->etat == ES_DEAD || e->wagon != b->wagon) continue;
            int hw = ennemi_hbw(e);
            int hh = ennemi_hbh(e);
            SDL_Rect er = {(int)e->x, (int)e->y, hw, hh};
            if (SDL_HasIntersection(&br, &er)) {
                int is_boss = ennemi_is_boss(e);
                int dmg = SHOOT_DAMAGE;
                e->hp -= dmg;
                e->hitFlashMs = 120;
                b->actif = 0;
                if (is_boss) boss_ai_on_hit();
                {
                    DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                                         ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
                    spawn_damage_number(e->x + hw/2.0f, e->y + hh/3.0f,
                                        dmg, t);
                }
                {
                    EffectsSystem *efx = effects_global();
                    int dirSign = (b->velX >= 0.0f) ? 1 : -1;
                    if (efx) {
                        effects_on_hit(efx, EFX_ENT_ENEMY, j, dirSign);
                        effects_spawn_impact(efx, e->x + hw/2.0f,
                                                  e->y + hh/3.0f,
                                                  dirSign, dmg);
                    }
                    /* Player attack → contact hitstop. */
                    hitstop_trigger(dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
                }
                if (e->hp <= 0) {
                    EffectsSystem *efx = effects_global();
                    int dirSign = (b->velX >= 0.0f) ? 1 : -1;
                    if (efx) effects_spawn_death_for_enemy(efx, e, dirSign);
                    /* Kill-tier hitstop — longer than the per-hit pulse. */
                    hitstop_trigger(0.12f);
                    int nf = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                             (e->type == ET_GUARD)    ? GUARD_NFRAMES[2]    :
                                                        WORKER_NFRAMES[2];
                    e->etat = ES_DEAD;
                    animation_set_row(&e->anim, ANIM_E_DEATH, nf,
                                      ENEMY_ANIM_FPS[ANIM_E_DEATH], 0);
                }
                break;
            }
        }
    }
}

void balles_update_fx(Balle *balles, int *nb,
                       Ennemi *ennemis, int *nbEnnemis,
                       ParticleField *fx, HitEffect *hfx, int *nbHfx,
                       SDL_Texture *hitFxSheet)
{
    for (int i = 0; i < *nb; i++) {
        Balle *b = &balles[i];
        if (!b->actif) continue;

        b->x += b->velX;
        if (b->x < WAGON_MUR_G_X || b->x > WAGON_MUR_D_X) { b->actif = 0; continue; }

        SDL_Rect br = {(int)b->x, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
        for (int j = 0; j < *nbEnnemis; j++) {
            Ennemi *e = &ennemis[j];
            if (!e->actif || e->etat == ES_DEAD || e->wagon != b->wagon) continue;
            int hw = ennemi_hbw(e);
            int hh = ennemi_hbh(e);
            SDL_Rect er = {(int)e->x, (int)e->y, hw, hh};
            if (SDL_HasIntersection(&br, &er)) {
                int is_boss = ennemi_is_boss(e);
                int dmg = SHOOT_DAMAGE;
                e->hp -= dmg;
                e->hitFlashMs = 120;
                b->actif = 0;
                if (is_boss) boss_ai_on_hit();
                {
                    DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                                         ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
                    spawn_damage_number(e->x + hw/2.0f, e->y + hh/3.0f,
                                        dmg, t);
                }
                {
                    EffectsSystem *efx = effects_global();
                    int dirSign = (b->velX >= 0.0f) ? 1 : -1;
                    if (efx) {
                        effects_on_hit(efx, EFX_ENT_ENEMY, j, dirSign);
                        effects_spawn_impact(efx, e->x + hw/2.0f,
                                                  e->y + hh/3.0f,
                                                  dirSign, dmg);
                    }
                    hitstop_trigger(dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
                }
                if (fx) particles_burst(fx, e->x + hw/2.0f, e->y + hh/2.0f);
                if (hfx && nbHfx)
                    hitfx_spawn(hfx, nbHfx, e->x + hw/2.0f, e->y + hh/2.0f, hitFxSheet);
                if (e->hp <= 0) {
                    EffectsSystem *efx = effects_global();
                    int dirSign = (b->velX >= 0.0f) ? 1 : -1;
                    if (efx) effects_spawn_death_for_enemy(efx, e, dirSign);
                    hitstop_trigger(0.12f);
                    int nf = (e->type == ET_ENFORCER) ? ENFORCER_NFRAMES[2] :
                             (e->type == ET_GUARD)    ? GUARD_NFRAMES[2]    :
                                                        WORKER_NFRAMES[2];
                    e->etat = ES_DEAD;
                    animation_set_row(&e->anim, ANIM_E_DEATH, nf,
                                      ENEMY_ANIM_FPS[ANIM_E_DEATH], 0);
                }
                break;
            }
        }
    }
}

void balles_afficher(Balle *balles, int nb, SDL_Renderer *r, SDL_Texture *bulletTex, int ox)
{
    for (int i = 0; i < nb; i++) {
        Balle *b = &balles[i];
        if (!b->actif) continue;
        SDL_Rect dst = {(int)b->x + ox, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
        if (bulletTex) {
            SDL_RendererFlip flip = (b->velX < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(r, bulletTex, NULL, &dst, 0, NULL, flip);
        } else {
            SDL_SetRenderDrawColor(r, 255, 200, 50, 255);
            SDL_RenderFillRect(r, &dst);
        }
    }
}

void hitfx_spawn(HitEffect *fx, int *nbFx, float x, float y, SDL_Texture *sheet)
{
    for (int i = 0; i < MAX_HIT_FX; i++) {
        if (!fx[i].actif) {
            fx[i].actif = 1;
            fx[i].x     = x;
            fx[i].y     = y;
            animation_init(&fx[i].anim, sheet, HITFX_COLS, HITFX_ROWS,
                           0, 4, 15.0f, 0);
            if (*nbFx <= i) *nbFx = i + 1;
            return;
        }
    }
}

void hitfx_update(HitEffect *fx, int *nb, float dt)
{
    for (int i = 0; i < *nb; i++) {
        if (!fx[i].actif) continue;
        animation_update(&fx[i].anim, dt);
        if (fx[i].anim.finished) fx[i].actif = 0;
    }
}

void hitfx_afficher(HitEffect *fx, int nb, SDL_Renderer *r, int ox)
{
    for (int i = 0; i < nb; i++) {
        if (!fx[i].actif) continue;
        SDL_Rect src = animation_get_src_rect(&fx[i].anim);
        SDL_Rect dst = {(int)fx[i].x + ox - HITFX_DRAW_W/2,
                        (int)fx[i].y - HITFX_DRAW_H/2,
                        HITFX_DRAW_W, HITFX_DRAW_H};
        if (fx[i].anim.texture) {
            SDL_RenderCopy(r, fx[i].anim.texture, &src, &dst);
        } else {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 255, 255, 100, 180);
            SDL_RenderFillRect(r, &dst);
        }
    }
}

int ennemi_is_boss(const Ennemi *e)
{
    return e && e->type == ET_ENFORCER && e->wagon == MAX_WAGONS - 1;
}
int ennemi_hbw(const Ennemi *e)
{
    if (!e) return 0;
    if (e->type == ET_GUARD)  return GUARD_HBW;
    if (e->type == ET_WORKER) return WORKER_HBW;
    return ennemi_is_boss(e) ? BOSS_HBW : ENFORCER_HBW;
}
int ennemi_hbh(const Ennemi *e)
{
    if (!e) return 0;
    if (e->type == ET_GUARD)  return GUARD_HBH;
    if (e->type == ET_WORKER) return WORKER_HBH;
    return ennemi_is_boss(e) ? BOSS_HBH : ENFORCER_HBH;
}

static int   enemy_hbw(TypeEnnemi t){ return t==ET_GUARD?GUARD_HBW:t==ET_WORKER?WORKER_HBW:ENFORCER_HBW; }
static int   enemy_hbh(TypeEnnemi t){ return t==ET_GUARD?GUARD_HBH:t==ET_WORKER?WORKER_HBH:ENFORCER_HBH; }
static int   enemy_dw(TypeEnnemi t) { return t==ET_GUARD?GUARD_DRAW_W:t==ET_WORKER?WORKER_DRAW_W:ENFORCER_DRAW_W; }
static int   enemy_dh(TypeEnnemi t) { return t==ET_GUARD?GUARD_DRAW_H:t==ET_WORKER?WORKER_DRAW_H:ENFORCER_DRAW_H; }
static int   enemy_nwalk(TypeEnnemi t){ return t==ET_GUARD?GUARD_NFRAMES[0]:t==ET_WORKER?WORKER_NFRAMES[0]:ENFORCER_NFRAMES[0]; }
static int   enemy_natk(TypeEnnemi t) { return t==ET_GUARD?GUARD_NFRAMES[1]:t==ET_WORKER?WORKER_NFRAMES[1]:ENFORCER_NFRAMES[1]; }
static int   enemy_ndth(TypeEnnemi t) { return t==ET_GUARD?GUARD_NFRAMES[2]:t==ET_WORKER?WORKER_NFRAMES[2]:ENFORCER_NFRAMES[2]; }
static int   enemy_dmg(TypeEnnemi t)  {
    int base = t==ET_GUARD?GUARD_DMG:t==ET_WORKER?WORKER_DMG:ENFORCER_DMG;
    return (int)((float)base * 1.25f);
}
static float enemy_spd(TypeEnnemi t)  {
    float base = t==ET_GUARD?1.8f:t==ET_WORKER?2.2f:1.2f;
    return base * 1.20f;
}
static int   enemy_rng(TypeEnnemi t)  { return t==ET_GUARD?55:t==ET_WORKER?45:70; }
static int   enemy_maxhp(TypeEnnemi t){ return t==ET_GUARD?GUARD_HP:t==ET_WORKER?WORKER_HP:ENFORCER_HP; }
static int   enemy_cols(TypeEnnemi t) { (void)t; return 6; }
static int   enemy_rows(TypeEnnemi t) { (void)t; return 3; }

void ennemis_spawn(Ennemi *ennemis, int *nb, TypeEnnemi type,
                   float x, float y, int wagon, SDL_Texture *sheet)
{
    if (*nb >= MAX_ENNEMIS) return;
    Ennemi *e = &ennemis[*nb];
    e->x              = x;
    e->y              = y;
    e->velY           = 0.0f;
    e->auSol          = 0;
    e->actif          = 1;
    e->hp             = enemy_maxhp(type);
    e->type           = type;
    e->etat           = ES_WALK;
    e->direction      = -1;
    e->wagon          = wagon;
    e->hitFlashMs     = 0;
    e->lastAttackTick = 0;
    e->attackCooldownMs = (type == ET_ENFORCER) ? 2000u :
                          (type == ET_GUARD)    ? 1400u : 1100u;

    animation_init(&e->anim, sheet,
                   enemy_cols(type), enemy_rows(type),
                   ANIM_E_WALK, enemy_nwalk(type),
                   ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
    (*nb)++;
}

void ennemis_update(Ennemi *ennemis, int *nb, int wagonActif, float dt,
                    Joueur *joueurs, int nbJoueurs,
                    int wagonSolY, int wagonPlafY)
{
    Uint32 now = SDL_GetTicks();

    EffectsSystem *efx = effects_global();
    for (int i = 0; i < *nb; i++) {
        Ennemi *e = &ennemis[i];
        if (!e->actif || e->wagon != wagonActif) continue;

        animation_update(&e->anim, dt);
        if (e->hitFlashMs > 0) e->hitFlashMs -= (int)(dt * 1000.0f);

        /* Final boss has its own dedicated AI (boss_ai.c).  We still tick
         * the animation update above (so the boss's hit-flash decays) but
         * skip the generic chase/attack logic entirely. */
        if (e->type == ET_ENFORCER && e->wagon == MAX_WAGONS - 1)
            continue;

        /* 200ms stun gate after being hit -- skip the AI so the enemy
         * doesn't immediately resume attacking through our knockback. */
        if (e->etat != ES_DEAD &&
            effects_is_stunned(efx, EFX_ENT_ENEMY, i)) {
            /* still apply gravity so a mid-air stunned enemy falls */
            if (!e->auSol) {
                e->velY += GRAVITE_JEU;
                e->y    += e->velY;
                int hh_local = (e->type == ET_GUARD)  ? GUARD_HBH  :
                               (e->type == ET_WORKER) ? WORKER_HBH : ENFORCER_HBH;
                if (e->y + hh_local >= wagonSolY) {
                    e->y = (float)(wagonSolY - hh_local);
                    e->velY = 0; e->auSol = 1;
                }
            }
            continue;
        }

        if (e->etat == ES_DEAD) {
            if (!e->auSol) {
                e->velY += GRAVITE_JEU;
                e->y    += e->velY;
                if (e->y + enemy_hbh(e->type) >= wagonSolY) {
                    e->y = (float)(wagonSolY - enemy_hbh(e->type));
                    e->auSol = 1; e->velY = 0;
                }
            }
            continue;
        }

        e->velY += GRAVITE_JEU;
        e->y    += e->velY;
        e->auSol = 0;
        if (e->y + enemy_hbh(e->type) >= wagonSolY) {
            e->y = (float)(wagonSolY - enemy_hbh(e->type));
            e->velY = 0; e->auSol = 1;
        }
        if (e->y < (float)wagonPlafY) { e->y = (float)wagonPlafY; e->velY = 0; }

        int   targetIdx = -1;
        float minDist   = 1e9f;
        for (int p = 0; p < nbJoueurs; p++) {
            if (!joueurs[p].actif || joueurs[p].hp <= 0) continue;
            float d = joueurs[p].x - e->x;
            if (d < 0) d = -d;
            if (d < minDist) { minDist = d; targetIdx = p; }
        }

        if (targetIdx < 0) {
            animation_set_row(&e->anim, ANIM_E_WALK,
                              enemy_nwalk(e->type), ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
            continue;
        }

        Joueur *tgt  = &joueurs[targetIdx];
        float dx     = tgt->x - e->x;
        float dist   = dx < 0 ? -dx : dx;
        int   rng    = enemy_rng(e->type);

        if (e->etat == ES_ATTACK) {
            if (e->anim.finished) {

                SDL_Rect jr = {(int)tgt->x, (int)tgt->y, tgt->w, tgt->h};
                SDL_Rect er = {(int)e->x - 10, (int)e->y,
                               enemy_hbw(e->type) + 20, enemy_hbh(e->type)};
                if (SDL_HasIntersection(&jr, &er) && tgt->etat != PS_DEAD) {
                    int _dmg = enemy_dmg(e->type);
                    tgt->hp -= _dmg;
                    if (tgt->hp < 0) tgt->hp = 0;
                    spawn_damage_number(tgt->x + tgt->w/2.0f,
                                        tgt->y + tgt->h/3.0f,
                                        _dmg, DMG_NUM_PLAYER);
                    EtatJoueur next = (tgt->hp == 0) ? PS_DEAD : PS_HIT;
                    if (tgt->etat != PS_HIT && tgt->etat != PS_DEAD) {
                        animation_set_row(&tgt->anim, ANIM_P_HIT,
                                          PLAYER_NFRAMES[ANIM_P_HIT],
                                          PLAYER_ANIM_FPS[ANIM_P_HIT], 0);
                        tgt->etat = next;
                        char_anim_set(&tgt->ca,
                            next == PS_DEAD ? ANIM_DEATH : ANIM_HIT_REACT);
                    }
                    /* push the player away from the enemy that hit them */
                    {
                        EffectsSystem *efx = effects_global();
                        int dirSign = (tgt->x >= e->x) ? 1 : -1;
                        if (efx) {
                            effects_on_hit(efx, EFX_ENT_PLAYER, targetIdx, dirSign);
                            effects_spawn_impact(efx, tgt->x + tgt->w/2.0f,
                                                      tgt->y + tgt->h/3.0f,
                                                      dirSign, _dmg);
                        }
                    }
                }
                e->etat = ES_WALK;
                animation_set_row(&e->anim, ANIM_E_WALK,
                                  enemy_nwalk(e->type),
                                  ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
                e->lastAttackTick = now;
            }
            continue;
        }

        e->direction = (dx >= 0) ? 1 : -1;
        if (dist > 300.0f) {
            animation_set_row(&e->anim, ANIM_E_WALK,
                              enemy_nwalk(e->type), ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
        } else if (dist > (float)rng) {
            float spd = enemy_spd(e->type);
            /* The final boss is skipped above; the speed-mult call from
             * the old AI has been removed (boss_ai handles its own movement). */
            e->x += (dx > 0 ? 1.0f : -1.0f) * spd;
            if (e->x < WAGON_MUR_G_X) e->x = (float)WAGON_MUR_G_X;
            if (e->x + enemy_hbw(e->type) > WAGON_MUR_D_X)
                e->x = (float)(WAGON_MUR_D_X - enemy_hbw(e->type));
            animation_set_row(&e->anim, ANIM_E_WALK,
                              enemy_nwalk(e->type), ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
        } else {
            if ((int)(now - e->lastAttackTick) >= 800) {
                e->etat = ES_ATTACK;
                animation_set_row(&e->anim, ANIM_E_ATTACK,
                                  enemy_natk(e->type), ENEMY_ANIM_FPS[ANIM_E_ATTACK], 0);
            }
        }
    }
}

void ennemis_afficher(Ennemi *ennemis, int nb, int wagonActif, SDL_Renderer *r, int ox)
{
    for (int i = 0; i < nb; i++) {
        Ennemi *e = &ennemis[i];
        if (!e->actif || e->wagon != wagonActif) continue;

        /* Skip the final-boss enforcer: it is rendered by boss_anim_render
         * from the caller (background.c) using the dedicated 7-row sprite
         * sheet, and its HP is displayed in the boss HUD — so the default
         * floating HP bar would also be redundant.  The boss is the unique
         * ET_ENFORCER in the last wagon (background.c only spawns one). */
        if (e->type == ET_ENFORCER && e->wagon == MAX_WAGONS - 1)
            continue;

        SDL_Rect src = animation_get_src_rect(&e->anim);
        int dw = enemy_dw(e->type);
        int dh = enemy_dh(e->type);
        int hw = enemy_hbw(e->type);
        int hh = enemy_hbh(e->type);

        SDL_Rect dst = {(int)e->x + ox + hw/2 - dw/2,
                        (int)e->y + hh - dh,
                        dw, dh};

        if (e->anim.texture) {
            if (e->hitFlashMs > 0)
                SDL_SetTextureColorMod(e->anim.texture, 255, 80, 80);

            SDL_RendererFlip flip = (e->direction == -1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(r, e->anim.texture, &src, &dst, 0, NULL, flip);

            if (e->hitFlashMs > 0)
                SDL_SetTextureColorMod(e->anim.texture, 255, 255, 255);
        } else {
            SDL_SetRenderDrawColor(r, 180, 30, 30, 255);
            SDL_Rect hb = {(int)e->x, (int)e->y, hw, hh};
            SDL_RenderFillRect(r, &hb);
        }

        if (e->etat != ES_DEAD) {
            int maxhp  = enemy_maxhp(e->type);
            int filled = (e->hp * dw) / maxhp;
            SDL_Rect bgBar = {dst.x, dst.y - 6, dw, 4};
            SDL_Rect fgBar = {dst.x, dst.y - 6, filled, 4};
            SDL_SetRenderDrawColor(r, 60, 20, 20, 255);
            SDL_RenderFillRect(r, &bgBar);
            SDL_SetRenderDrawColor(r, 220, 50, 50, 255);
            SDL_RenderFillRect(r, &fgBar);
        }
    }
}

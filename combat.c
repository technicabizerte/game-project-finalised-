#include "combat.h"
#include "background.h"
#include <stdio.h>

/* ═══════════════════════════════════════════
   ANIMATION SYSTEM
   ═══════════════════════════════════════════ */

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

/* ═══════════════════════════════════════════
   TEXTURE LOADING
   ═══════════════════════════════════════════ */

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

/* ═══════════════════════════════════════════
   BULLETS
   ═══════════════════════════════════════════ */

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
        if (b->x < -20 || b->x > W_ECRAN + 20) { b->actif = 0; continue; }

        SDL_Rect br = {(int)b->x, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
        for (int j = 0; j < *nbEnnemis; j++) {
            Ennemi *e = &ennemis[j];
            if (!e->actif || e->etat == ES_DEAD) continue;
            int hw = (e->type == ET_GUARD) ? GUARD_HBW  :
                     (e->type == ET_WORKER)? WORKER_HBW : ENFORCER_HBW;
            int hh = (e->type == ET_GUARD) ? GUARD_HBH  :
                     (e->type == ET_WORKER)? WORKER_HBH : ENFORCER_HBH;
            SDL_Rect er = {(int)e->x, (int)e->y, hw, hh};
            if (SDL_HasIntersection(&br, &er)) {
                e->hp -= SHOOT_DAMAGE;
                e->hitFlashMs = 120;
                b->actif = 0;
                if (e->hp <= 0) {
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

void balles_afficher(Balle *balles, int nb, SDL_Renderer *r, SDL_Texture *bulletTex)
{
    for (int i = 0; i < nb; i++) {
        Balle *b = &balles[i];
        if (!b->actif) continue;
        SDL_Rect dst = {(int)b->x, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
        if (bulletTex) {
            SDL_RendererFlip flip = (b->velX < 0) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(r, bulletTex, NULL, &dst, 0, NULL, flip);
        } else {
            SDL_SetRenderDrawColor(r, 255, 200, 50, 255);
            SDL_RenderFillRect(r, &dst);
        }
    }
}

/* ═══════════════════════════════════════════
   HIT EFFECTS
   ═══════════════════════════════════════════ */

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

void hitfx_afficher(HitEffect *fx, int nb, SDL_Renderer *r)
{
    for (int i = 0; i < nb; i++) {
        if (!fx[i].actif) continue;
        SDL_Rect src = animation_get_src_rect(&fx[i].anim);
        SDL_Rect dst = {(int)fx[i].x - HITFX_DRAW_W/2,
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

/* ═══════════════════════════════════════════
   ENEMIES
   ═══════════════════════════════════════════ */

static int   enemy_hbw(TypeEnnemi t){ return t==ET_GUARD?GUARD_HBW:t==ET_WORKER?WORKER_HBW:ENFORCER_HBW; }
static int   enemy_hbh(TypeEnnemi t){ return t==ET_GUARD?GUARD_HBH:t==ET_WORKER?WORKER_HBH:ENFORCER_HBH; }
static int   enemy_dw(TypeEnnemi t) { return t==ET_GUARD?GUARD_DRAW_W:t==ET_WORKER?WORKER_DRAW_W:ENFORCER_DRAW_W; }
static int   enemy_dh(TypeEnnemi t) { return t==ET_GUARD?GUARD_DRAW_H:t==ET_WORKER?WORKER_DRAW_H:ENFORCER_DRAW_H; }
static int   enemy_nwalk(TypeEnnemi t){ return t==ET_GUARD?GUARD_NFRAMES[0]:t==ET_WORKER?WORKER_NFRAMES[0]:ENFORCER_NFRAMES[0]; }
static int   enemy_natk(TypeEnnemi t) { return t==ET_GUARD?GUARD_NFRAMES[1]:t==ET_WORKER?WORKER_NFRAMES[1]:ENFORCER_NFRAMES[1]; }
static int   enemy_ndth(TypeEnnemi t) { return t==ET_GUARD?GUARD_NFRAMES[2]:t==ET_WORKER?WORKER_NFRAMES[2]:ENFORCER_NFRAMES[2]; }
static int   enemy_dmg(TypeEnnemi t)  { return t==ET_GUARD?GUARD_DMG:t==ET_WORKER?WORKER_DMG:ENFORCER_DMG; }
static float enemy_spd(TypeEnnemi t)  { return t==ET_GUARD?1.8f:t==ET_WORKER?2.2f:1.2f; }
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
                    float joueurX, float joueurY, int joueurW, int joueurH,
                    int *joueurHp, EtatJoueur *joueurEtat, AnimationSystem *joueurAnim,
                    int wagonSolY, int wagonPlafY)
{
    Uint32 now = SDL_GetTicks();

    for (int i = 0; i < *nb; i++) {
        Ennemi *e = &ennemis[i];
        if (!e->actif || e->wagon != wagonActif) continue;

        animation_update(&e->anim, dt);
        if (e->hitFlashMs > 0) e->hitFlashMs -= (int)(dt * 1000.0f);

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

        /* Gravity */
        e->velY += GRAVITE_JEU;
        e->y    += e->velY;
        e->auSol = 0;
        if (e->y + enemy_hbh(e->type) >= wagonSolY) {
            e->y = (float)(wagonSolY - enemy_hbh(e->type));
            e->velY = 0; e->auSol = 1;
        }
        if (e->y < (float)wagonPlafY) { e->y = (float)wagonPlafY; e->velY = 0; }

        float dx   = joueurX - e->x;
        float dist = dx < 0 ? -dx : dx;
        int   rng  = enemy_rng(e->type);

        if (e->etat == ES_ATTACK) {
            if (e->anim.finished) {
                /* Deal damage if still in range */
                SDL_Rect jr = {(int)joueurX, (int)joueurY, joueurW, joueurH};
                SDL_Rect er = {(int)e->x - 10, (int)e->y,
                               enemy_hbw(e->type) + 20, enemy_hbh(e->type)};
                if (SDL_HasIntersection(&jr, &er) && *joueurEtat != PS_DEAD) {
                    *joueurHp -= enemy_dmg(e->type);
                    if (*joueurHp < 0) *joueurHp = 0;
                    EtatJoueur next = (*joueurHp == 0) ? PS_DEAD : PS_HIT;
                    if (*joueurEtat != PS_HIT && *joueurEtat != PS_DEAD) {
                        animation_set_row(joueurAnim, ANIM_P_HIT,
                                          PLAYER_NFRAMES[ANIM_P_HIT],
                                          PLAYER_ANIM_FPS[ANIM_P_HIT], 0);
                        *joueurEtat = next;
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

        /* Walk toward player */
        e->direction = (dx >= 0) ? 1 : -1;
        if (dist > (float)rng) {
            e->x += (dx > 0 ? 1.0f : -1.0f) * enemy_spd(e->type);
            if (e->x < WAGON_MUR_G_X) e->x = (float)WAGON_MUR_G_X;
            if (e->x + enemy_hbw(e->type) > WAGON_MUR_D_X)
                e->x = (float)(WAGON_MUR_D_X - enemy_hbw(e->type));
            animation_set_row(&e->anim, ANIM_E_WALK,
                              enemy_nwalk(e->type), ENEMY_ANIM_FPS[ANIM_E_WALK], 1);
        } else {
            if ((int)(now - e->lastAttackTick) >= (int)e->attackCooldownMs) {
                e->etat = ES_ATTACK;
                animation_set_row(&e->anim, ANIM_E_ATTACK,
                                  enemy_natk(e->type), ENEMY_ANIM_FPS[ANIM_E_ATTACK], 0);
            }
        }
    }
}

void ennemis_afficher(Ennemi *ennemis, int nb, int wagonActif, SDL_Renderer *r)
{
    for (int i = 0; i < nb; i++) {
        Ennemi *e = &ennemis[i];
        if (!e->actif || e->wagon != wagonActif) continue;

        SDL_Rect src = animation_get_src_rect(&e->anim);
        int dw = enemy_dw(e->type);
        int dh = enemy_dh(e->type);
        int hw = enemy_hbw(e->type);
        int hh = enemy_hbh(e->type);

        /* Center sprite over hitbox, align bottom */
        SDL_Rect dst = {(int)e->x + hw/2 - dw/2,
                        (int)e->y + hh - dh,
                        dw, dh};

        if (e->anim.texture) {
            if (e->hitFlashMs > 0)
                SDL_SetTextureColorMod(e->anim.texture, 255, 80, 80);

            SDL_RendererFlip flip = (e->direction == 1) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderCopyEx(r, e->anim.texture, &src, &dst, 0, NULL, flip);

            if (e->hitFlashMs > 0)
                SDL_SetTextureColorMod(e->anim.texture, 255, 255, 255);
        } else {
            SDL_SetRenderDrawColor(r, 180, 30, 30, 255);
            SDL_Rect hb = {(int)e->x, (int)e->y, hw, hh};
            SDL_RenderFillRect(r, &hb);
        }

        /* Health bar */
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

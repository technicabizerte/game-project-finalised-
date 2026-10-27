#include "versus.h"
#include "two_player.h"
#include "char_anim.h"
#include "mode_select.h"
#include "combat.h"
#include "damage_numbers.h"
#include "effects.h"
#include "gameplay.h"
#include "boss_hud.h"               /* hitstop_trigger on PvP attacks    */
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>

#define VS_INTRO_SEC      1.0f
#define VS_ROUND_END_SEC  2.0f

/* One-hit-per-dash gate.  Without this the intersect check fires every
 * frame the rects overlap (~10 frames over a 200 ms dash @ 60 fps),
 * which stacked damage and made the dash feel broken.  Reset on each
 * fresh dash trigger, set on the first connecting hit.                */
static int s_dash_dmg_used[MAX_PLAYERS] = {0, 0};

/* Arena background — fit-to-width whole-image draw of the engine room
 * art.  Loaded once on the first begin_match and retained for the rest
 * of the session.  The image is wider than it is tall (1952×544), so a
 * width-fit leaves padding above/below — Brawlhalla-style fixed view.  */
static SDL_Texture *s_arena_bg     = NULL;
static int          s_arena_bg_w   = 0;
static int          s_arena_bg_h   = 0;
static SDL_Renderer *s_arena_owner = NULL;


static void versus_load_assets(SDL_Renderer *r)
{
    if (s_arena_bg && s_arena_owner == r) return;
    if (s_arena_bg) { SDL_DestroyTexture(s_arena_bg); s_arena_bg = NULL; }

    SDL_Surface *surf = IMG_Load("assets/arena/versus_arena.png");
    if (!surf) {
        fprintf(stderr, "[versus] arena bg not found: %s\n", IMG_GetError());
        s_arena_owner = r;
        return;
    }
    s_arena_bg_w = surf->w;
    s_arena_bg_h = surf->h;
    s_arena_bg = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    s_arena_owner = r;
}

/* ─── Phase helpers ───────────────────────────────────────────────── */

static int versus_target_wins(void)
{
#if VERSUS_SINGLE_LIFE
    return 1;
#else
    return VERSUS_TARGET_WINS;
#endif
}

static void clear_bullets(Background *bg)
{
    for (int p = 0; p < MAX_PLAYERS; p++) {
        for (int i = 0; i < MAX_BALLES; i++) bg->balles[p][i].actif = 0;
        bg->nbBalles[p] = 0;
    }
}

static void position_for_round(Background *bg)
{
    Joueur *p1 = &bg->joueurs[0];
    Joueur *p2 = &bg->joueurs[1];

    /* Tighter spawn than the campaign defaults so both fighters frame
     * inside the engine-room art instead of hugging the side walls.   */
    const int spawn_inset = 180;

    p1->x = (float)(VS_WAGON_L + spawn_inset);
    p1->y = (float)(VERSUS_FLOOR_Y - JOUEUR_H);
    p1->velX = p1->velY = 0.0f;
    p1->direction = 1;
    p1->auSol     = 1;
    p1->etat      = PS_IDLE;
    p1->hp        = VERSUS_MAX_HP;
    p1->actif     = 1;
    p1->pendingMeleeImpact = 0;
    p1->pendingGunFire     = 0;
    p1->muzzleFrames       = 0;
    p1->lastShootTick      = 0;
    p1->lastMeleeTick      = 0;
    p1->ca.is_locked       = 0;
    char_anim_set(&p1->ca, ANIM_IDLE);

    p2->x = (float)(VS_WAGON_R - spawn_inset - JOUEUR_W);
    p2->y = (float)(VERSUS_FLOOR_Y - JOUEUR_H);
    p2->velX = p2->velY = 0.0f;
    p2->direction = -1;
    p2->auSol     = 1;
    p2->etat      = PS_IDLE;
    p2->hp        = VERSUS_MAX_HP;
    p2->actif     = 1;
    p2->pendingMeleeImpact = 0;
    p2->pendingGunFire     = 0;
    p2->muzzleFrames       = 0;
    p2->lastShootTick      = 0;
    p2->lastMeleeTick      = 0;
    p2->ca.is_locked       = 0;
    char_anim_set(&p2->ca, ANIM_IDLE);
}

void versus_begin_match(VersusState *vs, Background *bg, GameplayState *gp)
{
    memset(vs, 0, sizeof(*vs));
    vs->phase       = VS_PHASE_INTRO;
    vs->phase_timer = 0.0f;
    vs->round_idx   = 1;
    vs->last_winner = -1;
    vs->ui_hover    = -1;
    vs->ui_cursor   =  0;

    /* Arena: a single flat wagon, no doors, no enemies, no scrolling.
     * Suppress the campaign-only transition logic via blockTransition*
     * + g_game_mode (the latter is what background.c actually reads). */
    bg->nbJoueursActifs       = 2;
    bg->wagonActif            = 0;
    bg->blockTransitionRight  = 1;
    bg->blockTransitionLeft   = 1;
    bg->transitionSlide.actif = 0;
    bg->nbEnnemis             = 0;

    /* Clear platforms in the chosen wagon so the arena reads flat.    */
    bg->stylesWagons[bg->wagonActif].obstacles.nb = 0;

    if (gp) {
        gameplay_reset_for_run(gp, MODE_MULTI);
        /* Versus doesn't use the door/kronole loop. */
        gp->door.actif = 0;
        gp->door.state = DOOR_IDLE;
        /* Wipe any stale dash state / trails from a previous match.  */
        for (int p = 0; p < NB_JOUEURS_MAX; p++) {
            gp->dash[p].active = 0;
            gp->dashLastEndTick[p] = 0;
        }
        gp->nbTrails = 0;
        for (int p = 0; p < MAX_PLAYERS; p++) s_dash_dmg_used[p] = 0;
    }

    /* joueur_init_interieur runs the per-slot char_anim_load gate
     * (Prompt 10): sheets only reload if the player's pick changed. */
    joueur_init_interieur(bg);

    clear_bullets(bg);
    position_for_round(bg);
}

static void start_next_round(VersusState *vs, Background *bg)
{
    vs->round_idx++;
    vs->phase       = VS_PHASE_INTRO;
    vs->phase_timer = 0.0f;
    clear_bullets(bg);
    position_for_round(bg);
}

static void end_round(VersusState *vs, int winner)
{
    vs->phase       = VS_PHASE_ROUND_END;
    vs->phase_timer = 0.0f;
    vs->last_winner = winner;
    if (winner >= 0 && winner < 2) vs->score[winner]++;
}

/* ─── Sprite-sized hit rect (PvP combat) ─────────────────────────────
 * The physics hitbox (JOUEUR_W × JOUEUR_H = 38×63) is intentionally
 * small for forgiving campaign collisions, but in PvP the +60% sprite
 * is ~150×200 px — so two players visibly touching can be 100+ px
 * apart in physics-box terms and attacks miss.  This helper returns a
 * combat-only rect sized to match the visible sprite, foot-anchored
 * to the same floor line.                                            */
static SDL_Rect versus_hit_rect(const Joueur *j)
{
    int sh = (int)((float)PLAYER_DRAW_H * VERSUS_PLAYER_SCALE + 0.5f); /* ~200 */
    int sw = (int)((float)PLAYER_DRAW_W * VERSUS_PLAYER_SCALE + 0.5f); /* ~150 */
    int foot_pad = (int)((float)sh * CHAR_FOOT_PADDING_FRAC + 0.5f);   /* ~28  */
    int cx = (int)j->x + JOUEUR_W / 2;
    int fy = (int)j->y + JOUEUR_H;        /* floor line                 */
    SDL_Rect r = {
        cx - sw / 2,
        fy - sh + foot_pad,               /* sprite top (above boots)   */
        sw,
        sh - foot_pad                     /* exclude empty foot padding */
    };
    return r;
}

/* ─── Bullet PvP tick ────────────────────────────────────────────── */

static void apply_bullet_hit(Background *bg, Joueur *tgt,
                             int tgt_idx, int dir_sign)
{
    int dmg = SHOOT_DAMAGE;
    tgt->hp -= dmg;
    if (tgt->hp < 0) tgt->hp = 0;

    DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                         ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
    spawn_damage_number(tgt->x + JOUEUR_W / 2.0f,
                        tgt->y + JOUEUR_H / 3.0f,
                        dmg, t);
    hitfx_spawn(bg->hitFx, &bg->nbHitFx,
                tgt->x + JOUEUR_W / 2.0f,
                tgt->y + JOUEUR_H / 3.0f,
                bg->hitFxSheet);
    particles_burst(&bg->particles,
                    tgt->x + JOUEUR_W / 2.0f,
                    tgt->y + JOUEUR_H / 3.0f);
    EffectsSystem *efx = effects_global();
    if (efx) {
        effects_on_hit(efx, EFX_ENT_PLAYER, tgt_idx, dir_sign);
        effects_spawn_impact(efx,
                             tgt->x + JOUEUR_W / 2.0f,
                             tgt->y + JOUEUR_H / 3.0f,
                             dir_sign, dmg);
    }
    /* PvP attack → contact hitstop (kill = longer). */
    if (tgt->hp == 0) {
        hitstop_trigger(0.12f);
#if VERSUS_KO_FINISHER
        if (efx) effects_spawn_ko_finisher(efx,
                     tgt->x + JOUEUR_W / 2.0f,
                     tgt->y + JOUEUR_H / 2.0f, dir_sign);
#endif
    } else {
        hitstop_trigger(dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
    }

    char_anim_set(&tgt->ca,
        tgt->hp == 0 ? ANIM_DEATH : ANIM_HIT_REACT);
}

static void versus_bullets_step(Background *bg)
{
    for (int p = 0; p < 2; p++) {
        Balle *bullets = bg->balles[p];
        int   *nb      = &bg->nbBalles[p];
        int    other_idx = p ^ 1;
        Joueur *other  = &bg->joueurs[other_idx];

        for (int i = 0; i < *nb; i++) {
            Balle *b = &bullets[i];
            if (!b->actif) continue;

            b->x += b->velX;
            if (b->x < VS_WAGON_L || b->x > VS_WAGON_R) {
                b->actif = 0; continue;
            }
            if (!other->actif || other->hp <= 0) continue;

            SDL_Rect br = {(int)b->x, (int)b->y, BULLET_DRAW_W, BULLET_DRAW_H};
            SDL_Rect tr = versus_hit_rect(other);
            if (SDL_HasIntersection(&br, &tr)) {
                int dir = (b->velX > 0) ? 1 : -1;
                apply_bullet_hit(bg, other, other_idx, dir);
                b->actif = 0;
            }
        }
    }
}

/* ─── Melee PvP — called from background.c::apply_melee_impact when
 *     g_game_mode == MODE_VERSUS.                                    */
void versus_apply_melee(Background *bg, int swinger_idx)
{
    if (swinger_idx < 0 || swinger_idx >= MAX_PLAYERS) return;
    int other_idx = swinger_idx ^ 1;
    if (other_idx >= bg->nbJoueursActifs) return;

    Joueur *j   = &bg->joueurs[swinger_idx];
    Joueur *tgt = &bg->joueurs[other_idx];
    if (!tgt->actif || tgt->hp <= 0) return;

    const WeaponStats *ws =
        &CHAR_WEAPON_STATS[player_cfg[swinger_idx].weapon];

    /* Swing collision: build a rect that spans the swinger's body
     * plus melee_range of reach in the swing direction, then test
     * against the target's sprite-sized hit rect.  Two players
     * touching always register; melee_range adds extra arc.         */
    SDL_Rect sr   = versus_hit_rect(j);
    SDL_Rect trgt = versus_hit_rect(tgt);
    SDL_Rect swing;
    if (j->meleeImpactDir == 1) {
        swing.x = sr.x;
        swing.w = sr.w + ws->melee_range;
    } else {
        swing.x = sr.x - ws->melee_range;
        swing.w = sr.w + ws->melee_range;
    }
    swing.y = sr.y;
    swing.h = sr.h;
    if (!SDL_HasIntersection(&swing, &trgt)) return;

    int dmg = (int)((float)ws->melee_damage * 0.80f);
    tgt->hp -= dmg;
    if (tgt->hp < 0) tgt->hp = 0;

    DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                         ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
    spawn_damage_number(tgt->x + JOUEUR_W / 2.0f,
                        tgt->y + JOUEUR_H / 3.0f,
                        dmg, t);
    hitfx_spawn(bg->hitFx, &bg->nbHitFx,
                tgt->x + JOUEUR_W / 2.0f,
                tgt->y + JOUEUR_H / 3.0f,
                bg->hitFxSheet);
    particles_burst(&bg->particles,
                    tgt->x + JOUEUR_W / 2.0f,
                    tgt->y + JOUEUR_H / 3.0f);
    EffectsSystem *efx = effects_global();
    if (efx) {
        effects_on_hit(efx, EFX_ENT_PLAYER, other_idx,
                       j->meleeImpactDir);
        effects_spawn_impact(efx,
                             tgt->x + JOUEUR_W / 2.0f,
                             tgt->y + JOUEUR_H / 3.0f,
                             j->meleeImpactDir, dmg);
    }
    if (tgt->hp == 0) {
        hitstop_trigger(0.12f);
#if VERSUS_KO_FINISHER
        if (efx) effects_spawn_ko_finisher(efx,
                     tgt->x + JOUEUR_W / 2.0f,
                     tgt->y + JOUEUR_H / 2.0f, j->meleeImpactDir);
#endif
    } else {
        hitstop_trigger(dmg >= DMG_NUM_CRIT_THRESHOLD ? 0.08f : 0.05f);
    }

    char_anim_set(&tgt->ca,
        tgt->hp == 0 ? ANIM_DEATH : ANIM_HIT_REACT);
}

/* ─── Update loop ────────────────────────────────────────────────── */

void versus_update(VersusState *vs, Background *bg, GameplayState *gp,
                   float dt)
{
    vs->phase_timer += dt;

    if (vs->phase == VS_PHASE_FIGHT) {
        vs->fight_elapsed += dt;

        /* Drive both players via the centralised input table (Prompt 10). */
        joueur_update_interieur_idx(bg, 0,
            player_input[0].gauche, player_input[0].droite,
            player_input[0].sauter, player_input[0].shoot,
            player_input[0].melee, dt);
        joueur_update_interieur_idx(bg, 1,
            player_input[1].gauche, player_input[1].droite,
            player_input[1].sauter, player_input[1].shoot,
            player_input[1].melee, dt);

        /* Dash physics + trails + PvP contact damage.  Mirrors the
         * campaign apply_dash_movement / update_dash_trails pair.     */
        versus_step_dash(gp, bg, dt);

        versus_bullets_step(bg);

        /* Visual feedback systems — same set the campaign world tick
         * runs (damage numbers, knockback decay, hit-fx, particles).  */
        damage_numbers_update(dt);
        EffectsSystem *efx = effects_global();
        if (efx) effects_update(efx, dt);
        hitfx_update(bg->hitFx, &bg->nbHitFx, dt);
        particles_update(&bg->particles, dt);

#if VERSUS_ROUND_TIME_S > 0
        if (vs->fight_elapsed >= (float)VERSUS_ROUND_TIME_S) {
            int p1hp = bg->joueurs[0].hp;
            int p2hp = bg->joueurs[1].hp;
            int winner;
            if (p1hp == p2hp)      winner = -1;
            else                    winner = (p1hp > p2hp) ? 0 : 1;
            end_round(vs, winner);
            return;
        }
#endif

        int p1_dead = (bg->joueurs[0].hp <= 0);
        int p2_dead = (bg->joueurs[1].hp <= 0);
        if (p1_dead || p2_dead) {
            int winner;
            if (p1_dead && p2_dead) winner = -1;
            else if (p1_dead)        winner = 1;
            else                     winner = 0;
            end_round(vs, winner);
        }
        return;
    }

    if (vs->phase == VS_PHASE_INTRO) {
        if (vs->phase_timer >= VS_INTRO_SEC) {
            vs->phase         = VS_PHASE_FIGHT;
            vs->phase_timer   = 0.0f;
            vs->fight_elapsed = 0.0f;
        }
        return;
    }

    if (vs->phase == VS_PHASE_ROUND_END) {
        if (vs->phase_timer >= VS_ROUND_END_SEC) {
            int won_match = 0;
            int N = versus_target_wins();
            if (vs->last_winner >= 0 && vs->score[vs->last_winner] >= N)
                won_match = 1;

            if (won_match) {
                vs->phase       = VS_PHASE_MATCH_END;
                vs->phase_timer = 0.0f;
                vs->ui_cursor   = 0;   /* land on REMATCH by default */
                vs->ui_hover    = -1;
            } else {
                start_next_round(vs, bg);
            }
        }
        return;
    }

    /* VS_PHASE_MATCH_END: idle until the user clicks Rematch or Menu. */
}

/* ─── Scaled render helper ───────────────────────────────────────── */

static void versus_render_player(SDL_Renderer *r, Joueur *j)
{
    if (!j || !j->actif) return;
    SDL_RendererFlip flip = (j->direction == -1) ? SDL_FLIP_HORIZONTAL
                                                 : SDL_FLIP_NONE;
    int foot_x = (int)j->x + JOUEUR_W / 2;
    int foot_y = (int)j->y + JOUEUR_H;
    int scaled_h = (int)((float)PLAYER_DRAW_H * VERSUS_PLAYER_SCALE + 0.5f);

    if (j->ca.loaded)
        char_anim_render(&j->ca, r, foot_x, foot_y, scaled_h, flip);
}

/* ─── Dash (PvP) ─────────────────────────────────────────────────── */

void versus_handle_dash_input(GameplayState *gp, Background *bg, int idx)
{
    if (!gp || !bg) return;
    if (idx < 0 || idx >= MAX_PLAYERS) return;
    Joueur *j = &bg->joueurs[idx];
    if (!j->actif || j->hp <= 0) return;
    if (gp->dash[idx].active) return;
    Uint32 now = SDL_GetTicks();
    if (now - gp->dashLastEndTick[idx] < DASH_COOLDOWN_MS &&
        gp->dashLastEndTick[idx] != 0) return;

    gp->dash[idx].active    = 1;
    gp->dash[idx].startTick = now;
    gp->dash[idx].direction = j->direction;
    s_dash_dmg_used[idx]    = 0;
}

static void versus_dash_pvp_contact(Background *bg, int dasher_idx)
{
    if (s_dash_dmg_used[dasher_idx]) return;     /* already hit once */
    int other_idx = dasher_idx ^ 1;
    Joueur *j   = &bg->joueurs[dasher_idx];
    Joueur *tgt = &bg->joueurs[other_idx];
    if (!tgt->actif || tgt->hp <= 0) return;

    SDL_Rect jr = versus_hit_rect(j);
    SDL_Rect tr = versus_hit_rect(tgt);
    if (!SDL_HasIntersection(&jr, &tr)) return;

    /* Single-tick PvP dash damage.  Lower than the campaign formula
     * (which scaled to enemy max HP) because dashing in close is a
     * mobility tool, not the main damage path.                       */
    int dmg = 6;
    tgt->hp -= dmg;
    if (tgt->hp < 0) tgt->hp = 0;
    s_dash_dmg_used[dasher_idx] = 1;

    DamageNumberType t = (dmg >= DMG_NUM_CRIT_THRESHOLD)
                         ? DMG_NUM_CRIT : DMG_NUM_NORMAL;
    spawn_damage_number(tgt->x + JOUEUR_W / 2.0f,
                        tgt->y + JOUEUR_H / 3.0f, dmg, t);
    hitfx_spawn(bg->hitFx, &bg->nbHitFx,
                tgt->x + JOUEUR_W / 2.0f,
                tgt->y + JOUEUR_H / 3.0f,
                bg->hitFxSheet);
    particles_burst(&bg->particles,
                    tgt->x + JOUEUR_W / 2.0f,
                    tgt->y + JOUEUR_H / 3.0f);
    EffectsSystem *efx = effects_global();
    int dirSign = (j->x < tgt->x) ? 1 : -1;
    if (efx) {
        effects_on_hit(efx, EFX_ENT_PLAYER, other_idx, dirSign);
        effects_spawn_impact(efx,
                             tgt->x + JOUEUR_W / 2.0f,
                             tgt->y + JOUEUR_H / 3.0f,
                             dirSign, dmg);
    }
    if (tgt->hp == 0) {
        hitstop_trigger(0.12f);
#if VERSUS_KO_FINISHER
        if (efx) effects_spawn_ko_finisher(efx,
                     tgt->x + JOUEUR_W / 2.0f,
                     tgt->y + JOUEUR_H / 2.0f, dirSign);
#endif
    } else {
        hitstop_trigger(0.05f);
    }

    char_anim_set(&tgt->ca,
        tgt->hp == 0 ? ANIM_DEATH : ANIM_HIT_REACT);
}

static void versus_step_dash_for(GameplayState *gp, Background *bg,
                                 int idx, float dt)
{
    if (!gp->dash[idx].active) return;
    Joueur *j = &bg->joueurs[idx];
    Uint32 now = SDL_GetTicks();
    if (now - gp->dash[idx].startTick >= DASH_DURATION_MS) {
        gp->dash[idx].active = 0;
        gp->dashLastEndTick[idx] = now;
        return;
    }
    float speed   = VITESSE_JOUEUR * DASH_SPEED_MULT;
    float advance = (float)gp->dash[idx].direction * speed * (dt * 60.0f);
    j->x += advance;
    j->velX = (float)gp->dash[idx].direction * speed;

    /* Versus arena uses full wagon width — no door padding clamps.    */
    float leftStop  = (float)VS_WAGON_L;
    float rightStop = (float)(VS_WAGON_R - JOUEUR_W);
    if (j->x < leftStop)  { j->x = leftStop;  j->velX = 0.0f; }
    if (j->x > rightStop) { j->x = rightStop; j->velX = 0.0f; }

    if (gp->nbTrails < MAX_DASH_TRAILS) {
        DashTrail *t = &gp->trails[gp->nbTrails++];
        t->x = j->x;
        t->y = j->y;
        t->alpha = 180;
        t->direction = j->direction;
        t->playerIdx = idx;
    }

    versus_dash_pvp_contact(bg, idx);
}

static void versus_step_dash_trails(GameplayState *gp, float dt)
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

void versus_step_dash(GameplayState *gp, Background *bg, float dt)
{
    if (!gp || !bg) return;
    for (int p = 0; p < MAX_PLAYERS; p++)
        versus_step_dash_for(gp, bg, p, dt);
    versus_step_dash_trails(gp, dt);
}

static void versus_render_dash_trails(GameplayState *gp, SDL_Renderer *r)
{
    if (!gp) return;

    /* Trail = compact ghost rect sized like the hitbox, centred on the
     * player's bottom centre.  The big +60% sprite would stack into a
     * wall of orange across the screen — keep the trail readable.    */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < gp->nbTrails; i++) {
        DashTrail *t = &gp->trails[i];
        SDL_Rect dst = { (int)t->x, (int)t->y, JOUEUR_W, JOUEUR_H };
        SDL_SetRenderDrawColor(r, 255, 200, 80,
            (Uint8)(t->alpha > 160 ? 160 : t->alpha));
        SDL_RenderFillRect(r, &dst);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* ─── World render ───────────────────────────────────────────────── */

void versus_render_world(VersusState *vs, SDL_Renderer *r, SDL_Window *win,
                         Background *bg, GameplayState *gp)
{
    (void)vs; (void)win;
    /* Two-pass rendering that decouples the BG from SDL's scaling math:
     *
     *   PASS 1 — BG at raw physical pixels.  Disable logical size, reset
     *            scale to 1.0, then draw the arena image directly into
     *            the actual render-output rect SDL reports.  Whatever
     *            number SDL reports IS the visible area — no scale, no
     *            letterbox, no float-rounding seam.  BG fills every
     *            single physical pixel.
     *
     *   PASS 2 — Players + HUD via SDL_RenderSetLogicalSize(1024, 576).
     *            SDL now stretches every subsequent draw call to fit a
     *            1024×576 logical canvas inside the physical area, with
     *            automatic letterboxing if the aspect doesn't match.
     *            BG was already drawn in pass 1, so any letterbox region
     *            shows the painted art underneath instead of black bars.
     *            SDL_RenderWindowToLogical (used by ui_mouse_logical for
     *            click hit-testing) reads the active logical-size + scale
     *            state, so clicks translate correctly into 1024×576
     *            coords where the buttons live.                          */

    versus_load_assets(r);

    /* PASS 1: raw physical BG ------------------------------------------- */
    SDL_RenderSetLogicalSize(r, 0, 0);   /* clears prior letterbox+scale */
    SDL_RenderSetScale(r, 1.0f, 1.0f);   /* belt + suspenders            */

    int pw = VS_CANVAS_W, ph = VS_CANVAS_H;
    SDL_GetRendererOutputSize(r, &pw, &ph);
    if (pw <= 0) pw = VS_CANVAS_W;
    if (ph <= 0) ph = VS_CANVAS_H;

    /* Diagnostic — print once so we can confirm SDL's reported sizes. */
    static int s_diag_printed = 0;
    if (!s_diag_printed) {
        int ww = 0, wh = 0;
        if (win) SDL_GetWindowSize(win, &ww, &wh);
        fprintf(stderr, "[versus] renderer_output=%dx%d  window_size=%dx%d\n",
                pw, ph, ww, wh);
        s_diag_printed = 1;
    }

    SDL_SetRenderDrawColor(r, 12, 8, 8, 255);
    SDL_Rect bg_phys = {0, 0, pw, ph};
    SDL_RenderFillRect(r, &bg_phys);
    if (s_arena_bg && s_arena_bg_w > 0 && s_arena_bg_h > 0) {
        SDL_RenderCopy(r, s_arena_bg, NULL, &bg_phys);
    }

    /* PASS 2: logical 1024×576 for everything else ---------------------- */
    SDL_RenderSetLogicalSize(r, VS_CANVAS_W, VS_CANVAS_H);

    /* Dash trails BEHIND the players so the live sprite reads on top
     * of the ghost trail.  Players + bullets + hit-fx + particles +
     * damage numbers.  We do NOT call wagonsInterieurs_afficher so the
     * starfield/snow path from the campaign wagon backdrop is skipped. */
    versus_render_dash_trails(gp, r);

    versus_render_player(r, &bg->joueurs[0]);
    if (bg->nbJoueursActifs >= 2)
        versus_render_player(r, &bg->joueurs[1]);

    balles_afficher(bg->balles[0], bg->nbBalles[0], r, bg->bulletTex, 0);
    balles_afficher(bg->balles[1], bg->nbBalles[1], r, bg->bulletTex, 0);
    hitfx_afficher (bg->hitFx,      bg->nbHitFx,    r, 0);
    particles_render(&bg->particles, r, 0);

    EffectsSystem *efx = effects_global();
    if (efx) {
        effects_render_hit_blink(efx, bg, r, 0);
        /* Slash arcs live inside effects_render_world (after sparks).
         * Call it here so PvP swings get the same VFX as campaign.   */
        effects_render_world(efx, r, 0);
    }

    damage_numbers_render(r, 0);
}

/* ─── HUD: HP bars + round pips + banner / match-end overlay ────── */

/* Button rects in the 1024×576 logical canvas — the per-frame stretch
 * scale set in versus_render_world maps these onto the physical view
 * port, and SDL_RenderWindowToLogical translates mouse events back
 * into the same logical space for hit-testing.                        */
static SDL_Rect rematch_rect(void) { return (SDL_Rect){VS_CANVAS_W/2 - 220, VS_CANVAS_H/2 + 40, 200, 60}; }
static SDL_Rect menu_rect   (void) { return (SDL_Rect){VS_CANVAS_W/2 +  20, VS_CANVAS_H/2 + 40, 200, 60}; }

void versus_render_hud(VersusState *vs, SDL_Renderer *r, SDL_Window *win,
                       UIFontPack *fonts, UITextCache *cache,
                       Background *bg)
{
    (void)win;

    SDL_Color amber  = {235, 180,  60, 255};
    SDL_Color cyan   = { 90, 200, 230, 255};
    SDL_Color barBG  = { 20,  20,  28, 255};
    SDL_Color jaune  = {255, 215,   0, 255};
    SDL_Color blanc  = {235, 235, 240, 255};
    SDL_Color hint   = {180, 190, 210, 230};

    /* HP bars — Prompt 10 widget.  P1 anchored to left edge, P2 to
     * right edge of the logical 1024×576 canvas (stretches to physical
     * via the renderer's scale).                                       */
    SDL_Rect p1Bar = { 15, 18, 220, 14 };
    SDL_Rect p2Bar = { VS_CANVAS_W - 235, 18, 220, 14 };
    two_player_render_hp_bar(r, p1Bar,
        bg->joueurs[0].hp, VERSUS_MAX_HP,
        amber, barBG, "P1", fonts, cache);
    two_player_render_hp_bar(r, p2Bar,
        bg->joueurs[1].hp, VERSUS_MAX_HP,
        cyan,  barBG, "P2", fonts, cache);

    /* Round pips: filled = won, outlined = remaining.  Centered. */
    int N = versus_target_wins();
    int pip = 14, gap = 6, sep = 36;
    int wpips_each = N * pip + (N - 1) * gap;
    int row_w  = wpips_each * 2 + sep;
    int row_x  = (VS_CANVAS_W - row_w) / 2;
    int row_y  = 20;

    for (int i = 0; i < N; i++) {
        SDL_Rect rp = { row_x + i * (pip + gap), row_y, pip, pip };
        SDL_SetRenderDrawColor(r, amber.r, amber.g, amber.b, 255);
        if (i < vs->score[0]) SDL_RenderFillRect(r, &rp);
        SDL_RenderDrawRect(r, &rp);
    }
    int p2_x = row_x + wpips_each + sep;
    for (int i = 0; i < N; i++) {
        SDL_Rect rp = { p2_x + i * (pip + gap), row_y, pip, pip };
        SDL_SetRenderDrawColor(r, cyan.r, cyan.g, cyan.b, 255);
        if (i < vs->score[1]) SDL_RenderFillRect(r, &rp);
        SDL_RenderDrawRect(r, &rp);
    }

    /* Phase overlays. */
    if (vs->phase == VS_PHASE_INTRO) {
        char buf[32];
        snprintf(buf, sizeof(buf), "ROUND %d", vs->round_idx);
        ui_draw_text_centered(cache, r, fonts->bebasTitle,
                              buf, jaune, VS_CANVAS_W/2, VS_CANVAS_H/2 - 30);
        if (vs->phase_timer > 0.65f)
            ui_draw_text_centered(cache, r, fonts->bebasHeader,
                                  "FIGHT!", blanc,
                                  VS_CANVAS_W/2, VS_CANVAS_H/2 + 20);
    } else if (vs->phase == VS_PHASE_ROUND_END) {
        char buf[48];
        if (vs->last_winner < 0)
            snprintf(buf, sizeof(buf), "DRAW");
        else
            snprintf(buf, sizeof(buf),
                     "PLAYER %d WINS!", vs->last_winner + 1);
        ui_draw_text_centered(cache, r, fonts->bebasTitle,
                              buf, jaune, VS_CANVAS_W/2, VS_CANVAS_H/2 - 30);
    } else if (vs->phase == VS_PHASE_MATCH_END) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
        SDL_Rect full = {0, 0, VS_CANVAS_W, VS_CANVAS_H};
        SDL_RenderFillRect(r, &full);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        int winner = (vs->score[0] > vs->score[1]) ? 0 : 1;
        char buf[64];
        snprintf(buf, sizeof(buf), "PLAYER %d WINS THE MATCH", winner + 1);
        ui_draw_text_centered(cache, r, fonts->bebasTitle, buf, jaune,
                              VS_CANVAS_W/2, 110);

        snprintf(buf, sizeof(buf), "%d  —  %d", vs->score[0], vs->score[1]);
        ui_draw_text_centered(cache, r, fonts->bebasHeader, buf, blanc,
                              VS_CANVAS_W/2, 190);

        SDL_Rect btnR = rematch_rect();
        SDL_Rect btnM = menu_rect();
        for (int b = 0; b < 2; b++) {
            SDL_Rect rect = (b == 0) ? btnR : btnM;
            int is_cur = (vs->ui_cursor == b);
            int is_hov = (vs->ui_hover  == b && !is_cur);
            SDL_Color col = is_cur ? jaune
                          : is_hov ? (SDL_Color){210, 215, 240, 255}
                                   : (SDL_Color){120, 125, 145, 255};
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            for (int t = 0; t < (is_cur ? 3 : 1); t++) {
                SDL_Rect bb = {rect.x - t, rect.y - t,
                               rect.w + 2*t, rect.h + 2*t};
                SDL_RenderDrawRect(r, &bb);
            }
            /* Center the label visually inside the rect.  ui_draw_text_
             * centered takes y as the TEXT TOP, so we offset by half the
             * font header height (~14 px) — keeps the visible label and
             * the click hit-rect aligned.                                */
            const char *label = (b == 0) ? "REMATCH" : "MENU";
            ui_draw_text_centered(cache, r, fonts->bebasHeader, label,
                                  col, rect.x + rect.w/2,
                                  rect.y + rect.h/2 - 14);
        }

        ui_draw_text_centered(cache, r, fonts->mono,
            "[<-/->] Naviguer  [ENTREE] Valider  [ECHAP] Menu",
            hint, VS_CANVAS_W/2, VS_CANVAS_H - 30);
    }
}

/* ─── Input ─────────────────────────────────────────────────────── */

void versus_handle_motion(VersusState *vs, int lx, int ly)
{
    if (vs->phase != VS_PHASE_MATCH_END) { vs->ui_hover = -1; return; }
    SDL_Rect r0 = rematch_rect();
    SDL_Rect r1 = menu_rect();
    SDL_Point p = {lx, ly};
    if      (SDL_PointInRect(&p, &r0)) { vs->ui_hover = 0; vs->ui_cursor = 0; }
    else if (SDL_PointInRect(&p, &r1)) { vs->ui_hover = 1; vs->ui_cursor = 1; }
    else                                 vs->ui_hover = -1;
}

void versus_handle_click(VersusState *vs, int lx, int ly)
{
    if (vs->phase != VS_PHASE_MATCH_END) return;
    SDL_Rect r0 = rematch_rect();
    SDL_Rect r1 = menu_rect();
    SDL_Point p = {lx, ly};
    if      (SDL_PointInRect(&p, &r0)) vs->request_rematch      = 1;
    else if (SDL_PointInRect(&p, &r1)) vs->request_exit_to_menu = 1;
}

void versus_handle_key(VersusState *vs, SDL_Keycode k)
{
    if (vs->phase != VS_PHASE_MATCH_END) {
        if (k == SDLK_ESCAPE) vs->request_exit_to_menu = 1;
        return;
    }
    if (k == SDLK_LEFT  || k == SDLK_a) vs->ui_cursor = 0;
    else if (k == SDLK_RIGHT || k == SDLK_d) vs->ui_cursor = 1;
    else if (k == SDLK_RETURN || k == SDLK_SPACE) {
        if (vs->ui_cursor == 1) vs->request_exit_to_menu = 1;
        else                    vs->request_rematch      = 1;
    } else if (k == SDLK_ESCAPE) {
        vs->request_exit_to_menu = 1;
    }
}

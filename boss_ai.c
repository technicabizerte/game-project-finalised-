/* ----------------------------------------------------------------------------
 * boss_ai — implementation.  Standalone singleton fed by main.c each frame.
 * Owns the boss entity's position/HP/animation transitions for the duration
 * of the final encounter.  combat.c::ennemis_update SKIPS the final boss
 * (the unique ET_ENFORCER in wagon MAX_WAGONS-1) so the two AIs never race.
 * -------------------------------------------------------------------------- */

#include "boss_ai.h"
#include "boss_anim.h"
#include "boss_hud.h"
#include "effects.h"
#include "damage_numbers.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- tunables ----------------------------------------------------------- */

#define BAI_MELEE_RANGE_PX        130       /* enter ATTACK / KICK at this distance */
#define BAI_KICK_RANGE_PX         150       /* slightly wider for the kick frames    */
#define BAI_SLAM_RANGE_PX         260       /* slam radius (knockback aoe)           */
#define BAI_HITBOX_PAD_PX         24        /* generous attack hitbox padding        */
#define BAI_KICK_HITBOX_PAD_PX    36

#define BAI_RUN_SPEED_PX_PER_SEC  280.0f
#define BAI_PHASE_2_SPEED_BOOST   1.15f
#define BAI_PHASE_3_SPEED_BOOST   1.05f

#define BAI_P1_RUN_MIN            0.7f
#define BAI_P1_RUN_MAX            1.0f
#define BAI_P2_RUN_MIN            0.5f
#define BAI_P2_RUN_MAX            0.7f

#define BAI_GS_COOLDOWN_P1        12.0f
#define BAI_GS_COOLDOWN_P2         8.0f
#define BAI_GS_COOLDOWN_P3         5.0f

#define BAI_DECIDE_HOLD_SEC       0.35f     /* brief pause between attacks          */

#define BAI_ATTACK_IMPACT_FRAC    0.40f
#define BAI_KICK_IMPACT_FRAC      0.45f
#define BAI_SLAM_IMPACT_FRAC      0.55f

#define BAI_ATTACK_DMG_BASE       18
#define BAI_KICK_DMG_BASE         14
#define BAI_SLAM_DMG_BASE         32

#define BAI_HIT_REACT_HALF_FRAC   0.50f     /* Phase 2 cuts HIT_REACT short          */

/* ---- module state ------------------------------------------------------- */

typedef enum { ATK_NONE = 0, ATK_ATTACK, ATK_KICK, ATK_GROUND_SLAM } BAIAttackKind;

typedef struct {
    /* engine pointers (borrowed) */
    Background    *bg;
    GameplayState *gp;
    AudioSystem   *audio;

    /* top-level state machine */
    BossAIState state;
    int         phase;                /* 1, 2, 3                              */
    float       decide_hold_timer;    /* tiny pause between attacks           */

    /* current attack bookkeeping */
    BAIAttackKind current_attack;
    BAIAttackKind last_attack;        /* anti-spam                            */
    bool          chain_kick_after_attack; /* the ATTACK+KICK combo flag      */
    bool          impact_fired_this_attack;

    /* run-charge */
    float run_charge_max_sec;
    float run_charge_timer;

    /* ground-slam cooldown */
    float ground_slam_cooldown;

    /* segment-event sub-FSM */
    int seg_sub_step;                 /* 0=HURT, 1=RAGE (for SEG_RAGE)        */

    /* phase 3 substitute */
    bool use_hurt_as_idle;

    /* boss cache */
    int boss_idx;
    int max_hp;

    /* finish flags read by main.c */
    bool win_ready;
    bool loss_ready;
} BossAIRuntime;

static BossAIRuntime g_ai;

/* ---- helpers ----------------------------------------------------------- */
static Ennemi *ai_find_boss(void)
{
    if (!g_ai.bg) return NULL;
    for (int i = 0; i < g_ai.bg->nbEnnemis; i++) {
        Ennemi *e = &g_ai.bg->ennemis[i];
        if (!e->actif) continue;
        if (e->wagon != MAX_WAGONS - 1) continue;
        if (e->type == ET_ENFORCER) { g_ai.boss_idx = i; return e; }
    }
    g_ai.boss_idx = -1;
    return NULL;
}

static Joueur *ai_pick_player(Ennemi *boss)
{
    if (!g_ai.bg || !boss) return NULL;
    Joueur *best = NULL;
    float bd = 1e9f;
    for (int p = 0; p < g_ai.bg->nbJoueursActifs; p++) {
        Joueur *j = &g_ai.bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        float d = fabsf(j->x - boss->x);
        if (d < bd) { bd = d; best = j; }
    }
    return best;
}

static int ai_facing(Ennemi *boss, Joueur *p)
{
    if (!p || !boss) return boss ? boss->direction : 1;
    return (p->x < boss->x) ? -1 : 1;
}

static float frand01(void) { return (float)(rand() % 10000) / 10000.0f; }
static float frand_range(float lo, float hi) { return lo + frand01() * (hi - lo); }

static float ai_phase2_atk_kick_scale(void)
{
    /* +15% faster = frame_time scaled by 1/1.15 */
    return 1.0f / 1.15f;
}

static void ai_apply_phase_settings(int phase)
{
    /* Reset every frame_time scale to default; then dial Phase 2 speedups. */
    boss_anim_set_frame_time_scale(BOSS_ATTACK, 1.0f);
    boss_anim_set_frame_time_scale(BOSS_KICK,   1.0f);
    boss_anim_set_frame_time_scale(BOSS_HIT_REACT, 1.0f);

    if (phase >= 2) {
        boss_anim_set_frame_time_scale(BOSS_ATTACK, ai_phase2_atk_kick_scale());
        boss_anim_set_frame_time_scale(BOSS_KICK,   ai_phase2_atk_kick_scale());
        /* Phase 2 plays half the HIT_REACT frames before returning by
         * effectively halving its frame_time too — keeps the visible recoil
         * length the same as halving frame_count, without changing the
         * metadata table.                                                    */
        boss_anim_set_frame_time_scale(BOSS_HIT_REACT, 0.5f);
    }
    g_ai.use_hurt_as_idle = (phase >= 3);
}

/* ---- lifecycle --------------------------------------------------------- */
void boss_ai_init(Background *bg, GameplayState *gp, AudioSystem *audio)
{
    memset(&g_ai, 0, sizeof(g_ai));
    g_ai.bg    = bg;
    g_ai.gp    = gp;
    g_ai.audio = audio;
    g_ai.state = BAI_IDLE;
    g_ai.phase = 1;
    g_ai.boss_idx = -1;
    g_ai.max_hp   = 1;
    g_ai.last_attack = ATK_NONE;
}

void boss_ai_reset(void)
{
    Background    *bg    = g_ai.bg;
    GameplayState *gp    = g_ai.gp;
    AudioSystem   *audio = g_ai.audio;
    memset(&g_ai, 0, sizeof(g_ai));
    g_ai.bg    = bg;
    g_ai.gp    = gp;
    g_ai.audio = audio;
    g_ai.state = BAI_IDLE;
    g_ai.phase = 1;
    g_ai.boss_idx = -1;
    g_ai.max_hp   = 1;
    g_ai.last_attack = ATK_NONE;
    ai_apply_phase_settings(1);
}

void boss_ai_arm_taunt(void)
{
    /* Called once after boss_intro completes (or is skipped because already
     * played).  Plays BOSS_POINT, locks AI until completion.                */
    Ennemi *boss = ai_find_boss();
    if (!boss) {
        /* No boss spawned yet (boss intro may still be ramping the wagon
         * load); arm anyway — the next update() that finds the boss will
         * begin the taunt animation.                                       */
    }
    g_ai.max_hp = (int)((float)ENFORCER_HP * ENEMY_HP_BASE_MULT * BOSS_HP_MULT);
    if (g_ai.max_hp < 1) g_ai.max_hp = 1;
    g_ai.state = BAI_TAUNT;
    g_ai.phase = 1;
    g_ai.last_attack = ATK_NONE;
    g_ai.ground_slam_cooldown = 0.0f;
    g_ai.current_attack = ATK_NONE;
    g_ai.chain_kick_after_attack = false;
    g_ai.impact_fired_this_attack = false;
    g_ai.decide_hold_timer = 0.0f;
    g_ai.run_charge_timer = 0.0f;
    g_ai.seg_sub_step = 0;
    g_ai.use_hurt_as_idle = false;
    g_ai.win_ready  = false;
    g_ai.loss_ready = false;
    ai_apply_phase_settings(1);
    boss_anim_set_force(BOSS_POINT);
}

/* ---- attack pool picker ------------------------------------------------ */
static BAIAttackKind ai_pick_next_attack(void)
{
    /* Build a candidate list per current phase, excluding the last_attack. */
    BAIAttackKind candidates[6];
    int n = 0;

    bool can_slam = (g_ai.ground_slam_cooldown <= 0.0f);

    if (g_ai.phase == 3) {
        /* Phase 3 pool: ATTACK / KICK (anti-spam alternates) + GS.         */
        if (g_ai.last_attack != ATK_ATTACK) candidates[n++] = ATK_ATTACK;
        if (g_ai.last_attack != ATK_KICK)   candidates[n++] = ATK_KICK;
        if (can_slam && g_ai.last_attack != ATK_GROUND_SLAM)
            candidates[n++] = ATK_GROUND_SLAM;
    } else {
        /* Phase 1 / 2 — full pool minus last_attack.  ATTACK enters via a
         * 50/50 with KICK (since "ATTACK then KICK" and "ATTACK alone" both
         * count as ATTACK).  We model the chain decision after picking.    */
        if (g_ai.last_attack != ATK_ATTACK) candidates[n++] = ATK_ATTACK;
        if (g_ai.last_attack != ATK_KICK)   candidates[n++] = ATK_KICK;
        if (can_slam && g_ai.last_attack != ATK_GROUND_SLAM)
            candidates[n++] = ATK_GROUND_SLAM;
    }

    if (n == 0) {
        /* Should never happen — fallback to ATTACK. */
        return ATK_ATTACK;
    }
    return candidates[rand() % n];
}

/* ---- starting an attack ------------------------------------------------ */
static void ai_begin_attack(BAIAttackKind kind, Ennemi *boss, Joueur *p)
{
    g_ai.current_attack          = kind;
    g_ai.impact_fired_this_attack = false;
    g_ai.chain_kick_after_attack  = false;
    if (boss && p) boss->direction = ai_facing(boss, p);

    /* Phase 1/2: ATTACK has a 50% chance to chain into KICK after. */
    if (kind == ATK_ATTACK && g_ai.phase < 3) {
        if (frand01() < 0.5f) g_ai.chain_kick_after_attack = true;
    }

    switch (kind) {
        case ATK_ATTACK:        boss_anim_set_force(BOSS_ATTACK);       break;
        case ATK_KICK:          boss_anim_set_force(BOSS_KICK);         break;
        case ATK_GROUND_SLAM:   boss_anim_set_force(BOSS_GROUND_SLAM);  break;
        default: break;
    }
    g_ai.state = BAI_ATTACKING;
}

/* ---- impact frame check + damage --------------------------------------- */
static void ai_apply_attack_damage(Ennemi *boss, Joueur *p, BAIAttackKind kind)
{
    if (!boss || !p || p->hp <= 0) return;
    int base_dmg, pad;
    switch (kind) {
        case ATK_ATTACK:      base_dmg = BAI_ATTACK_DMG_BASE; pad = BAI_HITBOX_PAD_PX;      break;
        case ATK_KICK:        base_dmg = BAI_KICK_DMG_BASE;   pad = BAI_KICK_HITBOX_PAD_PX; break;
        case ATK_GROUND_SLAM: base_dmg = BAI_SLAM_DMG_BASE;   pad = BAI_SLAM_RANGE_PX;      break;
        default: return;
    }
    int dmg = (int)((float)base_dmg * ENEMY_DMG_MULT);

    SDL_Rect br = {(int)boss->x - pad, (int)boss->y,
                   BOSS_HBW + 2 * pad, BOSS_HBH};
    SDL_Rect pr = {(int)p->x, (int)p->y, p->w, p->h};
    if (!SDL_HasIntersection(&br, &pr)) return;

    p->hp -= dmg;
    if (p->hp < 0) p->hp = 0;
    spawn_damage_number(p->x + p->w / 2.0f, p->y + p->h / 3.0f,
                        dmg, DMG_NUM_PLAYER);

    int dirSign = (p->x >= boss->x) ? 1 : -1;
    EffectsSystem *efx = effects_global();
    if (efx) {
        /* index in players: pick the matching slot */
        int pidx = (p == &g_ai.bg->joueurs[0]) ? 0 :
                   (g_ai.bg->nbJoueursActifs > 1 && p == &g_ai.bg->joueurs[1]) ? 1 : 0;
        effects_on_hit(efx, EFX_ENT_PLAYER, pidx, dirSign);
        effects_spawn_impact(efx, p->x + p->w / 2.0f, p->y + p->h / 3.0f,
                             dirSign, dmg);
    }

    /* Player hit reaction. */
    if (p->etat != PS_HIT && p->etat != PS_DEAD) {
        animation_set_row(&p->anim, ANIM_P_HIT, PLAYER_NFRAMES[ANIM_P_HIT],
                          PLAYER_ANIM_FPS[ANIM_P_HIT], 0);
        p->etat = (p->hp == 0) ? PS_DEAD : PS_HIT;
        char_anim_set(&p->ca, (p->hp == 0) ? ANIM_DEATH : ANIM_HIT_REACT);
    }

    if (g_ai.audio) audio_play_sfx(g_ai.audio, SFX_PLAYER_HURT);

    /* Heavier hitstop + shake on slam. */
    if (kind == ATK_GROUND_SLAM) {
        if (g_ai.gp) gameplay_trigger_shake(g_ai.gp, SHAKE_HAZARD_AMP, 0.30f);
        hitstop_trigger(0.10f);
    }
}

static int ai_attack_impact_frame(BAIAttackKind kind)
{
    BossState s = (kind == ATK_ATTACK)      ? BOSS_ATTACK :
                  (kind == ATK_KICK)        ? BOSS_KICK   :
                                              BOSS_GROUND_SLAM;
    int total = boss_anim_frame_count(s);
    float frac = (kind == ATK_ATTACK)      ? BAI_ATTACK_IMPACT_FRAC :
                 (kind == ATK_KICK)        ? BAI_KICK_IMPACT_FRAC   :
                                             BAI_SLAM_IMPACT_FRAC;
    int f = (int)((float)total * frac);
    if (f < 0)            f = 0;
    if (f >= total)       f = total - 1;
    return f;
}

/* ---- transitioning between AI states ---------------------------------- */
static void ai_enter_deciding(void)
{
    g_ai.state = BAI_DECIDING;
    g_ai.decide_hold_timer = BAI_DECIDE_HOLD_SEC;
    g_ai.current_attack = ATK_NONE;
    /* Phase 3 substitute: stay in HURT pose while deciding so the boss
     * reads as wounded between attacks.                                    */
    if (g_ai.use_hurt_as_idle) boss_anim_set(BOSS_HURT);
    else                       boss_anim_set(BOSS_IDLE);
}

static void ai_enter_run_charge(Ennemi *boss, Joueur *p)
{
    if (!boss) return;
    boss->direction = ai_facing(boss, p);
    if (g_ai.phase >= 2) {
        g_ai.run_charge_max_sec = frand_range(BAI_P2_RUN_MIN, BAI_P2_RUN_MAX);
    } else {
        g_ai.run_charge_max_sec = frand_range(BAI_P1_RUN_MIN, BAI_P1_RUN_MAX);
    }
    g_ai.run_charge_timer = 0.0f;
    g_ai.state = BAI_RUN_CHARGE;
    boss_anim_set_force(BOSS_RUN);
}

/* ---- update ----------------------------------------------------------- */
void boss_ai_update(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;

    Ennemi *boss = ai_find_boss();

    /* Cool down the ground-slam timer regardless of state. */
    if (g_ai.ground_slam_cooldown > 0.0f) {
        g_ai.ground_slam_cooldown -= dt;
        if (g_ai.ground_slam_cooldown < 0.0f) g_ai.ground_slam_cooldown = 0.0f;
    }

    /* boss_anim_update is driven by main.c outside the world-sim gate so
     * the sprite still advances during boss_ai's own segment freezes.
     * We deliberately do NOT call boss_anim_update here. */

    /* Terminal sequences first. */
    if (g_ai.state == BAI_DYING) {
        if (boss_anim_just_finished(BOSS_DEATH) ||
            (boss_anim_get_state() == BOSS_DEATH && boss_anim_is_finished()))
            g_ai.win_ready = true;
        if (g_ai.win_ready) g_ai.state = BAI_DONE;
        return;
    }
    if (g_ai.state == BAI_VICTORY) {
        if (boss_anim_just_finished(BOSS_VICTORY) ||
            (boss_anim_get_state() == BOSS_VICTORY && boss_anim_is_finished()))
            g_ai.loss_ready = true;
        if (g_ai.loss_ready) g_ai.state = BAI_DONE;
        return;
    }
    if (g_ai.state == BAI_DONE) return;

    /* IDLE — waiting for boss_ai_arm_taunt(). */
    if (g_ai.state == BAI_IDLE) return;

    /* From here on we need a boss entity.  If it's not yet spawned (intro
     * may still be loading the wagon), idle wait. */
    if (!boss) return;

    /* Gravity for the boss — combat.c skips the final boss entirely now. */
    boss->velY += GRAVITE_JEU;
    boss->y    += boss->velY;
    if (boss->y + BOSS_HBH >= WAGON_SOL_Y) {
        boss->y = (float)(WAGON_SOL_Y - BOSS_HBH);
        boss->velY = 0; boss->auSol = 1;
    }
    if (boss->y < WAGON_PLAF_Y) { boss->y = WAGON_PLAF_Y; boss->velY = 0; }

    Joueur *p = ai_pick_player(boss);

    /* ---- TAUNT ---- */
    if (g_ai.state == BAI_TAUNT) {
        if (boss_anim_get_state() != BOSS_POINT)
            boss_anim_set_force(BOSS_POINT);
        if (boss_anim_get_state() == BOSS_POINT && boss_anim_is_finished()) {
            g_ai.phase = 1;
            ai_apply_phase_settings(1);
            ai_enter_deciding();
        }
        return;
    }

    /* ---- SEG_HURT (80/60/40) ---- */
    if (g_ai.state == BAI_SEG_HURT) {
        if (boss_anim_get_state() != BOSS_HURT)
            boss_anim_set_force(BOSS_HURT);
        if (boss_anim_get_state() == BOSS_HURT && boss_anim_is_finished()) {
            if (!hitstop_active()) ai_enter_deciding();
        }
        return;
    }

    /* ---- SEG_RAGE (50) ---- */
    if (g_ai.state == BAI_SEG_RAGE) {
        if (g_ai.seg_sub_step == 0) {
            if (boss_anim_get_state() != BOSS_HURT)
                boss_anim_set_force(BOSS_HURT);
            if (boss_anim_get_state() == BOSS_HURT && boss_anim_is_finished()) {
                boss_anim_set_force(BOSS_RAGE);
                g_ai.seg_sub_step = 1;
                /* HUD pulse during RAGE per spec.                          */
                boss_hud_pulse_name();
            }
        } else {
            if (boss_anim_get_state() == BOSS_RAGE && boss_anim_is_finished()) {
                g_ai.phase = 2;
                ai_apply_phase_settings(2);
                ai_enter_deciding();
            }
        }
        return;
    }

    /* ---- SEG_SLAM (20) ---- */
    if (g_ai.state == BAI_SEG_SLAM) {
        if (boss_anim_get_state() != BOSS_GROUND_SLAM)
            boss_anim_set_force(BOSS_GROUND_SLAM);
        if (boss_anim_get_state() == BOSS_GROUND_SLAM && boss_anim_is_finished()) {
            g_ai.phase = 3;
            ai_apply_phase_settings(3);
            /* Apply slam damage at completion (single AoE hit). */
            if (p) ai_apply_attack_damage(boss, p, ATK_GROUND_SLAM);
            ai_enter_deciding();
        }
        return;
    }

    /* ---- HIT_RECOVER ---- */
    if (g_ai.state == BAI_HIT_RECOVER) {
        if (boss_anim_get_state() == BOSS_HIT_REACT &&
            boss_anim_is_finished()) {
            ai_enter_deciding();
        }
        return;
    }

    /* ---- RUN_CHARGE ---- */
    if (g_ai.state == BAI_RUN_CHARGE) {
        if (!p) { ai_enter_deciding(); return; }
        g_ai.run_charge_timer += dt;
        boss->direction = ai_facing(boss, p);
        float spd = BAI_RUN_SPEED_PX_PER_SEC;
        if (g_ai.phase == 2) spd *= BAI_PHASE_2_SPEED_BOOST;
        if (g_ai.phase == 3) spd *= BAI_PHASE_3_SPEED_BOOST;
        float step = spd * dt * (boss->direction);
        boss->x += step;
        if (boss->x < WAGON_MUR_G_X) boss->x = WAGON_MUR_G_X;
        if (boss->x + BOSS_HBW > WAGON_MUR_D_X)
            boss->x = WAGON_MUR_D_X - BOSS_HBW;

        float dist = fabsf(p->x - boss->x);
        if (dist <= BAI_MELEE_RANGE_PX ||
            g_ai.run_charge_timer >= g_ai.run_charge_max_sec) {
            ai_begin_attack(ATK_ATTACK, boss, p);
        }
        return;
    }

    /* ---- ATTACKING ---- */
    if (g_ai.state == BAI_ATTACKING) {
        /* Impact-frame damage application. */
        BAIAttackKind kind = g_ai.current_attack;
        if (!g_ai.impact_fired_this_attack && p) {
            int impact = ai_attack_impact_frame(kind);
            if (boss_anim_current_frame() >= impact) {
                ai_apply_attack_damage(boss, p, kind);
                g_ai.impact_fired_this_attack = true;
            }
        }
        if (boss_anim_is_finished()) {
            /* Attack ended.  Apply anti-spam memory + chain logic. */
            if (kind == ATK_ATTACK && g_ai.chain_kick_after_attack) {
                /* Combo continues: KICK now (counts as ATTACK for anti-spam). */
                g_ai.chain_kick_after_attack = false;
                g_ai.current_attack = ATK_KICK;
                g_ai.impact_fired_this_attack = false;
                boss_anim_set_force(BOSS_KICK);
                return;
            }
            /* Anti-spam memory: the combo is recorded under ATTACK only. */
            if (kind == ATK_ATTACK || kind == ATK_KICK || kind == ATK_GROUND_SLAM)
                g_ai.last_attack = (kind == ATK_GROUND_SLAM) ? ATK_GROUND_SLAM :
                                   (kind == ATK_ATTACK ||
                                    (kind == ATK_KICK && g_ai.last_attack == ATK_ATTACK))
                                        ? ATK_ATTACK : ATK_KICK;
            if (kind == ATK_GROUND_SLAM) {
                float cd = (g_ai.phase >= 3) ? BAI_GS_COOLDOWN_P3 :
                           (g_ai.phase >= 2) ? BAI_GS_COOLDOWN_P2 :
                                               BAI_GS_COOLDOWN_P1;
                g_ai.ground_slam_cooldown = cd;
            }
            ai_enter_deciding();
        }
        return;
    }

    /* ---- DECIDING ---- */
    if (g_ai.state == BAI_DECIDING) {
        if (g_ai.decide_hold_timer > 0.0f) {
            g_ai.decide_hold_timer -= dt;
            return;
        }
        if (!p) return;

        BAIAttackKind next = ai_pick_next_attack();
        float dist = fabsf(p->x - boss->x);

        /* In Phase 3 there's no RUN windup — close range or not, snap to
         * the picked attack directly.                                       */
        if (g_ai.phase == 3 || next == ATK_GROUND_SLAM) {
            ai_begin_attack(next, boss, p);
            return;
        }

        /* In Phases 1 and 2: if the player is in melee range, just attack;
         * otherwise either RUN charge then ATTACK (50% in P1, weighted)
         * or kick (which has slightly longer range).                       */
        if (dist > BAI_MELEE_RANGE_PX) {
            ai_enter_run_charge(boss, p);
        } else {
            ai_begin_attack(next, boss, p);
        }
        return;
    }
}

/* ---- segment / death hooks ------------------------------------------- */
void boss_ai_event_hurt_segment(int segment_idx)
{
    (void)segment_idx;
    if (g_ai.state == BAI_DYING || g_ai.state == BAI_VICTORY ||
        g_ai.state == BAI_DONE  || g_ai.state == BAI_TAUNT  ||
        g_ai.state == BAI_SEG_RAGE || g_ai.state == BAI_SEG_SLAM)
        return;
    g_ai.state = BAI_SEG_HURT;
    g_ai.impact_fired_this_attack = false;
    boss_anim_set_force(BOSS_HURT);
}

void boss_ai_event_rage_50(void)
{
    if (g_ai.state == BAI_DYING || g_ai.state == BAI_VICTORY ||
        g_ai.state == BAI_DONE  || g_ai.state == BAI_TAUNT)
        return;
    g_ai.state = BAI_SEG_RAGE;
    g_ai.seg_sub_step = 0;
    g_ai.impact_fired_this_attack = false;
    boss_anim_set_force(BOSS_HURT);
}

void boss_ai_event_slam_20(void)
{
    if (g_ai.state == BAI_DYING || g_ai.state == BAI_VICTORY ||
        g_ai.state == BAI_DONE  || g_ai.state == BAI_TAUNT)
        return;
    g_ai.state = BAI_SEG_SLAM;
    g_ai.impact_fired_this_attack = false;
    boss_anim_set_force(BOSS_GROUND_SLAM);
}

void boss_ai_request_death(void)
{
    if (g_ai.state == BAI_DYING || g_ai.state == BAI_DONE) return;
    g_ai.state = BAI_DYING;
    boss_anim_set_force(BOSS_DEATH);
}

void boss_ai_request_victory(void)
{
    if (g_ai.state == BAI_VICTORY || g_ai.state == BAI_DONE) return;
    g_ai.state = BAI_VICTORY;
    boss_anim_set_force(BOSS_VICTORY);
}

void boss_ai_on_hit(void)
{
    /* Only fire HIT_REACT when the boss is between attacks (DECIDING) or
     * mid-run.  Locked attacks, segment animations, taunt, and death stay
     * uninterrupted.                                                       */
    if (g_ai.state != BAI_DECIDING && g_ai.state != BAI_RUN_CHARGE) return;
    boss_anim_set_force(BOSS_HIT_REACT);
    g_ai.state = BAI_HIT_RECOVER;
}

/* ---- queries --------------------------------------------------------- */
bool boss_ai_world_frozen(void)
{
    return g_ai.state == BAI_TAUNT    ||
           g_ai.state == BAI_SEG_RAGE ||
           g_ai.state == BAI_SEG_SLAM ||
           g_ai.state == BAI_DYING    ||
           g_ai.state == BAI_VICTORY;
}

bool boss_ai_input_frozen(void)
{
    /* TAUNT, all SEG_*, DYING, VICTORY freeze player input.  SEG_HURT is
     * a brief HURT that still benefits from input lock so the player can't
     * keep hammering through it.                                          */
    return boss_ai_world_frozen() || g_ai.state == BAI_SEG_HURT;
}

bool        boss_ai_should_outro    (void) { return g_ai.win_ready;  }
bool        boss_ai_should_gameover (void) { return g_ai.loss_ready; }
BossAIState boss_ai_state           (void) { return g_ai.state;      }
int         boss_ai_phase           (void) { return g_ai.phase;      }
bool        boss_ai_taunt_active    (void) { return g_ai.state == BAI_TAUNT; }
int         boss_ai_boss_idx        (void) { return g_ai.boss_idx;   }

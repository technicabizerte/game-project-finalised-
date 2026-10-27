#include "enemy_taunts.h"
#include "combat.h"
#include "gameplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAUNT_RANGE        350.0f
#define TAUNT_MS_MIN       4500u
#define TAUNT_MS_MAX       8500u
#define TAUNT_LLM_TEXT_MAX 96
#define TAUNT_LLM_SHOW_MS  6500u
#define TAUNT_LLM_COOLDOWN 22000u
#define TAUNT_LLM_GRACE_MS 10000u
#define TAUNT_LOW_HP_THRESH 30

typedef struct {
    char   text[80];
    Uint32 change_tick;
    char   llm_text[TAUNT_LLM_TEXT_MAX];
    Uint32 llm_until;
} TauntSlot;

typedef enum {
    LLM_TRIG_NONE = 0,
    LLM_TRIG_LOW_HP,
    LLM_TRIG_LAST_STANDING
} LLMTauntTrigger;

typedef struct {
    bool          pending;
    int           target_idx;
    TypeEnnemi    target_type;
    int           target_wagon;
    LLMTauntTrigger trigger;
    Uint32        deadline_tick;
} LLMTauntInFlight;

static TauntSlot       g_slots[MAX_ENNEMIS];
static LLMTauntInFlight g_inflight;
static Uint32          g_last_llm_tick = 0;
static Uint32          g_next_check_tick = 0;

static const char *POOL_GENERIC[] = {
    "Tail-ender filth!",
    "Stay where you belong!",
    "You'll never reach the engine!",
    "This is as far as you go!",
    "Wilford warned us about you.",
    "Back to the tail with you!",
    "I see you, rebel.",
    "You'll regret this!",
    "Come on then, hero!",
    "Try me, tail-ender!",
    "You bleed like the rest!",
    "Time to be quiet, rebel."
};
static const int POOL_GENERIC_N =
    (int)(sizeof(POOL_GENERIC) / sizeof(POOL_GENERIC[0]));

static const char *POOL_GUARD[] = {
    "Stand down, by order!",
    "Guards! On me!",
    "You won't pass this car!",
    "Drop your weapon, rebel!",
    "I serve Wilford.",
    "The Eternal Engine demands order!",
    "You're outranked, tail-ender."
};
static const int POOL_GUARD_N =
    (int)(sizeof(POOL_GUARD) / sizeof(POOL_GUARD[0]));

static const char *POOL_WORKER[] = {
    "We were content until you came!",
    "You'll ruin everything!",
    "Keep your hands off our car!",
    "Tail-enders, always trouble.",
    "Stay out of our wagon!",
    "We work hard, we earn our keep!",
    "You don't belong here!"
};
static const int POOL_WORKER_N =
    (int)(sizeof(POOL_WORKER) / sizeof(POOL_WORKER[0]));

static const char *POOL_ENFORCER[] = {
    "I crush insects like you.",
    "End of the line, rebel.",
    "You won't survive this car!",
    "I am the law here!",
    "Bring it on, tail-ender!",
    "I've broken bigger than you.",
    "Pray to the Engine."
};
static const int POOL_ENFORCER_N =
    (int)(sizeof(POOL_ENFORCER) / sizeof(POOL_ENFORCER[0]));

static const char *random_taunt(TypeEnnemi t)
{
    int gen_chance = rand() % 3;
    if (gen_chance == 0) return POOL_GENERIC[rand() % POOL_GENERIC_N];
    switch (t) {
        case ET_GUARD:    return POOL_GUARD   [rand() % POOL_GUARD_N];
        case ET_WORKER:   return POOL_WORKER  [rand() % POOL_WORKER_N];
        case ET_ENFORCER: return POOL_ENFORCER[rand() % POOL_ENFORCER_N];
    }
    return POOL_GENERIC[rand() % POOL_GENERIC_N];
}

void enemy_taunts_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_inflight, 0, sizeof(g_inflight));
    g_inflight.target_idx = -1;
    g_last_llm_tick     = 0;
    g_next_check_tick   = 0;
}

static float min_dist_sq_to_player(const Background *bg, float ex, float ey,
                                    int *which_player)
{
    float best = 1e18f;
    int   best_p = -1;
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        const Joueur *j = &bg->joueurs[p];
        if (!j->actif || j->hp <= 0) continue;
        float dx = j->x + j->w / 2.0f - ex;
        float dy = j->y + j->h / 2.0f - ey;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; best_p = p; }
    }
    if (which_player) *which_player = best_p;
    return best;
}

static const char *type_name(TypeEnnemi t)
{
    switch (t) {
        case ET_GUARD:    return "loyal Wilford guard";
        case ET_WORKER:   return "frightened wagon worker";
        case ET_ENFORCER: return "armored enforcer brute";
    }
    return "guard";
}

static const char *wagon_short(int wagon)
{
    static const char *names[] = {
        "the squalid tail cars",
        "the industrial factory car",
        "the elite aquarium car",
        "the lush greenhouse car",
        "the luxury car"
    };
    if (wagon >= 0 && wagon < 5) return names[wagon];
    return "this wagon";
}

static int draw_w_for(TypeEnnemi t)
{
    if (t == ET_WORKER)   return WORKER_DRAW_W;
    if (t == ET_ENFORCER) return ENFORCER_DRAW_W;
    return GUARD_DRAW_W;
}

static int draw_h_for(TypeEnnemi t)
{
    if (t == ET_WORKER)   return WORKER_DRAW_H;
    if (t == ET_ENFORCER) return ENFORCER_DRAW_H;
    return GUARD_DRAW_H;
}

static void clean_taunt(char *s)
{
    if (!s) return;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '"') {
        memmove(s, s + 1, strlen(s));
    }
    size_t L = strlen(s);
    while (L > 0 && (s[L-1] == ' ' || s[L-1] == '\n' ||
                     s[L-1] == '\r' || s[L-1] == '\t' || s[L-1] == '"')) {
        s[--L] = 0;
    }
    char *nl = strchr(s, '\n');
    if (nl) *nl = 0;
    static const char *prefixes[] = {
        "TAUNT:", "Taunt:", "taunt:",
        "GUARD:", "Guard:", "guard:",
        "WORKER:", "Worker:", "worker:",
        "ENFORCER:", "Enforcer:", "enforcer:",
        "SAY:", "Say:", "say:",
        "ANSWER:", "Answer:", "answer:",
        NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t p = strlen(prefixes[i]);
        if (strncmp(s, prefixes[i], p) == 0) {
            memmove(s, s + p, strlen(s + p) + 1);
            while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
            break;
        }
    }
    if (strlen(s) > TAUNT_LLM_TEXT_MAX - 1) s[TAUNT_LLM_TEXT_MAX - 1] = 0;
}

static void on_taunt_response(bool ok, const char *response, void *userdata)
{
    (void)userdata;
    g_inflight.pending = false;
    if (!ok || !response || !response[0]) return;
    int idx = g_inflight.target_idx;
    if (idx < 0 || idx >= MAX_ENNEMIS) return;
    if (SDL_GetTicks() > g_inflight.deadline_tick) return;

    char tmp[TAUNT_LLM_TEXT_MAX];
    strncpy(tmp, response, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    clean_taunt(tmp);
    if (!tmp[0]) return;

    strncpy(g_slots[idx].llm_text, tmp, sizeof(g_slots[idx].llm_text) - 1);
    g_slots[idx].llm_text[sizeof(g_slots[idx].llm_text) - 1] = 0;
    g_slots[idx].llm_until = SDL_GetTicks() + TAUNT_LLM_SHOW_MS;
    g_last_llm_tick = SDL_GetTicks();
}

static void try_llm_request(Background *bg, Uint32 now, LLMQueue *q,
                             int target_idx, LLMTauntTrigger trig,
                             int player_hp_pct)
{
    if (!q || !llm_queue_has_capacity(q)) return;
    if (g_inflight.pending) return;
    if (target_idx < 0 || target_idx >= MAX_ENNEMIS) return;

    Ennemi *e = &bg->ennemis[target_idx];
    char prompt[1400];

    const char *trig_text;
    if (trig == LLM_TRIG_LOW_HP) {
        trig_text = "the rebel player is bleeding badly and almost dead";
    } else if (trig == LLM_TRIG_LAST_STANDING) {
        trig_text = "you are the last enemy left alive in this wagon and probably about to die";
    } else {
        trig_text = "the rebel just entered your kill zone";
    }

    snprintf(prompt, sizeof(prompt),
        "<start_of_turn>user\n"
        "You write 1-line in-character battle taunts for enemies in a Snowpiercer-themed action game. "
        "Reply in English, in ONE short sentence, MAX 12 words. "
        "No preamble, no quotes, no asterisks, no labels. Stay in character.\n\n"
        "Speaker: a %s, fighting in %s.\n"
        "Situation: %s. Player health is about %d%%.\n\n"
        "Write the taunt the speaker shouts now.<end_of_turn>\n"
        "<start_of_turn>model\n",
        type_name(e->type),
        wagon_short(bg->wagonActif),
        trig_text,
        player_hp_pct);

    g_inflight.pending       = true;
    g_inflight.target_idx    = target_idx;
    g_inflight.target_type   = e->type;
    g_inflight.target_wagon  = bg->wagonActif;
    g_inflight.trigger       = trig;
    g_inflight.deadline_tick = now + TAUNT_LLM_GRACE_MS;

    if (!llm_queue_submit(q, LLM_PRIO_LOW, prompt, 35, 0.85f,
                          on_taunt_response, NULL)) {
        g_inflight.pending = false;
    }
}

static void check_llm_trigger(Background *bg, Uint32 now, LLMQueue *q)
{
    if (!q) return;
    if (g_inflight.pending) return;
    if (now < g_next_check_tick) return;
    g_next_check_tick = now + 1500u;

    if (g_last_llm_tick != 0 && now - g_last_llm_tick < TAUNT_LLM_COOLDOWN) {
        return;
    }

    int alive_in_wagon = 0;
    int last_alive_idx = -1;
    int min_player_hp  = JOUEUR_MAX_HP;
    for (int i = 0; i < bg->nbEnnemis && i < MAX_ENNEMIS; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) continue;
        alive_in_wagon++;
        last_alive_idx = i;
    }
    for (int p = 0; p < bg->nbJoueursActifs; p++) {
        Joueur *j = &bg->joueurs[p];
        if (!j->actif) continue;
        if (j->hp > 0 && j->hp < min_player_hp) min_player_hp = j->hp;
    }
    int min_player_hp_pct = (min_player_hp * 100) / JOUEUR_MAX_HP;

    LLMTauntTrigger trig = LLM_TRIG_NONE;
    int target_idx = -1;

    if (alive_in_wagon == 1 && last_alive_idx >= 0) {
        trig = LLM_TRIG_LAST_STANDING;
        target_idx = last_alive_idx;
    } else if (min_player_hp_pct <= TAUNT_LOW_HP_THRESH && alive_in_wagon > 0) {
        float best_d2 = TAUNT_RANGE * TAUNT_RANGE;
        int   best    = -1;
        for (int i = 0; i < bg->nbEnnemis && i < MAX_ENNEMIS; i++) {
            Ennemi *e = &bg->ennemis[i];
            if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) continue;
            int dw = draw_w_for(e->type);
            int dh = draw_h_for(e->type);
            float ex = e->x + dw / 2.0f;
            float ey = e->y + dh / 2.0f;
            int dummy;
            float d2 = min_dist_sq_to_player(bg, ex, ey, &dummy);
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }
        if (best >= 0) {
            trig = LLM_TRIG_LOW_HP;
            target_idx = best;
        }
    }

    if (trig != LLM_TRIG_NONE && target_idx >= 0) {
        try_llm_request(bg, now, q, target_idx, trig, min_player_hp_pct);
    }
}

void enemy_taunts_update(Background *bg, Uint32 now_tick, LLMQueue *q)
{
    if (!bg) return;
    for (int i = 0; i < bg->nbEnnemis && i < MAX_ENNEMIS; i++) {
        Ennemi *e = &bg->ennemis[i];

        if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) {
            g_slots[i].text[0]     = 0;
            g_slots[i].llm_text[0] = 0;
            g_slots[i].llm_until   = 0;
            continue;
        }

        int draw_w = draw_w_for(e->type);
        int draw_h = draw_h_for(e->type);
        float ex = e->x + draw_w / 2.0f;
        float ey = e->y + draw_h / 2.0f;
        int   dummy;
        if (min_dist_sq_to_player(bg, ex, ey, &dummy) > TAUNT_RANGE * TAUNT_RANGE) {
            g_slots[i].text[0] = 0;
            (void)draw_h;
            continue;
        }

        if (g_slots[i].llm_until != 0 && now_tick > g_slots[i].llm_until) {
            g_slots[i].llm_text[0] = 0;
            g_slots[i].llm_until   = 0;
        }

        if (g_slots[i].text[0] == 0 || now_tick >= g_slots[i].change_tick) {
            const char *t = random_taunt(e->type);
            strncpy(g_slots[i].text, t, sizeof(g_slots[i].text) - 1);
            g_slots[i].text[sizeof(g_slots[i].text) - 1] = 0;
            Uint32 span = TAUNT_MS_MAX - TAUNT_MS_MIN;
            Uint32 dur  = TAUNT_MS_MIN + (Uint32)(rand() % (int)span);
            g_slots[i].change_tick = now_tick + dur;
        }
    }

    if (g_inflight.pending) {
        int idx = g_inflight.target_idx;
        if (idx >= 0 && idx < MAX_ENNEMIS) {
            Ennemi *e = &bg->ennemis[idx];
            bool valid = e->actif && e->etat != ES_DEAD &&
                         e->wagon == g_inflight.target_wagon;
            if (!valid && now_tick > g_inflight.deadline_tick) {
                g_inflight.pending = false;
            }
        }
    }

    check_llm_trigger(bg, now_tick, q);
}

void enemy_taunts_render(Background *bg, SDL_Renderer *r, int ox,
                          UIFontPack *fonts, UITextCache *cache)
{
    if (!bg || !r) return;
    TTF_Font *font = fonts->monoSmall ? fonts->monoSmall :
                     (fonts->mono ? fonts->mono : fonts->fallback);
    if (!font) return;

    SDL_Color text_col = {255, 220, 180, 255};
    SDL_Color text_llm = {255, 240, 200, 255};

    for (int i = 0; i < bg->nbEnnemis && i < MAX_ENNEMIS; i++) {
        Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) continue;

        const char *display = NULL;
        bool is_llm = false;
        if (g_slots[i].llm_text[0] && SDL_GetTicks() < g_slots[i].llm_until) {
            display = g_slots[i].llm_text;
            is_llm  = true;
        } else if (g_slots[i].text[0]) {
            display = g_slots[i].text;
        }
        if (!display) continue;

        int draw_w = draw_w_for(e->type);
        int draw_h = draw_h_for(e->type);
        int hbh    = (e->type == ET_GUARD)  ? GUARD_HBH  :
                     (e->type == ET_WORKER) ? WORKER_HBH : ENFORCER_HBH;
        /* Sprite is foot-anchored: top = (e->y + hbh) - draw_h.  The old
         * anchor (e->y) used the hitbox top, which now sits at the chest
         * since the sprites grew taller than their forgiving hitboxes —
         * the taunt landed on the stomach.  Use the actual sprite top. */
        int sprite_top = (int)e->y + hbh - draw_h;

        int tw = 0, th = 0;
        TTF_SizeUTF8(font, display, &tw, &th);
        int pad = 4;
        int box_w = tw + pad * 2;
        int box_h = th + pad * 2;
        int cx    = (int)(e->x + draw_w / 2.0f) + ox;
        int cy    = sprite_top - box_h - 6;
        SDL_Rect box = { cx - box_w / 2, cy, box_w, box_h };

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        if (is_llm) {
            SDL_SetRenderDrawColor(r, 50, 12, 32, 220);
            SDL_RenderFillRect(r, &box);
            SDL_SetRenderDrawColor(r, 240, 140, 220, 240);
            SDL_RenderDrawRect(r, &box);
        } else {
            SDL_SetRenderDrawColor(r, 35, 8, 8, 210);
            SDL_RenderFillRect(r, &box);
            SDL_SetRenderDrawColor(r, 220, 80, 60, 240);
            SDL_RenderDrawRect(r, &box);
        }

        int tip_y = sprite_top - 2;
        SDL_RenderDrawLine(r, cx - 4, cy + box_h, cx, tip_y);
        SDL_RenderDrawLine(r, cx + 4, cy + box_h, cx, tip_y);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        ui_draw_text(cache, r, font, display,
                      is_llm ? text_llm : text_col,
                      box.x + pad, box.y + pad);
    }
}

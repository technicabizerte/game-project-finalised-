#include "boss_monologue.h"
#include "combat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOSS_SHOW_MS         7500u
#define BOSS_FADE_MS         400u
#define BOSS_REQUEST_GRACE   12000u
#define BOSS_LOW_HP_PCT      35

static const char *WAGON_NAME[BOSS_MONO_WAGONS] = {
    "the squalid tail cars",
    "the industrial factory car",
    "the elite aquarium car",
    "the lush greenhouse car",
    "the luxury car"
};

static const char *FALLBACK_LINE[BOSS_MONO_WAGONS] = {
    "You climb out of the filth and call yourself a revolution? I bury rats like you for sport.",
    "Every step you took here was logged in blood. Your run ends in this factory.",
    "The wealthy paid me to clean up vermin. You picked the wrong aisle, rebel.",
    "The garden grows from the bones of trespassers. You will fertilize the next harvest.",
    "Wilford himself sent me. The luxury car will be the end of your little story."
};

static void clean_response(char *s)
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
    static const char *prefixes[] = {
        "MONOLOGUE:", "Monologue:", "monologue:",
        "ENFORCER:", "Enforcer:", "enforcer:",
        "BOSS:", "Boss:", "boss:",
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
}

static void on_boss_response(bool ok, const char *response, void *userdata)
{
    BossMonologue *bm = (BossMonologue*)userdata;
    if (!bm) return;
    bm->pending = false;

    int w = bm->show_wagon;
    if (w < 0 || w >= BOSS_MONO_WAGONS) return;

    char tmp[BOSS_MONO_TEXT_MAX];
    if (ok && response && response[0]) {
        strncpy(tmp, response, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        clean_response(tmp);
    } else {
        tmp[0] = 0;
    }

    if (!tmp[0]) {
        strncpy(tmp, FALLBACK_LINE[w], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
    }

    strncpy(bm->text, tmp, sizeof(bm->text) - 1);
    bm->text[sizeof(bm->text) - 1] = 0;
    bm->showing            = true;
    bm->show_started_tick  = SDL_GetTicks();
}

void boss_monologue_init(BossMonologue *bm)
{
    if (!bm) return;
    memset(bm, 0, sizeof(*bm));
    bm->show_wagon = -1;
}

void boss_monologue_reset_run(BossMonologue *bm)
{
    if (!bm) return;
    memset(bm->fired_for_wagon, 0, sizeof(bm->fired_for_wagon));
    bm->showing    = false;
    bm->pending    = false;
    bm->text[0]    = 0;
    bm->show_wagon = -1;
}

static int find_lone_low_enforcer(const Background *bg)
{
    int alive = 0;
    int last_enforcer_idx = -1;
    int low_hp_enforcer_idx = -1;
    for (int i = 0; i < bg->nbEnnemis && i < MAX_ENNEMIS; i++) {
        const Ennemi *e = &bg->ennemis[i];
        if (!e->actif || e->etat == ES_DEAD || e->wagon != bg->wagonActif) continue;
        alive++;
        if (e->type == ET_ENFORCER) {
            last_enforcer_idx = i;
            int max_hp = ENFORCER_HP * 2;
            if (e->hp * 100 <= max_hp * BOSS_LOW_HP_PCT) {
                low_hp_enforcer_idx = i;
            }
        }
    }
    if (alive == 1 && last_enforcer_idx >= 0) return last_enforcer_idx;
    if (low_hp_enforcer_idx >= 0)              return low_hp_enforcer_idx;
    return -1;
}

void boss_monologue_update(BossMonologue *bm,
                            Background *bg,
                            const struct GameplayState *gp,
                            Uint32 game_start_tick,
                            Uint32 now_tick,
                            LLMQueue *q)
{
    if (!bm || !bg) return;

    if (bm->showing && now_tick - bm->show_started_tick >= BOSS_SHOW_MS) {
        bm->showing = false;
    }

    if (bm->pending && now_tick - bm->request_tick >= BOSS_REQUEST_GRACE) {
        bm->pending = false;
    }

    if (bm->showing || bm->pending) return;

    int w = bg->wagonActif;
    if (w < 0 || w >= BOSS_MONO_WAGONS) return;
    if (bm->fired_for_wagon[w]) return;

    int idx = find_lone_low_enforcer(bg);
    if (idx < 0) return;
    if (!q || !llm_queue_has_capacity(q)) return;

    int kills = gp ? gp->enemiesKilled : 0;
    int wagons_cleared = w;
    Uint32 elapsed_ms = now_tick > game_start_tick ?
                        (now_tick - game_start_tick) : 0;
    int seconds = (int)(elapsed_ms / 1000);
    int mm = seconds / 60;
    int ss = seconds % 60;

    char prompt[1700];
    snprintf(prompt, sizeof(prompt),
        "<start_of_turn>user\n"
        "You write a short, dramatic boss monologue for an enemy in a Snowpiercer-themed action game. "
        "Reply in English, in 1-2 short sentences, MAX 32 words total. "
        "No preamble, no quotes, no asterisks, no labels. Stay in character as the boss.\n\n"
        "Speaker: a heavily-armored enforcer, the last of Wilford's lieutenants in this car. "
        "He is %s, fighting in %s.\n\n"
        "The rebel player has so far:\n"
        "- killed %d of his fellow enforcers and guards\n"
        "- cleared %d wagon%s of the train\n"
        "- been on this run for %d minute%s and %d seconds\n\n"
        "Reference at least one of those facts naturally in his words. "
        "Make it threatening but theatrical, like a final-stand villain.<end_of_turn>\n"
        "<start_of_turn>model\n",
        (idx >= 0 && bg->ennemis[idx].hp * 100 <= ENFORCER_HP * 2 * BOSS_LOW_HP_PCT)
            ? "wounded and furious" : "the last of his squad",
        WAGON_NAME[w],
        kills,
        wagons_cleared, wagons_cleared == 1 ? "" : "s",
        mm, mm == 1 ? "" : "s", ss);

    bm->show_wagon  = w;
    bm->pending     = true;
    bm->request_tick = now_tick;
    bm->fired_for_wagon[w] = true;

    if (!llm_queue_submit(q, LLM_PRIO_LOW, prompt, 100, 0.85f,
                           on_boss_response, bm)) {
        bm->pending = false;
        bm->fired_for_wagon[w] = false;
    }
}

static int compute_alpha(Uint32 elapsed)
{
    if (elapsed < BOSS_FADE_MS) {
        return (int)((elapsed * 255u) / BOSS_FADE_MS);
    }
    if (elapsed > BOSS_SHOW_MS - BOSS_FADE_MS) {
        Uint32 left = BOSS_SHOW_MS - elapsed;
        return (int)((left * 255u) / BOSS_FADE_MS);
    }
    return 255;
}

static int wrap_text(const char *src, char lines[4][BOSS_MONO_TEXT_MAX],
                      TTF_Font *font, int max_w)
{
    int n = 0;
    if (!src || !*src || !font) return 0;
    const char *p = src;
    while (*p && n < 4) {
        char  cur[BOSS_MONO_TEXT_MAX];
        int   cur_len = 0;
        cur[0] = 0;
        const char *word = p;
        while (*p && n < 4) {
            const char *next = p;
            while (*next && *next != ' ' && *next != '\n') next++;
            int wlen = (int)(next - p);
            char tentative[BOSS_MONO_TEXT_MAX];
            int sep = (cur_len > 0) ? 1 : 0;
            int tl = cur_len + sep + wlen;
            if (tl >= (int)sizeof(tentative)) tl = (int)sizeof(tentative) - 1;
            memcpy(tentative, cur, cur_len);
            if (sep) tentative[cur_len] = ' ';
            int copy_w = wlen;
            if (cur_len + sep + wlen >= (int)sizeof(tentative))
                copy_w = (int)sizeof(tentative) - cur_len - sep - 1;
            if (copy_w < 0) copy_w = 0;
            memcpy(tentative + cur_len + sep, p, copy_w);
            tentative[cur_len + sep + copy_w] = 0;

            int tw = 0, th = 0;
            TTF_SizeUTF8(font, tentative, &tw, &th);
            if (tw > max_w && cur_len > 0) break;

            memcpy(cur, tentative, strlen(tentative) + 1);
            cur_len = (int)strlen(cur);
            p = next;
            if (*p == '\n') { p++; word = p; break; }
            if (*p == ' ')  p++;
            word = p;
        }
        if (cur_len > 0) {
            strncpy(lines[n], cur, BOSS_MONO_TEXT_MAX - 1);
            lines[n][BOSS_MONO_TEXT_MAX - 1] = 0;
            n++;
        }
        if (!*p) break;
        (void)word;
    }
    return n;
}

void boss_monologue_render(const BossMonologue *bm, SDL_Renderer *r,
                            UIFontPack *fonts, UITextCache *cache)
{
    if (!bm || !r || !fonts) return;
    if (!bm->showing || !bm->text[0]) return;

    Uint32 elapsed = SDL_GetTicks() - bm->show_started_tick;
    int alpha = compute_alpha(elapsed);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    int box_w = 720;
    int box_h = 130;
    int box_x = 40;
    int box_y = 130;
    SDL_Rect box = { box_x, box_y, box_w, box_h };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 30, 6, 10, (Uint8)((alpha * 230) / 255));
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 230, 60, 60, (Uint8)alpha);
    SDL_RenderDrawRect(r, &box);
    SDL_Rect inner = { box.x + 2, box.y + 2, box.w - 4, box.h - 4 };
    SDL_SetRenderDrawColor(r, 90, 14, 14, (Uint8)alpha);
    SDL_RenderDrawRect(r, &inner);

    int strip_w = 8;
    SDL_Rect lstrip = { box.x + 6, box.y + 8, strip_w, box.h - 16 };
    SDL_Rect rstrip = { box.x + box.w - 6 - strip_w, box.y + 8, strip_w, box.h - 16 };
    SDL_SetRenderDrawColor(r, 200, 30, 30, (Uint8)alpha);
    SDL_RenderFillRect(r, &lstrip);
    SDL_RenderFillRect(r, &rstrip);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    TTF_Font *fHeader = fonts->bebasHeader ? fonts->bebasHeader : fonts->fallback;
    TTF_Font *fBody   = fonts->story       ? fonts->story       : fonts->fallback;

    SDL_Color crimson = {255, 100, 100, (Uint8)alpha};
    SDL_Color cream   = {245, 230, 215, (Uint8)alpha};

    ui_draw_text_centered((UITextCache*)cache, r, fHeader,
                          "WILFORD'S ENFORCER", crimson,
                          box.x + box.w / 2, box.y + 6);

    char lines[4][BOSS_MONO_TEXT_MAX];
    int  nl = wrap_text(bm->text, lines, fBody, box_w - 80);
    int  start_y = box.y + 44;
    int  line_h  = 22;
    if (nl > 3) nl = 3;
    if (nl == 1) start_y = box.y + 60;
    else if (nl == 2) start_y = box.y + 50;
    for (int i = 0; i < nl; i++) {
        ui_draw_text_centered((UITextCache*)cache, r, fBody, lines[i], cream,
                              box.x + box.w / 2, start_y + i * line_h);
    }
}

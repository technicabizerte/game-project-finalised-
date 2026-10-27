#include "npc_dialogue.h"
#include "background.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DLG_BOX_W      640
#define DLG_BOX_H      230
#define DLG_PAD        18
#define DLG_INPUT_H    36
#define DLG_LINE_H     22
#define DLG_TEXT_W    (DLG_BOX_W - 2 * DLG_PAD)

static const char *SYS_PROMPT_TEMPLATE =
    "You are %s, a calm, helpful ally on the Snowpiercer train. "
    "The world outside is a frozen wasteland; this train is humanity's last refuge. "
    "You are working to unlock the door to the next wagon while the player clears the path. "
    "Speak like a co-op partner over comms. Reply in 1 short sentence. "
    "Stay in character. Never reveal you are an AI. Never use asterisks.\n\n"
    "If the player asks for guidance or help, incorporate the ADVICE TO GIVE below in your reply, in your own words. "
    "If the player asks a general question (about you, the train, the world, themselves), answer naturally and briefly without forcing the advice in.";

static const char *WAGON_LABELS[] = {
    "the tail section (the poor cars)",
    "the aquarium car",
    "the greenhouse / garden car",
    "the industrial car",
    "the luxury car"
};
static const int WAGON_LABELS_N =
    (int)(sizeof(WAGON_LABELS) / sizeof(WAGON_LABELS[0]));

void npc_dialogue_init(NPCDialogue *dlg, const char *npc_name, const char *npc_role)
{
    memset(dlg, 0, sizeof(*dlg));
    if (npc_name) {
        strncpy(dlg->npc_name, npc_name, sizeof(dlg->npc_name) - 1);
    } else {
        strcpy(dlg->npc_name, "NPC");
    }
    if (npc_role) {
        strncpy(dlg->npc_role, npc_role, sizeof(dlg->npc_role) - 1);
    } else {
        strcpy(dlg->npc_role, "a survivor");
    }
    strcpy(dlg->player_name, "the rebel");
    strcpy(dlg->quest, "moving forward through the train");
    dlg->kronole_target = 3;
}

void npc_dialogue_set_context(NPCDialogue *dlg,
                               const char *player_name,
                               int player_wagon,
                               const char *quest)
{
    if (player_name && player_name[0]) {
        strncpy(dlg->player_name, player_name, sizeof(dlg->player_name) - 1);
        dlg->player_name[sizeof(dlg->player_name) - 1] = 0;
    }
    dlg->player_wagon = player_wagon;
    if (quest && quest[0]) {
        strncpy(dlg->quest, quest, sizeof(dlg->quest) - 1);
        dlg->quest[sizeof(dlg->quest) - 1] = 0;
    }
}

void npc_dialogue_set_state(NPCDialogue *dlg,
                             int alive_enemies,
                             int kronole_count,
                             int kronole_target,
                             int door_open,
                             int door_unlocking)
{
    if (!dlg) return;
    dlg->alive_enemies   = alive_enemies;
    dlg->kronole_count   = kronole_count;
    dlg->kronole_target  = kronole_target > 0 ? kronole_target : 3;
    dlg->door_open       = door_open ? 1 : 0;
    dlg->door_unlocking  = door_unlocking ? 1 : 0;
}

void npc_dialogue_open(NPCDialogue *dlg)
{
    dlg->active = true;
    dlg->waiting = false;
    dlg->typing  = true;
    dlg->user_input[0] = 0;
    dlg->user_input_len = 0;
    if (dlg->response[0] == 0) {
        snprintf(dlg->response, sizeof(dlg->response),
                 "[%s glances up from his work.]",
                 dlg->npc_name);
    }
    SDL_StartTextInput();
}

void npc_dialogue_close(NPCDialogue *dlg)
{
    dlg->active = false;
    dlg->waiting = false;
    dlg->typing  = false;
    SDL_StopTextInput();
}

bool npc_dialogue_is_active(const NPCDialogue *dlg)
{
    return dlg && dlg->active;
}

void npc_dialogue_handle_textinput(NPCDialogue *dlg, const char *utf8_text)
{
    if (!dlg->active || !dlg->typing || dlg->waiting) return;
    if (!utf8_text) return;
    size_t add = strlen(utf8_text);
    size_t cap = sizeof(dlg->user_input) - 1;
    if ((size_t)dlg->user_input_len + add > cap) {
        add = cap - (size_t)dlg->user_input_len;
    }
    if (add == 0) return;
    memcpy(dlg->user_input + dlg->user_input_len, utf8_text, add);
    dlg->user_input_len += (int)add;
    dlg->user_input[dlg->user_input_len] = 0;
}

static void wagon_label(int wagon, char *out, size_t cap)
{
    if (wagon >= 0 && wagon < WAGON_LABELS_N) {
        snprintf(out, cap, "wagon %d (%s)", wagon, WAGON_LABELS[wagon]);
    } else {
        snprintf(out, cap, "wagon %d", wagon);
    }
}

static void compute_advice(const NPCDialogue *dlg, char *out, size_t cap)
{
    int needed = dlg->kronole_target - dlg->kronole_count;
    if (needed < 0) needed = 0;

    if (dlg->alive_enemies > 0) {
        snprintf(out, cap,
                 "Fight the %d enemies in this wagon first - I will handle the door.",
                 dlg->alive_enemies);
    } else if (dlg->door_open) {
        snprintf(out, cap,
                 "The door is open - push forward to the next wagon now.");
    } else if (dlg->door_unlocking) {
        snprintf(out, cap,
                 "I am unlocking the door right now - cover me and watch for enemies.");
    } else if (needed > 0) {
        snprintf(out, cap,
                 "Find %d more Kronole pickup%s in this wagon - we need %d total to unlock the door.",
                 needed, needed == 1 ? "" : "s", dlg->kronole_target);
    } else {
        snprintf(out, cap,
                 "We have enough Kronole - come over to the door and trigger the unlock.");
    }
}

static void build_situation_block(const NPCDialogue *dlg, char *out, size_t cap)
{
    char wagon_buf[80];
    wagon_label(dlg->player_wagon, wagon_buf, sizeof(wagon_buf));
    char advice[200];
    compute_advice(dlg, advice, sizeof(advice));

    snprintf(out, cap,
             "The player is in %s. You are working on the door.\n\n"
             "ADVICE TO GIVE: \"%s\"\n\n",
             wagon_buf, advice);
}

static void build_prompt(const NPCDialogue *dlg,
                          const char *new_user_msg,
                          char *out, size_t cap)
{
    size_t off = 0;
    int n;
    char situation[400];
    build_situation_block(dlg, situation, sizeof(situation));

    n = snprintf(out + off, cap - off, "<start_of_turn>user\n");
    if (n < 0) return;
    off += (size_t)n;

    n = snprintf(out + off, cap - off, SYS_PROMPT_TEMPLATE, dlg->npc_name);
    if (n < 0) return;
    off += (size_t)n;

    n = snprintf(out + off, cap - off, "\n\n%s", situation);
    if (n < 0) return;
    off += (size_t)n;

    if (dlg->n_history > 0) {
        n = snprintf(out + off, cap - off,
                     "Player asks: \"%s\"<end_of_turn>\n"
                     "<start_of_turn>model\n%s<end_of_turn>\n",
                     dlg->hist_user[0], dlg->hist_npc[0]);
        if (n < 0) return;
        off += (size_t)n;

        for (int i = 1; i < dlg->n_history; i++) {
            n = snprintf(out + off, cap - off,
                         "<start_of_turn>user\nPlayer asks: \"%s\"<end_of_turn>\n"
                         "<start_of_turn>model\n%s<end_of_turn>\n",
                         dlg->hist_user[i], dlg->hist_npc[i]);
            if (n < 0) return;
            off += (size_t)n;
        }

        n = snprintf(out + off, cap - off,
                     "<start_of_turn>user\nPlayer asks: \"%s\"<end_of_turn>\n"
                     "<start_of_turn>model\n",
                     new_user_msg);
        if (n < 0) return;
        off += (size_t)n;
    } else {
        n = snprintf(out + off, cap - off,
                     "Player asks: \"%s\"<end_of_turn>\n"
                     "<start_of_turn>model\n",
                     new_user_msg);
        if (n < 0) return;
        off += (size_t)n;
    }
    (void)off;
}

static void push_history(NPCDialogue *dlg, const char *user_msg, const char *npc_msg)
{
    if (dlg->n_history >= NPC_DLG_HISTORY_MAX) {
        for (int i = 1; i < NPC_DLG_HISTORY_MAX; i++) {
            strcpy(dlg->hist_user[i - 1], dlg->hist_user[i]);
            strcpy(dlg->hist_npc [i - 1], dlg->hist_npc [i]);
        }
        dlg->n_history = NPC_DLG_HISTORY_MAX - 1;
    }
    int slot = dlg->n_history++;
    strncpy(dlg->hist_user[slot], user_msg ? user_msg : "",
            NPC_DLG_USER_INPUT_MAX);
    dlg->hist_user[slot][NPC_DLG_USER_INPUT_MAX] = 0;
    strncpy(dlg->hist_npc[slot], npc_msg ? npc_msg : "",
            NPC_DLG_RESPONSE_MAX - 1);
    dlg->hist_npc[slot][NPC_DLG_RESPONSE_MAX - 1] = 0;
}

void npc_dialogue_on_llm_response(bool ok, const char *resp, void *userdata)
{
    NPCDialogue *dlg = (NPCDialogue*)userdata;
    if (!dlg) return;
    dlg->waiting = false;
    if (ok && resp && resp[0]) {
        strncpy(dlg->response, resp, sizeof(dlg->response) - 1);
        dlg->response[sizeof(dlg->response) - 1] = 0;
        if (dlg->n_history > 0 && dlg->hist_npc[dlg->n_history - 1][0] == 0) {
            strncpy(dlg->hist_npc[dlg->n_history - 1], dlg->response,
                    NPC_DLG_RESPONSE_MAX - 1);
            dlg->hist_npc[dlg->n_history - 1][NPC_DLG_RESPONSE_MAX - 1] = 0;
        }
    } else {
        snprintf(dlg->response, sizeof(dlg->response),
                 "[the line crackles] %s", resp ? resp : "(no response)");
    }
}

bool npc_dialogue_handle_keydown(NPCDialogue *dlg, SDL_Keycode key,
                                  LLMQueue *q)
{
    if (!dlg->active) return false;

    if (key == SDLK_ESCAPE) {
        npc_dialogue_close(dlg);
        return true;
    }
    if (dlg->waiting) {
        return true;
    }
    if (key == SDLK_BACKSPACE) {
        if (dlg->user_input_len > 0) {
            int i = dlg->user_input_len - 1;
            while (i > 0 && (((unsigned char)dlg->user_input[i] & 0xC0) == 0x80)) {
                i--;
            }
            dlg->user_input_len = i;
            dlg->user_input[i] = 0;
        }
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (dlg->user_input_len == 0) return true;

        push_history(dlg, dlg->user_input, "");

        char prompt_buf[6144];
        build_prompt(dlg, dlg->user_input, prompt_buf, sizeof(prompt_buf));

        if (q && llm_queue_submit(q, LLM_PRIO_HIGH, prompt_buf, 80, 0.7f,
                                  npc_dialogue_on_llm_response, dlg)) {
            dlg->waiting = true;
            dlg->response[0] = 0;
            dlg->user_input[0] = 0;
            dlg->user_input_len = 0;
        } else {
            snprintf(dlg->response, sizeof(dlg->response),
                     q ? "[the radio crackles - line's jammed, try again]"
                       : "[the radio is silent - LLM unavailable]");
        }
        return true;
    }
    return true;
}

void npc_dialogue_update(NPCDialogue *dlg, Uint32 now_tick)
{
    if (!dlg->active) return;
    if (dlg->waiting && now_tick - dlg->last_anim_tick > 350u) {
        dlg->last_anim_tick = now_tick;
        dlg->anim_phase = (dlg->anim_phase + 1) % 4;
    }
}

static void wrap_render(SDL_Renderer *r, UIFontPack *fonts,
                         UITextCache *cache,
                         TTF_Font *font, SDL_Color col,
                         const char *text, int x, int y, int w, int max_lines)
{
    (void)fonts;
    if (!text || !*text) return;
    char line[160];
    int  line_n = 0;
    int  cur_y  = y;
    int  lines_drawn = 0;
    const char *p = text;

    while (*p && lines_drawn < max_lines) {
        line_n = 0;
        line[0] = 0;
        const char *word = p;
        const char *next = p;

        while (*next && lines_drawn < max_lines) {
            while (*next && *next != ' ' && *next != '\n') next++;
            int wlen = (int)(next - word);
            if (wlen > (int)sizeof(line) - 2) wlen = (int)sizeof(line) - 2;

            char tentative[160];
            int sep = (line_n > 0) ? 1 : 0;
            int tl = line_n + sep + wlen;
            if (tl >= (int)sizeof(tentative)) tl = (int)sizeof(tentative) - 1;
            memcpy(tentative, line, line_n);
            if (sep) tentative[line_n] = ' ';
            memcpy(tentative + line_n + sep, word, wlen);
            tentative[tl] = 0;

            int tw = 0, th = 0;
            TTF_SizeUTF8(font, tentative, &tw, &th);

            if (tw > w && line_n > 0) {
                break;
            }
            memcpy(line, tentative, tl + 1);
            line_n = tl;

            if (*next == '\n') {
                next++;
                word = next;
                break;
            }
            if (*next == ' ') next++;
            word = next;
        }
        if (line_n > 0) {
            ui_draw_text(cache, r, font, line, col, x, cur_y);
            cur_y += DLG_LINE_H;
            lines_drawn++;
        }
        p = word;
        if (*p == 0) break;
    }
}

void npc_dialogue_render(const NPCDialogue *dlg, SDL_Renderer *r,
                          UIFontPack *fonts, UITextCache *cache)
{
    if (!dlg->active) return;

    int box_x = (W_ECRAN - DLG_BOX_W) / 2;
    int box_y = H_ECRAN - DLG_BOX_H - 18;
    SDL_Rect box = { box_x, box_y, DLG_BOX_W, DLG_BOX_H };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 200, 150, 50, 255);
    SDL_RenderDrawRect(r, &box);
    SDL_Rect inner = { box.x + 1, box.y + 1, box.w - 2, box.h - 2 };
    SDL_SetRenderDrawColor(r, 90, 60, 20, 255);
    SDL_RenderDrawRect(r, &inner);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    SDL_Color amber  = {255, 200, 90, 255};
    SDL_Color cream  = {235, 225, 200, 255};
    SDL_Color pale   = {180, 180, 200, 255};
    SDL_Color gray   = {130, 130, 140, 255};

    TTF_Font *fHeader = fonts->bebasHeader ? fonts->bebasHeader : fonts->fallback;
    TTF_Font *fBody   = fonts->story       ? fonts->story       : fonts->fallback;
    TTF_Font *fInput  = fonts->mono        ? fonts->mono        : fonts->fallback;
    TTF_Font *fSmall  = fonts->novaLabel   ? fonts->novaLabel   : fonts->fallback;

    char header[80];
    snprintf(header, sizeof(header), "%s — %s",
             dlg->npc_name, dlg->npc_role);
    ui_draw_text(cache, r, fHeader, header, amber,
                 box.x + DLG_PAD, box.y + DLG_PAD - 4);

    int response_y = box.y + DLG_PAD + 30;
    int response_h_lines = 4;

    if (dlg->waiting) {
        const char *dots[] = {"", ".", "..", "..."};
        char buf[8];
        snprintf(buf, sizeof(buf), "%s", dots[dlg->anim_phase & 3]);
        ui_draw_text(cache, r, fBody, buf, pale,
                     box.x + DLG_PAD, response_y);
    } else if (dlg->response[0]) {
        wrap_render(r, fonts, cache, fBody, cream,
                    dlg->response,
                    box.x + DLG_PAD, response_y, DLG_TEXT_W,
                    response_h_lines);
    }

    int input_y = box.y + box.h - DLG_INPUT_H - DLG_PAD;
    SDL_Rect input_rect = { box.x + DLG_PAD, input_y,
                             DLG_TEXT_W, DLG_INPUT_H };

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 30, 30, 40, 220);
    SDL_RenderFillRect(r, &input_rect);
    SDL_SetRenderDrawColor(r, 120, 90, 40, 255);
    SDL_RenderDrawRect(r, &input_rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    char input_with_cursor[NPC_DLG_USER_INPUT_MAX + 8];
    if (dlg->waiting) {
        snprintf(input_with_cursor, sizeof(input_with_cursor),
                 "%s", dlg->user_input);
        ui_draw_text(cache, r, fInput,
                     input_with_cursor[0] ? input_with_cursor : "(waiting...)",
                     gray,
                     input_rect.x + 8, input_rect.y + 8);
    } else {
        Uint32 t = SDL_GetTicks();
        const char *cur = ((t / 500) % 2) ? "_" : " ";
        snprintf(input_with_cursor, sizeof(input_with_cursor),
                 "%s%s", dlg->user_input, cur);
        if (input_with_cursor[0] == cur[0] && input_with_cursor[1] == 0) {
            ui_draw_text(cache, r, fInput, "type your reply...",
                         gray, input_rect.x + 8, input_rect.y + 8);
        } else {
            ui_draw_text(cache, r, fInput, input_with_cursor,
                         cream, input_rect.x + 8, input_rect.y + 8);
        }
    }

    ui_draw_text(cache, r, fSmall,
                 "[Enter] send   [Backspace] delete   [Esc] leave",
                 gray, box.x + DLG_PAD, box.y + box.h - 18);
}

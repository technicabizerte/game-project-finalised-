#ifndef NPC_DIALOGUE_H
#define NPC_DIALOGUE_H

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stddef.h>

#include "ui.h"
#include "llm.h"
#include "llm_queue.h"

#define NPC_DLG_HISTORY_MAX     6
#define NPC_DLG_USER_INPUT_MAX  240
#define NPC_DLG_RESPONSE_MAX    1024
#define NPC_DLG_NAME_MAX        32
#define NPC_DLG_ROLE_MAX        96

typedef struct {
    bool        active;
    bool        waiting;
    bool        typing;

    char        response[NPC_DLG_RESPONSE_MAX];
    char        user_input[NPC_DLG_USER_INPUT_MAX + 1];
    int         user_input_len;

    Uint32      last_anim_tick;
    int         anim_phase;

    int         n_history;
    char        hist_user[NPC_DLG_HISTORY_MAX][NPC_DLG_USER_INPUT_MAX + 1];
    char        hist_npc [NPC_DLG_HISTORY_MAX][NPC_DLG_RESPONSE_MAX];

    char        npc_name[NPC_DLG_NAME_MAX];
    char        npc_role[NPC_DLG_ROLE_MAX];

    int         player_wagon;
    char        player_name[NPC_DLG_NAME_MAX];
    char        quest[NPC_DLG_ROLE_MAX];

    int         alive_enemies;
    int         kronole_count;
    int         kronole_target;
    int         door_open;
    int         door_unlocking;
} NPCDialogue;

void npc_dialogue_init(NPCDialogue *dlg, const char *npc_name, const char *npc_role);

void npc_dialogue_set_context(NPCDialogue *dlg,
                               const char *player_name,
                               int player_wagon,
                               const char *quest);

void npc_dialogue_set_state(NPCDialogue *dlg,
                             int alive_enemies,
                             int kronole_count,
                             int kronole_target,
                             int door_open,
                             int door_unlocking);

void npc_dialogue_open(NPCDialogue *dlg);
void npc_dialogue_close(NPCDialogue *dlg);
bool npc_dialogue_is_active(const NPCDialogue *dlg);

void npc_dialogue_handle_textinput(NPCDialogue *dlg, const char *utf8_text);
/* Submits the player's line to the shared LLM queue at HIGH priority, so it
 * jumps ahead of background flavour text (barker / boss / taunts) instead of
 * being dropped when the single worker is mid-request. */
bool npc_dialogue_handle_keydown(NPCDialogue *dlg, SDL_Keycode key,
                                  LLMQueue *q);

void npc_dialogue_on_llm_response(bool ok, const char *resp, void *userdata);

void npc_dialogue_update(NPCDialogue *dlg, Uint32 now_tick);

void npc_dialogue_render(const NPCDialogue *dlg, SDL_Renderer *r,
                          UIFontPack *fonts, UITextCache *cache);

#endif

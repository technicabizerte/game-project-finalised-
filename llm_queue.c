#include "llm_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LLM_QUEUE_SLOTS    8
#define LLM_QUEUE_PROMPT   8192

typedef struct {
    bool        used;
    LLMPriority prio;
    int         submit_seq;
    int         n_predict;
    float       temperature;
    LLMCallback user_cb;
    void       *user_ud;
    char        prompt[LLM_QUEUE_PROMPT];
} QSlot;

struct LLMQueue {
    LLMSystem *llm;
    QSlot      slots[LLM_QUEUE_SLOTS];
    int        in_flight;
    int        next_seq;
};

static int pick_next(const LLMQueue *q)
{
    int best = -1;
    int best_prio = 99;
    int best_seq  = 0x7fffffff;
    for (int i = 0; i < LLM_QUEUE_SLOTS; i++) {
        const QSlot *s = &q->slots[i];
        if (!s->used) continue;
        if ((int)s->prio < best_prio ||
            ((int)s->prio == best_prio && s->submit_seq < best_seq)) {
            best      = i;
            best_prio = (int)s->prio;
            best_seq  = s->submit_seq;
        }
    }
    return best;
}

static void on_underlying_done(bool ok, const char *response, void *userdata)
{
    LLMQueue *q = (LLMQueue*)userdata;
    if (!q || q->in_flight < 0) return;

    QSlot *s = &q->slots[q->in_flight];
    LLMCallback cb = s->user_cb;
    void *ud       = s->user_ud;

    s->used    = false;
    s->user_cb = NULL;
    s->user_ud = NULL;
    q->in_flight = -1;

    if (cb) cb(ok, response, ud);
}

LLMQueue *llm_queue_create(LLMSystem *llm)
{
    LLMQueue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->llm       = llm;
    q->in_flight = -1;
    q->next_seq  = 0;
    return q;
}

void llm_queue_destroy(LLMQueue *q)
{
    if (!q) return;
    free(q);
}

bool llm_queue_has_capacity(const LLMQueue *q)
{
    if (!q) return false;
    for (int i = 0; i < LLM_QUEUE_SLOTS; i++) {
        if (!q->slots[i].used) return true;
    }
    return false;
}

bool llm_queue_submit(LLMQueue *q,
                      LLMPriority prio,
                      const char *prompt,
                      int n_predict,
                      float temperature,
                      LLMCallback cb,
                      void *userdata)
{
    if (!q || !q->llm || !prompt) return false;

    int free_slot = -1;
    for (int i = 0; i < LLM_QUEUE_SLOTS; i++) {
        if (!q->slots[i].used) { free_slot = i; break; }
    }
    if (free_slot < 0) return false;

    QSlot *s = &q->slots[free_slot];
    s->used        = true;
    s->prio        = prio;
    s->submit_seq  = q->next_seq++;
    s->n_predict   = n_predict > 0 ? n_predict : 60;
    s->temperature = temperature;
    s->user_cb     = cb;
    s->user_ud     = userdata;
    size_t pl = strlen(prompt);
    if (pl >= sizeof(s->prompt)) pl = sizeof(s->prompt) - 1;
    memcpy(s->prompt, prompt, pl);
    s->prompt[pl] = 0;
    return true;
}

void llm_queue_pump(LLMQueue *q)
{
    if (!q || !q->llm) return;
    if (q->in_flight >= 0) return;
    if (llm_status(q->llm) == LLM_BUSY) return;

    int idx = pick_next(q);
    if (idx < 0) return;

    QSlot *s = &q->slots[idx];
    if (!llm_send(q->llm, s->prompt, s->n_predict, s->temperature,
                  on_underlying_done, q)) {
        return;
    }
    q->in_flight = idx;
}

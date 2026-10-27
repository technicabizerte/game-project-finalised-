#ifndef LLM_QUEUE_H
#define LLM_QUEUE_H

#include <stdbool.h>
#include "llm.h"

typedef enum {
    LLM_PRIO_HIGH = 0,
    LLM_PRIO_LOW  = 1
} LLMPriority;

typedef struct LLMQueue LLMQueue;

LLMQueue *llm_queue_create(LLMSystem *llm);
void      llm_queue_destroy(LLMQueue *q);

bool llm_queue_submit(LLMQueue *q,
                      LLMPriority prio,
                      const char *prompt,
                      int n_predict,
                      float temperature,
                      LLMCallback cb,
                      void *userdata);

void llm_queue_pump(LLMQueue *q);

bool llm_queue_has_capacity(const LLMQueue *q);

#endif

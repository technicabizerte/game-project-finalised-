#ifndef LLM_H
#define LLM_H

#include <stdbool.h>
#include <stddef.h>

typedef struct LLMSystem LLMSystem;

typedef enum {
    LLM_IDLE  = 0,
    LLM_BUSY  = 1,
    LLM_DONE  = 2,
    LLM_ERROR = 3
} LLMStatus;

typedef void (*LLMCallback)(bool ok, const char *response, void *userdata);

LLMSystem *llm_init(const char *server_binary,
                    const char *model_path,
                    const char *host,
                    int port);

LLMStatus llm_status(const LLMSystem *llm);

bool llm_send(LLMSystem *llm,
              const char *prompt,
              int n_predict,
              float temperature,
              LLMCallback on_done,
              void *userdata);

void llm_pump(LLMSystem *llm);

void llm_shutdown(LLMSystem *llm);

#endif

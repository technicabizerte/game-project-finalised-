#ifndef QUIZ_H
#define QUIZ_H

#include <SDL2/SDL.h>
#include "ui.h"
#include "audio.h"

#define QUIZ_QUESTIONS_MAX   16
#define QUIZ_ANSWER_COUNT    4
#define QUIZ_PUZZLE_MAX_WORDS 6

typedef struct {
    const char *question;
    const char *answers[QUIZ_ANSWER_COUNT];
    int         correct;
} QuizQuestion;

typedef struct {
    const char *prompt;
    const char *words[QUIZ_PUZZLE_MAX_WORDS];
    int         nbWords;
} QuizPuzzle;

typedef enum {
    QUIZ_PHASE_QUESTION = 0,
    QUIZ_PHASE_FEEDBACK,
    QUIZ_PHASE_PUZZLE,
    QUIZ_PHASE_PUZZLE_DONE,
    QUIZ_PHASE_FINISHED
} QuizPhase;

typedef struct {
    int         questionIdx;
    int         puzzleIdx;
    QuizPhase   phase;

    int         selectedAnswer;
    int         clickedAnswer;
    int         correctRevealed;
    Uint32      phaseStartTick;

    int         puzzleOrder[QUIZ_PUZZLE_MAX_WORDS];
    int         puzzleSlotFilled[QUIZ_PUZZLE_MAX_WORDS];
    int         puzzleWordPlaced[QUIZ_PUZZLE_MAX_WORDS];
    int         puzzleNextSlot;

    int         bonusScore;

    int         timerTotalMs;
    Uint32      timerStartTick;
} QuizState;

void quiz_init(QuizState *q);
void quiz_start(QuizState *q, int questionIdx, int puzzleIdx);

int  quiz_is_finished(const QuizState *q);

void quiz_get_data(const QuizQuestion **qs, int *nq,
                   const QuizPuzzle **ps, int *np);

int  quiz_handle_mouse_motion(QuizState *q, SDL_Renderer *r,
                               int winX, int winY);
int  quiz_handle_mouse_click(QuizState *q, SDL_Renderer *r,
                              int winX, int winY, AudioSystem *audio);
int  quiz_handle_key(QuizState *q, SDL_Keycode key, AudioSystem *audio);

void quiz_update(QuizState *q, AudioSystem *audio);

void quiz_render(const QuizState *q, SDL_Renderer *r,
                 UIAssets *assets, UIFontPack *fonts, UITextCache *cache);

#endif

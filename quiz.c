#include "quiz.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const QuizQuestion g_questions[] = {
    {
        "Qui est le realisateur de Snowpiercer (2013) ?",
        {"Park Chan-wook", "Bong Joon-ho", "Kim Jee-woon", "Hirokazu Kore-eda"},
        1
    },
    {
        "Quel est le nom du train dans le film ?",
        {"Le Transperceneige", "L'Arctique", "Le Dernier Wagon", "Le Brise-Glace"},
        0
    },
    {
        "Qui joue le role de Curtis, le heros ?",
        {"Tom Hardy", "Chris Evans", "Matt Damon", "Jake Gyllenhaal"},
        1
    },
    {
        "Que mangent les habitants de la queue du train ?",
        {"Des insectes compresses", "Des pommes de terre", "Des algues", "Des rats"},
        0
    },
    {
        "Quel personnage dirige le train ?",
        {"Wilford", "Gilliam", "Mason", "Namgoong"},
        0
    },
    {
        "Quelle drogue est utilisee dans le film ?",
        {"Heroine", "Kronole", "Ice-9", "Soma"},
        1
    },
    {
        "Qui joue le role de Mason ?",
        {"Tilda Swinton", "Octavia Spencer", "Kate Winslet", "Cate Blanchett"},
        0
    },
    {
        "Comment le monde est-il devenu gele ?",
        {"Guerre nucleaire", "Experience CW-7", "Eruption volcanique", "Impact d'asteroide"},
        1
    },
    {
        "De quelle nationalite est le realisateur ?",
        {"Japonaise", "Coreenne", "Chinoise", "Francaise"},
        1
    },
    {
        "Le film est adapte d'une bande dessinee...",
        {"Americaine", "Francaise", "Belge", "Japonaise"},
        1
    },
    {
        "Que decouvre Curtis sur le contenu des barres proteinees ?",
        {"Fait d'insectes", "Contient de la chair humaine", "Juste farine", "Synthetique pur"},
        0
    },
    {
        "Combien de temps le train met-il a faire le tour du monde ?",
        {"Un mois", "Six mois", "Un an", "Dix ans"},
        2
    }
};
static const int g_questionsCount = (int)(sizeof(g_questions)/sizeof(g_questions[0]));

static const QuizPuzzle g_puzzles[] = {
    { "Remettez la phrase dans l'ordre :",
      {"Le", "train", "tourne", "autour", "du", "monde"}, 6 },
    { "Remettez la phrase dans l'ordre :",
      {"Nous", "sommes", "tous", "des", "rouages"}, 5 },
    { "Remettez la phrase dans l'ordre :",
      {"La", "queue", "doit", "avancer"}, 4 },
    { "Remettez la phrase dans l'ordre :",
      {"Le", "moteur", "est", "sacre"}, 4 },
    { "Remettez la phrase dans l'ordre :",
      {"Le", "froid", "tue", "dehors"}, 4 }
};
static const int g_puzzlesCount = (int)(sizeof(g_puzzles)/sizeof(g_puzzles[0]));

#define QUIZ_ANSWER_BTN_W  320
#define QUIZ_ANSWER_BTN_H  48
#define QUIZ_ANSWER_GAP_X  20
#define QUIZ_ANSWER_GAP_Y  14
#define QUIZ_ANSWER_TOP    210

#define QUIZ_PUZZLE_WORD_W 100
#define QUIZ_PUZZLE_WORD_H 36
#define QUIZ_PUZZLE_GAP_X  8
#define QUIZ_PUZZLE_POOL_Y 260
#define QUIZ_PUZZLE_SLOT_Y 340

#define QUIZ_BTN_ANSWER_BASE 0
#define QUIZ_BTN_POOL_BASE   100
#define QUIZ_BTN_SLOT_BASE   200
#define QUIZ_BTN_SKIP        300

static void quiz_layout_answers(SDL_Rect out[QUIZ_ANSWER_COUNT])
{
    int rowW = QUIZ_ANSWER_BTN_W * 2 + QUIZ_ANSWER_GAP_X;
    int startX = (800 - rowW) / 2;
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        out[i].x = startX + col * (QUIZ_ANSWER_BTN_W + QUIZ_ANSWER_GAP_X);
        out[i].y = QUIZ_ANSWER_TOP + row * (QUIZ_ANSWER_BTN_H + QUIZ_ANSWER_GAP_Y);
        out[i].w = QUIZ_ANSWER_BTN_W;
        out[i].h = QUIZ_ANSWER_BTN_H;
    }
}

static const QuizPuzzle *quiz_layout_puzzle(const QuizState *q,
                                             SDL_Rect pool[QUIZ_PUZZLE_MAX_WORDS],
                                             SDL_Rect slot[QUIZ_PUZZLE_MAX_WORDS])
{
    const QuizPuzzle *p = &g_puzzles[q->puzzleIdx];
    int rowW = p->nbWords * QUIZ_PUZZLE_WORD_W + (p->nbWords - 1) * QUIZ_PUZZLE_GAP_X;
    int startX = (800 - rowW) / 2;
    for (int i = 0; i < p->nbWords; i++) {
        pool[i].x = startX + i * (QUIZ_PUZZLE_WORD_W + QUIZ_PUZZLE_GAP_X);
        pool[i].y = QUIZ_PUZZLE_POOL_Y;
        pool[i].w = QUIZ_PUZZLE_WORD_W;
        pool[i].h = QUIZ_PUZZLE_WORD_H;

        slot[i].x = startX + i * (QUIZ_PUZZLE_WORD_W + QUIZ_PUZZLE_GAP_X);
        slot[i].y = QUIZ_PUZZLE_SLOT_Y;
        slot[i].w = QUIZ_PUZZLE_WORD_W;
        slot[i].h = QUIZ_PUZZLE_WORD_H;
    }
    return p;
}

void quiz_init(QuizState *q)
{
    memset(q, 0, sizeof(*q));
    q->selectedAnswer = 0;
    q->clickedAnswer  = -1;
}

void quiz_get_data(const QuizQuestion **qs, int *nq,
                   const QuizPuzzle **ps, int *np)
{
    if (qs) *qs = g_questions;
    if (nq) *nq = g_questionsCount;
    if (ps) *ps = g_puzzles;
    if (np) *np = g_puzzlesCount;
}

static void quiz_scramble(QuizState *q)
{
    const QuizPuzzle *p = &g_puzzles[q->puzzleIdx];

    for (int i = 0; i < p->nbWords; i++) q->puzzleOrder[i] = i;

    unsigned seed = (unsigned)SDL_GetTicks() ^ (unsigned)q->puzzleIdx;
    for (int i = p->nbWords - 1; i > 0; i--) {
        seed = seed * 1664525u + 1013904223u;
        int j = (int)((seed >> 16) % (i + 1));
        int t = q->puzzleOrder[i];
        q->puzzleOrder[i] = q->puzzleOrder[j];
        q->puzzleOrder[j] = t;
    }

    int inPlace = 1;
    for (int i = 0; i < p->nbWords; i++)
        if (q->puzzleOrder[i] != i) { inPlace = 0; break; }
    if (inPlace && p->nbWords >= 2) {
        int t = q->puzzleOrder[0]; q->puzzleOrder[0] = q->puzzleOrder[1]; q->puzzleOrder[1] = t;
    }
    memset(q->puzzleSlotFilled, 0, sizeof(q->puzzleSlotFilled));
    memset(q->puzzleWordPlaced, 0, sizeof(q->puzzleWordPlaced));
    q->puzzleNextSlot = 0;
}

void quiz_start(QuizState *q, int questionIdx, int puzzleIdx)
{
    memset(q, 0, sizeof(*q));
    q->questionIdx    = questionIdx % g_questionsCount;
    q->puzzleIdx      = puzzleIdx   % g_puzzlesCount;
    q->phase          = QUIZ_PHASE_QUESTION;
    q->selectedAnswer = 0;
    q->clickedAnswer  = -1;
    q->correctRevealed = 0;
    q->phaseStartTick = SDL_GetTicks();
    q->timerTotalMs   = 15000;
    q->timerStartTick = SDL_GetTicks();
    q->bonusScore     = 0;
    quiz_scramble(q);
}

int quiz_is_finished(const QuizState *q) { return q->phase == QUIZ_PHASE_FINISHED; }

static void quiz_pick_answer(QuizState *q, int idx, AudioSystem *audio)
{
    if (q->phase != QUIZ_PHASE_QUESTION) return;
    if (idx < 0 || idx >= QUIZ_ANSWER_COUNT) return;
    q->clickedAnswer  = idx;
    q->phase          = QUIZ_PHASE_FEEDBACK;
    q->phaseStartTick = SDL_GetTicks();
    const QuizQuestion *Q = &g_questions[q->questionIdx];
    if (idx == Q->correct) {
        q->bonusScore += 500;
        if (audio) audio_play_sfx(audio, SFX_QUIZ_CORRECT);
    } else {
        if (audio) audio_play_sfx(audio, SFX_QUIZ_WRONG);
    }
}

int quiz_handle_key(QuizState *q, SDL_Keycode key, AudioSystem *audio)
{
    if (q->phase == QUIZ_PHASE_QUESTION) {
        if (key == SDLK_LEFT  || key == SDLK_a) {
            if (q->selectedAnswer % 2 == 1) q->selectedAnswer--;
            return 1;
        }
        if (key == SDLK_RIGHT || key == SDLK_d) {
            if (q->selectedAnswer % 2 == 0) q->selectedAnswer++;
            return 1;
        }
        if (key == SDLK_UP    || key == SDLK_w) {
            if (q->selectedAnswer >= 2) q->selectedAnswer -= 2;
            return 1;
        }
        if (key == SDLK_DOWN  || key == SDLK_s) {
            if (q->selectedAnswer < 2) q->selectedAnswer += 2;
            return 1;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            quiz_pick_answer(q, q->selectedAnswer, audio);
            return 1;
        }
        if (key >= SDLK_1 && key <= SDLK_4) {
            quiz_pick_answer(q, key - SDLK_1, audio);
            return 1;
        }
    }
    if (key == SDLK_ESCAPE) {
        q->phase = QUIZ_PHASE_FINISHED;
        return 1;
    }
    return 0;
}

int quiz_handle_mouse_motion(QuizState *q, SDL_Renderer *r,
                              int winX, int winY)
{
    (void)q; (void)r; (void)winX; (void)winY;

    return 1;
}

static void quiz_click_pool_word(QuizState *q, int poolIdx, AudioSystem *audio)
{
    const QuizPuzzle *p = &g_puzzles[q->puzzleIdx];
    if (poolIdx < 0 || poolIdx >= p->nbWords) return;
    if (q->puzzleWordPlaced[poolIdx]) return;
    if (q->puzzleNextSlot >= p->nbWords) return;

    int wordId = q->puzzleOrder[poolIdx];
    q->puzzleSlotFilled[q->puzzleNextSlot] = 1;
    q->puzzleOrder[q->puzzleNextSlot + QUIZ_PUZZLE_MAX_WORDS/2 ] = wordId;

    q->puzzleWordPlaced[poolIdx] = 1 + q->puzzleNextSlot;
    q->puzzleNextSlot++;

    if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);

    if (q->puzzleNextSlot == p->nbWords) {
        int correct = 1;
        for (int i = 0; i < p->nbWords; i++) {
            int pid = -1;
            for (int k = 0; k < p->nbWords; k++)
                if (q->puzzleWordPlaced[k] == i + 1) { pid = q->puzzleOrder[k]; break; }
            if (pid != i) { correct = 0; break; }
        }
        if (correct) {
            q->bonusScore += 1000;
            if (audio) audio_play_sfx(audio, SFX_QUIZ_CORRECT);
        } else {
            if (audio) audio_play_sfx(audio, SFX_QUIZ_WRONG);
        }
        q->phase = QUIZ_PHASE_PUZZLE_DONE;
        q->phaseStartTick = SDL_GetTicks();
    }
}

int quiz_handle_mouse_click(QuizState *q, SDL_Renderer *r,
                             int winX, int winY, AudioSystem *audio)
{
    int lx, ly;
    ui_mouse_logical(r, winX, winY, &lx, &ly);

    if (q->phase == QUIZ_PHASE_QUESTION) {
        SDL_Rect rects[QUIZ_ANSWER_COUNT];
        quiz_layout_answers(rects);
        for (int i = 0; i < QUIZ_ANSWER_COUNT; i++) {
            if (lx >= rects[i].x && lx < rects[i].x + rects[i].w &&
                ly >= rects[i].y && ly < rects[i].y + rects[i].h) {
                q->selectedAnswer = i;
                quiz_pick_answer(q, i, audio);
                return 1;
            }
        }
    } else if (q->phase == QUIZ_PHASE_PUZZLE) {
        SDL_Rect pool[QUIZ_PUZZLE_MAX_WORDS];
        SDL_Rect slot[QUIZ_PUZZLE_MAX_WORDS];
        const QuizPuzzle *p = quiz_layout_puzzle(q, pool, slot);
        for (int i = 0; i < p->nbWords; i++) {
            if (q->puzzleWordPlaced[i]) continue;
            if (lx >= pool[i].x && lx < pool[i].x + pool[i].w &&
                ly >= pool[i].y && ly < pool[i].y + pool[i].h) {
                quiz_click_pool_word(q, i, audio);
                return 1;
            }
        }
    }
    return 0;
}

void quiz_update(QuizState *q, AudioSystem *audio)
{
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - q->timerStartTick;
    int timeLeft = q->timerTotalMs - (int)elapsed;

    if (q->phase == QUIZ_PHASE_QUESTION) {
        if (timeLeft <= 0) {

            q->phase           = QUIZ_PHASE_FEEDBACK;
            q->phaseStartTick  = now;
            q->correctRevealed = 1;
            if (audio) audio_play_sfx(audio, SFX_QUIZ_WRONG);
        }
    } else if (q->phase == QUIZ_PHASE_FEEDBACK) {
        if ((int)(now - q->phaseStartTick) >= 1200) {
            q->phase          = QUIZ_PHASE_PUZZLE;
            q->phaseStartTick = now;
            q->timerTotalMs   = 20000;
            q->timerStartTick = now;
        }
    } else if (q->phase == QUIZ_PHASE_PUZZLE) {
        if (timeLeft <= 0) {
            q->phase          = QUIZ_PHASE_PUZZLE_DONE;
            q->phaseStartTick = now;
        }
    } else if (q->phase == QUIZ_PHASE_PUZZLE_DONE) {
        if ((int)(now - q->phaseStartTick) >= 1500) {
            q->phase = QUIZ_PHASE_FINISHED;
        }
    }
}

static void draw_border(SDL_Renderer *r, SDL_Rect rect, SDL_Color c, int thick)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int t = 0; t < thick; t++) {
        SDL_Rect b = {rect.x - t, rect.y - t, rect.w + 2 * t, rect.h + 2 * t};
        SDL_RenderDrawRect(r, &b);
    }
}

static void fill_rounded(SDL_Renderer *r, SDL_Rect rect, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rect);
}

void quiz_render(const QuizState *q, SDL_Renderer *r,
                 UIAssets *assets, UIFontPack *fonts, UITextCache *cache)
{

    for (int i = 0; i < 520; i++) {
        float t = (float)i / 520.0f;
        Uint8 rr = (Uint8)(30 * (1.0f - t) + 8  * t);
        Uint8 gg = (Uint8)(18 * (1.0f - t) + 6  * t);
        Uint8 bb = (Uint8)(22 * (1.0f - t) + 18 * t);
        SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
        SDL_RenderDrawLine(r, 0, i, 800, i);
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 120);
    SDL_Rect top    = {0, 0, 800, 70};  SDL_RenderFillRect(r, &top);
    SDL_Rect bottom = {0, 470, 800, 50}; SDL_RenderFillRect(r, &bottom);

    SDL_Color amber = {255, 180, 80, 255};
    SDL_Color cream = {240, 232, 210, 255};
    SDL_Color pale  = {200, 200, 220, 255};

    ui_draw_text_centered(cache, r, fonts->bebasTitle, "INTERLUDE",
                           amber, 400, 18);

    Uint32 now = SDL_GetTicks();
    int timeLeft = q->timerTotalMs - (int)(now - q->timerStartTick);
    if (timeLeft < 0) timeLeft = 0;
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%02d : %02d",
             timeLeft / 60000, (timeLeft / 1000) % 60);
    ui_draw_text_centered(cache, r, fonts->monoLarge, tbuf,
                           (SDL_Color){255, 220, 140, 255}, 720, 16);

    if (q->phase == QUIZ_PHASE_QUESTION || q->phase == QUIZ_PHASE_FEEDBACK) {
        const QuizQuestion *Q = &g_questions[q->questionIdx];

        ui_draw_text_centered(cache, r, fonts->story,    Q->question,
                               cream, 400, 110);
        char subnum[32];
        snprintf(subnum, sizeof(subnum), "Question %d / %d",
                 q->questionIdx + 1, g_questionsCount);
        ui_draw_text_centered(cache, r, fonts->novaLabel, subnum, pale, 400, 160);

        SDL_Rect rects[QUIZ_ANSWER_COUNT];
        quiz_layout_answers(rects);
        for (int i = 0; i < QUIZ_ANSWER_COUNT; i++) {
            SDL_Color baseTint;
            int isHover = (q->phase == QUIZ_PHASE_QUESTION && q->selectedAnswer == i);
            int flashGreen = 0, flashRed = 0;

            if (q->phase == QUIZ_PHASE_FEEDBACK) {
                if (q->clickedAnswer == i) {
                    if (i == Q->correct) flashGreen = 1; else flashRed = 1;
                }
                if (q->correctRevealed && i == Q->correct) flashGreen = 1;
            }

            if (assets->buttonTex) {
                if (flashGreen)      SDL_SetTextureColorMod(assets->buttonTex,  80, 255, 100);
                else if (flashRed)   SDL_SetTextureColorMod(assets->buttonTex, 255,  90,  90);
                else if (isHover)    SDL_SetTextureColorMod(assets->buttonTex, 255, 180,  80);
                else                 SDL_SetTextureColorMod(assets->buttonTex, 255, 255, 255);
                SDL_SetTextureAlphaMod(assets->buttonTex, 235);
                SDL_RenderCopy(r, assets->buttonTex, NULL, &rects[i]);
                SDL_SetTextureColorMod(assets->buttonTex, 255, 255, 255);
                SDL_SetTextureAlphaMod(assets->buttonTex, 255);
            } else {
                if (flashGreen) baseTint = (SDL_Color){ 60, 200,  80, 235};
                else if (flashRed) baseTint = (SDL_Color){220,  70,  70, 235};
                else if (isHover) baseTint = (SDL_Color){255, 180,  80, 235};
                else baseTint = (SDL_Color){ 30,  40,  65, 220};
                fill_rounded(r, rects[i], baseTint);
                draw_border(r, rects[i], (SDL_Color){200, 140, 60, 255}, 1);
            }

            char lab[96];
            snprintf(lab, sizeof(lab), "%d. %s", i + 1, Q->answers[i]);
            ui_draw_text_in_rect(cache, r, fonts->novaButton, lab,
                                  cream, rects[i]);
        }

        ui_draw_text_centered(cache, r, fonts->storySmall,
            "Clique ou 1-4 / fleches + ENTREE",
            pale, 400, 460);

    } else if (q->phase == QUIZ_PHASE_PUZZLE || q->phase == QUIZ_PHASE_PUZZLE_DONE) {
        const QuizPuzzle *p = &g_puzzles[q->puzzleIdx];
        ui_draw_text_centered(cache, r, fonts->story, p->prompt, cream, 400, 120);
        ui_draw_text_centered(cache, r, fonts->novaLabel,
            "Clique les mots dans le bon ordre",
            pale, 400, 170);

        SDL_Rect pool[QUIZ_PUZZLE_MAX_WORDS];
        SDL_Rect slot[QUIZ_PUZZLE_MAX_WORDS];
        quiz_layout_puzzle(q, pool, slot);

        ui_draw_text_centered(cache, r, fonts->novaLabel,
            "— Phrase reconstruite —", pale, 400, QUIZ_PUZZLE_SLOT_Y - 22);
        for (int i = 0; i < p->nbWords; i++) {
            fill_rounded(r, slot[i], (SDL_Color){20, 30, 50, 200});
            draw_border(r, slot[i], (SDL_Color){100, 110, 140, 255}, 1);
            if (q->puzzleSlotFilled[i]) {

                int wordId = -1;
                for (int k = 0; k < p->nbWords; k++)
                    if (q->puzzleWordPlaced[k] == i + 1) { wordId = q->puzzleOrder[k]; break; }
                if (wordId >= 0) {
                    ui_draw_text_in_rect(cache, r, fonts->novaButton,
                                          p->words[wordId], cream, slot[i]);
                }
            }
        }

        ui_draw_text_centered(cache, r, fonts->novaLabel,
            "— Mots melanges —", pale, 400, QUIZ_PUZZLE_POOL_Y - 22);
        for (int i = 0; i < p->nbWords; i++) {
            if (q->puzzleWordPlaced[i]) {
                fill_rounded(r, pool[i], (SDL_Color){20, 20, 25, 140});
                draw_border(r, pool[i], (SDL_Color){60, 60, 80, 200}, 1);
                continue;
            }
            if (assets->buttonTex) {
                SDL_SetTextureColorMod(assets->buttonTex, 255, 180, 80);
                SDL_RenderCopy(r, assets->buttonTex, NULL, &pool[i]);
                SDL_SetTextureColorMod(assets->buttonTex, 255, 255, 255);
            } else {
                fill_rounded(r, pool[i], (SDL_Color){230, 140, 50, 230});
                draw_border(r, pool[i], (SDL_Color){255, 215, 0, 255}, 1);
            }
            ui_draw_text_in_rect(cache, r, fonts->novaButton,
                                  p->words[q->puzzleOrder[i]],
                                  (SDL_Color){255, 250, 220, 255}, pool[i]);
        }

        if (q->phase == QUIZ_PHASE_PUZZLE_DONE) {

            int correct = 1;
            for (int i = 0; i < p->nbWords; i++) {
                int pid = -1;
                for (int k = 0; k < p->nbWords; k++)
                    if (q->puzzleWordPlaced[k] == i + 1) { pid = q->puzzleOrder[k]; break; }
                if (pid != i) { correct = 0; break; }
            }
            ui_draw_text_centered(cache, r, fonts->bebasHeader,
                correct ? "BRAVO !" : "TEMPS ECOULE",
                correct ? (SDL_Color){80,255,120,255} : (SDL_Color){255,90,90,255},
                400, 400);
        } else {
            ui_draw_text_centered(cache, r, fonts->storySmall,
                "Clique sur les mots du haut pour les placer en bas",
                pale, 400, 460);
        }
    }

    char bbuf[48];
    snprintf(bbuf, sizeof(bbuf), "BONUS : %d pts", q->bonusScore);
    ui_draw_text(cache, r, fonts->mono, bbuf,
                  (SDL_Color){180, 220, 255, 255}, 16, 488);

    ui_draw_text(cache, r, fonts->mono,
        "[ECHAP] Passer", (SDL_Color){180, 190, 210, 255}, 680, 488);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

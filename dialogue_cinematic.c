#include "dialogue_cinematic.h"
#include "background.h"   /* W_ECRAN / H_ECRAN */

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------------- *
 *  Dialogue cinematic.
 *
 *  Wraps the NPCDialogue state machine in a "stop the world, slide letterbox
 *  bars in, push the camera toward the speaker, type the response one char
 *  at a time" presentation layer.  No game systems are restructured: the
 *  existing dlg->response / dlg->waiting / dlg->user_input fields are still
 *  the canonical source of truth -- this module just delays the visible
 *  reveal and decorates the frame.
 *
 *  Stage transitions:
 *    IDLE  --start()--> ENTER  --(0.4s)--> ACTIVE  --close()--> EXIT
 *    EXIT  --(0.3s)--> IDLE
 *
 *  The wrapped dlg is opened on start() (so SDL_StartTextInput fires at the
 *  start of ENTER and the player can type immediately, even while the bars
 *  are still sliding -- this is what we want because the LLM round-trip is
 *  longer than the 0.4s slide anyway).
 * ------------------------------------------------------------------------- */

#define BAR_TOP_PX  ((int)((float)H_ECRAN * CIN_BAR_TOP_PCT))
#define BAR_BOT_PX  ((int)((float)H_ECRAN * CIN_BAR_BOT_PCT))

static DialogueCinematic *g_cin = NULL;

void dialogue_cinematic_bind_global(DialogueCinematic *c) { g_cin = c; }
DialogueCinematic *dialogue_cinematic_global(void)        { return g_cin; }

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* cubic ease-out: snappy at start, gentle settle */
static float ease_out_cubic(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    float k = 1.0f - t;
    return 1.0f - k * k * k;
}

/* djb2-ish so we can spot when dlg->response gets replaced */
static unsigned long str_hash(const char *s)
{
    unsigned long h = 5381;
    if (!s) return 0;
    for (; *s; ++s) h = ((h << 5) + h) + (unsigned char)*s;
    return h;
}

/* ------------------------------------------------------------------------- */
/* lifecycle                                                                 */

void dialogue_cinematic_init(DialogueCinematic *c)
{
    memset(c, 0, sizeof(*c));
    c->stage = CIN_IDLE;
}

void dialogue_cinematic_start(DialogueCinematic *c, NPCDialogue *dlg,
                               float npc_x,    float npc_y,
                               float player_x, float player_y)
{
    if (!c) return;
    /* if we're already going out, snap us back to ENTER from current
     * progress so a re-trigger doesn't pop the bars to closed */
    float carry = 0.0f;
    if (c->stage == CIN_EXIT) {
        /* invert progress so the bars don't jump */
        carry = c->progress;
    }
    c->stage         = CIN_ENTER;
    c->stage_time    = carry * CIN_ENTER_SECONDS;
    c->progress      = carry;
    c->npc_x         = npc_x;
    c->npc_y         = npc_y;
    c->player_x      = player_x;
    c->player_y      = player_y;
    c->type_char_index    = 0;
    c->type_char_timer    = 0.0f;
    c->type_is_complete   = false;
    c->type_response_hash = 0;
    c->dlg = dlg;

    if (dlg && !dlg->active) npc_dialogue_open(dlg);
}

void dialogue_cinematic_close(DialogueCinematic *c)
{
    if (!c || c->stage == CIN_IDLE || c->stage == CIN_EXIT) return;
    /* run the exit slide from current progress (so an instant Esc during
     * ENTER still slides out the partial bars instead of snapping) */
    c->stage      = CIN_EXIT;
    c->stage_time = 0.0f;
    if (c->dlg && c->dlg->active) npc_dialogue_close(c->dlg);
}

bool dialogue_cinematic_is_active(const DialogueCinematic *c)
{
    return c && c->stage != CIN_IDLE;
}

bool dialogue_cinematic_freezes_world(const DialogueCinematic *c)
{
    /* freeze through ENTER and ACTIVE; let the world breathe again as
     * soon as we start sliding the bars out -- that way the player's
     * world snaps back to motion while the chrome animates away. */
    if (!c) return false;
    return (c->stage == CIN_ENTER || c->stage == CIN_ACTIVE);
}

/* ------------------------------------------------------------------------- */
/* update                                                                    */

void dialogue_cinematic_update(DialogueCinematic *c, float dt)
{
    if (!c || c->stage == CIN_IDLE) return;
    c->stage_time += dt;

    switch (c->stage) {
        case CIN_ENTER: {
            float t = c->stage_time / CIN_ENTER_SECONDS;
            c->progress = ease_out_cubic(t);
            if (t >= 1.0f) {
                c->progress  = 1.0f;
                c->stage     = CIN_ACTIVE;
                c->stage_time = 0.0f;
            }
            break;
        }
        case CIN_ACTIVE: {
            c->progress = 1.0f;
            break;
        }
        case CIN_EXIT: {
            float t = c->stage_time / CIN_EXIT_SECONDS;
            c->progress = 1.0f - ease_out_cubic(t);
            if (t >= 1.0f) {
                c->progress  = 0.0f;
                c->stage     = CIN_IDLE;
                c->stage_time = 0.0f;
            }
            break;
        }
        default: break;
    }

    /* typewriter: only run during ACTIVE, and only when we actually have
     * a settled response (not the LLM "waiting" placeholder).  If the
     * response text changes underneath us (LLM finished, or the system
     * pushed a new line), reset the cursor. */
    if (c->dlg && !c->dlg->waiting && c->dlg->response[0]) {
        unsigned long h = str_hash(c->dlg->response);
        if (h != c->type_response_hash) {
            c->type_response_hash = h;
            c->type_char_index    = 0;
            c->type_char_timer    = 0.0f;
            c->type_is_complete   = false;
        }
        if (c->stage == CIN_ACTIVE && !c->type_is_complete) {
            int len = (int)strlen(c->dlg->response);
            c->type_char_timer += dt;
            float per_char = 1.0f / CIN_TYPE_CHARS_PER_S;
            while (c->type_char_timer >= per_char && c->type_char_index < len) {
                c->type_char_timer -= per_char;
                c->type_char_index++;
            }
            if (c->type_char_index >= len) {
                c->type_char_index  = len;
                c->type_is_complete = true;
            }
        }
    } else {
        /* nothing to reveal -- LLM still cooking or empty line */
        c->type_char_index    = 0;
        c->type_is_complete   = false;
        c->type_response_hash = 0;
    }
}

/* ------------------------------------------------------------------------- */
/* render-time queries                                                       */

float dialogue_cinematic_zoom(const DialogueCinematic *c)
{
    if (!c || c->stage == CIN_IDLE) return 1.0f;
    return 1.0f + (CIN_ZOOM_PEAK - 1.0f) * c->progress;
}

int dialogue_cinematic_world_pan_ox(const DialogueCinematic *c)
{
    if (!c || c->stage == CIN_IDLE) return 0;
    /* Aim the midpoint of (player, npc) at logical-screen center.  This
     * is the "pan" half of the camera move; the zoom itself comes from
     * SDL_RenderSetScale applied by the caller.                          */
    float midx  = (c->npc_x + c->player_x) * 0.5f;
    float want  = (float)(W_ECRAN / 2);
    float shift = (want - midx) * c->progress * 0.35f;
    return (int)shift;
}

int dialogue_cinematic_speaker_bob_offset(const DialogueCinematic *c)
{
    /* only during ACTIVE so the bob doesn't start while bars are sliding,
     * and so it stops cleanly as soon as we begin EXIT */
    if (!c || c->stage != CIN_ACTIVE) return 0;
    float t = c->stage_time;
    return (int)(sinf(t * 2.0f * (float)M_PI * CIN_BOB_HZ) * CIN_BOB_AMPLITUDE_PX);
}

void dialogue_cinematic_render_vignette(const DialogueCinematic *c,
                                         SDL_Renderer *r)
{
    if (!c || c->stage == CIN_IDLE) return;
    Uint8 a = (Uint8)((float)CIN_VIGNETTE_PEAK * c->progress);
    if (a == 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    /* layered radial-ish vignette: four rings of growing darkness around
     * the edges.  Cheap, no shader, looks the part at 800x520.            */
    const int rings   = 4;
    for (int i = 0; i < rings; i++) {
        int t = (i + 1) * 12;     /* thickness in px           */
        int off = i * 10;         /* inset from previous ring  */
        Uint8 alpha = (Uint8)((a * (rings - i)) / (rings + 1));
        SDL_SetRenderDrawColor(r, 0, 0, 0, alpha);
        /* top */
        SDL_Rect rt = {off, off, W_ECRAN - off * 2, t};
        SDL_RenderFillRect(r, &rt);
        /* bottom */
        SDL_Rect rb = {off, H_ECRAN - off - t, W_ECRAN - off * 2, t};
        SDL_RenderFillRect(r, &rb);
        /* left */
        SDL_Rect rl = {off, off + t, t, H_ECRAN - 2 * (off + t)};
        SDL_RenderFillRect(r, &rl);
        /* right */
        SDL_Rect rr = {W_ECRAN - off - t, off + t, t, H_ECRAN - 2 * (off + t)};
        SDL_RenderFillRect(r, &rr);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void render_chevrons(SDL_Renderer *r, int cx, int cy)
{
    /* blinking ">>" composed of pixel rectangles; tinted amber to match
     * the HUD's accent color used in npc_dialogue header text.           */
    Uint32 t = SDL_GetTicks();
    Uint8 a = ((t / 400) % 2) ? 255 : 80;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 200, 90, a);
    /* two stacked triangles drawn as small filled rects (poor man's > > ) */
    for (int k = 0; k < 2; k++) {
        int bx = cx + k * 7;
        for (int row = 0; row < 5; row++) {
            int w = 2 + (row < 3 ? row : 4 - row);
            SDL_Rect rc = {bx + row, cy - 2 + row, w, 1};
            SDL_RenderFillRect(r, &rc);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void dialogue_cinematic_render_chrome(const DialogueCinematic *c,
                                       SDL_Renderer *r,
                                       UIFontPack *fonts,
                                       UITextCache *cache)
{
    if (!c || c->stage == CIN_IDLE) return;

    /* --- letterbox bars (animated in/out) --- */
    int top_full = BAR_TOP_PX;
    int bot_full = BAR_BOT_PX;
    int top_h    = (int)((float)top_full * c->progress);
    int bot_h    = (int)((float)bot_full * c->progress);

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_Rect top = {0, 0, W_ECRAN, top_h};
    SDL_RenderFillRect(r, &top);
    SDL_Rect bot = {0, H_ECRAN - bot_h, W_ECRAN, bot_h};
    SDL_RenderFillRect(r, &bot);

    /* thin amber dividing lines so the bars don't look like dead pixels */
    if (top_h > 0) {
        SDL_SetRenderDrawColor(r, 200, 150, 50, 255);
        SDL_RenderDrawLine(r, 0, top_h, W_ECRAN - 1, top_h);
    }
    if (bot_h > 0) {
        SDL_SetRenderDrawColor(r, 200, 150, 50, 255);
        SDL_RenderDrawLine(r, 0, H_ECRAN - bot_h - 1,
                              W_ECRAN - 1, H_ECRAN - bot_h - 1);
    }

    /* --- text inside the bottom bar (only once it's tall enough) --- */
    if (c->stage != CIN_EXIT && c->dlg && bot_h >= 50) {
        SDL_Color amber = {255, 200,  90, 255};
        SDL_Color cream = {235, 225, 200, 255};
        SDL_Color gray  = {130, 130, 140, 255};

        TTF_Font *fName = fonts->bebasHeader ? fonts->bebasHeader : fonts->fallback;
        TTF_Font *fBody = fonts->story       ? fonts->story       : fonts->fallback;
        TTF_Font *fInput= fonts->mono        ? fonts->mono        : fonts->fallback;
        TTF_Font *fHint = fonts->novaLabel   ? fonts->novaLabel   : fonts->fallback;

        int pad_x  = 32;
        int bar_y  = H_ECRAN - bot_h;
        int name_y = bar_y + 6;

        /* speaker name */
        ui_draw_text(cache, r, fName,
                     c->dlg->npc_name[0] ? c->dlg->npc_name : "NPC",
                     amber, pad_x, name_y);

        /* typewriter-revealed response */
        int body_y = name_y + 30;
        if (c->dlg->waiting) {
            ui_draw_text(cache, r, fBody, "...", gray, pad_x, body_y);
        } else if (c->dlg->response[0]) {
            char buf[NPC_DLG_RESPONSE_MAX];
            int n = c->type_char_index;
            int len = (int)strlen(c->dlg->response);
            if (n > len) n = len;
            if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
            memcpy(buf, c->dlg->response, n);
            buf[n] = 0;
            ui_draw_text(cache, r, fBody, buf, cream, pad_x, body_y);

            if (c->type_is_complete) {
                /* place chevrons just to the right of the rendered text */
                int tw = 0, th = 0;
                TTF_SizeUTF8(fBody, buf, &tw, &th);
                int cx = pad_x + tw + 12;
                int cy = body_y + 8;
                if (cx > W_ECRAN - 40) cx = W_ECRAN - 40;
                render_chevrons(r, cx, cy);
            }
        }

        /* --- player input line: this is the field they type into ---
         * Sat right below the response so the player can SEE what they're
         * typing.  We render with a blinking '_' cursor (or solid '_' when
         * waiting on the LLM so the field reads as locked).               */
        int input_y    = bar_y + bot_h - 44;
        int input_w    = W_ECRAN - 2 * pad_x;
        SDL_Rect ir    = {pad_x, input_y, input_w, 26};

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 30, 30, 40, 220);
        SDL_RenderFillRect(r, &ir);
        SDL_SetRenderDrawColor(r, 120, 90, 40, 255);
        SDL_RenderDrawRect(r, &ir);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

        char input_line[NPC_DLG_USER_INPUT_MAX + 8];
        if (c->dlg->waiting) {
            /* LLM is thinking; show what we sent + a static cursor */
            ui_draw_text(cache, r, fInput,
                         c->dlg->user_input[0] ? c->dlg->user_input
                                               : "(waiting...)",
                         gray, ir.x + 8, ir.y + 5);
        } else {
            Uint32 t = SDL_GetTicks();
            const char *cur = ((t / 500) % 2) ? "_" : " ";
            snprintf(input_line, sizeof(input_line),
                     "%s%s", c->dlg->user_input, cur);
            if (c->dlg->user_input[0] == 0) {
                /* nothing typed yet -> placeholder; only show after the
                 * typewriter finishes, so the player isn't asked to reply
                 * to an incomplete line. */
                if (c->type_is_complete) {
                    ui_draw_text(cache, r, fInput,
                                 "type your reply...", gray,
                                 ir.x + 8, ir.y + 5);
                }
            } else {
                ui_draw_text(cache, r, fInput, input_line, cream,
                             ir.x + 8, ir.y + 5);
            }
        }

        /* small hint at the bottom-right of the bar */
        const char *hint = c->type_is_complete
            ? "[Enter] send    [Backspace] del    [Esc] leave"
            : "[Enter] skip    [Esc] leave";
        int tw = 0, th = 0;
        TTF_SizeUTF8(fHint, hint, &tw, &th);
        ui_draw_text(cache, r, fHint, hint, gray,
                     W_ECRAN - pad_x - tw, H_ECRAN - 16);
    }
}

bool dialogue_cinematic_handle_key(DialogueCinematic *c, SDL_Keycode key)
{
    if (!c || c->stage == CIN_IDLE) return false;

    /* Escape always means "get me out of this cinematic"; we close the
     * wrapped dialogue from inside close().                              */
    if (key == SDLK_ESCAPE) {
        dialogue_cinematic_close(c);
        return true;
    }

    /* If we're still typing characters out, an Enter press jumps to the
     * fully-revealed state and is consumed here so the wrapped chatbot
     * doesn't see it (and prematurely send an empty submit).             */
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (c->dlg && !c->dlg->waiting && c->dlg->response[0] &&
            !c->type_is_complete) {
            c->type_char_index  = (int)strlen(c->dlg->response);
            c->type_is_complete = true;
            return true;
        }
    }
    return false;
}

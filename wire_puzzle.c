#include "wire_puzzle.h"
#include "background.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WP_PANEL_W   600
#define WP_PANEL_H   320
#define WP_LEFT_X    155
#define WP_RIGHT_X   645
#define WP_ROW_GAP    60
#define WP_RADIUS     18
#define WP_HIT_RADIUS 28

typedef struct { SDL_Color color; char sym; } WireDef;

static const WireDef WIRES[5] = {
    {{220,  60,  60, 255}, 'T'},
    {{ 60, 120, 220, 255}, 'S'},
    {{245, 200,  66, 255}, 'C'},
    {{ 80, 180,  80, 255}, 'X'},
    {{232, 213, 163, 255}, 'D'}
};

typedef struct { float x, y, vx, vy; int alpha; } WPSpark;

static void filled_circle(SDL_Renderer *r, int cx, int cy, int rad, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = -rad; dy <= rad; dy++) {
        int dxw = (int)sqrtf((float)(rad*rad - dy*dy));
        SDL_RenderDrawLine(r, cx - dxw, cy + dy, cx + dxw, cy + dy);
    }
}

static void hollow_circle(SDL_Renderer *r, int cx, int cy, int rad, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int n = 48;
    for (int i = 0; i < n; i++) {
        float a1 = (float)i       * 6.28318f / n;
        float a2 = (float)(i + 1) * 6.28318f / n;

        SDL_RenderDrawLine(r,
            cx + (int)(cosf(a1) * rad),       cy + (int)(sinf(a1) * rad),
            cx + (int)(cosf(a2) * rad),       cy + (int)(sinf(a2) * rad));
        SDL_RenderDrawLine(r,
            cx + (int)(cosf(a1) * (rad - 2)), cy + (int)(sinf(a1) * (rad - 2)),
            cx + (int)(cosf(a2) * (rad - 2)), cy + (int)(sinf(a2) * (rad - 2)));
    }
}

static void bezier_wire(SDL_Renderer *r, int x0, int y0, int x1, int y1, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    int dx = (x1 - x0); if (dx < 0) dx = -dx;
    int droop = 20 + dx / 8; if (droop > 70) droop = 70;
    int cxA = x0 + (x1 - x0) / 3;
    int cxB = x1 - (x1 - x0) / 3;
    int cyA = y0 + droop, cyB = y1 + droop;
    int N = 48;
    int px = x0, py = y0;
    for (int i = 1; i <= N; i++) {
        float t = (float)i / N, u = 1.f - t;
        int ix = (int)(u*u*u*x0 + 3*u*u*t*cxA + 3*u*t*t*cxB + t*t*t*x1);
        int iy = (int)(u*u*u*y0 + 3*u*u*t*cyA + 3*u*t*t*cyB + t*t*t*y1);

        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(r, px, py,     ix, iy);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 180);
        SDL_RenderDrawLine(r, px, py + 1, ix, iy + 1);
        SDL_RenderDrawLine(r, px, py - 1, ix, iy - 1);
        px = ix; py = iy;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void glow_ring(SDL_Renderer *r, int cx, int cy, int rad, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int extra = 6; extra >= 1; extra--) {
        Uint8 a = (Uint8)(40 * extra);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
        int rr = rad + extra * 2;
        int n = 48;
        for (int i = 0; i < n; i++) {
            float a1 = (float)i       * 6.28318f / n;
            float a2 = (float)(i + 1) * 6.28318f / n;
            SDL_RenderDrawLine(r,
                cx + (int)(cosf(a1) * rr), cy + (int)(sinf(a1) * rr),
                cx + (int)(cosf(a2) * rr), cy + (int)(sinf(a2) * rr));
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

static void wire_stub(SDL_Renderer *r, int cx, int cy, int dir, SDL_Color c)
{

    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    int x0 = cx + dir * WP_RADIUS;
    int x1 = cx + dir * (WP_RADIUS + 22);
    SDL_RenderDrawLine(r, x0, cy,     x1, cy);
    SDL_RenderDrawLine(r, x0, cy + 1, x1, cy + 1);
    SDL_RenderDrawLine(r, x0, cy - 1, x1, cy - 1);
}

static int in_circle(int px, int py, int cx, int cy, int rad)
{
    int dx = px - cx, dy = py - cy;
    return dx*dx + dy*dy <= rad*rad;
}

void run_wire_puzzle(SDL_Renderer *renderer, SDL_Window *window,
                     int *score_bonus, int current_wagon,
                     UIFontPack *fonts, UITextCache *cache,
                     UIAssets *assets, AudioSystem *audio,
                     GameplayState *gp)
{
    (void)assets; (void)window;

    int nb = 3 + (current_wagon - 1);
    if (nb < 3) nb = 3;
    if (nb > WIRE_MAX_WIRES) nb = WIRE_MAX_WIRES;

    Uint32 timerMs = WIRE_TIMER_LV1_MS;
    if (current_wagon == 2)    timerMs = WIRE_TIMER_LV2_MS;
    else if (current_wagon >= 3) timerMs = WIRE_TIMER_LV3_MS;

    int rightOrder[WIRE_MAX_WIRES];
    for (int i = 0; i < nb; i++) rightOrder[i] = i;
    unsigned seed = (unsigned)SDL_GetTicks() ^ (unsigned)(current_wagon * 137u);
    for (int i = nb - 1; i > 0; i--) {
        seed = seed * 1664525u + 1013904223u;
        int j = (int)((seed >> 16) % (unsigned)(i + 1));
        int tmp = rightOrder[i]; rightOrder[i] = rightOrder[j]; rightOrder[j] = tmp;
    }

    int identity = 1;
    for (int i = 0; i < nb; i++) if (rightOrder[i] != i) { identity = 0; break; }
    if (identity && nb >= 2) {
        int tmp = rightOrder[0]; rightOrder[0] = rightOrder[1]; rightOrder[1] = tmp;
    }

    int leftConnected[WIRE_MAX_WIRES]   = {0};
    int rightConnectedTo[WIRE_MAX_WIRES]= {-1,-1,-1,-1,-1};
    int connectedCount = 0;
    int wrongAttempts  = 0;

    int dragging  = 0;
    int dragWire  = -1;
    int dragX     = 0;
    int dragY     = 0;
    int hoverSlot = -1;

    WPSpark sparks[64]; int nbSparks = 0;
    for (int i = 0; i < 64; i++) sparks[i].alpha = 0;
    int redFlash   = 0;
    int greenFlash = 0;
    int complete   = 0;
    Uint32 completeTick = 0;
    int finished   = 0;

    int panelX = (W_ECRAN - WP_PANEL_W) / 2;
    int panelY = (H_ECRAN - WP_PANEL_H) / 2 - 10;

    int topY = panelY + (WP_PANEL_H - (nb - 1) * WP_ROW_GAP) / 2;

    Uint32 startTick = SDL_GetTicks();
    Uint32 lastTick  = startTick;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    SDL_Texture *tex_panel  = ui_loadTexture(renderer, "wire_panel.png");
    SDL_Texture *tex_cap    = ui_loadTexture(renderer, "wire_cap_left.png");
    SDL_Texture *tex_socket = ui_loadTexture(renderer, "wire_socket.png");
    if (tex_cap)    SDL_SetTextureBlendMode(tex_cap,    SDL_BLENDMODE_BLEND);
    if (tex_socket) SDL_SetTextureBlendMode(tex_socket, SDL_BLENDMODE_BLEND);

    static const SDL_Rect capSrc[5] = {
        {  0,   0, 512, 341},
        {512,   0, 512, 341},
        {256, 341, 512, 341},
        {  0, 682, 512, 342},
        {512, 682, 512, 342},
    };

    while (!finished) {

        Uint32 now = SDL_GetTicks();
        float dt = (float)(now - lastTick) / 1000.f;
        if (dt > 0.05f) dt = 0.05f;
        lastTick = now;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {

            if (ev.type == SDL_QUIT) { SDL_PushEvent(&ev); finished = 1; break; }

            if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;

                if (k == SDLK_ESCAPE) {
                    if (dragging) { dragging = 0; dragWire = -1; }
                    else          { finished = 1; break; }
                }

                if (k >= SDLK_1 && k <= SDLK_5) {
                    int pick = (int)(k - SDLK_1);
                    if (pick < nb && !leftConnected[pick]) {
                        if (dragging && dragWire == pick) {
                            dragging = 0; dragWire = -1;
                        } else {
                            dragging = 1;
                            dragWire = pick;
                            dragX = WP_LEFT_X + WP_RADIUS + 22;
                            dragY = topY + pick * WP_ROW_GAP;
                        }
                    }
                }

                if (k >= SDLK_a && k <= SDLK_e) {
                    int slot = (int)(k - SDLK_a);
                    if (dragging && slot < nb && rightConnectedTo[slot] < 0) {
                        if (rightOrder[slot] == dragWire) {
                            leftConnected[dragWire]   = 1;
                            rightConnectedTo[slot]    = dragWire;
                            connectedCount++;
                            dragging = 0; dragWire = -1;
                            if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
                        } else {
                            wrongAttempts++;
                            redFlash = 200;
                            dragging = 0; dragWire = -1;
                            if (audio) audio_play_sfx(audio, SFX_ENEMY_DEATH);
                        }
                    }
                }
            }

            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                int lx, ly;
                ui_mouse_logical(renderer, ev.button.x, ev.button.y, &lx, &ly);
                dragX = lx; dragY = ly;

                if (!dragging) {

                    for (int i = 0; i < nb; i++) {
                        if (leftConnected[i]) continue;
                        int cy = topY + i * WP_ROW_GAP;
                        if (in_circle(lx, ly, WP_LEFT_X, cy, WP_HIT_RADIUS)) {
                            dragging = 1;
                            dragWire = i;
                            break;
                        }
                    }
                } else {

                    int cy = topY + dragWire * WP_ROW_GAP;
                    if (in_circle(lx, ly, WP_LEFT_X, cy, WP_HIT_RADIUS)) {
                        dragging = 0; dragWire = -1;
                    }
                }
            }

            if (ev.type == SDL_MOUSEMOTION) {
                int lx, ly;
                ui_mouse_logical(renderer, ev.motion.x, ev.motion.y, &lx, &ly);
                dragX = lx; dragY = ly;

                hoverSlot = -1;
                if (dragging) {
                    for (int i = 0; i < nb; i++) {
                        int cy = topY + i * WP_ROW_GAP;
                        if (in_circle(lx, ly, WP_RIGHT_X, cy, WP_HIT_RADIUS)) {
                            hoverSlot = i; break;
                        }
                    }
                }
            }

            if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                if (dragging) {
                    int lx, ly;
                    ui_mouse_logical(renderer, ev.button.x, ev.button.y, &lx, &ly);

                    int hit = -1;
                    for (int i = 0; i < nb; i++) {
                        int cy = topY + i * WP_ROW_GAP;
                        if (in_circle(lx, ly, WP_RIGHT_X, cy, WP_HIT_RADIUS)) {
                            hit = i; break;
                        }
                    }

                    if (hit >= 0 && rightConnectedTo[hit] < 0) {
                        if (rightOrder[hit] == dragWire) {

                            leftConnected[dragWire]  = 1;
                            rightConnectedTo[hit]    = dragWire;
                            connectedCount++;
                            dragging = 0; dragWire = -1; hoverSlot = -1;
                            if (audio) audio_play_sfx(audio, SFX_BUTTON_CLICK);
                        } else {

                            wrongAttempts++;
                            redFlash = 220;

                            for (int s = 0; s < 10; s++) {
                                int slot = (nbSparks < 64) ? nbSparks++ : 0;
                                WPSpark *sp = &sparks[slot];
                                float a = (float)rand() / (float)RAND_MAX * 6.28f;
                                sp->x  = (float)lx; sp->y  = (float)ly;
                                sp->vx = cosf(a) * 140.f;
                                sp->vy = sinf(a) * 140.f;
                                sp->alpha = 255;
                            }
                            dragging = 0; dragWire = -1; hoverSlot = -1;
                            if (audio) audio_play_sfx(audio, SFX_ENEMY_DEATH);
                        }
                    } else {

                        dragging = 0; dragWire = -1; hoverSlot = -1;
                    }
                }
            }
        }

        if (connectedCount >= nb && !complete) {
            complete = 1;
            greenFlash  = 255;
            completeTick = now;
            if (audio) audio_play_sfx(audio, SFX_QUIZ_CORRECT);
        }
        if (complete && now - completeTick >= 1500) {
            Uint32 elapsed = now - startTick;
            int timeLeft = (int)timerMs - (int)elapsed;
            if (timeLeft < 0) timeLeft = 0;
            int bonus = (timeLeft / 100) - (wrongAttempts * 50);
            if (bonus < 0) bonus = 0;
            if (score_bonus) *score_bonus = bonus;
            if (gp) gp->totalInterludeScore += bonus;
            finished = 1; break;
        }

        Uint32 elapsed = now - startTick;
        if (!complete && elapsed >= timerMs) {
            for (int i = 0; i < nb; i++) {
                leftConnected[i]    = 0;
                rightConnectedTo[i] = -1;
            }
            connectedCount = 0;
            wrongAttempts  = 0;
            dragging = 0; dragWire = -1; hoverSlot = -1;
            startTick = now;
            redFlash  = 200;
            if (audio) audio_play_sfx(audio, SFX_ENEMY_DEATH);
        }

        for (int i = 0; i < nbSparks; i++) {
            sparks[i].x += sparks[i].vx * dt;
            sparks[i].y += sparks[i].vy * dt;
            sparks[i].vy += 180.f * dt;
            sparks[i].alpha -= (int)(dt * 700.f);
            if (sparks[i].alpha < 0) sparks[i].alpha = 0;
        }
        while (nbSparks > 0 && sparks[nbSparks - 1].alpha <= 0) nbSparks--;
        if (redFlash   > 0) redFlash   -= (int)(dt * 700.f);
        if (greenFlash > 0) greenFlash -= (int)(dt * 500.f);



        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 8, 10, 18, 255);
        SDL_RenderClear(renderer);

        {
            SDL_Rect pdst = {panelX, panelY, WP_PANEL_W, WP_PANEL_H};
            if (tex_panel) {
                SDL_RenderCopy(renderer, tex_panel, NULL, &pdst);
            } else if (gp && gp->tex_wirePanel) {
                SDL_RenderCopy(renderer, gp->tex_wirePanel, NULL, &pdst);
            } else {
                SDL_SetRenderDrawColor(renderer, 22, 26, 36, 255);
                SDL_RenderFillRect(renderer, &pdst);
                SDL_SetRenderDrawColor(renderer, 50, 55, 70, 255);
                SDL_RenderDrawRect(renderer, &pdst);
                SDL_Rect inner = {panelX+2, panelY+2, WP_PANEL_W-4, WP_PANEL_H-4};
                SDL_SetRenderDrawColor(renderer, 35, 40, 55, 255);
                SDL_RenderDrawRect(renderer, &inner);
                SDL_Color bolt = {60, 65, 80, 255};
                int boltR = 5;
                int bxb[4] = {panelX+14, panelX+WP_PANEL_W-14,
                               panelX+14, panelX+WP_PANEL_W-14};
                int byb[4] = {panelY+14, panelY+14,
                               panelY+WP_PANEL_H-14, panelY+WP_PANEL_H-14};
                for (int b = 0; b < 4; b++) filled_circle(renderer, bxb[b], byb[b], boltR, bolt);
                SDL_SetRenderDrawColor(renderer, 40, 45, 60, 255);
                SDL_Rect lwall = {panelX, panelY, 36, WP_PANEL_H};
                SDL_Rect rwall = {panelX+WP_PANEL_W-36, panelY, 36, WP_PANEL_H};
                SDL_RenderFillRect(renderer, &lwall);
                SDL_RenderFillRect(renderer, &rwall);
            }
        }

        if (redFlash > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 255, 40, 40, (Uint8)redFlash);
            SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
            SDL_RenderFillRect(renderer, &full);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        if (greenFlash > 0) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 40, 220, 80, (Uint8)greenFlash);
            SDL_Rect full = {0, 0, W_ECRAN, H_ECRAN};
            SDL_RenderFillRect(renderer, &full);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        for (int i = 0; i < nb; i++) {
            if (rightConnectedTo[i] < 0) continue;
            int srcLeft = rightConnectedTo[i];
            int cyL = topY + srcLeft * WP_ROW_GAP;
            int cyR = topY + i       * WP_ROW_GAP;
            bezier_wire(renderer,
                WP_LEFT_X  + WP_RADIUS + 22, cyL,
                WP_RIGHT_X - WP_RADIUS - 22, cyR,
                WIRES[rightOrder[i]].color);
        }

        if (dragging && dragWire >= 0) {
            int cyL = topY + dragWire * WP_ROW_GAP;
            bezier_wire(renderer,
                WP_LEFT_X + WP_RADIUS + 22, cyL,
                dragX, dragY,
                WIRES[dragWire].color);
        }

        if (dragging && hoverSlot >= 0 && rightConnectedTo[hoverSlot] < 0) {
            int cy = topY + hoverSlot * WP_ROW_GAP;
            SDL_Color c = WIRES[rightOrder[hoverSlot]].color;
            if (rightOrder[hoverSlot] == dragWire) {

                glow_ring(renderer, WP_RIGHT_X, cy, WP_RADIUS, c);
            } else {

                SDL_Color warn = {200, 60, 60, 255};
                glow_ring(renderer, WP_RIGHT_X, cy, WP_RADIUS, warn);
            }
        }

        for (int i = 0; i < nb; i++) {
            int cy   = topY + i * WP_ROW_GAP;
            SDL_Color c = WIRES[i].color;
            int capSz = WP_RADIUS * 2 + 10;

            if (tex_cap) {
                if (leftConnected[i]) {
                    SDL_Color dim = {(Uint8)(c.r/3),(Uint8)(c.g/3),(Uint8)(c.b/3),255};
                    wire_stub(renderer, WP_LEFT_X, cy, +1, dim);
                    SDL_SetTextureAlphaMod(tex_cap, 80);
                    SDL_Rect dst = {WP_LEFT_X - capSz/2, cy - capSz/2, capSz, capSz};
                    SDL_RenderCopy(renderer, tex_cap, &capSrc[i], &dst);
                    SDL_SetTextureAlphaMod(tex_cap, 255);
                } else if (dragging && dragWire == i) {
                    wire_stub(renderer, WP_LEFT_X, cy, +1, c);
                    SDL_Rect dst = {WP_LEFT_X - capSz/2 - 3, cy - capSz/2 - 3,
                                    capSz + 6, capSz + 6};
                    SDL_RenderCopy(renderer, tex_cap, &capSrc[i], &dst);
                    hollow_circle(renderer, WP_LEFT_X, cy, WP_RADIUS + 9,
                                  (SDL_Color){255, 255, 255, 200});
                } else {
                    wire_stub(renderer, WP_LEFT_X, cy, +1, c);
                    SDL_Rect dst = {WP_LEFT_X - capSz/2, cy - capSz/2, capSz, capSz};
                    SDL_RenderCopy(renderer, tex_cap, &capSrc[i], &dst);
                }
            } else {
                if (leftConnected[i]) {
                    SDL_Color dim = {(Uint8)(c.r/3),(Uint8)(c.g/3),(Uint8)(c.b/3),255};
                    wire_stub(renderer, WP_LEFT_X, cy, +1, dim);
                    filled_circle(renderer, WP_LEFT_X, cy, WP_RADIUS - 2, dim);
                } else if (dragging && dragWire == i) {
                    wire_stub(renderer, WP_LEFT_X, cy, +1, c);
                    SDL_Color bright = {
                        (Uint8)(c.r < 200 ? c.r + 55 : 255),
                        (Uint8)(c.g < 200 ? c.g + 55 : 255),
                        (Uint8)(c.b < 200 ? c.b + 55 : 255), 255};
                    filled_circle(renderer, WP_LEFT_X, cy, WP_RADIUS + 3, bright);
                    hollow_circle(renderer, WP_LEFT_X, cy, WP_RADIUS + 6,
                                  (SDL_Color){255, 255, 255, 200});
                } else {
                    wire_stub(renderer, WP_LEFT_X, cy, +1, c);
                    filled_circle(renderer, WP_LEFT_X, cy, WP_RADIUS, c);
                }
            }

            char label[3] = {'1' + (char)i, 0, 0};
            ui_draw_text(cache, renderer, fonts->mono, label,
                         (SDL_Color){180, 180, 180, 255},
                         WP_LEFT_X - 38, cy - 8);
        }

        for (int i = 0; i < nb; i++) {
            int cy   = topY + i * WP_ROW_GAP;
            SDL_Color c = WIRES[rightOrder[i]].color;
            int occupied = (rightConnectedTo[i] >= 0);
            int sockSz = WP_RADIUS * 2 + 10;

            wire_stub(renderer, WP_RIGHT_X, cy, -1,
                      occupied ? (SDL_Color){(Uint8)(c.r/3),(Uint8)(c.g/3),(Uint8)(c.b/3),255} : c);

            if (tex_socket) {
                SDL_Rect dst = {WP_RIGHT_X - sockSz/2, cy - sockSz/2, sockSz, sockSz};
                if (occupied) {
                    SDL_SetTextureAlphaMod(tex_socket, 100);
                    SDL_RenderCopy(renderer, tex_socket, NULL, &dst);
                    SDL_SetTextureAlphaMod(tex_socket, 255);
                } else {
                    SDL_RenderCopy(renderer, tex_socket, NULL, &dst);

                    hollow_circle(renderer, WP_RIGHT_X, cy, WP_RADIUS + 5, c);
                }
            } else {
                if (occupied) {
                    SDL_Color dim = {(Uint8)(c.r/3),(Uint8)(c.g/3),(Uint8)(c.b/3),255};
                    filled_circle(renderer, WP_RIGHT_X, cy, WP_RADIUS - 2, dim);
                    hollow_circle(renderer, WP_RIGHT_X, cy, WP_RADIUS, dim);
                } else {
                    SDL_Color fill = {15, 18, 28, 255};
                    filled_circle(renderer, WP_RIGHT_X, cy, WP_RADIUS - 2, fill);
                    hollow_circle(renderer, WP_RIGHT_X, cy, WP_RADIUS, c);
                }
            }

            char label[3] = {'A' + (char)i, 0, 0};
            ui_draw_text(cache, renderer, fonts->mono, label,
                         (SDL_Color){180, 180, 180, 255},
                         WP_RIGHT_X + 28, cy - 8);
        }

        ui_draw_text(cache, renderer, fonts->bebasHeader,
            "CABLAGE ELECTRIQUE", (SDL_Color){235, 180, 60, 255}, 30, 22);

        {
            Uint32 elap2 = now - startTick;
            if (elap2 > timerMs) elap2 = timerMs;
            int barW = 260;
            int barX = W_ECRAN - barW - 30;
            int barY = 28;
            SDL_SetRenderDrawColor(renderer, 25, 28, 40, 255);
            SDL_Rect bg = {barX, barY, barW, 12};
            SDL_RenderFillRect(renderer, &bg);
            float frac = 1.f - (float)elap2 / (float)timerMs;
            SDL_Color bc = frac > .66f ? (SDL_Color){70,210,80,255}
                         : frac > .33f ? (SDL_Color){230,190,50,255}
                                       : (SDL_Color){230,70,50,255};
            SDL_SetRenderDrawColor(renderer, bc.r, bc.g, bc.b, 255);
            SDL_Rect fg = {barX, barY, (int)(barW * frac), 12};
            SDL_RenderFillRect(renderer, &fg);
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%02d s",
                     (int)((timerMs - elap2) / 1000));
            ui_draw_text(cache, renderer, fonts->mono, tbuf,
                         (SDL_Color){235,180,60,255}, W_ECRAN - 58, 22);
        }

        {
            int bx0 = W_ECRAN/2 - (WIRE_MAX_ATTEMPTS * 28) / 2;
            int by  = H_ECRAN - 52;
            for (int b = 0; b < WIRE_MAX_ATTEMPTS; b++) {
                int filled = (b < WIRE_MAX_ATTEMPTS - wrongAttempts);
                SDL_Color c = filled ? (SDL_Color){240,200,60,255}
                                     : (SDL_Color){70,50,50,255};
                int bx = bx0 + b * 28;
                SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
                SDL_RenderDrawLine(renderer, bx,    by+10, bx+6,  by);
                SDL_RenderDrawLine(renderer, bx+6,  by,    bx+4,  by+8);
                SDL_RenderDrawLine(renderer, bx+4,  by+8,  bx+14, by+8);
                SDL_RenderDrawLine(renderer, bx+14, by+8,  bx+8,  by+16);
            }
        }

        if (!complete && (now - startTick) < 4000) {
            ui_draw_text_centered(cache, renderer,
                fonts->story ? fonts->story : fonts->mono,
                "Faites glisser chaque fil vers la prise de meme couleur",
                (SDL_Color){200, 210, 230, 200},
                W_ECRAN/2, panelY + WP_PANEL_H + 14);
        }
        if (complete) {
            ui_draw_text_centered(cache, renderer,
                fonts->bebasTitle ? fonts->bebasTitle : fonts->mono,
                "ACCES AUTORISE",
                (SDL_Color){70, 230, 100, 255},
                W_ECRAN/2, H_ECRAN/2 - 20);
        }

        if (dragging && dragWire >= 0) {
            SDL_Color c = WIRES[dragWire].color;
            filled_circle(renderer, dragX, dragY, 8, c);
            hollow_circle(renderer, dragX, dragY, 10,
                          (SDL_Color){255, 255, 255, 180});
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < nbSparks; i++) {
            if (sparks[i].alpha <= 0) continue;
            SDL_SetRenderDrawColor(renderer, 255, 220, 100, (Uint8)sparks[i].alpha);
            SDL_Rect sr = {(int)sparks[i].x - 2, (int)sparks[i].y - 2, 5, 5};
            SDL_RenderFillRect(renderer, &sr);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    if (tex_panel)  SDL_DestroyTexture(tex_panel);
    if (tex_cap)    SDL_DestroyTexture(tex_cap);
    if (tex_socket) SDL_DestroyTexture(tex_socket);
}

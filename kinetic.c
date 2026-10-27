#include "kinetic.h"
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define KIN_SCR_W   800     /* must match W_ECRAN in background.h */
#define KIN_SCR_H   520     /* must match H_ECRAN in background.h */
#define KIN_NUM_WAGONS  5   /* must match MAX_WAGONS in background.h */
#define KIN_RT_W    (3 * KIN_SCR_W)   /* worldRT holds three wagon slots */

/* How much to zoom the hull PNG past a plain "fit screen height".  >1 makes
 * the centre wagon bigger (the image's top/bottom metal then runs off-screen,
 * which is fine -- it's just frame), at the cost of the side windows being
 * pushed further toward the screen edges (so the prev/next wagons peek less).
 * Bump toward ~3.0 for an old-style big wagon with no rest-peek; ~1.0 to see
 * all three windows full-size. */
#define KIN_HULL_ZOOM   2.2f
/* Logical y the centre of the hull's centre window is anchored to. */
#define KIN_WIN_CY      254

/* The y-line of the rushing wasteland behind the train. */
#define KIN_HORIZON (KIN_WORLD_Y + KIN_WORLD_H - 30)

/* ----------------------------------------------------------------- RNG --- */
static unsigned kin_rand(KineticTrain *k)
{
    k->rng = k->rng * 1664525u + 1013904223u;
    return (k->rng >> 16) & 0x7fffu;
}
static float kin_frand(KineticTrain *k) { return (float)kin_rand(k) / 32767.0f; }

/* stateless integer hash, for stable procedural shapes */
static unsigned kin_hash(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}

/* ----------------------------------------------------------- load / free - */
static SDL_Texture *kin_try_load(SDL_Renderer *r, const char *path)
{
    SDL_Surface *s = IMG_Load(path);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

/* Analyse the hull PNG.  It may carry 1 or 3 transparent windows arranged
 * horizontally (with metal "coupling" between them); the centre (or only) one
 * is where the player's wagon shows through, the side ones show the previous /
 * next wagons peeking in.  The image is drawn at native aspect, scaled to the
 * screen height, with the centre window centred horizontally.  Fills:
 *     k->hullDst  - where to blit the whole hull image (camX=0, rattle=0)
 *     k->slotW    - logical px between adjacent windows (==KIN_SCR_W if 1)
 *     k->worldDst - the centre wagon's on-screen rect (cover-fit into window)
 * Returns 1 on success; on failure the caller keeps its defaults. */
static int kin_analyze_hull(const char *path, KineticTrain *k)
{
    SDL_Surface *raw = IMG_Load(path);
    if (!raw) return 0;
    SDL_Surface *s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(raw);
    if (!s) return 0;
    int w = s->w, h = s->h, ok = 0;
    if (w < 16 || h < 16) { SDL_FreeSurface(s); return 0; }

    if (SDL_LockSurface(s) == 0) {
        const Uint8 *px = (const Uint8 *)s->pixels;
        int    pitch  = s->pitch;
        Uint32 amask  = s->format->Amask;
        int    ashift = s->format->Ashift;
        #define KIN_ALPHA(X,Y) ((( *(const Uint32*)(px + (Y)*(size_t)pitch + (X)*4) ) & amask) >> ashift)
        const Uint32 THR = 60;
        int scanY = h / 2;

        /* horizontal runs of transparent pixels on the centre scanline = windows */
        #define KIN_MAX_WIN 8
        int rl[KIN_MAX_WIN], rr[KIN_MAX_WIN], nrun = 0;
        for (int x = 0; x < w; ) {
            if (KIN_ALPHA(x, scanY) < THR) {
                int x0 = x;
                while (x < w && KIN_ALPHA(x, scanY) < THR) x++;
                if (x - x0 > w / 16 && nrun < KIN_MAX_WIN) {
                    rl[nrun] = x0; rr[nrun] = x - 1; nrun++;
                }
            } else x++;
        }
        if (nrun >= 1) {
            int ci = nrun / 2;                         /* centre window index */
            int cwL = rl[ci], cwR = rr[ci];
            int cmx = (cwL + cwR) / 2;
            int cwT = scanY; while (cwT > 0     && KIN_ALPHA(cmx, cwT) < THR) cwT--;
            int cwB = scanY; while (cwB < h - 1 && KIN_ALPHA(cmx, cwB) < THR) cwB++;
            cwT++; cwB--;
            int cww = cwR - cwL + 1, cwh = cwB - cwT + 1;
            int cwCx = (cwL + cwR) / 2, cwCy = (cwT + cwB) / 2;
            if (cww > w / 12 && cwh > h / 8) {
                /* fit the image to the screen height, then zoom in so the
                 * playable centre wagon is a usable size (the image's outer
                 * metal runs off-screen, which is fine) */
                float S     = KIN_HULL_ZOOM * (float)KIN_SCR_H / (float)h;
                int   drawW = (int)(w * S + 0.5f);
                int   drawH = (int)(h * S + 0.5f);
                int   drawX = (int)(KIN_SCR_W * 0.5f - cwCx * S + 0.5f);
                int   drawY = (int)(KIN_WIN_CY - cwCy * S + 0.5f);
                k->hullDst  = (SDL_Rect){ drawX, drawY, drawW, drawH };

                if (nrun >= 2) {
                    int sumGap = 0;
                    for (int i = 1; i < nrun; i++)
                        sumGap += ((rl[i] + rr[i]) - (rl[i-1] + rr[i-1])) / 2;
                    int gapImg = sumGap / (nrun - 1);
                    k->slotW = (int)(gapImg * S + 0.5f);
                } else {
                    k->slotW = KIN_SCR_W;                      /* lone window  */
                }
                if (k->slotW < 64) k->slotW = KIN_SCR_W;

                /* centre window in logical coords, widened a hair so the wagon
                 * edges tuck under the metal */
                float winX = drawX + cwL * S - 4.0f;
                float winY = drawY + cwT * S - 4.0f;
                float winW = cww  * S + 8.0f;
                float winH = cwh  * S + 8.0f;
                /* cover-fit the 800x520 wagon into it, anchored bottom-centre */
                float sc = winW / KIN_SCR_W;
                if (winH / KIN_SCR_H > sc) sc = winH / KIN_SCR_H;
                int fw = (int)(KIN_SCR_W * sc + 0.5f);
                int fh = (int)(KIN_SCR_H * sc + 0.5f);
                int fx = (int)(winX + (winW - fw) * 0.5f + 0.5f);
                int fy = (int)(winY + winH - fh + 0.5f);
                k->worldDst = (SDL_Rect){ fx, fy, fw, fh };
                ok = 1;
            }
        }
        #undef KIN_MAX_WIN
        #undef KIN_ALPHA
        SDL_UnlockSurface(s);
    }
    SDL_FreeSurface(s);
    return ok;
}

static void kin_respawn_wind(KineticTrain *k, KinWind *w, int firstFill)
{
    w->x     = firstFill ? kin_frand(k) * KIN_SCR_W
                         : (float)KIN_SCR_W + kin_frand(k) * 80.0f;
    w->y     = kin_frand(k) * KIN_SCR_H;
    w->front = (kin_rand(k) % 3u == 0u);
    w->len   = 2 + (int)(kin_frand(k) * 3.0f);                  /* 2..4 */
    w->speed = (w->front ? 640.0f : 360.0f) + kin_frand(k) * 360.0f;
    w->alpha = (w->front ? 75 : 36) + (int)(kin_frand(k) * 48.0f);
}

void kinetic_init(KineticTrain *k, SDL_Renderer *r)
{
    memset(k, 0, sizeof(*k));
    k->rng = (SDL_GetTicks() ^ 0x9e3779b9u) | 1u;
    k->enabled = 1;                  /* cinematic "outside" view by default */

    k->camX = 0; k->slideActive = 0; k->slideDir = 1;

    k->rttOk = 0;
    if (SDL_RenderTargetSupported(r) == SDL_TRUE) {
        /* the wagon art is downscaled into the viewport -- smooth it */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        k->worldRT = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET,
                                       KIN_RT_W, KIN_SCR_H);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        if (k->worldRT) {
            SDL_SetTextureBlendMode(k->worldRT, SDL_BLENDMODE_NONE);
            k->rttOk = 1;
        }
    }
    if (!k->rttOk)
        fprintf(stderr, "[kinetic] render targets unavailable; "
                        "running without the shrunk viewport\n");

    k->texHull = kin_try_load(r, "train_hull.png");
    k->texFar  = kin_try_load(r, "parallax_far.png");
    k->texMid  = kin_try_load(r, "parallax_mid.png");
    k->texBlur = kin_try_load(r, "blur_ground.png");
    if (k->texHull) SDL_SetTextureBlendMode(k->texHull, SDL_BLENDMODE_BLEND);
    if (k->texMid)  SDL_SetTextureBlendMode(k->texMid,  SDL_BLENDMODE_BLEND);

    /* the hull PNG decides where the wagons are composited; without it, fall
     * back to the fixed KIN_WORLD_* rectangle and a single 800-wide slot */
    k->worldDst = (SDL_Rect){ KIN_WORLD_X, KIN_WORLD_Y, KIN_WORLD_W, KIN_WORLD_H };
    k->hullDst  = (SDL_Rect){ 0, 0, KIN_SCR_W, KIN_SCR_H };
    k->slotW    = KIN_SCR_W;
    if (k->texHull) kin_analyze_hull("train_hull.png", k);

    for (int i = 0; i < KIN_WIND_MAX; i++) kin_respawn_wind(k, &k->wind[i], 1);
}

void kinetic_free(KineticTrain *k)
{
    if (!k) return;
    if (k->worldRT) SDL_DestroyTexture(k->worldRT);
    if (k->texHull) SDL_DestroyTexture(k->texHull);
    if (k->texFar)  SDL_DestroyTexture(k->texFar);
    if (k->texMid)  SDL_DestroyTexture(k->texMid);
    if (k->texBlur) SDL_DestroyTexture(k->texBlur);
    memset(k, 0, sizeof(*k));
}

/* ----------------------------------------------------------------- update */
void kinetic_update(KineticTrain *k, float dt)
{
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    /* micro-rattle of the train body (re-rolled every ~3-5 frames) */
    k->rattleTimer -= dt;
    if (k->rattleTimer <= 0.0f) {
        int v = (int)(kin_rand(k) % 3u) - 1;                /* -1 / 0 / 1 */
        if (kin_rand(k) % 7u == 0u) v *= 2;                 /* occasional +-2 */
        k->rattleY = v;
        k->rattleX = (kin_rand(k) % 13u == 0u) ? ((int)(kin_rand(k) % 3u) - 1) : 0;
        k->rattleTimer = 0.055f + kin_frand(k) * 0.040f;
    }

    /* parallax (slow) + foreground (fast) scroll, px/s.
     * Speeds bumped up after removing the moving rail strip so the
     * outside view still feels like the train is moving fast. */
    k->offFar  += 48.0f  * dt;
    k->offMid  += 180.0f * dt;
    k->offBlur += 540.0f * dt;
    if (k->offFar  >= 1.0e6f) k->offFar  = fmodf(k->offFar,  4096.0f);
    if (k->offMid  >= 1.0e6f) k->offMid  = fmodf(k->offMid,  4096.0f);
    if (k->offBlur >= 1.0e6f) k->offBlur = fmodf(k->offBlur, 4096.0f);

    /* aquarium pulse */
    k->pulse += dt * 1.8f;
    if (k->pulse > 6.2831853f) k->pulse -= 6.2831853f;

    /* wind streaks */
    for (int i = 0; i < KIN_WIND_MAX; i++) {
        KinWind *w = &k->wind[i];
        w->x -= w->speed * dt;
        if (w->x < -10.0f) kin_respawn_wind(k, w, 0);
    }
}

/* -------------------------------------------------------- world target -- */
int kinetic_begin_world(KineticTrain *k, SDL_Renderer *r)
{
    if (!k->enabled)              return 0;     /* classic full-screen view */
    if (!k->rttOk || !k->worldRT) return 0;

    /* Switch to the off-screen texture (which is two wagon-slots wide) and set
     * the logical size to the texture size: scale 1, full viewport, so the
     * wagon-draw routines' "ox" offsets land 1:1 in texture pixels. */
    if (SDL_SetRenderTarget(r, k->worldRT)) return 0;
    SDL_RenderSetClipRect(r, NULL);
    SDL_RenderSetLogicalSize(r, KIN_RT_W, KIN_SCR_H);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    return 1;
}

void kinetic_end_world(KineticTrain *k, SDL_Renderer *r)
{
    if (!k->enabled)              return;
    if (!k->rttOk || !k->worldRT) return;
    SDL_SetRenderTarget(r, NULL);
    SDL_RenderSetClipRect(r, NULL);
    /* Re-apply logical size now that the target is the window again: SDL
     * recomputes the centered letterbox viewport for the actual output size,
     * which is what fullscreen needs (the previous SetViewport on the texture
     * had clobbered it). */
    SDL_RenderSetLogicalSize(r, KIN_SCR_W, KIN_SCR_H);
}

/* ----------------------------------------------------------- backdrop --- */
static void kin_stepped_peak(SDL_Renderer *r, int x, int per, int baseY, int peak,
                             Uint8 cr, Uint8 cg, Uint8 cb)
{
    const int steps = 7;
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    for (int s = 0; s < steps; s++) {
        int inset = (per / 2) * (steps - 1 - s) / (steps - 1);
        SDL_Rect m = { x + inset, baseY - peak + s * peak / steps,
                       per - 2 * inset, peak / steps + 2 };
        SDL_RenderFillRect(r, &m);
    }
    SDL_Rect down = { x, baseY, per, KIN_SCR_H - baseY };
    SDL_RenderFillRect(r, &down);
}

void kinetic_render_backdrop(KineticTrain *k, SDL_Renderer *r)
{
    if (!k->enabled) return;                /* classic view: no backdrop      */
    if (!k->rttOk)   return;                /* no shrink -> no border to fill */

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* ---- cold overcast sky -------------------------------------------- */
    SDL_SetRenderDrawColor(r, 13, 17, 26, 255);
    SDL_Rect full = {0, 0, KIN_SCR_W, KIN_SCR_H};
    SDL_RenderFillRect(r, &full);
    for (int b = 0; b < 6; b++) {           /* faint haze toward the horizon */
        SDL_SetRenderDrawColor(r, 13 + b * 3, 17 + b * 3, 26 + b * 3, 255);
        SDL_Rect band = {0, KIN_HORIZON - 90 + b * 14, KIN_SCR_W, 14};
        SDL_RenderFillRect(r, &band);
    }

    /* ---- LAYER C far: distant frozen mountains (very slow) ------------- */
    if (k->texFar) {
        int tw, th; SDL_QueryTexture(k->texFar, NULL, NULL, &tw, &th);
        if (tw > 0 && th > 0) {
            int off = (int)k->offFar % tw; if (off < 0) off += tw;
            for (int x = -off; x < KIN_SCR_W; x += tw) {
                SDL_Rect d = {x, KIN_SCR_H - th, tw, th};
                SDL_RenderCopy(r, k->texFar, NULL, &d);
            }
        }
    } else {
        for (int row = 0; row < 2; row++) {
            int   per = (row == 0) ? 150 : 112;
            float spd = (row == 0) ? 0.55f : 1.0f;
            int   baseY = KIN_HORIZON + (row == 0 ? -4 : 12);
            Uint8 cr = (row == 0) ? 19 : 27, cg = (row == 0) ? 24 : 33,
                  cb = (row == 0) ? 35 : 46;
            int off = (int)(k->offFar * spd) % per; if (off < 0) off += per;
            for (int i = -1; i <= KIN_SCR_W / per + 1; i++) {
                unsigned h = kin_hash((unsigned)i * 2654435761u + 11u + row * 71u);
                int peak = (row == 0 ? 55 : 80) + (int)(h % (row == 0 ? 70u : 105u));
                kin_stepped_peak(r, i * per - off, per, baseY, peak, cr, cg, cb);
            }
        }
    }

    /* ---- ground band beyond the train --------------------------------- */
    SDL_SetRenderDrawColor(r, 15, 17, 21, 255);
    SDL_Rect ground = {0, KIN_HORIZON + 24, KIN_SCR_W, KIN_SCR_H - (KIN_HORIZON + 24)};
    SDL_RenderFillRect(r, &ground);

    /* ---- LAYER B(mid): near rocks & dead trees (medium speed) --------- */
    if (k->texMid) {
        int tw, th; SDL_QueryTexture(k->texMid, NULL, NULL, &tw, &th);
        if (tw > 0 && th > 0) {
            int off = (int)k->offMid % tw; if (off < 0) off += tw;
            for (int x = -off; x < KIN_SCR_W; x += tw) {
                SDL_Rect d = {x, KIN_SCR_H - th, tw, th};
                SDL_RenderCopy(r, k->texMid, NULL, &d);
            }
        }
    } else {
        int per = 76, gy = KIN_HORIZON + 20;
        int off = (int)k->offMid % per; if (off < 0) off += per;
        for (int i = -1; i <= KIN_SCR_W / per + 1; i++) {
            unsigned h = kin_hash((unsigned)i * 374761393u + 5u);
            int x = i * per - off;
            if ((h & 1u) == 0u) {                       /* dead leafless tree */
                int ht = 24 + (int)(h % 30u);
                SDL_SetRenderDrawColor(r, 13, 15, 13, 255);
                SDL_Rect trunk = {x + per / 2, gy - ht, 3, ht + 4};
                SDL_RenderFillRect(r, &trunk);
                SDL_RenderDrawLine(r, x + per/2 + 1, gy - ht + 5, x + per/2 + 9, gy - ht);
                SDL_RenderDrawLine(r, x + per/2 + 1, gy - ht + 9, x + per/2 - 7, gy - ht + 3);
                SDL_RenderDrawLine(r, x + per/2 + 1, gy - ht + 13, x + per/2 + 8, gy - ht + 8);
            } else {                                    /* ice-crusted boulder */
                int rw = 22 + (int)(h % 30u), rh = 12 + (int)((h >> 5) % 14u);
                SDL_SetRenderDrawColor(r, 23, 29, 37, 255);
                SDL_Rect rock = {x + (per - rw) / 2, gy - rh, rw, rh + 8};
                SDL_RenderFillRect(r, &rock);
                SDL_SetRenderDrawColor(r, 66, 82, 100, 255);
                SDL_RenderDrawLine(r, rock.x, rock.y, rock.x + rw, rock.y);
            }
        }
    }
}

/* --------------------------------------------------------- present world */
static void kin_aquarium_pulse(KineticTrain *k, SDL_Renderer *r, const SDL_Rect *dst)
{
    float s = 0.5f + 0.5f * sinf(k->pulse);
    Uint8 a = (Uint8)(7.0f + 24.0f * s);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(r, 26, 104, 196, a);
    SDL_RenderFillRect(r, dst);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void kinetic_present_world(KineticTrain *k, SDL_Renderer *r,
                           int wagonActif, int wagonCible,
                           int transActif, int transDir, int transOff)
{
    (void)wagonCible;
    if (!k->enabled)              return;
    if (!k->rttOk || !k->worldRT) return;

    /* the chain pans by exactly one slot over the course of a door transition */
    if (transOff < 0)         transOff = 0;
    if (transOff > KIN_SCR_W) transOff = KIN_SCR_W;
    int camX = (transActif && transOff < KIN_SCR_W)
                 ? transDir * transOff * k->slotW / KIN_SCR_W : 0;
    k->camX        = camX;
    k->slideActive = transActif;
    k->slideDir    = transDir;

    SDL_SetTextureBlendMode(k->worldRT, SDL_BLENDMODE_NONE);

    /* worldRT holds 3 wagon-slots: slot N = wagon (wagonActif-1+N), at
     * x[N*800 .. N*800+800].  On screen, slot N's window sits at the centre
     * window position offset by (N-1)*slotW, then panned by -camX. */
    for (int N = 0; N < 3; N++) {
        SDL_Rect dst = { k->worldDst.x + (N - 1) * k->slotW - camX + k->rattleX,
                         k->worldDst.y + k->rattleY,
                         k->worldDst.w, k->worldDst.h };
        if (dst.x + dst.w <= 0 || dst.x >= KIN_SCR_W) continue;   /* off-screen */
        int wi = wagonActif - 1 + N;
        if (wi < 0 || wi >= KIN_NUM_WAGONS) {
            /* no wagon there (the ends of the train) -- a sealed black bulkhead
             * so the hull's side window doesn't show the empty backdrop */
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderFillRect(r, &dst);
            continue;
        }
        SDL_Rect src = { N * KIN_SCR_W, 0, KIN_SCR_W, KIN_SCR_H };
        SDL_RenderCopy(r, k->worldRT, &src, &dst);
        if (wi == 2) kin_aquarium_pulse(k, r, &dst);            /* Aquarium glow */
    }
}

/* --------------------------------------------------------------- hull --- */
static void kin_corner_frost(SDL_Renderer *r, int cx, int cy, int sx, int sy)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    unsigned fr = 0x51ed270bu ^ (unsigned)(cx * 7919 + cy * 104729) ^ (unsigned)(sx * 3 + sy);
    for (int n = 0; n < 30; n++) {
        fr = fr * 1664525u + 1013904223u; int a  = (fr >> 16) % 24u;
        fr = fr * 1664525u + 1013904223u; int b  = (fr >> 16) % 24u;
        fr = fr * 1664525u + 1013904223u; int sz = 1 + (int)((fr >> 16) % 3u);
        int reach = a + b;
        if (reach > 34) continue;
        int px = cx + sx * a, py = cy + sy * b;
        Uint8 alpha = (Uint8)(165 - reach * 4);
        SDL_SetRenderDrawColor(r, 206, 227, 246, alpha);
        SDL_Rect fp = { sx < 0 ? px - sz + 1 : px, sy < 0 ? py - sz + 1 : py, sz, sz };
        SDL_RenderFillRect(r, &fp);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void kinetic_render_hull(KineticTrain *k, SDL_Renderer *r)
{
    if (!k->enabled) return;            /* classic view: no hull frame */

    int camX = k->camX;

    /* PNG hull frame: blitted at native aspect (scaled to screen height) so
     * its transparent windows let the wagons / parallax through.  We draw the
     * image tiled at slot offsets {-slotW, 0, +slotW}, all panned by -camX, so
     * the metal "couplings" line up with neighbouring slots and the camera
     * pans seamlessly hull-to-hull during a wagon transition. */
    if (k->texHull) {
        SDL_SetTextureBlendMode(k->texHull, SDL_BLENDMODE_BLEND);
        for (int t = -1; t <= 1; t++) {
            SDL_Rect d = { k->hullDst.x + t * k->slotW - camX + k->rattleX,
                           k->hullDst.y + k->rattleY,
                           k->hullDst.w, k->hullDst.h };
            if (d.x + d.w <= 0 || d.x >= KIN_SCR_W) continue;
            SDL_RenderCopy(r, k->texHull, NULL, &d);
        }
        return;
    }

    int T = KIN_HULL_T;
    int ox0, oy0, ox1, oy1;     /* outer extent of the bezel */
    int ix0, iy0, ix1, iy1;     /* inner hole (the visible car) */
    if (k->rttOk) {
        int X = k->worldDst.x - camX + k->rattleX, Y = k->worldDst.y + k->rattleY;
        int OV = 4;             /* let the bezel bite a touch into the car */
        ox0 = X - T;          oy0 = Y - T;
        ox1 = X + k->worldDst.w + T; oy1 = Y + k->worldDst.h + T;
        ix0 = X + OV;         iy0 = Y + OV;
        ix1 = X + k->worldDst.w - OV; iy1 = Y + k->worldDst.h - OV;
    } else {                    /* no shrink: a thin frame inset from the edges */
        ox0 = 0; oy0 = 0; ox1 = KIN_SCR_W; oy1 = KIN_SCR_H;
        ix0 = T; iy0 = T; ix1 = KIN_SCR_W - T; iy1 = KIN_SCR_H - T;
    }

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* 1. dark gunmetal bezel (four bands) */
    SDL_SetRenderDrawColor(r, 50, 52, 58, 255);
    SDL_Rect bTop = {ox0, oy0, ox1 - ox0, iy0 - oy0};
    SDL_Rect bBot = {ox0, iy1, ox1 - ox0, oy1 - iy1};
    SDL_Rect bLft = {ox0, iy0, ix0 - ox0, iy1 - iy0};
    SDL_Rect bRgt = {ix1, iy0, ox1 - ix1, iy1 - iy0};
    SDL_RenderFillRect(r, &bTop); SDL_RenderFillRect(r, &bBot);
    SDL_RenderFillRect(r, &bLft); SDL_RenderFillRect(r, &bRgt);

    /* 2. a darker weld crease down the middle of each band */
    int mTop = (oy0 + iy0) / 2, mBot = (iy1 + oy1) / 2;
    int mLft = (ox0 + ix0) / 2, mRgt = (ix1 + ox1) / 2;
    SDL_SetRenderDrawColor(r, 33, 34, 39, 255);
    SDL_RenderDrawLine(r, ox0, mTop, ox1, mTop);
    SDL_RenderDrawLine(r, ox0, mBot, ox1, mBot);
    SDL_RenderDrawLine(r, mLft, oy0, mLft, oy1);
    SDL_RenderDrawLine(r, mRgt, oy0, mRgt, oy1);

    /* 3. bevel highlights / shadows */
    SDL_SetRenderDrawColor(r, 96, 100, 110, 255);          /* lit top-left */
    if (k->rttOk) { SDL_RenderDrawLine(r, ix0, iy0, ix1, iy0);
                    SDL_RenderDrawLine(r, ix0, iy0, ix0, iy1); }
    SDL_RenderDrawLine(r, ox0, oy0, ox1, oy0);
    SDL_RenderDrawLine(r, ox0, oy0, ox0, oy1);
    SDL_SetRenderDrawColor(r, 21, 22, 26, 255);            /* shadowed bot-right */
    if (k->rttOk) { SDL_RenderDrawLine(r, ix0, iy1, ix1, iy1);
                    SDL_RenderDrawLine(r, ix1, iy0, ix1, iy1); }
    SDL_RenderDrawLine(r, ox0, oy1 - 1, ox1, oy1 - 1);
    SDL_RenderDrawLine(r, ox1 - 1, oy0, ox1 - 1, oy1);

    /* 4. riveted bolts along each band */
    for (int x = ox0 + 16; x < ox1 - 8; x += 34) {
        SDL_Rect rv = {x - 2, mTop - 2, 4, 4};
        SDL_SetRenderDrawColor(r, 29, 30, 35, 255); SDL_RenderFillRect(r, &rv);
        SDL_SetRenderDrawColor(r, 124, 128, 138, 255);
        SDL_RenderDrawPoint(r, x - 1, mTop - 2);
        rv.y = mBot - 2;
        SDL_SetRenderDrawColor(r, 29, 30, 35, 255); SDL_RenderFillRect(r, &rv);
        SDL_SetRenderDrawColor(r, 124, 128, 138, 255);
        SDL_RenderDrawPoint(r, x - 1, mBot - 2);
    }
    for (int y = oy0 + 16; y < oy1 - 8; y += 34) {
        SDL_Rect rv = {mLft - 2, y - 2, 4, 4};
        SDL_SetRenderDrawColor(r, 29, 30, 35, 255); SDL_RenderFillRect(r, &rv);
        SDL_SetRenderDrawColor(r, 124, 128, 138, 255);
        SDL_RenderDrawPoint(r, mLft - 2, y - 1);
        rv.x = mRgt - 2;
        SDL_SetRenderDrawColor(r, 29, 30, 35, 255); SDL_RenderFillRect(r, &rv);
        SDL_SetRenderDrawColor(r, 124, 128, 138, 255);
        SDL_RenderDrawPoint(r, mRgt - 2, y - 1);
    }

    /* 5. ice / frost crusting in the corners */
    if (k->rttOk) {
        kin_corner_frost(r, ix0, iy0,  1,  1);
        kin_corner_frost(r, ix1, iy0, -1,  1);
        kin_corner_frost(r, ix0, iy1,  1, -1);
        kin_corner_frost(r, ix1, iy1, -1, -1);
        /* a soft inner shadow so the car sits inside the hull */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
        SDL_Rect s1 = {ix0, iy0, ix1 - ix0, 3};
        SDL_Rect s2 = {ix0, iy1 - 3, ix1 - ix0, 3};
        SDL_Rect s3 = {ix0, iy0, 3, iy1 - iy0};
        SDL_Rect s4 = {ix1 - 3, iy0, 3, iy1 - iy0};
        SDL_RenderFillRect(r, &s1); SDL_RenderFillRect(r, &s2);
        SDL_RenderFillRect(r, &s3); SDL_RenderFillRect(r, &s4);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }
}

/* --------------------------------------------------------- blur ground -- */
void kinetic_render_blur_ground(KineticTrain *k, SDL_Renderer *r)
{
    if (!k->enabled) return;            /* classic view: no rushing ground */
    if (!k->rttOk)   return;

    int beltH = 28;
    int beltY = k->worldDst.y + k->worldDst.h - 6;            /* at the car's belly */
    if (beltY + beltH > KIN_SCR_H) beltY = KIN_SCR_H - beltH;
    if (beltY < 0) beltY = 0;

    if (k->texBlur) {
        int tw, th; SDL_QueryTexture(k->texBlur, NULL, NULL, &tw, &th);
        if (tw > 0 && th > 0) {
            int off = (int)k->offBlur % tw; if (off < 0) off += tw;
            for (int x = -off; x < KIN_SCR_W; x += tw) {
                SDL_Rect d = {x, beltY, tw, beltH};
                SDL_RenderCopy(r, k->texBlur, NULL, &d);
            }
        }
        return;
    }

    /* procedural: dark base + fast horizontal speed-streaks */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 17, 19, 23, 255);
    SDL_Rect base = {0, beltY, KIN_SCR_W, beltH};
    SDL_RenderFillRect(r, &base);
    SDL_SetRenderDrawColor(r, 56, 58, 66, 255);             /* rail glints */
    SDL_RenderDrawLine(r, 0, beltY + 8,  KIN_SCR_W, beltY + 8);
    SDL_RenderDrawLine(r, 0, beltY + 9,  KIN_SCR_W, beltY + 9);
    SDL_RenderDrawLine(r, 0, beltY + 19, KIN_SCR_W, beltY + 19);

    const int span = KIN_SCR_W + 220;
    int off = (int)k->offBlur;
    for (int i = 0; i < 64; i++) {
        unsigned h = kin_hash((unsigned)i * 2654435761u + 3u);
        int len  = 16 + (int)(h % 44u);
        int yy   = beltY + 2 + (int)((h >> 6) % (unsigned)(beltH - 4));
        int xx   = span - ((off + i * 53) % span);
        if (xx > KIN_SCR_W && xx - len > KIN_SCR_W) continue;
        switch ((h >> 4) % 3u) {
            case 0:  SDL_SetRenderDrawColor(r, 198, 208, 224, 200); break;  /* snow  */
            case 1:  SDL_SetRenderDrawColor(r,  88,  90, 100, 200); break;  /* steel */
            default: SDL_SetRenderDrawColor(r,  70,  54,  38, 200); break;  /* dirt  */
        }
        SDL_RenderDrawLine(r, xx, yy, xx + len, yy);
    }
    /* shadow line where the train belly meets the rushing ground */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 130);
    SDL_Rect sh = {0, beltY, KIN_SCR_W, 3};
    SDL_RenderFillRect(r, &sh);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* ---------------------------------------------------------- wind streaks - */
static void kin_draw_wind(KineticTrain *k, SDL_Renderer *r, int front)
{
    if (!k->enabled) return;            /* classic view: no wind streaks */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < KIN_WIND_MAX; i++) {
        KinWind *w = &k->wind[i];
        if (w->front != front) continue;
        int x = (int)w->x, y = (int)w->y;
        Uint8 a = (Uint8)w->alpha;
        SDL_SetRenderDrawColor(r, 224, 235, 250, a);
        SDL_RenderDrawLine(r, x, y, x + w->len, y);              /* bright head */
        SDL_SetRenderDrawColor(r, 224, 235, 250, (Uint8)(a / 3 + 1));
        SDL_RenderDrawLine(r, x + w->len, y, x + 2 * w->len + 3, y);  /* faint tail */
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void kinetic_render_wind_back (KineticTrain *k, SDL_Renderer *r) { kin_draw_wind(k, r, 0); }
void kinetic_render_wind_front(KineticTrain *k, SDL_Renderer *r) { kin_draw_wind(k, r, 1); }

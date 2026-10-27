#ifndef KINETIC_H
#define KINETIC_H

/* ---------------------------------------------------------------------------
 * Kinetic Train Overhaul.
 *
 * Turns the static "wagon interior" view into a high-speed, cinematic train
 * sequence:
 *   1. a shrunk "viewport" + a metallic train-hull frame letterboxing the
 *      action (the wagon world is rendered to an off-screen target, then
 *      composited smaller and framed);
 *   2. a three-layer movement system -- far parallax landscape (slow), the
 *      interior (static, plus a micro-rattle), and a motion-blur ground
 *      strip (fast);
 *   3. atmospheric wind streaks, some behind the car, some over the HUD;
 *   4. a soft blue light pulse inside the Aquarium wagon.
 *
 * The new layers (hull / parallax / blur ground) use optional PNG art when
 * present and fall back to procedural rendering otherwise.  Drop these files
 * next to the executable to use real pixel art:
 *      train_hull.png      800x520, transparent center  (the hull frame)
 *      parallax_far.png    tileable strip, dark mountains + overcast sky
 *      parallax_mid.png    tileable strip with alpha, frozen rocks / dead trees
 *      blur_ground.png     tileable short strip, motion-blurred rails + snow
 * ------------------------------------------------------------------------- */

#include <SDL2/SDL.h>

#define KIN_WIND_MAX  48

/* The shrunk wagon "viewport" inside the 800x520 screen (~0.80 scale). */
#define KIN_WORLD_X   80
#define KIN_WORLD_Y   36
#define KIN_WORLD_W   640
#define KIN_WORLD_H   416
/* Hull bezel thickness, drawn outward from the viewport into the border. */
#define KIN_HULL_T    18

typedef struct {
    float x, y;       /* head position on screen                         */
    float speed;      /* leftward speed, px/s                            */
    int   len;        /* streak length, 2..4 px                          */
    int   alpha;      /* base alpha                                       */
    int   front;      /* 1 = drawn over the HUD, 0 = behind the car      */
} KinWind;

typedef struct {
    int          enabled;             /* 1 = cinematic outside view,
                                         0 = classic full-screen inside view */
    SDL_Texture *worldRT;             /* 2400x520 render target, 3 wagon-slots:
                                         slot0 = wagonActif-1, slot1 = wagonActif
                                         (full), slot2 = wagonActif+1            */
    int          rttOk;               /* 0 if render targets unsupported  */
    SDL_Rect     worldDst;            /* the centre wagon's on-screen rect
                                         (cover-fit into the hull's centre
                                         window), 800x520 logical, at camX=0   */
    SDL_Rect     hullDst;             /* where the whole hull PNG is blitted,
                                         logical coords, at camX=0/rattle=0     */
    int          slotW;              /* logical px between adjacent hull
                                         windows; ==800 for a 1-window hull    */

    /* train-camera state, set by kinetic_present_world() and read back by
     * kinetic_render_hull() so the hull copies slide with the wagons */
    int    camX;                      /* logical-px the chain is panned by */
    int    slideActive;               /* 1 during a wagon-to-wagon slide   */
    int    slideDir;                  /* +1 = panning right, -1 = left     */

    int    rattleX, rattleY;          /* current micro-rattle, screen px  */
    float  rattleTimer;

    float  offFar, offMid, offBlur;   /* parallax / foreground scroll     */
    float  pulse;                     /* aquarium pulse phase (radians)   */

    KinWind  wind[KIN_WIND_MAX];
    unsigned rng;                     /* tiny LCG state                   */

    SDL_Texture *texHull;             /* optional art, NULL -> procedural */
    SDL_Texture *texFar;
    SDL_Texture *texMid;
    SDL_Texture *texBlur;
} KineticTrain;

void kinetic_init  (KineticTrain *k, SDL_Renderer *r);
void kinetic_free  (KineticTrain *k);
void kinetic_update(KineticTrain *k, float dt);

/* Render the wagon world into the off-screen target.  Returns 1 when the
 * target is active; 0 means render to the screen as before (no shrink). */
int  kinetic_begin_world(KineticTrain *k, SDL_Renderer *r);
void kinetic_end_world  (KineticTrain *k, SDL_Renderer *r);

/* Composite, in this order, after kinetic_end_world():
 *   backdrop  -> wind_back -> present_world -> hull -> blur_ground
 * then the game's HUD / dialogue / pause overlay, then:
 *   wind_front
 *
 * present_world takes the current wagon + (during a transition) the wagon
 * being slid into, and the transitionSlide state (active / direction / offset
 * in [0..800]); it stashes the resulting train-camera pan into the struct so
 * render_hull lines its hull copies up with the wagons.
 */
void kinetic_render_backdrop   (KineticTrain *k, SDL_Renderer *r);
void kinetic_render_wind_back  (KineticTrain *k, SDL_Renderer *r);
void kinetic_present_world     (KineticTrain *k, SDL_Renderer *r,
                                int wagonActif, int wagonCible,
                                int transActif, int transDir, int transOff);
void kinetic_render_hull       (KineticTrain *k, SDL_Renderer *r);
void kinetic_render_blur_ground(KineticTrain *k, SDL_Renderer *r);
void kinetic_render_wind_front (KineticTrain *k, SDL_Renderer *r);

#endif /* KINETIC_H */

#include "arduino_input.h"

#include <SDL2/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* SDLK_UNKNOWN sentinel — a direction/button that emits no second key. */
#define SC_NONE SDLK_UNKNOWN

#define DEAD_ENTER   80
#define DEAD_EXIT    30

#define SETTLE_FRAMES  60
#define CALIB_SAMPLES  25

/* Each stick direction may emit up to two keys at once: one that drives the
 * player in-game and one that navigates menus.  They are kept distinct so a
 * single physical push is meaningful in exactly one context — see the P1
 * preset below for why this matters in two-controller co-op.              */
typedef struct {
    SDL_Keycode shoot, melee, menu, sel, back, sw;
    SDL_Keycode leftA,  leftB;
    SDL_Keycode rightA, rightB;
    SDL_Keycode upA,    upB;
    SDL_Keycode downA,  downB;
} ArduinoKeymap;

/* Player 1: q/z/d move the character in-game; a/w/s navigate menus; NO arrow
 * keys are ever emitted.  This is the crucial bit for co-op: two_player_poll
 * reads the arrow keys as *player 2's* movement, so if controller 1 emitted
 * arrows it would drag P2 around whenever P1 moved.  Right/down only need a
 * single key each (d already means both "P1 right" and "menu right"; s is
 * menu-only since P1 has no in-game down action).                          */
static const ArduinoKeymap KEYMAP_P1 = {
    .shoot = SDLK_f, .melee = SDLK_e,
    .menu  = SDLK_ESCAPE, .sel = SDLK_RETURN, .back = SDLK_BACKSPACE,
    .sw    = SDLK_LSHIFT,
    .leftA  = SDLK_q,    .leftB  = SDLK_a,   /* game + menu  */
    .rightA = SDLK_d,    .rightB = SC_NONE,  /* d covers both */
    .upA    = SDLK_z,    .upB    = SDLK_w,   /* jump + menu  */
    .downA  = SDLK_s,    .downB  = SC_NONE,  /* menu only    */
};

/* Player 2: the arrow keys move the character in-game (two_player_poll maps
 * arrows -> P2) and also navigate menus, so one key per direction suffices.
 * Attack on k/l, dash on RShift — mirroring the campaign's P2 keyboard.    */
static const ArduinoKeymap KEYMAP_P2 = {
    .shoot = SDLK_k, .melee = SDLK_l,
    .menu  = SDLK_ESCAPE, .sel = SDLK_RETURN, .back = SDLK_BACKSPACE,
    .sw    = SDLK_RSHIFT,
    .leftA  = SDLK_LEFT,  .leftB  = SC_NONE,
    .rightA = SDLK_RIGHT, .rightB = SC_NONE,
    .upA    = SDLK_UP,    .upB    = SC_NONE,
    .downA  = SDLK_DOWN,  .downB  = SC_NONE,
};

typedef struct {
    int shoot, melee, menu, sel, back, sw;
    int x, y;
} ArdState;

struct ArduinoController {
    int           fd;
    int           debug;
    int           invertX, invertY, swapXY;
    ArduinoKeymap km;
    const char   *tag;          /* "P1" / "P2" for log lines */

    char     lineBuf[256];
    int      lineLen;
    int      haveBaseline;
    ArdState prev;
    int      prevDirX, prevDirY;
    int      settleCount;
    int      calibCount;
    long     calibSumX, calibSumY;
    int      centerX, centerY;
};

static void push_key(SDL_Keycode k, int down)
{
    if (k == SC_NONE) return;
    SDL_Event e;
    SDL_zero(e);
    e.type              = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state         = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat        = 0;
    e.key.timestamp     = SDL_GetTicks();
    e.key.windowID      = 0;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    e.key.keysym.sym    = k;
    e.key.keysym.mod    = 0;
    SDL_PushEvent(&e);
}

static void edge(ArduinoController *c, int prev, int cur, SDL_Keycode k)
{
    if (cur && !prev) {
        if (c->debug) fprintf(stderr, "[arduino %s] DOWN keycode=%d\n", c->tag, (int)k);
        push_key(k, 1);
    } else if (!cur && prev) {
        if (c->debug) fprintf(stderr, "[arduino %s] UP   keycode=%d\n", c->tag, (int)k);
        push_key(k, 0);
    }
}

static int find_int(const char *line, const char *key, int *out)
{
    size_t klen = strlen(key);
    const char *p = line;
    while ((p = strstr(p, key)) != NULL) {


        int ok = (p == line) || (p[-1] == ',');
        if (ok && p[klen] == ':') {
            char *end;
            long v = strtol(p + klen + 1, &end, 10);
            if (end != p + klen + 1) {
                *out = (int)v;
                return 1;
            }
        }
        p += klen;
    }
    return 0;
}

static int classify_axis(int v, int center, int prev)
{
    int delta = v - center;
    int adel  = delta < 0 ? -delta : delta;
    int sign  = (delta < 0) ? -1 : (delta > 0 ? +1 : 0);

    if (prev == 0) {
        return (adel > DEAD_ENTER) ? sign : 0;
    }
    if (adel < DEAD_EXIT) return 0;
    if (sign != prev && adel > DEAD_ENTER) return sign;
    return prev;
}

static void apply_state(ArduinoController *c, const ArdState *s)
{
    if (!c->haveBaseline) {
        c->prev = *s;
        c->prevDirX = 0;
        c->prevDirY = 0;
        c->haveBaseline = 1;
        return;
    }

    edge(c, c->prev.shoot, s->shoot, c->km.shoot);
    edge(c, c->prev.melee, s->melee, c->km.melee);
    edge(c, c->prev.menu,  s->menu,  c->km.menu);
    edge(c, c->prev.sel,   s->sel,   c->km.sel);
    edge(c, c->prev.back,  s->back,  c->km.back);
    edge(c, c->prev.sw,    s->sw,    c->km.sw);

    int rawX = s->x;
    int rawY = s->y;
    if (c->swapXY)  { int t = rawX; rawX = rawY; rawY = t; }
    if (c->invertX) rawX = 1023 - rawX;
    if (c->invertY) rawY = 1023 - rawY;

    if (c->settleCount < SETTLE_FRAMES) {
        c->settleCount++;
        c->prev = *s;
        return;
    }
    if (c->calibCount < CALIB_SAMPLES) {
        c->calibSumX += rawX;
        c->calibSumY += rawY;
        c->calibCount++;
        if (c->calibCount == CALIB_SAMPLES) {
            c->centerX = (int)(c->calibSumX / CALIB_SAMPLES);
            c->centerY = (int)(c->calibSumY / CALIB_SAMPLES);
            fprintf(stderr, "[arduino %s] calibrated center: X=%d Y=%d "
                    "(swap=%d invX=%d invY=%d)\n", c->tag,
                    c->centerX, c->centerY, c->swapXY, c->invertX, c->invertY);
        }
        c->prev = *s;
        return;
    }

    int dx = classify_axis(rawX, c->centerX, c->prevDirX);
    int dy = classify_axis(rawY, c->centerY, c->prevDirY);

    if (c->debug && (dx != c->prevDirX || dy != c->prevDirY)) {
        const char *dxs = (dx<0)?"L":(dx>0)?"R":"-";
        const char *dys = (dy<0)?"U":(dy>0)?"D":"-";
        fprintf(stderr, "[arduino %s] DIR x=%s y=%s  (rawX=%d rawY=%d)\n",
                c->tag, dxs, dys, rawX, rawY);
    }

    if (dx != c->prevDirX) {
        if (c->prevDirX == -1) { push_key(c->km.leftA,  0); push_key(c->km.leftB,  0); }
        if (c->prevDirX == +1) { push_key(c->km.rightA, 0); push_key(c->km.rightB, 0); }
        if (dx == -1)          { push_key(c->km.leftA,  1); push_key(c->km.leftB,  1); }
        if (dx == +1)          { push_key(c->km.rightA, 1); push_key(c->km.rightB, 1); }
        c->prevDirX = dx;
    }
    if (dy != c->prevDirY) {
        if (c->prevDirY == -1) { push_key(c->km.upA,   0); push_key(c->km.upB,   0); }
        if (c->prevDirY == +1) { push_key(c->km.downA, 0); push_key(c->km.downB, 0); }
        if (dy == -1)          { push_key(c->km.upA,   1); push_key(c->km.upB,   1); }
        if (dy == +1)          { push_key(c->km.downA, 1); push_key(c->km.downB, 1); }
        c->prevDirY = dy;
    }

    c->prev = *s;
}

static void parse_line(ArduinoController *c, char *line)
{
    if (c->debug) fprintf(stderr, "[arduino %s] RX: %s\n", c->tag, line);
    ArdState s = c->haveBaseline ? c->prev
                                 : (ArdState){ 0, 0, 0, 0, 0, 0, 512, 512 };
    find_int(line, "SHOOT",  &s.shoot);
    find_int(line, "MELEE",  &s.melee);
    find_int(line, "MENU",   &s.menu);
    find_int(line, "SELECT", &s.sel);
    find_int(line, "BACK",   &s.back);
    find_int(line, "SW",     &s.sw);
    find_int(line, "X",      &s.x);
    find_int(line, "Y",      &s.y);
    apply_state(c, &s);
}

/* Read a per-controller env var: P1 uses "ARDUINO_<name>", P2 uses
 * "ARDUINO2_<name>", so each box can be tuned (invert / swap / debug)
 * independently.  Returns the raw string or NULL if unset.               */
static const char *env_for(ArduinoPlayer player, const char *name)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "ARDUINO%s_%s",
             player == ARD_PLAYER2 ? "2" : "", name);
    return getenv(buf);
}

ArduinoController *arduino_input_open(const char *path, ArduinoPlayer player)
{
    if (!path || !*path) path = "/dev/ttyACM0";

    ArduinoController *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->fd  = -1;
    c->tag = (player == ARD_PLAYER2) ? "P2" : "P1";
    c->km  = (player == ARD_PLAYER2) ? KEYMAP_P2 : KEYMAP_P1;

    c->debug = (env_for(player, "DEBUG") != NULL);
    {
        const char *e = env_for(player, "INVERT_X");
        c->invertX = e ? atoi(e) != 0 : 1;
    }
    {
        const char *e = env_for(player, "INVERT_Y");
        c->invertY = e ? atoi(e) != 0 : 1;
    }
    c->swapXY  = (env_for(player, "SWAP_XY") != NULL);

    c->centerX = 512;
    c->centerY = 512;

    c->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (c->fd < 0) {
        fprintf(stderr, "[arduino %s] open %s: %s (controller disabled)\n",
                c->tag, path, strerror(errno));
        free(c);
        return NULL;
    }

    struct termios tio;
    if (tcgetattr(c->fd, &tio) != 0) {
        fprintf(stderr, "[arduino %s] tcgetattr: %s\n", c->tag, strerror(errno));
        close(c->fd); free(c); return NULL;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |=  (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |=  CS8;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(c->fd, TCSANOW, &tio) != 0) {
        fprintf(stderr, "[arduino %s] tcsetattr: %s\n", c->tag, strerror(errno));
        close(c->fd); free(c); return NULL;
    }
    tcflush(c->fd, TCIOFLUSH);

    fprintf(stderr, "[arduino %s] %s ready (115200 8N1)\n", c->tag, path);
    return c;
}

void arduino_input_close(ArduinoController *c)
{
    if (!c) return;
    if (c->fd >= 0) close(c->fd);
    free(c);
}

void arduino_input_send_event(ArduinoController *c, const char *name)
{
    if (!c || c->fd < 0 || !name || !*name) return;
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "EVT:%s\n", name);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = sizeof(buf);

    ssize_t wrote = write(c->fd, buf, (size_t)n);
    (void)wrote;
}

void arduino_input_poll(ArduinoController *c)
{
    if (!c || c->fd < 0) return;

    char buf[256];
    for (;;) {
        ssize_t n = read(c->fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n' || ch == '\r') {
                if (c->lineLen > 0) {
                    c->lineBuf[c->lineLen] = '\0';
                    parse_line(c, c->lineBuf);
                    c->lineLen = 0;
                }
            } else if (c->lineLen < (int)sizeof(c->lineBuf) - 1) {
                c->lineBuf[c->lineLen++] = ch;
            } else {
                c->lineLen = 0;
            }
        }
    }
}

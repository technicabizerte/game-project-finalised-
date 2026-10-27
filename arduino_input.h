#ifndef ARDUINO_INPUT_H
#define ARDUINO_INPUT_H

/* Opaque per-controller handle.  The game can open more than one Arduino
 * box at a time (e.g. one per player for co-op / versus); each handle owns
 * its own serial fd, calibration state, and key layout.                   */
typedef struct ArduinoController ArduinoController;

/* Which player a controller drives.  Selects the key layout the box's
 * buttons + stick are synthesized into:
 *
 *   ARD_PLAYER1 — movement on q/z/d (+ w/a/s for menus), attack f / melee e,
 *                 dash LShift.  Emits NO arrow keys, so it can never bleed
 *                 into player 2's arrow-key movement during co-op.
 *   ARD_PLAYER2 — movement on the arrow keys (which two_player_poll reads as
 *                 P2 and which also navigate menus), attack k / melee l,
 *                 dash RShift.                                            */
typedef enum {
    ARD_PLAYER1 = 0,
    ARD_PLAYER2 = 1
} ArduinoPlayer;

/* Open the serial controller on `path` (NULL / "" defaults to /dev/ttyACM0)
 * with the key layout for `player`.  Returns a handle on success, or NULL if
 * the port could not be opened — the caller treats a NULL handle as "no
 * controller" and the game keeps running.  poll/close/send_event all accept
 * NULL and become no-ops, so callers never need to branch on it.          */
ArduinoController *arduino_input_open(const char *path, ArduinoPlayer player);

/* Close and free a handle (NULL-safe). */
void arduino_input_close(ArduinoController *c);

/* Drain pending serial bytes and push synthetic SDL key events.  Call once
 * per frame, before SDL_PollEvent, for every open controller (NULL-safe).  */
void arduino_input_poll(ArduinoController *c);

/* Send a game event (HIT / DEAD / SHOOT / MELEE / WIN) to a controller's
 * LEDs + LCD (NULL-safe; dropped silently if the TX buffer is full).      */
void arduino_input_send_event(ArduinoController *c, const char *name);

#endif

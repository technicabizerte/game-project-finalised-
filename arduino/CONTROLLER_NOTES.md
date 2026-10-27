# Snowpiercer Arduino Controller — Build & Debug Notes

Summary of the controller integration work, written so future-you can pick this up cold.

## Hardware (final wiring)

Buttons are wired between the listed pin and **GND**. The sketch uses
`INPUT_PULLUP`, so a press pulls the pin low.

| Component                 | Pin    | Notes                                                                  |
|---------------------------|--------|------------------------------------------------------------------------|
| Button SHOOT              | D4     |                                                                        |
| Button MELEE              | D5     |                                                                        |
| Button MENU               | D6     | maps to ESC / pause                                                    |
| Button SELECT             | D7     | maps to ENTER / confirm                                                |
| Button BACK               | D8     | maps to BACKSPACE / cancel                                             |
| Analog-stick click (SW)   | **D9** | originally on D11; moved when D11 was repurposed for the yellow LED    |
| Red LED (+220 Ω → GND)    | D10    | HIT, DEAD                                                              |
| Yellow LED (+220 Ω → GND) | D11    | SHOOT, MELEE, WIN                                                      |
| Analog stick X            | A0     |                                                                        |
| Analog stick Y            | A1     |                                                                        |
| LCD SDA                   | A4     | I²C is hardware-locked to A4/A5 on Uno                                 |
| LCD SCL                   | A5     | same                                                                   |

**Power note:** the joystick module needs 5 V; on 3.3 V the analog range is
compressed and centers shift. The module we ended up using rests around
**X≈506, Y≈460** rather than the textbook 512/512.

## Linux side (USB-serial)

Board enumerates as `/dev/ttyUSB0` (CH340/CP2102). Genuine Unos use
`ttyACM0` — change the path in `arduino_input_open(...)` in `main.c` if
you swap boards.

Permissions: user must be in the `dialout` group:

```bash
sudo usermod -aG dialout $USER
# log out and back in (or: newgrp dialout)
```

Don't `chmod a+rw /dev/ttyUSB0` as a fix — the permissions reset on every
replug or reboot.

## Serial protocol

**Arduino → PC** (~50 Hz, one CSV line per loop, terminated by `\n`):
```
SHOOT:0,MELEE:0,MENU:0,SELECT:0,BACK:0,X:512,Y:512,SW:0
```
Buttons are 0/1, axes are 0..1023.

**PC → Arduino** (game events, newline-terminated):
```
EVT:HIT
EVT:DEAD
EVT:SHOOT
EVT:MELEE
EVT:WIN
```
Sketch reacts by lighting an LED with an auto-off timer and updating the LCD.

Baud: **115 200, 8N1**, no flow control.

## Source files

| File                                  | Role                                                                |
|---------------------------------------|---------------------------------------------------------------------|
| `arduino/snowpiercer_controller.ino`  | Sketch: reads inputs, sends CSV, receives EVT:*, drives LCD + LEDs  |
| `arduino_input.h`                     | Public API (open/close/poll/send_event)                             |
| `arduino_input.c`                     | Termios setup, line parser, hysteresis, calibration, event writer   |
| `main.c`                              | Calls `arduino_input_open` once, `arduino_input_poll` each frame, `arduino_input_send_event` at game events, `arduino_input_close` on shutdown |
| `Makefile`                            | Adds `arduino_input.c` / `.h` to the build                          |

The C side pushes synthetic `SDL_KEYDOWN`/`SDL_KEYUP` events into SDL's queue,
so existing keyboard handlers in `main.c` light up automatically — no
per-state controller logic.

## Key mapping (edit `SC_*_KEY` defines in `arduino_input.c`)

| Source            | Emitted key(s)            | Effect in game                 |
|-------------------|---------------------------|--------------------------------|
| SHOOT (D4)        | `SDLK_f`                  | P1 shoot                       |
| MELEE (D5)        | `SDLK_e`                  | P1 melee / interact            |
| MENU (D6)         | `SDLK_ESCAPE`             | back / cancel                  |
| SELECT (D7)       | `SDLK_RETURN`             | confirm                        |
| BACK (D8)         | `SDLK_BACKSPACE`          | spare cancel                   |
| Stick click (D9)  | `SDLK_LSHIFT`             | P1 dash                        |
| Stick LEFT        | `SDLK_LEFT` + `SDLK_q`    | menu nav + P1 left             |
| Stick RIGHT       | `SDLK_d`                  | menu nav + P1 right            |
| Stick UP          | `SDLK_UP` + `SDLK_z`      | menu nav + P1 jump             |
| Stick DOWN        | `SDLK_DOWN`               | menu nav (no in-game action)   |

The asymmetry on the stick is on purpose: in main.c menus, `SDLK_d` and
`SDLK_s` already navigate, so emitting both `SDLK_RIGHT` + `SDLK_d` would
double-fire (cursor jumps two cells). Only emit one key for those two
directions. UP and LEFT emit two each because their second key (`q`, `z`)
isn't a menu binding.

## Tunable env vars (`arduino_input.c`)

| Var                  | Default | Effect                                                                   |
|----------------------|---------|--------------------------------------------------------------------------|
| `ARDUINO_DEBUG=1`    | off     | Logs every CSV line received and every direction-classify decision       |
| `ARDUINO_INVERT_X=0` | **on**  | This module reads X with the sign flipped; default-on inverts it back. Set to `0` if you swap to a differently-wired stick |
| `ARDUINO_INVERT_Y=0` | **on**  | This module reads Y with the sign flipped (mounting orientation: push-up reads high). Default-on inverts it back. Set to `0` if the joystick gets remounted differently |
| `ARDUINO_SWAP_XY=1`  | off     | Swaps X and Y axes (use if pushing up moves X instead of Y)              |

Example (one-off run with verbose logging):
```bash
ARDUINO_DEBUG=1 ./jeu 2> arduino.log
```

## Constants worth knowing

| Define          | Value | What it does                                                       |
|-----------------|-------|--------------------------------------------------------------------|
| `DEAD_ENTER`    | 80    | Stick must move this far from center to engage a direction         |
| `DEAD_EXIT`     | 30    | Must come back within this much of center to disengage             |
| `CALIB_SAMPLES` | 25    | First N frames after open are averaged to compute true rest center |

Hysteresis (engage > release) kills flicker around the threshold. Auto-cal
absorbs the X≈506 / Y≈460 bias of cheap modules.

## Bugs we hit and how we fixed them

1. **Game compiled and ran but no controller input.** Cause: forgot to
   upload the sketch. Fix: upload via Arduino IDE (port = `/dev/ttyUSB0`,
   board = Uno, baud = 115200). Always close the IDE Serial Monitor before
   running the game — it holds the port.

2. **Stick was "all over the place".** Two bugs:
   - Single-threshold deadzone caused flicker around ±200 (fixed with
     hysteresis 80/30).
   - Stick-down emitted both `SDLK_DOWN` and `SDLK_s`, making menus jump
     two cells per push (fixed by emitting just one key on those axes).

3. **Y axis biased ~50 below 512.** Fixed by per-axis auto-calibration:
   first 25 frames average → true center. Prints
   `[arduino] calibrated center: X=… Y=…` once per run.

4. **X axis "completely dead".** Stable ~506 with no response to L/R push.
   Cause: `SW` had been wired to `A1` instead of the stick's `VRy`, so the
   stick's X output had no path to A0. Fix: physical rewire. After the
   fix, X readings worked but were sign-flipped (right = low) — covered by
   defaulting `ARDUINO_INVERT_X` to on.

5. **`cat /dev/ttyUSB0` printed only one line.** Cause: kernel applied
   default tty line-discipline. Workaround:
   ```bash
   stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb -echo raw
   cat /dev/ttyUSB0
   ```
   Better: use the in-game `ARDUINO_DEBUG=1` log, which shows raw RX and
   the directional decision in one place.

6. **D11 conflict.** D11 was the stick-click input AND was needed for the
   yellow LED. Resolution: moved SW from D11 to D9 in the sketch and on
   the breadboard.

7. **Axis cross-coupling that looks like a worn gimbal but is actually
   wiring.** Symptoms in-game: pushing right makes the character jump
   instead of walking, pushing up does nothing, character drifts in one
   direction on its own at rest. Diagnostic shows the calibrated center
   pinned near a rail (e.g. `X=4 Y=905`, or `Y=638` when actual rest is
   ~505), and "pure" UP/DOWN pushes drag X by as much or more than Y.
   The data looks identical to a worn joystick gimbal, but it isn't —
   it's the breadboard.

   **Causes seen so far, in order of how often they were the answer:**
   - **Dangling jumpers from removed components.** A jumper still
     plugged into the Arduino but with the other end loose, or plugged
     into a breadboard row that shares a column with an analog input
     (A0/A1) or a power rail. The floating end picks up noise OR the
     row is shorted to wherever the loose end happens to land. Either
     way, A0 or A1 gets held at a rail or wobbles, calibration captures
     a bogus center, and every push from rest reads as a direction.
   - **Joystick power on a shared breadboard rail.** LEDs or LCD
     pulling current from the same +5V/GND rail bounce the joystick's
     reference voltage. Calibration captures one rest voltage, gameplay
     sees a different one once the LCD/LEDs are active. Drift, ghost
     directions, jumps on horizontal push.
   - **Stick physically not at rest during calibration.** Calibration
     averages the first 25 frames after settle (frames 60..84 with the
     SETTLE_FRAMES patch). If your hand or a wire is on the stick
     during that window, the captured center is wherever the stick was
     held. Symptom is the same rail-pinned calibration value.

   **Diagnostic recipe to distinguish from real gimbal failure:**
   1. Pull every wire that isn't the joystick (GND, +5V, VRx, VRy, SW),
      using direct Arduino pins, no breadboard rails shared with
      anything else. Hands off. Run with `ARDUINO_DEBUG=1` and check
      `grep calibrated`. Healthy: X≈505–533, Y≈490–510.
   2. If healthy on bare wiring → the module is fine, the breadboard is
      the problem. Reintroduce components one at a time and rerun the
      diagnostic after each; the offender is the last one added.
   3. If still rail-pinned on bare wiring → then it's the module. Buy
      a new one.

   **Fixes applied to the codebase:**
   - `SETTLE_FRAMES` (60 frames ≈ 1.2 s) added in `arduino_input.c`
     before calibration sampling, so LCD backlight inrush and LED
     initial states don't contaminate the captured center.
   - The joystick's +5V and GND should go directly to dedicated
     Arduino pins, not down a rail shared with the LEDs/LCD.

## Diagnostic recipe

If the controller stops working:

```bash
ARDUINO_DEBUG=1 ./jeu 2> arduino.log
# push the stick: UP, DOWN, LEFT, RIGHT, hold each ~1 s
# then quit
grep -E "ready|calibrated|RX:|DIR" arduino.log | head -60
```

What to look for:
- `[arduino] /dev/ttyUSB0 ready` — port opened.
- `[arduino] calibrated center: X=… Y=…` — calibration completed; values
  should be near 506/460 for this stick.
- `RX:` lines — should appear ~50/sec; X and Y change when stick moves; SW
  goes 0→1 on click.
- `DIR x=L|R|- y=U|D|-` — only on direction changes; hovering near center
  should produce no DIR lines (proves hysteresis is working).

## LCD + LEDs status

The sketch in `arduino/snowpiercer_controller.ino` includes:
- Bidirectional serial (CSV out, EVT:* in)
- LED outputs on D10/D11 with auto-off timers
- I²C LCD init at address `0x27` (change to `0x3F` if blank)

**Outstanding:** when last tested, `LiquidCrystal_I2C` wasn't available in
the user's Library Manager and a DIYables variant was downloaded instead.
The exact include and class name from that variant haven't been confirmed,
so the sketch may need the include line tweaked. If compilation fails
with a "no such file" or "undeclared" error around `lcd`, look at the
DIYables library's example sketches for the correct
`#include <…>` and class name, and replace the two lines marked at the top
of the sketch.

## Build

The Makefile builds everything in one shot:

```bash
make
./jeu
```

Equivalent direct invocation (for reference):

```bash
gcc main.c background.c combat.c ui.c audio.c quiz.c weaponfx.c \
    gameplay.c interlude_manager.c wire_puzzle.c highscore_screen.c \
    arduino_input.c \
    puzzle/enigme2.c puzzle/enigme2_systems.c puzzle/rotozoom.c \
    quizz/enigme/enigme.c "high score menu /leaderboard.c" \
    -o jeu \
    $(sdl2-config --cflags --libs) \
    -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm \
    -Ipuzzle -Iquizz/enigme
```

No extra link flags — `termios` / `open` / `read` / `write` are all in libc.

## Game-event hooks (where in `main.c`)

| Event   | Trigger                                               |
|---------|-------------------------------------------------------|
| SHOOT   | `gameplay_handle_player_shoot` returns true (P1 / P2) |
| MELEE   | E or L key pressed in `ETAT_JEU` (P1 / P2)            |
| HIT     | `bg.joueurs[i].hp` decreases between frames           |
| DEAD    | state transitions into `ETAT_GAMEOVER`                |
| WIN     | state transitions into `ETAT_ENDING`                  |

State transitions are detected with a `lastEtat` tracker so each event
fires exactly once per entry, not every frame.

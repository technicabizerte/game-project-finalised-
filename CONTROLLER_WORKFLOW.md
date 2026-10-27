# Controller Workflow & Code Explanation

This document explains **how the Arduino controller is integrated into the
game**, file by file, and walks through the runtime flow from a button press
on the physical box to a reaction on screen — and the reverse path, from a
game event back to an LED/LCD on the controller.

For hardware wiring, USB permissions, and the wire protocol, see
`arduino/CONTROLLER_NOTES.md`. This file focuses on the **code**.

---

## 1. The big picture

Three pieces of code participate:

```
   ┌────────────────────────────┐         USB-serial          ┌──────────────────────┐
   │ Arduino sketch             │  ── CSV @ 50 Hz ─────────►  │ arduino_input.c      │
   │ snowpiercer_controller.ino │                             │ (line parser +       │
   │  - reads buttons + stick   │  ◄── "EVT:HIT\n" etc. ──    │  SDL event injector) │
   │  - drives LEDs + LCD       │                             └──────────┬───────────┘
   └────────────────────────────┘                                        │
                                                                         │ pushes synthetic
                                                                         │ SDL_KEYDOWN / SDL_KEYUP
                                                                         ▼
                                                              ┌──────────────────────┐
                                                              │ main.c game loop     │
                                                              │  SDL_PollEvent(...)  │
                                                              └──────────────────────┘
```

The crucial design choice: **`arduino_input.c` does not expose a "controller
API" to the rest of the game.** It synthesizes the same `SDL_KEYDOWN` /
`SDL_KEYUP` events that a real keyboard would produce and pushes them into
SDL's event queue. Every existing menu and gameplay handler in `main.c`
already listens for those keys, so the controller "just works" everywhere
with zero per-state plumbing.

The only places `main.c` is aware of the controller are:

- `arduino_input_open(...)` — once at startup (`main.c:294`)
- `arduino_input_poll()`    — once per frame (`main.c:329`)
- `arduino_input_send_event(...)` — at a few game events (HIT, DEAD, SHOOT, MELEE, WIN)
- `arduino_input_close()`   — once at shutdown (`main.c:1436`)

---

## 2. File-by-file roles

| File                                      | Role                                                                             |
|-------------------------------------------|----------------------------------------------------------------------------------|
| `arduino/snowpiercer_controller/*.ino`    | Firmware running on the Uno: input sampling + event reaction (LEDs/LCD).         |
| `arduino_input.h`                         | Public C API — 4 functions, no controller types leak into the rest of the game.  |
| `arduino_input.c`                         | Serial port setup, line-by-line CSV parser, calibration, hysteresis, SDL injector. |
| `main.c`                                  | Calls open/poll/send_event/close at the right moments.                           |
| `Makefile`                                | Adds `arduino_input.c` to the build target.                                      |

---

## 3. Arduino sketch — `snowpiercer_controller.ino`

Two responsibilities, run in lockstep inside `loop()`:

### 3a. Sample inputs → emit CSV (PC-bound)

Every ~20 ms (≈ 50 Hz):

1. `digitalRead` each button. The buttons are `INPUT_PULLUP`, so **pressed = LOW**;
   the sketch flips this so `1` means "pressed."
2. `analogRead` the stick X and Y (0..1023).
3. Print one line on the serial port:
   ```
   SHOOT:0,MELEE:0,MENU:0,SELECT:0,BACK:0,X:512,Y:512,SW:0\n
   ```

### 3b. Receive game events → drive LEDs + LCD (PC → Arduino)

`pollIncoming()` accumulates serial bytes into `g_inBuf` until it sees `\n`/`\r`,
then calls `handleEvent(payload)`:

| Event   | LED        | Duration  |
|---------|------------|-----------|
| `HIT`   | red        | 250 ms    |
| `DEAD`  | red        | 1500 ms   |
| `SHOOT` | yellow     | 120 ms    |
| `MELEE` | yellow     | 120 ms    |
| `WIN`   | yellow     | 1500 ms   |

LEDs auto-extinguish: `handleEvent` records `g_redOffAt = now + duration`,
and the top of `loop()` checks `if (now >= g_redOffAt) digitalWrite(LOW)`.
This avoids any blocking `delay()` and keeps input sampling smooth.

The LCD shows the most recent event name on line 1 and a running event
counter on line 2.

---

## 4. PC side — `arduino_input.c`

This is the bridge. There are four public functions and a lot of state.

### 4a. `arduino_input_open(path)` — one-time setup

Called once from `main.c:294` with `/dev/ttyUSB0` (CH340 cable; genuine Unos
use `/dev/ttyACM0`).

Steps:

1. Read env vars: `ARDUINO_DEBUG`, `ARDUINO_INVERT_X`, `ARDUINO_INVERT_Y`,
   `ARDUINO_SWAP_XY`. **`INVERT_X` and `INVERT_Y` default to 1** because the
   physical module on this build has both axes wired sign-flipped.
2. `open()` the serial device non-blocking.
3. `tcgetattr` / `cfmakeraw` / `tcsetattr` to put the port in **raw 115200 8N1,
   no flow control**. `VMIN=0, VTIME=0` so reads return immediately if no data
   is buffered (we never want to stall the game's frame).
4. Reset all internal state (calibration counters, baseline flag, line
   buffer).

If anything fails the function logs a message and returns -1, but the game
keeps running — controller is treated as optional.

### 4b. `arduino_input_poll()` — called every frame

`main.c` calls this once per frame (`main.c:329`), **before** `SDL_PollEvent`.
Why before: `arduino_input_poll` pushes synthetic events into SDL's queue, so
the immediately-following `SDL_PollEvent` loop will consume them as if the
user had pressed those keys.

Inside `arduino_input_poll`:

```
read() into a 256-byte buffer (non-blocking)
for each byte:
    if '\n' or '\r' → terminate g_lineBuf, call parse_line(), reset
    else            → append to g_lineBuf
```

Multiple lines may be parsed per call if the kernel buffered several frames.

### 4c. `parse_line` → `apply_state` — the core logic

`parse_line` extracts each `KEY:value` field with `find_int()` (which guards
against false matches: a `,` or start-of-line must precede the key).

`apply_state(s)` is where the synthesis happens. Five things in order:

**1. Baseline frame.** The first frame after open just records state with no
events emitted — otherwise every button held at startup would generate a
spurious "press."

**2. Edge-triggered button events.** For every button, `edge(prev, cur, key)`:
- `0 → 1` pushes `SDL_KEYDOWN` for `key`
- `1 → 0` pushes `SDL_KEYUP`

That's it — the rest of the game sees a normal keystroke.

**3. Axis transforms.** Optional `swapXY`, then `1023 - raw` if inverted.

**4. Settle + calibrate (first ~85 frames after open).**

```
settle window (60 frames ≈ 1.2 s)  → ignore stick entirely
calibrate window (next 25 frames)  → average rawX/rawY → g_centerX/g_centerY
```

Why two phases: the LCD backlight + LED initial states pull the stick's
reference voltage off for ~1 s after launch. Sampling during that window
captures a biased center, which then makes the stick read as
"permanently held in some direction" once the rails settle. Skipping
`SETTLE_FRAMES` first solves that. **The user must not be holding the
stick at game launch.**

**5. Hysteresis classification.** `classify_axis(v, center, prev)`:

```
delta = v - center
if currently centered:
    only engage if |delta| > DEAD_ENTER  (80)
if currently engaged:
    only release if |delta| < DEAD_EXIT  (30)
    direction flip only if |delta| > DEAD_ENTER in opposite sign
```

A single threshold would flicker when the stick rests near the boundary;
the dual-threshold dead band kills that.

**6. Direction → key mapping.** When `dx` or `dy` changes:

| Stick direction | Keys pushed                   |
|-----------------|-------------------------------|
| left            | `SDLK_LEFT` + `SDLK_q`        |
| right           | `SDLK_d` (alone — see below)  |
| up              | `SDLK_UP` + `SDLK_z`          |
| down            | `SDLK_DOWN` (alone)           |

The asymmetry is deliberate. Menus accept arrows AND WASD for navigation:
`up = UP||w`, `down = DOWN||s`, `left = LEFT||a`, `right = RIGHT||d`. If we
emitted both an arrow AND its WASD twin for every direction, menus would
navigate twice per stick push wherever the WASD key happens to also be a
menu binding (that's `d` and `s`). So:

- `left` → `q + LEFT` is safe because `q` has **no** menu binding.
- `right` → just `d` (which already covers menus AND in-game P1 right).
- `up`   → `z + UP` is safe because `z` has **no** menu binding.
- `down` → just `DOWN` (P1 has no down action; emitting `s` would double-fire).

This is why the `#define`s in `arduino_input.c` look asymmetric — comment
above them spells it out.

### 4d. `arduino_input_send_event(name)` — game → controller

Writes `EVT:<name>\n` to the serial fd. Non-blocking; if the kernel TX
buffer is full the event is dropped silently. Events are advisory (visual
LED feedback) so dropping is preferable to stalling a frame.

### 4e. `arduino_input_close()` — shutdown

Plain `close(fd)`. No protocol shutdown handshake.

---

## 5. `main.c` integration points

Five call sites total:

| Location           | Call                                     | Purpose                                                                   |
|--------------------|------------------------------------------|---------------------------------------------------------------------------|
| `main.c:294`       | `arduino_input_open("/dev/ttyUSB0")`     | Open serial port once, after audio init, before main loop.                |
| `main.c:318`       | `arduino_input_send_event("DEAD")`       | Fired when state transitions into `ETAT_GAMEOVER`.                        |
| `main.c:319`       | `arduino_input_send_event("WIN")`        | Fired when state transitions into `ETAT_ENDING`.                          |
| `main.c:329`       | `arduino_input_poll()`                   | First thing inside the per-frame loop, before `SDL_PollEvent`.            |
| `main.c:541,553`   | `arduino_input_send_event("SHOOT")`      | When a player fires a bullet.                                             |
| `main.c:546,558`   | `arduino_input_send_event("MELEE")`      | When a player swings melee.                                               |
| `main.c:939`       | `arduino_input_send_event("HIT")`        | When the player takes damage.                                             |
| `main.c:1436`      | `arduino_input_close()`                  | Shutdown.                                                                 |

The `etat`-transition events (`DEAD`/`WIN`) use a `lastEtat` latch so each
transition fires exactly once instead of every frame the state holds.

---

## 6. End-to-end timeline of a single frame

Suppose the player pushes the stick fully right and presses SHOOT.

```
T=0    Arduino samples: SHOOT=1, X≈900, others 0/512
       Sends: "SHOOT:1,MELEE:0,...,X:900,Y:512,SW:0\n"

T+~1ms USB buffers it; kernel makes it readable on /dev/ttyUSB0.

Game frame begins:
   arduino_input_poll() reads the line.
   parse_line() extracts SHOOT=1 and X=900.
   apply_state():
     edge(prev.shoot=0, cur.shoot=1, SDLK_f)
        → SDL_PushEvent(SDL_KEYDOWN, SDLK_f)
     classify_axis(900, center≈506, prev=0)
        → delta=394 > DEAD_ENTER(80) → dx=+1
     dx changed 0 → +1
        → SDL_PushEvent(SDL_KEYDOWN, SDLK_d)

   SDL_PollEvent loop in main.c:332 sees:
     - KEYDOWN SDLK_f  → keyShoot = 1 (handled by gameplay)
     - KEYDOWN SDLK_d  → player 1 starts running right

   Gameplay code at main.c:541 fires the bullet, calls
   arduino_input_send_event("SHOOT").

   write(fd, "EVT:SHOOT\n", 10) — non-blocking, returns immediately.

T+~2ms Arduino's pollIncoming() reads "EVT:SHOOT\n":
       handleEvent("SHOOT") → digitalWrite(YELLOW, HIGH), g_yellowOffAt = now+120
       LCD line 1: "EVT: SHOOT      "
       LCD line 2: "COUNT: <n>"

T+120ms Arduino's loop() top: now >= g_yellowOffAt → LED off.
```

When the player releases the stick and SHOOT, the same path runs in reverse:
`SHOOT 1→0` produces `SDL_KEYUP SDLK_f`, and `dx +1→0` (after `|delta| <
DEAD_EXIT`) produces `SDL_KEYUP SDLK_d`.

---

## 7. Common gotchas worth re-reading the code for

- **No events at startup**: that's the `g_haveBaseline` guard in
  `apply_state` — the first frame is consumed silently to establish the
  "previous" state, otherwise any held button at boot would fire a press.
- **Stick reads as "permanently held"**: hands off the stick during the
  first ~1.2 s after launch. That's the `SETTLE_FRAMES` window; the
  calibration sample taken during it determines the dead-zone center.
- **Right and down stick directions only emit one key each** (`d` and
  `DOWN`), unlike left/up which emit two. This is intentional — see §4c
  step 6.
- **Controller absent ≠ game broken**: `arduino_input_open` failing logs a
  warning and disables the module. All subsequent `poll`/`send_event`/`close`
  calls become no-ops via the `g_fd < 0` guard.
- **Wrong device path?** Genuine Uno = `/dev/ttyACM0`, CH340/CP2102 clones
  (this build) = `/dev/ttyUSB0`. Edit `main.c:294` to swap.
- **Stick axis flipped after a hardware change?** Set `ARDUINO_INVERT_X=0`
  or `ARDUINO_INVERT_Y=0` in the environment instead of editing code; the
  current defaults assume this build's mounting orientation.

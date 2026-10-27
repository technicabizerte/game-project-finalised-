# Second Arduino Controller — Task Notes & Handoff

Adding a **second Arduino controller** so player 2 has their own physical box
(co-op + versus/PvP). This file is the pick-up-cold record: what's already
done in code, what's left on the hardware/VM side, and how to test.

> If you're resuming after a VM reset, the **code is already done and committed
> to the working tree** — jump to §4 (flash) and §5 (VM passthrough). Nothing
> in the game needs re-editing.

---

## 1. Status at a glance

| Piece | State |
|---|---|
| `arduino_input.c/.h` refactor (singleton → 2 instances) | ✅ done |
| `main.c` opens/polls/routes both controllers | ✅ done |
| Build (`make`) | ✅ clean, zero warnings on changed files |
| Controller 1 (P1) working | ✅ |
| Controller 2 board flashed | ✅ (115200, sends clean protocol) |
| VM sees the USB boards | ✅ both on `ttyUSB0` / `ttyUSB1` (no ACM) |
| Controller 2 buttons (k/l/SW-dash) | ✅ working |
| Controller 2 analog stick | ❌ **blocker** — intermittent **+5V** wire to the joystick |
| Both controllers tested in-game | ⬜ TODO (after the +5V fix) |

**Current blocker (hardware):** controller 2's joystick **+5V wire is
misfiring** (loose crimp / tired breadboard hole). Symptom: buttons + SW-click
work, but the analog reads only **left + up**, and the monitor showed the
**calibrated center pinned at 1023** instead of ~512. A railed rest = one pot
end-terminal floating; SW works because it only needs GND, so the open leg is
the pots' shared **+5V**. Fix: run +5V straight from an Arduino **5V** pin to
the joystick (not a shared rail), reseat/re-crimp or solder the joint. When
solid, rest reads ~512 and all four directions fire. **This is not fixable in
software** — the game calibrates the center from rest, so a railed signal makes
direction-reading impossible. See §7.5.

> Earlier VirtualBox USB-passthrough blocker is **resolved** — both boards pass
> through to the guest now. §5 kept for reference / after a VM reset.

---

## 2. What changed in the code

### Design decision (the important one)
The original single controller emitted **arrow keys** for movement. But
`src/two_player.c` (`two_player_poll`) reads the **arrow keys as player 2's
movement**. So if controller 1 kept emitting arrows, P1 moving would also drag
P2 around in co-op. The two keymaps are therefore cleanly separated:

| | Controller 1 → **Player 1** | Controller 2 → **Player 2** |
|---|---|---|
| Move (in-game) | `q` / `z` / `d` | **arrow keys** |
| Move (menus) | `a` / `w` / `s` | arrow keys |
| Arrows emitted? | **No** (so it never bleeds into P2) | Yes |
| Shoot / Melee | `f` / `e` | `k` / `l` |
| Dash | `LShift` | `RShift` |
| Menu / Select / Back | `Esc` / `Enter` / `Backspace` | same |

All menus were verified to accept WASD twins, so controller 1 still navigates
every screen despite no longer emitting arrows. Both pads can drive menus (one
via WASD, one via arrows).

### Files touched
- **`arduino_input.h`** — API is now instance-based:
  ```c
  typedef struct ArduinoController ArduinoController;   // opaque handle
  typedef enum { ARD_PLAYER1, ARD_PLAYER2 } ArduinoPlayer;
  ArduinoController *arduino_input_open(const char *path, ArduinoPlayer player);
  void arduino_input_close(ArduinoController *c);
  void arduino_input_poll(ArduinoController *c);
  void arduino_input_send_event(ArduinoController *c, const char *name);
  ```
  All four are NULL-safe — a controller that fails to open just disables that
  player; the game keeps running.
- **`arduino_input.c`** — every former global moved into the
  `ArduinoController` struct (fd, calibration, line buffer, hysteresis state),
  plus a per-instance `ArduinoKeymap`. Two presets: `KEYMAP_P1`, `KEYMAP_P2`.
- **`main.c`** — two file-scope handles `ctrl1` / `ctrl2`:
  - opened after audio init (env-overridable paths, see §6)
  - both polled every frame before `SDL_PollEvent`
  - event routing: P1 `SHOOT`/`MELEE` → `ctrl1`, P2's → `ctrl2`; `HIT` split
    per player (each box's red LED shows its own damage); `DEAD`/`WIN` → both
  - both closed at shutdown

---

## 3. Hardware pin mapping (identical for BOTH boards)

Buttons wired pin → **GND**, sketch uses `INPUT_PULLUP` (pressed = LOW).

| Component | Pin |
|---|---|
| Button SHOOT | D4 |
| Button MELEE | D5 |
| Button MENU | D6 |
| Button SELECT | D7 |
| Button BACK | D8 |
| Stick click (SW) | D9 |
| Red LED (+220Ω→GND) | D10 |
| Yellow LED (+220Ω→GND) | D11 |
| Stick X | A0 |
| Stick Y | A1 |
| LCD SDA | A4 |
| LCD SCL | A5 |

Joystick needs **5V** (3.3V compresses the range). Reference module rests
around X≈506, Y≈460 and is wired sign-flipped on both axes (hence the
`INVERT_X`/`INVERT_Y` defaults — see §6).

---

## 4. Flashing controller 2

Upload the **same** sketch `arduino/snowpiercer_controller/snowpiercer_controller.ino`
to the new board — no changes needed. Same pins, same
`Serial.begin(115200)`.

> **Baud is set by the sketch, not the board model.** A newer/different board
> still talks 115200 8N1 as long as it runs this sketch. The only thing that
> differs by board is the **device path** (`/dev/ttyACM*` vs `/dev/ttyUSB*`).

Close the Arduino IDE Serial Monitor after uploading — it holds the port and
blocks both the game and (on a VM) USB passthrough.

---

## 5. VirtualBox USB passthrough (the current blocker)

The guest sees no real USB devices. Diagnosis from inside the VM:
```bash
ls -l /dev/ttyUSB* /dev/ttyACM*     # "No such file or directory"
lsusb                               # only shows "VirtualBox USB Tablet"
```

Fix (most steps on the **host**, i.e. the real machine running VirtualBox):

1. **Host sees the boards?**
   ```bash
   lsusb | grep -iE "arduino|ch341|ch340|cp210|ftdi|serial"
   ls /dev/ttyUSB* /dev/ttyACM*
   ```
   If the host doesn't see them → cable/board problem, not the VM.

2. **Host user in `vboxusers`** (one time, then log out/in):
   ```bash
   sudo usermod -aG vboxusers $USER
   ```

3. **VM Settings → USB** (VM powered off): enable the USB controller. **USB
   1.1 (OHCI)** is enough and needs nothing extra. USB 2.0/3.0 requires the
   matching **Extension Pack** or USB breaks entirely.

4. **Pass the boards through** — either:
   - **Live:** VM running → host menu **Devices → USB →** click each board
     (shows as *QinHeng CH340* / *Arduino* / *USB Serial*).
   - **Persistent:** VM off → **Settings → USB →** add a **USB device filter**
     per board (add filters one at a time if the boards are identical clones).

5. **Re-check inside the VM:**
   ```bash
   ls -l /dev/ttyACM* /dev/ttyUSB* ; lsusb
   ```

6. **Guest permissions** (once nodes appear): guest user in `dialout`
   (already done for controller 1):
   ```bash
   sudo usermod -aG dialout $USER   # then log out/in
   ```

Gotchas: a host-side Serial Monitor holding the port stops passthrough; identical
clone boards need their filters added one-at-a-time to tell them apart.

> **VM reset note:** if the reset wipes the USB filters/Extension Pack, redo
> steps 3–4. A live "Devices → USB" attach (step 4 live option) is the fastest
> way back in after a reset.

---

## 6. Running with two controllers

**Confirmed on this rig:** both boards enumerate as **`ttyUSB`** (no ACM) and
both run at **115200 baud** — verified by probing every common baud and seeing
100% clean protocol only at 115200 (every other baud = framing garbage). Baud
is set by the sketch, so this is expected. The old "swapping boards broke it"
symptom was the **hardcoded `/dev/ttyUSB0` path**, not baud: the swapped board
came up as `ttyUSB1` while the game still read `ttyUSB0`. Now path-configurable.

Run command for this rig (ttyUSB0 → P1, ttyUSB1 → P2):

```bash
ARDUINO2_PORT=/dev/ttyUSB1 ./jeu
```

Defaults if unset: `ctrl1=/dev/ttyUSB0`, `ctrl2=/dev/ttyACM0`. Override per board:

```bash
ARDUINO_PORT=/dev/ttyUSB0 ARDUINO2_PORT=/dev/ttyUSB1 ./jeu
```

> A re-probe tool lives at `$CLAUDE_JOB_DIR/tmp/baud_probe.c` (throwaway) — if a
> board ever seems mute, recompile and run `./baud_probe /dev/ttyUSB0 /dev/ttyUSB1`
> to confirm it's sending and at what baud.

> Permissions: `chmod a+rw /dev/ttyUSB*` works but resets on replug/reboot — put
> the guest user in `dialout` for a permanent fix.

Per-controller tuning env vars (controller 1 = un-suffixed, controller 2 =
`...2_`):

| Var (P1 / P2) | Default | Effect |
|---|---|---|
| `ARDUINO_DEBUG` / `ARDUINO2_DEBUG` | off | log every RX line + direction decision |
| `ARDUINO_INVERT_X` / `ARDUINO2_INVERT_X` | **on** | sign-flip stick X |
| `ARDUINO_INVERT_Y` / `ARDUINO2_INVERT_Y` | **on** | sign-flip stick Y |
| `ARDUINO_SWAP_XY` / `ARDUINO2_SWAP_XY` | off | swap X/Y axes |

Log lines are tagged `[arduino P1]` / `[arduino P2]`. Each box auto-calibrates
its stick center in the first ~85 frames — **don't hold either stick at launch.**

Debug run:
```bash
ARDUINO_DEBUG=1 ARDUINO2_DEBUG=1 ./jeu 2> controllers.log
grep -E "ready|calibrated|DIR" controllers.log
```

---

## 7. Testing checklist

- [ ] Both `[arduino P1] ... ready` and `[arduino P2] ... ready` print at launch.
- [ ] Both `calibrated center` lines look sane (X≈505–533, Y≈490–510 on bare wiring).
- [ ] In a co-op match: controller 1 moves **only** P1, controller 2 moves **only** P2 (no cross-drag — this is what the keymap split fixes).
- [ ] Controller 1 navigates all menus (WASD path); controller 2 navigates via arrows.
- [ ] SHOOT/MELEE light the correct box's yellow LED; taking damage lights the correct box's red LED.
- [ ] If one board is unplugged, the game still launches and that player falls back to the keyboard.

---

## 7.5 Analog-stick troubleshooting (railed center / one-directional)

If a stick only registers some directions, or the character drifts / lags:

**Diagnose with the standalone monitor** (no game needed, isolates hardware):
```bash
gcc $CLAUDE_JOB_DIR/tmp/stick_monitor.c -o /tmp/stick_monitor   # if not built
/tmp/stick_monitor /dev/ttyUSB1
# push the stick to all 4 extremes + click during the 25s window
```
Read the summary:
- `calibrated center: X≈512 Y≈512` and **all 4 directions seen** → healthy.
- **Center near 0 or 1023** / only one direction per axis → the stick is
  **railed**: one end-terminal of the pot is floating. A pot is
  `+5V — wiper — GND`; with one end open there's no divider, so the wiper
  pins to the connected rail and can only twitch one way.
- **`Raw ADC range` span < 100 on an axis even when pushed hard** → that pin
  (A0/A1) truly dead/unwired.

**The +5V case (what bit board 2):** SW-click + buttons work but the analog is
railed → the missing leg is the pots' shared **+5V** (SW only needs GND, so GND
is fine). An *intermittent* +5V flickers between ~512 and a rail → jitter/lag
and a garbage auto-cal center. Fix: dedicated +5V wire Arduino-pin → joystick
(never a rail shared with LEDs/LCD), reseat/re-crimp, solder if it still
flickers. Confirm with a multimeter: joystick `+5V` vs `GND` should read ~5V
(board 1 does; a broken board 2 reads ~0 / floppy).

Tools live in `$CLAUDE_JOB_DIR/tmp/` (throwaway): `stick_monitor.c` (live axis
monitor) and `baud_probe.c` (confirm a board is sending + at what baud).

---

## 8. Reference

- `CONTROLLER_WORKFLOW.md` — code-level walkthrough of the controller integration.
- `arduino/CONTROLLER_NOTES.md` — original wiring, USB permissions, protocol,
  and the full debugging history (stick drift, axis cross-coupling, etc.).

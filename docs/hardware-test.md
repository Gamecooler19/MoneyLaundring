# Hardware test checklist — MoneyLaundring

Step-by-step bench checklist to go from "firmware flashed, nothing yet
confirmed" to a first supervised autonomous run. Do these in order. Every
step before "12V on" is USB-only and the console stays disarmed, so
nothing can move yet no matter what you type.

## 1. Power-off preflight (do this before connecting anything)

Check every item below with both the USB cable and the 12V supply
disconnected.

- [ ] All 4 IR obstacle modules are wired to the ESP32 **3V3** pin, not
  5V. These modules' OUT pin tracks their VCC rail; 5V on an ESP32 GPIO
  is outside its 3.3V-tolerant input range and can damage the pin.
- [ ] The L298N's **ENA and ENB jumper caps are removed**. If left on,
  ENA/ENB are tied to 5V internally and the motors always run at full
  speed regardless of commanded PWM duty - `MAX_DUTY` would have no
  effect.
- [ ] **Common ground**: L298N `GND` is wired to ESP32 `GND`. Without
  this, sensor and direction-pin readings are unreliable or simply wrong.
- [ ] **12V polarity** into the L298N's barrel jack / `12V IN` + `GND` is
  correct (check against the jack's own polarity marking, not just by
  feel).
- [ ] **Never connect USB and the L298N 5V output to the ESP32 at the
  same time.** Pick exactly one of: USB power (bring-up, this
  checklist), or L298N 5V output with the 5V-enable jumper fitted and USB
  disconnected (standalone mode, step 7 below). Back-feeding both at once
  can damage either supply.

Only once every box above is checked, proceed to step 2.

## 2. Bring-up with USB only

1. Connect USB only (12V can be connected or not at this point; just
   don't power the ESP32 from both USB and the L298N 5V output at once).
2. Open the serial console at 115200 baud (`pio device monitor`).
3. Read the boot banner. It reprints the full pin map and tunables
   (including Module 4's `RUN_TELEMETRY`/`AUTO_RUN_ON_BOOT`/
   `AUTO_RUN_DELAY_MS`) and warns that the pin map is unconfirmed. It
   also prints the ESP32 reset reason (`[boot] reset reason: ...`) -
   expect `POWERON` here since this is a fresh USB power-up.
4. Type `mon` if it isn't already printing (it's on by default) - watch
   the continuous sensor status line.

## 3. Find the real sensor GPIOs with `scan`

The default `X_MIN`/`X_MAX`/`Y_MIN`/`Y_MAX` pins in `include/config.h`
are unconfirmed defaults, not verified against this board.

1. Type `scan`. It watches a fixed list of candidate GPIOs (excluding the
   six configured motor-driver pins) for level changes, after a 1-second
   baseline.
2. During the scan window, touch/cover one IR sensor at a time with a
   black-body surface (finger, dark cloth) and note which GPIO number the
   console reports changing for each of the four sensors.
3. If any reported GPIO differs from the default in `include/config.h`,
   edit the corresponding `X_MIN`/`X_MAX`/`Y_MIN`/`Y_MAX` constant there
   to match, then reflash (`pio run -e esp32dev -t upload`) and repeat
   `scan` to confirm.

## 4. 12V on, arm, low speed

1. Connect the 12V supply to the L298N.
2. Type `speed 60` (or similar - well below `MAX_DUTY`, which prints in
   `map`) so the first moves are slow and easy to stop by eye.
3. Type `arm`. The console is now armed; motors can move. It auto-disarms
   after `ARM_TIMEOUT_MS` idle.

## 5. `xf`/`yf` jogs to settle the FORWARD mapping constants

`X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX` in `config.h` decide
which end "FORWARD" drives toward on each axis, and both default to
`true` - an unconfirmed guess.

Each jog is refused when the sensor at its destination reads active. The
destination is selected using the still-unconfirmed
`X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX` mapping, so an incorrect
mapping can make the console identify the wrong destination. If both endpoint
sensors on an axis read active, both jog directions are refused.

Before jogging, run `mon` and confirm all four sensors read clear (no
`*` marker) when nothing is covering them. If a sensor reads active when its
endpoint is not physically present, and `mon` confirms the active indication
is inverted, use `pol` and re-check the readings before jogging. Otherwise,
an active destination must be cleared before retrying: if the carriage is
resting at that endpoint, turn off motor power and move it clear by hand; if
the sensor is active unexpectedly, inspect and correct its wiring, placement,
or configured pin. Do not bypass the refusal.

1. With `mon` running, type `xf`. Watch which sensor's `*` marker lights
   up as the X-axis carriage approaches it.
   - If `X_MAX` activates, `X_FORWARD_IS_TOWARD_MAX = true` is correct.
   - If `X_MIN` activates instead, set it to `false` in `config.h`.
   - If `xf` is refused, check `mon` to see which endpoint is active. If
     both X endpoint sensors are active, both X jog directions are blocked.
     Use `pol` only if the active indication is opposite to the actual sensor
     behavior. Otherwise turn off motor power and clear the active endpoint
     by hand, or correct the sensor wiring/placement/pin, before retrying.
     If the sensor shown active is not physically at that endpoint, verify
     polarity and the FORWARD mapping before moving.
2. Repeat with `yf` for `Y_FORWARD_IS_TOWARD_MAX`.
3. Try `xr`/`yr` too and confirm the opposite sensor activates, as a
   cross-check.
4. Edit `include/config.h` to match what was observed, then reflash.

## 6. `pol` for polarity

1. With `mon` running, cover one sensor with a black-body surface and
   check that its raw level flips and that the `*` marker (meaning
   "endpoint active") appears for the reading you expect to mean
   "detected."
2. If the marker is backwards (lit when nothing is covering the sensor,
   or absent when something is), type `pol` to flip the active level at
   runtime and re-check.
3. Once confirmed, copy the printed active level into
   `SENSOR_ACTIVE_LEVEL` in `config.h` so it survives a reboot, then
   reflash.

## 7. Find the minimum usable speed per axis

`MAX_TRAVEL_MS`/`MAX_CLEAR_MS` are fixed constants, but run speed
(`sx`/`sy`) is adjustable at runtime, and a lower duty means a longer
travel time and a longer clear time for the same physical distance. A
timeout sized against a *higher* duty's travel time will be too tight -
and will raise a false `TRAVEL_TIMEOUT` - if the gantry is later run at a
*lower* duty. So find the lowest duty you will ever actually run at
*before* sizing the timeouts in step 9, not after.

1. Type `speed 40` (or similarly low), then `xf`. Watch whether the
   X-axis carriage actually starts moving and keeps moving smoothly
   against the real rack-and-pinion friction, or stalls/judders.
2. If it stalls or barely moves, raise `speed` a little and retry `xf`
   until it starts and keeps moving reliably.
3. Repeat with `yf` for the Y axis (speed may differ between axes due to
   friction/gearing differences).
4. Record the lowest duty each axis reliably starts and keeps moving at
   as that axis's **minimum usable speed**. This is the duty you should
   actually plan to run at (or above) - running below it risks a stall
   that looks like a `TRAVEL_TIMEOUT` fault for the wrong reason.
5. If you later decide to lower either axis's run speed below what you
   tested here, re-check this step and re-measure step 9's timeouts
   before doing so - both depend on the actual lowest duty in use.

## 8. First supervised low-speed run

1. Set `sx`/`sy` (and `speed` for jogs) to each axis's minimum usable
   speed from step 7, or any higher value you intend to actually run at -
   not an arbitrary "low" number disconnected from step 7's finding.
2. Keep a hand on the 12V supply's power switch or plug the whole time.
3. Type `run`. Watch both axes bounce. Watch for:
   - A fault line (`[fault] axis ... FAULT: ...`) - see the fault table
     below.
   - `[run] <axis> reversed -> ...` and `[run] <axis> cleared the end in
     ... ms` lines (printed because `RUN_TELEMETRY` is on by default) -
     these should appear on every reversal; if they never appear, `run`
     never actually produced a tick where the axis was running (check
     `st`).
4. Type `halt` or `stop` immediately if anything looks wrong - both work
   at any time, with `stop` also disarming.
5. Let it keep bouncing (don't `halt` yet) until each axis has reversed
   **at least four times** - see step 9 for why.

## 9. Turning run telemetry into real `MAX_TRAVEL_MS`/`MAX_CLEAR_MS`

Two things make this step easy to get wrong if rushed:

- **Measure at the lowest duty you will actually run, not a higher
  one.** `MAX_TRAVEL_MS`/`MAX_CLEAR_MS` are fixed constants while speed
  is adjustable; a lower duty means a longer travel time and a longer
  clear time for the same physical distance, so a value sized against a
  higher duty's telemetry will be too tight (and will raise a false
  `TRAVEL_TIMEOUT`/`STUCK_AT_LIMIT`) once run at a lower duty. Run at
  each axis's minimum usable speed from step 7 while collecting the
  numbers below, and re-measure this step if that minimum usable speed
  ever changes later.
- **Ignore the first segment after `run` starts.** The gantry starts
  mid-track, not at an end, so the very first segment (before the first
  reversal) is shorter than a normal end-to-end segment and will
  under-report `longestSegmentMs` if read too early. Only read `st`
  after **at least four reversals per axis** (step 8's last item keeps
  it running that long) so the longest values reflect real end-to-end
  segments and clears, not that short first one.

Once both conditions are met, type `st`. It prints `longestSegmentMs`
and `longestClearMs` per axis - the longest a travel segment and a
reversal-clear actually took, observed on the real hardware at the
speed actually tested.

- Set `MAX_TRAVEL_MS` to roughly **2x** the longest `longestSegmentMs`
  observed across both axes - enough headroom that a normal segment never
  false-trips `TRAVEL_TIMEOUT`, but still catches a genuinely stuck axis.
- Set `MAX_CLEAR_MS` to roughly **3x** the longest `longestClearMs`
  observed, with a **floor of 1000 ms** (i.e. `max(3 * longestClearMs,
  1000)`) - this is meant to be a short window covering the moment right
  at a reversal, not real travel time, so it shouldn't be allowed to
  shrink to something a normal reversal could occasionally exceed by
  chance.
- Edit both constants in `include/config.h` and reflash.
- If you later run at a lower speed than step 7's minimum usable speed
  (not recommended - see step 7), repeat this step at that lower speed
  before trusting these timeouts again.

## 10. Fault table

| Fault | Likely cause | Fix |
|-------|-------------|-----|
| `BOTH_LIMITS_ACTIVE` | both limit sensors on that axis read active at once | sensor polarity or placement wrong for that axis - recheck wiring and `pol` |
| `OPPOSITE_LIMIT_HIT` | the sensor behind the current heading went active (rising edge) | that axis's FORWARD mapping constant is flipped - redo step 5 |
| `STUCK_AT_LIMIT` | the behind limit stayed active for `MAX_CLEAR_MS` while the axis was otherwise allowed to run | classically inverted motor wiring with the carriage already resting at that end - redo step 5; otherwise check for a mechanical jam or too low a run speed (redo step 7) |
| `TRAVEL_TIMEOUT` | no endpoint sensor was seen within `MAX_TRAVEL_MS` | check that axis's sensor wiring; or the run speed is below step 7's minimum usable speed; or `MAX_TRAVEL_MS` was sized against a higher duty than currently in use (redo step 9) |

A fault always disarms and never auto-retries. Fix the underlying cause,
then `arm` and `run` again manually.

## 11. Standalone mode (no USB, no bench supervision)

Once calibration (steps 3, 5, 6) and sizing (steps 7, 9) are done and the
gantry has run cleanly multiple times under supervision:

1. Power off everything. Disconnect USB.
2. Fit the L298N's 5V-enable jumper (the one that routes its onboard 5V
   regulator output to the board edge pin), so the L298N's own 5V
   regulator powers the ESP32 instead of USB.
3. Wire that L298N 5V output to the ESP32's 5V/VIN pin (not 3V3 - the
   ESP32 has its own onboard 3.3V regulator fed from 5V/VIN).
4. Confirm USB is disconnected and stays disconnected - per the preflight
   rule, never power the ESP32 from both sources at once.
5. Only now consider enabling `AUTO_RUN_ON_BOOT = true` in `config.h` (it
   defaults to `false`) if you want the gantry to start bouncing on its
   own with no operator present. Understand exactly what this flag does
   before enabling it: on the classic ESP32, `esp_reset_reason()` cannot
   tell an EN-pin/reset-button reset apart from a true power cycle - both
   report `ESP_RST_POWERON` - so with the flag true, **every reflash,
   every press of the physical reset button, and every time a serial
   monitor or `esptool` opens the port** (which toggles DTR/RTS and
   resets the board the same way) **starts the countdown**, not only a
   literal power-cycle. The guards that actually hold are: the flag
   defaults to `false`; any received serial byte during the countdown
   cancels it; and firing always reuses the exact same internal
   `arm`/`run` code paths as typing `arm` then `run` manually - there is
   no separate auto-run motion path. A fault still disarms and never
   auto-retries even in this mode. Enable this flag only for the final
   standalone flash (reflash with it set to `false` for every bring-up/
   bench session), keep your hands clear of the gantry for the entire
   `AUTO_RUN_DELAY_MS` countdown after every reset from then on, and read
   the boot banner's always-printed `[boot] reset reason: ...` line on
   the bench as the evidence of what reset actually happened.
6. Power on from 12V only and confirm the gantry behaves as expected
   before leaving it unattended.

## 12. What to send back

If you're running this checklist for someone else (e.g. reporting back
to whoever wrote the firmware) and can't share the live hardware, copy
the following straight out of the serial monitor and send it as plain
text - this is exactly what's needed to confirm calibration without
being on the bench:

- The full boot banner, **including the `[boot] reset reason: ...`
  line**, captured twice: once right after pressing the physical RST
  button, and once right after unplugging and replugging the USB cable.
  (Both are expected to print the same reset-reason name on the classic
  ESP32 - see the README's Safety rules for why.)
- The full output of `map`.
- The full output of `scan`, run once with each of the four sensors
  covered in turn during the window (so all four GPIO-change lines
  appear).
- The output of `pol`.
- Which sensor's `*` marker activates for `xf` and which activates for
  `yf` (i.e. the raw observation step 5 is based on, not just the
  resulting `X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX` values).
- The output of `st`, taken after each axis has reversed at least four
  times during a `run` (per step 9), together with every `[run] ...`
  line printed during that same run (both the `reversed` and `cleared
  the end` lines, for both axes).

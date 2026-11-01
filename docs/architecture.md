# Architecture — MoneyLaundring

## Goal

A two-axis rack-and-pinion gantry controller on ESP32. Each axis drives
its motor in one direction until the IR sensor at that end reports the
black-body endpoint, then reverses, with adjustable speed. Both axes run
independently and without blocking.

## Design decision: separate pure logic from hardware

The reversing/bounce logic lives in a pure `Axis` class that never calls
Arduino functions, includes `Arduino.h`, or includes `config.h` (verified
by grep — see reports/module-2-report.md). It takes the current time and
the two debounced sensor states as inputs and returns a motor command
(`AxisOutput`: run/dir/state/fault/reversed). The hardware wrappers
(`Motor`, `LimitSensor`) do the actual `digitalRead` / PWM; `GantryController`
is the only class that bridges the two.

Why: it makes the reversing logic unit-testable on a normal computer with
native `pio test` — no ESP32 needed to prove the state machine is correct.
Putting `digitalRead` calls directly inside the decision logic can only be
tested by flashing and eyeballing. This split is the difference between "a
sketch" and a maintainable, testable repo.

Alternative considered: single-file sketch with everything inline in
`loop()`. Simpler to write, but untestable and hard to extend safely once
bounce logic and a watchdog are added. Rejected.

## Design decision: BringupConsole is kept and extended, not replaced

Module 2 did not add a second console or a build flag to switch between
"bring-up mode" and "run mode." `BringupConsole` from Module 1 is the one
and only console, extended with `run`/`halt`/`sx`/`sy`/`st`. The
bring-up commands (`scan`, `pol`, `speed`, `xf`/`xr`/`yf`/`yr`) all refuse
with a clear message while the gantry is running, and the run-starting
command (`run`) refuses while a scan or jog is in progress, so the two
modes never interleave. This was a deliberate simplification: an operator
bringing up new hardware and an operator running the finished gantry use
the same serial session, the same baud rate, and the same `arm` concept,
with no separate firmware image to flash and no compile-time toggle to
remember.

## Design decision: GantryController is the only hardware glue for Axis

`GantryController` owns two `Axis` instances plus references to the two
`Motor` and four `LimitSensor` objects. It is the only class that:
- builds each `Axis`'s `AxisConfig` from `config.h`'s `REVERSE_LOCKOUT_MS`
  / `REVERSE_PAUSE_MS` / `MAX_TRAVEL_MS`,
- feeds each `Axis::update()` the current debounced `LimitSensor::active()`
  states,
- maps the resulting `AxisDir` to `Motor::FORWARD`/`REVERSE` using the
  `X_FORWARD_IS_TOWARD_MAX` / `Y_FORWARD_IS_TOWARD_MAX` constants,
- calls `Motor::drive()`/`stop()` only when the commanded run state,
  direction, or duty actually changed since the previous tick (not every
  loop), and
- on either axis faulting, immediately stops both motors, halts both
  axes, latches a fault flag, and prints which axis/fault/likely cause.

`GantryController` has no idea `BringupConsole` exists; `BringupConsole`
holds a reference to one `GantryController` and drives it (`startRun`,
`haltRun`, `setSpeedX`/`Y`, `printStatus`, polling `takeFaultFlag()` once
per loop). This keeps the pure/hardware split intact one level up: `Axis`
doesn't know about `Motor`, and `GantryController` doesn't know about
`BringupConsole`.

## What's built

**`Motor`** (hardware) — one L298N channel.
- `begin(enaPin, in1Pin, in2Pin, pwmChannel, freqHz, resBits)`
- `drive(Direction dir, uint8_t duty)` — sets direction pins, then writes
  PWM duty, clamped to `MAX_DUTY` with a printed notice if the requested
  duty was higher. This clamp is unconditional; there is no code path that
  skips it. `drive()` is called from exactly two places in the whole
  codebase: `BringupConsole::cmdJog()` (gated by armed + not-running) and
  `GantryController::applyAxisCommand()` (only reached while running and
  only for a non-faulted axis).
- `stop()` — both direction pins LOW, duty 0.
- Uses arduino-esp32 **2.0.x** LEDC API only: `ledcSetup`, `ledcAttachPin`,
  `ledcWrite(channel, duty)`. Never the 3.x `ledcAttach(pin, freq, res)`.

**`LimitSensor`** (hardware) — one IR endpoint sensor.
- `begin(pin, activeLevel)` — `pinMode(pin, INPUT)` (no internal pull-up;
  the IR module drives OUT actively).
- `update(nowMs)` — non-blocking debounce step, called exactly once per
  `loop()` from `main.cpp` (not from `BringupConsole` — moved there in
  Module 2 so no sensor is ever updated twice in one tick).
- `raw()` — instantaneous `digitalRead`, no debounce.
- `active()` — debounced reading against the current active level.
- `setActiveLevel(int)` — flip polarity at runtime (used by the console's
  `pol` command, refused while running).

**`Axis`** (pure logic, no Arduino, no config.h) — one axis's bounce
decision engine. See `src/Axis.h`/`Axis.cpp` for the exact rule order:
not-RUNNING short-circuit, BOTH_LIMITS_ACTIVE / OPPOSITE_LIMIT_HIT checks
(suppressed for `lockoutMs + reversePauseMs` after a reversal, never
suppressed right after `start()`), then **STUCK_AT_LIMIT** (Module 3,
never suppressed — see below), then TRAVEL_TIMEOUT, the reversal trigger
(gated by `lockoutMs` since the last reversal), then the post-reversal
pause. All time arithmetic uses unsigned subtraction so it survives
`millis()` rollover. Every rule is proven by a native Unity test that has
actually been run and that a four-mutation sanity check (Module 3)
confirmed can fail — see `test/test_axis/test_axis.cpp` and
`reports/module-3-report.md`.

**STUCK_AT_LIMIT (Module 3)** closes a gap `OPPOSITE_LIMIT_HIT` cannot
cover: if the carriage is already resting at an end when `start()` is
called, and that axis's motor wiring is inverted relative to its FORWARD
mapping constant, the "drive away from this end" command actually keeps
driving into it. The limit behind the heading was already active before
any update could observe a rising edge, so it never rises and
`OPPOSITE_LIMIT_HIT` can never fire — without a second check, the axis
would just keep "running" into its end stop until the much longer
`maxTravelMs` eventually caught it. `AxisConfig.clearMs` bounds how long
the behind limit may stay continuously active while the axis is otherwise
allowed to run (not held in the post-reversal pause): a clock accumulates
elapsed time while that condition holds, resets to zero the instant the
behind limit goes inactive, and also resets at every reversal. Once the
clock reaches `clearMs`, the axis faults `STUCK_AT_LIMIT`. This check runs
immediately after the `OPPOSITE_LIMIT_HIT` check and before
`TRAVEL_TIMEOUT`, and — unlike `BOTH_LIMITS_ACTIVE`/`OPPOSITE_LIMIT_HIT`
— is never subject to the post-reversal suppression window, since
suppressing it would defeat the point (the stuck condition is exactly the
kind of thing that can start right at a reversal).

**`GantryController`** (hardware glue) — see the design decision above.
Public interface: `begin(...)`, `startRun(nowMs)`, `haltRun()`,
`isRunning()`, `setSpeedX`/`setSpeedY` (both clamped to `MAX_DUTY`),
`printStatus()`, `takeFaultFlag()` (latched, polled and cleared by the
console), `invalidateCommandCache()` (Module 3 — see below), and
`update(nowMs)`.

**Per-motor command cache (Module 3 audit)** — `GantryController` only
calls `Motor::drive()`/`stop()` when the commanded run state, direction,
or duty actually changed since the last tick, tracked per-motor in a
small cache. That cache is invalidated in `startRun()`, in `haltRun()`,
and in the fault-shutdown branch of `update()` — all already true as of
Module 2. The Module 3 audit found one gap: `BringupConsole`'s jog
commands (`xf`/`xr`/`yf`/`yr`) call `Motor::drive()`/`stop()` directly,
bypassing `GantryController` entirely, so a jog could leave the cache
holding a stale "last commanded" value the controller never actually
issued. If a later `run`'s first command happened to match that stale
value, the cache would wrongly treat it as unchanged and skip the
`Motor::drive()` call — leaving the motor idle while the controller
believed it was running. Fixed by calling the new
`GantryController::invalidateCommandCache()` both when a jog starts
(`cmdJog()`) and when it ends (`updateJog()`), so the first command of
every `run` is now always sent to the hardware.

**`BringupConsole`** (hardware) — the Module 1 deliverable, extended in
Module 2. Non-blocking serial command parser at `SERIAL_BAUD`. Bring-up
commands: `?`, `map`, `mon`, `pol`, `scan`, `arm`, `disarm`, `stop`,
`speed <n>`, `xf`/`xr`/`yf`/`yr`. Run-mode commands (Module 2): `run`,
`halt`, `sx <0-255>`, `sy <0-255>`, `st`. Boots disarmed and not running;
every motor-moving command checks the armed flag first; `run` additionally
refuses unless armed and idle (no scan/jog in progress); the bring-up
commands refuse while running. Arming auto-expires after `ARM_TIMEOUT_MS`
idle, *except* while running (the safety stops in run mode are `halt`,
`stop`, `disarm`, and the fault shutdown, not an idle timer). `disarm`,
`stop`, and the fault shutdown (via `takeFaultFlag()`) each also end any
run. `scan` only runs while disarmed and not running, and never touches
the six configured motor-driver pins or ever sets a pin to `OUTPUT`.

**`config.h`** — every pin and tunable as a named constant: the X/Y motor
pins, the four sensor pins, `PWM_FREQ`/`PWM_RES`/PWM channels,
`SERIAL_BAUD`, `DEBOUNCE_MS`, `SENSOR_ACTIVE_LEVEL` (provisional),
`JOG_MS`, `MONITOR_PERIOD_MS`, `ARM_TIMEOUT_MS`, `SCAN_WINDOW_MS`,
`SCAN_BASELINE_MS`, `SUPPLY_VOLTS`, `MOTOR_RATED_VOLTS`, the derived
`MAX_DUTY`, `DEFAULT_SPEED`, and (Module 2) `X_FORWARD_IS_TOWARD_MAX` /
`Y_FORWARD_IS_TOWARD_MAX` (provisional), `REVERSE_LOCKOUT_MS`,
`REVERSE_PAUSE_MS`, `MAX_TRAVEL_MS` (provisional), `RUN_SPEED_X`,
`RUN_SPEED_Y`, and (Module 3) `MAX_CLEAR_MS` (provisional).

## Repo layout

```
MoneyLaundring/
  platformio.ini          ; esp32dev (arduino) + native (Axis unit tests)
  include/config.h        ; pin map + tunables (all unconfirmed defaults)
  src/
    main.cpp              ; wire objects together; setup()/loop()
    Motor.h  Motor.cpp
    LimitSensor.h  LimitSensor.cpp
    Axis.h  Axis.cpp              ; pure, no Arduino, no config.h
    GantryController.h  GantryController.cpp  ; hardware glue for Axis
    BringupConsole.h  BringupConsole.cpp
  test/test_axis/test_axis.cpp    ; native Unity tests for Axis
  docs/                    ; this file + wiring.md
  reports/                 ; module-N-report.md (gitignored, not committed)
```

## Fixed loop() ordering

`main.cpp`'s `loop()` always does, in this order, exactly once per tick:
1. All four `LimitSensor::update(now)` calls.
2. `GantryController::update(now)` (reads the now-current debounced sensor
   states; no-op if not running).
3. `BringupConsole::update(now)` (reads serial input, advances its own
   state machines, polls the gantry's fault flag).

This ordering means no sensor is ever updated twice in one tick, and the
controller always acts on this tick's debounced reading, not a stale one.

## Build order (one module at a time)

- **Module 1 — bring-up.** Scaffolded the PlatformIO project, `config.h`,
  `Motor`, `LimitSensor`, and `BringupConsole`. Let the operator confirm
  the pin map, motor direction, and sensor polarity on the real board. No
  bounce logic, no `Axis` class, no native tests.
- **Module 2 — axis bounce logic, native tests, run mode.** Added `Axis`
  (pure bounce decision logic, natively unit-tested) and
  `GantryController` (hardware glue), extended `BringupConsole` with
  `run`/`halt`/`sx`/`sy`/`st`, re-added `[env:native]` for `pio test`, and
  fixed the `loop()` ordering so sensors update exactly once per tick.
- **Module 3 (this module) — verification and hardening.** The `Axis`
  bounce logic had never actually been run by any test before this
  module — ran the existing 11 native tests for the first time (all
  passed; no fix was needed). Added `STUCK_AT_LIMIT` to close the safety
  gap where a carriage resting at an end with inverted motor wiring could
  otherwise push into its end stop for up to `maxTravelMs`. Extended the
  native test suite with threshold boundary tests, a stuck-at-limit test,
  inverted-mapping simulated-carriage runs, and a deterministic
  xorshift32 fuzz test. Confirmed, via a four-mutation sanity check on
  `Axis.cpp` (each mutation restored afterward), that the test suite can
  actually catch a broken rule rather than passing regardless of what the
  code does. Also audited and fixed `GantryController`'s per-motor
  command cache so a jog can never leave it holding a stale value.
- **Module 4 (this module) — reproducible testing, run telemetry,
  auto-run, final docs.** `pio test -e native` needs a host g++; added
  `scripts/test-native.cmd`/`.sh`, which fall back to a pinned Docker
  image (`docker/Dockerfile.native-test`, PlatformIO 6.2.0) when none is
  found, with `PLATFORMIO_BUILD_DIR` pointed inside the container (never
  the mounted repo's `.pio\build`) and `PLATFORMIO_CORE_DIR` backed by a
  named volume so the native platform downloads once. Added
  `.gitattributes`/`.editorconfig` (every tracked blob was already stored
  as LF; the one working-tree CRLF anomaly found via `git ls-files --eol`
  was a local checkout artifact of `core.autocrlf=true`, not mixed
  content in the repo, so no renormalize commit was needed). Added
  observation-only `RUN_TELEMETRY` to `GantryController` (reversal and
  clear-time lines, longest-segment/longest-clear tracking shown in
  `st`) — strictly read-only with respect to motor commands, evaluated
  after `applyAxisCommand()` has already decided the tick's motor
  command. Added `AUTO_RUN_ON_BOOT`/`AUTO_RUN_DELAY_MS` (default off),
  gated on `esp_reset_reason() == ESP_RST_POWERON` — on the classic
  ESP32 this reason covers any hardware reset (EN pin / reset button /
  DTR-RTS toggle from a serial monitor or `esptool`, as well as a true
  power cycle; `ESP_RST_EXT` is explicitly documented as "not applicable
  for ESP32" in `esp_system.h`, i.e. there is no separate code for an
  external-pin reset on this chip), not power-on specifically — cancelable
  by any serial byte during the countdown, reusing the exact `cmdArm()`/
  `cmdRun()` paths the typed commands use — never a separate motion
  path, and a fault never auto-retries. Rewrote the README as the final
  operator document and added `docs/hardware-test.md`, the step-by-step
  bench calibration checklist.

Each module ends with a completion report in `reports/`. The next module
should be written only after reading the previous module's report.

## Known limitations (as of Module 4)

- **No persistence.** Run speeds (`sx`/`sy`), the sensor active level set
  via `pol`, and any other runtime-adjusted value reset to their
  `config.h` compiled-in defaults on every reboot. There is no NVS (or
  other) storage layer in this firmware.
- **`GantryController` has never been tested against real hardware.**
  `Axis` (the pure bounce-decision logic) is natively unit-tested and
  mutation-checked (Module 3); `GantryController`, the glue that drives
  real `Motor`/`LimitSensor` objects from `Axis`'s output, including the
  Module 4 run-telemetry logic layered on top of it, has only ever been
  exercised by compiling for `esp32dev` — never flashed to or run on the
  physical gantry.
- **Run speed is adjustable only over serial.** There is no physical
  knob, button, or other input device; `sx`/`sy`/`speed` are the only way
  to change it, and only while a serial connection is available (or, once
  set at compile time via `RUN_SPEED_X`/`RUN_SPEED_Y`, not adjustable at
  all in standalone mode without a serial cable). This matters for
  `MAX_TRAVEL_MS`/`MAX_CLEAR_MS` too: those are fixed constants while
  speed is adjustable, so they must be measured at the lowest duty ever
  actually run (a lower duty means a longer travel/clear time for the
  same distance) and re-measured if that lowest duty later changes — see
  `docs/hardware-test.md` steps 7 and 9.
- **Hardware calibration is an operator step, not a module.** The pin
  map, sensor polarity, and `X_FORWARD_IS_TOWARD_MAX`/
  `Y_FORWARD_IS_TOWARD_MAX` cannot be determined from a dev machine
  without the physical gantry in front of someone; no module in this
  repo claims to have done this. `docs/hardware-test.md` is the
  checklist an operator with the real board follows to do it once, after
  which the discovered values should be committed back into
  `include/config.h`/`docs/wiring.md`, but that act is calibration, not
  firmware development.
- **The jog safety gate trusts the same unconfirmed FORWARD mapping it
  is meant to help calibrate.** `cmdJog()`'s refusal (added in Module
  4, see `src/JogGate.h`) decides which sensor is "ahead" of a jog
  using `X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX` — the exact
  constants `docs/hardware-test.md` step 5 exists to determine in the
  first place. If that mapping is wrong, the gate can name the wrong
  sensor in its refusal message, or (equivalently) refuse/permit the
  opposite of what a correct mapping would. This is why step 5 starts
  with the carriage roughly mid-track rather than at an end, and why
  the gate's refusal message always states which mapping constant it
  assumed, so the operator can judge for themselves whether to trust
  it, jog the other way, or use `force`.


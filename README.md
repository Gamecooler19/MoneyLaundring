# MoneyLaundring

ESP32 firmware for a two-axis rack-and-pinion gantry ("moving dartboard").
Each axis drives a motor until an IR sensor at that end detects the
black-body endpoint, then reverses, bouncing back and forth indefinitely
with an independently adjustable speed per axis.

Everything in this repo is built and native-tested; **nothing has been
run on the physical board**. The pin map, sensor polarity, and the
FORWARD-direction mapping constants are all provisional defaults that
must be confirmed on real hardware before ever typing `run`. See
[`docs/hardware-test.md`](docs/hardware-test.md) for the bench checklist
that does exactly that.

## Hardware

- 38-pin ESP32 DevKit (micro-USB)
- Red L298N dual H-bridge motor driver, 12V barrel-jack fed
- 2x TT gear motors, each on its own 3D-printed rack-and-pinion axis
  (the two axes are perpendicular)
- 4x 3-pin IR obstacle modules (VCC/GND/OUT, LM393 trim-pot), 2 per axis,
  one at each end of travel

Full pin map, power/ground rules, and the standalone-power option are in
[`docs/wiring.md`](docs/wiring.md).

## How it works

Two independent axes. The reversing decision lives in a pure,
hardware-independent `Axis` class (unit-tested natively, no ESP32
required - see `test/test_axis/`): each axis drives toward one end, and
once the sensor there goes active, reverses after a short pause, with a
lockout so sensor chatter can't cause a second reversal too soon.
`GantryController` is the only class that connects `Axis` to the real
`Motor`/`LimitSensor` hardware. Design details and the full module-by-module
build history are in [`docs/architecture.md`](docs/architecture.md).

## Quick start

1. **Flash.** `pio run -e esp32dev -t upload` (USB connected, 12V supply
   can be connected or not).
2. **Open the serial console.** `pio device monitor` at 115200 baud. The
   boot banner reprints the full pin map and tunables and warns that the
   pin map is unconfirmed.
3. **Calibrate against the real board first.** Do not type `arm` or
   `run` yet - walk through [`docs/hardware-test.md`](docs/hardware-test.md),
   which uses `scan`, `mon`, `pol`, and `xf`/`xr`/`yf`/`yr` to confirm the
   sensor pins, sensor polarity, and the FORWARD-direction mapping
   constants against the actual wiring, editing `include/config.h` as
   needed and reflashing.
4. **First run.** Once calibrated, `arm` then `run` starts autonomous
   bouncing on both axes. `st` reports live status; `halt` stops the
   autonomous bounce while staying armed; `stop` stops everything and
   disarms.

## Build and test (PlatformIO)

```
pio run -e esp32dev           # compile for the ESP32
pio run -e esp32dev -t upload # flash
pio device monitor            # serial console @ 115200
pio test -e native            # run the native Axis unit tests (no board needed)
```

### Running the native tests reproducibly

`pio test -e native` needs a host g++ toolchain. If you don't have one on
PATH, use the reproducible wrapper scripts instead:

- `scripts\test-native.cmd` (Windows) / `scripts/test-native.sh`
  (Linux/macOS/Git Bash) each check for `g++` on PATH first (fast path,
  runs `pio test -e native` directly); if it's missing, they build
  `docker/Dockerfile.native-test` once (pinned to PlatformIO 6.2.0, tagged
  `moneylaundring-native-test`) and run the tests inside a throwaway
  container instead.
- Inside that container, `PLATFORMIO_BUILD_DIR` is set to a path inside
  the container (`/tmp/pio-native-build`), not under the mounted repo, so
  the container's build never touches the host's `.pio\build` - confirmed
  by checking directory timestamps before and after a container run.
- `PLATFORMIO_CORE_DIR` (PlatformIO's platform/tool cache) is backed by a
  named Docker volume (`moneylaundring-pio-core`), so the native platform
  is downloaded once and reused by every later throwaway container
  instead of being re-fetched every run.

Both scripts report `19 of 19 succeeded` when the `Axis` test suite
passes.

### Building a flashable image

`scripts/package_firmware.py` turns a clean `pio run -e esp32dev` build
into a single flashable binary (bootloader + partition table +
`boot_app0` + application merged at their real flash offsets), plus the
four component images, a `SHA256SUMS` file, and a `MANIFEST.md`
describing exactly what was built and from which commit.

1. Build first: `pio run -e esp32dev` (the script reads its output from
   `.pio/build/esp32dev`; it does not build anything itself).
2. Run the script with PlatformIO's own Python interpreter (the one
   `pio` itself resolves to - check with `pio system info` if unsure
   which one that is on your machine):
   ```
   <platformio-python> scripts/package_firmware.py
   ```
3. The script refuses to run (writes nothing) if the git working tree
   is dirty, or if `AUTO_RUN_ON_BOOT` in `include/config.h` is not
   exactly `false` - packaging a build that could auto-run on boot is
   not allowed.
4. Output goes to `dist/` (gitignored, never committed): the merged
   image (`moneylaundring-<shortsha>-merged.bin`), the four component
   images, `SHA256SUMS`, and `MANIFEST.md`.
5. To flash the merged image: `python -m esptool --chip esp32 --port
   COMx --baud 460800 write_flash 0x0 moneylaundring-<shortsha>-merged.bin`
   (replace `COMx` with the real port). The exact command is also
   printed in `MANIFEST.md`.

**The image is only as current as the commit it names.** `dist/` is
never committed and is overwritten by every run of the script; if the
source has moved on since a `dist/` image was built, rebuild before
flashing. `MANIFEST.md` always states the exact source commit the image
inside that same `dist/` folder was built from - trust that, not the
filename alone, if in doubt.

## Command reference (@ 115200 baud, newline-terminated)

| Command       | Effect |
|---------------|--------|
| `?`           | print the command list |
| `map`         | print the pin map and all tunables, including Module 4's `RUN_TELEMETRY`/`AUTO_RUN_ON_BOOT`/`AUTO_RUN_DELAY_MS` |
| `mon`         | toggle the continuous sensor status line (on by default) |
| `pol`         | flip the active sensor level at runtime (not while running) |
| `scan`        | (disarmed, not running) watch candidate GPIOs for level changes, to find each sensor's real pin |
| `arm`         | enable motor output; auto-disarms if idle too long (not while running) |
| `disarm`      | disable motor output (also ends any run) |
| `stop`        | stop both motors immediately and disarm (also ends any run) |
| `speed <n>`   | set jog duty 0-255, clamped to `MAX_DUTY` (not while running) |
| `xf` / `xr`   | jog axis X forward/reverse for `JOG_MS` (armed, not while running) - use with `mon` to see which sensor it approaches. Refuses if the sensor ahead of that direction is already active; add `force` (e.g. `xf force`) to jog anyway once you have checked by eye that the carriage is not at that end |
| `yf` / `yr`   | jog axis Y forward/reverse for `JOG_MS` (armed, not while running), with the same engaged-sensor refusal and `force` override as `xf`/`xr` |
| `run`         | start autonomous bounce on both axes (armed, idle only) |
| `halt`        | stop autonomous bounce, stay armed |
| `sx <0-255>`  | set axis X run speed, clamped to `MAX_DUTY` (allowed while running) |
| `sy <0-255>`  | set axis Y run speed, clamped to `MAX_DUTY` (allowed while running) |
| `st`          | print gantry status: state, heading, speed, reversals, fault, and (Module 4) the longest segment/clear times seen since the last `run` |

While running, two more lines print automatically whenever
`RUN_TELEMETRY` (on by default) is enabled: `[run] <axis> reversed -> ...`
on every reversal, and `[run] <axis> cleared the end in ... ms` once the
carriage clears the sensor it just reversed off of. `MAX_TRAVEL_MS`/
`MAX_CLEAR_MS` are fixed constants while run speed is adjustable, so
they must be measured (via `st`) at the **lowest duty you will ever
actually run**, not a higher one - a lower duty means a longer travel
time for the same distance, so a value sized against a higher duty will
raise a false `TRAVEL_TIMEOUT`/`STUCK_AT_LIMIT` once run slower. The
first segment after `run` starts mid-track and is short, so only read
`st`'s longest values after at least four reversals per axis. See
[`docs/hardware-test.md`](docs/hardware-test.md) steps 7 and 9 for the
full procedure.

## Fault codes

If either axis faults, `GantryController` stops both motors immediately,
the console auto-disarms, and it prints which axis, which fault, and a
likely cause:

| Fault | Cause | Fix |
|-------|-------|-----|
| `BOTH_LIMITS_ACTIVE` | both limit sensors on that axis read active at once | sensor polarity or placement is wrong for that axis - check wiring and `pol` |
| `OPPOSITE_LIMIT_HIT` | the limit sensor *behind* the current heading went active (rising edge) | that axis's FORWARD mapping constant is flipped - flip `X_FORWARD_IS_TOWARD_MAX`/`Y_FORWARD_IS_TOWARD_MAX` in `config.h` |
| `STUCK_AT_LIMIT` | the behind limit stayed active for `MAX_CLEAR_MS` while the axis was otherwise allowed to run - the carriage never left the end it started/reversed at | classically inverted motor wiring with the carriage already resting at that end: flip the FORWARD mapping constant; otherwise check for a mechanical jam or too low a run speed |
| `TRAVEL_TIMEOUT` | no endpoint sensor was seen within `MAX_TRAVEL_MS` | check that axis's sensor wiring; or the run speed is below the bench-tested minimum usable speed (see `docs/hardware-test.md` step 7); or `MAX_TRAVEL_MS` was sized against a higher duty than currently in use (re-measure per step 9) |

A fault is sticky until the next `arm`+`run`; the firmware never
auto-retries a fault on its own, including via `AUTO_RUN_ON_BOOT` (see
below).

## Safety rules

- Nothing can move until the operator explicitly types `arm`. Arming
  auto-expires after `ARM_TIMEOUT_MS` idle, except while an autonomous
  run is in progress (the safety stops while running are `halt`, `stop`,
  `disarm`, and a fault, not an idle timer).
- Every motor-moving command is clamped to `MAX_DUTY` (153 by default,
  derived from the 12V supply and the motors' ~6V rating); there is no
  code path that bypasses this clamp.
- Any axis fault stops both motors immediately and disarms the console.
  There is no automatic retry.
- `AUTO_RUN_ON_BOOT` defaults to `false`. When enabled, it starts the
  countdown after **any hardware reset** - every flash, every press of
  the physical reset/EN button, and every time a serial monitor or
  `esptool` opens the port and toggles DTR/RTS (which also pulls EN low
  on the classic ESP32) - because the classic ESP32 cannot distinguish
  an EN-pin reset from a true power cycle; both report
  `esp_reset_reason() == ESP_RST_POWERON`. It never fires after a
  software reset (`esp_restart()`), a panic, a watchdog reset, or a
  brownout - those are the guards that actually hold, along with the
  flag defaulting to `false` and the cancel-on-any-byte countdown window.
  When it does fire, it calls exactly the same internal `arm`/`run` code
  paths the typed commands use. Enable it only for the final standalone
  flash, keep hands clear of the gantry for the whole countdown, and
  read the always-printed `[boot] reset reason: ...` line as the
  evidence of what actually happened. See
  [`docs/hardware-test.md`](docs/hardware-test.md) for when to actually
  turn it on.

## Repo layout

```
MoneyLaundring/
  platformio.ini              ; esp32dev (arduino) + native (Axis unit tests)
  include/config.h            ; pin map + tunables (see docs/wiring.md)
  src/
    main.cpp                  ; wire objects together; setup()/loop()
    Motor.h  Motor.cpp
    LimitSensor.h  LimitSensor.cpp
    Axis.h  Axis.cpp           ; pure bounce logic, no Arduino, no config.h
    GantryController.h  GantryController.cpp  ; hardware glue + run telemetry
    BringupConsole.h  BringupConsole.cpp       ; serial console + auto-run
  test/test_axis/test_axis.cpp ; native Unity tests for Axis
  scripts/test-native.cmd, test-native.sh      ; reproducible native testing
  docker/Dockerfile.native-test                ; pinned PlatformIO test image
  docs/
    architecture.md            ; design decisions and module history
    wiring.md                  ; hardware contract: pin map, power, grounding
    hardware-test.md           ; step-by-step bench calibration checklist
  reports/                     ; module-N-report.md (gitignored, not committed)
```

## Status

Built in modules, each ending with a completion report in `reports/`
(gitignored, not committed). See [`docs/architecture.md`](docs/architecture.md)
for the full module-by-module history and design rationale.

- **Module 1 - bring-up.** `config.h`, `Motor`, `LimitSensor`,
  `BringupConsole`. No bounce logic.
- **Module 2 - axis bounce logic, native tests, run mode.** `Axis` (pure
  bounce decision logic, native-tested) and `GantryController` (hardware
  glue), extended `BringupConsole` with `run`/`halt`/`sx`/`sy`/`st`.
- **Module 3 - verification and hardening.** Confirmed the native test
  suite actually runs and every `Axis` rule is proven by a test that can
  fail. Added `STUCK_AT_LIMIT` (catches a carriage resting at an end with
  inverted motor wiring). Fixed a `GantryController` command-cache gap
  around jogs.
- **Module 4 (current) - reproducible testing, run telemetry, auto-run,
  final docs.** Reproducible `pio test -e native` via Docker when no host
  g++ is available; `.gitattributes`/`.editorconfig`; observation-only run
  telemetry (`RUN_TELEMETRY`); `AUTO_RUN_ON_BOOT` gated on
  `esp_reset_reason() == ESP_RST_POWERON` (any hardware reset - flash,
  reset button, or a serial monitor opening - not a true power cycle
  specifically), reusing the exact arm/run code paths; this README and
  the hardware-test checklist. See `docs/architecture.md` for the known
  limitations and `reports/module-4-report.md` for the full writeup.

Hardware calibration (the real pin map, sensor polarity, and
FORWARD-mapping constants) is an **operator step**, done once on the real
board via `docs/hardware-test.md`, not something any module can do from a
dev machine without the physical gantry.

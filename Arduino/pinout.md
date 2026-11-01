# ESP32-WROVER pinout guide

This guide maps the gantry's two L298N motor channels and four endpoint
sensors to ESP32 GPIO numbers. The sketch defaults are intended as a
convenient starting layout for a typical ESP32-WROVER development board. They
are **not verified against your particular board or wiring**: check the GPIO
labels on the board, not the physical header position, before connecting
anything.

## Recommended plug-in layout

The recommended layout is the same as the sketch defaults. It keeps motor
control on output-capable GPIOs and puts sensors on input-only GPIOs, while
avoiding GPIOs 16/17 commonly used for WROVER PSRAM.

### Motors: ESP32 to L298N

| Axis | ESP32 signal | GPIO | Connect to L298N |
|---|---|---:|---|
| X | PWM enable | 25 | ENA |
| X | Direction 1 | 26 | IN1 |
| X | Direction 2 | 27 | IN2 |
| Y | PWM enable | 33 | ENB |
| Y | Direction 1 | 32 | IN3 |
| Y | Direction 2 | 13 | IN4 |

Remove the L298N's ENA and ENB jumper caps when using PWM speed control.

### Sensors: ESP32 to sensor OUT

| Sensor position | Default sketch name | GPIO | Special-IR sketch name |
|---|---|---:|---|
| X axis, one end | `X_MIN` | 34 | `X_MIN` |
| X axis, extreme opposite end | `X_MAX` | 35 | `special_IR` |
| Y axis, one end | `Y_MIN` | 36 | `Y_MIN` |
| Y axis, opposite end | `Y_MAX` | 39 | `Y_MAX` |

In the special sketch, `special_IR` **replaces `X_MAX`** at the extreme end
of the X axis. It is not an additional fifth sensor. Its default GPIO is 35
because `SPECIAL_IR_PIN` is set to `X_MAX_PIN`.

## Electrical and board notes

- Connect each sensor's OUT to its listed GPIO and power the IR modules from
  **3.3V**, not 5V. ESP32 GPIOs are not 5V tolerant.
- Connect ESP32 GND, sensor GND, and L298N GND together.
- GPIOs 34, 35, 36, and 39 are input-only and have no internal pull-up or
  pull-down. They are suitable for actively driven sensor outputs; do not use
  them for motor direction or PWM output.
- GPIOs 6-11 are normally connected to flash and should not be used. On many
  WROVER modules GPIOs 16/17 are used by PSRAM. Avoid boot-strapping pins
  (including GPIO 0, 2, 5, 12, and 15) for this setup.
- Board variants may omit or repurpose some GPIOs. If a listed GPIO is not
  exposed or is reserved on your board, select a suitable replacement using
  the instructions below.
- During bring-up, power the ESP32 via USB and the motors from the L298N
  supply. Do not connect USB and the L298N 5V output to the ESP32 at the same
  time.

## Change the pinout

Both sketches share their pin definitions and controller code in
[`Arduino.ino`](./Arduino.ino). To customize:

1. Open `Arduino.ino` in a text editor.
2. At the top, change the relevant GPIO constants:
   - Motor signals: `X_ENA`, `X_IN1`, `X_IN2`, `Y_ENB`, `Y_IN3`, `Y_IN4`.
   - Sensors: `X_MIN_PIN`, `X_MAX_PIN`, `Y_MIN_PIN`, `Y_MAX_PIN`.
3. Save, reopen or reload the selected sketch in Arduino IDE, and upload it.
4. Open Serial Monitor at 115200 baud and type `map` to confirm the sketch
   reports the GPIO assignments you expect.
5. With motor power off, verify sensor `raw` readings and endpoint indicators.
   Then use the supervised hardware checklist in `README.md` before enabling
   motor movement.

For the SpecialIR sketch, `SPECIAL_IR_PIN` defaults to `X_MAX_PIN`; changing
`X_MAX_PIN` therefore moves the special sensor as well. If you want to define
it separately, edit `SPECIAL_IR_PIN` explicitly and keep it on the extreme
X-axis endpoint. Do not assign both `X_MAX` and `special_IR` to separate
sensors on the same GPIO.

Pin assignment and endpoint polarity are separate settings. Set the relevant
`*_ACTIVE_LEVEL` constant (`HIGH` or `LOW`) to the electrical output observed
when that sensor detects its endpoint. In the SpecialIR sketch, use
`SPECIAL_IR_ACTIVE_LEVEL` for `special_IR`. A white-to-black or black-to-white
surface transition does not by itself tell you whether the module's digital
output is HIGH or LOW; check the Serial Monitor.

If you change a motor GPIO, also confirm its direction using a short, low-speed
jog and update `X_FORWARD_TOWARD_MAX` or `Y_FORWARD_TOWARD_MAX` if needed.
Never start autonomous `run` until all pin assignments, sensor readings, and
motor directions have been checked on the actual hardware.

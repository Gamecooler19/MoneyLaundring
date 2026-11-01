# Arduino sketches

This folder contains two Arduino IDE sketches for the ESP32 two-axis gantry.
Both print all four sensor readings continuously in Serial Monitor and include
the basic motor jog and endpoint-to-endpoint run commands.

## Choose a sketch

- `Arduino.ino` is the standard version. Its sensors are `X_MIN`, `X_MAX`,
  `Y_MIN`, and `Y_MAX`.
- `SpecialIR/SpecialIR.ino` is the mixed-polarity version. It declares the
  sensor variable `special_IR` in place of `X_MAX`. This special sensor is
  connected at the **extreme end of the X axis** (the X_MAX endpoint) and has
  its own active output level. The other three sensors retain their own
  settings. The special sketch includes the shared controller implementation
  from `Arduino.ino`, so fixes to the common behavior apply to both variants.

Open the selected `.ino` file in Arduino IDE, choose an ESP32-WROVER-compatible
board and the correct serial port, then upload. Open Serial Monitor at
**115200 baud**. Only open one sketch at a time; the two files are alternatives,
not sketches to combine in a single IDE window.

See [`pinout.md`](./pinout.md) for the default and recommended plug-in wiring,
board/GPIO cautions, and steps to customize pin assignments.

## Configure pins and sensor polarity

At the top of `Arduino.ino`, check the motor and sensor GPIO constants against
your board wiring. The defaults are provisional. The sensor defaults are GPIO
34 (`X_MIN`), 35 (`X_MAX` or `special_IR`), 36 (`Y_MIN`), and 39 (`Y_MAX`).
The special sensor uses the X_MAX slot at GPIO 35 by default. Change
`SPECIAL_IR_PIN` if it is wired to another suitable input GPIO.

Each sensor has an `*_ACTIVE_LEVEL` setting. This is the electrical level
(HIGH or LOW) reported by that module when its endpoint is detected; it is
separate from whether the surface changes white-to-black or black-to-white.
The standard sketch defaults all four to HIGH. In the special version,
`special_IR` defaults to LOW while the other three stay HIGH, allowing the
opposite electrical polarity described by this use case. These are starting
values: check the printed `raw=` value while moving each sensor between its
two surfaces and set its active level to the value observed at the endpoint.

Sensor readings appear about four times per second:

```text
[sensors] X_MIN raw=HIGH endpoint=no special_IR raw=LOW endpoint=YES ...
```

`raw` is the debounced electrical reading. `endpoint=YES` means that reading
matches that sensor's configured active level. This monitor runs even while
motors are stopped.

## Commands

Type a command in Serial Monitor and press Enter:

| Command | Effect |
|---|---|
| `?` | Print commands |
| `map` | Print GPIO assignments and active levels |
| `arm` | Permit motor commands; does not start motion by itself |
| `speed 60` | Set jog duty |
| `xf`, `xr`, `yf`, `yr` | Jog one axis for 400 ms |
| `sx 80`, `sy 80` | Set an axis's autonomous run speed |
| `run` | Bounce both axes between their endpoint sensors |
| `halt` | Stop motion while remaining armed |
| `stop` | Stop motors and disarm |

Motor duty is limited to `MAX_DUTY` (153 by default). Verify motor direction
with short, low-speed jogs before using `run`. If an endpoint is detected at
startup, the run starts by moving away from that endpoint.

## Safety and first use

1. Verify the wiring and GPIOs before connecting motor power.
2. Power the IR modules from **3.3V**, not 5V. ESP32 GPIOs are not 5V tolerant.
   Connect sensor, ESP32, and motor-driver grounds together.
3. Keep motor power off initially. Upload the chosen sketch and verify that
   each sensor's `raw` value changes and its `endpoint=YES` indication is
   correct at the intended endpoint. In particular, verify `special_IR` at
   the extreme X endpoint.
4. With the gantry clear and motor supply switch accessible, use the lowest
   reliable jog speed to confirm each motor direction. Update the direction
   constants if necessary.
5. Only after sensor polarity, pin mapping, and motor direction are confirmed,
   type `arm` and then `run`. Type `stop` immediately if anything behaves
   unexpectedly.

The sketch is a simplified standalone Arduino IDE version, separate from the
PlatformIO firmware in the repository root. Its timing and safety checks are
intended for supervised bring-up, not a substitute for validating the physical
hardware.

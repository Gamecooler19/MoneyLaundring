# Gantry live simulator

This folder contains a browser visualization of the ALT 1 gantry, its SCAD
layout, and exported part meshes. The assembly is upright, with axis A
horizontal and axis B vertical.

## Run it

From the repository root, start a local web server:

```bash
python3 -m http.server 8765 --directory simulation
```

Open <http://localhost:8765/simulate.html> in Chrome or Edge. The STL parts
and Three.js modules are included locally, but the page still needs HTTP for
module loading and Web Serial access; opening it directly as a `file://` URL
will not work.

## Live firmware mode

1. Connect the ESP32 by USB and upload/run the firmware.
2. Do not have Arduino Serial Monitor or another program connected to the same
   serial port; only one program can own it at a time.
3. Click **Connect ESP32**, select the board's serial port, and grant browser
   access. Live mode reads at 115200 baud and asks the firmware for `map` and
   `st` reports.
4. The simulator follows firmware `mon` sensor lines, run/reversal/fault and
   jog messages, and status reports. It accepts both the PlatformIO console
   format (`[mon]`, `[st]`, `[run]`) and the standalone Arduino sketch format
   (`[sensors]`, jog/run messages). `mon` must be on for current endpoint
   readings; use the command field to send `mon` if needed.
5. To operate the gantry, type a firmware console command in the command box.
   Commands that may arm or move the hardware (`arm`, `run`, and jogs) require
   an extra confirmation. `halt` and `stop` are available to stop motion.

The command box sends commands to the actual connected controller; this is
not a motor-control sandbox. Keep clear of the mechanism, verify its sensor
and direction setup, and be ready to stop it before sending any motion command.

### What live position means

The current firmware has endpoint switches, not encoders. The simulator cannot
read an exact position between endpoints. It anchors a live position when an
endpoint is reported, then estimates subsequent movement using observed run
telemetry; unanchored positions are shown as unknown. Jog motion is only
estimated when the direction map and a usable run-time estimate are available.
Treat these values as visualization, not measurement or a safety input.

Manual sliders, animation, and endpoint states are available while
disconnected. Live endpoint states come from firmware output instead of the
preview sliders.

## Included source assets

- `alt1_print.scad` — supplied OpenSCAD assembly and part definitions.
- `guide.stl`, `junction.stl`, `pinion.stl`, `rack.stl`,
  `tool_holder.stl` — supplied print exports used to render the assembly.
- `PRINT_LIST.txt` — supplied print and hardware list.
- `simulate.html` — standalone UI and Web Serial live monitor.
- `vendor/three/` — local Three.js 0.169.0 ES modules and their license.

The SCAD explicitly leaves out the IR sensors, motor housings, and brackets.
This simulator therefore does not show sensor hardware or claim exact sensor
mounting locations. `X_MIN`/`X_MAX` and `Y_MIN`/`Y_MAX` are endpoint signals,
not coordinates for mounting modules. Mount and align each real sensor to
detect its moving target near the corresponding travel limit and before a
mechanical stop; verify the wiring, pin mapping, and polarity on the hardware.
In the SpecialIR sketch, `special_IR` replaces `X_MAX`.

Both axes use the same 250 mm rack. The SCAD defines rack pitch as
`PI * 1.5 mm`, with home tooth positions 44 (A) and 45 (B), and end position
2. The resulting modeled travel is 42 pitches (197.920 mm) for A and 43
pitches (202.633 mm) for B. These travel distances are distinct from rack
length. The simulator derives its limits from those same pitch counts.

The model is not a physics simulation. Geometry clearances, collisions, motor
dynamics, and sensor beam behavior have not been validated.

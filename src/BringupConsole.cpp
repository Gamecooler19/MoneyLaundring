#include "BringupConsole.h"

#include <esp_system.h>

#include "config.h"

namespace {

// All candidate GPIOs the `scan` command may put into INPUT mode, verbatim
// from the Module 1 spec. The six configured motor-driver pins are removed
// from this list at scan time (never touched by scan at all) so a wire
// still attached to a direction/enable pin can't be reassigned to INPUT
// mid-bring-up.
constexpr uint8_t SCAN_CANDIDATE_PINS[] = {
    4, 5, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39,
};
constexpr size_t SCAN_CANDIDATE_COUNT =
    sizeof(SCAN_CANDIDATE_PINS) / sizeof(SCAN_CANDIDATE_PINS[0]);

bool isMotorDriverPin(uint8_t pin) {
  return pin == X_ENA || pin == X_IN1 || pin == X_IN2 || pin == Y_ENB ||
         pin == Y_IN3 || pin == Y_IN4;
}

// Module 4: human-readable name for esp_reset_reason_t, printed on every
// boot regardless of AUTO_RUN_ON_BOOT so the reset reason is always
// visible in the log.
const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

void BringupConsole::begin(Motor &motorX, Motor &motorY, LimitSensor &xMin,
                            LimitSensor &xMax, LimitSensor &yMin,
                            LimitSensor &yMax, GantryController &gantry) {
  _motorX = &motorX;
  _motorY = &motorY;
  _xMin = &xMin;
  _xMax = &xMax;
  _yMin = &yMin;
  _yMax = &yMax;
  _gantry = &gantry;

  _activeLevel = SENSOR_ACTIVE_LEVEL;
  _jogDuty = DEFAULT_SPEED;
  _lastCommandMs = millis();
  _lastMonitorMs = millis();

  // Module 4: the reset reason is always printed, whether or not auto-run
  // is enabled or fires, so the operator can tell at a glance why the
  // board came up (or why auto-run was skipped).
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("[boot] reset reason: ");
  Serial.println(resetReasonName(reason));

  if (!AUTO_RUN_ON_BOOT) {
    return;
  }
  if (reason != ESP_RST_POWERON) {
    Serial.print("[boot] AUTO_RUN_ON_BOOT is true but reset reason is not "
                  "POWERON (");
    Serial.print(resetReasonName(reason));
    Serial.println("); auto-run skipped.");
    return;
  }

  _autoRunPending = true;
  _autoRunStartMs = millis();
  Serial.print("[boot] AUTO_RUN_ON_BOOT: power-on reset detected. "
                "Auto-arm+run in ");
  Serial.print(AUTO_RUN_DELAY_MS);
  Serial.println(" ms. Send any character now to cancel.");
}

void BringupConsole::printPinMapAndTunables() {
  Serial.println("  Motor driver (L298N):");
  Serial.print("    X_ENA=");
  Serial.print(X_ENA);
  Serial.print("  X_IN1=");
  Serial.print(X_IN1);
  Serial.print("  X_IN2=");
  Serial.println(X_IN2);
  Serial.print("    Y_ENB=");
  Serial.print(Y_ENB);
  Serial.print("  Y_IN3=");
  Serial.print(Y_IN3);
  Serial.print("  Y_IN4=");
  Serial.println(Y_IN4);
  Serial.println("  IR endpoint sensors:");
  Serial.print("    X_MIN=");
  Serial.print(X_MIN);
  Serial.print("  X_MAX=");
  Serial.println(X_MAX);
  Serial.print("    Y_MIN=");
  Serial.print(Y_MIN);
  Serial.print("  Y_MAX=");
  Serial.println(Y_MAX);
  Serial.println("  Tunables:");
  Serial.print("    SERIAL_BAUD=");
  Serial.println(SERIAL_BAUD);
  Serial.print("    PWM_FREQ=");
  Serial.print(PWM_FREQ);
  Serial.print("  PWM_RES=");
  Serial.println(PWM_RES);
  Serial.print("    DEBOUNCE_MS=");
  Serial.println(DEBOUNCE_MS);
  Serial.print("    SENSOR_ACTIVE_LEVEL (compiled default)=");
  Serial.println(SENSOR_ACTIVE_LEVEL == HIGH ? "HIGH" : "LOW");
  Serial.print("    current active level (runtime, via pol)=");
  Serial.println(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.print("    JOG_MS=");
  Serial.println(JOG_MS);
  Serial.print("    MONITOR_PERIOD_MS=");
  Serial.println(MONITOR_PERIOD_MS);
  Serial.print("    ARM_TIMEOUT_MS=");
  Serial.println(ARM_TIMEOUT_MS);
  Serial.print("    SCAN_WINDOW_MS=");
  Serial.println(SCAN_WINDOW_MS);
  Serial.print("    SUPPLY_VOLTS=");
  Serial.print(SUPPLY_VOLTS);
  Serial.print("  MOTOR_RATED_VOLTS=");
  Serial.println(MOTOR_RATED_VOLTS);
  Serial.print("    MAX_DUTY=");
  Serial.println(MAX_DUTY);
  Serial.print("    current jog speed (duty)=");
  Serial.println(_jogDuty);
  Serial.println("  Axis bounce (Module 2):");
  Serial.print("    X_FORWARD_IS_TOWARD_MAX=");
  Serial.print(X_FORWARD_IS_TOWARD_MAX ? "true" : "false");
  Serial.print("  Y_FORWARD_IS_TOWARD_MAX=");
  Serial.println(Y_FORWARD_IS_TOWARD_MAX ? "true" : "false");
  Serial.print("    REVERSE_LOCKOUT_MS=");
  Serial.print(REVERSE_LOCKOUT_MS);
  Serial.print("  REVERSE_PAUSE_MS=");
  Serial.println(REVERSE_PAUSE_MS);
  Serial.print("    MAX_TRAVEL_MS=");
  Serial.println(MAX_TRAVEL_MS);
  Serial.print("    MAX_CLEAR_MS=");
  Serial.println(MAX_CLEAR_MS);
  Serial.print("    RUN_SPEED_X=");
  Serial.print(RUN_SPEED_X);
  Serial.print("  RUN_SPEED_Y=");
  Serial.println(RUN_SPEED_Y);
  Serial.println("  Module 4:");
  Serial.print("    RUN_TELEMETRY=");
  Serial.println(RUN_TELEMETRY ? "true" : "false");
  Serial.print("    AUTO_RUN_ON_BOOT=");
  Serial.print(AUTO_RUN_ON_BOOT ? "true" : "false");
  Serial.print("  AUTO_RUN_DELAY_MS=");
  Serial.println(AUTO_RUN_DELAY_MS);
}

void BringupConsole::printBanner() {
  Serial.println();
  Serial.println("=============================================================");
  Serial.println(" MoneyLaundring — Module 1 hardware bring-up console");
  Serial.println("=============================================================");
  Serial.println(" !!! PIN MAP BELOW IS UNCONFIRMED !!!");
  Serial.println(" It was never read off the physical board. Use `scan`, `mon`,");
  Serial.println(" and `pol` to verify every pin and the sensor polarity against");
  Serial.println(" the real wiring BEFORE typing `arm`. Motors will not move");
  Serial.println(" until you arm, and auto-disarm after an idle timeout.");
  Serial.println("-------------------------------------------------------------");
  printPinMapAndTunables();
  Serial.println("-------------------------------------------------------------");
  Serial.println(" Type ? for the command list.");
  Serial.println("=============================================================");
}

void BringupConsole::cmdHelp() {
  Serial.println("Commands:");
  Serial.println("  ?           show this help");
  Serial.println("  map         print pin map and tunables");
  Serial.println("  mon         toggle the continuous sensor status line (on by default)");
  Serial.println("  pol         flip the active sensor level and print the new value (not while running)");
  Serial.println("  scan        scan candidate GPIOs for level changes (disarmed only, not while running)");
  Serial.println("  arm         enable motor output (auto-disarms if idle too long, unless running)");
  Serial.println("  disarm      disable motor output (also ends any run)");
  Serial.println("  stop        stop both motors immediately and disarm (also ends any run)");
  Serial.println("  speed <n>   set jog duty 0-255, clamped to MAX_DUTY (not while running)");
  Serial.println("  xf          jog X forward for JOG_MS (armed only, not while running)");
  Serial.println("  xr          jog X reverse for JOG_MS (armed only, not while running)");
  Serial.println("  yf          jog Y forward for JOG_MS (armed only, not while running)");
  Serial.println("  yr          jog Y reverse for JOG_MS (armed only, not while running)");
  Serial.println("  run         start autonomous bounce on both axes (armed, idle only)");
  Serial.println("  halt        stop autonomous bounce, stay armed");
  Serial.println("  sx <0-255>  set axis X run speed, clamped to MAX_DUTY (allowed while running)");
  Serial.println("  sy <0-255>  set axis Y run speed, clamped to MAX_DUTY (allowed while running)");
  Serial.println("  st          print gantry status: state, heading, speed, reversals, fault, per axis");
}

void BringupConsole::cmdMap() {
  Serial.println("[map] pin map and tunables:");
  printPinMapAndTunables();
}

void BringupConsole::applyActiveLevelToSensors() {
  _xMin->setActiveLevel(_activeLevel);
  _xMax->setActiveLevel(_activeLevel);
  _yMin->setActiveLevel(_activeLevel);
  _yMax->setActiveLevel(_activeLevel);
}

void BringupConsole::cmdMon() {
  _monitoring = !_monitoring;
  Serial.print("[mon] monitor ");
  Serial.println(_monitoring ? "ON" : "OFF");
}

void BringupConsole::cmdPol() {
  if (_gantry->isRunning()) {
    Serial.println("[pol] refused: gantry is running. `halt` first.");
    return;
  }
  _activeLevel = (_activeLevel == HIGH) ? LOW : HIGH;
  applyActiveLevelToSensors();
  Serial.print("[pol] active level is now ");
  Serial.println(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.print("[pol] to make this permanent, set SENSOR_ACTIVE_LEVEL = ");
  Serial.print(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.println(" in include/config.h");
}

void BringupConsole::printSensorLine() {
  Serial.print("[mon] X_MIN(");
  Serial.print(X_MIN);
  Serial.print(")=");
  Serial.print(_xMin->raw());
  Serial.print(_xMin->active() ? "*" : " ");
  Serial.print("  X_MAX(");
  Serial.print(X_MAX);
  Serial.print(")=");
  Serial.print(_xMax->raw());
  Serial.print(_xMax->active() ? "*" : " ");
  Serial.print("  Y_MIN(");
  Serial.print(Y_MIN);
  Serial.print(")=");
  Serial.print(_yMin->raw());
  Serial.print(_yMin->active() ? "*" : " ");
  Serial.print("  Y_MAX(");
  Serial.print(Y_MAX);
  Serial.print(")=");
  Serial.print(_yMax->raw());
  Serial.print(_yMax->active() ? "*" : " ");
  Serial.print("  (level/active*, pol=");
  Serial.print(_activeLevel == HIGH ? "HIGH" : "LOW");
  Serial.println(")");
}

void BringupConsole::cmdScanStart(unsigned long nowMs) {
  if (_gantry->isRunning()) {
    Serial.println("[scan] refused: gantry is running. `halt` first.");
    return;
  }
  if (_armed) {
    Serial.println("[scan] refused: disarm first (scan only runs while disarmed)");
    return;
  }
  if (_scanPhase != ScanPhase::IDLE) {
    Serial.println("[scan] already running");
    return;
  }

  _scanPinCount = 0;
  for (size_t i = 0; i < SCAN_CANDIDATE_COUNT && _scanPinCount < SCAN_MAX_PINS; i++) {
    uint8_t pin = SCAN_CANDIDATE_PINS[i];
    if (isMotorDriverPin(pin)) {
      continue;  // never touch the six configured motor-driver pins
    }
    pinMode(pin, INPUT);  // never OUTPUT — these are unidentified/sensor pins
    _scanPins[_scanPinCount] = pin;
    _scanPinCount++;
  }

  Serial.print("[scan] watching ");
  Serial.print(_scanPinCount);
  Serial.println(" candidate pins (motor-driver pins excluded).");
  Serial.println("[scan] warning: unconnected pins may float and flicker on their own.");
  Serial.println("[scan] capturing 1s baseline...");

  _scanPhase = ScanPhase::BASELINE;
  _scanPhaseStartMs = nowMs;
}

void BringupConsole::cmdArm(unsigned long nowMs) {
  if (_scanPhase != ScanPhase::IDLE) {
    Serial.println("[arm] refused: scan is still running");
    return;
  }
  _armed = true;
  _lastCommandMs = nowMs;
  Serial.print("[arm] ARMED. Motors may move. Auto-disarm after ");
  Serial.print(ARM_TIMEOUT_MS);
  Serial.println(" ms without a command.");
}

void BringupConsole::cmdDisarm(const char *reason) {
  bool wasArmed = _armed;
  _armed = false;
  _jogAxis = JogAxis::NONE;
  if (_gantry->isRunning()) {
    _gantry->haltRun();
  }
  _motorX->stop();
  _motorY->stop();
  if (wasArmed) {
    Serial.print("[disarm] ");
    Serial.println(reason);
  }
}

void BringupConsole::cmdStop() {
  if (_gantry->isRunning()) {
    _gantry->haltRun();
  }
  _motorX->stop();
  _motorY->stop();
  _jogAxis = JogAxis::NONE;
  _armed = false;
  Serial.println("[stop] both motors stopped, disarmed");
}

void BringupConsole::cmdSpeed(const String &arg) {
  if (_gantry->isRunning()) {
    Serial.println("[speed] refused: gantry is running. Use sx/sy, or `halt` first.");
    return;
  }
  if (arg.length() == 0) {
    Serial.print("[speed] current jog duty is ");
    Serial.println(_jogDuty);
    return;
  }
  long requested = arg.toInt();
  if (requested < 0) {
    requested = 0;
  }
  if (requested > 255) {
    requested = 255;
  }
  uint8_t clamped = (uint8_t)requested;
  if (clamped > MAX_DUTY) {
    Serial.print("[speed] requested ");
    Serial.print(requested);
    Serial.print(" exceeds MAX_DUTY ");
    Serial.print(MAX_DUTY);
    Serial.print("; clamped to ");
    Serial.println(MAX_DUTY);
    clamped = MAX_DUTY;
  }
  _jogDuty = clamped;
  Serial.print("[speed] jog duty set to ");
  Serial.println(_jogDuty);
}

Motor *BringupConsole::motorFor(JogAxis axis) {
  if (axis == JogAxis::X) {
    return _motorX;
  }
  if (axis == JogAxis::Y) {
    return _motorY;
  }
  return nullptr;
}

void BringupConsole::cmdJog(JogAxis axis, Direction dir, const char *label,
                             unsigned long nowMs) {
  if (_gantry->isRunning()) {
    Serial.print("[");
    Serial.print(label);
    Serial.println("] refused: gantry is running. `halt` first.");
    return;
  }
  if (!_armed) {
    Serial.print("[");
    Serial.print(label);
    Serial.println("] refused: not armed. Type `arm` first.");
    return;
  }
  if (_jogAxis != JogAxis::NONE) {
    Serial.print("[");
    Serial.print(label);
    Serial.println("] refused: a jog is already in progress.");
    return;
  }

  Motor *m = motorFor(axis);
  if (m == nullptr) {
    return;
  }

  Serial.print("[");
  Serial.print(label);
  Serial.print("] driving for ");
  Serial.print(JOG_MS);
  Serial.print(" ms @ duty ");
  Serial.println(_jogDuty);

  // Module 3 cache audit: this drive() call reaches the motor directly,
  // bypassing GantryController entirely, so its per-motor command cache
  // has no idea this happened. Invalidate it now so that if a `run`
  // later commands this exact same direction/duty as its first command,
  // GantryController does not mistake it for "unchanged" and skip
  // calling Motor::drive() — the motor must not be left idle while the
  // controller believes it commanded it to run.
  _gantry->invalidateCommandCache();

  m->drive(dir, _jogDuty);
  _jogAxis = axis;
  _jogDir = dir;
  _jogStartMs = nowMs;
}

void BringupConsole::cmdRun(unsigned long nowMs) {
  if (!_armed) {
    Serial.println("[run] refused: not armed. Type `arm` first.");
    return;
  }
  if (_scanPhase != ScanPhase::IDLE) {
    Serial.println("[run] refused: a scan is in progress.");
    return;
  }
  if (_jogAxis != JogAxis::NONE) {
    Serial.println("[run] refused: a jog is in progress.");
    return;
  }
  if (_gantry->isRunning()) {
    Serial.println("[run] already running.");
    return;
  }
  _gantry->startRun(nowMs);
  Serial.println("[run] autonomous bounce started on both axes.");
}

void BringupConsole::cmdHalt() {
  if (!_gantry->isRunning()) {
    Serial.println("[halt] not running.");
    return;
  }
  _gantry->haltRun();
  Serial.println("[halt] autonomous bounce stopped. Still armed.");
}

void BringupConsole::cmdSetSpeedX(const String &arg) {
  if (arg.length() == 0) {
    Serial.println("[sx] usage: sx <0-255>");
    return;
  }
  long requested = arg.toInt();
  if (requested < 0) requested = 0;
  if (requested > 255) requested = 255;
  _gantry->setSpeedX((uint8_t)requested);
}

void BringupConsole::cmdSetSpeedY(const String &arg) {
  if (arg.length() == 0) {
    Serial.println("[sy] usage: sy <0-255>");
    return;
  }
  long requested = arg.toInt();
  if (requested < 0) requested = 0;
  if (requested > 255) requested = 255;
  _gantry->setSpeedY((uint8_t)requested);
}

void BringupConsole::cmdStatus() {
  _gantry->printStatus();
}

void BringupConsole::handleLine(String line, unsigned long nowMs) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  _lastCommandMs = nowMs;

  String lower = line;
  lower.toLowerCase();

  String cmd = lower;
  String arg = "";
  int spaceIdx = lower.indexOf(' ');
  if (spaceIdx >= 0) {
    cmd = lower.substring(0, spaceIdx);
    arg = lower.substring(spaceIdx + 1);
    arg.trim();
  }

  if (cmd == "?") {
    cmdHelp();
  } else if (cmd == "map") {
    cmdMap();
  } else if (cmd == "mon") {
    cmdMon();
  } else if (cmd == "pol") {
    cmdPol();
  } else if (cmd == "scan") {
    cmdScanStart(nowMs);
  } else if (cmd == "arm") {
    cmdArm(nowMs);
  } else if (cmd == "disarm") {
    cmdDisarm("manual disarm");
  } else if (cmd == "stop") {
    cmdStop();
  } else if (cmd == "speed") {
    cmdSpeed(arg);
  } else if (cmd == "xf") {
    cmdJog(JogAxis::X, Direction::FORWARD, "xf", nowMs);
  } else if (cmd == "xr") {
    cmdJog(JogAxis::X, Direction::REVERSE, "xr", nowMs);
  } else if (cmd == "yf") {
    cmdJog(JogAxis::Y, Direction::FORWARD, "yf", nowMs);
  } else if (cmd == "yr") {
    cmdJog(JogAxis::Y, Direction::REVERSE, "yr", nowMs);
  } else if (cmd == "run") {
    cmdRun(nowMs);
  } else if (cmd == "halt") {
    cmdHalt();
  } else if (cmd == "sx") {
    cmdSetSpeedX(arg);
  } else if (cmd == "sy") {
    cmdSetSpeedY(arg);
  } else if (cmd == "st") {
    cmdStatus();
  } else {
    Serial.print("[?] unknown command: ");
    Serial.println(line);
    Serial.println("Type ? for the command list.");
  }
}

void BringupConsole::updateArmTimeout(unsigned long nowMs) {
  // The idle auto-disarm must not apply while the gantry is autonomously
  // running: the safety stops in that mode are halt/stop/disarm/fault,
  // not an idle timer (a `run` can legitimately go unattended for a long
  // time without being a problem).
  if (_gantry->isRunning()) {
    return;
  }
  if (_armed && (nowMs - _lastCommandMs >= ARM_TIMEOUT_MS)) {
    cmdDisarm("idle timeout reached, auto-disarmed");
  }
}

void BringupConsole::updateJog(unsigned long nowMs) {
  if (_jogAxis == JogAxis::NONE) {
    return;
  }
  if (nowMs - _jogStartMs >= JOG_MS) {
    Motor *m = motorFor(_jogAxis);
    if (m != nullptr) {
      m->stop();
    }
    // See cmdJog()'s matching comment: this stop() also bypasses
    // GantryController, so invalidate its cache here too.
    _gantry->invalidateCommandCache();
    Serial.println("[jog] done, stopped");
    _jogAxis = JogAxis::NONE;
  }
}

void BringupConsole::updateScan(unsigned long nowMs) {
  if (_scanPhase == ScanPhase::IDLE) {
    return;
  }

  if (_scanPhase == ScanPhase::BASELINE) {
    if (nowMs - _scanPhaseStartMs >= SCAN_BASELINE_MS) {
      for (size_t i = 0; i < _scanPinCount; i++) {
        _scanLastLevel[i] = digitalRead(_scanPins[i]);
      }
      Serial.println("[scan] baseline captured. Touch/cover one sensor at a time now.");
      Serial.print("[scan] watching for ");
      Serial.print(SCAN_WINDOW_MS);
      Serial.println(" ms...");
      _scanPhase = ScanPhase::WINDOW;
      _scanPhaseStartMs = nowMs;
    }
    return;
  }

  // ScanPhase::WINDOW
  for (size_t i = 0; i < _scanPinCount; i++) {
    int level = digitalRead(_scanPins[i]);
    if (level != _scanLastLevel[i]) {
      _scanLastLevel[i] = level;
      Serial.print("[scan] GPIO ");
      Serial.print(_scanPins[i]);
      Serial.print(" -> ");
      Serial.println(level);
    }
  }

  if (nowMs - _scanPhaseStartMs >= SCAN_WINDOW_MS) {
    // Restore: the pins we touched have no other configured role in this
    // module (the six motor-driver pins were never touched), so leaving
    // them as INPUT is the correct restored state. The four sensor pins
    // among them are re-applied as INPUT here for clarity, matching how
    // LimitSensor::begin() already configured them.
    for (size_t i = 0; i < _scanPinCount; i++) {
      pinMode(_scanPins[i], INPUT);
    }
    Serial.println("[scan] window complete. Pin modes restored.");
    _scanPhase = ScanPhase::IDLE;
    _scanPinCount = 0;
  }
}

void BringupConsole::updateMonitor(unsigned long nowMs) {
  if (!_monitoring) {
    return;
  }
  if (nowMs - _lastMonitorMs >= MONITOR_PERIOD_MS) {
    _lastMonitorMs = nowMs;
    printSensorLine();
  }
}

void BringupConsole::updateFaultPoll() {
  if (_gantry->takeFaultFlag()) {
    // GantryController has already stopped both motors and halted both
    // axes by the time this flag is set; disarming here is this class's
    // own safety contract (every motion path requires `arm`, and a fault
    // must not leave the console in a state where another `run`/jog could
    // be issued without the operator explicitly re-arming first).
    cmdDisarm("gantry fault detected, auto-disarmed");
  }
}

void BringupConsole::updateAutoRun(unsigned long nowMs) {
  if (!_autoRunPending) {
    return;
  }
  if (nowMs - _autoRunStartMs < AUTO_RUN_DELAY_MS) {
    return;  // countdown still running
  }

  _autoRunPending = false;
  Serial.println("[boot] AUTO_RUN_ON_BOOT: countdown elapsed, auto-arming "
                  "and running now.");
  // Reuses the exact same internal paths the typed `arm` and `run`
  // commands use - there is no separate auto-run motion path. cmdArm()
  // updates _lastCommandMs itself (feeding the normal idle auto-disarm
  // timer); cmdRun() checks _armed, scan/jog state, and whether the
  // gantry is already running, exactly as it does for a typed `run`.
  cmdArm(nowMs);
  cmdRun(nowMs);
}

void BringupConsole::update(unsigned long nowMs) {
  // Sensor updates and GantryController::update() happen once per loop in
  // main.cpp, before this call, so no sensor is ever updated twice in one
  // tick. This method only handles console I/O and its own state
  // machines (jog/scan/monitor/arm-timeout), plus polling the gantry's
  // fault latch.
  updateFaultPoll();

  // Module 4: any byte received while an auto-run countdown is pending
  // cancels it immediately and is discarded entirely (not fed to
  // handleLine as a command), per AUTO_RUN_ON_BOOT's contract. Checked
  // before the normal read loop below so a cancelling byte can never also
  // be interpreted as the start of a command.
  if (_autoRunPending && Serial.available() > 0) {
    while (Serial.available() > 0) {
      Serial.read();
    }
    _autoRunPending = false;
    Serial.println("[boot] AUTO_RUN_ON_BOOT: cancelled by serial input.");
    return;
  }

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (_lineOverflowed) {
        // The whole overlong line, through this terminating newline,
        // is discarded: nothing from it (including whatever arrived
        // right before this '\n') is ever dispatched to handleLine().
        Serial.println("[?] line too long, discarded");
        _lineOverflowed = false;
      } else {
        handleLine(_inputBuffer, nowMs);
      }
      _inputBuffer = "";
    } else if (c != '\r') {
      if (_inputBuffer.length() >= MAX_LINE_LEN) {
        _lineOverflowed = true;
      } else {
        _inputBuffer += c;
      }
    }
  }

  updateArmTimeout(nowMs);
  updateJog(nowMs);
  updateScan(nowMs);
  updateMonitor(nowMs);
  updateAutoRun(nowMs);
}

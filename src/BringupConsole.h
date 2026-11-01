// BringupConsole.h — non-blocking serial command console for Module 1
// hardware bring-up.
//
// Purpose: let the operator find and verify the four sensor GPIOs, the
// sensor polarity, and each motor's direction on the real board, with the
// motors disabled until the operator explicitly arms them. This class
// implements no bounce/reversing logic at all — that is Module 2's `Axis`
// class (see docs/architecture.md).
//
// Every method that can run a motor goes through drive(), which clamps to
// MAX_DUTY (see Motor.cpp); every motor-moving command also checks the
// armed flag first and refuses to run otherwise. There is no command that
// bypasses either check.

#pragma once

#include <Arduino.h>

#include "GantryController.h"
#include "LimitSensor.h"
#include "Motor.h"

class BringupConsole {
 public:
  // Wire up the motor, sensor, and gantry-controller objects this console
  // will drive and report on. Does not touch any hardware itself
  // (Motor/LimitSensor begin() and GantryController::begin() must already
  // have been called).
  void begin(Motor &motorX, Motor &motorY, LimitSensor &xMin,
             LimitSensor &xMax, LimitSensor &yMin, LimitSensor &yMax,
             GantryController &gantry);

  // Print the startup banner: pin map, tunables, and the "unconfirmed,
  // check before arming" warning. Call once from setup(), after begin().
  void printBanner();

  // Call every loop() iteration with the current millis(), after the four
  // LimitSensor::update() calls and the GantryController::update() call
  // have already happened for this tick (see main.cpp). Non-blocking:
  // reads any available serial input, advances the jog/scan state
  // machines, checks the arm timeout (suspended while running), polls the
  // gantry's fault flag (disarming if set), and prints the periodic
  // sensor monitor line. Never calls delay().
  void update(unsigned long nowMs);

 private:
  enum class JogAxis { NONE, X, Y };
  enum class ScanPhase { IDLE, BASELINE, WINDOW };

  Motor *_motorX = nullptr;
  Motor *_motorY = nullptr;
  LimitSensor *_xMin = nullptr;
  LimitSensor *_xMax = nullptr;
  LimitSensor *_yMin = nullptr;
  LimitSensor *_yMax = nullptr;
  GantryController *_gantry = nullptr;

  bool _armed = false;
  unsigned long _lastCommandMs = 0;

  bool _monitoring = true;
  unsigned long _lastMonitorMs = 0;

  int _activeLevel = HIGH;  // current sensor polarity, flipped by `pol`

  uint8_t _jogDuty = 0;  // set from DEFAULT_SPEED in begin(), clamp-checked by `speed`
  JogAxis _jogAxis = JogAxis::NONE;
  Direction _jogDir = Direction::FORWARD;
  unsigned long _jogStartMs = 0;

  static constexpr size_t SCAN_MAX_PINS = 20;
  ScanPhase _scanPhase = ScanPhase::IDLE;
  unsigned long _scanPhaseStartMs = 0;
  uint8_t _scanPins[SCAN_MAX_PINS] = {0};
  int _scanLastLevel[SCAN_MAX_PINS] = {0};
  size_t _scanPinCount = 0;

  // Longest line handleLine() will ever see. Any character received
  // past this limit, for the rest of the current line (through and
  // including its terminating '\n'), is discarded instead of being
  // appended to _inputBuffer, so an overlong line can never leave a
  // partial tail behind to be misread as a fresh command once the
  // newline finally arrives. No real command is anywhere near this
  // length.
  static constexpr size_t MAX_LINE_LEN = 128;
  String _inputBuffer;
  bool _lineOverflowed = false;  // true while the current line exceeds MAX_LINE_LEN

  void handleLine(String line, unsigned long nowMs);

  void cmdHelp();
  void cmdMap();
  void cmdMon();
  void cmdPol();
  void cmdScanStart(unsigned long nowMs);
  void cmdArm(unsigned long nowMs);
  void cmdDisarm(const char *reason);
  void cmdStop();
  void cmdSpeed(const String &arg);
  void cmdJog(JogAxis axis, Direction dir, const char *label, unsigned long nowMs);
  void cmdRun(unsigned long nowMs);
  void cmdHalt();
  void cmdSetSpeedX(const String &arg);
  void cmdSetSpeedY(const String &arg);
  void cmdStatus();

  void updateArmTimeout(unsigned long nowMs);
  void updateJog(unsigned long nowMs);
  void updateScan(unsigned long nowMs);
  void updateMonitor(unsigned long nowMs);
  void updateFaultPoll();
  void updateAutoRun(unsigned long nowMs);

  void printPinMapAndTunables();
  void printSensorLine();
  void applyActiveLevelToSensors();
  Motor *motorFor(JogAxis axis);

  // Auto-run on boot (Module 4). Set up once in begin(), consumed by
  // updateAutoRun()/update(). See config.h's AUTO_RUN_ON_BOOT comment for
  // the full contract: fires only once, on any hardware reset the classic
  // ESP32 reports as ESP_RST_POWERON (EN pin, reset button, a reflash, or
  // a serial monitor opening the port - not only a true power cycle, since
  // this chip has no separate reset-reason code for an external-pin
  // reset), cancelable by any serial byte during the countdown, and
  // always calls cmdArm()/cmdRun() - the exact same internal paths the
  // typed `arm` and `run` commands use - never a separate motion path.
  bool _autoRunPending = false;
  unsigned long _autoRunStartMs = 0;
};

/*
  Two-axis ESP32 gantry controller (Arduino IDE sketch).

  Open this file in Arduino IDE, select the ESP32 board/port, upload, then
  open Serial Monitor at 115200 baud. Sensor readings are printed continuously.

  Check every GPIO and sensor polarity below against your actual wiring before
  enabling the motors. Sensor outputs connected to ESP32 GPIOs must not exceed
  3.3V. Keep the motor supply off during initial sensor checks.

  Commands (newline-terminated):
    ?                   show commands
    arm                 allow motor commands
    stop                stop motors and disarm
    speed <0-255>       set both jog speeds (clamped to MAX_DUTY)
    sx <0-255>          set X autonomous speed
    sy <0-255>          set Y autonomous speed
    xf / xr / yf / yr   jog a motor for 400 ms
    run                 bounce both axes between their end sensors
    halt                stop autonomous motion but stay armed
*/

#include <Arduino.h>
#include <stdlib.h>

// Provisional default wiring: verify these GPIOs on your ESP32-WROVER board.
constexpr uint8_t X_ENA = 25;
constexpr uint8_t X_IN1 = 26;
constexpr uint8_t X_IN2 = 27;
constexpr uint8_t Y_ENB = 33;
constexpr uint8_t Y_IN3 = 32;
constexpr uint8_t Y_IN4 = 13;

constexpr uint8_t X_MIN_PIN = 34;
constexpr uint8_t X_MAX_PIN = 35;
constexpr uint8_t Y_MIN_PIN = 36;
constexpr uint8_t Y_MAX_PIN = 39;
constexpr uint8_t SPECIAL_IR_PIN = X_MAX_PIN;

// Set each value to HIGH or LOW to match that sensor's output at its endpoint.
// The fourth sensor can use a different polarity from the other three.
constexpr int X_MIN_ACTIVE_LEVEL = HIGH;
constexpr int X_MAX_ACTIVE_LEVEL = HIGH;
constexpr int Y_MIN_ACTIVE_LEVEL = HIGH;
constexpr int Y_MAX_ACTIVE_LEVEL = HIGH;
constexpr int SPECIAL_IR_ACTIVE_LEVEL = LOW;

// Verify motor direction during low-speed jogs before using run.
constexpr bool X_FORWARD_TOWARD_MAX = true;
constexpr bool Y_FORWARD_TOWARD_MAX = true;

constexpr unsigned long SERIAL_BAUD = 115200;
constexpr unsigned long SENSOR_REPORT_MS = 250;
constexpr unsigned long DEBOUNCE_MS = 20;
constexpr unsigned long JOG_MS = 400;
constexpr unsigned long REVERSE_PAUSE_MS = 150;
constexpr unsigned long MAX_TRAVEL_MS = 20000;
constexpr unsigned long MAX_CLEAR_MS = 3000;
constexpr uint8_t MAX_DUTY = 153;
constexpr uint8_t DEFAULT_SPEED = 110;

struct Sensor {
  const char *name;
  uint8_t pin;
  int activeLevel;
  int lastRaw;
  bool active;
  unsigned long lastChangeMs;
};

struct Axis {
  const char *name;
  uint8_t enablePin;
  uint8_t in1Pin;
  uint8_t in2Pin;
  uint8_t minSensor;
  uint8_t maxSensor;
  bool forwardTowardMax;
  bool headingToMax;
  bool running;
  bool hasReversed;
  uint8_t speed;
  unsigned long segmentStartMs;
  unsigned long lastReverseMs;
  unsigned long lastUpdateMs;
  unsigned long clearTimeMs;
  unsigned long reversalCount;
};

Sensor xMinSensor = {"X_MIN", X_MIN_PIN, X_MIN_ACTIVE_LEVEL, LOW, false, 0};
#ifdef SPECIAL_IR_MODE
Sensor special_IR = {"special_IR", SPECIAL_IR_PIN, SPECIAL_IR_ACTIVE_LEVEL,
                     LOW, false, 0};
#else
Sensor xMaxSensor = {"X_MAX", X_MAX_PIN, X_MAX_ACTIVE_LEVEL, LOW, false, 0};
#endif
Sensor yMinSensor = {"Y_MIN", Y_MIN_PIN, Y_MIN_ACTIVE_LEVEL, LOW, false, 0};
Sensor yMaxSensor = {"Y_MAX", Y_MAX_PIN, Y_MAX_ACTIVE_LEVEL, LOW, false, 0};

#ifdef SPECIAL_IR_MODE
Sensor *sensors[] = {&xMinSensor, &special_IR, &yMinSensor, &yMaxSensor};
#else
Sensor *sensors[] = {&xMinSensor, &xMaxSensor, &yMinSensor, &yMaxSensor};
#endif

Axis axes[] = {
    {"X", X_ENA, X_IN1, X_IN2, 0, 1, X_FORWARD_TOWARD_MAX, true, false,
     false, DEFAULT_SPEED, 0, 0, 0, 0, 0},
    {"Y", Y_ENB, Y_IN3, Y_IN4, 2, 3, Y_FORWARD_TOWARD_MAX, true, false,
     false, DEFAULT_SPEED, 0, 0, 0, 0, 0},
};

constexpr size_t SENSOR_COUNT = sizeof(sensors) / sizeof(sensors[0]);
constexpr size_t AXIS_COUNT = sizeof(axes) / sizeof(axes[0]);

bool armed = false;
uint8_t jogSpeed = DEFAULT_SPEED;
bool jogActive = false;
size_t jogAxis = 0;
unsigned long jogStartMs = 0;
String inputLine;
unsigned long lastSensorReportMs = 0;

const char *levelName(int level) {
  return level == HIGH ? "HIGH" : "LOW";
}

void stopAxisMotor(const Axis &axis) {
  digitalWrite(axis.in1Pin, LOW);
  digitalWrite(axis.in2Pin, LOW);
  analogWrite(axis.enablePin, 0);
}

void drivePhysical(Axis &axis, bool forward, uint8_t duty) {
  duty = min(duty, MAX_DUTY);
  digitalWrite(axis.in1Pin, forward ? HIGH : LOW);
  digitalWrite(axis.in2Pin, forward ? LOW : HIGH);
  analogWrite(axis.enablePin, duty);
}

void driveToward(Axis &axis, bool towardMax) {
  const bool forward = towardMax == axis.forwardTowardMax;
  drivePhysical(axis, forward, axis.speed);
}

void updateSensors(unsigned long nowMs) {
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    Sensor &sensor = *sensors[i];
    const int raw = digitalRead(sensor.pin);
    if (raw != sensor.lastRaw) {
      sensor.lastRaw = raw;
      sensor.lastChangeMs = nowMs;
    } else if (nowMs - sensor.lastChangeMs >= DEBOUNCE_MS) {
      sensor.active = raw == sensor.activeLevel;
    }
  }
}

void printSensorReadings() {
  Serial.print("[sensors]");
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    const Sensor &sensor = *sensors[i];
    Serial.print(' ');
    Serial.print(sensor.name);
    Serial.print(" raw=");
    Serial.print(levelName(sensor.lastRaw));
    Serial.print(" endpoint=");
    Serial.print(sensor.active ? "YES" : "no");
  }
  Serial.println();
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  ?                   show this help");
  Serial.println("  arm                 allow motor commands");
  Serial.println("  stop                stop motors and disarm");
  Serial.println("  speed <0-255>       set both jog speeds");
  Serial.println("  sx <0-255>          set X autonomous speed");
  Serial.println("  sy <0-255>          set Y autonomous speed");
  Serial.println("  xf / xr / yf / yr   jog a motor for 400 ms");
  Serial.println("  run                 bounce both axes between endpoints");
  Serial.println("  halt                stop autonomous motion, stay armed");
  Serial.println("Sensor readings print continuously at 115200 baud.");
}

void printMap() {
  Serial.println("GPIO map (verify against your board wiring):");
  Serial.printf("  X motor ENA/IN1/IN2: %u/%u/%u\n",
                static_cast<unsigned>(X_ENA), static_cast<unsigned>(X_IN1),
                static_cast<unsigned>(X_IN2));
  Serial.printf("  Y motor ENB/IN3/IN4: %u/%u/%u\n",
                static_cast<unsigned>(Y_ENB), static_cast<unsigned>(Y_IN3),
                static_cast<unsigned>(Y_IN4));
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    Serial.printf("  %-9s GPIO %u, endpoint active at %s\n", sensors[i]->name,
                  static_cast<unsigned>(sensors[i]->pin),
                  levelName(sensors[i]->activeLevel));
  }
  Serial.printf("  MAX_DUTY=%u, jog speed=%u, X speed=%u, Y speed=%u\n",
                static_cast<unsigned>(MAX_DUTY),
                static_cast<unsigned>(jogSpeed),
                static_cast<unsigned>(axes[0].speed),
                static_cast<unsigned>(axes[1].speed));
}

void stopAllMotors() {
  for (size_t i = 0; i < AXIS_COUNT; ++i) {
    axes[i].running = false;
    stopAxisMotor(axes[i]);
  }
  jogActive = false;
}

void faultStop(const Axis &axis, const char *reason) {
  Serial.printf("[fault] axis %s: %s; stopping both motors and disarming\n",
                axis.name, reason);
  stopAllMotors();
  armed = false;
}

bool startAxis(Axis &axis, unsigned long nowMs) {
  const bool minActive = sensors[axis.minSensor]->active;
  const bool maxActive = sensors[axis.maxSensor]->active;
  if (minActive && maxActive) {
    Serial.printf("[fault] axis %s has both endpoint sensors active\n",
                  axis.name);
    return false;
  }

  axis.headingToMax = minActive || !maxActive;
  axis.running = true;
  axis.hasReversed = false;
  axis.segmentStartMs = nowMs;
  axis.lastReverseMs = nowMs;
  axis.lastUpdateMs = nowMs;
  axis.clearTimeMs = 0;
  axis.reversalCount = 0;
  stopAxisMotor(axis);
  return true;
}

bool startRun(unsigned long nowMs) {
  if (!armed) {
    Serial.println("Not armed. Type 'arm' first.");
    return false;
  }
  if (jogActive || axes[0].running || axes[1].running) {
    Serial.println("Stop the current motion before starting a run.");
    return false;
  }

  if (!startAxis(axes[0], nowMs) || !startAxis(axes[1], nowMs)) {
    stopAllMotors();
    armed = false;
    Serial.println("Run not started; correct the sensor condition and re-arm.");
    return false;
  }
  Serial.println("[run] both axes started");
  return true;
}

void updateAxis(Axis &axis, unsigned long nowMs) {
  if (!axis.running) {
    return;
  }

  const bool minActive = sensors[axis.minSensor]->active;
  const bool maxActive = sensors[axis.maxSensor]->active;
  const unsigned long elapsed = nowMs - axis.lastUpdateMs;
  axis.lastUpdateMs = nowMs;

  if (minActive && maxActive) {
    faultStop(axis, "both endpoint sensors active");
    return;
  }

  const bool behindActive = axis.headingToMax ? minActive : maxActive;
  const bool inPause =
      axis.hasReversed && nowMs - axis.lastReverseMs < REVERSE_PAUSE_MS;
  if (!behindActive) {
    axis.clearTimeMs = 0;
  } else if (!inPause) {
    axis.clearTimeMs += elapsed;
  }
  if (axis.clearTimeMs >= MAX_CLEAR_MS) {
    faultStop(axis, "endpoint sensor did not clear after moving away");
    return;
  }

  if (nowMs - axis.segmentStartMs >= MAX_TRAVEL_MS) {
    faultStop(axis, "travel timeout; no endpoint reached");
    return;
  }

  const bool aheadActive = axis.headingToMax ? maxActive : minActive;
  if (aheadActive) {
    axis.headingToMax = !axis.headingToMax;
    axis.hasReversed = true;
    axis.lastReverseMs = nowMs;
    axis.segmentStartMs = nowMs;
    axis.clearTimeMs = 0;
    axis.lastUpdateMs = nowMs;
    ++axis.reversalCount;
    stopAxisMotor(axis);
    Serial.printf("[run] axis %s reversed, count=%lu\n", axis.name,
                  axis.reversalCount);
    return;
  }

  if (inPause) {
    stopAxisMotor(axis);
  } else {
    driveToward(axis, axis.headingToMax);
  }
}

bool parseDuty(const String &argument, uint8_t &duty) {
  const char *start = argument.c_str();
  while (*start == ' ') {
    ++start;
  }
  if (*start == '\0') {
    return false;
  }
  char *end = nullptr;
  const long value = strtol(start, &end, 10);
  while (*end == ' ') {
    ++end;
  }
  if (end == start || *end != '\0' || value < 0 || value > 255) {
    return false;
  }
  duty = static_cast<uint8_t>(min(value, static_cast<long>(MAX_DUTY)));
  if (value > MAX_DUTY) {
    Serial.printf("Duty limited to MAX_DUTY=%u\n",
                  static_cast<unsigned>(MAX_DUTY));
  }
  return true;
}

void setSpeed(const String &command, const String &argument) {
  uint8_t duty = 0;
  if (!parseDuty(argument, duty)) {
    Serial.println("Use a duty from 0 to 255.");
    return;
  }
  if (axes[0].running || axes[1].running) {
    Serial.println("Stop the autonomous run before changing speeds.");
    return;
  }

  if (command == "speed") {
    jogSpeed = duty;
    Serial.printf("Jog speed set to %u\n", jogSpeed);
  } else if (command == "sx") {
    axes[0].speed = duty;
    Serial.printf("X run speed set to %u\n", axes[0].speed);
  } else {
    axes[1].speed = duty;
    Serial.printf("Y run speed set to %u\n", axes[1].speed);
  }
}

void startJog(size_t axisIndex, bool forward, unsigned long nowMs) {
  if (!armed) {
    Serial.println("Not armed. Type 'arm' first.");
    return;
  }
  if (axes[0].running || axes[1].running || jogActive) {
    Serial.println("Stop the current motion before jogging.");
    return;
  }
  jogAxis = axisIndex;
  jogStartMs = nowMs;
  jogActive = true;
  drivePhysical(axes[axisIndex], forward, jogSpeed);
  Serial.printf("[jog] axis %s %s for %lu ms\n", axes[axisIndex].name,
                forward ? "forward" : "reverse", JOG_MS);
}

void updateJog(unsigned long nowMs) {
  if (jogActive && nowMs - jogStartMs >= JOG_MS) {
    stopAxisMotor(axes[jogAxis]);
    jogActive = false;
    Serial.println("[jog] complete");
  }
}

void handleCommand(String line, unsigned long nowMs) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  const int separator = line.indexOf(' ');
  const String command = separator < 0 ? line : line.substring(0, separator);
  const String argument = separator < 0 ? "" : line.substring(separator + 1);

  if (command == "?" || command == "help") {
    printHelp();
  } else if (command == "map") {
    printMap();
  } else if (command == "arm") {
    armed = true;
    Serial.println("Armed. Motors may move only on explicit commands.");
  } else if (command == "stop" || command == "disarm") {
    stopAllMotors();
    armed = false;
    Serial.println("Motors stopped; disarmed.");
  } else if (command == "halt") {
    stopAllMotors();
    Serial.println("Run halted; still armed.");
  } else if (command == "run") {
    startRun(nowMs);
  } else if (command == "speed" || command == "sx" || command == "sy") {
    setSpeed(command, argument);
  } else if (command == "xf") {
    startJog(0, true, nowMs);
  } else if (command == "xr") {
    startJog(0, false, nowMs);
  } else if (command == "yf") {
    startJog(1, true, nowMs);
  } else if (command == "yr") {
    startJog(1, false, nowMs);
  } else {
    Serial.println("Unknown command. Type '?' for help.");
  }
}

void readSerialCommands(unsigned long nowMs) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (inputLine.length() > 0) {
        handleCommand(inputLine, nowMs);
        inputLine = "";
      }
    } else if (inputLine.length() < 80) {
      inputLine += c;
    } else {
      inputLine = "";
      Serial.println("Input too long; discarded.");
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  analogWriteResolution(8);

  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    Sensor &sensor = *sensors[i];
    pinMode(sensor.pin, INPUT);
    sensor.lastRaw = digitalRead(sensor.pin);
    sensor.active = sensor.lastRaw == sensor.activeLevel;
    sensor.lastChangeMs = millis();
  }

  for (size_t i = 0; i < AXIS_COUNT; ++i) {
    Axis &axis = axes[i];
    pinMode(axis.enablePin, OUTPUT);
    pinMode(axis.in1Pin, OUTPUT);
    pinMode(axis.in2Pin, OUTPUT);
    stopAxisMotor(axis);
  }

  Serial.println();
  Serial.println("ESP32 two-axis gantry controller");
  Serial.println("Sensor monitor is on. Open Serial Monitor at 115200 baud.");
  Serial.println("Check pin mapping and per-sensor polarity before motor use.");
  Serial.println("Type '?' for commands.");
  printMap();
}

void loop() {
  const unsigned long nowMs = millis();
  updateSensors(nowMs);
  readSerialCommands(nowMs);
  updateJog(nowMs);
  updateAxis(axes[0], nowMs);
  updateAxis(axes[1], nowMs);

  if (nowMs - lastSensorReportMs >= SENSOR_REPORT_MS) {
    lastSensorReportMs = nowMs;
    printSensorReadings();
  }
}

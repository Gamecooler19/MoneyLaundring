// JogGate.h — pure decision helper: should a jog be refused because the
// sensor ahead of the jogged direction is already active?
//
// Header-only, intentionally pure like Axis.h: no Arduino include, no
// config.h include, no Serial, no hardware types. Two tiny inline
// functions operating purely on bools, so this is testable natively via
// test/test_jog_gate/test_jog_gate.cpp with no ESP32/hardware involved.
//
// This file does not know about Motor::Direction, X_FORWARD_IS_TOWARD_MAX,
// or any GPIO — the caller (BringupConsole::cmdJog) is responsible for
// translating its own Direction/axis/sensor state into the three plain
// bools these functions take.

#pragma once

namespace JogGate {

// Given whether the jogged direction equals the motor's FORWARD
// direction, and whether FORWARD is mapped toward the MAX-end sensor
// for this axis, returns true if the sensor ahead of this jog is the
// MAX sensor (false means the ahead sensor is the MIN sensor instead).
//
// jogIsForward == forwardIsTowardMax captures both cases that put the
// ahead sensor at MAX: driving FORWARD when FORWARD is toward MAX, or
// driving REVERSE when FORWARD is toward MIN (i.e. REVERSE is toward
// MAX). The other two combinations put the ahead sensor at MIN.
inline bool aheadIsMax(bool jogIsForward, bool forwardIsTowardMax) {
  return jogIsForward == forwardIsTowardMax;
}

// Given which sensor is ahead (the result of aheadIsMax() above) and
// the two current debounced active flags, returns true if the jog must
// be refused because the sensor ahead is already active. A behind-only
// engagement (the sensor on the opposite end from the ahead one) never
// refuses — only the ahead sensor's own state matters here.
inline bool mustRefuse(bool aheadIsMaxResult, bool minActive, bool maxActive) {
  return aheadIsMaxResult ? maxActive : minActive;
}

}  // namespace JogGate

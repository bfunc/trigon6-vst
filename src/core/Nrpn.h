// NRPN encode/decode for the Trigon-6: CC 99 (param MSB), 98 (param LSB), 6 (value MSB), 38 (value LSB).
// Parameter numbers and values are 14-bit, split into two 7-bit bytes.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace t6
{

struct Cc
{
    uint8_t controller;
    uint8_t value;
};

// The four CCs of one NRPN, in sending order.
std::array<Cc, 4> encodeNrpn (int param, int value);

// The same as raw MIDI bytes: 4 x (status, controller, value), no running status. channel is 1..16.
std::array<uint8_t, 12> encodeNrpnBytes (int channel, int param, int value);

struct NrpnEvent
{
    int param;
    int value;
};

// Collects incoming CCs (on one channel) into NRPN events. A value is complete when its LSB (CC 38) arrives,
// which is how the Trigon-6 sends them. Increment/decrement (CC 96/97) are not handled: the synth does not send them.
class NrpnDecoder
{
public:
    // Returns an event when this CC completes one; other CCs return nothing and leave the state alone.
    std::optional<NrpnEvent> feed (uint8_t controller, uint8_t value);
    void reset();

private:
    int paramMsb = -1, paramLsb = -1, valueMsb = 0;
};

} // namespace t6

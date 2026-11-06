// Is the plugin's current sound the same as what is stored? Compares it with the synth slot it came from
// (the cached program dump) and with the last .syx saved to the PC, for the indicator in the top bar.
#pragma once

#include "ParamTable.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace t6
{

// The value of p held in 1024 program bytes (the byte at its NRPN number); nullopt when the bytes can't hold it.
std::optional<int> valueInProgram (const ParamDef& p, const std::vector<uint8_t>& data);

struct SoundDiff
{
    std::vector<size_t> params;  // indexes into paramTable() whose value differs
    bool name = false;           // the name differs (counts as a change)
    int count() const { return (int) params.size() + (name ? 1 : 0); }
    bool same() const { return count() == 0; }
};

// values: one per paramTable() entry, as NRPN values (a choice is min + index).
// name: the current sound's name; "" = not known, then names are not compared.
SoundDiff compareSound (const std::vector<uint8_t>& stored, const std::vector<int>& values, const std::string& name);

enum class SyncState
{
    NoSlot,    // the plugin does not know which slot the sound came from
    NotRead,   // the slot is known but its stored version is not read yet
    Checking,  // the slot is being read from the synth
    InSync,    // same as the slot
    PcOnly,    // differs from the slot, same as the last file saved to the PC
    Edited     // differs from the slot (and from the last PC file)
};

struct SyncInput
{
    bool slotKnown = false;
    int bank = 0, program = 0;
    std::vector<uint8_t> slotData;  // the slot's 1024 bytes from the cache; empty = not read
    bool checking = false;          // a read of the slot is running
    std::vector<int> values;
    std::string name;
    std::vector<uint8_t> pcData;    // the last file saved to the PC since this slot was picked; empty = none
    bool seesSynthKnobs = true;     // false: the synth does not send knob moves (Param Xmit is not NR)
};

struct SyncResult
{
    SyncState state = SyncState::NoSlot;
    SoundDiff diff;
    std::string text;     // short, for the top bar
    std::string tooltip;  // what differs, line by line
};

SyncResult syncStatus (const SyncInput&);

} // namespace t6

// The Trigon-6 global settings: where each one sits on the panel (Globals button + program selector 0-9),
// its NRPN number, and the value names the synth's display shows.
//
// Value numbering: the 1.1 manual's global table is partly the Prophet-6 one (e.g. it says Param Xmit
// 0 = NRPN). What the synth actually sends matches the panel's own lists instead (Param Xmit = 2 while
// NRPNs keep arriving, i.e. Off, CC, NR, ...). So `values` follow the panel lists; `verified` says
// whether that order was checked on the synth.
#pragma once

#include <string>
#include <vector>

namespace t6
{

struct GlobalDef
{
    int row;            // 0 = top row (Globals pressed once, red LED), 1 = bottom row (pressed twice, yellow LED)
    int button;         // program selector 0-9
    const char* name;   // as printed above the button
    int nrpn;           // -1 = none known
    std::vector<std::string> values;  // display names; empty = a plain number
    int numberOffset;   // for plain numbers: shown value = raw + offset
    std::vector<int> wanted;          // raw values this plugin needs; empty = any
    const char* wantedText;           // what to set, for the help text ("" if any)
    bool verified;      // value order checked on the synth
};

const std::vector<GlobalDef>& globalTable();
const GlobalDef* globalByNrpn (int nrpn);
const GlobalDef* globalAt (int row, int button);

constexpr int kGlobalNrpnBase = 1024;
inline bool isGlobalNrpn (int nrpn) { return nrpn >= kGlobalNrpnBase && nrpn < kGlobalNrpnBase + 64; }

// "NR", "All", "+3", or "raw 7" when out of range.
std::string globalValueName (const GlobalDef& g, int raw);

// NRPNs of the settings the connection needs.
constexpr int kGlobalMidiChannel = 1026;
constexpr int kGlobalParamXmit = 1029;
constexpr int kGlobalParamRcv = 1030;
constexpr int kGlobalSysex = 1032;
constexpr int kGlobalMidiOut = 1033;

} // namespace t6

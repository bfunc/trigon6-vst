// Connection status: what the MIDI layer has seen (a snapshot), turned into a checklist for the
// Connection page. Plain C++, so the rules are tested without JUCE or a synth.
#pragma once

#include "Sysex.h"

#include <map>
#include <string>
#include <vector>

namespace t6
{

struct LinkSnapshot
{
    double nowMs = 0;
    int pluginChannel = 1;
    bool hostOutput = false;  // NRPN go out through the host (FL) instead of a port the plugin opens

    std::string inName, outName;  // the ports in use ("" = none found)
    bool inOpen = false, outOpen = false;
    std::string inError, outError;

    double probeSentMs = -1;  // last "Check": device inquiry + globals + edit buffer requests
    double inquiryMs = -1;
    DeviceInfo device;
    double globalsMs = -1;
    std::vector<int> globalsDump;  // 25 values, in dump order
    double editBufferMs = -1;
    bool editBufferOk = false;
    size_t editBufferSize = 0;

    int nrpnInCount = 0;
    double lastNrpnMs = -1;
    int lastNrpnParam = -1, lastNrpnValue = 0, lastNrpnChannel = 0;
    std::map<int, int> globalNrpn;  // a global the synth sent as NRPN (when changed on the panel): nrpn -> raw
    std::map<int, double> globalNrpnMs;
    double lastAnyInMs = -1;  // any MIDI message at all from the synth
};

constexpr double kProbeTimeoutMs = 2500;  // from the first request; the three requests go out 250 ms apart

// Where a global's value comes from. The dump's order is assumed to be NRPN order (index = nrpn - 1024);
// that is not checked on the synth yet, so a value only from the dump is marked as a guess.
constexpr bool kDumpOrderVerified = false;

struct GlobalReading
{
    bool known = false;
    int raw = 0;
    bool fromDump = false;
};
GlobalReading readGlobal (const LinkSnapshot&, int nrpn);

enum class Health { Ok, Warn, Bad, Unknown };

struct GlobalDef;
// A setting the connection needs (MIDI Channel, Param Xmit/Rcv, SysEx, MIDI Out)?
bool isRequiredGlobal (const GlobalDef&);
// Ok / Bad against the wanted values (Warn if only the unchecked dump position says Bad); Unknown if not read.
// For a setting that isn't required: Ok once its value is known.
Health globalHealth (const LinkSnapshot&, const GlobalDef&);

struct CheckItem
{
    Health health;
    std::string title;
    std::string detail;
};

struct Report
{
    std::vector<CheckItem> items;
    bool allOk = false;  // every required check is Ok: the plugin opens on its panel instead of this page
};

Report evaluate (const LinkSnapshot&);

} // namespace t6

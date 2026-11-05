// Connection diagnosis: what the MIDI layer has seen (a snapshot), turned into a ladder of checks, a single
// verdict (the most likely cause and what to do), and warnings. Plain C++, so the rules are tested without
// JUCE or a synth.
#pragma once

#include "Sysex.h"

#include <map>
#include <string>
#include <vector>

namespace t6
{

enum class NoteTest { NotRun, Waiting, Heard, NotHeard };

struct LinkSnapshot
{
    double nowMs = 0;
    int pluginChannel = 1;
    bool hostOutput = false;  // NRPN go out through the host (FL) instead of a port the plugin opens
    int sharedBy = 1;         // plugin copies in this process sharing the connection

    std::string inName, outName;  // the ports in use ("" = none found)
    bool inOpen = false, outOpen = false;
    std::string inError, outError;
    std::string inSavedMissing, outSavedMissing;  // a saved port name that is not there (a fallback is used)

    double probeSentMs = -1;  // last Check: device inquiry + globals + edit buffer requests
    double inquiryMs = -1;
    DeviceInfo device;
    double globalsMs = -1;
    std::vector<int> globalsDump;  // 24 values, in dump order
    double editBufferMs = -1;
    bool editBufferOk = false;
    size_t editBufferSize = 0;
    std::string otherSysex;  // the last SysEx from the synth the plugin did not use: size and first bytes

    int nrpnInCount = 0;
    double lastNrpnMs = -1;
    int lastNrpnParam = -1, lastNrpnValue = 0, lastNrpnChannel = 0;
    int paramCcInCount = 0;  // CCs that look like panel parameters (not wheels, pedals, NRPN parts)
    int lastCcChannel = 0, lastCcNumber = -1;
    std::map<int, int> globalNrpn;  // a global the synth sent as NRPN (when changed on the panel): nrpn -> raw
    std::map<int, double> globalNrpnMs;
    double lastAnyInMs = -1;  // any MIDI message at all from the synth
    int echoCount = 0;        // NRPN we sent that came straight back: a MIDI loop

    NoteTest noteTest = NoteTest::NotRun;
};

constexpr double kProbeTimeoutMs = 2500;  // from the first request; the three requests go out 250 ms apart

// The dump's order is assumed to be NRPN order (index = nrpn - 1024); that is not checked on the synth yet,
// so a value seen only there is a guess: it never overrides proof and a mismatch is only a warning.
constexpr bool kDumpOrderVerified = false;

// A CC number that a panel control would send with Param Xmit = CC (not a wheel, pedal or NRPN part).
bool isParameterCc (int cc);

enum class Source { None, Nrpn, Proof, Dump };

struct GlobalReading
{
    bool known = false;
    int raw = 0;
    Source source = Source::None;
    std::string text;  // display text; for a proof it may cover several values ("USB or btH")
};
// Priority: the synth's own NRPN (newest), then what is proven by traffic, then the dump.
GlobalReading readGlobal (const LinkSnapshot&, int nrpn);

enum class Health { Ok, Warn, Bad, Unknown };

struct GlobalDef;
bool isRequiredGlobal (const GlobalDef&);  // MIDI Channel, Param Xmit/Rcv, SysEx, MIDI Out
// For a required setting: Ok / Bad against the wanted values (Warn when only the dump says Bad);
// Unknown if not read. For any other setting: Ok once its value is known.
Health globalHealth (const LinkSnapshot&, const GlobalDef&);

struct CheckItem
{
    Health health = Health::Unknown;
    std::string title;
    std::string detail;
    std::string fix;        // what to do, when not Ok
    bool required = true;   // counts for "connected"
    bool blocked = false;   // can't be judged until an earlier step works
    int globalRow = -1, globalButton = -1;  // the Globals button the fix is about
};

struct Report
{
    std::vector<CheckItem> steps;     // in order: each depends on the ones before
    std::vector<CheckItem> warnings;  // don't stop the connection, but explain odd behaviour
    bool allOk = false;               // every required step is Ok
    Health verdictHealth = Health::Unknown;
    std::string verdict, verdictFix;  // the first required step that is not Ok, or "Connected"
    int highlightRow = -1, highlightButton = -1;
    int suggestChannel = 0;           // the synth's channel when it differs from the plugin's (0 = none)
    bool offerNoteTest = false;       // the note test would tell the likely causes apart
};

Report evaluate (const LinkSnapshot&);

} // namespace t6

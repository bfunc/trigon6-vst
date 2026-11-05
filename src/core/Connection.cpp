#include "Connection.h"

#include "Globals.h"

#include <algorithm>

namespace t6
{

GlobalReading readGlobal (const LinkSnapshot& s, int nrpn)
{
    GlobalReading r;
    const double dumpMs = s.globalsMs;
    const auto it = s.globalNrpn.find (nrpn);
    const double nrpnMs = it != s.globalNrpn.end() ? s.globalNrpnMs.at (nrpn) : -1;
    const size_t index = (size_t) (nrpn - kGlobalNrpnBase);

    // The newer of the two sources wins.
    if (nrpnMs >= 0 && nrpnMs >= dumpMs)
        return { true, it->second, false };
    if (dumpMs >= 0 && nrpn >= kGlobalNrpnBase && index < s.globalsDump.size())
        return { true, s.globalsDump[index], true };
    return r;
}

namespace
{
std::string where (const GlobalDef& g)
{
    return std::string (g.row == 0 ? "top row" : "bottom row") + ", button " + std::to_string (g.button);
}

bool answeredSinceProbe (const LinkSnapshot& s, double ms) { return ms >= 0 && ms >= s.probeSentMs; }
bool probeTimedOut (const LinkSnapshot& s) { return s.probeSentMs >= 0 && s.nowMs - s.probeSentMs > kProbeTimeoutMs; }

// One reply to "Check": Ok if it came, Bad after the timeout, Unknown while waiting or before any check.
Health replyHealth (const LinkSnapshot& s, double replyMs)
{
    if (replyMs >= 0 && (s.probeSentMs < 0 || answeredSinceProbe (s, replyMs)))
        return Health::Ok;
    return probeTimedOut (s) ? Health::Bad : Health::Unknown;
}

std::vector<int> wantedValues (const LinkSnapshot& s, const GlobalDef& g)
{
    return g.nrpn == kGlobalMidiChannel ? std::vector<int> { 0, s.pluginChannel } : g.wanted;
}

CheckItem globalItem (const LinkSnapshot& s, int nrpn)
{
    const GlobalDef& g = *globalByNrpn (nrpn);
    const std::string title = std::string (g.name) + " (Globals " + where (g) + ")";
    const auto r = readGlobal (s, nrpn);
    const std::string want = nrpn == kGlobalMidiChannel ? std::to_string (s.pluginChannel) + " or All" : g.wantedText;
    const Health h = globalHealth (s, g);
    if (h == Health::Unknown)
        return { h, title,
                 "Not read yet. Set it to " + want
                     + ". (Check reads it when SysEx works; changing it on the synth also reports it.)" };

    std::string detail = "Is " + globalValueName (g, r.raw) + " (raw " + std::to_string (r.raw) + ")";
    if (r.fromDump && ! kDumpOrderVerified)
        detail += ", from the global dump (position not checked yet)";
    return { h, title, h == Health::Ok ? detail + "." : detail + ". Set it to " + want + "." };
}
} // namespace

bool isRequiredGlobal (const GlobalDef& g) { return ! g.wanted.empty() || g.nrpn == kGlobalMidiChannel; }

Health globalHealth (const LinkSnapshot& s, const GlobalDef& g)
{
    if (g.nrpn < 0)
        return Health::Unknown;
    const auto r = readGlobal (s, g.nrpn);
    if (! r.known)
        return Health::Unknown;
    if (! isRequiredGlobal (g))
        return Health::Ok;
    const auto wanted = wantedValues (s, g);
    if (std::find (wanted.begin(), wanted.end(), r.raw) != wanted.end())
        return Health::Ok;
    // A value read only from the unchecked dump position may be a wrong guess: warn, don't fail.
    return r.fromDump && ! kDumpOrderVerified ? Health::Warn : Health::Bad;
}

Report evaluate (const LinkSnapshot& s)
{
    Report rep;
    auto& items = rep.items;

    // Ports
    if (s.inName.empty())
        items.push_back ({ Health::Bad, "MIDI input from the synth",
                           "No \"Trigon 6\" input port. Is the synth on and its USB cable in? (Or pick a port above.)" });
    else if (! s.inOpen)
        items.push_back ({ Health::Bad, "MIDI input from the synth",
                           s.inName + " can't be opened"
                               + (s.inError.empty() ? std::string() : ": " + s.inError)
                               + ". Windows gives a MIDI port to one program at a time: close other apps using it "
                                 "(FL Studio: Options > MIDI settings, disable the Trigon input)." });
    else
        items.push_back ({ Health::Ok, "MIDI input from the synth", s.inName + " is open." });

    if (s.hostOutput)
        items.push_back ({ Health::Ok, "MIDI output to the synth",
                           "Through the host's MIDI out (in FL: the wrapper's MIDI output port, routed to the Trigon)." });
    else if (s.outName.empty())
        items.push_back ({ Health::Bad, "MIDI output to the synth",
                           "No \"Trigon 6\" output port. Is the synth on and its USB cable in? (Or pick a port above.)" });
    else if (! s.outOpen)
        items.push_back ({ Health::Bad, "MIDI output to the synth",
                           s.outName + " can't be opened"
                               + (s.outError.empty() ? std::string() : ": " + s.outError)
                               + ". Close other apps using it (FL Studio: Options > MIDI settings, Output)." });
    else
        items.push_back ({ Health::Ok, "MIDI output to the synth", s.outName + " is open." });

    // Replies to Check
    {
        CheckItem it { replyHealth (s, s.inquiryMs), "Synth answers (device inquiry)", "" };
        if (it.health == Health::Ok)
        {
            it.detail = s.device.isTrigon6 ? "Trigon-6" : "A device answered, but not as a Trigon-6";
            if (s.device.versionMajor >= 0)
                it.detail += ", firmware " + std::to_string (s.device.versionMajor) + "." + std::to_string (s.device.versionMinor);
            it.detail += ".";
            if (! s.device.isTrigon6)
                it.health = Health::Warn;
        }
        else if (it.health == Health::Bad)
            it.detail = "No answer. On the synth set MIDI Out = USB (Globals top row, button 9) and "
                        "MIDI SysEx = USB (top row, button 8), then press Check.";
        else
            it.detail = s.probeSentMs < 0 ? "Press Check." : "Waiting for the answer...";
        items.push_back (it);
    }
    {
        CheckItem it { replyHealth (s, s.globalsMs), "SysEx: global settings dump", "" };
        if (it.health == Health::Ok)
            it.detail = "Received (" + std::to_string (s.globalsDump.size()) + " values).";
        else if (it.health == Health::Bad)
            it.detail = "No dump. Set MIDI SysEx = USB (Globals top row, button 8).";
        else
            it.detail = s.probeSentMs < 0 ? "Press Check." : "Waiting...";
        items.push_back (it);
    }
    {
        CheckItem it { replyHealth (s, s.editBufferMs), "SysEx: current sound (edit buffer dump)", "" };
        if (it.health == Health::Ok)
        {
            it.detail = std::to_string (s.editBufferSize) + " bytes";
            it.detail += s.editBufferOk ? ", unpacked to 1024: OK." : ", expected 1171 (packed 1024): not understood.";
            if (! s.editBufferOk)
                it.health = Health::Warn;
        }
        else if (it.health == Health::Bad)
            it.detail = "No dump. Set MIDI SysEx = USB (Globals top row, button 8).";
        else
            it.detail = s.probeSentMs < 0 ? "Press Check." : "Waiting...";
        items.push_back (it);
    }

    // The globals the connection depends on
    for (int nrpn : { kGlobalMidiOut, kGlobalSysex, kGlobalParamRcv, kGlobalParamXmit, kGlobalMidiChannel })
        items.push_back (globalItem (s, nrpn));

    const size_t required = items.size();

    // Optional: proof that knob moves arrive
    if (s.nrpnInCount > 0)
    {
        CheckItem it { Health::Ok, "Knob moves from the synth (NRPN in)",
                       std::to_string (s.nrpnInCount) + " received; last NRPN " + std::to_string (s.lastNrpnParam) + " = "
                           + std::to_string (s.lastNrpnValue) + " on channel " + std::to_string (s.lastNrpnChannel) + "." };
        if (s.lastNrpnChannel != s.pluginChannel)
        {
            it.health = Health::Warn;
            it.detail += " The plugin uses channel " + std::to_string (s.pluginChannel) + ": set the same channel here or on the synth.";
        }
        items.push_back (it);
    }
    else
        items.push_back ({ Health::Unknown, "Knob moves from the synth (NRPN in)",
                           "None yet. Turn a knob on the synth to test (needs Param Xmit = NR)." });

    rep.allOk = std::all_of (items.begin(), items.begin() + (long) required,
                             [] (const CheckItem& i) { return i.health == Health::Ok; });
    return rep;
}

} // namespace t6

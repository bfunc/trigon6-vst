#include "Connection.h"

#include "Globals.h"

#include <algorithm>

namespace t6
{

namespace
{
std::string hexByte (int b)
{
    static const char* hex = "0123456789ABCDEF";
    return std::string { hex[(b >> 4) & 0x0F], hex[b & 0x0F] };
}
} // namespace

bool isParameterCc (int cc)
{
    switch (cc)
    {
        case 0: case 1: case 2: case 4: case 6: case 7: case 10: case 11: case 32: case 38: case 64:
        case 96: case 97: case 98: case 99: case 100: case 101:
            return false;  // bank select, wheels, pedals, volume, sustain, (N)RPN parts
        default:
            return cc < 120;  // 120-127 are channel mode messages
    }
}

GlobalReading readGlobal (const LinkSnapshot& s, int nrpn)
{
    const GlobalDef* g = globalByNrpn (nrpn);
    auto reading = [&] (int raw, Source src, std::string text)
    {
        if (text.empty() && g != nullptr)
            text = globalValueName (*g, raw);
        return GlobalReading { true, raw, src, text };
    };

    // 1. What the synth itself sent when the setting was changed on its panel.
    const auto it = s.globalNrpn.find (nrpn);
    if (it != s.globalNrpn.end()
        && ! (kDumpOrderVerified && s.globalsMs > s.globalNrpnMs.at (nrpn)))
        return reading (it->second, Source::Nrpn, "");

    // 2. What the traffic proves.
    switch (nrpn)
    {
        case kGlobalMidiOut:
            if (s.nrpnInCount > 0 || s.paramCcInCount > 0)
                return reading (1, Source::Proof, "USB or btH");  // panel moves arrive over USB
            break;
        case kGlobalParamXmit:
            if (s.nrpnInCount > 0)
                return reading (2, Source::Proof, "NR or nAS");
            if (s.paramCcInCount > 0)
                return reading (1, Source::Proof, "CC or CAS");
            break;
        case kGlobalSysex:
            if (s.globalsMs >= 0 || s.editBufferMs >= 0)
                return reading (1, Source::Proof, "USB");  // a dump arrived over USB
            break;
        case kGlobalMidiChannel:
            if (s.nrpnInCount > 0)
                return reading (s.lastNrpnChannel, Source::Proof, std::to_string (s.lastNrpnChannel) + " (seen)");
            if (s.paramCcInCount > 0)
                return reading (s.lastCcChannel, Source::Proof, std::to_string (s.lastCcChannel) + " (seen)");
            break;
        default: break;
    }

    // 3. The global dump (position assumed).
    const size_t index = (size_t) (nrpn - kGlobalNrpnBase);
    if (s.globalsMs >= 0 && nrpn >= kGlobalNrpnBase && index < s.globalsDump.size())
        return reading (s.globalsDump[index], Source::Dump, "");
    return {};
}

bool isRequiredGlobal (const GlobalDef& g) { return ! g.wanted.empty() || g.nrpn == kGlobalMidiChannel; }

namespace
{
std::vector<int> wantedValues (const LinkSnapshot& s, const GlobalDef& g)
{
    return g.nrpn == kGlobalMidiChannel ? std::vector<int> { 0, s.pluginChannel } : g.wanted;
}

bool answered (const LinkSnapshot& s, double ms) { return ms >= 0 && (s.probeSentMs < 0 || ms >= s.probeSentMs); }
bool timedOut (const LinkSnapshot& s) { return s.probeSentMs >= 0 && s.nowMs - s.probeSentMs > kProbeTimeoutMs; }

// "On the synth: press GLOBALS once (red LED), press 9 (MIDI Out), then BANK / TENS to USB, press GLOBALS to leave."
std::string globalFix (int nrpn, const std::string& value)
{
    const GlobalDef& g = *globalByNrpn (nrpn);
    return std::string ("On the synth: press GLOBALS ") + (g.row == 0 ? "once (red LED)" : "twice (yellow LED)")
           + ", press button " + std::to_string (g.button) + " (" + g.name + "), then BANK (up) / TENS (down) until it shows "
           + value + ". Press GLOBALS to leave.";
}

void pointAt (CheckItem& it, int nrpn)
{
    const GlobalDef& g = *globalByNrpn (nrpn);
    it.globalRow = g.row;
    it.globalButton = g.button;
}

std::string sourceNote (const GlobalReading& r)
{
    switch (r.source)
    {
        case Source::Nrpn:  return " (sent by the synth when it was changed)";
        case Source::Proof: return " (proven by what arrives)";
        case Source::Dump:  return kDumpOrderVerified ? " (from the global dump)" : " (from the global dump; position not checked yet)";
        case Source::None:  break;
    }
    return "";
}
} // namespace

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
    return r.source == Source::Dump && ! kDumpOrderVerified ? Health::Warn : Health::Bad;
}

Report evaluate (const LinkSnapshot& s)
{
    Report rep;
    auto& steps = rep.steps;
    const std::string busyFix =
        "Windows gives a MIDI port to one program at a time. Close the program that has it: FL Studio (Options > MIDI "
        "settings: disable the Trigon port), MIDI-OX, Sequential's updater, or the other Trigon6 Control (the standalone "
        "app and the plugin in FL can't both have it).";

    // 1. Input port
    CheckItem in { Health::Ok, "Input port (synth to computer)", "", "" };
    if (s.inName.empty())
    {
        in.health = Health::Bad;
        in.detail = "No port named \"Trigon 6\".";
        in.fix = "Switch the synth on and check the USB cable (a charge-only cable or an unpowered hub shows no port). "
                 "With a DIN cable through a MIDI interface, pick the interface's port above.";
    }
    else if (! s.inOpen)
    {
        in.health = Health::Bad;
        in.detail = s.inName + " is busy or unavailable.";
        in.fix = busyFix;
    }
    else
        in.detail = s.inName + " is open.";
    if (! s.inSavedMissing.empty() && in.health == Health::Ok)
        in.detail += " (The saved port \"" + s.inSavedMissing + "\" is gone; using this one.)";
    steps.push_back (in);

    // 2. Output port
    CheckItem out { Health::Ok, "Output port (computer to synth)", "", "" };
    if (s.hostOutput)
        out.detail = "Through the host's MIDI out (in FL: the wrapper's MIDI output port, routed to the Trigon).";
    else if (s.outName.empty())
    {
        out.health = Health::Bad;
        out.detail = "No port named \"Trigon 6\".";
        out.fix = in.health == Health::Bad && s.inName.empty() ? in.fix : "Pick the synth's output port above.";
    }
    else if (! s.outOpen)
    {
        out.health = Health::Bad;
        out.detail = s.outName + " is busy or unavailable.";
        out.fix = busyFix;
    }
    else
        out.detail = s.outName + " is open.";
    if (! s.outSavedMissing.empty() && out.health == Health::Ok)
        out.detail += " (The saved port \"" + s.outSavedMissing + "\" is gone; using this one.)";
    steps.push_back (out);

    const bool inOk = in.health == Health::Ok;
    const bool outOk = out.health == Health::Ok;

    // 3. Synth -> computer
    CheckItem rx { Health::Unknown, "The synth sends to the computer (MIDI Out)", "", "" };
    pointAt (rx, kGlobalMidiOut);
    if (s.lastAnyInMs >= 0)
    {
        rx.health = Health::Ok;
        rx.detail = s.nrpnInCount > 0 ? "Knob moves arrive." : s.inquiryMs >= 0 ? "The synth answered." : "MIDI arrives.";
    }
    else if (timedOut (s))
    {
        rx.health = Health::Bad;
        rx.detail = "Nothing arrived, not even an answer to Check. Most likely the synth sends to its DIN port instead of USB.";
        rx.fix = globalFix (kGlobalMidiOut, "USB") + " Also set MIDI SysEx = USB (button 8).";
    }
    else
    {
        rx.detail = s.probeSentMs < 0 ? "Nothing yet." : "Waiting for the answer...";
        rx.fix = "Press Check, or turn a knob on the synth.";
    }
    if (! inOk)
    {
        rx.blocked = true;
        rx.health = Health::Unknown;
    }
    steps.push_back (rx);
    const bool rxOk = rx.health == Health::Ok;

    // 4. Computer -> synth
    CheckItem tx { Health::Unknown, "The synth gets what the computer sends", "", "" };
    if (s.inquiryMs >= 0 && answered (s, s.inquiryMs))
    {
        tx.health = Health::Ok;
        tx.detail = std::string (s.device.isTrigon6 ? "A Trigon-6" : "A device") + " answered the inquiry";
        if (s.device.versionMajor >= 0)
            tx.detail += ", firmware " + std::to_string (s.device.versionMajor) + "." + std::to_string (s.device.versionMinor);
        tx.detail += ".";
    }
    else if (s.noteTest == NoteTest::Heard)
    {
        tx.health = Health::Ok;
        tx.detail = "The test note played.";
    }
    else if (s.noteTest == NoteTest::NotHeard)
    {
        tx.health = Health::Bad;
        tx.detail = "The test note did not play.";
        tx.fix = s.hostOutput ? "Check the host's routing: in FL, the wrapper's MIDI output port must match the Trigon's port in "
                                "Options > MIDI settings, Output."
                              : "Check the output port above and the synth's master volume. With a DIN cable, check the "
                                "interface's MIDI Out.";
    }
    else if (s.noteTest == NoteTest::Waiting)
    {
        tx.detail = "Did you hear the test note?";
        tx.fix = "Answer Yes or No.";
    }
    else if (s.hostOutput)
    {
        tx.required = false;  // nothing comes back through a host; only the note test can tell
        tx.detail = "Can't be checked through the host without you.";
        tx.fix = "Press Play test note.";
        rep.offerNoteTest = true;
    }
    else if (timedOut (s))
    {
        tx.health = Health::Warn;
        tx.detail = "No answer to the inquiry: either MIDI SysEx is not USB, or nothing reaches the synth.";
        tx.fix = "Press Play test note: it tells the two apart.";
        rep.offerNoteTest = true;
    }
    else
    {
        tx.detail = s.probeSentMs < 0 ? "Not checked yet." : "Waiting for the answer...";
        tx.fix = "Press Check.";
    }
    if (! outOk)
    {
        tx.blocked = true;
        tx.health = Health::Unknown;
    }
    steps.push_back (tx);
    const bool txOk = tx.health == Health::Ok;

    // 5. SysEx
    CheckItem sx { Health::Unknown, "SysEx: global settings and the current sound", "", "" };
    pointAt (sx, kGlobalSysex);
    if ((s.globalsMs >= 0 && answered (s, s.globalsMs)) || (s.editBufferMs >= 0 && answered (s, s.editBufferMs)))
    {
        sx.health = s.editBufferMs >= 0 && ! s.editBufferOk ? Health::Warn : Health::Ok;
        sx.detail = std::string (s.globalsMs >= 0 ? "Global dump received. " : "No global dump. ")
                    + (s.editBufferMs >= 0 ? (s.editBufferOk ? "Sound dump received (1171 bytes)."
                                                             : "Sound dump has " + std::to_string (s.editBufferSize)
                                                                   + " bytes instead of 1171.")
                                           : "No sound dump.");
        if (s.globalsMs < 0 && ! s.otherSysex.empty())
            sx.detail += " Other SysEx: " + s.otherSysex;
        if (sx.health == Health::Warn)
            sx.fix = "Press Check again. If the size stays wrong, the USB MIDI driver cuts long SysEx.";
    }
    else if (s.hostOutput)
    {
        sx.required = false;
        sx.detail = "The host may not pass SysEx from a plugin.";
        sx.fix = "For Check, switch Send NRPN to Direct (needs the ports free of the host).";
    }
    else if (timedOut (s))
    {
        sx.health = txOk ? Health::Bad : Health::Warn;
        sx.detail = "No dump arrived.";
        sx.fix = txOk ? globalFix (kGlobalSysex, "USB") : "First find out whether the synth gets anything: Play test note.";
        const auto sysex = readGlobal (s, kGlobalSysex);
        if (txOk && sysex.known && sysex.source != Source::Dump && sysex.raw == 1)
        {
            // The setting is right, so the synth ignored the request itself.
            sx.detail = "MIDI SysEx is USB, but the synth did not answer the dump request.";
            sx.fix = "Press Check again. If it stays red, send the developer the inquiry reply"
                     + std::string (s.device.raw.empty() ? "." : ": " + s.device.raw);
        }
    }
    else
    {
        sx.detail = s.probeSentMs < 0 ? "Not checked yet." : "Waiting...";
        sx.fix = "Press Check.";
    }
    if (! rxOk && sx.health != Health::Ok)
    {
        sx.blocked = true;
        sx.health = Health::Unknown;
    }
    steps.push_back (sx);

    // 6. Knob moves from the synth (needed for the plugin to follow the synth, not for sending)
    CheckItem kx { Health::Unknown, "Knob moves arrive as NRPN (Param Xmit)", "", "" };
    kx.required = false;
    pointAt (kx, kGlobalParamXmit);
    const auto xmit = readGlobal (s, kGlobalParamXmit);
    if (s.nrpnInCount > 0)
    {
        kx.health = Health::Ok;
        kx.detail = std::to_string (s.nrpnInCount) + " received; last NRPN " + std::to_string (s.lastNrpnParam) + " = "
                    + std::to_string (s.lastNrpnValue) + ".";
    }
    else if (xmit.known && globalHealth (s, *globalByNrpn (kGlobalParamXmit)) != Health::Ok)
    {
        kx.health = xmit.source == Source::Dump ? Health::Warn : Health::Bad;
        kx.detail = "Param Xmit is " + xmit.text + sourceNote (xmit) + ".";
        kx.fix = globalFix (kGlobalParamXmit, "NR");
    }
    else
    {
        kx.detail = "None yet.";
        kx.fix = "Turn any knob on the synth.";
    }
    if (! rxOk && kx.health != Health::Ok)
    {
        kx.blocked = true;
        kx.health = Health::Unknown;
    }
    steps.push_back (kx);

    // 7. The synth takes NRPN (Param Rcv): only readable, not provable without changing the sound
    CheckItem pr { Health::Unknown, "The synth takes NRPN (Param Rcv)", "", "" };
    pointAt (pr, kGlobalParamRcv);
    const auto rcv = readGlobal (s, kGlobalParamRcv);
    if (rcv.known)
    {
        pr.health = globalHealth (s, *globalByNrpn (kGlobalParamRcv));
        pr.detail = "Param Rcv is " + rcv.text + sourceNote (rcv) + ".";
        if (pr.health != Health::Ok)
            pr.fix = globalFix (kGlobalParamRcv, "NR");
    }
    else
    {
        pr.detail = "Not known: it shows in the global dump, or when you change it on the synth.";
        pr.fix = "Make sure Param Rcv = NR. " + globalFix (kGlobalParamRcv, "NR");
    }
    if (! txOk && pr.health != Health::Ok && ! s.hostOutput)
    {
        pr.blocked = true;
        pr.health = Health::Unknown;
    }
    steps.push_back (pr);

    // 8. MIDI channel
    CheckItem ch { Health::Unknown, "Same MIDI channel", "", "" };
    pointAt (ch, kGlobalMidiChannel);
    const auto chan = readGlobal (s, kGlobalMidiChannel);
    if (chan.known)
    {
        ch.health = globalHealth (s, *globalByNrpn (kGlobalMidiChannel));
        ch.detail = "The synth is on " + chan.text + sourceNote (chan) + "; the plugin on " + std::to_string (s.pluginChannel) + ".";
        if (ch.health != Health::Ok)
        {
            rep.suggestChannel = chan.raw >= 1 && chan.raw <= 16 ? chan.raw : 0;
            ch.fix = (rep.suggestChannel ? "Use channel " + std::to_string (rep.suggestChannel) + " in the plugin, or " : std::string ("Set "))
                     + globalFix (kGlobalMidiChannel, std::to_string (s.pluginChannel) + " or All");
        }
    }
    else
    {
        ch.detail = "The synth's channel is not known yet.";
        ch.fix = "Turn a knob on the synth (its channel shows up), or make sure: " + globalFix (kGlobalMidiChannel, std::to_string (s.pluginChannel) + " or All");
    }
    if (! rxOk && ch.health != Health::Ok && ! s.hostOutput)
    {
        ch.blocked = true;
        ch.health = Health::Unknown;
    }
    steps.push_back (ch);

    for (auto& it : steps)
        if (it.blocked)
        {
            it.detail = "Can't tell until the steps above work.";
            it.fix.clear();
        }

    // Warnings
    auto& w = rep.warnings;
    if (s.echoCount >= 2)
        w.push_back ({ Health::Warn, "MIDI loop",
                       std::to_string (s.echoCount) + " messages the plugin sent came straight back.",
                       "Don't route the Trigon's input back to its output (in FL: MIDI thru, or a MIDI Out channel on the same port)." });
    if (s.inquiryMs >= 0 && ! s.device.isTrigon6)
    {
        if (s.device.manufacturer == 0x01)
            w.push_back ({ Health::Warn, "Unexpected model ID",
                           "A Sequential synth answered with model " + hexByte (s.device.family)
                               + " (expected " + hexByte (kTrigon6Id) + "). Dump requests now use its ID. Reply: " + s.device.raw,
                           "If this is the Trigon-6, send this reply to the developer." });
        else
            w.push_back ({ Health::Warn, "Not a Trigon-6",
                           "The device on this port answered, but not as a Trigon-6. Reply: " + s.device.raw,
                           "Pick the synth's ports above." });
    }
    auto known = [&] (int nrpn, std::initializer_list<int> raws)
    {
        const auto r = readGlobal (s, nrpn);
        // Not from the dump while its order is a guess: a wrong warning would send you to the wrong button.
        return r.known && (r.source != Source::Dump || kDumpOrderVerified)
               && std::find (raws.begin(), raws.end(), r.raw) != raws.end();
    };
    if (known (1035, { 0 }))
        w.push_back ({ Health::Warn, "Local Ctrl is Off", "The synth's own keys and knobs don't play its sound.",
                       "If that's not on purpose: GLOBALS twice (yellow LED), button 0, set On." });
    if (known (1027, { 2, 3, 4 }))
        w.push_back ({ Health::Warn, "MIDI Clock is In", "The arpeggiator and sequencer wait for MIDI clock and don't run without it.",
                       "Send clock from the DAW, or GLOBALS once (red LED), button 3, set Off or Out." });
    if (s.sharedBy > 1)
        w.push_back ({ Health::Ok, "Shared connection",
                       std::to_string (s.sharedBy) + " copies of the plugin in this program share these ports.", "" });

    // Verdict
    rep.allOk = std::all_of (steps.begin(), steps.end(), [] (const CheckItem& i) { return ! i.required || i.health == Health::Ok; });
    for (const auto& it : steps)
        if (it.required && it.health != Health::Ok && ! it.blocked)
        {
            rep.verdictHealth = it.health;
            rep.verdict = it.title + ": " + it.detail;
            rep.verdictFix = it.fix;
            rep.highlightRow = it.globalRow;
            rep.highlightButton = it.fix.find ("GLOBALS") != std::string::npos ? it.globalButton : -1;
            if (rep.highlightButton < 0)
                rep.highlightRow = -1;
            break;
        }
    if (rep.allOk)
    {
        rep.verdictHealth = Health::Ok;
        rep.verdict = "Connected.";
        rep.verdictFix = "";
    }
    else if (rep.verdict.empty())
    {
        rep.verdict = "Not checked yet.";
        rep.verdictFix = "Press Check.";
    }
    return rep;
}

} // namespace t6

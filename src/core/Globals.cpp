#include "Globals.h"

namespace t6
{

namespace
{
std::vector<std::string> channelNames()
{
    std::vector<std::string> v { "All" };
    for (int ch = 1; ch <= 16; ++ch)
        v.push_back (std::to_string (ch));
    return v;
}

std::vector<std::string> tuningNames()
{
    std::vector<std::string> v { "Nor" };
    for (int t = 1; t <= 16; ++t)
        v.push_back (std::to_string (t));
    return v;
}

std::vector<GlobalDef> build()
{
    // Positions and value names: User's Guide 1.1, "Global Settings". NRPN numbers: its Appendix B.
    // Checked on the synth (it sends a global as NRPN when you change it): 1029, 1030, 1032, 1033.
    // Transpose 1024 and Master Tune 1025: the global dump starts 12, 50 (both centred), and 50 is out of
    // Transpose's range 0-24.
    return {
        // Top row
        { 0, 0, "Transpose",    1024, {}, -12, {}, "", false },
        { 0, 1, "Master Tune",  1025, {}, -50, {}, "", false },
        { 0, 2, "MIDI Channel", kGlobalMidiChannel, channelNames(), 0, {}, "the plugin's channel, or All", false },
        { 0, 3, "MIDI Clock",   1027, { "Off", "Out", "In", "i-0", "nSS" }, 0, {}, "", false },
        { 0, 4, "Clock Port",   1028, { "MID", "USB" }, 0, {}, "", false },
        { 0, 5, "Param Xmit",   kGlobalParamXmit, { "Off", "CC", "NR", "CAS", "nAS" }, 0, { 2, 4 }, "NR", true },
        { 0, 6, "Param Rcv",    kGlobalParamRcv, { "Off", "CC", "NR" }, 0, { 2 }, "NR", true },
        { 0, 7, "MIDI Control", 1031, { "Off", "On" }, 0, {}, "", false },
        { 0, 8, "MIDI SysEx",   kGlobalSysex, { "MID", "USB" }, 0, { 1 }, "USB", true },
        { 0, 9, "MIDI Out",     kGlobalMidiOut, { "MID", "USB", "btH", "PLY" }, 0, { 1, 2 }, "USB (or btH)", true },
        // Bottom row
        { 1, 0, "Local Ctrl",   1035, { "Off", "On" }, 0, {}, "", false },
        { 1, 1, "Seq Jack",     1039, { "NOR", "Tri", "Gat", "T-g" }, 0, {}, "", false },
        { 1, 2, "Pot Mode",     1037, { "Rel", "Pas", "Jup" }, 0, {}, "", false },
        { 1, 3, "Sustain +/-",  1040, { "Nor", "Rev", "n-r", "r-n" }, 0, {}, "", false },
        { 1, 4, "Alt Tuning",   1044, tuningNames(), 0, {}, "", false },
        { 1, 5, "Vel Response", 1041, {}, 0, {}, "", false },
        { 1, 6, "AT Response",  1042, {}, 0, {}, "", false },
        { 1, 7, "Stereo/Mono",  1043, { "Ste", "Mon" }, 0, {}, "", false },
        { 1, 8, "Pgm Dump",     -1, {}, 0, {}, "", false },  // an action, not a setting
        { 1, 9, "Beat Sync",    -1, {}, 0, {}, "", false },  // NRPN not in the manual
    };
}
} // namespace

const std::vector<GlobalDef>& globalTable()
{
    static const std::vector<GlobalDef> table = build();
    return table;
}

const GlobalDef* globalByNrpn (int nrpn)
{
    for (const auto& g : globalTable())
        if (g.nrpn >= 0 && g.nrpn == nrpn)
            return &g;
    return nullptr;
}

const GlobalDef* globalAt (int row, int button)
{
    for (const auto& g : globalTable())
        if (g.row == row && g.button == button)
            return &g;
    return nullptr;
}

GlobalInfo globalInfo (const GlobalDef& g)
{
    const std::string n = g.name;
    if (n == "Transpose")    return { "Shifts the whole synth up or down in semitones.", { "-12 to +12 semitones; 0 = no shift" } };
    if (n == "Master Tune")  return { "Fine-tunes the whole synth.", { "-50 to +50 cents; 0 = concert pitch" } };
    if (n == "MIDI Channel") return { "The channel the synth sends and receives on. This plugin sends on its own channel (top right).",
                                      { "All: receives on every channel", "1-16: one channel" } };
    if (n == "MIDI Clock")   return { "Whether the arpeggiator and sequencer run on their own clock or follow another device.",
                                      { "Off: own clock, sends none", "Out: own clock, sends it", "In: follows an incoming clock",
                                        "i-0: follows an incoming clock and sends it on", "nSS: follows an incoming clock, ignores start / stop" } };
    if (n == "Clock Port")   return { "Where the clock comes in and goes out.", { "MID: the 5-pin MIDI ports", "USB: the USB port" } };
    if (n == "Param Xmit")   return { "How the synth sends knob and button moves. This plugin needs NR.",
                                      { "Off: sends nothing", "CC: control changes", "NR: NRPN (full resolution)",
                                        "CAS: CC, with more of the panel (not checked yet)", "nAS: NRPN, with more of the panel (not checked yet)" } };
    if (n == "Param Rcv")    return { "Which incoming messages change the sound. This plugin sends NRPN, so it needs NR.",
                                      { "Off: none", "CC: control changes", "NR: NRPN" } };
    if (n == "MIDI Control") return { "Whether the synth reacts to MIDI controllers: mod wheel, pitch bend, sustain and the like.",
                                      { "Off: ignores them", "On: reacts to them" } };
    if (n == "MIDI SysEx")   return { "Which port carries SysEx: sound and settings dumps. This plugin needs USB to read sounds and presets.",
                                      { "MID: the 5-pin MIDI ports", "USB: the USB port" } };
    if (n == "MIDI Out")     return { "Where the synth's MIDI goes out. This plugin needs USB (or btH).",
                                      { "MID: the 5-pin MIDI out", "USB: the USB port", "btH: both", "PLY: poly chain (voices shared with a second unit)" } };
    if (n == "Local Ctrl")   return { "Whether the panel plays the voices directly.",
                                      { "Off: the panel only sends MIDI (for a DAW that sends it back)", "On: the panel plays the voices" } };
    if (n == "Seq Jack")     return { "What the sequencer jack on the back does.",
                                      { "NOR: normal", "Tri: trigger", "Gat: gate", "T-g: trigger and gate" } };
    if (n == "Pot Mode")     return { "What a knob does when its position and the stored sound differ.",
                                      { "Rel: relative, moves from the stored value", "Pas: passthrough, takes over when it passes the stored value",
                                        "Jup: jumps to the knob's position" } };
    if (n == "Sustain +/-")  return { "The sustain pedal's polarity.",
                                      { "Nor: normal", "Rev: reversed", "n-r, r-n: for a pedal of the other kind" } };
    if (n == "Alt Tuning")   return { "The tuning the voices play in.", { "Nor: equal temperament", "1-16: one of the alternative tunings" } };
    if (n == "Vel Response") return { "The velocity curve: how hard you play maps to the velocity the voices get.", { "A number: one of the curves" } };
    if (n == "AT Response")  return { "The aftertouch curve.", { "A number: one of the curves" } };
    if (n == "Stereo/Mono")  return { "The outputs.", { "Ste: stereo", "Mon: the same mono mix on both outputs" } };
    if (n == "Pgm Dump")     return { "An action, not a setting: sends every program as SysEx (for a backup).", {} };
    if (n == "Beat Sync")    return { "Not in the manual's NRPN list; the plugin cannot read it.", {} };
    return {};
}

std::string globalValueName (const GlobalDef& g, int raw)
{
    if (g.values.empty())
    {
        const int v = raw + g.numberOffset;
        return (g.numberOffset != 0 && v > 0 ? "+" : "") + std::to_string (v);
    }
    if (raw >= 0 && raw < (int) g.values.size())
        return g.values[(size_t) raw];
    return "raw " + std::to_string (raw);
}

} // namespace t6

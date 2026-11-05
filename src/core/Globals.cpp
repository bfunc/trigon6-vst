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
    return {
        // Top row
        { 0, 0, "Transpose",    1025, {}, -12, {}, "", false },
        { 0, 1, "Master Tune",  1024, {}, -50, {}, "", false },
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

#include "SoundSync.h"

#include "Presets.h"
#include "Sysex.h"

namespace t6
{

static std::string trimmed (const std::string& s)
{
    const auto a = s.find_first_not_of (' ');
    if (a == std::string::npos)
        return {};
    return s.substr (a, s.find_last_not_of (' ') - a + 1);
}

std::optional<int> valueInProgram (const ParamDef& p, const std::vector<uint8_t>& data)
{
    if (p.nrpn < 0 || (size_t) p.nrpn >= data.size() || (p.nrpn2 >= 0 && (size_t) p.nrpn2 >= data.size()) || p.max > 255)
        return std::nullopt;
    const int v = valueFromBytes (p, data[(size_t) p.nrpn], p.nrpn2 >= 0 ? data[(size_t) p.nrpn2] : 0);
    if (v < p.min || v > p.max)
        return std::nullopt;
    return v;
}

SoundDiff compareSound (const std::vector<uint8_t>& stored, const std::vector<int>& values, const std::string& name)
{
    SoundDiff d;
    const auto& table = paramTable();
    for (size_t i = 0; i < table.size() && i < values.size(); ++i)
    {
        const auto v = valueInProgram (table[i], stored);
        if (v && *v != values[i])
            d.params.push_back (i);
    }
    if (! name.empty() && stored.size() >= kNameOffset + kNameLength)
        d.name = trimmed (programName (stored)) != trimmed (name);
    return d;
}

namespace
{

std::string diffLines (const SoundDiff& d, const std::vector<uint8_t>& stored, const std::vector<int>& values, const std::string& name)
{
    const auto& table = paramTable();
    std::string out;
    if (d.name)
        out += "Name: " + trimmed (programName (stored)) + " -> " + trimmed (name) + "\n";
    size_t shown = 0;
    for (size_t i : d.params)
    {
        if (++shown > 12)
        {
            out += "... and " + std::to_string (d.params.size() - 12) + " more\n";
            break;
        }
        const auto& p = table[i];
        out += std::string (p.name) + ": " + formatValue (p, *valueInProgram (p, stored)) + " -> " + formatValue (p, values[i]) + "\n";
    }
    return out;
}

std::string changes (int n) { return std::to_string (n) + (n == 1 ? " change" : " changes"); }
} // namespace

SyncResult syncStatus (const SyncInput& in)
{
    SyncResult r;
    const auto label = programLabel (in.bank, in.program);
    const std::string blind = in.seesSynthKnobs ? ""
        : "\nThe synth does not send its knob moves (Param Xmit is not NR), so changes made on the synth are not seen here. Get sound reads them.";

    if (! in.slotKnown)
    {
        r.state = SyncState::NoSlot;
        r.text = "Slot unknown";
        r.tooltip = "Pick a program on the synth or Load one on the Presets tab, so the plugin knows which slot this sound came from.";
        return r;
    }
    if (in.checking)
    {
        r.state = SyncState::Checking;
        r.text = "Checking " + label + "...";
        r.tooltip = "Reading " + label + " from the synth, to compare it with the current sound.";
        return r;
    }
    if (in.slotData.size() != kProgramBytes)
    {
        r.state = SyncState::NotRead;
        r.text = label + " not read";
        r.tooltip = label + " is not read from the synth yet, so the sound can't be compared with it. Re-read it on the Presets tab.";
        return r;
    }

    r.diff = compareSound (in.slotData, in.values, in.name);
    if (r.diff.same())
    {
        r.state = SyncState::InSync;
        r.text = "Saved in " + label;
        r.tooltip = "The current sound is the same as " + label + " on the synth." + blind;
        if (! in.seesSynthKnobs)
            r.text += " ?";
        return r;
    }
    if (in.pcData.size() == kProgramBytes && compareSound (in.pcData, in.values, in.name).same())
    {
        r.state = SyncState::PcOnly;
        r.text = "Saved on PC only";
        r.tooltip = "The current sound is saved in a file on the PC, but " + label + " on the synth differs (" + changes (r.diff.count())
                    + "):\n" + diffLines (r.diff, in.slotData, in.values, in.name) + blind;
        return r;
    }
    r.state = SyncState::Edited;
    r.text = "Edited - " + changes (r.diff.count());
    if (! in.seesSynthKnobs)
        r.text += " ?";
    r.tooltip = "Changed from " + label + " on the synth:\n" + diffLines (r.diff, in.slotData, in.values, in.name) + blind;
    return r;
}

} // namespace t6

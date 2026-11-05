// The Trigon-6 program parameters: one declarative table, the single source of truth.
// Everything else (plugin parameters, NRPN sending, UI layout, tests) is built from it.
#pragma once

#include <string>
#include <vector>

namespace t6
{

// Panel sections, in the order the UI shows them.
enum class Section
{
    Oscillators,
    Mixer,
    Filter,
    FilterEnv,
    AmpEnv,
    Lfo,
    PolyMod,
    Aftertouch,
    Effects,
    ArpSeq,
    Misc
};

const char* sectionName (Section s);

enum class Kind
{
    Int,      // plain number
    Bipolar,  // shown as value - center (e.g. 0..254 -> -127..+127)
    Bool,     // 0 = off, 1 = on
    Choice    // value - min indexes `choices`
};

struct ParamDef
{
    int nrpn;
    const char* id;    // plugin parameter id; never rename once released (DAW automation uses it)
    const char* name;  // shown in the DAW and the UI
    Section section;
    int min, max;      // NRPN value range, inclusive
    Kind kind;
    int center = 0;                    // Bipolar only
    std::vector<std::string> choices;  // Choice only: max - min + 1 names
};

const std::vector<ParamDef>& paramTable();

// Index into paramTable() for an NRPN number, or -1 if the plugin does not know it.
int indexOfNrpn (int nrpn);

// Display text for a raw NRPN value, e.g. "-12", "On", "PH1".
std::string formatValue (const ParamDef& p, int value);

} // namespace t6

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
    // A second NRPN for one control (Osc 3 saw: Off / Saw / Ramp). The synth sends `nrpn` = 1 for "on"
    // and nrpn2 = 1 for "rising"; -1 = a plain one-NRPN parameter.
    int nrpn2 = -1;
    // Bool only: the synth's value is the opposite of the button's light (2 Pole: NRPN 33 = 0 is 2-pole).
    bool inverted = false;
};

struct NrpnValue
{
    int nrpn, value;
};

// What goes out for a parameter value: one NRPN, or for a two-NRPN control both, in the synth's order.
std::vector<NrpnValue> nrpnValues (const ParamDef& p, int value);
// The parameter's value after the synth sent `nrpn` = nrpnValue (nrpn is p.nrpn or p.nrpn2).
int valueAfterNrpn (const ParamDef& p, int current, int nrpn, int nrpnValue);
// The parameter's value from a program's bytes at nrpn and nrpn2 (for a plain parameter, the first).
int valueFromBytes (const ParamDef& p, int atNrpn, int atNrpn2);

const std::vector<ParamDef>& paramTable();

// Index into paramTable() for an NRPN number (either NRPN of a two-NRPN control), or -1 if the plugin does not know it.
int indexOfNrpn (int nrpn);

// Display text for a raw NRPN value, e.g. "-12", "On", "PH1".
std::string formatValue (const ParamDef& p, int value);

} // namespace t6

// Finding the synth's MIDI port by name. Windows changes port names: a number in front when the synth moves
// to another USB socket ("2- Trigon 6 Module"), an index behind with the newer MIDI service
// ("Trigon 6 Module [0]", "Trigon 6 Module 0"). A saved name must still find its port.
#pragma once

#include <string>
#include <vector>

namespace t6
{

// Lower case, without a "2- " prefix, a "[0]" or " 0" suffix and extra spaces.
std::string normalizePortName (const std::string& name);

struct PortPick
{
    int index = -1;              // into the list; -1 = none
    bool savedMissing = false;   // a saved name was given but nothing like it is there
};

// Exact saved name, else the same normalized name, else (no saved name, or it is gone) the first port
// containing "trigon".
PortPick pickPort (const std::vector<std::string>& names, const std::string& saved);

} // namespace t6

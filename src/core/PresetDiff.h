// How the current sound differs from its preset (the sound read from the synth or last saved), for the panel's
// marks on knobs and dots on buttons.
#pragma once

#include "ParamTable.h"

#include <string>
#include <vector>

namespace t6
{

// Indexes into paramTable() whose value differs; both vectors hold NRPN values, one per table entry.
// An empty preset (nothing loaded) differs nowhere.
std::vector<size_t> differingParams (const std::vector<int>& preset, const std::vector<int>& now);

// "preset 80, now 100, delta +20": the delta only for a number (knob), not for a button or a choice.
std::string diffText (const ParamDef& p, int preset, int now);

} // namespace t6

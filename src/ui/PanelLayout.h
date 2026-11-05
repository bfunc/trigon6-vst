// Where each parameter sits on the panel, after the Trigon-6 front panel (docs/trigon6-panel.jpg).
// Parameters themselves come from core/ParamTable; this file only arranges them by id.
#pragma once

#include <string>
#include <vector>

namespace t6ui
{

// A cell is a parameter id, "" (an empty slot) or "?Label" (a panel control whose NRPN the manual does not list).
using Row = std::vector<std::string>;

struct SectionLayout
{
    const char* title;
    float x, y, w, h;  // share of the panel area, 0..1
    std::vector<Row> rows;
};

const std::vector<SectionLayout>& panelLayout();

} // namespace t6ui

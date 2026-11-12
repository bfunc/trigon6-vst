// Where every control sits on the Panel tab, measured from the panel mockup (docs/panel-mockup.html) by
// tools/panel-geometry. All numbers are in design pixels on a canvas of panelGeo().width x height; the panel scales
// that canvas to the window. Parameters themselves come from core/ParamTable; this file only places them by id.
#pragma once

#include <vector>

namespace t6ui
{

struct Box
{
    float x, y, w, h;
};

struct Item
{
    Box box;
    const char* text;
};

struct SectionGeo
{
    const char* title;
    bool big;  // the three large sections: Oscillators, Filter, Effects
    Box frame, head;
};

// One control with its value and name. id is a parameter id, or "env:fenv" / "env:vca" for the two envelope displays.
struct CellGeo
{
    const char* id;
    Box cell, ctl, main;   // the whole cell, the row the control sits in, the control itself
    Box val;               // the value under a knob (w == 0: none)
    Box lab;               // the name (w == 0: none)
    const char* labText;   // the name as the mockup shows it
    bool inBlock;          // one of a block of target buttons: the name is set a little tighter
    bool marker;           // shows the dot that tells the value differs from the preset
    std::vector<Item> items;  // LED lists, the clock table and the LED row: one entry per choice, in drawing order
};

struct PanelGeo
{
    float width, height;
    std::vector<SectionGeo> sections;
    std::vector<CellGeo> cells;
};

const PanelGeo& panelGeo();

} // namespace t6ui

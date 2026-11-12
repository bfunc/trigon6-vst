# Panel geometry

The Panel tab is drawn on a fixed canvas (1382 x 785 design pixels) and scaled to the window. Where every control sits
comes from the panel mockup, `docs/panel-mockup.html`, a self-contained page: the layout there is CSS flow, so the
numbers are measured from the rendered page instead of being worked out again in C++.

To change the layout:

1. Change the mockup (`docs/panel-mockup.html`) and look at it in Chrome.
2. `npm i puppeteer-core`, then `node tools/panel-geometry/measure.cjs <path to chrome.exe> <folder>` writes `geo.json` and
   `mockup.png` (the page at scale 1) into the folder.
3. `python tools/panel-geometry/gen.py <folder>/geo.json src/ui/PanelLayout.cpp` rewrites the table.

The mockup's wooden cheeks (22 px) and top bar (44 px) are not part of the canvas; the editor draws its own.

`tests/PanelSnapshot.cpp` (local only, `-DT6_SNAPSHOT=ON`) draws the tab to a PNG so it can be compared with `mockup.png`.
It can build on Linux: all the JUCE code here is portable.

What the mockup shows that the plugin only follows in part: the two knobs of each effect have the ranges of the synth's
flangers (255 / 127) and Ring Mod (127 / 255) only in the mockup; the plugin's parameters are still 0..127 for both.

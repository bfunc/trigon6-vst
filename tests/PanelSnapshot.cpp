// Draws the Panel tab to a PNG without a window, for comparing it with the mockup (docs/panel-mockup.html). Built only
// with -DT6_SNAPSHOT=ON (a local tool, not in CI).
//   t6_snapshot <out.png> [state.json] [scale] [parameter id to click | all]
// state.json: { "saved": { id: value, ... }, "now": { id: value, ... } }  (the preset, then the values that differ)

#include "PluginProcessor.h"
#include "core/ParamTable.h"
#include "ui/PanelLayout.h"
#include "ui/PanelView.h"

#include <cstdio>

static void setAll (Trigon6Processor& proc, const juce::var& values)
{
    const auto& table = t6::paramTable();
    if (auto* o = values.getDynamicObject())
        for (const auto& prop : o->getProperties())
            for (size_t i = 0; i < table.size(); ++i)
                if (prop.name.toString() == table[i].id)
                    proc.setParamTo (i, (int) prop.value);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: t6_snapshot out.png [state.json] [scale]\n");
        return 2;
    }
    Trigon6Processor proc;
    if (argc > 2)
    {
        const auto state = juce::JSON::parse (juce::File (argv[2]));
        setAll (proc, state["saved"]);
        proc.noteSavedToSynth (0, 0, "Snapshot");
        setAll (proc, state["now"]);
    }
    const float scale = argc > 3 ? (float) std::atof (argv[3]) : 1.0f;

    PanelView view (proc);
    const auto size = PanelView::designSize();
    view.setBounds (0, 0, size.x, size.y);
    int failures = 0;
    if (argc > 4 && juce::String (argv[4]) == "all")
    {
        // click every control once: a knob, button, saw, LED list or octave dial must change its value
        for (const auto& c : t6ui::panelGeo().cells)
        {
            const juce::String id (c.id);
            if (id.startsWith ("env:"))
                continue;
            size_t index = 0;
            for (size_t i = 0; i < t6::paramTable().size(); ++i)
                if (id == t6::paramTable()[i].id)
                    index = i;
            const int before = proc.currentValue (index);
            view.activate (id);
            const bool window = proc.currentValue (index) == before && t6::paramTable()[index].kind == t6::Kind::Choice
                                && t6::paramTable()[index].nrpn2 < 0 && id != "lfo_shape" && id != "arp_mode" && id != "arp_octaves"
                                && id != "clock_divide" && id != "filter_keyboard" && ! id.endsWith ("_octave");
            if (proc.currentValue (index) == before && ! window)
            {
                std::printf ("no change after a click on %s\n", c.id);
                ++failures;
            }
        }
        std::printf ("clicked every control, %d without effect\n", failures);
    }
    else if (argc > 4)
        view.activate (argv[4]);
    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, scale);
    juce::File out (argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk())
    {
        std::printf ("cannot write %s\n", argv[1]);
        return 1;
    }
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("wrote %s (%d x %d)\n", argv[1], image.getWidth(), image.getHeight());
    return failures == 0 ? 0 : 1;
}

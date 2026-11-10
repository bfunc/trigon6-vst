// Offline check of the processor's preset reference: what counts as the preset, "reset to preset" for one parameter
// and for all, that the reset reaches the synth as NRPN, and the output mode default. Built only with
// -DT6_PLAYTEST=ON (a local check, not in CI).

#include "PluginProcessor.h"
#include "core/Nrpn.h"
#include "core/ParamTable.h"

#include <cstdio>
#include <map>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// The NRPN values that went out in one run of blocks: param -> last value
static std::map<int, int> sentIn (Trigon6Processor& p, int blocks)
{
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    t6::NrpnDecoder dec;
    std::map<int, int> sent;
    for (int i = 0; i < blocks; ++i)
    {
        midi.clear();
        p.processBlock (audio, midi);
        juce::Thread::sleep (3); // the send queue paces each parameter (20 ms apart)
        for (const auto m : midi)
            if (const auto msg = m.getMessage(); msg.isController())
                if (const auto ev = dec.feed ((uint8_t) msg.getControllerNumber(), (uint8_t) msg.getControllerValue()))
                    sent[ev->param] = ev->value;
    }
    return sent;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto& table = t6::paramTable();
    const size_t cutoff = (size_t) t6::indexOfNrpn (29), reson = (size_t) t6::indexOfNrpn (30);

    // a new plugin sends straight to the synth's port
    {
        Trigon6Processor p;
        CHECK (! p.isHostOutput());
        CHECK (! p.hasPreset());
        CHECK (p.presetDiffCount() == 0);
        p.resetAllToPreset(); // no preset: nothing happens
        CHECK (p.presetDiffCount() == 0);

        // a saved state keeps its own choice; the default does not override it
        p.setHostOutput (true);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        Trigon6Processor q;
        CHECK (! q.isHostOutput());
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.isHostOutput());
        p.setHostOutput (false);
        mb.reset();
        p.getStateInformation (mb);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (! q.isHostOutput());
    }

    // the preset is the sound at the last save (and Get sound, which goes through the same step)
    {
        Trigon6Processor p;
        p.setHostOutput (true);
        sentIn (p, 4); // the first blocks take the values as already on the synth

        std::vector<uint8_t> data (1024, 0);
        p.noteSavedToPc (data);
        CHECK (p.hasPreset());
        CHECK (p.getPresetValues() == p.currentValues());

        const int cutoffPreset = p.getPresetValues()[cutoff], resonPreset = p.getPresetValues()[reson];
        p.setParamTo (cutoff, 100);
        p.setParamTo (reson, 7);
        CHECK (p.currentValue (cutoff) == 100 && p.currentValue (reson) == 7);
        CHECK (p.presetDiffCount() == 2);
        const auto sent = sentIn (p, 20);
        CHECK (sent.count (29) && sent.at (29) == 100);

        // one parameter back
        p.resetToPreset (cutoff);
        CHECK (p.currentValue (cutoff) == cutoffPreset);
        CHECK (p.presetDiffCount() == 1);
        auto back = sentIn (p, 20);
        CHECK (back.count (29) && back.at (29) == cutoffPreset);
        CHECK (! back.count (30)); // the other one was not touched

        // everything back, a switch and a choice too
        size_t boolIdx = table.size(), choiceIdx = table.size();
        for (size_t i = 0; i < table.size(); ++i)
        {
            if (table[i].kind == t6::Kind::Bool && table[i].nrpn2 < 0 && boolIdx == table.size()) boolIdx = i;
            if (table[i].kind == t6::Kind::Choice && table[i].nrpn2 < 0 && choiceIdx == table.size()) choiceIdx = i;
        }
        CHECK (boolIdx < table.size() && choiceIdx < table.size());
        const int boolPreset = p.getPresetValues()[boolIdx], choicePreset = p.getPresetValues()[choiceIdx];
        p.setParamTo (boolIdx, boolPreset == 0 ? 1 : 0);
        p.setParamTo (choiceIdx, table[choiceIdx].max);
        CHECK (p.presetDiffCount() == 3);
        sentIn (p, 20); // the moved values reach the synth first
        p.resetAllToPreset();
        CHECK (p.presetDiffCount() == 0);
        CHECK (p.currentValue (reson) == resonPreset && p.currentValue (boolIdx) == boolPreset && p.currentValue (choiceIdx) == choicePreset);
        auto all = sentIn (p, 20);
        CHECK (all.count (30) && all.at (30) == resonPreset);
        CHECK (all.count (table[boolIdx].nrpn) && all.count (table[choiceIdx].nrpn));

        // saving makes the current sound the preset
        p.setParamTo (cutoff, 55);
        CHECK (p.presetDiffCount() == 1);
        p.noteSavedToSynth (0, 1, "Test");
        CHECK (p.presetDiffCount() == 0 && p.getPresetValues()[cutoff] == 55);
    }

    std::printf (failures ? "%d failures\n" : "processor ok\n", failures);
    return failures ? 1 : 0;
}

#pragma once

#include "PluginProcessor.h"
#include "ui/HardwareLook.h"

#include <memory>

class PresetsPage;
class SavePanel;
#include <vector>

// Top bar (title, sound status, MIDI channel, Get sound, Save, Send all, sent count) over three tabs: Connection
// (ports, status, the synth's Globals buttons), Panel (styled after the Trigon-6 front panel) and Presets.
class Trigon6Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Trigon6Editor (Trigon6Processor&);
    ~Trigon6Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    Trigon6Processor& proc;
    hw::Look look;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Label channelLabel, status, soundStatus;
    juce::ComboBox channel;
    juce::TextButton sendAll { "Send all" };
    juce::TextButton getSound { "Get sound" };
    juce::TextButton save { "Save" };
    void openSave();
    std::unique_ptr<SavePanel> savePanel;
    PresetsPage* presetsPage = nullptr;  // owned by tabs
    class Tabs;
    std::unique_ptr<Tabs> tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Editor)
};

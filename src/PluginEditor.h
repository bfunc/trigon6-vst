#pragma once

#include "PluginProcessor.h"

#include <memory>
#include <vector>

// Top bar (MIDI channel, Send all, sent count) over two tabs: Connection (ports, status, the synth's Globals
// buttons) and Panel (laid out like the Trigon-6 front panel).
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
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Label title, channelLabel, status;
    juce::ComboBox channel;
    juce::TextButton sendAll { "Send all" };
    class Tabs;
    std::unique_ptr<Tabs> tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Editor)
};

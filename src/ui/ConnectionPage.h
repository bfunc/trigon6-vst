// The Connection page: output mode and ports; a verdict (the most likely cause and what to do) over a
// ladder of checks and warnings; active tests (Check, a test note); and a map of the synth's Globals
// buttons (which program selector 0-9 holds which setting, its current and wanted value), since the
// labels on the synth are hard to read.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "core/Connection.h"

class ConnectionPage : public juce::Component, private juce::Timer
{
public:
    explicit ConnectionPage (Trigon6Processor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    const t6::Report& getReport() const { return report; }
    std::function<void()> onReportChanged;

private:
    void timerCallback() override;
    void refreshPortLists();
    void applyPorts();
    void paintChecklist (juce::Graphics&, juce::Rectangle<int>);
    void updateButtons();
    void paintGlobals (juce::Graphics&, juce::Rectangle<int>);

    Trigon6Processor& proc;
    t6::LinkSnapshot snap;
    t6::Report report;

    juce::Label outputLabel, inPortLabel, outPortLabel;
    juce::ComboBox outputMode, inPort, outPort;
    juce::TextButton check { "Check" }, playNote { "Play test note" }, heard { "I heard it" }, notHeard { "No sound" };
    juce::TextButton useChannel;
    juce::StringArray inNames, outNames;

    juce::Rectangle<int> checklistArea, globalsArea;
    // Monitor: every NRPN the synth sends, newest first (to find what a panel control sends).
    juce::Label monitorTitle;
    juce::TextEditor monitor;
    juce::TextButton clearMonitor { "Clear" };
    juce::StringArray monitorLines;
    uint32_t monitorSeen = 0;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConnectionPage)
};

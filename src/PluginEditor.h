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
    void parentHierarchyChanged() override;  // the standalone's title bar shows the version

private:
    void timerCallback() override;

    Trigon6Processor& proc;
    hw::Look look;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Label channelLabel, status, soundStatus;
    juce::Label sync;  // is the current sound saved: "Saved in U 012", "Edited - 3 changes", "Saved on PC only"
    void updateSync();
    juce::ComboBox channel;
    juce::TextButton sendAll { "Send all" };
    juce::TextButton getSound { "Get sound" };
    juce::TextButton save { "Save" };
    juce::TextButton resetAll { "Reset" };  // to the preset (tooltip)
    // Hold to hear the preset; release to go back to the edit. Releases on mouse up, focus loss or disable.
    struct HoldButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        ~HoldButton() override { release(); }
        std::function<void()> onPress, onRelease;
        bool held = false;
        void mouseDown (const juce::MouseEvent&) override
        {
            if (! isEnabled() || held)
                return;
            held = true;
            setToggleState (true, juce::dontSendNotification);
            if (onPress)
                onPress();
        }
        void mouseUp (const juce::MouseEvent&) override { release(); }
        void focusLost (FocusChangeType) override { release(); }
        void enablementChanged() override
        {
            juce::TextButton::enablementChanged();
            if (! isEnabled())
                release();
        }
        void release()
        {
            if (! held)
                return;
            held = false;
            setToggleState (false, juce::dontSendNotification);
            if (onRelease)
                onRelease();
        }
    };
    HoldButton compare { "Compare" };
    void openSave();
    void openPanel (int bank, int program, bool rename);
    std::unique_ptr<SavePanel> savePanel;
    PresetsPage* presetsPage = nullptr;  // owned by tabs
    class Tabs;
    std::unique_ptr<Tabs> tabs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Editor)
};

// Save the current sound: to a user slot on the synth, to a .syx file on the PC, or both.
// Rename: the same panel for one stored program; its sound stays, only the name changes.
// Both can set the program's type (kept on the PC, see PresetTypes.h).
// Before a slot is written its old contents go to Documents\Trigon6 Control\Backups, and after the write the
// slot is read back and compared. Factory banks are never offered.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/AppSettings.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class SavePanel : public juce::Component, private juce::Timer
{
public:
    enum class Mode { Save, Rename };
    SavePanel (Trigon6Processor&, int bank, int program, Mode = Mode::Save);
    ~SavePanel() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Target { Synth, Pc, Both };
    enum class Stage { Idle, ReadSound, ReadSlot, Write, Verify };
    enum class SlotState { Reading, Known, NoReply };

    void timerCallback() override;
    void setTarget (Target, bool store);
    bool toSynth() const { return target != Target::Pc; }
    bool toPc() const { return target != Target::Synth; }
    int bank() const;
    int program() const;
    void readSlot();
    void startSave();
    void soundArrived();
    void slotArrived (const t6::StoredProgram&);
    void verifyArrived (const t6::StoredProgram&);
    void finish (const juce::String& text, juce::Colour);
    juce::File folder() const;
    juce::File pcFile() const;
    void chooseFolder();
    void update();

    Trigon6Processor& proc;
    const Mode mode;
    bool renaming() const { return mode == Mode::Rename; }
    juce::String chosenType() const;  // "" = from the name
    Target target = Target::Synth;
    Stage stage = Stage::Idle;
    double stageMs = 0;
    int tries = 0;
    uint32_t soundSeqAtRequest = 0, programSeen = 0;
    bool saving = false;  // false: the read on opening, only for the sound's name

    SlotState slotState = SlotState::Reading;
    juce::String slotName;
    double slotAskedMs = 0;
    int slotTries = 0;

    std::vector<uint8_t> data;  // the sound being saved, with its new name

    juce::Rectangle<int> card;
    juce::Label title, nameLabel, nameCount, targetLabel, slotLabel, slotInfo, slotWarn, typeLabel, folderLabel, folderPath, fileInfo, status;
    juce::TextEditor nameEdit;
    juce::TextButton synthButton { "Synth" }, pcButton { "PC" }, bothButton { "Synth + PC" };
    juce::ComboBox bankBox, programBox, typeBox;
    juce::TextButton changeFolder { "Change..." }, cancelButton { "Cancel" }, saveButton { "Save" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<AppSettings> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SavePanel)
};

// The Presets page: the synth's stored programs by bank, read one at a time (Fetch), kept in a cache on disk
// and shown at start without asking the synth; Load sends a program change; programs save as .syx files.
// A type (Bass, Pad, ...) can be set per program (kept on the PC) and the list filtered by type.
// Nothing here writes into the synth's memory itself: Rename and Save open the SavePanel, which does.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "core/Presets.h"
#include "ui/AppSettings.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class PresetsPage : public juce::Component, private juce::TableListBoxModel, private juce::Timer, private juce::ChangeListener
{
public:
    explicit PresetsPage (Trigon6Processor&);
    ~PresetsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The selected row when exactly one is selected: the default slot for Save.
    std::optional<std::pair<int, int>> selectedSlot() const;

    // Rename: the editor opens the SavePanel for this slot.
    std::function<void (int bank, int program)> onRename;

private:
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;

    int currentBank() const;
    std::pair<int, int> slotOfRow (int row) const;  // bank, program
    juce::String typeOf (int bank, int program);
    void rebuildRows();
    void setFilter (const juce::String& type);
    void showTypeMenu();
    void setTypeForSelected (const juce::String& type);
    void renameSelected();
    t6::PresetEntry& entry (int bank, int program);
    juce::File cacheDir() const;
    void loadCache();
    void writeCache (int bank, int program);

    void loadSelected();
    void rereadSelected();
    void fetchBank (int bank);
    void fetchAll();
    void saveAll();
    void saveAs();
    void updateButtons();

    Trigon6Processor& proc;
    std::vector<t6::PresetEntry> entries;  // bank * kProgramsPerBank + program
    std::vector<int> rows;                 // what the table shows: indexes into entries
    juce::String typeFilter;               // "" = the selected bank; else that type in all banks
    t6::FetchQueue queue;
    uint32_t programSeen = 0, pcSeen = 0;
    bool wasBusy = false;

    juce::ComboBox bankBox;
    juce::TableListBox table;
    juce::TextButton loadButton { "Load" }, rereadButton { "Re-read" }, fetchBankButton { "Fetch this bank" },
        fetchAllButton { "Fetch all banks" }, cancelButton { "Cancel" }, saveAllButton { "Save all to disk" },
        saveAsButton { "Save preset as..." }, renameButton { "Rename..." }, typeButton { "Set type..." };
    juce::OwnedArray<juce::TextButton> chips;  // All, then one per type
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::Label status, note;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<AppSettings> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetsPage)
};

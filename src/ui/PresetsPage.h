// The Presets page: the synth's stored programs by bank, read one at a time (Fetch), kept in a cache on disk
// and shown at start without asking the synth; Load sends a program change; programs save as .syx files.
// Nothing here writes into the synth's memory (Save in the top bar does, see SavePanel).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "core/Presets.h"
#include "ui/AppSettings.h"

#include <memory>
#include <optional>
#include <vector>

class PresetsPage : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    explicit PresetsPage (Trigon6Processor&);
    ~PresetsPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The selected row when exactly one is selected: the default slot for Save.
    std::optional<std::pair<int, int>> selectedSlot() const;

private:
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void timerCallback() override;

    int currentBank() const;
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
    t6::FetchQueue queue;
    uint32_t programSeen = 0, pcSeen = 0;
    bool wasBusy = false;

    juce::ComboBox bankBox;
    juce::TableListBox table;
    juce::TextButton loadButton { "Load" }, rereadButton { "Re-read" }, fetchBankButton { "Fetch this bank" },
        fetchAllButton { "Fetch all banks" }, cancelButton { "Cancel" }, saveAllButton { "Save all to disk" },
        saveAsButton { "Save preset as..." };
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::Label status, note;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::SharedResourcePointer<AppSettings> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetsPage)
};

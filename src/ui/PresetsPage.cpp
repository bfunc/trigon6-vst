#include "PresetsPage.h"

#include "Theme.h"

namespace
{
enum Column { kProgramColumn = 1, kNameColumn, kStatusColumn };

juce::File documentsFolder (const char* sub)
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Trigon6 Control").getChildFile (sub);
}

juce::Colour statusColour (t6::PresetStatus s)
{
    switch (s)
    {
        case t6::PresetStatus::Updated: return theme::warn;
        case t6::PresetStatus::Error:   return theme::bad;
        case t6::PresetStatus::Fetched: return theme::ok;
        case t6::PresetStatus::Cached:
        case t6::PresetStatus::Empty:   break;
    }
    return theme::dim;
}
} // namespace

PresetsPage::PresetsPage (Trigon6Processor& p)
    : proc (p), entries ((size_t) (t6::kBankCount * t6::kProgramsPerBank)), table ("Programs", this)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Trigon6 Control";
    o.filenameSuffix = ".settings";
    o.folderName = "Trigon6 Control";
    o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (o);

    for (int b = 0; b < t6::kBankCount; ++b)
        bankBox.addItem (juce::String (b < t6::kUserBanks ? "User " : "Factory ") + juce::String (t6::programLabel (b, 0)) + " - "
                             + juce::String (t6::programLabel (b, t6::kProgramsPerBank - 1)),
                         b + 1);
    bankBox.setSelectedId (1, juce::dontSendNotification);
    bankBox.onChange = [this]
    {
        table.deselectAllRows();
        table.updateContent();
        table.repaint();
        updateButtons();
    };
    addAndMakeVisible (bankBox);

    auto& header = table.getHeader();
    header.addColumn ("Program", kProgramColumn, 90, 70, 120, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Name", kNameColumn, 280, 120, 600, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Status", kStatusColumn, 110, 80, 200, juce::TableHeaderComponent::notSortable);
    table.setMultipleSelectionEnabled (true);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, theme::background);
    table.setColour (juce::ListBox::outlineColourId, theme::border);
    table.setOutlineThickness (1);
    addAndMakeVisible (table);

    loadButton.setTooltip ("Switch the synth to this program (a program change; double-click a row does the same)");
    loadButton.onClick = [this] { loadSelected(); };
    rereadButton.setTooltip ("Read the selected programs from the synth again");
    rereadButton.onClick = [this] { rereadSelected(); };
    fetchBankButton.setTooltip ("Read the 100 programs of this bank from the synth");
    fetchBankButton.onClick = [this] { fetchBank (currentBank()); };
    fetchAllButton.setTooltip ("Read (or refresh) every program of every bank from the synth");
    fetchAllButton.onClick = [this] { fetchAll(); };
    cancelButton.onClick = [this] { queue.cancel(); };
    saveAllButton.setTooltip ("Write every program read so far as .syx files, plus an index, into a folder");
    saveAllButton.onClick = [this] { saveAll(); };
    saveAsButton.setTooltip ("Write the selected program (or, with none selected, the current sound) to a .syx file");
    saveAsButton.onClick = [this] { saveAs(); };
    for (auto* b : { &loadButton, &rereadButton, &fetchBankButton, &fetchAllButton, &cancelButton, &saveAllButton, &saveAsButton })
        addAndMakeVisible (*b);

    addAndMakeVisible (progressBar);
    status.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (status);
    note.setColour (juce::Label::textColourId, theme::dim);
    note.setText ("Load switches the synth's current sound; nothing here writes into the synth's memory.",
                  juce::dontSendNotification);
    addAndMakeVisible (note);

    loadCache();
    programSeen = proc.getLink().programSeq();
    pcSeen = proc.getLink().lastProgramChange().seq;
    updateButtons();
    startTimerHz (20);
}

PresetsPage::~PresetsPage() { stopTimer(); }

int PresetsPage::currentBank() const { return juce::jlimit (0, t6::kBankCount - 1, bankBox.getSelectedId() - 1); }

t6::PresetEntry& PresetsPage::entry (int bank, int program)
{
    return entries[(size_t) (bank * t6::kProgramsPerBank + program)];
}

juce::File PresetsPage::cacheDir() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Trigon6 Control").getChildFile ("Cache");
}

void PresetsPage::loadCache()
{
    int count = 0;
    for (const auto& f : cacheDir().findChildFiles (juce::File::findFiles, false, "*.syx"))
    {
        const auto bp = t6::parseCacheFileName (f.getFileName().toStdString());
        if (! bp)
            continue;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb) || mb.getSize() < 4)
            continue;
        const auto* bytes = static_cast<const uint8_t*> (mb.getData());
        auto sp = t6::parseStoredProgram (bytes, mb.getSize(), bytes[2]);
        if (! sp)
            continue;
        entry (bp->first, bp->second) = { t6::PresetStatus::Cached, sp->name, sp->sysex };
        ++count;
    }
    status.setText (count > 0 ? juce::String (count) + " programs from the cache. Fetch reads them from the synth."
                              : juce::String ("Nothing read yet. Fetch this bank or all banks to read the programs from the synth."),
                    juce::dontSendNotification);
}

void PresetsPage::writeCache (int bank, int program)
{
    const auto& e = entry (bank, program);
    if (e.sysex.empty())
        return;
    const auto dir = cacheDir();
    dir.createDirectory();
    dir.getChildFile (t6::cacheFileName (bank, program)).replaceWithData (e.sysex.data(), e.sysex.size());
}

void PresetsPage::timerCallback()
{
    auto& link = proc.getLink();
    bool changed = false;

    std::vector<SynthLink::ProgramIn> got;
    programSeen = link.readPrograms (programSeen, got);
    for (const auto& in : got)
    {
        const auto& sp = in.program;
        if (sp.bank < 0 || sp.bank >= t6::kBankCount || sp.program < 0 || sp.program >= t6::kProgramsPerBank)
            continue;
        if (t6::storeFetched (entry (sp.bank, sp.program), sp))
            writeCache (sp.bank, sp.program);
        queue.received (sp.bank, sp.program);
        changed = true;
    }

    // A program picked on the synth: read it again quietly, so the list catches changes saved with WRITE.
    const auto pc = link.lastProgramChange();
    if (pc.seq != pcSeen)
    {
        pcSeen = pc.seq;
        if (pc.bank >= 0 && pc.bank < t6::kBankCount && pc.program >= 0 && pc.program < t6::kProgramsPerBank)
            queue.enqueue (pc.bank, pc.program, true);
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int k = 0; k < 4; ++k)
    {
        const auto step = queue.poll (now);
        if (step.kind == t6::FetchQueue::Step::Send)
            link.requestProgram (step.bank, step.program);
        else if (step.kind == t6::FetchQueue::Step::Failed)
        {
            entry (step.bank, step.program).status = t6::PresetStatus::Error;
            changed = true;
        }
        else
            break;
    }

    const bool busy = queue.busy();
    if (busy)
    {
        progress = queue.total() > 0 ? (double) queue.finished() / queue.total() : 0.0;
        if (queue.total() > 1)
            status.setText ("Reading " + juce::String (queue.finished() + 1) + " of " + juce::String (queue.total()) + "...",
                            juce::dontSendNotification);
    }
    else if (wasBusy)
    {
        progress = queue.total() > 0 ? 1.0 : 0.0;
        if (queue.total() > 1 || queue.failed() > 0)
            status.setText ("Done: " + juce::String (queue.finished() - queue.failed()) + " read"
                                + (queue.failed() > 0 ? ", " + juce::String (queue.failed()) + " without a reply (is MIDI SysEx = USB?)" : juce::String()),
                            juce::dontSendNotification);
    }
    if (busy != wasBusy)
    {
        wasBusy = busy;
        updateButtons();
    }
    if (changed)
        table.repaint();
}

int PresetsPage::getNumRows() { return t6::kProgramsPerBank; }

void PresetsPage::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::led.withAlpha (0.35f) : (row % 2 ? theme::section : theme::background));
}

void PresetsPage::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= t6::kProgramsPerBank)
        return;
    const auto& e = entry (currentBank(), row);
    juce::String text;
    g.setColour (theme::text);
    switch (column)
    {
        case kProgramColumn: text = juce::String (t6::programLabel (currentBank(), row)); g.setColour (theme::dim); break;
        case kNameColumn:    text = juce::String (e.name); break;
        case kStatusColumn:  text = t6::statusName (e.status); g.setColour (statusColour (e.status)); break;
        default: break;
    }
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void PresetsPage::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    table.selectRow (row);
    loadSelected();
}

void PresetsPage::selectedRowsChanged (int) { updateButtons(); }

void PresetsPage::loadSelected()
{
    const int row = table.getSelectedRow();
    if (row < 0)
        return;
    const int bank = currentBank();
    proc.getLink().sendProgramChange (proc.getMidiChannel(), bank, row);
    status.setText ("Loaded " + juce::String (t6::programLabel (bank, row)) + " " + juce::String (entry (bank, row).name) + " on the synth.",
                    juce::dontSendNotification);
    // Then read the new sound into the panel, once the synth has switched.
    juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<PresetsPage> (this)]
    {
        if (safe != nullptr)
            safe->proc.requestGetSound();
    });
}

void PresetsPage::rereadSelected()
{
    const auto rows = table.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        queue.enqueue (currentBank(), rows[i]);
    updateButtons();
}

void PresetsPage::fetchBank (int bank)
{
    for (int p = 0; p < t6::kProgramsPerBank; ++p)
        queue.enqueue (bank, p);
    updateButtons();
}

void PresetsPage::fetchAll()
{
    for (int b = 0; b < t6::kBankCount; ++b)
        fetchBank (b);
}

void PresetsPage::saveAll()
{
    const auto folder = documentsFolder ("Banks").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Save all programs into a folder", folder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, folder] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (dir != folder && folder.isDirectory() && folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
            folder.deleteFile();  // the default folder was made for the dialog and not used
        if (dir == juce::File())
            return;
        dir.createDirectory();
        juce::String index ("Program\tBank\tNumber\tName\tFile\n");
        int written = 0;
        for (int b = 0; b < t6::kBankCount; ++b)
            for (int p = 0; p < t6::kProgramsPerBank; ++p)
            {
                const auto& e = entry (b, p);
                if (e.sysex.empty())
                    continue;
                const auto name = juce::String (t6::presetFileName (b, p, e.name));
                // Never overwrite a file that is already there.
                auto file = dir.getChildFile (name);
                if (file.exists())
                    file = dir.getNonexistentChildFile (name.upToLastOccurrenceOf (".syx", false, false), ".syx", true);
                if (file.replaceWithData (e.sysex.data(), e.sysex.size()))
                {
                    ++written;
                    index << juce::String (t6::programLabel (b, p)) << "\t" << b << "\t" << p << "\t" << juce::String (e.name) << "\t"
                          << file.getFileName() << "\n";
                }
            }
        dir.getNonexistentChildFile ("index", ".txt", true).replaceWithText (index);
        status.setText (written > 0 ? "Saved " + juce::String (written) + " programs to " + dir.getFullPathName()
                                    : juce::String ("Nothing to save yet: fetch the programs first."),
                        juce::dontSendNotification);
    });
}

void PresetsPage::saveAs()
{
    std::vector<uint8_t> sysex;
    juce::String name;
    const int row = table.getSelectedRow();
    if (row >= 0 && ! entry (currentBank(), row).sysex.empty())
    {
        sysex = entry (currentBank(), row).sysex;
        name = entry (currentBank(), row).name;
    }
    else
    {
        sysex = proc.getLink().editBufferSysex();
        name = "Current sound";
    }
    if (sysex.empty())
    {
        status.setText (row >= 0 ? "This program is not read yet: Re-read it first."
                                 : "No sound read yet: press Get sound (top bar) or select a program.",
                        juce::dontSendNotification);
        return;
    }

    juce::File folder (settings->getValue ("lastPresetFolder"));
    if (! folder.isDirectory())
    {
        folder = documentsFolder ("Presets");
        folder.createDirectory();
    }
    const auto start = folder.getChildFile (juce::String (t6::sanitizeFileName (name.toStdString())) + ".syx");
    chooser = std::make_unique<juce::FileChooser> ("Save preset as", start, "*.syx");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, sysex] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        if (! file.hasFileExtension ("syx"))
            file = file.withFileExtension ("syx");
        if (file.replaceWithData (sysex.data(), sysex.size()))
        {
            settings->setValue ("lastPresetFolder", file.getParentDirectory().getFullPathName());
            settings->saveIfNeeded();
            status.setText ("Saved " + file.getFullPathName(), juce::dontSendNotification);
        }
        else
            status.setText ("Could not write " + file.getFullPathName(), juce::dontSendNotification);
    });
}

void PresetsPage::updateButtons()
{
    const bool busy = queue.busy();
    const bool one = table.getNumSelectedRows() == 1;
    loadButton.setEnabled (one);
    rereadButton.setEnabled (table.getNumSelectedRows() > 0);
    fetchBankButton.setEnabled (! busy);
    fetchAllButton.setEnabled (! busy);
    cancelButton.setEnabled (busy);
}

void PresetsPage::paint (juce::Graphics& g) { g.fillAll (theme::background); }

void PresetsPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (30);
    bankBox.setBounds (top.removeFromLeft (260));
    top.removeFromLeft (10);
    for (auto* b : { &loadButton, &rereadButton })
    {
        b->setBounds (top.removeFromLeft (90));
        top.removeFromLeft (6);
    }
    for (auto* b : { &saveAsButton, &saveAllButton })
    {
        b->setBounds (top.removeFromRight (140));
        top.removeFromRight (6);
    }

    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (24);
    note.setBounds (bottom);
    r.removeFromBottom (6);
    auto fetchRow = r.removeFromBottom (28);
    for (auto* b : { &fetchBankButton, &fetchAllButton, &cancelButton })
    {
        b->setBounds (fetchRow.removeFromLeft (130));
        fetchRow.removeFromLeft (6);
    }
    progressBar.setBounds (fetchRow.removeFromLeft (220).reduced (0, 3));
    fetchRow.removeFromLeft (10);
    status.setBounds (fetchRow);
    r.removeFromBottom (8);
    table.setBounds (r);
}

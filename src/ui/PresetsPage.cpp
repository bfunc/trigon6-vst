#include "PresetsPage.h"

#include "PresetTypes.h"
#include "Theme.h"

namespace
{
enum Column { kProgramColumn = 1, kNameColumn, kTypeColumn, kStatusColumn };

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
    for (int b = 0; b < t6::kBankCount; ++b)
        bankBox.addItem (juce::String (b < t6::kUserBanks ? "User " : "Factory ") + juce::String (t6::programLabel (b, 0)) + " - "
                             + juce::String (t6::programLabel (b, t6::kProgramsPerBank - 1)),
                         b + 1);
    bankBox.setSelectedId (1, juce::dontSendNotification);
    bankBox.onChange = [this]
    {
        typeFilter = {};
        table.deselectAllRows();
        rebuildRows();
    };
    addAndMakeVisible (bankBox);

    auto& header = table.getHeader();
    header.addColumn ("Program", kProgramColumn, 90, 70, 120, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Name", kNameColumn, 280, 120, 600, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Type", kTypeColumn, 90, 60, 160, juce::TableHeaderComponent::notSortable);
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
    renameButton.setTooltip ("Give the selected program a new name on the synth (user banks; the old one is backed up first)");
    renameButton.onClick = [this] { renameSelected(); };
    typeButton.setTooltip ("Set the type of the selected programs (kept on this PC; right-click a row does the same)");
    typeButton.onClick = [this] { showTypeMenu(); };
    for (auto* b : { &loadButton, &rereadButton, &renameButton, &typeButton, &fetchBankButton, &fetchAllButton, &cancelButton,
                     &saveAllButton, &saveAsButton })
        addAndMakeVisible (*b);

    // Type filter: All, then each type. Clicking a program's type does the same.
    juce::StringArray chipNames { "All" };
    for (const auto& t : t6::programTypes())
        chipNames.add (juce::String (t));
    for (const auto& n : chipNames)
    {
        auto* c = chips.add (new juce::TextButton (n));
        c->setTooltip (n == "All" ? juce::String ("Show the selected bank") : "Show every " + n + " program in all banks");
        c->onClick = [this, n] { setFilter (n == "All" ? juce::String() : n); };
        addAndMakeVisible (c);
    }
    settings->file->addChangeListener (this);

    addAndMakeVisible (progressBar);
    status.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (status);
    note.setColour (juce::Label::textColourId, theme::dim);
    note.setText ("Load switches the synth's current sound. Save (top bar) writes the current sound into a user slot or a file.",
                  juce::dontSendNotification);
    addAndMakeVisible (note);

    loadCache();
    rebuildRows();
    programSeen = proc.getLink().programSeq();
    pcSeen = proc.getLink().lastProgramChange().seq;
    updateButtons();
    startTimerHz (20);
}

PresetsPage::~PresetsPage()
{
    settings->file->removeChangeListener (this);
    stopTimer();
}

std::optional<std::pair<int, int>> PresetsPage::selectedSlot() const
{
    if (table.getNumSelectedRows() != 1 || table.getSelectedRow() >= (int) rows.size())
        return std::nullopt;
    return slotOfRow (table.getSelectedRow());
}

std::pair<int, int> PresetsPage::slotOfRow (int row) const
{
    const int i = row >= 0 && row < (int) rows.size() ? rows[(size_t) row] : 0;
    return { i / t6::kProgramsPerBank, i % t6::kProgramsPerBank };
}

juce::String PresetsPage::typeOf (int bank, int program)
{
    return presetTypes::shown (*settings->file, bank, program, entry (bank, program).name);
}

void PresetsPage::rebuildRows()
{
    std::vector<int> now;
    if (typeFilter.isEmpty())
        for (int p = 0; p < t6::kProgramsPerBank; ++p)
            now.push_back (currentBank() * t6::kProgramsPerBank + p);
    else
        for (int b = 0; b < t6::kBankCount; ++b)
            for (int p = 0; p < t6::kProgramsPerBank; ++p)
                if (! entry (b, p).sysex.empty() && typeOf (b, p) == typeFilter)
                    now.push_back (b * t6::kProgramsPerBank + p);
    if (now != rows)
    {
        rows = std::move (now);
        table.deselectAllRows();
        table.updateContent();
    }
    for (auto* c : chips)
        c->setToggleState (c->getButtonText() == (typeFilter.isEmpty() ? juce::String ("All") : typeFilter), juce::dontSendNotification);
    if (typeFilter.isNotEmpty())
        status.setText (typeFilter + ": " + juce::String ((int) rows.size()) + " programs in all banks. All shows the bank again.",
                        juce::dontSendNotification);
    table.repaint();
    updateButtons();
}

void PresetsPage::setFilter (const juce::String& type)
{
    typeFilter = type;
    if (type.isEmpty())
        status.setText ({}, juce::dontSendNotification);
    rebuildRows();
}

void PresetsPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildRows();  // a type was set
}

void PresetsPage::showTypeMenu()
{
    if (table.getNumSelectedRows() == 0)
        return;
    juce::PopupMenu m;
    const bool one = table.getNumSelectedRows() == 1;
    if (one)
    {
        const auto slot = slotOfRow (table.getSelectedRow());
        m.addItem (1, "Rename...", t6::isWritableSlot (slot.first, slot.second) && ! entry (slot.first, slot.second).sysex.empty());
        m.addSeparator();
    }
    m.addSectionHeader (one ? juce::String ("Type") : "Type of the " + juce::String (table.getNumSelectedRows()) + " programs");
    m.addItem (2, "From the name");
    const auto& types = t6::programTypes();
    for (size_t i = 0; i < types.size(); ++i)
        m.addItem (10 + (int) i, juce::String (types[i]));
    m.showMenuAsync (juce::PopupMenu::Options(), [this] (int id)
    {
        if (id == 1)
            renameSelected();
        else if (id == 2)
            setTypeForSelected ({});
        else if (id >= 10 && id < 10 + (int) t6::programTypes().size())
            setTypeForSelected (juce::String (t6::programTypes()[(size_t) (id - 10)]));
    });
}

void PresetsPage::setTypeForSelected (const juce::String& type)
{
    const auto selected = table.getSelectedRows();
    std::vector<std::pair<int, int>> slots;
    for (int i = 0; i < selected.size(); ++i)
        slots.push_back (slotOfRow (selected[i]));
    for (const auto& slot : slots)
        if (! entry (slot.first, slot.second).sysex.empty())
            presetTypes::set (*settings->file, slot.first, slot.second, entry (slot.first, slot.second).name, type);
}

void PresetsPage::renameSelected()
{
    const auto slot = selectedSlot();
    if (! slot || ! onRename)
        return;
    if (! t6::isWritableSlot (slot->first, slot->second))
    {
        status.setText ("Factory programs can't be renamed. Save the sound into a user slot instead.", juce::dontSendNotification);
        return;
    }
    onRename (slot->first, slot->second);
}

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
    {
        if (typeFilter.isNotEmpty())
            rebuildRows();
        table.repaint();
    }
}

int PresetsPage::getNumRows() { return (int) rows.size(); }

void PresetsPage::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::led.withAlpha (0.35f) : (row % 2 ? theme::section : theme::background));
}

void PresetsPage::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    const auto slot = slotOfRow (row);
    const int bank = slot.first, program = slot.second;
    const auto& e = entry (bank, program);
    juce::String text;
    g.setColour (theme::text);
    switch (column)
    {
        case kProgramColumn: text = juce::String (t6::programLabel (bank, program)); g.setColour (theme::dim); break;
        case kNameColumn:    text = juce::String (e.name); break;
        case kTypeColumn:
            if (! e.sysex.empty())
            {
                // set by the user: bright; from the name: dim
                const bool own = presetTypes::user (*settings->file, bank, program, e.name).isNotEmpty();
                text = typeOf (bank, program);
                g.setColour (own ? theme::text : theme::dim);
            }
            break;
        case kStatusColumn:  text = t6::statusName (e.status); g.setColour (statusColour (e.status)); break;
        default: break;
    }
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (text, 6, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void PresetsPage::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (! table.isRowSelected (row))
            table.selectRow (row);
        showTypeMenu();
        return;
    }
    // A click on a program's type shows that type in all banks (again: back to the bank).
    if (column == kTypeColumn && row >= 0 && row < (int) rows.size())
    {
        const auto slot = slotOfRow (row);
        if (! entry (slot.first, slot.second).sysex.empty())
        {
            const auto t = typeOf (slot.first, slot.second);
            setFilter (t == typeFilter ? juce::String() : t);
        }
    }
}

void PresetsPage::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    table.selectRow (row);
    loadSelected();
}

void PresetsPage::selectedRowsChanged (int) { updateButtons(); }

void PresetsPage::loadSelected()
{
    const auto slot = selectedSlot();
    if (! slot)
        return;
    const int bank = slot->first, program = slot->second;
    // The processor reads the new sound into the panel once the synth has switched; the slot is read again
    // too, so the list (and the top bar) catch a change saved with WRITE on the synth.
    proc.loadProgram (bank, program);
    queue.enqueue (bank, program, true);
    status.setText ("Loaded " + juce::String (t6::programLabel (bank, program)) + " " + juce::String (entry (bank, program).name) + " on the synth.",
                    juce::dontSendNotification);
}

std::vector<uint8_t> PresetsPage::storedData (int bank, int program)
{
    if (bank < 0 || bank >= t6::kBankCount || program < 0 || program >= t6::kProgramsPerBank)
        return {};
    const auto& e = entry (bank, program);
    if (e.sysex.size() < 4)
        return {};
    const auto sp = t6::parseStoredProgram (e.sysex.data(), e.sysex.size(), e.sysex[2]);
    return sp ? sp->data : std::vector<uint8_t> {};
}

void PresetsPage::rereadSelected()
{
    const auto selected = table.getSelectedRows();
    for (int i = 0; i < selected.size(); ++i)
    {
        const auto slot = slotOfRow (selected[i]);
        queue.enqueue (slot.first, slot.second);
    }
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
        juce::String index ("Program\tBank\tNumber\tName\tType\tFile\n");
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
                    index << juce::String (t6::programLabel (b, p)) << "\t" << b << "\t" << p << "\t" << juce::String (e.name) << "\t" << typeOf (b, p) << "\t"
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
    const auto slot = selectedSlot();
    const int row = slot ? 0 : -1;
    if (slot && ! entry (slot->first, slot->second).sysex.empty())
    {
        sysex = entry (slot->first, slot->second).sysex;
        name = entry (slot->first, slot->second).name;
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

    juce::File folder (settings->file->getValue ("lastPresetFolder"));
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
            settings->file->setValue ("lastPresetFolder", file.getParentDirectory().getFullPathName());
            settings->file->saveIfNeeded();
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
    const auto slot = selectedSlot();
    renameButton.setEnabled (slot && t6::isWritableSlot (slot->first, slot->second) && ! entry (slot->first, slot->second).sysex.empty());
    typeButton.setEnabled (table.getNumSelectedRows() > 0);
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
    for (auto* b : { &loadButton, &rereadButton, &renameButton, &typeButton })
    {
        b->setBounds (top.removeFromLeft (96));
        top.removeFromLeft (6);
    }
    for (auto* b : { &saveAsButton, &saveAllButton })
    {
        b->setBounds (top.removeFromRight (140));
        top.removeFromRight (6);
    }

    r.removeFromTop (8);
    auto chipRow = r.removeFromTop (26);
    const int chipW = juce::jmin (90, chipRow.getWidth() / juce::jmax (1, chips.size()));
    for (auto* c : chips)
        c->setBounds (chipRow.removeFromLeft (chipW).reduced (2, 0));
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

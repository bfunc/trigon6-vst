#include "SavePanel.h"

#include "Theme.h"
#include "core/Presets.h"
#include "core/Sysex.h"

namespace
{
constexpr double kReplyTimeoutMs = 1500.0;
constexpr double kWriteSettleMs = 700.0;  // time the synth gets to store the slot before it is read back

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

juce::String slotText (int bank, int program) { return juce::String (t6::programLabel (bank, program)); }

juce::String nameChars()
{
    juce::String s;
    for (char c = 32; c <= 126; ++c)
        s += juce::String::charToString ((juce::juce_wchar) c);
    return s;
}

bool writeFile (const juce::File& f, const std::vector<uint8_t>& bytes)
{
    f.getParentDirectory().createDirectory();
    return ! bytes.empty() && f.replaceWithData (bytes.data(), bytes.size());
}
} // namespace

SavePanel::SavePanel (Trigon6Processor& p, int defaultBank, int defaultProgram) : proc (p)
{
    setWantsKeyboardFocus (true);

    auto setup = [this] (juce::Label& l, const juce::String& text, juce::Colour c, float size = 14.0f)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, c);
        l.setFont (juce::FontOptions (size));
        addAndMakeVisible (l);
    };
    setup (title, "SAVE CURRENT SOUND", theme::text, 18.0f);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    setup (nameLabel, "Name", theme::dim);
    setup (nameCount, "", theme::dim, 12.5f);
    setup (targetLabel, "Save to", theme::dim);
    setup (slotLabel, "Slot", theme::dim);
    setup (slotInfo, "", theme::text);
    setup (slotWarn, "", theme::warn, 13.0f);
    setup (folderLabel, "Folder", theme::dim);
    setup (folderPath, "", theme::text, 13.0f);
    folderPath.setMinimumHorizontalScale (0.6f);
    setup (fileInfo, "", theme::dim, 13.0f);
    setup (status, "Reading the current sound from the synth...", theme::dim, 13.5f);

    nameEdit.setInputRestrictions ((int) t6::kNameLength, nameChars());
    nameEdit.setFont (juce::FontOptions (16.0f));
    nameEdit.setColour (juce::TextEditor::backgroundColourId, theme::background);
    nameEdit.setColour (juce::TextEditor::textColourId, theme::text);
    nameEdit.setColour (juce::TextEditor::outlineColourId, theme::border);
    nameEdit.setTextToShowWhenEmpty ("name of the sound", theme::dim);
    nameEdit.onTextChange = [this] { update(); };
    nameEdit.onReturnKey = [this] { if (saveButton.isEnabled()) startSave(); };
    nameEdit.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (nameEdit);

    for (auto* b : { &synthButton, &pcButton, &bothButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (4711);
        addAndMakeVisible (*b);
    }
    synthButton.setTooltip ("Write into a user slot of the synth's memory");
    pcButton.setTooltip ("Write a .syx file on this computer; the synth's memory stays as it is");
    bothButton.setTooltip ("Write into the slot and a .syx file");
    synthButton.onClick = [this] { setTarget (Target::Synth, true); };
    pcButton.onClick = [this] { setTarget (Target::Pc, true); };
    bothButton.onClick = [this] { setTarget (Target::Both, true); };

    // User banks only: factory sounds are never overwritten.
    for (int b = 0; b < t6::kUserBanks; ++b)
        bankBox.addItem ("User " + slotText (b, 0) + " - " + slotText (b, t6::kProgramsPerBank - 1), b + 1);
    for (int i = 0; i < t6::kProgramsPerBank; ++i)
        programBox.addItem (juce::String (i).paddedLeft ('0', 2), i + 1);
    const bool writable = t6::isWritableSlot (defaultBank, defaultProgram);
    bankBox.setSelectedId ((writable ? defaultBank : 0) + 1, juce::dontSendNotification);
    programBox.setSelectedId ((writable ? defaultProgram : 0) + 1, juce::dontSendNotification);
    bankBox.onChange = [this] { readSlot(); };
    programBox.onChange = [this] { readSlot(); };
    addAndMakeVisible (bankBox);
    addAndMakeVisible (programBox);

    changeFolder.onClick = [this] { chooseFolder(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    saveButton.onClick = [this] { startSave(); };
    for (auto* b : { &changeFolder, &cancelButton, &saveButton })
        addAndMakeVisible (*b);

    const auto stored = settings->file->getValue ("saveTarget", "synth");
    setTarget (stored == "pc" ? Target::Pc : stored == "both" ? Target::Both : Target::Synth, false);

    // The current sound (for its name) and what the slot holds now.
    auto& link = proc.getLink();
    programSeen = link.programSeq();
    soundSeqAtRequest = link.editBufferSeq();
    stage = Stage::ReadSound;
    stageMs = nowMs();
    tries = 0;
    link.requestEditBuffer();
    readSlot();
    update();
    startTimerHz (20);
}

SavePanel::~SavePanel() { stopTimer(); }

int SavePanel::bank() const { return juce::jlimit (0, t6::kUserBanks - 1, bankBox.getSelectedId() - 1); }
int SavePanel::program() const { return juce::jlimit (0, t6::kProgramsPerBank - 1, programBox.getSelectedId() - 1); }

void SavePanel::setTarget (Target t, bool store)
{
    target = t;
    synthButton.setToggleState (t == Target::Synth, juce::dontSendNotification);
    pcButton.setToggleState (t == Target::Pc, juce::dontSendNotification);
    bothButton.setToggleState (t == Target::Both, juce::dontSendNotification);
    if (store)
    {
        settings->file->setValue ("saveTarget", t == Target::Pc ? "pc" : t == Target::Both ? "both" : "synth");
        settings->file->saveIfNeeded();
    }
    update();
}

void SavePanel::readSlot()
{
    slotState = SlotState::Reading;
    slotName = {};
    slotTries = 0;
    slotAskedMs = nowMs();
    proc.getLink().requestProgram (bank(), program());
    update();
}

juce::File SavePanel::folder() const
{
    juce::File f (settings->file->getValue ("lastPresetFolder"));
    return f.isDirectory() ? f : documentsFolder ("Presets");
}

juce::File SavePanel::pcFile() const
{
    const auto name = nameEdit.getText().trim().toStdString();
    // In a slot's file name the slot comes first ("U023 Bright Bass.syx"), as Presets > Save names them.
    const juce::String fileName = toSynth() ? juce::String (t6::presetFileName (bank(), program(), name))
                                            : juce::String (t6::sanitizeFileName (name)) + ".syx";
    const auto dir = folder();
    const auto f = dir.getChildFile (fileName);
    return f.exists() ? dir.getNonexistentChildFile (fileName.upToLastOccurrenceOf (".syx", false, false), ".syx", true) : f;
}

void SavePanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Folder for saved sounds", folder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (dir == juce::File())
            return;
        settings->file->setValue ("lastPresetFolder", dir.getFullPathName());
        settings->file->saveIfNeeded();
        update();
    });
}

void SavePanel::startSave()
{
    if (nameEdit.getText().trim().isEmpty())
    {
        finish ("Give the sound a name first.", theme::warn);
        return;
    }
    // Read the sound again: it may have changed since the panel opened.
    auto& link = proc.getLink();
    soundSeqAtRequest = link.editBufferSeq();
    stage = Stage::ReadSound;
    stageMs = nowMs();
    tries = 0;
    saving = true;
    link.requestEditBuffer();
    status.setText ("Reading the current sound...", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, theme::dim);
    update();
}

void SavePanel::soundArrived()
{
    data = proc.getLink().editBuffer();
    if (! saving)
    {
        // The read on opening: offer the sound's own name.
        if (nameEdit.getText().isEmpty())
        {
            nameEdit.setText (juce::String (t6::programName (data)), juce::dontSendNotification);
            nameEdit.selectAll();
        }
        finish ({}, theme::dim);
        return;
    }
    if (data.size() != t6::kProgramBytes)
    {
        finish ("The synth's sound did not arrive complete. Nothing saved.", theme::bad);
        return;
    }
    t6::setProgramName (data, nameEdit.getText().trim().toStdString());

    juce::String saved;
    if (toPc())
    {
        const auto file = pcFile();
        const auto bytes = toSynth() ? t6::programDump (bank(), program(), data) : t6::editBufferDump (data);
        if (! writeFile (file, bytes))
        {
            finish ("Could not write " + file.getFullPathName() + ". Nothing saved.", theme::bad);
            return;
        }
        saved = file.getFullPathName();
        if (! toSynth())
        {
            finish ("Saved to " + saved, theme::ok);
            return;
        }
    }
    // To the synth: first read what the slot holds now, for the backup.
    stage = Stage::ReadSlot;
    stageMs = nowMs();
    tries = 0;
    proc.getLink().requestProgram (bank(), program());
    status.setText ((saved.isNotEmpty() ? "Saved to " + saved + ". " : juce::String()) + "Backing up " + slotText (bank(), program()) + "...",
                    juce::dontSendNotification);
    update();
}

void SavePanel::slotArrived (const t6::StoredProgram& old)
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    const auto backup = documentsFolder ("Backups").getChildFile (stamp + " " + juce::String (t6::presetFileName (bank(), program(), old.name)));
    if (! writeFile (backup, old.sysex))
    {
        finish ("Could not write the backup " + backup.getFullPathName() + ". The slot was not written.", theme::bad);
        return;
    }
    proc.getLink().writeProgram (bank(), program(), data);
    stage = Stage::Write;
    stageMs = nowMs();
    status.setText ("Writing " + slotText (bank(), program()) + "...", juce::dontSendNotification);
    update();
}

void SavePanel::verifyArrived (const t6::StoredProgram& back)
{
    const int diff = t6::countDifferences (back.data, data);
    const auto where = slotText (bank(), program()) + " " + juce::String (back.name);
    if (diff == 0)
        finish ("Saved to " + where + " on the synth (read back: the same)." + (toPc() ? " The file is saved too." : ""), theme::ok);
    else
        finish ("Written to " + slotText (bank(), program()) + ", but reading it back gave " + juce::String (diff)
                    + " different bytes. The old sound is in Backups.", theme::warn);
    slotState = SlotState::Known;
    slotName = juce::String (back.name);
}

void SavePanel::finish (const juce::String& text, juce::Colour c)
{
    stage = Stage::Idle;
    saving = false;
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, c);
    update();
}

void SavePanel::timerCallback()
{
    auto& link = proc.getLink();
    const double now = nowMs();

    std::vector<SynthLink::ProgramIn> got;
    programSeen = link.readPrograms (programSeen, got);
    for (const auto& in : got)
    {
        const auto& sp = in.program;
        if (sp.bank != bank() || sp.program != program())
            continue;
        if (stage == Stage::ReadSlot)
            slotArrived (sp);
        else if (stage == Stage::Verify)
            verifyArrived (sp);
        else if (stage != Stage::Write)
        {
            slotState = SlotState::Known;
            slotName = juce::String (sp.name);
            update();
        }
    }

    switch (stage)
    {
        case Stage::ReadSound:
            if (link.editBufferSeq() != soundSeqAtRequest)
                soundArrived();
            else if (now - stageMs > kReplyTimeoutMs)
            {
                if (++tries <= 1)
                {
                    stageMs = now;
                    link.requestEditBuffer();
                }
                else
                    finish (saving ? "The synth did not send its sound (is MIDI SysEx = USB?). Nothing saved."
                                   : "The synth did not send its sound yet (is MIDI SysEx = USB?).",
                            saving ? theme::bad : theme::warn);
            }
            break;
        case Stage::ReadSlot:
        case Stage::Verify:
            if (now - stageMs > kReplyTimeoutMs)
            {
                if (++tries <= 1)
                {
                    stageMs = now;
                    link.requestProgram (bank(), program());
                }
                else if (stage == Stage::ReadSlot)
                    finish (slotText (bank(), program()) + " did not answer, so it could not be backed up. The slot was not written.", theme::bad);
                else
                    finish ("Written to " + slotText (bank(), program()) + ", but it did not answer the read-back. Check it on the synth.", theme::warn);
            }
            break;
        case Stage::Write:
            if (now - stageMs > kWriteSettleMs)
            {
                stage = Stage::Verify;
                stageMs = now;
                tries = 0;
                link.requestProgram (bank(), program());
                status.setText ("Reading " + slotText (bank(), program()) + " back...", juce::dontSendNotification);
            }
            break;
        case Stage::Idle:
            break;
    }

    if (slotState == SlotState::Reading && stage == Stage::Idle && now - slotAskedMs > kReplyTimeoutMs)
    {
        if (++slotTries <= 1)
        {
            slotAskedMs = now;
            link.requestProgram (bank(), program());
        }
        else
        {
            slotState = SlotState::NoReply;
            update();
        }
    }
}

void SavePanel::update()
{
    const bool busy = stage != Stage::Idle;
    const auto name = nameEdit.getText().trim();
    nameCount.setText (juce::String (nameEdit.getText().length()) + " / " + juce::String ((int) t6::kNameLength), juce::dontSendNotification);

    const auto slot = slotText (bank(), program());
    switch (slotState)
    {
        case SlotState::Reading: slotInfo.setText ("Reading " + slot + "...", juce::dontSendNotification); break;
        case SlotState::Known:
            slotInfo.setText ("Now in " + slot + ": " + (slotName.isNotEmpty() ? slotName : juce::String ("(no name)")), juce::dontSendNotification);
            break;
        case SlotState::NoReply:
            slotInfo.setText (slot + " did not answer (is MIDI SysEx = USB?)", juce::dontSendNotification);
            break;
    }
    slotInfo.setColour (juce::Label::textColourId, slotState == SlotState::NoReply ? theme::bad : theme::text);
    slotWarn.setText (slotState == SlotState::Known
                          ? "Saving replaces \"" + (slotName.isNotEmpty() ? slotName : slot) + "\". It goes to Documents\\Trigon6 Control\\Backups first."
                          : juce::String(),
                      juce::dontSendNotification);

    folderPath.setText (folder().getFullPathName(), juce::dontSendNotification);
    fileInfo.setText (name.isNotEmpty() ? "File: " + pcFile().getFileName() : juce::String ("File: (needs a name)"), juce::dontSendNotification);

    const bool synthOn = toSynth(), pcOn = toPc();
    for (auto* c : std::initializer_list<juce::Component*> { &slotLabel, &bankBox, &programBox, &slotInfo, &slotWarn })
        c->setAlpha (synthOn ? 1.0f : 0.35f);
    for (auto* c : std::initializer_list<juce::Component*> { &folderLabel, &folderPath, &fileInfo, &changeFolder })
        c->setAlpha (pcOn ? 1.0f : 0.35f);
    bankBox.setEnabled (synthOn && ! busy);
    programBox.setEnabled (synthOn && ! busy);
    changeFolder.setEnabled (pcOn && ! busy);
    nameEdit.setReadOnly (busy);
    for (auto* b : { &synthButton, &pcButton, &bothButton })
        b->setEnabled (! busy);

    saveButton.setButtonText (target == Target::Synth ? "Save to synth" : target == Target::Pc ? "Save to PC" : "Save to both");
    // Writing a slot needs its old contents read first (for the backup).
    saveButton.setEnabled (! busy && name.isNotEmpty() && (! synthOn || slotState == SlotState::Known));
    cancelButton.setButtonText (status.getText().startsWith ("Saved") ? "Close" : "Cancel");
}

bool SavePanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

void SavePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.65f));
    g.setColour (theme::section);
    g.fillRoundedRectangle (card.toFloat(), 8.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (card.toFloat(), 8.0f, 1.0f);

    // section titles and rules
    auto r = card.reduced (24);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    auto rule = [&] (int y, const juce::String& text)
    {
        g.setColour (theme::border);
        g.fillRect (r.getX(), y, r.getWidth(), 1);
        g.setColour (theme::dim);
        g.drawText (text, r.getX(), y + 4, 200, 16, juce::Justification::centredLeft);
    };
    rule (slotLabel.getY() - 26, "ON THE SYNTH");
    rule (folderLabel.getY() - 26, "ON THE PC");
}

void SavePanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (640, getWidth() - 40), juce::jmin (470, getHeight() - 40));
    auto r = card.reduced (24);
    const int labelW = 70;

    title.setBounds (r.removeFromTop (28));
    r.removeFromTop (14);

    auto row = r.removeFromTop (32);
    nameLabel.setBounds (row.removeFromLeft (labelW));
    nameCount.setBounds (row.removeFromRight (60));
    nameEdit.setBounds (row.reduced (0, 1));
    r.removeFromTop (12);

    row = r.removeFromTop (30);
    targetLabel.setBounds (row.removeFromLeft (labelW));
    for (auto* b : { &synthButton, &pcButton, &bothButton })
    {
        b->setBounds (row.removeFromLeft (120));
        row.removeFromLeft (6);
    }
    r.removeFromTop (34);

    row = r.removeFromTop (28);
    slotLabel.setBounds (row.removeFromLeft (labelW));
    bankBox.setBounds (row.removeFromLeft (200));
    row.removeFromLeft (6);
    programBox.setBounds (row.removeFromLeft (70));
    r.removeFromTop (4);
    slotInfo.setBounds (r.removeFromTop (22).withTrimmedLeft (labelW));
    slotWarn.setBounds (r.removeFromTop (20).withTrimmedLeft (labelW));
    r.removeFromTop (34);

    row = r.removeFromTop (28);
    folderLabel.setBounds (row.removeFromLeft (labelW));
    changeFolder.setBounds (row.removeFromRight (96));
    row.removeFromRight (6);
    folderPath.setBounds (row);
    fileInfo.setBounds (r.removeFromTop (22).withTrimmedLeft (labelW));

    auto buttons = r.removeFromBottom (32);
    saveButton.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (96));
    r.removeFromBottom (8);
    status.setBounds (r);
    status.setJustificationType (juce::Justification::topLeft);
}

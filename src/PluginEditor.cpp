#include "PluginEditor.h"

#include "core/Connection.h"
#include "core/Globals.h"
#include "core/ParamTable.h"
#include "core/PresetDiff.h"
#include "core/SoundSync.h"
#include "ui/ConnectionPage.h"
#include "ui/HardwareLook.h"
#include "ui/PanelView.h"
#include "ui/PresetsPage.h"
#include "ui/SavePanel.h"
#include "ui/Theme.h"

#include <cstring>
#include <functional>
#include <set>
#include <tuple>

namespace
{
namespace colours = theme;

constexpr int kWood = 18;      // the wooden end cheeks, left and right
constexpr int kTopBar = 56;

} // namespace

class Trigon6Editor::Tabs : public juce::TabbedComponent
{
public:
    Tabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
    void currentTabChanged (int, const juce::String&) override
    {
        if (! programmatic)
            userPicked = true;
    }
    void show (int index)
    {
        programmatic = true;
        setCurrentTabIndex (index);
        programmatic = false;
    }
    bool userPicked = false;

private:
    bool programmatic = false;
};

Trigon6Editor::Trigon6Editor (Trigon6Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);

    channelLabel.setText ("MIDI CH", juce::dontSendNotification);
    channelLabel.setFont (hw::labelFont (12.0f));
    channelLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb8b8bc));
    channelLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (channelLabel);
    for (int ch = 1; ch <= 16; ++ch)
        channel.addItem (juce::String (ch), ch);
    channel.setSelectedId (proc.getMidiChannel(), juce::dontSendNotification);
    channel.onChange = [this] { proc.setMidiChannel (channel.getSelectedId()); };
    addAndMakeVisible (channel);

    sendAll.setTooltip ("Send every parameter to the synth (overwrites its edit buffer)");
    sendAll.onClick = [this] { proc.requestSendAll(); };
    addAndMakeVisible (sendAll);

    getSound.setTooltip ("Read the synth's current sound into the plugin (sends nothing that changes the sound)");
    getSound.onClick = [this] { proc.requestGetSound(); };
    addAndMakeVisible (getSound);
    resetAll.setTooltip ("Put every parameter back to the preset (the sound read from the synth or last saved) and send it to the synth");
    resetAll.onClick = [this] { proc.resetAllToPreset(); };
    resetAll.setEnabled (false);
    addAndMakeVisible (resetAll);
    save.setTooltip ("Save the current sound into a user slot on the synth, a .syx file on the PC, or both");
    save.onClick = [this] { openSave(); };
    addAndMakeVisible (save);
    soundStatus.setColour (juce::Label::textColourId, hw::col::value);
    soundStatus.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (soundStatus);

    status.setColour (juce::Label::textColourId, juce::Colour (0xffb8b8bc));
    status.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (status);

    sync.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    sync.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (sync);

    // Connection first; the editor opens on it unless the synth is fully connected.
    tabs = std::make_unique<Tabs>();
    tabs->setTabBarDepth (38);
    tabs->setOutline (0);
    tabs->setIndent (10);
    auto* connection = new ConnectionPage (proc);
    tabs->addTab ("Connection", colours::background, connection, true);
    tabs->addTab ("Panel", hw::col::panel, new PanelView (proc), true);
    presetsPage = new PresetsPage (proc);
    presetsPage->onRename = [this] (int bank, int program) { openPanel (bank, program, true); };
    tabs->addTab ("Presets", colours::background, presetsPage, true);
    tabs->show (connection->getReport().allOk ? 1 : 0);
    // Just after opening (the standalone app checks the synth on start), move to the panel once all is green.
    const auto openedMs = juce::Time::getMillisecondCounter();
    connection->onReportChanged = [this, connection, openedMs]
    {
        if (connection->getReport().allOk && ! tabs->userPicked && juce::Time::getMillisecondCounter() - openedMs < 8000)
            tabs->show (1);
    };
    addAndMakeVisible (*tabs);

    setResizable (true, true);
    setResizeLimits (1100, 640, 2600, 1500);
    setSize (kWood * 2 + 20 + PanelView::designSize().x, kTopBar + 38 + 20 + PanelView::designSize().y);  // the Panel tab at scale 1
    startTimerHz (4);
}

Trigon6Editor::~Trigon6Editor()
{
    savePanel.reset();
    tabs.reset();
    setLookAndFeel (nullptr);
}

void Trigon6Editor::paint (juce::Graphics& g)
{
    g.fillAll (hw::col::strip);
    const auto r = getLocalBounds().toFloat();
    hw::drawWood (g, r.withWidth ((float) kWood), true);
    hw::drawWood (g, r.withTrimmedLeft (r.getWidth() - (float) kWood), false);

    // the title, two-tone like a panel print
    auto bar = getLocalBounds().withTrimmedLeft (kWood + 18).removeFromTop (kTopBar).toFloat();
    const auto font = hw::titleFont (19.0f);
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, "TRIGON-6 ", 0.0f, 0.0f);
    const float w = ga.getBoundingBox (0, -1, true).getWidth();
    g.setFont (font);
    g.setColour (hw::col::title);
    g.drawText ("TRIGON-6", bar, juce::Justification::centredLeft, false);
    g.setColour (juce::Colour (0xff8c8c90));
    g.drawText ("CONTROL", bar.withTrimmedLeft (w), juce::Justification::centredLeft, false);

    g.setColour (juce::Colour (0xff3a3a3c));
    g.fillRect ((float) kWood, (float) kTopBar - 1.0f, r.getWidth() - 2.0f * (float) kWood, 1.0f);
}

void Trigon6Editor::resized()
{
    auto r = getLocalBounds().reduced (kWood, 0);
    auto bar = r.removeFromTop (kTopBar).reduced (18, 11);
    bar.removeFromLeft (330);  // the painted title
    status.setBounds (bar.removeFromRight (150));
    bar.removeFromRight (12);
    sendAll.setBounds (bar.removeFromRight (104));
    bar.removeFromRight (8);
    resetAll.setBounds (bar.removeFromRight (122));
    bar.removeFromRight (8);
    save.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (8);
    getSound.setBounds (bar.removeFromRight (104));
    bar.removeFromRight (18);
    channel.setBounds (bar.removeFromRight (64));
    channelLabel.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (12);
    sync.setBounds (bar.removeFromLeft (220));
    bar.removeFromLeft (10);
    soundStatus.setBounds (bar);

    tabs->setBounds (r);
    if (savePanel != nullptr)
        savePanel->setBounds (getLocalBounds());
}

void Trigon6Editor::openSave()
{
    // The default slot: the program selected on the Presets tab, else the one last picked on the synth.
    int bank = 0, program = 0;
    const auto pc = proc.getLink().lastProgramChange();
    if (tabs->getCurrentContentComponent() == presetsPage && presetsPage->selectedSlot())
        std::tie (bank, program) = *presetsPage->selectedSlot();
    else if (pc.seq != 0)
        std::tie (bank, program) = std::make_pair (pc.bank, pc.program);
    openPanel (bank, program, false);
}

void Trigon6Editor::openPanel (int bank, int program, bool rename)
{
    if (savePanel != nullptr)
        return;
    savePanel = std::make_unique<SavePanel> (proc, bank, program, rename ? SavePanel::Mode::Rename : SavePanel::Mode::Save);
    savePanel->onClose = [safe = juce::Component::SafePointer<Trigon6Editor> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->savePanel.reset();
        });
    };
    addAndMakeVisible (*savePanel);
    savePanel->setBounds (getLocalBounds());
    savePanel->grabKeyboardFocus();
}

void Trigon6Editor::timerCallback()
{
    if (channel.getSelectedId() != proc.getMidiChannel())  // "Use channel N" on the Connection page
        channel.setSelectedId (proc.getMidiChannel(), juce::dontSendNotification);
    status.setText ("NRPN sent: " + juce::String (proc.getSentCount())
                        + (proc.isHostOutput() ? "  (host)" : proc.getLink().isOutputOpen() ? "  (port)" : "  (no port)"),
                    juce::dontSendNotification);
    const auto s = proc.getSoundStatus();
    soundStatus.setText (s, juce::dontSendNotification);
    soundStatus.setTooltip (s);
    resetAll.setEnabled (proc.hasPreset() && proc.presetDiffCount() > 0);
    updateSync();
}

void Trigon6Editor::updateSync()
{
    const auto& o = proc.getOrigin();
    t6::SyncInput in;
    in.slotKnown = o.slotKnown;
    in.bank = o.bank;
    in.program = o.program;
    if (o.slotKnown && presetsPage != nullptr)
    {
        in.slotData = presetsPage->storedData (o.bank, o.program);
        in.checking = presetsPage->isReading (o.bank, o.program);
    }
    in.values = proc.currentValues();
    in.name = o.name;
    in.pcData = o.pcData;
    // Knob moves on the synth reach the plugin only with Param Xmit = NR (raw 2); unknown counts as seen.
    const auto xmit = t6::readGlobal (proc.getLink().snapshot (proc.getMidiChannel()), t6::kGlobalParamXmit);
    in.seesSynthKnobs = ! xmit.known || xmit.raw == 2;

    const auto r = t6::syncStatus (in);
    juce::Colour c = juce::Colour (0xff8c8c90);
    juce::String dot;
    switch (r.state)
    {
        case t6::SyncState::InSync: c = juce::Colour (0xff7fcf6f); break;
        case t6::SyncState::PcOnly: c = juce::Colour (0xff6fb5ff); dot = juce::String::charToString (0x25CF) + " "; break;
        case t6::SyncState::Edited: c = hw::col::buttonOnTop; dot = juce::String::charToString (0x25CF) + " "; break;
        case t6::SyncState::NoSlot:
        case t6::SyncState::NotRead:
        case t6::SyncState::Checking: break;
    }
    sync.setText (dot + juce::String (r.text), juce::dontSendNotification);
    sync.setColour (juce::Label::textColourId, c);
    sync.setTooltip (juce::String (r.tooltip).trimEnd());
}

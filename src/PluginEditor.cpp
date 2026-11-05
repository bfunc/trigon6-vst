#include "PluginEditor.h"

#include "core/ParamTable.h"
#include "ui/ConnectionPage.h"
#include "ui/PanelLayout.h"
#include "ui/Theme.h"

#include <cstring>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace colours = theme;

const t6::ParamDef* findParam (const std::string& id)
{
    for (const auto& p : t6::paramTable())
        if (id == p.id)
            return &p;
    return nullptr;
}

// The label under a control: the section title already says "LFO", "Poly Mod", ... so drop that part.
juce::String shortName (const t6::ParamDef& p)
{
    const juce::String n (p.name);
    if (n.contains ("> "))
        return n.fromFirstOccurrenceOf ("> ", false, false);
    if (n.endsWith ("Octave"))
        return n;  // "Osc 1 Octave" names its oscillator row
    for (const char* pre : { "Osc 1 ", "Osc 2 ", "Osc 3 ", "FEnv ", "Amp ", "LFO ", "AT ", "PolyMod ", "Glide ", "Arp " })
        if (n.startsWith (pre))
            return n.substring ((int) std::strlen (pre));
    if (n.startsWith ("FX "))
        return n.substring (3);  // "A Type", "B Dry/Wet"
    return n;
}

// One control on the panel: a big knob, an LED-style toggle, a drop-down, or a placeholder for an unknown NRPN.
class Cell : public juce::Component, public juce::SettableTooltipClient
{
public:
    Cell (APVTS& state, const std::string& cellId)
    {
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, colours::text);
        label.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (label);

        if (! cellId.empty() && cellId[0] == '?')
        {
            label.setText (juce::String (cellId.substr (1)) + "\n(NRPN ?)", juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, colours::dim);
            label.setTooltip ("This control is not in the manual's NRPN table; its number is found from the synth's own NRPN output.");
            return;
        }

        const auto* p = findParam (cellId);
        jassert (p != nullptr);  // PanelLayout names an id that ParamTable does not have
        if (p == nullptr)
            return;
        auto* param = state.getParameter (p->id);

        switch (p->kind)
        {
            case t6::Kind::Bool:
                button = std::make_unique<juce::TextButton> (shortName (*p));
                button->setClickingTogglesState (true);
                button->setColour (juce::TextButton::buttonOnColourId, colours::led);
                addAndMakeVisible (*button);
                buttonAttach = std::make_unique<APVTS::ButtonAttachment> (state, p->id, *button);
                label.setVisible (false);
                break;

            case t6::Kind::Choice:
                combo = std::make_unique<juce::ComboBox>();
                for (size_t i = 0; i < p->choices.size(); ++i)
                    combo->addItem (p->choices[i], (int) i + 1);
                addAndMakeVisible (*combo);
                comboAttach = std::make_unique<APVTS::ComboBoxAttachment> (state, p->id, *combo);
                label.setText (shortName (*p), juce::dontSendNotification);
                break;

            case t6::Kind::Int:
            case t6::Kind::Bipolar:
                knob = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
                knob->setMouseDragSensitivity (300);
                addAndMakeVisible (*knob);
                sliderAttach = std::make_unique<APVTS::SliderAttachment> (state, p->id, *knob);
                knob->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
                label.setText (shortName (*p), juce::dontSendNotification);
                break;
        }
        setTooltip (juce::String (p->name) + "  (NRPN " + juce::String (p->nrpn) + ")");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (3);
        if (button)
        {
            button->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (34, r.getHeight())));
            return;
        }
        auto labelArea = r.removeFromBottom (18);
        label.setBounds (labelArea);
        if (combo)
            combo->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (28, r.getHeight())));
        else if (knob)
            knob->setBounds (r);
        else
            label.setBounds (getLocalBounds());  // placeholder: text fills the cell
    }

private:
    juce::Label label;
    std::unique_ptr<juce::Slider> knob;
    std::unique_ptr<juce::TextButton> button;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttach;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttach;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAttach;
};
} // namespace

// A titled box of rows; all rows of a section share one column grid, so the oscillator rows line up.
class SectionPanel : public juce::Component
{
public:
    SectionPanel (APVTS& state, const t6ui::SectionLayout& l) : layout (l)
    {
        for (const auto& row : layout.rows)
        {
            columns = juce::jmax (columns, (int) row.size());
            auto& cells = rows.emplace_back();
            for (const auto& id : row)
            {
                cells.push_back (std::make_unique<Cell> (state, id));
                addAndMakeVisible (*cells.back());
            }
        }
    }

    const t6ui::SectionLayout& layout;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (colours::section);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (juce::String (layout.title).toUpperCase(), r.removeFromTop (24.0f), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (20);
        if (rows.empty())
            return;
        const int rowH = r.getHeight() / (int) rows.size();
        const int cellW = r.getWidth() / juce::jmax (1, columns);
        for (auto& cells : rows)
        {
            auto rowArea = r.removeFromTop (rowH);
            for (auto& c : cells)
                c->setBounds (rowArea.removeFromLeft (cellW));
        }
    }

private:
    std::vector<std::vector<std::unique_ptr<Cell>>> rows;
    int columns = 1;
};

// The synth's front panel: section boxes placed by the fractions in PanelLayout.
class PanelPage : public juce::Component
{
public:
    explicit PanelPage (APVTS& state)
    {
        for (const auto& l : t6ui::panelLayout())
        {
            sections.push_back (std::make_unique<SectionPanel> (state, l));
            addAndMakeVisible (*sections.back());
        }
    }

    void paint (juce::Graphics& g) override { g.fillAll (colours::background); }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (6).toFloat();
        for (auto& s : sections)
        {
            const auto& l = s->layout;
            s->setBounds (juce::Rectangle<float> (area.getX() + l.x * area.getWidth(), area.getY() + l.y * area.getHeight(),
                                                  l.w * area.getWidth(), l.h * area.getHeight())
                              .toNearestInt());
        }
    }

private:
    std::vector<std::unique_ptr<SectionPanel>> sections;
};

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
    lnf.setColour (juce::Slider::rotarySliderFillColourId, colours::led);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, colours::border);
    lnf.setColour (juce::Slider::thumbColourId, colours::text);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::Slider::textBoxTextColourId, colours::text);
    setLookAndFeel (&lnf);

    title.setText ("TRIGON-6 CONTROL", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (title);

    channelLabel.setText ("MIDI channel", juce::dontSendNotification);
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

    status.setColour (juce::Label::textColourId, colours::dim);
    addAndMakeVisible (status);

    // Connection first; the editor opens on it unless the synth is fully connected.
    tabs = std::make_unique<Tabs>();
    tabs->setTabBarDepth (32);
    tabs->setOutline (0);
    auto* connection = new ConnectionPage (proc);
    tabs->addTab ("Connection", colours::section, connection, true);
    tabs->addTab ("Panel", colours::section, new PanelPage (proc.apvts), true);
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
    setResizeLimits (1000, 560, 2600, 1400);
    setSize (1440, 780);
    startTimerHz (4);
}

Trigon6Editor::~Trigon6Editor()
{
    tabs.reset();
    setLookAndFeel (nullptr);
}

void Trigon6Editor::paint (juce::Graphics& g) { g.fillAll (colours::background); }

void Trigon6Editor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (40).reduced (10, 6);
    title.setBounds (bar.removeFromLeft (240));
    status.setBounds (bar.removeFromRight (230));
    sendAll.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (16);
    channel.setBounds (bar.removeFromRight (70));
    channelLabel.setBounds (bar.removeFromRight (110));

    tabs->setBounds (r);
}

void Trigon6Editor::timerCallback()
{
    if (channel.getSelectedId() != proc.getMidiChannel())  // "Use channel N" on the Connection page
        channel.setSelectedId (proc.getMidiChannel(), juce::dontSendNotification);
    status.setText ("NRPN sent: " + juce::String (proc.getSentCount())
                        + (proc.isHostOutput() ? "  (host)" : proc.getLink().isOutputOpen() ? "  (port)" : "  (no port)"),
                    juce::dontSendNotification);
}

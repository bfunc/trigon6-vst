#include "PluginEditor.h"

#include "core/Connection.h"
#include "core/Globals.h"
#include "core/ParamTable.h"
#include "core/PresetDiff.h"
#include "core/SoundSync.h"
#include "ui/ConnectionPage.h"
#include "ui/HardwareLook.h"
#include "ui/PanelLayout.h"
#include "ui/PresetsPage.h"
#include "ui/SavePanel.h"
#include "ui/Theme.h"

#include <cstring>
#include <functional>
#include <set>
#include <tuple>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;

namespace colours = theme;

constexpr int kWood = 18;      // the wooden end cheeks, left and right
constexpr int kTopBar = 56;

const t6::ParamDef* findParam (const std::string& id)
{
    for (const auto& p : t6::paramTable())
        if (id == p.id)
            return &p;
    return nullptr;
}

// How a choice parameter is shown: LEDs in a column (like the LFO shape), LEDs in a row with their names above
// (like the oscillator octave), or a red window you click for a menu (effect type, glide mode, ...).
enum class ChoiceStyle { LedColumn, LedRow, Window };

ChoiceStyle choiceStyle (const std::string& id)
{
    static const std::set<std::string> column { "lfo_shape", "arp_mode", "arp_octaves" };
    static const std::set<std::string> row { "osc1_octave", "osc2_octave", "osc3_octave", "filter_keyboard" };
    if (column.count (id))
        return ChoiceStyle::LedColumn;
    if (row.count (id))
        return ChoiceStyle::LedRow;
    return ChoiceStyle::Window;
}

// The label under a control: the section title already says "LFO", "Poly Mod", ... so drop that part.
juce::String shortName (const t6::ParamDef& p)
{
    const juce::String n (p.name);
    if (n.contains ("> "))
        return n.fromFirstOccurrenceOf ("> ", false, false);
    if (n.endsWith ("Octave"))
        return "Octave";  // the row number says which oscillator
    for (const char* pre : { "Osc 1 ", "Osc 2 ", "Osc 3 ", "FEnv ", "Amp ", "LFO ", "AT ", "PolyMod ", "Glide ", "Arp " })
        if (n.startsWith (pre))
            return n.substring ((int) std::strlen (pre));
    if (n.startsWith ("FX "))
        return n.substring (3);  // "A Type", "B Dry/Wet"
    if (n == "Feedback<>Drive")
        return juce::String::fromUTF8 ("Fdbk \xe2\x80\xb9\xe2\x80\xba Drive");
    return n;
}

// Osc 3 saw, as on the synth: one button with two LEDs above it, falling saw (left) and rising ramp (right).
// A click steps Off -> Saw -> Ramp -> Off like the synth's button; values from the synth light the LEDs.
class SawRampButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SawRampButton (juce::RangedAudioParameter& p)
        : attachment (p, [this] (float v) { value = juce::roundToInt (v); repaint(); }, nullptr)
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float cx = r.getCentreX(), gap = 10.0f;
        auto top = r.removeFromTop (juce::jmin (30.0f, r.getHeight() * 0.5f));

        hw::drawLed (g, { cx - gap, top.getY() + 5.0f }, 7.0f, value == 1);
        hw::drawLed (g, { cx + gap, top.getY() + 5.0f }, 7.0f, value == 2);

        // the wave symbols under the LEDs
        const float y0 = top.getY() + 13.0f, y1 = top.getBottom() - 3.0f, w = 4.0f;
        juce::Path saw, ramp;
        saw.startNewSubPath (cx - gap - w, y1);
        saw.lineTo (cx - gap - w, y0);
        saw.lineTo (cx - gap + w, y1);
        ramp.startNewSubPath (cx + gap - w, y1);
        ramp.lineTo (cx + gap + w, y0);
        ramp.lineTo (cx + gap + w, y1);
        g.setColour (hw::col::label);
        g.strokePath (saw, juce::PathStrokeType (1.3f));
        g.strokePath (ramp, juce::PathStrokeType (1.3f));

        hw::drawPanelButton (g, r.withSizeKeepingCentre (30.0f, juce::jmin (26.0f, r.getHeight())), value > 0,
                             isMouseOver(), pressed);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        pressed = true;
        attachment.setValueAsCompleteGesture ((float) ((value + 1) % 3));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        pressed = false;
        repaint();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::ParameterAttachment attachment;
    int value = 0;
    bool pressed = false;
};

// A choice shown as red LEDs with names; a click on a name (or its LED) picks it, values from the synth light it.
class LedChoice : public juce::Component, public juce::SettableTooltipClient
{
public:
    LedChoice (juce::RangedAudioParameter& p, std::vector<std::string> names, bool asRow)
        : attachment (p, [this] (float v) { value = juce::roundToInt (v); repaint(); }, nullptr),
          items (std::move (names)), row (asRow)
    {
        attachment.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::FontOptions (row ? 10.0f : 11.0f));
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto cell = cellBounds (i);
            const juce::String name (items[(size_t) i]);
            g.setColour (i == value ? hw::col::label : hw::col::label.withAlpha (0.75f));
            if (row)
            {
                g.drawText (name, cell.withHeight (12.0f), juce::Justification::centred, false);
                hw::drawLed (g, { cell.getCentreX(), cell.getY() + 19.0f }, 7.0f, i == value);
            }
            else
            {
                hw::drawLed (g, { cell.getX() + 5.0f, cell.getCentreY() }, 7.0f, i == value);
                g.drawText (name, cell.withTrimmedLeft (14.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) items.size(); ++i)
            if (cellBounds (i).contains (e.position))
                attachment.setValueAsCompleteGesture ((float) i);
    }

private:
    juce::Rectangle<float> cellBounds (int i) const
    {
        const auto r = getLocalBounds().toFloat();
        const int n = juce::jmax (1, (int) items.size());
        if (row)
        {
            const float w = juce::jmin (26.0f, r.getWidth() / (float) n);
            const float x0 = r.getCentreX() - w * (float) n / 2.0f;
            return { x0 + w * (float) i, r.getCentreY() - 12.0f, w, 26.0f };
        }
        const float h = juce::jmin (15.0f, r.getHeight() / (float) n);
        const float y0 = r.getCentreY() - h * (float) n / 2.0f;
        return { r.getX(), y0 + h * (float) i, r.getWidth(), h };
    }

    juce::ParameterAttachment attachment;
    std::vector<std::string> items;
    bool row;
    int value = 0;
};

// A knob that shows how far it is from the preset: a small mark on the ring at the preset's value (always visible when
// the value differs) and an arc from the mark to the pointer. A click on the mark, or a double click on the knob, puts
// the preset's value back (and so sends it to the synth). Without a preset it behaves as before.
class DiffKnob : public juce::Slider
{
public:
    DiffKnob (Trigon6Processor& p, size_t idx)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow), proc (p), index (idx)
    {
    }

    bool differs() const
    {
        return proc.hasPreset() && index < proc.getPresetValues().size() && proc.getPresetValues()[index] != proc.currentValue (index);
    }

    juce::String getTooltip() override
    {
        auto text = juce::Slider::getTooltip();
        if (! differs())
            return text;
        const auto& def = t6::paramTable()[index];
        return text + "\n" + juce::String::fromUTF8 (t6::diffText (def, proc.getPresetValues()[index], proc.currentValue (index)).c_str())
               + "\nClick the mark or double-click: back to the preset";
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! differs())
            return;
        const auto geo = geometry();
        if (! geo.ok)
            return;
        const float a0 = geo.angleOf (proc.getPresetValues()[index]), a1 = geo.angleOf (proc.currentValue (index));
        const auto orange = hw::col::buttonOnTop;
        juce::Path arc;
        arc.addCentredArc (geo.c.x, geo.c.y, geo.ringR, geo.ringR, 0.0f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
        g.setColour (orange);
        g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        // the mark: never smaller than this, so a difference of one step still shows
        const auto m = geo.c.getPointOnCircumference (geo.ringR, a0);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (m));
        g.setColour (orange);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (m));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        swallowed = false;
        if (differs())
        {
            const auto geo = geometry();
            if (geo.ok && geo.c.getPointOnCircumference (geo.ringR, geo.angleOf (proc.getPresetValues()[index])).getDistanceFrom (e.position) <= 7.0f)
            {
                swallowed = true;
                proc.resetToPreset (index);
                return;
            }
        }
        juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! swallowed)
            juce::Slider::mouseDrag (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! swallowed)
            juce::Slider::mouseUp (e);
        swallowed = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (proc.hasPreset() && index < proc.getPresetValues().size())
            proc.resetToPreset (index);
        else
            juce::Slider::mouseDoubleClick (e);
    }

private:
    struct Geo
    {
        bool ok = false;
        juce::Point<float> c;
        float ringR = 0.0f;
        std::function<float (double)> angleOf;
    };
    // The same circle HardwareLook draws the knob on; the mark sits on its ring of ticks.
    Geo geometry()
    {
        Geo geo;
        const auto area = getLookAndFeel().getSliderLayout (*this).sliderBounds.toFloat();
        const float d = juce::jmin (area.getWidth(), area.getHeight()) - 12.0f;
        if (d < 10.0f)
            return geo;
        const auto rp = getRotaryParameters();
        geo.c = area.getCentre();
        geo.ringR = d / 2.0f + 3.8f;
        geo.angleOf = [this, rp] (double v)
        {
            return rp.startAngleRadians + (float) valueToProportionOfLength (v) * (rp.endAngleRadians - rp.startAngleRadians);
        };
        geo.ok = true;
        return geo;
    }

    Trigon6Processor& proc;
    size_t index;
    bool swallowed = false;
};

// A small orange dot at the corner of a button, switch or window whose value differs from the preset; a click puts the
// preset's value back.
class DiffDot : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;
    DiffDot() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
        g.setColour (hw::col::buttonOnTop);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }
};

// One control on the panel: a knob, a glowing toggle, LEDs or a red window, with its name underneath.
class Cell : public juce::Component, public juce::SettableTooltipClient
{
public:
    Cell (Trigon6Processor& processor, const std::string& cellId) : proc (processor)
    {
        auto& state = proc.apvts;
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, hw::col::label);
        label.setFont (hw::labelFont (10.5f));
        label.setMinimumHorizontalScale (0.7f);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        if (cellId.empty())
            return;
        if (cellId[0] == '?')
        {
            label.setText (juce::String (cellId.substr (1)).toUpperCase() + "\n(NRPN ?)", juce::dontSendNotification);
            label.setColour (juce::Label::textColourId, hw::col::value);
            label.setTooltip ("This control is not in the manual's NRPN table; its number is found from the synth's own NRPN output.");
            return;
        }

        const auto* p = findParam (cellId);
        jassert (p != nullptr);  // PanelLayout names an id that ParamTable does not have
        if (p == nullptr)
            return;
        paramIndex = (int) (p - t6::paramTable().data());
        auto* param = state.getParameter (p->id);
        label.setText (shortName (*p).toUpperCase(), juce::dontSendNotification);

        switch (p->kind)
        {
            case t6::Kind::Bool:
                button = std::make_unique<juce::TextButton>();
                button->setClickingTogglesState (true);
                addAndMakeVisible (*button);
                buttonAttach = std::make_unique<APVTS::ButtonAttachment> (state, p->id, *button);
                break;

            case t6::Kind::Choice:
                if (p->nrpn2 >= 0)
                {
                    sawRamp = std::make_unique<SawRampButton> (*param);
                    addAndMakeVisible (*sawRamp);
                    break;
                }
                if (const auto style = choiceStyle (p->id); style != ChoiceStyle::Window)
                {
                    leds = std::make_unique<LedChoice> (*param, p->choices, style == ChoiceStyle::LedRow);
                    addAndMakeVisible (*leds);
                    break;
                }
                combo = std::make_unique<juce::ComboBox>();
                combo->getProperties().set (hw::windowProperty, true);
                combo->setColour (juce::ComboBox::textColourId, hw::col::windowText);
                for (size_t i = 0; i < p->choices.size(); ++i)
                    combo->addItem (p->choices[i], (int) i + 1);
                addAndMakeVisible (*combo);
                comboAttach = std::make_unique<APVTS::ComboBoxAttachment> (state, p->id, *combo);
                break;

            case t6::Kind::Int:
            case t6::Kind::Bipolar:
                knob = std::make_unique<DiffKnob> (proc, (size_t) paramIndex);
                knob->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
                knob->setMouseDragSensitivity (300);
                addAndMakeVisible (*knob);
                sliderAttach = std::make_unique<APVTS::SliderAttachment> (state, p->id, *knob);
                knob->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
                break;
        }
        setTooltip (juce::String (p->name) + "  (NRPN " + juce::String (p->nrpn)
                    + (p->nrpn2 >= 0 ? " + " + juce::String (p->nrpn2) + "; click steps Off, Saw, Ramp" : juce::String()) + ")");
        for (auto* c : getChildren())
            if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c); t != nullptr && c != &label)
                t->setTooltip (getTooltip());

        // buttons, switches and windows: a dot when the value differs from the preset (a knob draws its own mark)
        if (! knob)
        {
            dot.onClick = [this] { proc.resetToPreset ((size_t) paramIndex); };
            dot.setVisible (false);
            addChildComponent (dot);
        }
    }

    // Called a few times a second: shows or hides the dot, repaints the knob's mark when the picture changed.
    void updateDiff()
    {
        if (paramIndex < 0)
            return;
        const auto idx = (size_t) paramIndex;
        const bool has = proc.hasPreset() && idx < proc.getPresetValues().size();
        const int preset = has ? proc.getPresetValues()[idx] : 0, now = proc.currentValue (idx);
        const bool differs = has && preset != now;
        const auto key = std::make_tuple (differs, preset, now);
        if (key == lastDiff)
            return;
        lastDiff = key;
        if (knob)
        {
            knob->repaint();
            return;
        }
        dot.setVisible (differs);
        if (differs)
            dot.setTooltip (juce::String::fromUTF8 (t6::diffText (t6::paramTable()[idx], preset, now).c_str()) + "\nClick: back to the preset");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        if (! knob && ! button && ! combo && ! sawRamp && ! leds)
        {
            label.setBounds (r);  // placeholder (or empty slot): text fills the cell
            return;
        }
        label.setBounds (r.removeFromBottom (15));
        if (knob)
        {
            const int side = juce::jlimit (20, 62, juce::jmin (r.getWidth(), r.getHeight() - 14));
            knob->setBounds (r.withSizeKeepingCentre (juce::jmax (side, 56), side + 14));
        }
        else if (button)
            button->setBounds (r.withSizeKeepingCentre (38, 34).withY (r.getBottom() - 36));
        else if (sawRamp)
            sawRamp->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 50), juce::jmin (r.getHeight(), 60)));
        else if (leds)
            leds->setBounds (r);
        else if (combo)
            combo->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 112), juce::jmin (26, r.getHeight())));

        juce::Rectangle<int> ctrl;
        if (button) ctrl = button->getBounds();
        else if (combo) ctrl = combo->getBounds();
        else if (sawRamp) ctrl = sawRamp->getBounds();
        else if (leds) ctrl = leds->getBounds();
        if (! ctrl.isEmpty())
            dot.setBounds (juce::Rectangle<int> (12, 12).withPosition (juce::jmin (ctrl.getRight() - 10, getWidth() - 12), juce::jmax (0, ctrl.getY() - 2)));
    }

private:
    Trigon6Processor& proc;
    int paramIndex = -1;
    DiffDot dot;
    std::tuple<bool, int, int> lastDiff { false, 0, 0 };
    juce::Label label;
    std::unique_ptr<DiffKnob> knob;
    std::unique_ptr<juce::TextButton> button;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<SawRampButton> sawRamp;
    std::unique_ptr<LedChoice> leds;
    std::unique_ptr<APVTS::SliderAttachment> sliderAttach;
    std::unique_ptr<APVTS::ButtonAttachment> buttonAttach;
    std::unique_ptr<APVTS::ComboBoxAttachment> comboAttach;
};
} // namespace

// A titled area of rows, divided from its neighbours by thin lines like the synth's panel print; all rows of a
// section share one column grid, so the oscillator rows line up.
class SectionPanel : public juce::Component
{
public:
    SectionPanel (Trigon6Processor& proc, const t6ui::SectionLayout& l) : layout (l)
    {
        for (const auto& row : layout.rows)
        {
            columns = juce::jmax (columns, (int) row.size());
            auto& cells = rows.emplace_back();
            for (const auto& id : row)
            {
                cells.push_back (std::make_unique<Cell> (proc, id));
                addAndMakeVisible (*cells.back());
            }
        }
    }

    const t6ui::SectionLayout& layout;

    void updateDiff()
    {
        for (auto& cells : rows)
            for (auto& c : cells)
                c->updateDiff();
    }

    bool isStrip() const { return layout.y >= 0.8f; }  // the bottom performance strip

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (isStrip())
        {
            g.setColour (hw::col::strip);
            g.fillRect (r);
        }
        g.setColour (hw::col::line);
        g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
        g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);

        g.setColour (hw::col::title);
        g.setFont (hw::titleFont (titleHeight() * 0.62f));
        g.drawText (juce::String (layout.title).toUpperCase(), r.removeFromTop ((float) titleHeight()).withTrimmedTop (4.0f),
                    juce::Justification::centred, false);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 4);
        r.removeFromTop (titleHeight() - 2);
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
    int titleHeight() const { return isStrip() ? 20 : juce::jlimit (22, 34, getHeight() / 6); }

    std::vector<std::vector<std::unique_ptr<Cell>>> rows;
    int columns = 1;
};

// The synth's front panel: section areas placed by the fractions in PanelLayout, between wooden end cheeks.
class PanelPage : public juce::Component, private juce::Timer
{
public:
    explicit PanelPage (Trigon6Processor& proc)
    {
        for (const auto& l : t6ui::panelLayout())
        {
            sections.push_back (std::make_unique<SectionPanel> (proc, l));
            addAndMakeVisible (*sections.back());
        }
        startTimerHz (10);
    }

    void timerCallback() override
    {
        for (auto& s : sections)
            s->updateDiff();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (hw::col::panelTop, 0.0f, 0.0f, hw::col::panel, 0.0f, r.getBottom(), false));
        g.fillRect (r);
    }

    void resized() override
    {
        const auto area = getLocalBounds().toFloat();
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
    tabs->addTab ("Panel", hw::col::panel, new PanelPage (proc), true);
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
    setSize (1500, 860);
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

#include "PanelView.h"

#include "HardwareLook.h"
#include "PanelLayout.h"
#include "core/ParamTable.h"
#include "core/PresetDiff.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

namespace
{
using juce::Colour;
using juce::Point;
using juce::Rectangle;
using t6ui::Box;

const Colour kPanel (0xff121214), kSection (0xff18181a), kEdge (0xff2e2e32), kTitle (0xffececee);
const Colour kName (0xff8d8d95), kBright (0xfff1f1f3), kDim (0xffc6c6ca), kLed (0xffff3b1f), kMark (0xff84848c);

Rectangle<float> rc (const Box& b) { return { b.x, b.y, b.w, b.h }; }
bool present (const Box& b) { return b.w > 0.0f; }
float rad (float deg) { return deg * juce::MathConstants<float>::pi / 180.0f; }

juce::Font narrow (int weight, float px, float trackEm = 0.0f)
{
    juce::Font f { juce::FontOptions (hw::archivoNarrow (weight)).withPointHeight (px) };
    if (trackEm != 0.0f)
        f.setExtraKerningFactor (trackEm * px / f.getHeight());
    return f;
}

juce::Font wide (int weight, float px)
{
    return juce::Font { juce::FontOptions (hw::archivo (weight)).withPointHeight (px) };
}

float textWidth (const juce::Font& f, const juce::String& s)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

// text with its baseline at y; anchor: 0 middle, -1 ends at x, +1 starts at x
void drawAt (juce::Graphics& g, const juce::Font& f, const juce::String& s, float x, float baseline, int anchor)
{
    const float w = textWidth (f, s);
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, s, anchor < 0 ? x - w : anchor > 0 ? x : x - w / 2.0f, baseline);
    ga.draw (g);
}

// a soft light: solid to r0, then fading out over `spread`
void glow (juce::Graphics& g, Point<float> c, float r0, float spread, Colour col, float alpha)
{
    const float r = r0 + spread;
    juce::ColourGradient grad (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + r, c.y, true);
    grad.addColour ((double) (r0 / r), col.withAlpha (alpha));
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
}

// state: 0 off, 1 on, 2 the preset's choice while another one is on
void drawLed (juce::Graphics& g, Point<float> c, int state)
{
    const float r = 3.5f;
    if (state == 1)
        glow (g, c, r, 5.0f, kLed, 0.4f);
    else if (state == 2)
        glow (g, c, r, 4.0f, juce::Colours::white, 0.11f);
    g.setColour (state == 1 ? kLed : state == 2 ? Colour (0xff55555b) : Colour (0xff3a1410));
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    g.setColour (state == 1 ? Colour (0xffff8a70) : state == 2 ? Colour (0xff8a8a92) : Colour (0x88000000));
    g.drawEllipse (c.x - r + 0.5f, c.y - r + 0.5f, 2.0f * r - 1.0f, 2.0f * r - 1.0f, 1.0f);
}

void drawSquareButton (juce::Graphics& g, Rectangle<float> r, bool on)
{
    if (on)
    {
        const float spread[] { 5.0f, 3.5f, 2.0f, 1.0f };
        const float alpha[] { 0.05f, 0.07f, 0.09f, 0.12f };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (Colour (0xffff5a1a).withAlpha (alpha[i]));
            g.fillRoundedRectangle (r.expanded (spread[i]), 3.0f + spread[i]);
        }
    }
    g.setGradientFill (juce::ColourGradient (on ? Colour (0xffff7a32) : Colour (0xff3a3a3e), r.getX(), r.getY(),
                                             on ? Colour (0xffe24a12) : Colour (0xff26262a), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (Colour (0xff060607));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (on ? Colour (0xffffa27a) : Colour (0xff4d4d52));
    g.fillRect (r.getX() + 2.0f, r.getY() + 1.0f, r.getWidth() - 4.0f, 1.0f);
}

void drawMarker (juce::Graphics& g, Point<float> c)
{
    g.setColour (kSection);
    g.fillEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f);
    g.setColour (kMark);
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
}

void drawCap (juce::Graphics& g, Point<float> c, float r, bool lift)
{
    g.setGradientFill (juce::ColourGradient (lift ? Colour (0xff313135) : Colour (0xff252528), c.x, c.y - r,
                                             lift ? Colour (0xff222225) : Colour (0xff1a1a1d), c.x, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
}

// The C1 knob of the mockup, drawn in a 120 x 120 box: a track, an arc from the preset's value to the current one when
// they differ, a flat cap with a dot for the current value (the cap lifts while it is held).
void drawKnob (juce::Graphics& g, Rectangle<float> box, int lo, int hi, int now, int preset, bool differs, bool held)
{
    juce::Graphics::ScopedSaveState saved (g);
    g.addTransform (juce::AffineTransform::scale (box.getWidth() / 120.0f).translated (box.getX(), box.getY()));

    const auto angle = [lo, hi] (int v) { return -135.0f + 270.0f * (float) (v - lo) / (float) juce::jmax (1, hi - lo); };
    const auto pt = [] (float deg, float r) { return Point<float> (60.0f + r * std::sin (rad (deg)), 60.0f - r * std::cos (rad (deg))); };
    const auto arc = [] (float a1, float a2, float r)
    {
        juce::Path p;
        p.addCentredArc (60.0f, 60.0f, r, r, 0.0f, rad (std::min (a1, a2)), rad (std::max (a1, a2)), true);
        return p;
    };

    g.setColour (Colour (0xff29292d));
    g.strokePath (arc (-135.0f, 135.0f, 45.0f), juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (differs)
    {
        const float as = angle (preset);
        float av = angle (now);
        if (std::abs (av - as) < 4.0f)
            av = as + (av >= as ? 4.0f : -4.0f);
        g.setColour (kMark);
        g.strokePath (arc (as, av, 45.0f), juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        const auto e = pt (av, 45.0f);
        g.fillEllipse (e.x - 4.0f, e.y - 4.0f, 8.0f, 8.0f);
    }

    const float cr = held ? 32.0f : 30.0f;
    if (held)
        glow (g, { 60.0f, 63.0f }, 24.0f, 14.0f, juce::Colours::black, 0.55f);
    drawCap (g, { 60.0f, 60.0f }, cr, held);
    g.setColour (juce::Colours::black);
    g.drawEllipse (60.0f - cr, 60.0f - cr, 2.0f * cr, 2.0f * cr, 1.5f);
    g.setColour (Colour (0x14ffffff));
    g.drawEllipse (60.0f - cr + 3.0f, 60.0f - cr + 3.0f, 2.0f * (cr - 3.0f), 2.0f * (cr - 3.0f), 1.0f);

    const auto d = pt (angle (now), 21.0f);
    if (held)
        glow (g, d, 3.0f, 8.0f, juce::Colours::white, 0.45f);
    g.setColour (Colour (0xfff6f6f6));
    g.fillEllipse (d.x - 4.0f, d.y - 4.0f, 8.0f, 8.0f);
}

void drawGlyph (juce::Graphics& g, Rectangle<float> box, const char* kind)
{
    static const std::unordered_map<std::string, std::vector<Point<float>>> shapes {
        { "tri", { { 1, 11 }, { 6, 1 }, { 11, 11 } } },
        { "saw", { { 1, 11 }, { 1, 1 }, { 11, 11 } } },
        { "ramp", { { 1, 11 }, { 11, 1 }, { 11, 11 } } },
        { "pulse", { { 1, 11 }, { 1, 1 }, { 6, 1 }, { 6, 11 }, { 11, 11 } } },
    };
    const auto& pts = shapes.at (kind);
    juce::Path p;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const Point<float> q (box.getX() + pts[i].x, box.getY() + pts[i].y);
        if (i == 0)
            p.startNewSubPath (q);
        else
            p.lineTo (q);
    }
    g.setColour (Colour (0xffd0d0d5));
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

// ---- what the synth's manual and the synth itself say about the values --------------------------------------------

const char* const kDelayNotes[] { "16", "16d", "8t", "8", "8d", "4t", "4", "4d", "2", "2d", "1" };
const char* const kLfoNotes[] { "32o", "16o", "8o", "6o", "4o", "3o", "2o", "4d", "4", "4t", "8d", "8", "8t", "16d", "16", "16t", "32", "32t" };

template <size_t N>
juce::String noteAt (const char* const (&list)[N], int v, int lo, int hi)
{
    const int i = juce::jlimit (0, (int) N - 1, (int) std::floor ((double) (v - lo) / (double) (hi - lo + 1) * (double) N));
    return list[i];
}

// the two knobs of an effect take the names the manual gives them; the flangers are not named there
const char* effectKnobName (int type, int knob)
{
    static const char* const names[][2] {
        { "Param 1", "Param 2" },  // Off
        { "Delay time", "Feedback" }, { "Delay time", "Feedback" },
        { "Rate", "Depth" }, { "Rate", "Depth" }, { "Rate", "Depth" }, { "Rate", "Depth" },
        { "Mod freq", "Tracking" },
        { "Param 1", "Param 2" }, { "Param 1", "Param 2" },
        { "Rev. time", "Early ref." }, { "Rev. time", "Early ref." }, { "Rev. time", "Early ref." },
        { "Decay", "Tone" },
    };
    return names[juce::jlimit (0, 13, type)][knob];
}

const char* const kEffectNames[] { "Off", "BBD", "D. Delay", "Chorus", "Phaser 1", "Phaser 2", "Phaser 3", "Ring Mod", "Flanger 1", "Flanger 2",
                                   "Rev. Hall", "Rev. Room", "Rev. Plate", "Rev. Spring" };
const char* const kEffectNotes[] { "", "Bucket-brigade delay emulation", "Classic digital delay", "Vintage chorus", "6-stage phaser, high resonance",
                                   "6-stage phaser, lower resonance", "Original Oberheim phaser design", "Ring modulator",
                                   "Vintage flanger, high resonance", "Vintage flanger, low resonance", "Classic hall reverb",
                                   "Classic room reverb", "Classic plate reverb", "Guitar-amp-style spring reverb" };
const char* const kGlideNames[] { "Fix Rate", "Fix Rate A", "Fix Time", "Fix Time A" };

const std::unordered_map<std::string, size_t>& indexById()
{
    static const auto map = []
    {
        std::unordered_map<std::string, size_t> m;
        const auto& t = t6::paramTable();
        for (size_t i = 0; i < t.size(); ++i)
            m[t[i].id] = i;
        return m;
    }();
    return map;
}

// ---- one parameter as the panel sees it ------------------------------------------------------------------------

struct Binding
{
    Binding() = default;
    Binding (Trigon6Processor& p, const std::string& id) : proc (&p)
    {
        const auto it = indexById().find (id);
        if (it == indexById().end())
            return;
        index = it->second;
        def = &t6::paramTable()[index];
        param = p.apvts.getParameter (def->id);
    }

    bool ok() const { return proc != nullptr && def != nullptr && param != nullptr; }
    int value() const { return ok() ? proc->currentValue (index) : 0; }
    bool hasPreset() const { return ok() && proc->hasPreset() && index < proc->getPresetValues().size(); }
    int preset() const { return hasPreset() ? proc->getPresetValues()[index] : value(); }
    bool differs() const { return hasPreset() && preset() != value(); }

    void set (int v) const
    {
        if (! ok())
            return;
        v = juce::jlimit (def->min, def->max, v);
        const float plain = (float) (def->kind == t6::Kind::Choice ? v - def->min : v);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }
    void begin() const { if (ok()) param->beginChangeGesture(); }
    void end() const { if (ok()) param->endChangeGesture(); }
    void put (int v) const { begin(); set (v); end(); }
    void toPreset() const { if (hasPreset()) proc->resetToPreset (index); }
    void toDefault() const
    {
        if (! ok())
            return;
        begin();
        param->setValueNotifyingHost (param->getDefaultValue());
        end();
    }

    Trigon6Processor* proc = nullptr;
    size_t index = 0;
    const t6::ParamDef* def = nullptr;
    juce::RangedAudioParameter* param = nullptr;
};

// ---- the menu --------------------------------------------------------------------------------------------------

struct MenuItem
{
    juce::String text, note;
};

// A list of choices drawn over the panel, never outside it: below the control if it fits, else above, else on the roomier
// side with a scroll. The rest of the panel is covered while it is open; a click there closes it.
class MenuOverlay : public juce::Component
{
public:
    MenuOverlay() { setWantsKeyboardFocus (true); setVisible (false); }

    void open (Rectangle<float> anchorBox, std::vector<MenuItem> list, int chosen, int saved, std::function<void (int)> pickCallback)
    {
        if (isVisible() && anchor == anchorBox)
        {
            close();
            return;
        }
        anchor = anchorBox;
        items = std::move (list);
        on = chosen;
        pre = saved;
        pick = std::move (pickCallback);
        hover = -1;
        scroll = 0.0f;
        setBounds (getParentComponent()->getLocalBounds());
        layout();
        setVisible (true);
        toFront (false);
        grabKeyboardFocus();
        repaint();
    }

    void close()
    {
        setVisible (false);
        anchor = {};
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto m = menu;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (m.translated (0.0f, 6.0f).expanded (3.0f), 8.0f);
        g.setColour (Colour (0xff0a0a0b));
        g.fillRoundedRectangle (m, 3.0f);
        g.setColour (Colour (0xff3a3a3e));
        g.drawRoundedRectangle (m.reduced (0.5f), 3.0f, 1.0f);

        g.saveState();
        g.reduceClipRegion (m.reduced (1.0f).toNearestInt());
        const auto nameFont = wide (600, 14.0f), noteFont = wide (500, 12.0f);
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto r = itemBox (i);
            if (r.getBottom() < m.getY() || r.getY() > m.getBottom())
                continue;
            if (i == hover)
            {
                g.setColour (Colour (0xff232327));
                g.fillRoundedRectangle (r, 2.0f);
            }
            drawLed (g, { r.getX() + 14.0f, r.getCentreY() }, i == on ? 1 : (i == pre && pre != on) ? 2 : 0);
            g.setColour (i == on ? juce::Colours::white : Colour (0xffcfcfd4));
            g.setFont (nameFont);
            g.drawText (items[(size_t) i].text, r.withTrimmedLeft (28.0f), juce::Justification::centredLeft, false);
            if (items[(size_t) i].note.isNotEmpty())
            {
                g.setColour (kName);
                g.setFont (noteFont);
                g.drawText (items[(size_t) i].note, r.withTrimmedRight (14.0f), juce::Justification::centredRight, false);
            }
        }
        g.restoreState();
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (indexAt (e.position)); }
    void mouseDrag (const juce::MouseEvent& e) override { setHover (indexAt (e.position)); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! menu.contains (e.position))
        {
            close();
            return;
        }
        if (const int i = indexAt (e.position); i >= 0)
            choose (i);
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scrollBy (-w.deltaY * 120.0f); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
            close();
        else if (k == juce::KeyPress::downKey)
            setHover (juce::jmin ((int) items.size() - 1, (hover < 0 ? on : hover) + 1), true);
        else if (k == juce::KeyPress::upKey)
            setHover (juce::jmax (0, (hover < 0 ? on : hover) - 1), true);
        else if (k == juce::KeyPress::returnKey && hover >= 0)
            choose (hover);
        else
            return false;
        return true;
    }

private:
    static constexpr float kItemH = 32.0f, kPad = 5.0f;

    void choose (int i)
    {
        auto callback = pick;
        close();
        if (callback)
            callback (i);
    }

    void setHover (int i, bool reveal = false)
    {
        if (i == hover)
            return;
        hover = i;
        if (reveal && i >= 0)
        {
            const auto r = itemBox (i);
            if (r.getY() < menu.getY() + kPad)
                scrollBy (r.getY() - menu.getY() - kPad);
            else if (r.getBottom() > menu.getBottom() - kPad)
                scrollBy (r.getBottom() - menu.getBottom() + kPad);
        }
        repaint();
    }

    void scrollBy (float dy)
    {
        scroll = juce::jlimit (0.0f, juce::jmax (0.0f, contentH - menu.getHeight() + 2.0f * kPad), scroll + dy);
        repaint();
    }

    Rectangle<float> itemBox (int i) const
    {
        return { menu.getX() + kPad, menu.getY() + kPad + kItemH * (float) i - scroll, menu.getWidth() - 2.0f * kPad, kItemH };
    }

    int indexAt (Point<float> p) const
    {
        if (! menu.contains (p))
            return -1;
        for (int i = 0; i < (int) items.size(); ++i)
            if (itemBox (i).contains (p))
                return i;
        return -1;
    }

    void layout()
    {
        const auto nameFont = wide (600, 14.0f), noteFont = wide (500, 12.0f);
        float w = 0.0f;
        for (const auto& it : items)
            w = std::max (w, 10.0f + 8.0f + 10.0f + textWidth (nameFont, it.text) + (it.note.isEmpty() ? 0.0f : 18.0f + textWidth (noteFont, it.note)) + 14.0f);
        w = std::max (w + 2.0f * kPad, std::max (120.0f, anchor.getWidth()));
        contentH = kItemH * (float) items.size();
        float h = contentH + 2.0f * kPad;
        const float canvasW = (float) getWidth(), canvasH = (float) getHeight();
        const float below = canvasH - anchor.getBottom() - 10.0f, above = anchor.getY() - 10.0f;
        float top;
        if (h <= below)
            top = anchor.getBottom() + 3.0f;
        else if (h <= above)
            top = anchor.getY() - h - 3.0f;
        else if (below >= above)
        {
            top = anchor.getBottom() + 3.0f;
            h = below;
        }
        else
        {
            h = above;
            top = anchor.getY() - above - 3.0f;
        }
        const float left = juce::jmax (6.0f, juce::jmin (anchor.getX(), canvasW - 6.0f - w));
        menu = { left, top, w, h };
        if (on >= 0)  // bring the chosen item into view
        {
            const float y = kItemH * (float) on;
            if (y + kItemH > h - 2.0f * kPad)
                scroll = juce::jmin (y + kItemH - (h - 2.0f * kPad), juce::jmax (0.0f, contentH - h + 2.0f * kPad));
        }
    }

    std::vector<MenuItem> items;
    std::function<void (int)> pick;
    Rectangle<float> anchor, menu;
    int on = -1, pre = -1, hover = -1;
    float scroll = 0.0f, contentH = 0.0f;
};

// ---- the cells -------------------------------------------------------------------------------------------------

enum class Style { Knob, Button, Saw, Column, Clock, Row, Octave, Window, Env };

Style styleOf (const t6::ParamDef& d)
{
    const std::string id = d.id;
    if (d.nrpn2 >= 0)
        return Style::Saw;
    if (id == "clock_divide")
        return Style::Clock;
    if (id == "lfo_shape" || id == "arp_mode" || id == "arp_octaves")
        return Style::Column;
    if (id == "osc1_octave" || id == "osc2_octave" || id == "osc3_octave")
        return Style::Octave;
    if (id == "filter_keyboard")
        return Style::Row;
    switch (d.kind)
    {
        case t6::Kind::Int:
        case t6::Kind::Bipolar: return Style::Knob;
        case t6::Kind::Bool: return Style::Button;
        case t6::Kind::Choice: break;
    }
    return Style::Window;
}

const char* waveOf (const std::string& id)
{
    if (id.size() > 5 && id.compare (0, 3, "osc") == 0 && id[4] == '_')
    {
        const auto w = id.substr (5);
        if (w == "tri" || w == "saw" || w == "pulse")
            return w == "tri" ? "tri" : w == "saw" ? "saw" : "pulse";
    }
    return nullptr;
}

class Cell : public juce::Component, public juce::SettableTooltipClient
{
public:
    Cell (const t6ui::CellGeo& geo, std::vector<Binding> bindings, MenuOverlay& menuOverlay)
        : g (geo), b (std::move (bindings)), menu (menuOverlay)
    {
        Rectangle<float> u = rc (g.cell);
        for (const auto* box : { &g.ctl, &g.main, &g.val, &g.lab })
            if (present (*box))
                u = u.getUnion (rc (*box));
        for (const auto& it : g.items)
            u = u.getUnion (rc (it.box));
        const auto bi = u.expanded (12.0f).getSmallestIntegerContainer();
        setBounds (bi);
        origin = bi.getPosition().toFloat();
        last.assign (b.size() * 2 + 1, -1);
    }

    const char* id() const { return g.id; }
    virtual void activate() {}  // as a click on the control (a window opens its list)

    // Called a few times a second: repaints when a value, the preset or a related value changed.
    void tick()
    {
        std::vector<int> now;
        for (const auto& x : b)
        {
            now.push_back (x.value());
            now.push_back (x.hasPreset() ? x.preset() : -1000000);
        }
        now.push_back (0);
        if (now != last)
        {
            last = std::move (now);
            repaint();
        }
    }

    bool hitTest (int x, int y) override
    {
        const Point<float> p ((float) x, (float) y);
        return hot().contains (p) || (marked() && markerBox().contains (p));
    }

    juce::String getTooltip() override
    {
        auto t = baseTip();
        if (const auto& m = b.front(); diffTip() && m.ok() && m.differs())
            t += "\n" + juce::String::fromUTF8 (t6::diffText (*m.def, m.preset(), m.value()).c_str()) + "\n" + hintBack();
        return t;
    }

    void paint (juce::Graphics& gr) override
    {
        paintBody (gr);
        paintName (gr);
        if (marked())
            drawMarker (gr, markerPoint());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (marked() && markerBox().contains (e.position))
        {
            b.front().toPreset();
            swallowed = true;
            repaint();
            return;
        }
        swallowed = false;
        down (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! swallowed) drag (e); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! swallowed)
            up (e);
        swallowed = false;
    }

protected:
    virtual void paintBody (juce::Graphics&) = 0;
    virtual void down (const juce::MouseEvent&) {}
    virtual void drag (const juce::MouseEvent&) {}
    virtual void up (const juce::MouseEvent&) {}
    virtual Rectangle<float> hot() const { return loc (g.main); }
    virtual juce::String baseTip() const
    {
        const auto& m = b.front();
        return m.ok() ? juce::String (m.def->name) + "  (NRPN " + juce::String (m.def->nrpn)
                              + (m.def->nrpn2 >= 0 ? " + " + juce::String (m.def->nrpn2) : juce::String()) + ")"
                       : juce::String();
    }
    virtual juce::String hintBack() const { return "Click the dot: back to the preset"; }
    virtual bool diffTip() const { return true; }
    virtual bool lit() const { return false; }  // the name goes bright (a button that is on)
    virtual juce::String nameText() const { return juce::String::fromUTF8 (g.labText); }

    Rectangle<float> loc (const Box& box) const { return rc (box).translated (-origin.x, -origin.y); }
    Point<float> loc (Point<float> p) const { return p - origin; }

    bool marked() const { return g.marker && b.front().differs(); }
    bool bottomMarker() const { return waveOf (g.id) != nullptr || styleOf (*b.front().def) == Style::Saw; }
    Point<float> markerPoint() const
    {
        const auto m = loc (g.main);
        return { m.getRight() + 1.2f, bottomMarker() ? m.getBottom() + 1.2f : m.getY() - 1.2f };
    }
    Rectangle<float> markerBox() const { return Rectangle<float> (16.0f, 16.0f).withCentre (markerPoint()); }

    void paintName (juce::Graphics& gr)
    {
        if (! present (g.lab))
            return;
        auto r = loc (g.lab);
        const auto cell = loc (g.cell);
        r = r.withX (cell.getX()).withWidth (cell.getWidth()).translated (0.0f, 1.0f);
        gr.setColour (lit() ? kBright : kName);
        gr.setFont (narrow (700, 10.5f, g.inBlock ? 0.02f : 0.07f));
        gr.drawText (nameText().toUpperCase(), r, juce::Justification::centred, true);
    }

    const t6ui::CellGeo& g;
    std::vector<Binding> b;  // the cell's own parameter first, then the ones its text depends on
    MenuOverlay& menu;
    Point<float> origin;
    std::vector<int> last;
    bool swallowed = false;
};

// --- knobs

class KnobCell : public Cell
{
public:
    KnobCell (const t6ui::CellGeo& geo, std::vector<Binding> bindings, MenuOverlay& m, bool effectKnob)
        : Cell (geo, std::move (bindings), m), effect (effectKnob)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

protected:
    void paintBody (juce::Graphics& gr) override
    {
        const auto& m = b.front();
        const auto box = loc (g.main);  // the knob is a square, centred in the box it was given
        drawKnob (gr, Rectangle<float> (std::min (box.getWidth(), box.getHeight()), std::min (box.getWidth(), box.getHeight())).withCentre (box.getCentre()),
                  m.def->min, m.def->max, m.value(), m.preset(), m.differs(), held || keyboard);
        if (present (g.val))
        {
            gr.setColour (kBright);
            gr.setFont (narrow (500, 13.4f));
            const auto cell = loc (g.cell);
            gr.drawText (valueText(), loc (g.val).withX (cell.getX()).withWidth (cell.getWidth()), juce::Justification::centred, false);
        }
    }

    juce::String nameText() const override
    {
        if (effect)
            return effectKnobName (b[1].value(), g.id[std::strlen ("fxa_param")] == '2' ? 1 : 0);
        return juce::String::fromUTF8 (g.labText);
    }

    juce::String valueText() const
    {
        const auto& m = b.front();
        const int v = m.value();
        const std::string id = m.def->id;
        if (id == "lfo_freq" && b[1].value() != 0)
            return noteAt (kLfoNotes, v, m.def->min, m.def->max);
        if (effect)
        {
            const int type = b[1].value();
            if (id.back() == '1' && b[2].value() != 0 && (type == 1 || type == 2))
                return noteAt (kDelayNotes, v, m.def->min, m.def->max);
            if (id.back() == '2' && type == 7)
                return v > m.def->max / 2 ? "On" : "Off";
        }
        if (m.def->kind == t6::Kind::Bipolar)
            return juce::String (v - m.def->center);
        return juce::String (t6::formatValue (*m.def, v));
    }

    juce::String hintBack() const override { return "Double-click: back to the preset"; }

    void activate() override
    {
        const auto& m = b.front();
        m.put (m.value() + juce::jmax (1, (m.def->max - m.def->min) / 20));
        repaint();
    }

    void down (const juce::MouseEvent& e) override
    {
        held = true;
        startY = e.position.y;
        startValue = b.front().value();
        b.front().begin();
        grabKeyboardFocus();
        keyboard = false;
        repaint();
    }
    void drag (const juce::MouseEvent& e) override
    {
        const auto& m = b.front();
        const float perPixel = (float) (m.def->max - m.def->min) / (e.mods.isShiftDown() ? 800.0f : 200.0f);
        m.set ((int) std::lround ((float) startValue + (startY - e.position.y) * perPixel));
        repaint();
    }
    void up (const juce::MouseEvent&) override
    {
        b.front().end();
        held = false;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (b.front().hasPreset())
            b.front().put (b.front().preset());
        else
            b.front().toDefault();
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const auto& m = b.front();
        const int step = juce::jmax (1, (m.def->max - m.def->min) / 127);
        m.put (m.value() + (w.deltaY > 0 ? step : -step));
        repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto& m = b.front();
        const int step = juce::jmax (1, (m.def->max - m.def->min) / 127) * (k.getModifiers().isShiftDown() ? 8 : 1);
        if (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::rightKey))
            m.put (m.value() + step);
        else if (k.isKeyCode (juce::KeyPress::downKey) || k.isKeyCode (juce::KeyPress::leftKey))
            m.put (m.value() - step);
        else if (k.isKeyCode (juce::KeyPress::homeKey) || k.isKeyCode (juce::KeyPress::escapeKey))
            m.hasPreset() ? m.put (m.preset()) : m.toDefault();
        else
            return false;
        keyboard = true;
        repaint();
        return true;
    }
    void focusLost (FocusChangeType) override
    {
        keyboard = false;
        repaint();
    }

private:
    bool effect, held = false, keyboard = false;
    float startY = 0.0f;
    int startValue = 0;
};

// --- a square button; the three wave buttons of an oscillator show their wave above

class ButtonCell : public Cell
{
public:
    using Cell::Cell;

protected:
    void paintBody (juce::Graphics& gr) override
    {
        const auto r = loc (g.main);
        const char* wave = waveOf (g.id);
        if (wave != nullptr)
            drawGlyph (gr, Rectangle<float> (12.0f, 12.0f).withCentre ({ r.getCentreX(), r.getY() - 12.0f }), wave);
        drawSquareButton (gr, r, b.front().value() != 0);
    }
    bool lit() const override { return b.front().value() != 0; }
    void activate() override { toggle(); }
    void down (const juce::MouseEvent&) override { toggle(); }
    void toggle() { b.front().put (b.front().value() != 0 ? 0 : 1); repaint(); }
};

// --- Osc 3 saw: one button with two LEDs, Off -> Saw -> Ramp -> Off

class SawCell : public Cell
{
public:
    using Cell::Cell;

protected:
    void paintBody (juce::Graphics& gr) override
    {
        const auto r = loc (g.main);
        const int v = b.front().value();
        const float cx = r.getCentreX();
        drawLed (gr, { cx - 10.5f, r.getY() - 28.5f }, v == 1 ? 1 : 0);
        drawLed (gr, { cx + 10.5f, r.getY() - 28.5f }, v == 2 ? 1 : 0);
        drawGlyph (gr, Rectangle<float> (12.0f, 12.0f).withCentre ({ cx - 10.5f, r.getY() - 12.0f }), "saw");
        drawGlyph (gr, Rectangle<float> (12.0f, 12.0f).withCentre ({ cx + 10.5f, r.getY() - 12.0f }), "ramp");
        drawSquareButton (gr, r, v > 0);
    }
    juce::String baseTip() const override { return Cell::baseTip() + "; click steps Off, Saw, Ramp"; }
    void activate() override { step(); }
    void down (const juce::MouseEvent&) override { step(); }
    void step() { b.front().put ((b.front().value() + 1) % 3); repaint(); }
};

// --- LEDs with names: a column, the clock table (two columns) or a row with the names above

class ListCell : public Cell
{
public:
    ListCell (const t6ui::CellGeo& geo, std::vector<Binding> bindings, MenuOverlay& m, Style s) : Cell (geo, std::move (bindings), m), style (s)
    {
        const auto& d = *b.front().def;
        for (size_t i = 0; i < g.items.size(); ++i)
        {
            int idx = (int) i;
            for (size_t c = 0; c < d.choices.size(); ++c)
                if (d.choices[c] == g.items[i].text)
                    idx = (int) c;
            choice.push_back (idx + d.min);
        }
    }

protected:
    Rectangle<float> hot() const override
    {
        Rectangle<float> u;
        for (const auto& it : g.items)
            u = u.isEmpty() ? loc (it.box) : u.getUnion (loc (it.box));
        return u;
    }

    void paintBody (juce::Graphics& gr) override
    {
        const auto& m = b.front();
        const int now = m.value(), saved = m.preset();
        for (size_t i = 0; i < g.items.size(); ++i)
        {
            const auto r = loc (g.items[i].box);
            const bool on = now == choice[i], pre = m.differs() && saved == choice[i];
            const int state = on ? 1 : pre ? 2 : 0;
            if (style == Style::Row)
            {
                gr.setColour (kDim);
                gr.setFont (narrow (600, 9.5f));
                gr.drawText (g.items[i].text, Rectangle<float> (r.getX() - 6.0f, r.getY() + 3.0f, r.getWidth() + 12.0f, 9.5f), juce::Justification::centred, false);
                drawLed (gr, { r.getCentreX(), r.getY() + 24.0f }, state);
            }
            else
            {
                drawLed (gr, { r.getX() + 3.5f, r.getCentreY() }, state);
                gr.setColour (on ? juce::Colours::white : kDim);
                gr.setFont (narrow (600, 10.5f, 0.04f));
                gr.drawText (g.items[i].text, r.withTrimmedLeft (18.0f).withWidth (r.getWidth() + 14.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    void activate() override { pick (g.items.size() > 1 ? 1 : 0); }
    void down (const juce::MouseEvent& e) override
    {
        for (size_t i = 0; i < g.items.size(); ++i)
            if (loc (g.items[i].box).contains (e.position))
            {
                pick (i);
                return;
            }
    }

private:
    void pick (size_t i)
    {
        if (i < choice.size())
            b.front().put (choice[i]);
        repaint();
    }

    Style style;
    std::vector<int> choice;
};

// --- the octave dial: a cap with the choices on an arc above it; a click on a spot picks it, elsewhere steps on

class OctaveCell : public Cell
{
public:
    using Cell::Cell;

protected:
    void paintBody (juce::Graphics& gr) override
    {
        const auto& m = b.front();
        const auto& names = m.def->choices;
        const auto box = loc (g.main);
        const float s = std::min (box.getWidth() / 100.0f, box.getHeight() / 92.0f);
        juce::Graphics::ScopedSaveState saved (gr);
        gr.addTransform (juce::AffineTransform::scale (s).translated (box.getX() + (box.getWidth() - 100.0f * s) / 2.0f, box.getY() + (box.getHeight() - 92.0f * s) / 2.0f));
        const auto font = narrow (600, 10.0f);
        for (int i = 0; i < (int) names.size(); ++i)
        {
            const float a = angleOf (i, (int) names.size());
            const auto q = spot (a);
            const bool on = m.value() - m.def->min == i, pre = m.differs() && m.preset() - m.def->min == i;
            float tx, ty;
            int anchor = 0;
            if (a == -105.0f) { tx = q.x; ty = q.y + 14.0f; }
            else if (std::abs (a) > 60.0f) { tx = q.x + (a < 0 ? -10.0f : 10.0f); ty = q.y + 3.2f; anchor = a < 0 ? -1 : 1; }
            else if (a == 0.0f) { tx = q.x; ty = q.y - 9.5f; }
            else { tx = q.x + (a < 0 ? -4.0f : 4.0f); ty = q.y - 9.0f; }
            drawLed (gr, q, on ? 1 : pre ? 2 : 0);
            gr.setColour (on ? juce::Colours::white : kDim);
            drawAt (gr, font, names[(size_t) i] == "Lo" ? "LO" : juce::String (names[(size_t) i]), tx, ty, anchor);
        }
        drawCap (gr, { 50.0f, 46.0f }, 17.0f, false);
        gr.setColour (juce::Colours::black);
        gr.drawEllipse (50.0f - 17.0f, 46.0f - 17.0f, 34.0f, 34.0f, 1.3f);
        gr.setColour (Colour (0x14ffffff));
        gr.drawEllipse (50.0f - 14.5f, 46.0f - 14.5f, 29.0f, 29.0f, 1.0f);
    }

    void activate() override
    {
        const auto& m = b.front();
        m.put (m.def->min + (m.value() - m.def->min + 1) % (int) m.def->choices.size());
        repaint();
    }

    void down (const juce::MouseEvent& e) override
    {
        const auto& m = b.front();
        const int n = (int) m.def->choices.size();
        const auto box = loc (g.main);
        const float s = std::min (box.getWidth() / 100.0f, box.getHeight() / 92.0f);
        const Point<float> p ((e.position.x - box.getX() - (box.getWidth() - 100.0f * s) / 2.0f) / s, (e.position.y - box.getY() - (box.getHeight() - 92.0f * s) / 2.0f) / s);
        for (int i = 0; i < n; ++i)
            if (spot (angleOf (i, n)).getDistanceFrom (p) <= 9.0f)
            {
                m.put (m.def->min + i);
                repaint();
                return;
            }
        m.put (m.def->min + (m.value() - m.def->min + 1) % n);
        repaint();
    }

    juce::String hintBack() const override { return "Click the dot: back to the preset"; }

private:
    static float angleOf (int i, int n)
    {
        static const float five[] { -70.0f, -35.0f, 0.0f, 35.0f, 70.0f };
        static const float six[] { -105.0f, -70.0f, -35.0f, 0.0f, 35.0f, 70.0f };
        return n == 6 ? six[i] : five[juce::jmin (i, 4)];
    }
    static Point<float> spot (float deg) { return { 50.0f + 29.0f * std::sin (rad (deg)), 46.0f - 29.0f * std::cos (rad (deg)) }; }
};

// --- a red window; a click opens the list of choices

class WindowCell : public Cell
{
public:
    WindowCell (const t6ui::CellGeo& geo, std::vector<Binding> bindings, MenuOverlay& m) : Cell (geo, std::move (bindings), m)
    {
        const std::string id = b.front().def->id;
        effect = id == "fxa_type" || id == "fxb_type";
        glide = id == "glide_mode";
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

protected:
    juce::String nameOf (int i) const
    {
        const auto& d = *b.front().def;
        if (effect && i >= 0 && i < 14)
            return kEffectNames[i];
        if (glide && i >= 0 && i < 4)
            return kGlideNames[i];
        return juce::String (d.choices[(size_t) juce::jlimit (0, (int) d.choices.size() - 1, i)]);
    }

    void paintBody (juce::Graphics& gr) override
    {
        const auto r = loc (g.main);
        gr.setColour (Colour (0xff050506));
        gr.fillRoundedRectangle (r, 2.0f);
        gr.setGradientFill (juce::ColourGradient (Colour (0xcc000000), r.getX(), r.getY(), Colour (0x00000000), r.getX(), r.getY() + 5.0f, false));
        gr.fillRect (r.reduced (1.0f).withHeight (5.0f));
        gr.setColour (juce::Colours::black);
        gr.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
        gr.setColour (kLed);
        gr.setFont (juce::Font { juce::FontOptions (hw::segmentFont (12.5f).getTypefacePtr()).withPointHeight (12.5f) });
        gr.drawText (nameOf (b.front().value() - b.front().def->min), r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
    }

    void activate() override { openList(); }
    void down (const juce::MouseEvent&) override { openList(); }

    void openList()
    {
        const auto& m = b.front();
        std::vector<MenuItem> items;
        for (int i = 0; i < (int) m.def->choices.size(); ++i)
            items.push_back ({ nameOf (i), effect && kEffectNotes[i][0] != 0 ? juce::String (kEffectNotes[i]) : juce::String() });
        const int chosen = m.value() - m.def->min, saved = m.differs() ? m.preset() - m.def->min : -1;
        const auto anchor = loc (g.main) + origin;
        menu.open (anchor, std::move (items), chosen, saved, [mm = m, lo = m.def->min] (int i) { mm.put (lo + i); });
    }

private:
    bool effect = false, glide = false;
};

// --- an envelope drawn as a display: three dots drag attack, decay with sustain, and release

class EnvCell : public Cell
{
public:
    using Cell::Cell;

protected:
    struct Shape
    {
        float x0, seg, top, bot, xa, xd, ys, xs, xr;
    };

    Shape shape (const int v[4], float w) const
    {
        Shape s;
        const float pad = 7.0f, h = 62.0f;
        s.x0 = pad;
        s.seg = (w - 2.0f * pad) * 0.28f;
        const float hold = (w - 2.0f * pad) * 0.16f;
        s.top = pad;
        s.bot = h - pad;
        s.xa = s.x0 + (float) v[0] / 127.0f * s.seg;
        s.xd = s.xa + (float) v[1] / 127.0f * s.seg;
        s.ys = s.top + (1.0f - (float) v[2] / 127.0f) * (s.bot - s.top);
        s.xs = s.xd + hold;
        s.xr = s.xs + (float) v[3] / 127.0f * s.seg;
        return s;
    }

    static juce::Path outline (const Shape& s, bool close)
    {
        juce::Path p;
        p.startNewSubPath (s.x0, s.bot);
        p.lineTo (s.xa, s.top);
        p.lineTo (s.xd, s.ys);
        p.lineTo (s.xs, s.ys);
        p.lineTo (s.xr, s.bot);
        if (close)
            p.closeSubPath();
        return p;
    }

    void paintBody (juce::Graphics& gr) override
    {
        const auto box = loc (g.main);
        const float w = box.getWidth(), h = box.getHeight();
        juce::Graphics::ScopedSaveState saved (gr);
        gr.addTransform (juce::AffineTransform::translation (box.getX(), box.getY()));
        gr.setGradientFill (juce::ColourGradient (Colour (0xff0a0a0b), 0.0f, 0.0f, Colour (0xff1f1f22), 0.0f, h, false));
        gr.fillRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 2.0f);
        gr.setColour (juce::Colours::black);
        gr.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 2.0f, 1.0f);
        gr.setColour (Colour (0x1affffff));
        for (int i = 1; i < 12; ++i)
            gr.fillRect ((float) i * w / 12.0f, 1.0f, 1.0f, h - 2.0f);
        for (int i = 1; i < 4; ++i)
            gr.fillRect (1.0f, (float) i * h / 4.0f, w - 2.0f, 1.0f);

        int cur[4], pre[4];
        bool differs = false;
        for (int i = 0; i < 4; ++i)
        {
            cur[i] = b[(size_t) i].value();
            pre[i] = b[(size_t) i].preset();
            differs = differs || b[(size_t) i].differs();
        }
        const auto s = shape (cur, w);
        if (differs)
        {
            juce::Path dashed;
            const auto sp = outline (shape (pre, w), false);
            const float dashes[] { 5.0f, 3.0f };
            juce::PathStrokeType (1.6f, juce::PathStrokeType::curved).createDashedStroke (dashed, sp, dashes, 2);
            gr.setColour (kMark);
            gr.fillPath (dashed);
        }
        gr.setColour (Colour (0xff78787e).withAlpha (0.45f));
        gr.fillPath (outline (s, true));
        gr.setColour (Colour (0xffd6d6da));
        gr.strokePath (outline (s, false), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
        const Point<float> dots[] { { s.xa, s.top }, { s.xd, s.ys }, { s.xr, s.bot } };
        for (int i = 0; i < 3; ++i)
        {
            const float r = dragging == i ? 6.0f : 4.4f;
            gr.setColour (dragging == i ? juce::Colours::white : Colour (0xffcfcfd3));
            gr.fillEllipse (dots[i].x - r, dots[i].y - r, 2.0f * r, 2.0f * r);
        }
    }

    juce::String baseTip() const override
    {
        juce::String t = juce::String (g.id == std::string ("env:fenv") ? "Filter" : "Amp") + " envelope: attack " + juce::String (b[0].value())
                         + ", decay " + juce::String (b[1].value()) + ", sustain " + juce::String (b[2].value()) + ", release " + juce::String (b[3].value());
        if (b[0].differs() || b[1].differs() || b[2].differs() || b[3].differs())
            t += "\npreset: " + juce::String (b[0].preset()) + ", " + juce::String (b[1].preset()) + ", " + juce::String (b[2].preset()) + ", " + juce::String (b[3].preset())
                 + "\nDouble-click: back to the preset";
        return t;
    }

    bool diffTip() const override { return false; }

    void down (const juce::MouseEvent& e) override
    {
        int cur[4];
        for (int i = 0; i < 4; ++i)
            cur[i] = b[(size_t) i].value();
        const auto box = loc (g.main);
        const auto s = shape (cur, box.getWidth());
        const Point<float> p = e.position - box.getPosition();
        const Point<float> dots[] { { s.xa, s.top }, { s.xd, s.ys }, { s.xr, s.bot } };
        dragging = -1;
        for (int i = 0; i < 3; ++i)
            if (dots[i].getDistanceFrom (p) <= 11.0f)
            {
                dragging = i;
                break;
            }
        if (dragging < 0)
            return;
        for (auto& x : b)
            x.begin();
        repaint();
    }

    void drag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        int cur[4];
        for (int i = 0; i < 4; ++i)
            cur[i] = b[(size_t) i].value();
        const auto box = loc (g.main);
        const auto s = shape (cur, box.getWidth());
        const Point<float> p = e.position - box.getPosition();
        const auto to127 = [&s] (float x) { return juce::jlimit (0, 127, (int) std::lround (x / s.seg * 127.0f)); };
        if (dragging == 0)
            b[0].set (to127 (p.x - s.x0));
        else if (dragging == 1)
        {
            b[1].set (to127 (p.x - s.xa));
            b[2].set (juce::jlimit (0, 127, (int) std::lround ((s.bot - p.y) / (s.bot - s.top) * 127.0f)));
        }
        else
            b[3].set (to127 (p.x - s.xs));
        repaint();
    }

    void up (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            for (auto& x : b)
                x.end();
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        for (auto& x : b)
            x.hasPreset() ? x.put (x.preset()) : x.toDefault();
        repaint();
    }

    Rectangle<float> hot() const override { return loc (g.main); }

private:
    int dragging = -1;
};

std::unique_ptr<Cell> makeCell (Trigon6Processor& proc, const t6ui::CellGeo& geo, MenuOverlay& menu)
{
    const std::string id = geo.id;
    if (id.rfind ("env:", 0) == 0)
    {
        const std::string prefix = id == "env:fenv" ? "fenv_" : "vca_";
        std::vector<Binding> ids;
        for (const char* n : { "attack", "decay", "sustain", "release" })
            ids.emplace_back (proc, prefix + n);
        return std::make_unique<EnvCell> (geo, std::move (ids), menu);
    }
    Binding own (proc, id);
    jassert (own.ok());  // PanelLayout names an id that ParamTable does not have
    if (! own.ok())
        return nullptr;
    std::vector<Binding> list { own };
    switch (styleOf (*own.def))
    {
        case Style::Knob:
        {
            const bool effect = id.rfind ("fxa_param", 0) == 0 || id.rfind ("fxb_param", 0) == 0;
            if (id == "lfo_freq")
                list.emplace_back (proc, "lfo_sync");
            if (effect)
            {
                const std::string fx = id.substr (0, 3);
                list.emplace_back (proc, fx + "_type");
                list.emplace_back (proc, fx + "_sync");
            }
            return std::make_unique<KnobCell> (geo, std::move (list), menu, effect);
        }
        case Style::Button: return std::make_unique<ButtonCell> (geo, std::move (list), menu);
        case Style::Saw: return std::make_unique<SawCell> (geo, std::move (list), menu);
        case Style::Column: return std::make_unique<ListCell> (geo, std::move (list), menu, Style::Column);
        case Style::Clock: return std::make_unique<ListCell> (geo, std::move (list), menu, Style::Clock);
        case Style::Row: return std::make_unique<ListCell> (geo, std::move (list), menu, Style::Row);
        case Style::Octave: return std::make_unique<OctaveCell> (geo, std::move (list), menu);
        case Style::Window: return std::make_unique<WindowCell> (geo, std::move (list), menu);
        case Style::Env: break;
    }
    return nullptr;
}
} // namespace

// The canvas: section frames and titles, the cells on top, the menu over everything.
class PanelView::Canvas : public juce::Component, private juce::Timer
{
public:
    explicit Canvas (Trigon6Processor& proc)
    {
        const auto& geo = t6ui::panelGeo();
        menu = std::make_unique<MenuOverlay>();
        setSize ((int) std::ceil (geo.width), (int) std::ceil (geo.height));
        for (const auto& c : geo.cells)
            if (auto cell = makeCell (proc, c, *menu))
            {
                addAndMakeVisible (*cell);
                cells.push_back (std::move (cell));
            }
        addChildComponent (*menu);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (kPanel);
        for (const auto& s : t6ui::panelGeo().sections)
        {
            const auto f = rc (s.frame);
            g.setColour (kSection);
            g.fillRect (f);
            g.setColour (kEdge);
            g.drawRect (f, 1.0f);
            const float px = s.big ? 15.0f : 11.0f;
            auto font = juce::Font { juce::FontOptions (hw::titleFont (px).getTypefacePtr()).withPointHeight (px) };
            font.setExtraKerningFactor ((s.big ? 0.24f : 0.2f) * px / font.getHeight());
            g.setColour (kTitle);
            g.setFont (font);
            g.drawText (juce::String (s.title).toUpperCase(), rc (s.head).withTrimmedTop (s.big ? 6.0f : 4.0f), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        if (menu != nullptr)
            menu->setBounds (getLocalBounds());
    }

    void activate (const juce::String& id)
    {
        for (auto& c : cells)
            if (id == c->id())
                c->activate();
    }

private:
    void timerCallback() override
    {
        for (auto& c : cells)
            c->tick();
    }

    std::vector<std::unique_ptr<Cell>> cells;
    std::unique_ptr<MenuOverlay> menu;
};

PanelView::PanelView (Trigon6Processor& proc) : canvas (std::make_unique<Canvas> (proc))
{
    addAndMakeVisible (*canvas);
}

PanelView::~PanelView() = default;

void PanelView::activate (const juce::String& id)
{
    canvas->activate (id);
}

juce::Point<int> PanelView::designSize()
{
    const auto& g = t6ui::panelGeo();
    return { (int) std::ceil (g.width), (int) std::ceil (g.height) };
}

void PanelView::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
}

void PanelView::resized()
{
    const auto size = designSize();
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const float s = std::min ((float) getWidth() / (float) size.x, (float) getHeight() / (float) size.y);
    const float ox = ((float) getWidth() - s * (float) size.x) / 2.0f, oy = ((float) getHeight() - s * (float) size.y) / 2.0f;
    canvas->setBounds (0, 0, size.x, size.y);
    canvas->setTransform (juce::AffineTransform::scale (s).translated (ox, oy));
}

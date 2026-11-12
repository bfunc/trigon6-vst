#include "HardwareLook.h"

#include "BinaryData.h"

namespace hw
{
namespace
{
juce::Typeface::Ptr michroma()
{
    static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf,
                                                              (size_t) BinaryData::MichromaRegular_ttfSize);
    return tf;
}

juce::Typeface::Ptr shareTechMono()
{
    static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf,
                                                              (size_t) BinaryData::ShareTechMonoRegular_ttfSize);
    return tf;
}
} // namespace

juce::Typeface::Ptr archivoNarrow (int weight)
{
    static auto medium = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoNarrowMedium_ttf, (size_t) BinaryData::ArchivoNarrowMedium_ttfSize);
    static auto semi = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoNarrowSemiBold_ttf, (size_t) BinaryData::ArchivoNarrowSemiBold_ttfSize);
    static auto bold = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoNarrowBold_ttf, (size_t) BinaryData::ArchivoNarrowBold_ttfSize);
    return weight >= 700 ? bold : weight >= 600 ? semi : medium;
}

juce::Typeface::Ptr archivo (int weight)
{
    static auto medium = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoMedium_ttf, (size_t) BinaryData::ArchivoMedium_ttfSize);
    static auto semi = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoSemiBold_ttf, (size_t) BinaryData::ArchivoSemiBold_ttfSize);
    return weight >= 600 ? semi : medium;
}

juce::Font titleFont (float height)
{
    return juce::Font (juce::FontOptions (michroma()).withHeight (height)).withExtraKerningFactor (0.12f);
}

juce::Font segmentFont (float height)
{
    return juce::Font (juce::FontOptions (shareTechMono()).withHeight (height)).withExtraKerningFactor (0.05f);
}

juce::Font labelFont (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold)).withExtraKerningFactor (0.05f);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float d, bool on)
{
    if (on)
    {
        g.setColour (col::ledOn.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (d * 2.2f, d * 2.2f).withCentre (c));
    }
    g.setColour (on ? col::ledOn : col::ledOff);
    g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
}

void drawPanelButton (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool highlighted, bool down)
{
    if (on)
    {
        g.setColour (col::buttonOnBottom.withAlpha (0.28f));
        g.fillRoundedRectangle (r.expanded (4.0f), 6.0f);
    }
    auto top = on ? col::buttonOnTop : col::buttonOffTop;
    auto bottom = on ? col::buttonOnBottom : col::buttonOffBottom;
    if (highlighted)
    {
        top = top.brighter (0.15f);
        bottom = bottom.brighter (0.15f);
    }
    if (down)
        std::swap (top, bottom);
    g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void drawWood (juce::Graphics& g, juce::Rectangle<float> r, bool leftSide)
{
    juce::ColourGradient grad (leftSide ? col::woodDark : col::woodLight, r.getX(), 0.0f,
                               leftSide ? col::woodLight : col::woodDark, r.getRight(), 0.0f, false);
    grad.addColour (0.5, col::woodLight.darker (0.08f));
    g.setGradientFill (grad);
    g.fillRect (r);
    // a few faint grain lines
    g.setColour (col::woodDark.withAlpha (0.25f));
    for (int i = 1; i < 4; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / 4.0f;
        g.drawLine (x, r.getY(), x + (leftSide ? 1.0f : -1.0f), r.getBottom(), 0.6f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine (leftSide ? r.getRight() - 0.5f : r.getX() + 0.5f, r.getY(), leftSide ? r.getRight() - 0.5f : r.getX() + 0.5f,
                r.getBottom(), 2.0f);
}

Look::Look()
{
    using C = juce::Colour;
    setColourScheme ({ C (0xff0e0e0f), C (0xff1c1c1e), C (0xff1c1c1e), C (0xff3a3a3c), C (0xffe8e8e8),
                       C (0xff2e2e30), C (0xffffffff), C (0xffe2400f), C (0xffe8e8e8) });
    setColour (juce::Slider::textBoxTextColourId, col::value);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::buttonOnBottom.withAlpha (0.5f));
    setColour (juce::TextButton::textColourOffId, col::label);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::buttonOnBottom);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1c1c1e));
    setColour (juce::TooltipWindow::textColourId, col::label);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float d = juce::jmin (area.getWidth(), area.getHeight()) - 12.0f;
    if (d < 10.0f)
        return;
    const float r = d / 2.0f;
    const auto c = area.getCentre();

    // tick marks around the knob, as printed on the panel
    g.setColour (col::value.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const auto p1 = c.getPointOnCircumference (r + 2.5f, a);
        const auto p2 = c.getPointOnCircumference (r + 5.5f, a);
        g.drawLine ({ p1, p2 }, 1.0f);
    }

    const auto body = juce::Rectangle<float> (d, d).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (body.translated (0.0f, 2.5f).expanded (1.0f));
    g.setColour (juce::Colour (0xff1a1a1b));
    g.fillEllipse (body.expanded (1.5f));
    juce::ColourGradient grad (juce::Colour (0xff3c3c3e), c.x - r * 0.3f, c.y - r * 0.4f,
                               juce::Colour (0xff0b0b0c), c.x + r * 0.6f, c.y + r * 0.7f, true);
    g.setGradientFill (grad);
    g.fillEllipse (body);

    // the white pointer line
    const float a = start + pos * (end - start);
    const auto inner = c.getPointOnCircumference (r * 0.30f, a);
    const auto outer = c.getPointOnCircumference (r * 0.86f, a);
    g.setColour (juce::Colour (0xfff4f4f4));
    g.drawLine ({ inner, outer }, juce::jmax (2.0f, d / 16.0f));
}

juce::Label* Look::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (11.0f));
    l->setColour (juce::Label::textColourId, col::value);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    // A panel toggle: a square button with no text that glows when on.
    if (b.getButtonText().isEmpty() && b.getClickingTogglesState())
    {
        drawPanelButton (g, r.reduced (4.0f), b.getToggleState(), highlighted, down);
        return;
    }
    // Any other button (Send all, Check, Fetch ...): a dark hardware key.
    auto top = juce::Colour (0xff3a3a3c), bottom = juce::Colour (0xff252527);
    if (b.getToggleState())
    {
        top = col::buttonOnTop;
        bottom = col::buttonOnBottom;
    }
    if (highlighted)
    {
        top = top.brighter (0.12f);
        bottom = bottom.brighter (0.12f);
    }
    if (down)
        std::swap (top, bottom);
    if (! b.isEnabled())
    {
        top = top.withMultipliedAlpha (0.5f);
        bottom = bottom.withMultipliedAlpha (0.5f);
    }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (0xff4a4a4e));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    if (b.getButtonText().isEmpty())
        return;
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1, 0.7f);
}

juce::Font Look::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return labelFont (juce::jmin (13.0f, (float) buttonHeight * 0.45f));
}

void Look::drawComboBox (juce::Graphics& g, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox& box)
{
    if (! box.getProperties().contains (windowProperty))
    {
        LookAndFeel_V4::drawComboBox (g, w, h, down, bx, by, bw, bh, box);
        return;
    }
    const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);
    g.setColour (col::window);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colour (box.isMouseOver (true) ? 0xff4a4a4e : 0xff2a2a2a));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    // inner shadow along the top, like a recessed window
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRect (r.withHeight (6.0f).reduced (1.0f, 0.0f));
}

juce::Font Look::getComboBoxFont (juce::ComboBox& box)
{
    if (box.getProperties().contains (windowProperty))
        return segmentFont (juce::jmin (17.0f, (float) box.getHeight() * 0.68f));
    return LookAndFeel_V4::getComboBoxFont (box);
}

void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    if (! box.getProperties().contains (windowProperty))
    {
        LookAndFeel_V4::positionComboBoxText (box, label);
        return;
    }
    label.setBounds (box.getLocalBounds().reduced (2, 1));
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centred);
    label.setMinimumHorizontalScale (0.6f);
}

void Look::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (2.0f, 4.0f);
    const bool front = b.isFrontTab();
    if (front)
    {
        g.setColour (juce::Colour (0xffe8e8e8));
        g.fillRoundedRectangle (r, 3.0f);
    }
    else
    {
        g.setColour (juce::Colour (over ? 0xff4a4a4e : 0xff2c2c2e));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
    g.setColour (front ? juce::Colour (0xff111111) : juce::Colour (0xffb8b8bc));
    g.setFont (labelFont (12.0f).withExtraKerningFactor (0.1f));
    g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred, false);
}

int Look::getTabButtonBestWidth (juce::TabBarButton& b, int)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (labelFont (12.0f).withExtraKerningFactor (0.1f), b.getButtonText().toUpperCase(), 0.0f, 0.0f);
    return juce::roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + 40;
}

void Look::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour (col::strip);
    g.fillRect (bar.getLocalBounds());
    g.setColour (juce::Colour (0xff3a3a3c));
    g.fillRect (bar.getLocalBounds().removeFromBottom (1));
}

} // namespace hw

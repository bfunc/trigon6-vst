// The look of the Trigon-6 front panel: black knobs with a white pointer, square buttons that glow orange
// when on, small red LEDs, red seven-segment-style windows, and a wide display face for section titles.
// Reference design: docs/panel-restyle-spec.md. Fonts (SIL OFL) are in assets/fonts.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hw
{
namespace col
{
const juce::Colour panel { 0xff111112 };
const juce::Colour panelTop { 0xff161617 };
const juce::Colour strip { 0xff0b0b0c };     // the bottom performance strip and the top bar
const juce::Colour line { 0xff5a5a5e };      // section dividers
const juce::Colour label { 0xffe6e6e6 };
const juce::Colour value { 0xff9c9ca2 };
const juce::Colour title { 0xffefefef };
const juce::Colour ledOn { 0xffff3b1f };
const juce::Colour ledOff { 0xff4a1410 };
const juce::Colour buttonOnTop { 0xffff7a2e };
const juce::Colour buttonOnBottom { 0xffe2400f };
const juce::Colour buttonOffTop { 0xff3a3a3c };
const juce::Colour buttonOffBottom { 0xff232325 };
const juce::Colour window { 0xff000000 };
const juce::Colour windowText { 0xffff3b1f };
const juce::Colour woodLight { 0xffe3cfa8 };
const juce::Colour woodDark { 0xffa88a5f };
} // namespace col

juce::Font titleFont (float height);    // Michroma, letter-spaced
juce::Font segmentFont (float height);  // Share Tech Mono, for the red windows
juce::Font labelFont (float height);    // small bold caps under the controls

void drawLed (juce::Graphics&, juce::Point<float> centre, float diameter, bool on);
void drawPanelButton (juce::Graphics&, juce::Rectangle<float>, bool on, bool highlighted, bool down);
void drawWood (juce::Graphics&, juce::Rectangle<float>, bool leftSide);

// A ComboBox drawn as a red window (effect type, glide mode, key mode ...): set this property on it.
inline const juce::Identifier windowProperty { "hwWindow" };

class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool over, bool down) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int depth) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) override {}
};

} // namespace hw

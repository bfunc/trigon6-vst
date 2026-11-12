// The Panel tab: every parameter as on the panel mockup (docs/panel-mockup.html). The controls are placed on a canvas
// of fixed design size (core PanelLayout, measured from the mockup) which is scaled to fit the tab.
#pragma once

#include "PluginProcessor.h"

#include <memory>

class PanelView : public juce::Component
{
public:
    explicit PanelView (Trigon6Processor&);
    ~PanelView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Point<int> designSize();  // the canvas at scale 1
    void activate (const juce::String& parameterId);  // as a click on that control (for the picture tool)

private:
    class Canvas;
    std::unique_ptr<Canvas> canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelView)
};

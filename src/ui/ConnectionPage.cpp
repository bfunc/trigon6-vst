#include "ConnectionPage.h"

#include "Theme.h"
#include "core/Globals.h"

namespace
{
constexpr int kAutoId = 1;  // combo item "Auto"; ports start at 2

juce::Colour colourOf (t6::Health h)
{
    switch (h)
    {
        case t6::Health::Ok:      return theme::ok;
        case t6::Health::Warn:    return theme::warn;
        case t6::Health::Bad:     return theme::bad;
        case t6::Health::Unknown: break;
    }
    return theme::unknown;
}

void fillPorts (juce::ComboBox& box, const juce::StringArray& names, const juce::String& choice)
{
    box.clear (juce::dontSendNotification);
    box.addItem ("Auto (Trigon 6)", kAutoId);
    auto all = names;
    if (choice.isNotEmpty() && ! all.contains (choice))
        all.add (choice);  // keep a saved choice visible while that port is missing
    for (int i = 0; i < all.size(); ++i)
        box.addItem (all[i] + (names.contains (all[i]) ? "" : "  (not found)"), i + 2);
    const int idx = all.indexOf (choice);
    box.setSelectedId (choice.isEmpty() || idx < 0 ? kAutoId : idx + 2, juce::dontSendNotification);
}

juce::String chosenName (const juce::ComboBox& box, const juce::StringArray& names, const juce::String& saved)
{
    const int id = box.getSelectedId();
    if (id <= kAutoId)
        return {};
    auto all = names;
    if (saved.isNotEmpty() && ! all.contains (saved))
        all.add (saved);
    return all[id - 2];
}

juce::TextLayout layoutText (const juce::String& title, const juce::String& detail, float width)
{
    juce::AttributedString as;
    as.append (title + "\n", juce::FontOptions (14.5f, juce::Font::bold), theme::text);
    as.append (detail, juce::FontOptions (13.0f), theme::dim);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (as, width);
    return tl;
}
} // namespace

ConnectionPage::ConnectionPage (Trigon6Processor& p) : proc (p)
{
    auto setupLabel = [this] (juce::Label& l, const char* text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        l.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (l);
    };
    setupLabel (outputLabel, "Send NRPN");
    setupLabel (inPortLabel, "Input port");
    setupLabel (outPortLabel, "Output port");

    outputMode.addItem ("Direct to the synth's port", 1);
    outputMode.addItem ("Through the host (DAW MIDI out)", 2);
    outputMode.setSelectedId (proc.isHostOutput() ? 2 : 1, juce::dontSendNotification);
    outputMode.onChange = [this]
    {
        proc.setHostOutput (outputMode.getSelectedId() == 2);
        outPort.setEnabled (! proc.isHostOutput());
    };
    addAndMakeVisible (outputMode);

    inPort.onChange = [this] { applyPorts(); };
    outPort.onChange = [this] { applyPorts(); };
    outPort.setEnabled (! proc.isHostOutput());
    addAndMakeVisible (inPort);
    addAndMakeVisible (outPort);
    refreshPortLists();

    check.setTooltip ("Ask the synth who it is, and request its global settings and current sound (SysEx). Changes nothing on the synth.");
    check.onClick = [this] { proc.getLink().probe(); };
    addAndMakeVisible (check);

    timerCallback();
    startTimerHz (5);
}

void ConnectionPage::refreshPortLists()
{
    const auto ins = SynthLink::inputNames();
    const auto outs = SynthLink::outputNames();
    if (ins == inNames && outs == outNames && inPort.getNumItems() > 0)
        return;
    inNames = ins;
    outNames = outs;
    fillPorts (inPort, inNames, proc.getLink().getInputChoice());
    fillPorts (outPort, outNames, proc.getLink().getOutputChoice());
}

void ConnectionPage::applyPorts()
{
    auto& link = proc.getLink();
    proc.setPortChoice (chosenName (inPort, inNames, link.getInputChoice()),
                        chosenName (outPort, outNames, link.getOutputChoice()));
}

void ConnectionPage::timerCallback()
{
    if (++ticks % 10 == 0)
        refreshPortLists();

    snap = proc.getLink().snapshot (proc.getMidiChannel());
    const bool was = report.allOk;
    report = t6::evaluate (snap);
    if (was != report.allOk && onReportChanged)
        onReportChanged();
    repaint (checklistArea);
    repaint (globalsArea);
}

void ConnectionPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto bar = r.removeFromTop (30);
    outputLabel.setBounds (bar.removeFromLeft (80));
    outputMode.setBounds (bar.removeFromLeft (250).reduced (4, 0));
    inPortLabel.setBounds (bar.removeFromLeft (90));
    inPort.setBounds (bar.removeFromLeft (220).reduced (4, 0));
    outPortLabel.setBounds (bar.removeFromLeft (90));
    outPort.setBounds (bar.removeFromLeft (220).reduced (4, 0));
    bar.removeFromLeft (12);
    check.setBounds (bar.removeFromLeft (100));

    r.removeFromTop (12);
    checklistArea = r.removeFromLeft ((int) (r.getWidth() * 0.42f));
    r.removeFromLeft (12);
    globalsArea = r;
}

void ConnectionPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    paintChecklist (g, checklistArea);
    paintGlobals (g, globalsArea);
}

void ConnectionPage::paintChecklist (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto box = area.toFloat();
    g.setColour (theme::section);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (box, 6.0f, 1.0f);

    auto r = area.reduced (12);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.setColour (report.allOk ? theme::ok : theme::text);
    g.drawText (report.allOk ? "CONNECTED" : "CONNECTION STATUS", r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (6);

    for (const auto& item : report.items)
    {
        const float textX = (float) r.getX() + 22.0f;
        const float textW = (float) r.getWidth() - 22.0f;
        auto tl = layoutText (item.title, item.detail, textW);
        const float h = tl.getHeight();
        if ((float) r.getHeight() < h)
            break;
        g.setColour (colourOf (item.health));
        g.fillEllipse ((float) r.getX() + 2.0f, (float) r.getY() + 4.0f, 11.0f, 11.0f);
        tl.draw (g, { textX, (float) r.getY(), textW, h });
        r.removeFromTop ((int) std::ceil (h) + 8);
    }
}

void ConnectionPage::paintGlobals (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto box = area.toFloat();
    g.setColour (theme::section);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (box, 6.0f, 1.0f);

    auto r = area.reduced (12);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("GLOBALS ON THE SYNTH: WHICH BUTTON IS WHAT", r.removeFromTop (24), juce::Justification::centredLeft);

    {
        auto tl = layoutText ("How to change a setting",
                              "1. Press GLOBALS once: red LED = top row. Press it again: yellow LED = bottom row.\n"
                              "2. Press the program button 0-9 of the setting (the numbers below).\n"
                              "3. BANK steps the value up, TENS steps it down.  4. Press GLOBALS to leave.",
                              (float) r.getWidth());
        tl.draw (g, r.removeFromTop ((int) std::ceil (tl.getHeight())).toFloat());
    }
    r.removeFromTop (10);

    // Footer: where the values come from, and the raw dump (to check the dump's order on the synth)
    juce::String footer;
    if (snap.globalsMs >= 0)
    {
        footer << "Global dump received " << juce::String ((snap.nowMs - snap.globalsMs) / 1000.0, 0) << " s ago. Raw values: ";
        for (size_t i = 0; i < snap.globalsDump.size(); ++i)
            footer << snap.globalsDump[i] << (i + 1 < snap.globalsDump.size() ? " " : "");
        footer << ".  Values marked ? come only from the dump, whose order is not checked yet.";
    }
    else
        footer = "No global dump yet (press Check; needs MIDI SysEx = USB). Values also appear when you change a setting on the synth.";
    auto footerTl = layoutText ("", footer, (float) r.getWidth());
    auto footerArea = r.removeFromBottom ((int) std::ceil (footerTl.getHeight()));
    footerTl.draw (g, footerArea.toFloat());
    r.removeFromBottom (8);

    const int rowGap = 10;
    const int titleH = 22;
    const int rowH = (r.getHeight() - 2 * titleH - rowGap) / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto title = r.removeFromTop (titleH);
        g.setColour (row == 0 ? theme::led : theme::lowerRow);
        g.fillEllipse ((float) title.getX(), (float) title.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (row == 0 ? "TOP ROW  (GLOBALS pressed once, red LED)" : "BOTTOM ROW  (GLOBALS pressed twice, yellow LED)",
                    title.withTrimmedLeft (16), juce::Justification::centredLeft);

        auto cells = r.removeFromTop (rowH);
        const float cellW = (float) cells.getWidth() / 10.0f;
        for (int b = 0; b < 10; ++b)
        {
            const auto* def = t6::globalAt (row, b);
            auto c = juce::Rectangle<float> ((float) cells.getX() + b * cellW, (float) cells.getY(), cellW, (float) cells.getHeight()).reduced (3.0f);
            const bool required = def != nullptr && t6::isRequiredGlobal (*def);
            const auto health = def != nullptr ? t6::globalHealth (snap, *def) : t6::Health::Unknown;

            g.setColour (theme::background);
            g.fillRoundedRectangle (c, 5.0f);
            g.setColour (required ? colourOf (health) : theme::border);
            g.drawRoundedRectangle (c, 5.0f, required ? 2.5f : 1.0f);

            auto inner = c.reduced (4.0f);
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
            g.drawText (juce::String (b), inner.removeFromTop (30.0f), juce::Justification::centred);
            if (def == nullptr)
                continue;

            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawFittedText (def->name, inner.removeFromTop (32.0f).toNearestInt(), juce::Justification::centred, 2);

            const auto reading = def->nrpn >= 0 ? t6::readGlobal (snap, def->nrpn) : t6::GlobalReading {};
            g.setFont (juce::FontOptions (13.0f));
            g.setColour (reading.known ? (required ? colourOf (health) : theme::text) : theme::dim);
            const juce::String now = reading.known
                                         ? juce::String (t6::globalValueName (*def, reading.raw))
                                               + (reading.fromDump && ! t6::kDumpOrderVerified ? " ?" : "")
                                         : juce::String ("-");
            g.drawFittedText ("now: " + now, inner.removeFromTop (18.0f).toNearestInt(), juce::Justification::centred, 1);

            if (required)
            {
                const juce::String want = def->nrpn == t6::kGlobalMidiChannel
                                              ? juce::String (proc.getMidiChannel()) + " / All"
                                              : juce::String (def->wantedText);
                g.setColour (theme::text);
                g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
                g.drawFittedText ("need: " + want, inner.removeFromTop (34.0f).toNearestInt(), juce::Justification::centredTop, 2);
            }
        }
        r.removeFromTop (rowGap);
    }
}

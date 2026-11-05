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

    playNote.setTooltip ("Play middle C for a moment. If you hear it, MIDI reaches the synth (whatever Param Rcv and SysEx are).");
    playNote.onClick = [this] { proc.getLink().playTestNote (proc.getMidiChannel()); };
    heard.onClick = [this] { proc.getLink().answerNoteTest (true); };
    notHeard.onClick = [this] { proc.getLink().answerNoteTest (false); };
    useChannel.onClick = [this]
    {
        if (report.suggestChannel > 0)
            proc.setMidiChannel (report.suggestChannel);
    };
    for (auto* b : { &playNote, &heard, &notHeard, &useChannel })
        addChildComponent (*b);
    playNote.setVisible (true);

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
    updateButtons();
    repaint (checklistArea);
    repaint (globalsArea);
}

void ConnectionPage::updateButtons()
{
    const bool waiting = snap.noteTest == t6::NoteTest::Waiting;
    heard.setVisible (waiting);
    notHeard.setVisible (waiting);
    playNote.setEnabled (snap.hostOutput || snap.outOpen);
    playNote.setColour (juce::TextButton::buttonColourId,
                        report.offerNoteTest && ! waiting ? theme::warn.darker (0.4f) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    useChannel.setVisible (report.suggestChannel > 0);
    useChannel.setButtonText ("Use channel " + juce::String (report.suggestChannel));
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

    r.removeFromTop (8);
    auto tests = r.removeFromTop (30);
    check.setBounds (tests.removeFromLeft (100));
    tests.removeFromLeft (8);
    playNote.setBounds (tests.removeFromLeft (130));
    tests.removeFromLeft (8);
    heard.setBounds (tests.removeFromLeft (100));
    tests.removeFromLeft (4);
    notHeard.setBounds (tests.removeFromLeft (100));
    tests.removeFromLeft (16);
    useChannel.setBounds (tests.removeFromLeft (140));

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

    // Verdict: the one thing to do now
    {
        auto tl = layoutText (report.verdict, report.verdictFix, (float) r.getWidth() - 20.0f);
        const int h = (int) std::ceil (tl.getHeight()) + 16;
        auto card = r.removeFromTop (h);
        g.setColour (theme::background);
        g.fillRoundedRectangle (card.toFloat(), 5.0f);
        g.setColour (colourOf (report.verdictHealth));
        g.fillRoundedRectangle (card.removeFromLeft (6).toFloat(), 3.0f);
        tl.draw (g, card.reduced (8).toFloat().withTrimmedLeft (4.0f));
    }
    r.removeFromTop (10);

    // The ladder: the first failing step shows its fix; blocked steps are dimmed
    bool fixShown = false;
    int n = 0;
    auto drawItem = [&] (const t6::CheckItem& item, const juce::String& prefix)
    {
        const bool showFix = ! fixShown && item.health != t6::Health::Ok && ! item.fix.empty() && ! item.blocked;
        juce::String detail = item.detail;
        if (showFix)
        {
            detail << "\n> " << item.fix;
            fixShown = true;
        }
        if (! item.required && ! item.blocked)
            detail << (item.health == t6::Health::Ok ? "" : "  (optional)");
        const float textX = (float) r.getX() + 22.0f;
        const float textW = (float) r.getWidth() - 22.0f;
        auto tl = layoutText (prefix + item.title, detail, textW);
        const float h = tl.getHeight();
        if ((float) r.getHeight() < h)
            return false;
        if (item.blocked)
            g.beginTransparencyLayer (0.45f);
        g.setColour (colourOf (item.health));
        g.fillEllipse ((float) r.getX() + 2.0f, (float) r.getY() + 4.0f, 11.0f, 11.0f);
        tl.draw (g, { textX, (float) r.getY(), textW, h });
        if (item.blocked)
            g.endTransparencyLayer();
        r.removeFromTop ((int) std::ceil (h) + 7);
        return true;
    };

    for (const auto& item : report.steps)
        if (! drawItem (item, juce::String (++n) + ". "))
            return;

    if (! report.warnings.empty())
    {
        r.removeFromTop (6);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("ALSO NOTE", r.removeFromTop (20), juce::Justification::centredLeft);
        fixShown = false;
        for (const auto& w : report.warnings)
        {
            fixShown = false;  // every warning shows its own advice
            if (! drawItem (w, ""))
                return;
        }
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
        footer << ".  Values marked ? come only from the dump, whose order is not checked yet. A white frame marks the button to press now.";
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

            const bool highlighted = row == report.highlightRow && b == report.highlightButton;
            g.setColour (highlighted ? theme::section.brighter (0.25f) : theme::background);
            g.fillRoundedRectangle (c, 5.0f);
            g.setColour (highlighted ? theme::text : required ? colourOf (health) : theme::border);
            g.drawRoundedRectangle (c, 5.0f, highlighted ? 4.0f : required ? 2.5f : 1.0f);

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
                                         ? juce::String (reading.text)
                                               + (reading.source == t6::Source::Dump && ! t6::kDumpOrderVerified ? " ?" : "")
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

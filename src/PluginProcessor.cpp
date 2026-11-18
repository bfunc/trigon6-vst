#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "core/Nrpn.h"
#include "core/ParamTable.h"
#include "core/PresetDiff.h"
#include "core/Presets.h"

namespace
{
const juce::Identifier kChannel { "midiChannel" };
const juce::Identifier kOutputMode { "outputMode" };  // "host" or "direct"
const juce::Identifier kInPort { "inputPort" };       // "" = automatic
const juce::Identifier kOutPort { "outputPort" };
const juce::Identifier kResendOsc1 { "resendOsc1OnPlay" };
constexpr double kOsc1ResendDelayMs = 10.0;
const int kOsc1OctaveNrpn = 0;
}

juce::AudioProcessorValueTreeState::ParameterLayout Trigon6Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : t6::paramTable())
    {
        const juce::ParameterID id { p.id, 1 };
        const auto attrs = juce::AudioParameterIntAttributes().withStringFromValueFunction (
            [&p] (int v, int) { return juce::String (t6::formatValue (p, v)); });

        switch (p.kind)
        {
            case t6::Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, p.name, false));
                break;
            case t6::Kind::Choice:
            {
                juce::StringArray names;
                for (const auto& c : p.choices)
                    names.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, p.name, names, 0));
                break;
            }
            case t6::Kind::Bipolar:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, p.name, p.min, p.max, p.center, attrs));
                break;
            case t6::Kind::Int:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, p.name, p.min, p.max, p.min, attrs));
                break;
        }
    }
    return layout;
}

Trigon6Processor::Trigon6Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Trigon6", createLayout()),
      queue (t6::paramTable().size()),
      link (SynthLink::acquire()),
      projectWins (! juce::JUCEApplicationBase::isStandaloneApp())
{
    for (const auto& p : t6::paramTable())
        raw.push_back (apvts.getRawParameterValue (p.id));
    apvts.state.setProperty (kChannel, 1, nullptr);

    // A new plugin (or the standalone app) talks to the synth's port itself; a saved state brings its own choice
    // back (setStateInformation).
    setHostOutput (false);
    incomingSeen = link->incomingSeq();
    pcSeen = link->lastProgramChange().seq;
    pump.startThread();
    startTimerHz (30);
}

Trigon6Processor::~Trigon6Processor()
{
    stopTimer();
    pump.stopThread (1000);
}

void Trigon6Processor::meetTheSynth (double nowMs)
{
    const auto s = link->snapshot (midiChannel.load());
    const bool connected = s.inOpen && (s.outOpen || s.hostOutput);
    if (connected && ! wasConnected)
    {
        if (projectWins && projectLoaded.load())
            pushAtMs = juce::jmax (pushAtMs.load(), nowMs + kMeetDelayMs);
        else if (autoGetSoundMs < 0 && ! soundPending)
            autoGetSoundMs = nowMs + kMeetDelayMs;
    }
    wasConnected = connected;

    const double due = pushAtMs.load();
    if (due >= 0)
        autoGetSoundMs = -1;  // a project arrived: its sound wins over a read planned before it
    if (due >= 0 && nowMs >= due && connected && ! soundPending)
    {
        pushAtMs = -1;
        requestSendAll();
        soundStatus = "Sent the project's sound to the synth (its stored programs are not touched)";
    }
}

void Trigon6Processor::timerCallback()
{
    meetTheSynth (juce::Time::getMillisecondCounterHiRes());

    // A program picked on the synth's panel: that slot is now the origin; read the new sound shortly.
    const auto pc = link->lastProgramChange();
    if (pc.seq != pcSeen)
    {
        pcSeen = pc.seq;
        if (pc.channel == midiChannel.load())
        {
            setSlot (pc.bank, pc.program);
            autoGetSoundMs = juce::Time::getMillisecondCounterHiRes() + 300.0;
        }
    }
    if (autoGetSoundMs > 0 && juce::Time::getMillisecondCounterHiRes() >= autoGetSoundMs && ! soundPending)
    {
        autoGetSoundMs = -1;
        requestGetSound();
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto editSeq = link->editBufferSeq();
    if (soundPending)
    {
        if (editSeq != soundSeq)
        {
            soundPending = false;
            watchPending = false;  // the same dump answers a watch request too; Get sound takes it whole
            applySound (link->editBuffer());
        }
        else if (nowMs - soundAskedMs > 3000)
        {
            soundPending = false;
            soundStatus = "No sound came back. Check the Connection tab (MIDI SysEx = USB).";
        }
    }
    if (watchPending)
    {
        if (editSeq != watchSeq)
        {
            watchPending = false;
            takeWatched (link->editBuffer(), nowMs);
        }
        else if (nowMs - watchAskedMs > 3000)
        {
            watchPending = false;
            ++watchStats.timeouts;
            ++watchFailures;  // ask less often until the synth answers again
        }
    }
    watchTheSynth (nowMs);

    std::vector<SynthLink::InEvent> events;
    incomingSeen = link->readIncoming (incomingSeen, events);
    if (events.empty())
        return;

    const auto& table = t6::paramTable();
    const int ch = midiChannel.load();
    // Under queueLock, so the sender sees the new value and the synced mark together and sends nothing back.
    const juce::ScopedLock sl (queueLock);
    for (const auto& e : events)
    {
        if (e.channel != ch)
            continue;
        const int i = t6::indexOfNrpn (e.param);
        if (i < 0)
            continue;
        const auto& p = table[(size_t) i];
        // A smooth knob after a quiet spell: the user is shaping the sound on the synth, not in Write mode (there only
        // stepped knobs send), so it's safe to read the whole sound now (it may have been Init'ed).
        const bool smooth = (p.kind == t6::Kind::Int || p.kind == t6::Kind::Bipolar) && p.max - p.min >= 100;
        if (smooth && nowMs - synthQuietSinceMs >= 2000.0)
            watchWanted = true;
        synthQuietSinceMs = nowMs;
        const int current = (int) std::lround (raw[(size_t) i]->load()) + (p.kind == t6::Kind::Choice ? p.min : 0);
        const int v = juce::jlimit (p.min, p.max, t6::valueAfterNrpn (p, current, e.param, e.value));
        applyFromSynth ((size_t) i, v);
        if (watchPending && (size_t) i < valuesAtWatch.size())
            valuesAtWatch[(size_t) i] = v;  // the synth's own move: not an edit the reply could undo
    }
}

void Trigon6Processor::applyFromSynth (size_t i, int v)
{
    const auto& p = t6::paramTable()[i];
    queue.markSynced (i, v);
    if (auto* param = apvts.getParameter (p.id))
    {
        const float plain = (float) (p.kind == t6::Kind::Choice ? v - p.min : v);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }
}

void Trigon6Processor::watchTheSynth (double nowMs)
{
    // Anything the plugin sends (a knob, Compare, a whole sound) restarts the quiet time.
    const int sent = sentCount.load();
    if (sent != sentSeen)
    {
        sentSeen = sent;
        quietSinceMs = nowMs;
    }

    const auto w = watch.load();
    if (w == Watch::Off || ! wasConnected || link->isHostOutput())  // through the host, a request can't go out on time
    {
        watchWanted = false;
        return;
    }
    if (watchPending || soundPending || autoGetSoundMs > 0 || pushAtMs.load() >= 0 || previewing.load())
        return;
    constexpr double kQuietMs = 1000.0;
    if (nowMs - quietSinceMs < kQuietMs)
        return;  // the plugin sent something: let it settle first

    if (watchWanted && nowMs - watchAskedMs < 2000.0)
        watchWanted = false;  // read moments ago: fresh enough
    const bool timed = w == Watch::Every2s || w == Watch::Every250ms;
    const bool timedDue = timed && nowMs - synthQuietSinceMs >= kQuietMs && nowMs >= nextWatchMs;
    if (! watchWanted && ! timedDue)
        return;
    watchWanted = false;

    watchPending = true;
    watchSeq = link->editBufferSeq();
    watchAskedMs = nowMs;
    sentAtWatch = sent;
    valuesAtWatch = currentValues();
    ++watchStats.asked;
    link->requestEditBuffer();
    const double every = w == Watch::Every250ms ? 250.0 : 2000.0;
    nextWatchMs = nowMs + juce::jmin (30000.0, every * (double) (1 << juce::jmin (watchFailures, 4)));
}

void Trigon6Processor::takeWatched (const std::vector<uint8_t>& data, double nowMs)
{
    const double took = nowMs - watchAskedMs;  // up to one timer tick (33 ms) more than the real reply time
    watchStats.lastReplyMs = took;
    watchStats.slowestReplyMs = juce::jmax (watchStats.slowestReplyMs, took);
    ++watchStats.answered;
    watchFailures = 0;

    // Something was edited or sent while the reply was on its way: it may be older than the edit. Skip it; the next
    // request comes after the next quiet second.
    if (sentCount.load() != sentAtWatch || previewing.load() || currentValues() != valuesAtWatch)
    {
        ++watchStats.skipped;
        return;
    }

    const auto& table = t6::paramTable();
    std::vector<std::pair<size_t, int>> changes;
    for (size_t i = 0; i < table.size() && i < valuesAtWatch.size(); ++i)
    {
        const auto& p = table[i];
        const int v = t6::valueFromProgram (p, data);
        if (v < p.min || v > p.max)
            continue;  // not read (-1) or impossible: Get sound reports those
        if (v != valuesAtWatch[i])
            changes.emplace_back (i, v);
    }
    if (changes.empty())
        return;

    {
        // Not osc1Octave: if FL's MIDI Out knocked Osc 1 Octave to +2, the re-send must still restore the old value.
        const juce::ScopedLock sl (queueLock);
        for (const auto& [i, v] : changes)
            applyFromSynth (i, v);
    }
    origin.name = t6::programName (data);
    ++watchStats.taken;
    juce::StringArray names;
    for (const auto& c : changes)
        if (names.size() < 4)
            names.add (table[c.first].name);
    watchStats.lastTaken = juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + juce::String ((int) changes.size())
                         + " (" + names.joinIntoString (", ") + (changes.size() > 4 ? ", ..." : "") + ")";
    soundStatus = "The synth's sound changed on the synth: took " + juce::String ((int) changes.size()) + " values from it";
}

void Trigon6Processor::requestGetSound()
{
    soundSeq = link->editBufferSeq();
    soundPending = true;
    soundAskedMs = juce::Time::getMillisecondCounterHiRes();
    soundStatus = "Asking the synth for its sound...";
    link->requestEditBuffer();
}

void Trigon6Processor::setSlot (int bank, int program)
{
    if (! origin.slotKnown || origin.bank != bank || origin.program != program)
        origin.pcData.clear();
    origin.slotKnown = true;
    origin.bank = bank;
    origin.program = program;
}

void Trigon6Processor::loadProgram (int bank, int program)
{
    link->sendProgramChange (midiChannel.load(), bank, program);
    setSlot (bank, program);
    autoGetSoundMs = juce::Time::getMillisecondCounterHiRes() + 300.0;
}

void Trigon6Processor::noteSavedToSynth (int bank, int program, const std::string& name)
{
    setSlot (bank, program);
    origin.name = name;
    takePresetFromCurrent();
}

void Trigon6Processor::noteSavedToPc (const std::vector<uint8_t>& data)
{
    origin.pcData = data;
    origin.name = t6::programName (data);
    takePresetFromCurrent();
}

std::vector<int> Trigon6Processor::currentValues() const
{
    const auto& table = t6::paramTable();
    std::vector<int> v;
    v.reserve (table.size());
    for (size_t i = 0; i < table.size(); ++i)
    {
        const int x = (int) std::lround (raw[i]->load());
        v.push_back (table[i].kind == t6::Kind::Choice ? table[i].min + x : x);
    }
    return v;
}

int Trigon6Processor::currentValue (size_t i) const
{
    const auto& p = t6::paramTable()[i];
    const int x = (int) std::lround (raw[i]->load());
    return p.kind == t6::Kind::Choice ? p.min + x : x;
}

void Trigon6Processor::takePresetFromCurrent() { presetValues = currentValues(); }

int Trigon6Processor::presetDiffCount() const { return (int) t6::differingParams (presetValues, currentValues()).size(); }

void Trigon6Processor::setParamTo (size_t i, int value)
{
    const auto& p = t6::paramTable()[i];
    if (auto* param = apvts.getParameter (p.id))
    {
        const float plain = (float) (p.kind == t6::Kind::Choice ? value - p.min : value);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
        param->endChangeGesture();
    }
}

void Trigon6Processor::resetToPreset (size_t i)
{
    if (hasPreset() && i < presetValues.size())
        setParamTo (i, presetValues[i]);
}

void Trigon6Processor::beginCompare()
{
    if (previewing.load() || ! hasPreset() || presetDiffCount() == 0)
        return;
    previewValues = presetValues;
    previewing.store (true, std::memory_order_release);
}

void Trigon6Processor::endCompare() { previewing.store (false, std::memory_order_release); }

void Trigon6Processor::resetAllToPreset()
{
    for (const auto i : t6::differingParams (presetValues, currentValues()))
        setParamTo (i, presetValues[i]);
}

void Trigon6Processor::applySound (const std::vector<uint8_t>& data)
{
    origin.name = t6::programName (data);
    const auto& table = t6::paramTable();
    int set = 0;
    juce::StringArray outOfRange, skipped;
    const juce::ScopedLock sl (queueLock);
    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto& p = table[i];
        const int v = t6::valueFromProgram (p, data);
        if (v < 0)
        {
            skipped.add (p.name);
            continue;
        }
        if (v < p.min || v > p.max)
        {
            outOfRange.add (juce::String (p.name) + " " + juce::String (v));
            continue;
        }
        applyFromSynth (i, v);
        if (p.nrpn == kOsc1OctaveNrpn)
            osc1Octave = v;
        ++set;
    }
    takePresetFromCurrent();
    soundStatus = "Got sound: " + juce::String (set) + " set";
    if (! outOfRange.isEmpty())
        soundStatus << "; " << outOfRange.size() << " impossible (" << outOfRange.joinIntoString (", ") << ")";
    if (! skipped.isEmpty())
        soundStatus << "; not read: " << skipped.joinIntoString (", ");
}

void Trigon6Processor::setHostOutput (bool host)
{
    link->setHostOutput (host);
    apvts.state.setProperty (kOutputMode, host ? "host" : "direct", nullptr);
}

void Trigon6Processor::setPortChoice (const juce::String& inputName, const juce::String& outputName)
{
    link->setPortChoice (inputName, outputName);
    apvts.state.setProperty (kInPort, inputName, nullptr);
    apvts.state.setProperty (kOutPort, outputName, nullptr);
}

void Trigon6Processor::setResendOsc1OnPlay (bool on)
{
    resendOsc1OnPlay = on;
    apvts.state.setProperty (kResendOsc1, on, nullptr);
}

template <typename Emit>
void Trigon6Processor::resendOsc1IfDue (Emit&& emit)
{
    const double due = osc1ResendAtMs.load();
    if (due < 0 || juce::Time::getMillisecondCounterHiRes() < due)
        return;
    osc1ResendAtMs = -1;
    const int v = osc1Octave.load();
    if (v < 0 || ! resendOsc1OnPlay.load())
        return;
    emit (midiChannel.load(), kOsc1OctaveNrpn, v);
    ++sentCount;
}

void Trigon6Processor::setMidiChannel (int ch)
{
    ch = juce::jlimit (1, 16, ch);
    midiChannel = ch;
    apvts.state.setProperty (kChannel, ch, nullptr);
}

template <typename Emit>
void Trigon6Processor::runQueue (Emit&& emit)
{
    const auto& table = t6::paramTable();
    // A choice parameter's raw value is the choice index, counted from the NRPN minimum.
    const bool preview = previewing.load (std::memory_order_acquire);
    auto valueOf = [&] (size_t i)
    {
        if (preview && i < previewValues.size())
            return previewValues[i];  // Compare held: the synth plays the preset
        const int v = (int) std::lround (raw[i]->load());
        return table[i].kind == t6::Kind::Choice ? table[i].min + v : v;
    };

    if (needsPrime.exchange (false))
        for (size_t i = 0; i < raw.size(); ++i)
            queue.markSynced (i, valueOf (i));

    if (sendAllRequested.exchange (false))
        queue.markAllDirty();

    for (size_t i = 0; i < raw.size(); ++i)
        queue.setValue (i, valueOf (i));

    const int ch = midiChannel.load();
    queue.collect (juce::Time::getMillisecondCounterHiRes(), [&] (size_t i, int value)
    {
        if (table[i].nrpn == kOsc1OctaveNrpn)
            osc1Octave = value;  // a change made in the plugin is the one to restore from now on
        for (const auto& nv : t6::nrpnValues (table[i], value))
        {
            emit (ch, nv.nrpn, nv.value);
            ++sentCount;
        }
    });
}

void Trigon6Processor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    midi.clear();  // incoming MIDI is not passed on (the plugin reads the synth through its own input port)

    // Transport start or stop: schedule the Osc 1 Octave restore (sent by the pump in direct mode, below in host mode).
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            playing = pos->getIsPlaying();
    if (playing != wasPlaying && resendOsc1OnPlay.load())
        osc1ResendAtMs = juce::Time::getMillisecondCounterHiRes() + kOsc1ResendDelayMs;
    wasPlaying = playing;

    if (! link->isHostOutput())
        return;

    link->drainHostOutput (midi);
    const juce::ScopedTryLock sl (queueLock);
    if (! sl.isLocked())
        return;  // a mode switch is in progress; the next block sends
    auto toHost = [&] (int ch, int param, int value)
    {
        for (auto cc : t6::encodeNrpn (param, value))
            midi.addEvent (juce::MidiMessage::controllerEvent (ch, cc.controller, cc.value), 0);
        link->noteSentThroughHost (param, value);
    };
    runQueue (toHost);
    resendOsc1IfDue (toHost);
}

void Trigon6Processor::pumpDirect()
{
    // Until the port is open, changes stay pending and go out once it is.
    if (link->isHostOutput() || ! link->isOutputOpen())
        return;
    const juce::ScopedLock sl (queueLock);
    auto toPort = [&] (int ch, int param, int value) { link->sendNrpn (ch, param, value); };
    runQueue (toPort);
    resendOsc1IfDue (toPort);
}

void Trigon6Processor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void Trigon6Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            midiChannel = juce::jlimit (1, 16, (int) apvts.state.getProperty (kChannel, 1));
            if (apvts.state.hasProperty (kOutputMode))
                link->setHostOutput (apvts.state.getProperty (kOutputMode).toString() == "host");
            link->setPortChoice (apvts.state.getProperty (kInPort).toString(), apvts.state.getProperty (kOutPort).toString());
            resendOsc1OnPlay = (bool) apvts.state.getProperty (kResendOsc1, false);
            needsPrime = true;  // nothing goes out by itself on load; meetTheSynth() pushes the whole sound
            projectLoaded = true;
            if (projectWins)
                pushAtMs = juce::Time::getMillisecondCounterHiRes() + kMeetDelayMs;  // again when the synth reconnects
        }
}

juce::AudioProcessorEditor* Trigon6Processor::createEditor() { return new Trigon6Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Trigon6Processor(); }

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "core/Nrpn.h"
#include "core/ParamTable.h"

namespace
{
const juce::Identifier kChannel { "midiChannel" };
const juce::Identifier kOutputMode { "outputMode" };  // "host" or "direct"
const juce::Identifier kInPort { "inputPort" };       // "" = automatic
const juce::Identifier kOutPort { "outputPort" };

bool isStandalone()
{
    return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
}
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
      link (SynthLink::acquire())
{
    for (const auto& p : t6::paramTable())
        raw.push_back (apvts.getRawParameterValue (p.id));
    apvts.state.setProperty (kChannel, 1, nullptr);

    // The standalone app talks to the synth's port itself; in a DAW the host routes the plugin's MIDI out.
    setHostOutput (! isStandalone());
    incomingSeen = link->incomingSeq();
    pump.startThread();
    startTimerHz (30);
}

Trigon6Processor::~Trigon6Processor()
{
    stopTimer();
    pump.stopThread (1000);
}

void Trigon6Processor::timerCallback()
{
    if (soundPending)
    {
        if (link->editBufferSeq() != soundSeq)
        {
            soundPending = false;
            applySound (link->editBuffer());
        }
        else if (juce::Time::getMillisecondCounterHiRes() - soundAskedMs > 3000)
        {
            soundPending = false;
            soundStatus = "No sound came back. Check the Connection tab (MIDI SysEx = USB).";
        }
    }

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
        const int current = (int) std::lround (raw[(size_t) i]->load()) + (p.kind == t6::Kind::Choice ? p.min : 0);
        applyFromSynth ((size_t) i, juce::jlimit (p.min, p.max, t6::valueAfterNrpn (p, current, e.param, e.value)));
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

void Trigon6Processor::requestGetSound()
{
    soundSeq = link->editBufferSeq();
    soundPending = true;
    soundAskedMs = juce::Time::getMillisecondCounterHiRes();
    soundStatus = "Asking the synth for its sound...";
    link->requestEditBuffer();
}

void Trigon6Processor::applySound (const std::vector<uint8_t>& data)
{
    const auto& table = t6::paramTable();
    int set = 0;
    juce::StringArray outOfRange, skipped;
    const juce::ScopedLock sl (queueLock);
    for (size_t i = 0; i < table.size(); ++i)
    {
        const auto& p = table[i];
        if (p.nrpn < 0 || (size_t) p.nrpn >= data.size() || (p.nrpn2 >= 0 && (size_t) p.nrpn2 >= data.size()) || p.max > 255)
        {
            skipped.add (p.name);
            continue;
        }
        const int v = t6::valueFromBytes (p, data[(size_t) p.nrpn], p.nrpn2 >= 0 ? data[(size_t) p.nrpn2] : 0);
        if (v < p.min || v > p.max)
        {
            outOfRange.add (juce::String (p.name) + " " + juce::String (v));
            continue;
        }
        applyFromSynth (i, v);
        ++set;
    }
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
    auto valueOf = [&] (size_t i)
    {
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

    if (! link->isHostOutput())
        return;

    link->drainHostOutput (midi);
    const juce::ScopedTryLock sl (queueLock);
    if (! sl.isLocked())
        return;  // a mode switch is in progress; the next block sends
    runQueue ([&] (int ch, int param, int value)
    {
        for (auto cc : t6::encodeNrpn (param, value))
            midi.addEvent (juce::MidiMessage::controllerEvent (ch, cc.controller, cc.value), 0);
        link->noteSentThroughHost (param, value);
    });
}

void Trigon6Processor::pumpDirect()
{
    // Until the port is open, changes stay pending and go out once it is.
    if (link->isHostOutput() || ! link->isOutputOpen())
        return;
    const juce::ScopedLock sl (queueLock);
    runQueue ([&] (int ch, int param, int value) { link->sendNrpn (ch, param, value); });
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
            needsPrime = true;  // a loaded project does not overwrite the synth; "Send all" does that
        }
}

juce::AudioProcessorEditor* Trigon6Processor::createEditor() { return new Trigon6Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Trigon6Processor(); }

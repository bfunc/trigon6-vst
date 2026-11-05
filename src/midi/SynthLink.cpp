#include "SynthLink.h"

#include "core/Globals.h"
#include "core/Sysex.h"

namespace
{
constexpr int kTickMs = 250;
constexpr int kPortCheckTicks = 6;  // look for the ports every 1.5 s

juce::MidiDeviceInfo pick (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& choice)
{
    for (const auto& d : devices)
        if (choice.isEmpty() ? d.name.containsIgnoreCase ("Trigon") : d.name == choice)
            return d;
    return {};
}

juce::StringArray names (const juce::Array<juce::MidiDeviceInfo>& devices)
{
    juce::StringArray n;
    for (const auto& d : devices)
        n.add (d.name);
    return n;
}
} // namespace

SynthLink::SynthLink() { startTimer (kTickMs); }

SynthLink::~SynthLink()
{
    stopTimer();
    if (input)
        input->stop();
    input.reset();
    const juce::ScopedLock sl (outLock);
    output.reset();
}

juce::StringArray SynthLink::inputNames() { return names (juce::MidiInput::getAvailableDevices()); }
juce::StringArray SynthLink::outputNames() { return names (juce::MidiOutput::getAvailableDevices()); }

void SynthLink::setPortChoice (const juce::String& inputName, const juce::String& outputName)
{
    {
        const juce::ScopedLock sl (stateLock);
        inChoice = inputName;
        outChoice = outputName;
    }
    ticks = 0;  // look again on the next tick
}

juce::String SynthLink::getInputChoice() const
{
    const juce::ScopedLock sl (stateLock);
    return inChoice;
}

juce::String SynthLink::getOutputChoice() const
{
    const juce::ScopedLock sl (stateLock);
    return outChoice;
}

void SynthLink::setHostOutput (bool host)
{
    hostOutput = host;
    ticks = 0;
}

void SynthLink::updatePorts()
{
    juce::String wantIn, wantOut;
    {
        const juce::ScopedLock sl (stateLock);
        wantIn = inChoice;
        wantOut = outChoice;
    }
    const auto inDev = pick (juce::MidiInput::getAvailableDevices(), wantIn);
    const auto outDev = hostOutput ? juce::MidiDeviceInfo() : pick (juce::MidiOutput::getAvailableDevices(), wantOut);
    bool opened = false;

    // Input: close it when it is gone or another port is wanted, then (re)open. A busy port is retried each time.
    if (input != nullptr && inDev.identifier != inputId)
    {
        input->stop();
        input.reset();
        inputId = {};
    }
    if (input == nullptr && inDev.identifier.isNotEmpty())
    {
        input = juce::MidiInput::openDevice (inDev.identifier, this);
        if (input != nullptr)
        {
            input->start();
            inputId = inDev.identifier;
            for (auto& d : decoders)
                d.reset();
            opened = true;
        }
    }

    {
        const juce::ScopedLock sl (outLock);
        if (output != nullptr && outDev.identifier != outputId)
        {
            output.reset();
            outputId = {};
        }
        if (output == nullptr && outDev.identifier.isNotEmpty())
        {
            output = juce::MidiOutput::openDevice (outDev.identifier);
            if (output != nullptr)
            {
                outputId = outDev.identifier;
                opened = true;
            }
        }
        outputOpen = output != nullptr;
    }

    const juce::ScopedLock sl (stateLock);
    state.inName = inDev.name.toStdString();
    state.inOpen = input != nullptr;
    state.inError = inDev.identifier.isNotEmpty() && input == nullptr ? "busy or unavailable" : "";
    state.outName = outDev.name.toStdString();
    state.outOpen = outputOpen.load();
    state.outError = outDev.identifier.isNotEmpty() && ! outputOpen ? "busy or unavailable" : "";
    if (opened)
    {
        // A new connection: old answers no longer say anything about it.
        state.inquiryMs = state.globalsMs = state.editBufferMs = state.probeSentMs = -1;
        probeAfterOpen = true;
    }
}

void SynthLink::probe() { probeStep = 0; }

void SynthLink::timerCallback()
{
    if (ticks++ % kPortCheckTicks == 0)
        updatePorts();

    const bool canSend = hostOutput || outputOpen;
    if (probeAfterOpen && canSend && input != nullptr)
    {
        probeAfterOpen = false;
        probe();
    }

    if (probeStep < 0 || ! canSend)
        return;
    switch (probeStep)
    {
        case 0:
        {
            const juce::ScopedLock sl (stateLock);
            state.probeSentMs = nowMs();
        }
            sendSysex (t6::requestDeviceInquiry());
            break;
        case 1: sendSysex (t6::requestGlobals()); break;
        default: sendSysex (t6::requestEditBuffer()); break;
    }
    probeStep = probeStep < 2 ? probeStep + 1 : -1;
}

void SynthLink::sendNrpn (int channel, int param, int value)
{
    const auto bytes = t6::encodeNrpnBytes (channel, param, value);
    const juce::ScopedLock sl (outLock);
    if (output == nullptr)
        return;
    for (size_t i = 0; i < bytes.size(); i += 3)
        output->sendMessageNow (juce::MidiMessage (bytes[i], bytes[i + 1], bytes[i + 2]));
}

void SynthLink::sendSysex (const std::vector<uint8_t>& message)
{
    if (message.size() < 2)
        return;
    // JUCE adds F0 and F7 itself.
    const auto m = juce::MidiMessage::createSysExMessage (message.data() + 1, (int) message.size() - 2);
    if (hostOutput)
    {
        const juce::SpinLock::ScopedLockType sl (hostLock);
        hostPending.push_back (m);
        return;
    }
    const juce::ScopedLock sl (outLock);
    if (output != nullptr)
        output->sendMessageNow (m);
}

void SynthLink::drainHostOutput (juce::MidiBuffer& midi)
{
    const juce::SpinLock::ScopedTryLockType sl (hostLock);
    if (! sl.isLocked())
        return;
    for (const auto& m : hostPending)
        midi.addEvent (m, 0);
    hostPending.clear();
}

void SynthLink::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    const double t = nowMs();

    if (m.isSysEx())
    {
        const auto* d = m.getSysExData();
        const auto n = (size_t) m.getSysExDataSize();
        const juce::ScopedLock sl (stateLock);
        state.lastAnyInMs = t;
        switch (t6::classifySysex (d, n))
        {
            case t6::SysexKind::DeviceInquiryReply:
                state.device = *t6::parseDeviceInquiryReply (d, n);
                state.inquiryMs = t;
                break;
            case t6::SysexKind::GlobalDump:
                if (auto g = t6::parseGlobalDump (d, n))
                {
                    state.globalsDump = *g;
                    state.globalsMs = t;
                }
                break;
            case t6::SysexKind::EditBufferDump:
                state.editBufferSize = n + 2;
                state.editBufferOk = t6::parseProgramDump (d, n).has_value();
                state.editBufferMs = t;
                break;
            default: break;
        }
        return;
    }

    std::optional<t6::NrpnEvent> ev;
    if (m.isController())
        ev = decoders[(size_t) (m.getChannel() - 1)].feed ((uint8_t) m.getControllerNumber(), (uint8_t) m.getControllerValue());

    const juce::ScopedLock sl (stateLock);
    state.lastAnyInMs = t;
    if (! ev)
        return;
    ++state.nrpnInCount;
    state.lastNrpnMs = t;
    state.lastNrpnParam = ev->param;
    state.lastNrpnValue = ev->value;
    state.lastNrpnChannel = m.getChannel();
    if (t6::isGlobalNrpn (ev->param))
    {
        state.globalNrpn[ev->param] = ev->value;
        state.globalNrpnMs[ev->param] = t;
    }
}

t6::LinkSnapshot SynthLink::snapshot (int pluginChannel) const
{
    const juce::ScopedLock sl (stateLock);
    auto s = state;
    s.nowMs = nowMs();
    s.pluginChannel = pluginChannel;
    s.hostOutput = hostOutput;
    return s;
}

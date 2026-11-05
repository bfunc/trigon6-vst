#include "SynthLink.h"

#include "core/Globals.h"
#include "core/PortMatch.h"
#include "core/Sysex.h"

#include <mutex>

namespace
{
constexpr int kTickMs = 50;
constexpr int kPortCheckTicks = 30;  // look for the ports every 1.5 s
constexpr int kProbeStepTicks = 5;   // the three Check requests go out 250 ms apart
constexpr double kEchoWindowMs = 300;
constexpr int kTestNote = 60;

std::mutex instanceMutex;
std::weak_ptr<SynthLink> instance;

struct Pick
{
    juce::MidiDeviceInfo device;
    std::string savedMissing;
};

Pick pick (const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& choice)
{
    std::vector<std::string> names;
    for (const auto& d : devices)
        names.push_back (d.name.toStdString());
    const auto p = t6::pickPort (names, choice.toStdString());
    Pick r;
    if (p.index >= 0)
        r.device = devices[p.index];
    if (p.savedMissing)
        r.savedMissing = choice.toStdString();
    return r;
}

juce::StringArray names (const juce::Array<juce::MidiDeviceInfo>& devices)
{
    juce::StringArray n;
    for (const auto& d : devices)
        n.add (d.name);
    return n;
}
} // namespace

std::shared_ptr<SynthLink> SynthLink::acquire()
{
    const std::lock_guard<std::mutex> lock (instanceMutex);
    auto link = instance.lock();
    if (link == nullptr)
    {
        link = std::shared_ptr<SynthLink> (new SynthLink());
        instance = link;
    }
    return link;
}

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
    const auto inPick = pick (juce::MidiInput::getAvailableDevices(), wantIn);
    const auto outPick = hostOutput ? Pick() : pick (juce::MidiOutput::getAvailableDevices(), wantOut);
    const auto& inDev = inPick.device;
    const auto& outDev = outPick.device;
    bool opened = false;

    // Close a port that is gone or no longer wanted, then (re)open. A busy port is retried each time.
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
    state.inSavedMissing = inPick.savedMissing;
    state.outName = outDev.name.toStdString();
    state.outOpen = outputOpen.load();
    state.outError = outDev.identifier.isNotEmpty() && ! outputOpen ? "busy or unavailable" : "";
    state.outSavedMissing = outPick.savedMissing;
    if (opened)
    {
        // A new connection: what was seen before says nothing about it.
        const auto keep = state;
        state = t6::LinkSnapshot();
        state.inName = keep.inName;
        state.inOpen = keep.inOpen;
        state.inError = keep.inError;
        state.inSavedMissing = keep.inSavedMissing;
        state.outName = keep.outName;
        state.outOpen = keep.outOpen;
        state.outError = keep.outError;
        state.outSavedMissing = keep.outSavedMissing;
        probeAfterOpen = true;
    }
}

void SynthLink::probe() { probeStep = 0; }

void SynthLink::playTestNote (int channel)
{
    noteChannel = juce::jlimit (1, 16, channel);
    sendMessage (juce::MidiMessage::noteOn (noteChannel, kTestNote, (juce::uint8) 100));
    noteOffAtMs = nowMs() + 400;
    const juce::ScopedLock sl (stateLock);
    state.noteTest = t6::NoteTest::Waiting;
}

void SynthLink::answerNoteTest (bool heard)
{
    const juce::ScopedLock sl (stateLock);
    state.noteTest = heard ? t6::NoteTest::Heard : t6::NoteTest::NotHeard;
}

void SynthLink::timerCallback()
{
    if (ticks++ % kPortCheckTicks == 0)
        updatePorts();

    if (noteOffAtMs >= 0 && nowMs() >= noteOffAtMs)
    {
        noteOffAtMs = -1;
        sendMessage (juce::MidiMessage::noteOff (noteChannel, kTestNote));
    }

    const bool canSend = hostOutput || outputOpen;
    if (probeAfterOpen && canSend && input != nullptr)
    {
        probeAfterOpen = false;
        probe();
    }

    if (probeStep < 0 || ! canSend || ticks % kProbeStepTicks != 0)
        return;
    switch (probeStep)
    {
        case 0:
        {
            {
                const juce::ScopedLock sl (stateLock);
                state.probeSentMs = nowMs();
            }
            sendSysex (t6::requestDeviceInquiry());
            break;
        }
        case 1: sendSysex (t6::requestGlobals ((uint8_t) sysexId.load())); break;
        default: sendSysex (t6::requestEditBuffer ((uint8_t) sysexId.load())); break;
    }
    probeStep = probeStep < 2 ? probeStep + 1 : -1;
}

void SynthLink::sendMessage (const juce::MidiMessage& m)
{
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

void SynthLink::rememberSent (int param, int value, bool fromAudioThread)
{
    const double t = nowMs();
    auto put = [&]
    {
        sent[sentNext] = { param, value, t };
        sentNext = (sentNext + 1) % sent.size();
    };
    if (fromAudioThread)
    {
        const juce::SpinLock::ScopedTryLockType sl (sentLock);
        if (sl.isLocked())
            put();
    }
    else
    {
        const juce::SpinLock::ScopedLockType sl (sentLock);
        put();
    }
}

void SynthLink::sendNrpn (int channel, int param, int value)
{
    const auto bytes = t6::encodeNrpnBytes (channel, param, value);
    {
        const juce::ScopedLock sl (outLock);
        if (output == nullptr)
            return;
        for (size_t i = 0; i < bytes.size(); i += 3)
            output->sendMessageNow (juce::MidiMessage (bytes[i], bytes[i + 1], bytes[i + 2]));
    }
    rememberSent (param, value, false);
}

void SynthLink::noteSentThroughHost (int param, int value) { rememberSent (param, value, true); }

void SynthLink::sendSysex (const std::vector<uint8_t>& message)
{
    if (message.size() < 2)
        return;
    // JUCE adds F0 and F7 itself.
    sendMessage (juce::MidiMessage::createSysExMessage (message.data() + 1, (int) message.size() - 2));
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
        const auto id = (uint8_t) sysexId.load();
        const juce::ScopedLock sl (stateLock);
        state.lastAnyInMs = t;
        auto noteOther = [&] { state.otherSysex = std::to_string (n + 2) + " bytes: F0 " + t6::toHex (d, n, 16); };
        switch (t6::classifySysex (d, n, id))
        {
            case t6::SysexKind::DeviceInquiryReply:
                state.device = *t6::parseDeviceInquiryReply (d, n);
                state.inquiryMs = t;
                // A Sequential synth: address the dump requests to the model it reports.
                if (state.device.manufacturer == t6::kDsiId && state.device.family > 0)
                    sysexId = state.device.family;
                break;
            case t6::SysexKind::GlobalDump:
                if (auto g = t6::parseGlobalDump (d, n, id))
                {
                    state.globalsDump = *g;
                    state.globalsMs = t;
                }
                else
                    noteOther();
                break;
            case t6::SysexKind::EditBufferDump:
            {
                auto data = t6::parseProgramDump (d, n, id);
                state.editBufferSize = n + 2;
                state.editBufferOk = data.has_value();
                state.editBufferMs = t;
                if (data)
                {
                    editData = std::move (*data);
                    ++editSeq;
                }
                break;
            }
            default: noteOther(); break;
        }
        return;
    }

    if (m.isActiveSense() || m.isMidiClock())
        return;  // say nothing about the settings; and clock would flood the counters

    std::optional<t6::NrpnEvent> ev;
    bool paramCc = false;
    if (m.isController())
    {
        const int cc = m.getControllerNumber();
        ev = decoders[(size_t) (m.getChannel() - 1)].feed ((uint8_t) cc, (uint8_t) m.getControllerValue());
        paramCc = t6::isParameterCc (cc);
    }

    bool echo = false;
    if (ev)
    {
        const juce::SpinLock::ScopedLockType sl (sentLock);
        for (const auto& s : sent)
            if (s.param == ev->param && s.value == ev->value && t - s.ms < kEchoWindowMs)
                echo = true;
    }

    const juce::ScopedLock sl (stateLock);
    state.lastAnyInMs = t;
    if (paramCc)
    {
        ++state.paramCcInCount;
        state.lastCcChannel = m.getChannel();
        state.lastCcNumber = m.getControllerNumber();
    }
    if (! ev)
        return;
    if (echo)
    {
        ++state.echoCount;
        return;
    }
    ++state.nrpnInCount;
    state.lastNrpnMs = t;
    state.lastNrpnParam = ev->param;
    state.lastNrpnValue = ev->value;
    state.lastNrpnChannel = m.getChannel();
    if (t6::isGlobalNrpn (ev->param))
    {
        state.globalNrpn[ev->param] = ev->value;
        state.globalNrpnMs[ev->param] = t;
        return;
    }
    ++inSeq;
    inEvents[inSeq % inEvents.size()] = { inSeq, m.getChannel(), ev->param, ev->value };
}

void SynthLink::requestEditBuffer() { sendSysex (t6::requestEditBuffer ((uint8_t) sysexId.load())); }

uint32_t SynthLink::editBufferSeq() const
{
    const juce::ScopedLock sl (stateLock);
    return editSeq;
}

std::vector<uint8_t> SynthLink::editBuffer() const
{
    const juce::ScopedLock sl (stateLock);
    return editData;
}

uint32_t SynthLink::incomingSeq() const
{
    const juce::ScopedLock sl (stateLock);
    return inSeq;
}

uint32_t SynthLink::readIncoming (uint32_t afterSeq, std::vector<InEvent>& out) const
{
    const juce::ScopedLock sl (stateLock);
    // Fell behind by more than the ring holds: skip what was overwritten.
    uint32_t from = inSeq - afterSeq > inEvents.size() ? inSeq - (uint32_t) inEvents.size() : afterSeq;
    for (uint32_t s = from + 1; s <= inSeq; ++s)
        out.push_back (inEvents[s % inEvents.size()]);
    return inSeq;
}

t6::LinkSnapshot SynthLink::snapshot (int pluginChannel) const
{
    const juce::ScopedLock sl (stateLock);
    auto s = state;
    s.nowMs = nowMs();
    s.pluginChannel = pluginChannel;
    s.hostOutput = hostOutput;
    {
        const std::lock_guard<std::mutex> lock (instanceMutex);
        s.sharedBy = juce::jmax (1, (int) instance.use_count());
    }
    return s;
}

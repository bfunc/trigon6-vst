// The plugin's own MIDI connection to the synth: opens the Trigon-6 USB ports (or ports picked by name),
// reopens them when the synth is switched on or plugged in, sends NRPN/SysEx/test notes, and records
// everything that comes back for the Connection page.
//
// One per program: Windows gives a MIDI port to one client at a time, so every plugin copy in the same
// host shares this object (acquire()). Its settings (ports, output mode) are shared too.
//
// Threads: the port timer runs on the message thread, MIDI input arrives on the MIDI thread, and NRPN are
// sent from the processor's pump thread or the audio thread. The audio thread only ever try-locks.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "core/Connection.h"
#include "core/Nrpn.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class SynthLink : private juce::MidiInputCallback, private juce::Timer
{
public:
    static std::shared_ptr<SynthLink> acquire();  // message thread
    ~SynthLink() override;

    // Port choice by name; "" = automatic (the first port whose name contains "Trigon").
    void setPortChoice (const juce::String& inputName, const juce::String& outputName);
    juce::String getInputChoice() const;
    juce::String getOutputChoice() const;
    static juce::StringArray inputNames();
    static juce::StringArray outputNames();

    // true: NRPN, SysEx and test notes go out through the host's MIDI out (drainHostOutput); no output port.
    void setHostOutput (bool);
    bool isHostOutput() const { return hostOutput.load(); }
    bool isOutputOpen() const { return outputOpen.load(); }

    void probe();                       // Check: device inquiry, then the global and edit buffer dump requests
    void playTestNote (int channel);    // middle C for 0.4 s; then answerNoteTest()
    void answerNoteTest (bool heard);

    void sendNrpn (int channel, int param, int value);     // to the output port (direct mode)
    void sendSysex (const std::vector<uint8_t>& message);  // F0 ... F7; direct or through the host
    void noteSentThroughHost (int param, int value);       // audio thread: for loop detection

    // Audio thread, host mode: moves queued SysEx and test notes into the block. Never blocks.
    void drainHostOutput (juce::MidiBuffer&);

    t6::LinkSnapshot snapshot (int pluginChannel) const;

    // NRPN the synth sent (a knob turned on the panel, with Param Xmit = NR), numbered in arrival order.
    // Every plugin copy reads them on its own: pass the last seq it saw, get the newer events and the new
    // last seq. Echoes of what the plugin sent are not included.
    struct InEvent
    {
        uint32_t seq = 0;
        int channel = 0, param = -1, value = 0;
    };
    uint32_t readIncoming (uint32_t afterSeq, std::vector<InEvent>& out) const;
    uint32_t incomingSeq() const;

private:
    SynthLink();
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void updatePorts();
    void sendMessage (const juce::MidiMessage&);
    void rememberSent (int param, int value, bool fromAudioThread);
    static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    mutable juce::CriticalSection stateLock;
    t6::LinkSnapshot state;  // nowMs, pluginChannel, hostOutput and sharedBy are filled in by snapshot()
    juce::String inChoice, outChoice;

    juce::CriticalSection outLock;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String outputId;
    std::unique_ptr<juce::MidiInput> input;
    juce::String inputId;

    std::atomic<bool> hostOutput { false };
    std::atomic<bool> outputOpen { false };
    std::atomic<int> sysexId { t6::kTrigon6Id };  // the model byte for requests; learned from the inquiry reply

    juce::SpinLock hostLock;
    std::vector<juce::MidiMessage> hostPending;

    // NRPN sent in the last moments, to recognise them if they come straight back (a MIDI loop)
    struct Sent
    {
        int param = -1, value = 0;
        double ms = -1;
    };
    juce::SpinLock sentLock;
    std::array<Sent, 32> sent;
    size_t sentNext = 0;

    std::array<t6::NrpnDecoder, 16> decoders;  // MIDI thread only

    std::array<InEvent, 256> inEvents;  // ring, guarded by stateLock
    uint32_t inSeq = 0;                 // seq of the newest event, guarded by stateLock

    int probeStep = -1;  // message thread: next request of a running Check
    int ticks = 0;
    bool probeAfterOpen = false;
    double noteOffAtMs = -1;
    int noteChannel = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthLink)
};

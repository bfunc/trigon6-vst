// The plugin's own MIDI connection to the synth: opens the Trigon-6 USB ports (or ports picked by name),
// reopens them when the synth is switched on or plugged in, sends NRPN/SysEx, and records everything that
// comes back for the Connection page.
//
// Threads: the port timer runs on the message thread, MIDI input arrives on the MIDI thread, and NRPN are
// sent from the processor's pump thread or the audio thread. The shared state sits behind locks; none of
// it is touched from the audio thread except drainHostOutput(), which only try-locks.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "core/Connection.h"
#include "core/Nrpn.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class SynthLink : private juce::MidiInputCallback, private juce::Timer
{
public:
    SynthLink();
    ~SynthLink() override;

    // Port choice by name; "" = automatic (the first port whose name contains "Trigon").
    void setPortChoice (const juce::String& inputName, const juce::String& outputName);
    juce::String getInputChoice() const;
    juce::String getOutputChoice() const;
    static juce::StringArray inputNames();
    static juce::StringArray outputNames();

    // true: NRPN and SysEx go out through the host's MIDI out (drainHostOutput), and no output port is opened.
    void setHostOutput (bool);
    bool isHostOutput() const { return hostOutput.load(); }
    bool isOutputOpen() const { return outputOpen.load(); }

    // "Check": device inquiry, then the global and edit buffer dump requests.
    void probe();

    void sendNrpn (int channel, int param, int value);  // to the output port (direct mode)
    void sendSysex (const std::vector<uint8_t>& message);  // F0 ... F7; direct or through the host

    // Audio thread, host mode: moves queued SysEx into the block. Never blocks.
    void drainHostOutput (juce::MidiBuffer&);

    t6::LinkSnapshot snapshot (int pluginChannel) const;

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void updatePorts();
    static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    mutable juce::CriticalSection stateLock;
    t6::LinkSnapshot state;  // nowMs and pluginChannel are filled in by snapshot()
    juce::String inChoice, outChoice;

    juce::CriticalSection outLock;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String outputId;
    std::unique_ptr<juce::MidiInput> input;
    juce::String inputId;

    std::atomic<bool> hostOutput { false };
    std::atomic<bool> outputOpen { false };

    juce::SpinLock hostLock;
    std::vector<juce::MidiMessage> hostPending;

    std::array<t6::NrpnDecoder, 16> decoders;  // MIDI thread only

    int probeStep = -1;  // message thread: next request of a running Check
    int ticks = 0;
    bool probeAfterOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthLink)
};

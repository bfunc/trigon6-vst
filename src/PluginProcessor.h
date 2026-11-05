#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/SendQueue.h"
#include "midi/SynthLink.h"

#include <atomic>
#include <vector>

// Holds the Trigon-6 program parameters (built from t6::paramTable()) and sends every change as NRPN,
// either on the plugin's MIDI output (host mode, the default in a DAW) or straight to the synth's port
// (direct mode, the default in the standalone app). It makes no sound.
class Trigon6Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    Trigon6Processor();
    ~Trigon6Processor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // UI side
    juce::AudioProcessorValueTreeState apvts;
    int getMidiChannel() const { return midiChannel.load(); }
    void setMidiChannel (int ch);
    void requestSendAll() { sendAllRequested = true; }
    // Get sound: ask the synth for its current sound and set the parameters from it (message thread).
    // Each parameter is read from the byte at its NRPN number; that position is a guess being tested.
    void requestGetSound();
    juce::String getSoundStatus() const { return soundStatus; }  // message thread
    int getSentCount() const { return sentCount.load(); }

    SynthLink& getLink() { return *link; }
    bool isHostOutput() const { return link->isHostOutput(); }
    void setHostOutput (bool host);
    void setPortChoice (const juce::String& inputName, const juce::String& outputName);

private:
    template <typename Emit>
    void runQueue (Emit&& emit);  // the caller holds queueLock
    void pumpDirect();             // pump thread: direct mode sending
    void timerCallback() override;  // message thread: takes knob moves the synth sent
    void applyFromSynth (size_t index, int value);  // queueLock held: set without sending it back
    void applySound (const std::vector<uint8_t>& data);

    struct Pump : juce::Thread
    {
        explicit Pump (Trigon6Processor& p) : juce::Thread ("Trigon6 NRPN out"), proc (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                proc.pumpDirect();
                wait (2);
            }
        }
        Trigon6Processor& proc;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::vector<std::atomic<float>*> raw;  // one per paramTable() entry, same order
    t6::SendQueue queue;
    juce::CriticalSection queueLock;  // the audio thread only try-locks it
    std::shared_ptr<SynthLink> link;  // shared by every plugin copy in this program
    Pump pump { *this };
    std::atomic<int> midiChannel { 1 };
    std::atomic<bool> sendAllRequested { false };
    std::atomic<bool> needsPrime { true };  // after load: take current values as "already on the synth"
    std::atomic<int> sentCount { 0 };
    uint32_t incomingSeen = 0;  // message thread: last SynthLink::readIncoming seq applied
    bool soundPending = false;  // message thread: Get sound is waiting for the dump
    uint32_t soundSeq = 0;
    double soundAskedMs = 0;
    juce::String soundStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Processor)
};

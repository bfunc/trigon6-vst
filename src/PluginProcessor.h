#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/SendQueue.h"
#include "midi/SynthLink.h"

#include <atomic>
#include <vector>

// Holds the Trigon-6 program parameters (built from t6::paramTable()) and sends every change as NRPN,
// either on the plugin's MIDI output (host mode, the default in a DAW) or straight to the synth's port
// (direct mode, the default in the standalone app). It makes no sound.
class Trigon6Processor : public juce::AudioProcessor
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
    int getSentCount() const { return sentCount.load(); }

    SynthLink& getLink() { return *link; }
    bool isHostOutput() const { return link->isHostOutput(); }
    void setHostOutput (bool host);
    void setPortChoice (const juce::String& inputName, const juce::String& outputName);

private:
    template <typename Emit>
    void runQueue (Emit&& emit);  // the caller holds queueLock
    void pumpDirect();             // pump thread: direct mode sending

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Processor)
};

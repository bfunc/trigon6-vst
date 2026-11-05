#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/SendQueue.h"

#include <atomic>
#include <vector>

// Holds the Trigon-6 program parameters (built from t6::paramTable()) and sends every change as NRPN
// on the plugin's MIDI output. It makes no sound.
class Trigon6Processor : public juce::AudioProcessor
{
public:
    Trigon6Processor();

    void prepareToPlay (double sampleRate, int) override;
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::vector<std::atomic<float>*> raw;  // one per paramTable() entry, same order
    t6::SendQueue queue;
    std::atomic<int> midiChannel { 1 };
    std::atomic<bool> sendAllRequested { false };
    std::atomic<bool> needsPrime { true };  // after load: take current values as "already on the synth"
    std::atomic<int> sentCount { 0 };
    double sampleRate = 44100.0;
    double clockMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Processor)
};

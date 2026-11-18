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
    // Each parameter is read from the byte at its NRPN number (checked against 1000 stored programs: every value in range).
    void requestGetSound();
    juce::String getSoundStatus() const { return soundStatus; }  // message thread
    int getSentCount() const { return sentCount.load(); }

    // Where the current sound came from, for the "saved / edited" indicator (message thread).
    // The slot is set by a program picked on the synth, Load on the Presets tab, or a save into a slot;
    // a program change also reads the new sound (Get sound) after a moment.
    struct SoundOrigin
    {
        bool slotKnown = false;
        int bank = 0, program = 0;
        std::string name;             // the sound's name: from Get sound or the last save; "" = not known
        std::vector<uint8_t> pcData;  // the last .syx saved to the PC since the slot was picked
    };
    const SoundOrigin& getOrigin() const { return origin; }
    void loadProgram (int bank, int program);  // program change + read the new sound
    void noteSavedToSynth (int bank, int program, const std::string& name);
    void noteSavedToPc (const std::vector<uint8_t>& data);  // 1024 bytes, name included
    std::vector<int> currentValues() const;                 // NRPN values, one per paramTable() entry
    int currentValue (size_t index) const;

    // The preset the panel compares the sound with: the sound read from the synth (Get sound, also after a program
    // is picked) or the one last saved. Empty = no preset yet. Message thread.
    bool hasPreset() const { return ! presetValues.empty(); }
    const std::vector<int>& getPresetValues() const { return presetValues; }
    int presetDiffCount() const;
    // Set one parameter as a hand move would: the host sees it and the synth gets it.
    void setParamTo (size_t index, int value);
    void resetToPreset (size_t index);
    void resetAllToPreset();
    // Compare: while held, the synth plays the preset (only the differing parameters go out as NRPN); the panel keeps
    // showing the edit. Message thread. begin does nothing without a preset or when nothing differs.
    void beginCompare();
    void endCompare();
    bool comparing() const { return previewing.load(); }

    SynthLink& getLink() { return *link; }
    bool isHostOutput() const { return link->isHostOutput(); }
    void setHostOutput (bool host);
    void setPortChoice (const juce::String& inputName, const juce::String& outputName);

    // Optional workaround, off by default: FL Studio's MIDI Out knocks the synth's Osc 1 Octave to +2 when the
    // transport starts or stops. With this on, the Osc 1 Octave from the last Get sound (or the last one the plugin
    // sent) goes to the synth again 10 ms after each start and stop.
    bool getResendOsc1OnPlay() const { return resendOsc1OnPlay.load(); }
    void setResendOsc1OnPlay (bool on);

    // Watch the synth's sound (poll test): Init on the synth sends nothing, so while nothing is being edited the
    // plugin asks for the edit buffer every few seconds and takes the synth's values when they differ. The preset
    // (the slot reference) stays, so the panel shows how far the sound is from it. Not saved; on by default.
    enum class Watch { Off, Every2s, Every250ms };
    Watch getWatch() const { return watch.load(); }
    void setWatch (Watch w) { watch = w; }
    struct WatchStats
    {
        int asked = 0, answered = 0, skipped = 0, taken = 0, timeouts = 0;
        double lastReplyMs = -1, slowestReplyMs = -1;
        juce::String lastTaken;  // what the last change was, for the test
    };
    const WatchStats& getWatchStats() const { return watchStats; }  // message thread

private:
    template <typename Emit>
    void runQueue (Emit&& emit);  // the caller holds queueLock
    void pumpDirect();             // pump thread: direct mode sending
    void timerCallback() override;  // message thread: takes knob moves the synth sent
    void applyFromSynth (size_t index, int value);  // queueLock held: set without sending it back
    void applySound (const std::vector<uint8_t>& data);
    template <typename Emit>
    void resendOsc1IfDue (Emit&& emit);  // the caller holds queueLock

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
    std::atomic<bool> resendOsc1OnPlay { false };
    std::atomic<int> osc1Octave { -1 };       // NRPN value to restore on play; -1 = none yet
    std::atomic<double> osc1ResendAtMs { -1 };  // when the restore is due; -1 = none pending
    bool wasPlaying = false;                    // audio thread: transport state in the last block
    uint32_t incomingSeen = 0;  // message thread: last SynthLink::readIncoming seq applied
    bool soundPending = false;  // message thread: Get sound is waiting for the dump
    uint32_t soundSeq = 0;
    double soundAskedMs = 0;
    juce::String soundStatus;
    SoundOrigin origin;
    std::vector<int> presetValues;  // message thread; see hasPreset()
    std::atomic<bool> previewing { false };
    std::vector<int> previewValues;  // written only while previewing is false; read by the senders while it is true
    void takePresetFromCurrent();
    uint32_t pcSeen = 0;          // message thread: last SynthLink program change handled
    double autoGetSoundMs = -1;   // message thread: when to read the sound after a program change
    // The plugin and the synth meet when the link comes up (at start, or after the synth was away). In a DAW a loaded
    // project pushes its sound to the synth's edit buffer; otherwise (the standalone, a new plugin copy) the plugin
    // reads the synth's sound. Waits kMeetDelayMs for a host that restores its state more than once.
    static constexpr double kMeetDelayMs = 600.0;
    const bool projectWins;                     // a DAW plugin, not the standalone app
    std::atomic<bool> projectLoaded { false };  // setStateInformation ran (any thread)
    std::atomic<double> pushAtMs { -1 };        // when to push the project's sound; -1 = not wanted
    bool wasConnected = false;                  // message thread: the link was up at the last tick
    void meetTheSynth (double nowMs);           // message thread, from the timer
    void setSlot (int bank, int program);

    // Watch (see getWatch). Message thread.
    std::atomic<Watch> watch { Watch::Every2s };
    WatchStats watchStats;
    bool watchPending = false;
    uint32_t watchSeq = 0;
    double watchAskedMs = 0, nextWatchMs = 0, quietSinceMs = 0;
    int sentSeen = 0, sentAtWatch = 0, watchFailures = 0;
    std::vector<int> valuesAtWatch;
    void watchTheSynth (double nowMs);
    void takeWatched (const std::vector<uint8_t>& data, double nowMs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Trigon6Processor)
};

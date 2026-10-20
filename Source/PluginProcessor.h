#pragma once

#include <JuceHeader.h>
#include "WasapiLoopbackCapture.h"
#include "EchoCancellerEngine.h"
#include <vector>

class AEC3EchoCancellerAudioProcessor  : public juce::AudioProcessor,
                                         private juce::Timer
{
public:
    AEC3EchoCancellerAudioProcessor();
    /** Destroy on the message thread: juce::Timer::stopTimer() does not wait for a watchdog callback
        already running on another thread, so destroying elsewhere could race with one. */
    ~AEC3EchoCancellerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Bypass crossfade length in host samples. */
    static constexpr int kBypassRampSamples = 480;

    /** Tests set this to false so prepareToPlay does not start WASAPI capture. */
    bool startCaptureOnPrepare = true;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }
    bool isCaptureWatchdogRunning() const { return isTimerRunning(); }   // test hook
    WasapiLoopbackCapture& getLoopbackCapture() { return loopbackCapture; }
    EchoCancellerEngine& getEchoCancellerEngine() { return echoCancellerEngine; }

    // UI page persistence (main / advanced)
    bool isAdvancedOpen() const                        { return uiMode == "advanced"; }
    void setAdvancedOpen (bool open)                   { uiMode = open ? "advanced" : "basic"; }

private:
    void timerCallback() override;   // capture watchdog (message thread)
    void processInternal (juce::AudioBuffer<float>&, bool forceBypass);

    juce::AudioProcessorValueTreeState parameters;
    WasapiLoopbackCapture loopbackCapture;
    EchoCancellerEngine echoCancellerEngine;

    // Core controls
    std::atomic<float>* enableParam = nullptr;
    std::atomic<float>* monitorCaptureParam = nullptr;

    // Normal suppression sliders
    std::atomic<float>* lfSuppressionParam = nullptr;
    std::atomic<float>* lfTransparencyParam = nullptr;
    std::atomic<float>* hfSuppressionParam = nullptr;
    std::atomic<float>* hfTransparencyParam = nullptr;
    std::atomic<float>* lfAttackParam = nullptr;
    std::atomic<float>* lfDecayParam = nullptr;
    std::atomic<float>* hfDuckingParam = nullptr;
    std::atomic<float>* hfEchoThresholdParam = nullptr;

    // Double-talk suppression sliders
    std::atomic<float>* dtLfSuppressionParam = nullptr;
    std::atomic<float>* dtLfTransparencyParam = nullptr;
    std::atomic<float>* dtHfSuppressionParam = nullptr;
    std::atomic<float>* dtHfTransparencyParam = nullptr;
    std::atomic<float>* dtLfAttackParam = nullptr;
    std::atomic<float>* dtLfDecayParam = nullptr;

    // System params
    std::atomic<float>* latencyCompParam = nullptr;
    std::atomic<float>* roomReverbParam = nullptr;
    std::atomic<float>* boundedErlParam = nullptr;
    std::atomic<float>* clockDriftParam = nullptr;

    // Noise suppression
    std::atomic<float>* nsLevelParam = nullptr;
    std::atomic<float>* monitorNoiseParam = nullptr;

    // Level (the AGC values are read through aec::readAgcSettings)
    std::atomic<float>* limiterEnabledParam = nullptr;

    // Device ID — stored outside APVTS (dynamic list doesn't fit APVTS well)
    juce::String selectedDeviceId;

    // UI mode — persisted in state XML, restored by editor on open
    juce::String uiMode { "basic" };

    std::vector<float> monoInputBuffer;
    std::vector<float> monoOutputBuffer;
    std::vector<float> dryBuffer;
    int preparedBlock = 0;

    // Whole-plugin bypass (audio thread only)
    aec::DelayLine dryDelay;
    int bypassCount = 0;        // 0 = fully processed, kBypassRampSamples = fully dry
    bool engineActive = true;   // engine.setEnabled state as last set by processInternal
    bool wasFaulted = false;    // engine error / unusable state on the previous block
    int holdRemaining = 0;      // host samples until the engine's output is real after (re)enabling

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AEC3EchoCancellerAudioProcessor)
};

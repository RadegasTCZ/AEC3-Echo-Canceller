#pragma once

#include <JuceHeader.h>
#include "WasapiLoopbackCapture.h"
#include "EchoCancellerEngine.h"
#include <vector>

class AEC3EchoCancellerAudioProcessor  : public juce::AudioProcessor
{
public:
    AEC3EchoCancellerAudioProcessor();
    ~AEC3EchoCancellerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    WasapiLoopbackCapture& getLoopbackCapture() { return loopbackCapture; }
    EchoCancellerEngine& getEchoCancellerEngine() { return echoCancellerEngine; }

private:
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

    // Device ID — stored outside APVTS (dynamic list doesn't fit APVTS well)
    juce::String selectedDeviceId;

    std::vector<float> monoInputBuffer;
    std::vector<float> monoOutputBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AEC3EchoCancellerAudioProcessor)
};

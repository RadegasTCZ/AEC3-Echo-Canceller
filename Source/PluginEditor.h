#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class AEC3EchoCancellerAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                private juce::Timer
{
public:
    AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor&);
    ~AEC3EchoCancellerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area);
    void drawPeakMeter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, float db);

    // Helper to create a labelled slider row
    struct SliderRow
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupSlider (SliderRow& row, const juce::String& name, const juce::String& paramId,
                      const juce::String& suffix = "", int textBoxWidth = 55,
                      const juce::String& tooltip = "");

    AEC3EchoCancellerAudioProcessor& audioProcessor;

    // Core toggles
    juce::ToggleButton enableButton { "AEC Enabled" };
    juce::ToggleButton monitorCaptureButton { "Monitor Capture" };

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> enableAttachment;
    std::unique_ptr<ButtonAttachment> monitorCaptureAttachment;

    // Normal suppression sliders
    SliderRow lfSuppression, lfTransparency, hfSuppression, hfTransparency;
    SliderRow lfAttack, lfDecay, hfDucking, hfEchoThreshold;

    // Double-talk sliders
    SliderRow dtLfSuppression, dtLfTransparency, dtHfSuppression, dtHfTransparency;
    SliderRow dtLfAttack, dtLfDecay;

    // System sliders
    SliderRow latencyComp, roomReverb;

    // System toggles
    juce::ToggleButton boundedErlButton { "Conservative Echo Est." };
    juce::ToggleButton clockDriftButton { "Clock Drift Comp." };
    std::unique_ptr<ButtonAttachment> boundedErlAttachment;
    std::unique_ptr<ButtonAttachment> clockDriftAttachment;

    // Device selection
    juce::Label deviceLabel;
    juce::ComboBox deviceCombo;
    std::vector<WasapiLoopbackCapture::DeviceInfo> cachedDevices;
    void refreshDeviceList();

    // Presets
    juce::ComboBox presetCombo;
    int presetFlashCountdown = 0; // timer ticks until combo reverts to "Presets"
    void applyPreset (int presetId);
    void applyFactoryReset();

    // Status + version
    juce::Label statusLabel;
    juce::Label versionLabel;

    // Pre-rendered peak meter gradient
    static constexpr float kMeterGamma = 2.0f; // >1 expands hot zone, compresses quiet end
    juce::Image meterGradientImage;
    void buildMeterGradient();

    // Metering bounds for targeted repaint
    juce::Rectangle<int> getMeteringBounds() const;

    // Spectrum display cache
    static constexpr int kSpectrumBands = 16;
    float spectrumBands[kSpectrumBands] = {};

    // Peak meter cache (dBFS)
    float peakIn  = -100.0f;
    float peakOut = -100.0f;
    float peakRef = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AEC3EchoCancellerAudioProcessorEditor)
};

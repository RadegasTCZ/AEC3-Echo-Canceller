#include "PluginProcessor.h"
#include "PluginEditor.h"

AEC3EchoCancellerAudioProcessorEditor::AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    addAndMakeVisible (enableButton);
    enableAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "aecEnabled", enableButton);

    addAndMakeVisible (aggressiveModeButton);
    aggressiveModeAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "aggressiveMode", aggressiveModeButton);

    addAndMakeVisible (monitorCaptureButton);
    monitorCaptureAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "monitorCapture", monitorCaptureButton);

    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setText ("Disconnected", juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
    addAndMakeVisible (statusLabel);

    setSize (400, 220);
    startTimer (500);
}

AEC3EchoCancellerAudioProcessorEditor::~AEC3EchoCancellerAudioProcessorEditor()
{
    stopTimer();
}

void AEC3EchoCancellerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void AEC3EchoCancellerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    int rowHeight = 30;
    int spacing = 10;

    enableButton.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (spacing);

    aggressiveModeButton.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (spacing);

    monitorCaptureButton.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (spacing);

    statusLabel.setBounds (area.removeFromTop (rowHeight));
}

void AEC3EchoCancellerAudioProcessorEditor::timerCallback()
{
    auto status = audioProcessor.getLoopbackCapture().getStatus();

    switch (status)
    {
        case WasapiLoopbackCapture::Status::Connected:
        {
            int rate = audioProcessor.getLoopbackCapture().getCapturedSampleRate();
            statusLabel.setText ("Connected (" + juce::String (rate) + " Hz)",
                                juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colours::limegreen);
            break;
        }
        case WasapiLoopbackCapture::Status::Connecting:
            statusLabel.setText ("Connecting...", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colours::yellow);
            break;
        case WasapiLoopbackCapture::Status::Error:
            statusLabel.setText ("Error", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
            break;
        case WasapiLoopbackCapture::Status::Disconnected:
        default:
            statusLabel.setText ("Disconnected", juce::dontSendNotification);
            statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
            break;
    }
}

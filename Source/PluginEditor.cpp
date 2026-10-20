#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
}

AEC3EchoCancellerAudioProcessorEditor::AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      header (p),
      echoBox (p),
      voiceBox (p),
      advancedPage (p)
{
    addAndMakeVisible (header);
    addAndMakeVisible (echoBox);
    addAndMakeVisible (voiceBox);
    addChildComponent (advancedPage);

    header.onAdvancedToggled = [this] (bool open) { showAdvanced (open); };
    header.onTabSelected = [this] (int tab) { advancedPage.setTab (tab); };
    advancedPage.getOtherTab().onDeviceReset = [this] { header.refreshDeviceList(); };

    // last: a Slider rebuilds its value box only on a look-and-feel change, so every child must exist first
    setLookAndFeel (&theme);

    setResizable (false, false);
    setSize (kWidth, kHeight);              // the only setSize call (fixed size, no resizing)

    showAdvanced (audioProcessor.isAdvancedOpen());
    echoBox.getStrengthSelector().refresh();
    updateConnection();

    lastTickSeconds = nowSeconds();
    startTimerHz (30);
}

AEC3EchoCancellerAudioProcessorEditor::~AEC3EchoCancellerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void AEC3EchoCancellerAudioProcessorEditor::showAdvanced (bool open)
{
    advancedShown = open;
    audioProcessor.setAdvancedOpen (open);
    echoBox.setVisible (! open);
    voiceBox.setVisible (! open);
    advancedPage.setVisible (open);
    header.setAdvancedShown (open);
    header.setTab (advancedPage.getTab());
}

void AEC3EchoCancellerAudioProcessorEditor::updateConnection()
{
    auto& loopback = audioProcessor.getLoopbackCapture();
    const auto status = loopback.getStatus();

    bool stalled = false;
    if (status == WasapiLoopbackCapture::Status::Connected)
    {
        // heartbeat: detect a capture thread stuck in a COM call
        const auto heartbeat = loopback.getLastHeartbeatTicks();
        const double staleSec = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - heartbeat);
        stalled = heartbeat > 0 && staleSec > 2.0;
    }

    using aec::ui::LinkState;
    const bool halted = audioProcessor.getEchoCancellerEngine().hasError();
    LinkState state = LinkState::Problem;                            // Stalled / Error / AEC3 Error
    juce::String word = "Error";
    if (halted)
        word = "AEC3 Error";
    else if (stalled)
        word = "Stalled";
    else if (status == WasapiLoopbackCapture::Status::Connected)
        state = LinkState::Connected, word = "Connected";
    else if (status == WasapiLoopbackCapture::Status::Connecting)
        state = LinkState::Connecting, word = "Connecting";
    else if (status == WasapiLoopbackCapture::Status::Disconnected)   // normal after releaseResources / stopCapture
        state = LinkState::Disconnected, word = "Disconnected";

    const int rate = status == WasapiLoopbackCapture::Status::Connected ? loopback.getCapturedSampleRate() : 0;
    echoBox.setConnection (state, rate);
    echoBox.setConnectionTooltip (halted ? "AEC3 Error - use Factory reset (Advanced > Other)" : word);
    advancedPage.getOtherTab().setStatus (halted ? "AEC3 Error - use Factory reset" : word, state, rate);
    if (const double hostRate = audioProcessor.getSampleRate(); hostRate > 0.0)
        advancedPage.getOtherTab().setLatencyMs (juce::roundToInt (1000.0 * audioProcessor.getLatencySamples() / hostRate));
}

void AEC3EchoCancellerAudioProcessorEditor::timerCallback()
{
    const double now = nowSeconds();
    const auto dt = (float) juce::jlimit (0.0, 0.1, now - lastTickSeconds);
    lastTickSeconds = now;

    if (advancedShown)
        advancedPage.tick();
    else
    {
        echoBox.tick (dt);
        voiceBox.tick (dt);
        echoBox.getStrengthSelector().refresh();
        voiceBox.getAgcSelector().refresh();
    }

    auto& loopback = audioProcessor.getLoopbackCapture();
    if (loopback.hasDeviceListChanged())
        header.refreshDeviceList();

    updateConnection();
}

void AEC3EchoCancellerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (aec::ui::colours::bg);
}

void AEC3EchoCancellerAudioProcessorEditor::resized()
{
    header.setBounds (0, 0, kWidth, aec::ui::Header::kHeight);
    echoBox.setBounds (8, 44, aec::ui::EchoBox::kWidth, aec::ui::EchoBox::kHeight);
    voiceBox.setBounds (8, 148, aec::ui::VoiceBox::kWidth, aec::ui::VoiceBox::kHeight);
    advancedPage.setBounds (8, 44, aec::ui::AdvancedPage::kWidth, aec::ui::AdvancedPage::kHeight);
}

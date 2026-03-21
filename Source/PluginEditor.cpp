#include "PluginProcessor.h"
#include "PluginEditor.h"

void AEC3EchoCancellerAudioProcessorEditor::buildMeterGradient()
{
    const int gradientW = 189; // meterAreaW(220) - labelW(28) - gap(3)
    const int gradientH = 10;  // meterH

    meterGradientImage = juce::Image (juce::Image::ARGB, gradientW, gradientH, true);

    const float dbMin = -80.0f;
    const float dbMax = 0.0f;
    const float yellowNorm = (-12.0f - dbMin) / (dbMax - dbMin);
    const float redNorm    = (-6.0f  - dbMin) / (dbMax - dbMin);

    juce::Image::BitmapData bmp (meterGradientImage, juce::Image::BitmapData::writeOnly);

    for (int px = 0; px < gradientW; ++px)
    {
        // Inverse gamma: pixel position → linear dB norm for color lookup
        float pxNorm = (float) px / (float) gradientW;
        float dbNorm = std::pow (pxNorm, 1.0f / kMeterGamma);
        float hue;

        if (dbNorm < yellowNorm)
        {
            float t = dbNorm / yellowNorm;
            hue = 0.55f - t * 0.4f;
        }
        else if (dbNorm < redNorm)
        {
            float t = (dbNorm - yellowNorm) / (redNorm - yellowNorm);
            hue = 0.15f - t * 0.08f;
        }
        else
        {
            hue = 0.0f;
        }

        auto col = juce::Colour::fromHSV (hue, 0.8f, 0.75f, 1.0f);
        for (int y = 0; y < gradientH; ++y)
            bmp.setPixelColour (px, y, col);
    }
}

juce::Rectangle<int> AEC3EchoCancellerAudioProcessorEditor::getMeteringBounds() const
{
    const int margin = 10;
    const int meterAreaW = 220;
    const int meterH = 10;
    const int meterGap = 3;
    const int graphH = 55;
    const int graphY = getHeight() - graphH - margin;
    const int meterX = getWidth() - meterAreaW - margin;
    const int myTop = graphY - 14 - (meterH + meterGap) * 3 - 14;

    return { meterX, myTop, meterAreaW, getHeight() - margin - myTop };
}

void AEC3EchoCancellerAudioProcessorEditor::setupSlider (SliderRow& row, const juce::String& name, const juce::String& paramId,
                                                         const juce::String& suffix, int textBoxWidth,
                                                         const juce::String& tooltip)
{
    row.label.setText (name, juce::dontSendNotification);
    row.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (row.label);

    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, textBoxWidth, 20);
    if (suffix.isNotEmpty())
        row.slider.setTextValueSuffix (" " + suffix);
    row.slider.setNumDecimalPlacesToDisplay (0);
    if (tooltip.isNotEmpty())
        row.slider.setTooltip (tooltip);
    addAndMakeVisible (row.slider);

    row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.getAPVTS(), paramId, row.slider);
}

AEC3EchoCancellerAudioProcessorEditor::AEC3EchoCancellerAudioProcessorEditor (AEC3EchoCancellerAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Core toggles
    addAndMakeVisible (enableButton);
    enableAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "aecEnabled", enableButton);

    addAndMakeVisible (monitorCaptureButton);
    monitorCaptureButton.setTooltip ("Output the raw loopback reference signal instead of AEC output (for debugging)");
    monitorCaptureAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "monitorCapture", monitorCaptureButton);

    // Normal suppression
    setupSlider (lfSuppression,   "LF Suppress",    "lfSuppression",   "%", 55,
        "How aggressively low-frequency echo is removed");
    setupSlider (lfTransparency,  "LF Transp.",     "lfTransparency",  "%", 55,
        "How quickly low-frequency suppression kicks in");
    setupSlider (hfSuppression,   "HF Suppress",    "hfSuppression",   "%", 55,
        "How aggressively high-frequency echo is removed");
    setupSlider (hfTransparency,  "HF Transp.",     "hfTransparency",  "%", 55,
        "How quickly high-frequency suppression kicks in");
    setupSlider (lfAttack,        "LF Attack",      "lfAttack",        "%", 55,
        "How fast suppression gain can increase (recover)");
    setupSlider (lfDecay,         "LF Decay",       "lfDecay",         "%", 55,
        "How fast suppression gain can decrease (attenuate)");
    setupSlider (hfDucking,       "HF Ducking",     "hfDucking",       "%", 55,
        "Reduce high-frequency content during detected echo");
    setupSlider (hfEchoThreshold, "HF Echo Thr.",   "hfEchoThreshold", "%", 55,
        "Sensitivity of high-frequency echo detection");

    // Double-talk suppression
    setupSlider (dtLfSuppression,  "DT LF Supp.",   "dtLfSuppression",  "%", 55,
        "Low-frequency suppression when you speak during playback");
    setupSlider (dtLfTransparency, "DT LF Trn.",    "dtLfTransparency",  "%", 55,
        "Low-frequency sensitivity during double-talk");
    setupSlider (dtHfSuppression,  "DT HF Supp.",   "dtHfSuppression",  "%", 55,
        "High-frequency suppression when you speak during playback");
    setupSlider (dtHfTransparency, "DT HF Trn.",    "dtHfTransparency",  "%", 55,
        "High-frequency sensitivity during double-talk");
    setupSlider (dtLfAttack,       "DT LF Atk.",    "dtLfAttack",       "%", 55,
        "Gain recovery speed during double-talk");
    setupSlider (dtLfDecay,        "DT LF Dec.",    "dtLfDecay",        "%", 55,
        "Gain attenuation speed during double-talk");

    // System sliders
    setupSlider (latencyComp, "Latency",    "latencyComp", "ms", 65,
        "Compensate for fixed delay between mic and reference (e.g. wireless mics)");
    latencyComp.slider.setNumDecimalPlacesToDisplay (0);
    setupSlider (roomReverb,  "Room Reverb", "roomReverb",  "",   55,
        "Echo tail length — increase for reverberant rooms");
    roomReverb.slider.textFromValueFunction = [] (double v) { return "L" + juce::String ((int) v); };
    roomReverb.slider.setNumDecimalPlacesToDisplay (0);

    // System toggles
    addAndMakeVisible (boundedErlButton);
    boundedErlButton.setTooltip ("Use conservative echo estimation — helps when echo is inconsistent");
    boundedErlAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "boundedErl", boundedErlButton);

    addAndMakeVisible (clockDriftButton);
    clockDriftButton.setTooltip ("Enable if capture and playback devices use different clocks (e.g. USB mic + onboard speakers)");
    clockDriftAttachment = std::make_unique<ButtonAttachment> (
        audioProcessor.getAPVTS(), "clockDrift", clockDriftButton);

    // Device selection
    deviceLabel.setText ("Device:", juce::dontSendNotification);
    deviceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (deviceLabel);

    deviceCombo.onChange = [this]
    {
        int idx = deviceCombo.getSelectedId();
        if (idx == 1)
        {
            audioProcessor.getLoopbackCapture().setDeviceId ("");
        }
        else if (idx >= 2 && (size_t) (idx - 2) < cachedDevices.size())
        {
            audioProcessor.getLoopbackCapture().setDeviceId (cachedDevices[(size_t) (idx - 2)].id);
        }
    };
    addAndMakeVisible (deviceCombo);
    refreshDeviceList();

    // Presets
    presetCombo.addItem ("Default",     1);
    presetCombo.addItem ("Aggressive",  2);
    presetCombo.addItem ("Transparent", 3);
    presetCombo.addItem ("Voice Chat",  4);
    presetCombo.addSeparator();
    presetCombo.addItem ("Factory Reset", 100);

    presetCombo.setTextWhenNothingSelected ("Presets");
    presetCombo.onChange = [this]
    {
        int id = presetCombo.getSelectedId();
        if (id == 100)
            applyFactoryReset();
        else if (id > 0)
            applyPreset (id);

        // Show the selected preset name briefly, then revert after ~1.5s
        if (id > 0)
            presetFlashCountdown = 22; // 22 ticks at 15Hz ≈ 1.5s
    };
    addAndMakeVisible (presetCombo);

    // Status
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setText ("Disconnected", juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::red);
    addAndMakeVisible (statusLabel);

    // Version
    versionLabel.setText ("Version: " JucePlugin_VersionString, juce::dontSendNotification);
    versionLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    versionLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (versionLabel);

    buildMeterGradient();

    setSize (820, 500);
    startTimerHz (15);
}

AEC3EchoCancellerAudioProcessorEditor::~AEC3EchoCancellerAudioProcessorEditor()
{
    stopTimer();
}

void AEC3EchoCancellerAudioProcessorEditor::refreshDeviceList()
{
    cachedDevices = WasapiLoopbackCapture::enumerateDevices();
    auto currentId = audioProcessor.getLoopbackCapture().getDeviceId();

    deviceCombo.clear (juce::dontSendNotification);
    deviceCombo.addItem ("System Default", 1);

    int selectedId = 1;
    for (int i = 0; i < (int) cachedDevices.size(); ++i)
    {
        int itemId = i + 2;
        deviceCombo.addItem (cachedDevices[(size_t) i].name, itemId);

        if (cachedDevices[(size_t) i].id == currentId)
            selectedId = itemId;
    }

    deviceCombo.setSelectedId (selectedId, juce::dontSendNotification);
}

void AEC3EchoCancellerAudioProcessorEditor::applyPreset (int presetId)
{
    auto& apvts = audioProcessor.getAPVTS();
    auto set = [&] (const juce::String& id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    // Preset slider values: { lfSupp, lfTrans, hfSupp, hfTrans, lfAtk, lfDec, hfDuck, hfEchoThr,
    //                         dtLfSupp, dtLfTrans, dtHfSupp, dtHfTrans, dtLfAtk, dtLfDec,
    //                         latency, roomReverb, boundedErl, clockDrift }
    switch (presetId)
    {
        case 1: // Default — AEC3 stock values
            set ("lfSuppression",    75.0f);  set ("lfTransparency",   80.0f);
            set ("hfSuppression",    80.0f);  set ("hfTransparency",   85.0f);
            set ("lfAttack",         30.0f);  set ("lfDecay",          25.0f);
            set ("hfDucking",         0.0f);  set ("hfEchoThreshold",  50.0f);
            set ("dtLfSuppression",  25.0f);  set ("dtLfTransparency", 30.0f);
            set ("dtHfSuppression",  40.0f);  set ("dtHfTransparency", 80.0f);
            set ("dtLfAttack",       30.0f);  set ("dtLfDecay",        25.0f);
            set ("latencyComp",       0.0f);  set ("roomReverb",       13.0f);
            set ("boundedErl",        0.0f);  set ("clockDrift",        0.0f);
            break;

        case 2: // Aggressive — speakers / heavy echo
            set ("lfSuppression",    95.0f);  set ("lfTransparency",  100.0f);
            set ("hfSuppression",    95.0f);  set ("hfTransparency",  100.0f);
            set ("lfAttack",         50.0f);  set ("lfDecay",          50.0f);
            set ("hfDucking",        50.0f);  set ("hfEchoThreshold",  75.0f);
            set ("dtLfSuppression",  50.0f);  set ("dtLfTransparency", 55.0f);
            set ("dtHfSuppression",  60.0f);  set ("dtHfTransparency", 90.0f);
            set ("dtLfAttack",       50.0f);  set ("dtLfDecay",        40.0f);
            set ("latencyComp",       0.0f);  set ("roomReverb",       20.0f);
            set ("boundedErl",        0.0f);  set ("clockDrift",        0.0f);
            break;

        case 3: // Transparent — headset / mild leakage
            set ("lfSuppression",    50.0f);  set ("lfTransparency",   60.0f);
            set ("hfSuppression",    50.0f);  set ("hfTransparency",   60.0f);
            set ("lfAttack",         20.0f);  set ("lfDecay",          15.0f);
            set ("hfDucking",         0.0f);  set ("hfEchoThreshold",  25.0f);
            set ("dtLfSuppression",  15.0f);  set ("dtLfTransparency", 20.0f);
            set ("dtHfSuppression",  25.0f);  set ("dtHfTransparency", 60.0f);
            set ("dtLfAttack",       20.0f);  set ("dtLfDecay",        15.0f);
            set ("latencyComp",       0.0f);  set ("roomReverb",       10.0f);
            set ("boundedErl",        0.0f);  set ("clockDrift",        0.0f);
            break;

        case 4: // Voice Chat — permissive double-talk
            set ("lfSuppression",    75.0f);  set ("lfTransparency",   80.0f);
            set ("hfSuppression",    80.0f);  set ("hfTransparency",   85.0f);
            set ("lfAttack",         30.0f);  set ("lfDecay",          25.0f);
            set ("hfDucking",         0.0f);  set ("hfEchoThreshold",  50.0f);
            set ("dtLfSuppression",  15.0f);  set ("dtLfTransparency", 15.0f);
            set ("dtHfSuppression",  20.0f);  set ("dtHfTransparency", 60.0f);
            set ("dtLfAttack",       20.0f);  set ("dtLfDecay",        15.0f);
            set ("latencyComp",       0.0f);  set ("roomReverb",       13.0f);
            set ("boundedErl",        0.0f);  set ("clockDrift",        0.0f);
            break;

        default:
            break;
    }
}

void AEC3EchoCancellerAudioProcessorEditor::applyFactoryReset()
{
    // Reset all params to defaults
    applyPreset (1);

    // Reset device selection to system default
    audioProcessor.getLoopbackCapture().setDeviceId ("");
    refreshDeviceList();

    // Re-enable AEC + disable monitor
    auto& apvts = audioProcessor.getAPVTS();
    if (auto* param = apvts.getParameter ("aecEnabled"))
        param->setValueNotifyingHost (1.0f);
    if (auto* param = apvts.getParameter ("monitorCapture"))
        param->setValueNotifyingHost (0.0f);

    // Signal the audio thread to do the full engine re-initialization
    audioProcessor.getEchoCancellerEngine().requestReset();
}

void AEC3EchoCancellerAudioProcessorEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<int> area)
{
    const float dbMin = -100.0f;
    const float dbMax = -10.0f;
    const float dbRange = dbMax - dbMin;

    // Background
    g.setColour (juce::Colour (0xff0a1528));
    g.fillRect (area);

    // Grid lines every 10dB
    g.setColour (juce::Colour (0xff1a2a40));
    for (float dbLine = -90.0f; dbLine <= -20.0f; dbLine += 10.0f)
    {
        float yNorm = (dbLine - dbMin) / dbRange;
        int yPx = area.getBottom() - (int) (yNorm * area.getHeight());
        g.drawHorizontalLine (yPx, (float) area.getX(), (float) area.getRight());
    }

    // 10px bars + 1px gaps
    const int barW = 10;
    const int gap = 1;
    int xOffset = area.getX();

    for (int i = 0; i < kSpectrumBands; ++i)
    {
        float mag = spectrumBands[i];
        float db = 20.0f * std::log10 (std::max (mag, 1e-7f));
        float norm = (db - dbMin) / dbRange;
        norm = juce::jlimit (0.0f, 1.0f, norm);

        int barH = (int) (norm * area.getHeight());
        int x = xOffset + i * (barW + gap);

        float hue = 0.55f - 0.4f * norm;
        g.setColour (juce::Colour::fromHSV (hue, 0.8f, 0.7f + 0.3f * norm, 1.0f));
        g.fillRect (x, area.getBottom() - barH, barW, barH);
    }

    // Border
    g.setColour (juce::Colour (0xff2a3a50));
    g.drawRect (area, 1);

    // Label
    g.setColour (juce::Colours::grey.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("Reference Spectrum", area.getX(), area.getY() - 12, area.getWidth(), 12, juce::Justification::centredLeft);
}

void AEC3EchoCancellerAudioProcessorEditor::drawPeakMeter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label, float db)
{
    const float dbMin = -80.0f;
    const float dbMax = 0.0f;

    // Label on the left
    int labelW = 28;
    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (label, area.getX(), area.getY(), labelW, area.getHeight(), juce::Justification::centredRight);

    auto barArea = area.withTrimmedLeft (labelW + 3);

    // Background
    g.setColour (juce::Colour (0xff0a1528));
    g.fillRect (barArea);

    // Blit pre-rendered gradient, clipped to fill width (non-linear: expand hot zone)
    float norm = juce::jlimit (0.0f, 1.0f, (db - dbMin) / (dbMax - dbMin));
    norm = std::pow (norm, kMeterGamma);
    int fillW = (int) (norm * barArea.getWidth());

    if (fillW > 0 && ! meterGradientImage.isNull())
    {
        int srcW = juce::jmin (fillW, meterGradientImage.getWidth());
        g.drawImage (meterGradientImage,
                     barArea.getX(), barArea.getY(), srcW, barArea.getHeight(),
                     0, 0, srcW, meterGradientImage.getHeight());
    }

    // Border
    g.setColour (juce::Colour (0xff2a3a50));
    g.drawRect (barArea, 1);
}

void AEC3EchoCancellerAudioProcessorEditor::timerCallback()
{
    // Revert preset combo label after flash period
    if (presetFlashCountdown > 0 && --presetFlashCountdown == 0)
        presetCombo.setSelectedId (0, juce::dontSendNotification);

    // Refresh device list if devices changed
    if (audioProcessor.getLoopbackCapture().hasDeviceListChanged())
        refreshDeviceList();

    // Copy spectrum and peak data from engine
    auto& engine = audioProcessor.getEchoCancellerEngine();
    for (int i = 0; i < kSpectrumBands; ++i)
        spectrumBands[i] = engine.displaySpectrumBands[i].load (std::memory_order_relaxed);

    peakIn  = engine.peakInDb.load (std::memory_order_relaxed);
    peakOut = engine.peakOutDb.load (std::memory_order_relaxed);
    peakRef = engine.peakRefDb.load (std::memory_order_relaxed);

    // Update status
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

    repaint (getMeteringBounds());
}

void AEC3EchoCancellerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff020b1a));

    int margin = 10;
    int toggleH = 28;
    int headerH = 20;
    int halfW = getWidth() / 2;
    int rowH = 24;
    int headerY = margin + toggleH + 6;

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));

    // Section headers
    g.drawText ("Echo Suppression (Normal)",      margin, headerY, halfW - margin, headerH, juce::Justification::centredLeft);
    g.drawText ("Echo Suppression (Double-Talk)", halfW,  headerY, halfW - margin, headerH, juce::Justification::centredLeft);

    // System header
    int systemY = headerY + headerH + 2 + rowH * 8 + 8;
    g.drawText ("System", margin, systemY, halfW - margin, headerH, juce::Justification::centredLeft);

    // Metering area — bottom right
    const int meterAreaW = 220;
    const int meterX = getWidth() - meterAreaW - margin;
    const int meterH = 10;
    const int meterGap = 3;

    // Spectrum (16 bars * 10 + 15 gaps = 175)
    const int graphW = 176; // +1 for right border pixel
    const int graphH = 55;
    const int graphX = getWidth() - graphW - margin;
    const int graphY = getHeight() - graphH - margin;
    drawSpectrum (g, { graphX, graphY, graphW, graphH });

    // Peak meters above spectrum
    int my = graphY - 14 - (meterH + meterGap) * 3;
    g.setColour (juce::Colours::grey.withAlpha (0.6f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("Level Meters", meterX, my - 12, meterAreaW, 12, juce::Justification::centredLeft);

    drawPeakMeter (g, { meterX, my, meterAreaW, meterH }, "IN",  peakIn);  my += meterH + meterGap;
    drawPeakMeter (g, { meterX, my, meterAreaW, meterH }, "OUT", peakOut); my += meterH + meterGap;
    drawPeakMeter (g, { meterX, my, meterAreaW, meterH }, "REF", peakRef);
}

void AEC3EchoCancellerAudioProcessorEditor::resized()
{
    int margin = 10;
    int rowH = 24;
    int labelW = 95;
    int toggleH = 28;
    int headerH = 20;
    int halfW = getWidth() / 2;

    // AEC Enabled — top left, Presets — top right
    enableButton.setBounds (margin, margin, 200, toggleH);
    presetCombo.setBounds (getWidth() - margin - 140, margin, 140, toggleH);

    // Slider columns start below the header
    int sliderStartY = margin + toggleH + 6 + headerH + 2;

    auto layoutSliderAt = [] (SliderRow& row, int x, int y, int w, int lw, int rh)
    {
        row.label.setBounds (x, y, lw, rh);
        row.slider.setBounds (x + lw, y, w - lw, rh);
    };

    // Left column — Normal suppression (8 sliders)
    int lx = margin;
    int lw = halfW - margin * 2;
    int y = sliderStartY;

    layoutSliderAt (lfSuppression,   lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (lfTransparency,  lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (hfSuppression,   lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (hfTransparency,  lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (lfAttack,        lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (lfDecay,         lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (hfDucking,       lx, y, lw, labelW, rowH); y += rowH;
    layoutSliderAt (hfEchoThreshold, lx, y, lw, labelW, rowH); y += rowH;

    // Right column — Double-talk suppression (6 sliders)
    int rx = halfW;
    int rw = halfW - margin;
    y = sliderStartY;

    layoutSliderAt (dtLfSuppression,  rx, y, rw, labelW, rowH); y += rowH;
    layoutSliderAt (dtLfTransparency, rx, y, rw, labelW, rowH); y += rowH;
    layoutSliderAt (dtHfSuppression,  rx, y, rw, labelW, rowH); y += rowH;
    layoutSliderAt (dtHfTransparency, rx, y, rw, labelW, rowH); y += rowH;
    layoutSliderAt (dtLfAttack,       rx, y, rw, labelW, rowH); y += rowH;
    layoutSliderAt (dtLfDecay,        rx, y, rw, labelW, rowH); y += rowH;

    // System section — below left column
    int systemY = sliderStartY + rowH * 8 + 8 + headerH + 2;

    // Device selection first
    deviceLabel.setBounds (lx, systemY, labelW, rowH);
    deviceCombo.setBounds (lx + labelW, systemY, lw - labelW, rowH);
    systemY += rowH;

    layoutSliderAt (latencyComp, lx, systemY, lw, labelW, rowH); systemY += rowH;
    layoutSliderAt (roomReverb,  lx, systemY, lw, labelW, rowH); systemY += rowH;

    systemY += 4;

    // System toggles
    boundedErlButton.setBounds (lx, systemY, 200, toggleH);
    clockDriftButton.setBounds (lx + 210, systemY, 200, toggleH);
    systemY += toggleH + 8;

    // Monitor Capture
    monitorCaptureButton.setBounds (margin, systemY, 200, toggleH);

    // Status — bottom left, above version
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setBounds (margin, getHeight() - 38, 250, 16);

    // Version — bottom left
    versionLabel.setBounds (margin, getHeight() - 20, 150, 16);
}

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamIds.h"
#include "StateMigration.h"
#include "AgcPresets.h"

/** nsLevel choice index from its raw value; a non-finite value (NaN automation) is the default 12 dB. */
static int nsLevelIndex (float raw)
{
    return std::isfinite (raw) ? juce::roundToInt (raw) : 2;
}

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Core controls
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "aecEnabled", 1 }, "AEC Enabled", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "monitorCapture", 1 }, "Monitor Capture", false));

    // Normal suppression (0-100%, step 5)
    auto pctRange = juce::NormalisableRange<float> (0.0f, 100.0f, 5.0f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfSuppression", 1 }, "LF Suppression", pctRange, 75.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfTransparency", 1 }, "LF Transparency", pctRange, 80.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfSuppression", 1 }, "HF Suppression", pctRange, 80.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfTransparency", 1 }, "HF Transparency", pctRange, 85.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfAttack", 1 }, "LF Attack", pctRange, 35.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfDecay", 1 }, "LF Decay", pctRange, 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfDucking", 1 }, "HF Ducking", pctRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfEchoThreshold", 1 }, "HF Echo Threshold", pctRange, 55.0f));

    // Double-talk suppression (0-100%, step 5)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtLfSuppression", 1 }, "DT LF Suppression", pctRange, 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtLfTransparency", 1 }, "DT LF Transparency", pctRange, 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtHfSuppression", 1 }, "DT HF Suppression", pctRange, 40.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtHfTransparency", 1 }, "DT HF Transparency", pctRange, 80.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtLfAttack", 1 }, "DT LF Attack", pctRange, 35.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtLfDecay", 1 }, "DT LF Decay", pctRange, 25.0f));

    // System params
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "latencyComp", 1 }, "Latency Compensation",
        juce::NormalisableRange<float> (0.0f, 200.0f, 5.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "roomReverb", 1 }, "Room Reverberance",
        juce::NormalisableRange<float> (13.0f, 40.0f, 1.0f), 13.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "boundedErl", 1 }, "Conservative Echo Estimation", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "clockDrift", 1 }, "Clock Drift Compensation", false));

    // Noise suppression (v6)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { aec::ids::nsLevel, 1 }, "Noise Level",
        juce::StringArray { "Off", "6 dB", "12 dB", "18 dB", "21 dB" }, 2));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { aec::ids::monitorNoise, 1 }, "Hear Removed", false));

    // Level: auto gain (AGC2 adaptive digital) and output limiter (v6.1)
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { aec::ids::agcEnabled, 1 }, "Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { aec::ids::agcHeadroom, 1 }, "AGC Headroom",
        juce::NormalisableRange<float> (0.0f, 20.0f, 1.0f), 5.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { aec::ids::agcMaxGain, 1 }, "AGC Max Gain",
        juce::NormalisableRange<float> (1.0f, 50.0f, 1.0f), 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { aec::ids::agcAdaptSpeed, 1 }, "AGC Adapt Speed",
        juce::NormalisableRange<float> (1.0f, 30.0f, 1.0f), 6.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { aec::ids::agcNoiseCeiling, 1 }, "AGC Noise Ceiling",
        juce::NormalisableRange<float> (-80.0f, -20.0f, 1.0f), -50.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { aec::ids::limiterEnabled, 1 }, "Output Limiter", true));

    return layout;
}

AEC3EchoCancellerAudioProcessor::AEC3EchoCancellerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#else
     :
#endif
       parameters (*this, nullptr, juce::Identifier ("AEC3"), createParameterLayout())
{
    // Core
    enableParam         = parameters.getRawParameterValue ("aecEnabled");
    monitorCaptureParam = parameters.getRawParameterValue ("monitorCapture");

    // Normal suppression
    lfSuppressionParam   = parameters.getRawParameterValue ("lfSuppression");
    lfTransparencyParam  = parameters.getRawParameterValue ("lfTransparency");
    hfSuppressionParam   = parameters.getRawParameterValue ("hfSuppression");
    hfTransparencyParam  = parameters.getRawParameterValue ("hfTransparency");
    lfAttackParam        = parameters.getRawParameterValue ("lfAttack");
    lfDecayParam         = parameters.getRawParameterValue ("lfDecay");
    hfDuckingParam       = parameters.getRawParameterValue ("hfDucking");
    hfEchoThresholdParam = parameters.getRawParameterValue ("hfEchoThreshold");

    // Double-talk suppression
    dtLfSuppressionParam  = parameters.getRawParameterValue ("dtLfSuppression");
    dtLfTransparencyParam = parameters.getRawParameterValue ("dtLfTransparency");
    dtHfSuppressionParam  = parameters.getRawParameterValue ("dtHfSuppression");
    dtHfTransparencyParam = parameters.getRawParameterValue ("dtHfTransparency");
    dtLfAttackParam       = parameters.getRawParameterValue ("dtLfAttack");
    dtLfDecayParam        = parameters.getRawParameterValue ("dtLfDecay");

    // System
    latencyCompParam = parameters.getRawParameterValue ("latencyComp");
    roomReverbParam  = parameters.getRawParameterValue ("roomReverb");
    boundedErlParam  = parameters.getRawParameterValue ("boundedErl");
    clockDriftParam  = parameters.getRawParameterValue ("clockDrift");

    // Noise suppression
    nsLevelParam      = parameters.getRawParameterValue (aec::ids::nsLevel);
    monitorNoiseParam = parameters.getRawParameterValue (aec::ids::monitorNoise);

    // Level
    limiterEnabledParam = parameters.getRawParameterValue (aec::ids::limiterEnabled);

    startTimer (250);   // capture watchdog: restarts a dead capture thread even with no editor open
}

AEC3EchoCancellerAudioProcessor::~AEC3EchoCancellerAudioProcessor()
{
    // Message thread (see the header): stopTimer() then cannot overlap a watchdog callback. stopCapture()
    // also blocks any later restart on its own (wantRunning).
    stopTimer();
    loopbackCapture.stopCapture();
}

void AEC3EchoCancellerAudioProcessor::timerCallback()
{
    loopbackCapture.checkHealthAndRecover();   // restart the capture thread if it died
}

const juce::String AEC3EchoCancellerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AEC3EchoCancellerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AEC3EchoCancellerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AEC3EchoCancellerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AEC3EchoCancellerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AEC3EchoCancellerAudioProcessor::getNumPrograms()
{
    return 1;
}

int AEC3EchoCancellerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AEC3EchoCancellerAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AEC3EchoCancellerAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AEC3EchoCancellerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

void AEC3EchoCancellerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    echoCancellerEngine.setNoiseLevel (nsLevelIndex (nsLevelParam->load()));   // first suppressor at the saved level
    echoCancellerEngine.setAgc (aec::readAgcSettings (parameters));            // first AGC stage from the saved values
    echoCancellerEngine.setLimiterEnabled (limiterEnabledParam->load() >= 0.5f);
    echoCancellerEngine.prepare (sampleRate, samplesPerBlock, loopbackCapture.getCapturedSampleRate());

    preparedBlock = juce::jmax (1, samplesPerBlock);
    monoInputBuffer.assign (static_cast<size_t> (preparedBlock), 0.0f);
    monoOutputBuffer.assign (static_cast<size_t> (preparedBlock), 0.0f);
    dryBuffer.assign (static_cast<size_t> (preparedBlock), 0.0f);

    // Firm latency (frame + band split + AEC3 + noise suppressor, padded to a constant)
    const int latency = echoCancellerEngine.getLatencySamples();
    setLatencySamples (latency);

    // Dry path: the mono mic delayed by exactly the reported latency
    dryDelay.prepare (latency);
    dryDelay.setDelay (latency);

    // Start on the dry path. A plugin that is enabled holds it until the engine's output is real
    // again (one latency plus its fade-in), so the first output is never the engine's priming silence.
    bypassCount = kBypassRampSamples;
    engineActive = enableParam->load() >= 0.5f;
    holdRemaining = engineActive ? echoCancellerEngine.getPrimingSamples() : 0;
    echoCancellerEngine.setEnabled (engineActive);

    if (startCaptureOnPrepare)
        loopbackCapture.startCapture();
}

void AEC3EchoCancellerAudioProcessor::releaseResources()
{
    loopbackCapture.stopCapture();
    echoCancellerEngine.release();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AEC3EchoCancellerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void AEC3EchoCancellerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, false);
}

void AEC3EchoCancellerAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processInternal (buffer, true);   // same latency as the normal path
}

void AEC3EchoCancellerAudioProcessor::processInternal (juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || preparedBlock <= 0)
        return;

    // Clear unused output channels
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Engine settings from parameters
    echoCancellerEngine.setMonitorFarEnd (monitorCaptureParam->load() >= 0.5f);

    EchoCancellerEngine::Params p;
    p.lfSuppression   = lfSuppressionParam->load();
    p.lfTransparency  = lfTransparencyParam->load();
    p.hfSuppression   = hfSuppressionParam->load();
    p.hfTransparency  = hfTransparencyParam->load();
    p.lfAttack        = lfAttackParam->load();
    p.lfDecay         = lfDecayParam->load();
    p.hfDucking       = hfDuckingParam->load();
    p.hfEchoThreshold = hfEchoThresholdParam->load();

    p.dtLfSuppression  = dtLfSuppressionParam->load();
    p.dtLfTransparency = dtLfTransparencyParam->load();
    p.dtHfSuppression  = dtHfSuppressionParam->load();
    p.dtHfTransparency = dtHfTransparencyParam->load();
    p.dtLfAttack       = dtLfAttackParam->load();
    p.dtLfDecay        = dtLfDecayParam->load();

    p.roomReverb    = roomReverbParam->load();
    p.boundedErl    = boundedErlParam->load() >= 0.5f;
    p.clockDrift    = clockDriftParam->load() >= 0.5f;

    // Both setters replace non-finite values (NaN automation) by their defaults
    echoCancellerEngine.setLatencyCompMs (latencyCompParam->load());
    echoCancellerEngine.setParams (p);

    echoCancellerEngine.setNoiseLevel (nsLevelIndex (nsLevelParam->load()));
    echoCancellerEngine.setMonitorNoiseResidual (monitorNoiseParam->load() >= 0.5f);

    // Auto gain (sanitised, debounced, built by the worker) and the output limiter
    echoCancellerEngine.setAgc (aec::readAgcSettings (parameters));
    echoCancellerEngine.setLimiterEnabled (limiterEnabledParam->load() >= 0.5f);

    // A halted or unusable engine passes the mic through undelayed: treat it as bypass, and on the
    // fault edge snap to the (correctly delayed) dry signal since the wet path is already invalid.
    const bool faulted = echoCancellerEngine.hasError() || ! echoCancellerEngine.isReady();
    const bool bypass = forceBypass || enableParam->load() < 0.5f || faulted;
    const int priming = echoCancellerEngine.getPrimingSamples();

    if (faulted && ! wasFaulted)
        bypassCount = kBypassRampSamples;
    else if (! faulted && wasFaulted)
        holdRemaining = priming;   // engine re-primes on recovery: hold dry until its output is real
    wasFaulted = faulted;

    // Blocks larger than announced are split into prepared-size chunks (no reallocation)
    for (int offset = 0; offset < numSamples; offset += preparedBlock)
    {
        const int n = juce::jmin (preparedBlock, numSamples - offset);
        float* mono = monoInputBuffer.data();
        float* wet  = monoOutputBuffer.data();
        float* dry  = dryBuffer.data();

        // Downmix input channels to mono
        if (totalNumInputChannels > 1)
        {
            const float gain = 1.0f / (float) totalNumInputChannels;
            const float* ch0 = buffer.getReadPointer (0) + offset;

            for (int i = 0; i < n; ++i)
                mono[i] = ch0[i] * gain;

            for (int ch = 1; ch < totalNumInputChannels; ++ch)
            {
                const float* src = buffer.getReadPointer (ch) + offset;
                for (int i = 0; i < n; ++i)
                    mono[i] += src[i] * gain;
            }
        }
        else if (totalNumInputChannels == 1)
        {
            std::memcpy (mono, buffer.getReadPointer (0) + offset, sizeof (float) * static_cast<size_t> (n));
        }
        else
        {
            std::fill (mono, mono + n, 0.0f);
        }

        // Dry path, always running so its delay line stays aligned
        dryDelay.process (mono, dry, n);

        // Engine on/off follows the bypass ramp: off only once the output is pure dry; back on
        // with the output held on dry until the engine's output is real again.
        if (bypass && engineActive && bypassCount >= kBypassRampSamples)
        {
            engineActive = false;
            holdRemaining = 0;
            echoCancellerEngine.setEnabled (false);   // pauses capture
        }
        else if (! bypass && ! engineActive)
        {
            engineActive = true;
            holdRemaining = priming;
            echoCancellerEngine.setEnabled (true);
        }

        echoCancellerEngine.process (mono, wet, n, loopbackCapture);

        // out = b * dry + (1 - b) * wet, exact dry when b == 1
        bool dryAudible = false;

        for (int i = 0; i < n; ++i)
        {
            const bool holding = holdRemaining > 0;
            if (holding)
                --holdRemaining;

            if (bypass)
                bypassCount = juce::jmin (kBypassRampSamples, bypassCount + 1);
            else if (! holding)
                bypassCount = juce::jmax (0, bypassCount - 1);

            if (bypassCount > 0)
                dryAudible = true;

            const float b = (float) bypassCount / (float) kBypassRampSamples;
            wet[i] = bypassCount >= kBypassRampSamples ? dry[i] : b * dry[i] + (1.0f - b) * wet[i];
        }

        // The engine only meters its own output; while the dry path is audible OUT shows what is heard
        if (dryAudible)
            echoCancellerEngine.outPeak.push (wet, n);

        // Copy mono output to all output channels
        for (int ch = 0; ch < totalNumOutputChannels; ++ch)
            std::memcpy (buffer.getWritePointer (ch) + offset, wet, sizeof (float) * static_cast<size_t> (n));
    }
}

bool AEC3EchoCancellerAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AEC3EchoCancellerAudioProcessor::createEditor()
{
    return new AEC3EchoCancellerAudioProcessorEditor (*this);
}

void AEC3EchoCancellerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Save device ID alongside APVTS state
    xml->setAttribute ("captureDeviceId", loopbackCapture.getDeviceId());

    // Save UI mode (basic / advanced)
    xml->setAttribute ("uiMode", uiMode);

    copyXmlToBinary (*xml, destData);
}

void AEC3EchoCancellerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        // Restore device ID
        selectedDeviceId = xml->getStringAttribute ("captureDeviceId", "");
        loopbackCapture.setDeviceId (selectedDeviceId);

        parameters.replaceState (juce::ValueTree::fromXml (*xml));
        aec::applyMissingDefaults (parameters, *xml);

        // Restore UI mode (basic / advanced) — fall back to "basic" if missing/invalid
        auto stateMode = xml->getStringAttribute ("uiMode", "basic");
        if (stateMode == "basic" || stateMode == "advanced")
            uiMode = stateMode;
        else
            uiMode = "basic";
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AEC3EchoCancellerAudioProcessor();
}

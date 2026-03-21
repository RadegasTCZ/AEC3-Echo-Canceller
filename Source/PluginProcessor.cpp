#include "PluginProcessor.h"
#include "PluginEditor.h"

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
        juce::ParameterID { "lfAttack", 1 }, "LF Attack", pctRange, 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lfDecay", 1 }, "LF Decay", pctRange, 25.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfDucking", 1 }, "HF Ducking", pctRange, 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "hfEchoThreshold", 1 }, "HF Echo Threshold", pctRange, 50.0f));

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
        juce::ParameterID { "dtLfAttack", 1 }, "DT LF Attack", pctRange, 30.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dtLfDecay", 1 }, "DT LF Decay", pctRange, 25.0f));

    // System params
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "latencyComp", 1 }, "Latency Compensation",
        juce::NormalisableRange<float> (0.0f, 200.0f, 5.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "roomReverb", 1 }, "Room Reverberance",
        juce::NormalisableRange<float> (1.0f, 40.0f, 1.0f), 13.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "boundedErl", 1 }, "Conservative Echo Estimation", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "clockDrift", 1 }, "Clock Drift Compensation", false));

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
}

AEC3EchoCancellerAudioProcessor::~AEC3EchoCancellerAudioProcessor()
{
    loopbackCapture.stopCapture();
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
    echoCancellerEngine.prepare (sampleRate, samplesPerBlock);

    monoInputBuffer.resize (static_cast<size_t> (samplesPerBlock));
    monoOutputBuffer.resize (static_cast<size_t> (samplesPerBlock));

    // Report latency: one AEC3 frame (480 samples at 48kHz) converted to host rate
    int aecLatency = static_cast<int> (std::ceil (480.0 * sampleRate / 48000.0));
    setLatencySamples (aecLatency);

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

void AEC3EchoCancellerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Update engine state from parameters
    echoCancellerEngine.setEnabled (enableParam->load() >= 0.5f);
    echoCancellerEngine.setMonitorFarEnd (monitorCaptureParam->load() >= 0.5f);

    // Build params struct from APVTS atomics
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

    echoCancellerEngine.setLatencyCompMs (latencyCompParam->load());
    echoCancellerEngine.setParams (p);

    // Downmix input channels to mono
    if (totalNumInputChannels > 1)
    {
        const float gain = 1.0f / (float) totalNumInputChannels;
        const float* ch0 = buffer.getReadPointer (0);

        for (int i = 0; i < numSamples; ++i)
            monoInputBuffer[static_cast<size_t> (i)] = ch0[i] * gain;

        for (int ch = 1; ch < totalNumInputChannels; ++ch)
        {
            const float* src = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                monoInputBuffer[static_cast<size_t> (i)] += src[i] * gain;
        }

        echoCancellerEngine.process (monoInputBuffer.data(), monoOutputBuffer.data(),
                                    numSamples, loopbackCapture);
    }
    else if (totalNumInputChannels == 1)
    {
        echoCancellerEngine.process (buffer.getReadPointer (0), monoOutputBuffer.data(),
                                    numSamples, loopbackCapture);
    }
    else
    {
        std::memset (monoOutputBuffer.data(), 0, sizeof (float) * static_cast<size_t> (numSamples));
    }

    // Copy mono output to all output channels
    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
    {
        float* dest = buffer.getWritePointer (ch);
        std::memcpy (dest, monoOutputBuffer.data(), sizeof (float) * static_cast<size_t> (numSamples));
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
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AEC3EchoCancellerAudioProcessor();
}

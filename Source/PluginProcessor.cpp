#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "aecEnabled", 1 }, "AEC Enabled", true));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "aggressiveMode", 1 }, "Aggressive Mode", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "monitorCapture", 1 }, "Monitor Capture", false));

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
    enableParam = parameters.getRawParameterValue ("aecEnabled");
    aggressiveModeParam = parameters.getRawParameterValue ("aggressiveMode");
    monitorCaptureParam = parameters.getRawParameterValue ("monitorCapture");
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
    echoCancellerEngine.setAggressiveMode (aggressiveModeParam->load() >= 0.5f);
    echoCancellerEngine.setMonitorFarEnd (monitorCaptureParam->load() >= 0.5f);

    // Extract channel 0 as mono input
    if (totalNumInputChannels > 0)
    {
        const float* ch0 = buffer.getReadPointer (0);
        std::memcpy (monoInputBuffer.data(), ch0, sizeof (float) * static_cast<size_t> (numSamples));
    }
    else
    {
        std::memset (monoInputBuffer.data(), 0, sizeof (float) * static_cast<size_t> (numSamples));
    }

    // Process through echo canceller
    echoCancellerEngine.process (monoInputBuffer.data(), monoOutputBuffer.data(),
                                numSamples, loopbackCapture);

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
    copyXmlToBinary (*xml, destData);
}

void AEC3EchoCancellerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AEC3EchoCancellerAudioProcessor();
}

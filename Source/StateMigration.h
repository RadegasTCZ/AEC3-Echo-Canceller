#pragma once

#include <JuceHeader.h>

namespace aec
{
    /** After APVTS::replaceState: every parameter whose PARAM child was absent from `loaded`, whose
        saved value is not a finite number, or whose current value is not finite is set to its default
        (setValueNotifyingHost(getDefaultValue())). Out-of-range values were already clamped by the
        APVTS range; this function re-asserts it for roomReverb/latencyComp. */
    void applyMissingDefaults (juce::AudioProcessorValueTreeState&, const juce::XmlElement& loaded);
}

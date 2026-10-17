#pragma once

#include <JuceHeader.h>

namespace PresetIO
{
    /** All parameter IDs that a preset file carries. */
    extern const juce::StringArray kPresetParamIds;

    /** Write the current APVTS state of the listed audio parameters to file as INI.
        @returns true on success. errorOut is populated on failure. */
    bool savePreset (const juce::File& file,
                     const juce::AudioProcessorValueTreeState& apvts,
                     juce::String& errorOut);

    /** Forgiving load: rejects (apvts untouched, errorOut set) only if the file lacks an
        [AEC3Preset] section or any recognised key. Otherwise unparseable/missing keys take the
        parameter default, numbers are clamped to range, unknown keys are ignored. */
    bool loadPreset (const juce::File& file,
                     juce::AudioProcessorValueTreeState& apvts,
                     juce::String& errorOut);
}

#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace aec::help
{
struct HelpEntry
{
    juce::String title;
    juce::String whatItDoes;
    juce::String whenToAdjust;
    std::vector<juce::String> relatedIds;
};

/** Entry for a parameter ID, or nullptr if there is none. */
const HelpEntry* find (const juce::String& paramId);

/** All help entries, keyed by parameter ID. */
const std::map<juce::String, HelpEntry>& all();
} // namespace aec::help

#include "StateMigration.h"
#include "ParamIds.h"
#include <cmath>

namespace aec
{
    void applyMissingDefaults (juce::AudioProcessorValueTreeState& apvts, const juce::XmlElement& loaded)
    {
        // A PARAM that is absent, or whose value is not a finite number ("nan" parses as NaN and
        // passes every range clamp), counts as missing.
        juce::StringArray present;

        for (auto* child : loaded.getChildWithTagNameIterator ("PARAM"))
            if (std::isfinite (child->getStringAttribute ("value").getFloatValue()))
                present.add (child->getStringAttribute ("id"));

        for (auto* p : apvts.processor.getParameters())
        {
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const float v = ranged->getValue();
                const bool finite = std::isfinite (v) && std::isfinite (ranged->convertFrom0to1 (v));

                if (! finite || ! present.contains (ranged->getParameterID()))
                    ranged->setValueNotifyingHost (ranged->getDefaultValue());
            }
        }

        // Re-assert range clamping for the two parameters with a restricted range.
        for (auto* id : { ids::roomReverb, ids::latencyComp })
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue()));
    }
}

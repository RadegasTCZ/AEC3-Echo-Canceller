#include "PresetIO.h"
#include <map>
#include <vector>

namespace PresetIO
{

const juce::StringArray kPresetParamIds {
    // Suppression
    "lfSuppression", "lfTransparency", "hfSuppression", "hfTransparency",
    "lfAttack", "lfDecay", "hfDucking", "hfEchoThreshold",
    // Double-talk
    "dtLfSuppression", "dtLfTransparency", "dtHfSuppression", "dtHfTransparency",
    "dtLfAttack", "dtLfDecay",
    // System
    "latencyComp", "roomReverb", "boundedErl", "clockDrift", "nsLevel",
    // Level
    "agcEnabled", "agcHeadroom", "agcMaxGain", "agcAdaptSpeed", "agcNoiseCeiling", "limiterEnabled"
};

namespace
{
    struct ParsedKv { juce::String section, key, value; int lineNumber; };

    bool isBoolParam (const juce::String& id)
    {
        return id == "boundedErl" || id == "clockDrift" || id == "agcEnabled" || id == "limiterEnabled";
    }

    bool isChoiceParam (const juce::String& id)
    {
        return id == "nsLevel";
    }

    juce::String paramLocaleSafeFloat (float v)
    {
        // Force '.' decimal separator regardless of locale.
        return juce::String (v, 4, false);
    }

    bool isStrictNumeric (const juce::String& s)
    {
        // Accepts: optional leading sign, digits, at most one '.', optional digits.
        // Rejects: empty, whitespace-only, alphabetic, multiple dots, hex/exponent.
        // We intentionally don't accept exponent notation since savePreset never writes it.
        if (s.isEmpty()) return false;

        int i = 0;
        if (s[i] == '-' || s[i] == '+') ++i;
        bool sawDigit = false;
        bool sawDot   = false;
        for (; i < s.length(); ++i)
        {
            const auto c = s[i];
            if (c >= '0' && c <= '9')      sawDigit = true;
            else if (c == '.' && ! sawDot) sawDot   = true;
            else                           return false;
        }
        return sawDigit;
    }

    /** Forgiving INI parse: malformed lines are skipped, not fatal. */
    std::vector<ParsedKv> parseLines (const juce::StringArray& lines, bool& sawHeader)
    {
        std::vector<ParsedKv> kvs;
        juce::String currentSection;
        for (int i = 0; i < lines.size(); ++i)
        {
            auto raw = lines[i].trim();
            if (raw.isEmpty() || raw.startsWithChar (';') || raw.startsWithChar ('#'))
                continue;

            if (raw.startsWithChar ('['))
            {
                if (raw.endsWithChar (']'))
                    currentSection = raw.substring (1, raw.length() - 1).trim();
                if (currentSection == "AEC3Preset")
                    sawHeader = true;
                continue;
            }

            const int eq = raw.indexOfChar ('=');
            if (eq < 0)
                continue;

            kvs.push_back ({ currentSection, raw.substring (0, eq).trim(),
                             raw.substring (eq + 1).trim(), i + 1 });
        }
        return kvs;
    }
}

bool savePreset (const juce::File& file,
                 const juce::AudioProcessorValueTreeState& apvts,
                 juce::String& errorOut)
{
    juce::String content;
    content
        << "[AEC3Preset]" << juce::newLine
        << "version=1"   << juce::newLine
        << "name="       << file.getFileNameWithoutExtension() << juce::newLine
        << juce::newLine;

    auto writeKv = [&] (const juce::String& id)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const float normalised = p->getValue();
            const auto& range = apvts.getParameterRange (id);
            const float real = range.convertFrom0to1 (normalised);

            if (isBoolParam (id))
                content << id << "=" << (real >= 0.5f ? "true" : "false") << juce::newLine;
            else if (isChoiceParam (id))
                content << id << "=" << juce::roundToInt (real) << juce::newLine;
            else
                content << id << "=" << paramLocaleSafeFloat (real) << juce::newLine;
        }
    };

    content << "[Suppression]" << juce::newLine;
    for (auto* id : { "lfSuppression", "lfTransparency", "hfSuppression", "hfTransparency",
                      "lfAttack", "lfDecay", "hfDucking", "hfEchoThreshold" })
        writeKv (id);
    content << juce::newLine;

    content << "[DoubleTalk]" << juce::newLine;
    for (auto* id : { "dtLfSuppression", "dtLfTransparency", "dtHfSuppression", "dtHfTransparency",
                      "dtLfAttack", "dtLfDecay" })
        writeKv (id);
    content << juce::newLine;

    content << "[System]" << juce::newLine;
    for (auto* id : { "latencyComp", "roomReverb", "boundedErl", "clockDrift", "nsLevel" })
        writeKv (id);
    content << juce::newLine;

    content << "[Level]" << juce::newLine;
    for (auto* id : { "agcEnabled", "agcHeadroom", "agcMaxGain", "agcAdaptSpeed", "agcNoiseCeiling",
                      "limiterEnabled" })
        writeKv (id);

    if (! file.replaceWithText (content, false, false, "\n"))
    {
        errorOut = "Could not write to file: " + file.getFullPathName();
        return false;
    }
    return true;
}

bool loadPreset (const juce::File& file,
                 juce::AudioProcessorValueTreeState& apvts,
                 juce::String& errorOut)
{
    // Step 1: Open
    if (! file.existsAsFile())
    {
        errorOut = "File does not exist: " + file.getFullPathName();
        return false;
    }

    // A preset is about 1 KB: never read a large file picked by mistake into memory
    constexpr juce::int64 kMaxPresetBytes = 64 * 1024;
    if (file.getSize() > kMaxPresetBytes)
    {
        errorOut = "Not an AEC3 Echo Canceller preset file (larger than 64 KB).";
        return false;
    }

    auto text = file.loadFileAsString();
    auto lines = juce::StringArray::fromLines (text);

    // Only a file without the header, or without any recognised key, is rejected.
    bool hasHeader = false;
    const auto kvs = parseLines (lines, hasHeader);

    if (! hasHeader)
    {
        errorOut = "Not an AEC3 Echo Canceller preset file (missing [AEC3Preset] section).";
        return false;
    }

    std::map<juce::String, juce::String> values; // id -> raw string (recognised keys only)
    for (const auto& kv : kvs)
        if (kv.section != "AEC3Preset" && kPresetParamIds.contains (kv.key))
            values[kv.key] = kv.value;

    if (values.empty())
    {
        errorOut = "No recognised preset parameters found in file.";
        return false;
    }

    // Every preset parameter is applied: unparseable or missing -> default, numbers clamped.
    for (const auto& id : kPresetParamIds)
    {
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            continue;

        const auto& range = apvts.getParameterRange (id);
        float normalised = p->getDefaultValue();

        const auto it = values.find (id);
        if (it != values.end())
        {
            const auto& raw = it->second;

            if (isBoolParam (id))
            {
                if (raw == "true")       normalised = 1.0f;
                else if (raw == "false") normalised = 0.0f;
            }
            else
            {
                // Replacing ',' with '.' tolerates files hand-edited with locale mishaps.
                const auto cleaned = raw.replaceCharacter (',', '.');
                if (isStrictNumeric (cleaned))
                    normalised = range.convertTo0to1 (juce::jlimit (range.start, range.end,
                                                                    cleaned.getFloatValue()));
            }
        }

        p->beginChangeGesture();   // hosts in automation write / touch mode record the change
        p->setValueNotifyingHost (normalised);
        p->endChangeGesture();
    }

    return true;
}

} // namespace PresetIO

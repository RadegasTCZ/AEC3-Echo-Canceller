#include "AecMapping.h"

namespace aec::mapping
{

// LF suppression: 0% → 1.5 (very permissive), 100% → 0.01 (max suppression)
float mapSuppression (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.5f - t * 1.49f;  // 1.5 → 0.01
}

// HF suppression: 0% → 0.5, 100% → 0.01 (narrower range, HF is more sensitive)
float mapHfSuppression (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f - t * 0.49f;  // 0.5 → 0.01
}

// LF transparency: 0% → 1.5 (very permissive), 100% → 0.01
float mapTransparency (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.5f - t * 1.49f;  // 1.5 → 0.01
}

// HF transparency: 0% → 0.5, 100% → 0.005
float mapHfTransparency (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f - t * 0.495f;  // 0.5 → 0.005
}

// Attack (max_inc_factor): 0% → 0.5, 100% → 5.0
float mapAttack (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.5f + t * 4.5f;
}

// Decay (max_dec_factor_lf): 0% → 0.01, 100% → 1.0
float mapDecay (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 0.01f + t * 0.99f;
}

// HF ducking (max_gain_during_echo): 0% → 1.0 (no ducking), 100% → 0.0 (full ducking)
float mapHfDucking (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 1.0f - t;
}

// HF echo threshold (enr_threshold): 0% → 2.0 (less sensitive), 100% → 0.1 (very sensitive)
float mapHfEchoThreshold (float pct)
{
    float t = juce::jlimit (0.0f, 100.0f, pct) / 100.0f;
    return 2.0f - t * 1.9f;  // 2.0 → 0.1
}

} // namespace aec::mapping

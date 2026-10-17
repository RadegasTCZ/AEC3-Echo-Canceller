#pragma once

#include <JuceHeader.h>

// Slider (0-100%) -> AEC3 config value mapping, shared by the engine and the tests.
namespace aec::mapping
{
    float mapSuppression (float pct);       // 0-100% -> 1.5-0.01 (inverted)
    float mapHfSuppression (float pct);     // 0-100% -> 0.5-0.01 (inverted, narrower range)
    float mapTransparency (float pct);      // 0-100% -> 1.5-0.01 (inverted)
    float mapHfTransparency (float pct);    // 0-100% -> 0.5-0.005 (inverted)
    float mapAttack (float pct);            // 0-100% -> 0.5-5.0
    float mapDecay (float pct);             // 0-100% -> 0.01-1.0
    float mapHfDucking (float pct);         // 0-100% -> 1.0-0.0 (inverted)
    float mapHfEchoThreshold (float pct);   // 0-100% -> 2.0-0.1 (inverted)
}

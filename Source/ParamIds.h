#pragma once

namespace aec::ids
{
    inline constexpr const char* aecEnabled       = "aecEnabled";
    inline constexpr const char* monitorCapture   = "monitorCapture";

    inline constexpr const char* lfSuppression    = "lfSuppression";
    inline constexpr const char* lfTransparency   = "lfTransparency";
    inline constexpr const char* hfSuppression    = "hfSuppression";
    inline constexpr const char* hfTransparency   = "hfTransparency";
    inline constexpr const char* lfAttack         = "lfAttack";
    inline constexpr const char* lfDecay          = "lfDecay";
    inline constexpr const char* hfDucking        = "hfDucking";
    inline constexpr const char* hfEchoThreshold  = "hfEchoThreshold";

    inline constexpr const char* dtLfSuppression  = "dtLfSuppression";
    inline constexpr const char* dtLfTransparency = "dtLfTransparency";
    inline constexpr const char* dtHfSuppression  = "dtHfSuppression";
    inline constexpr const char* dtHfTransparency = "dtHfTransparency";
    inline constexpr const char* dtLfAttack       = "dtLfAttack";
    inline constexpr const char* dtLfDecay        = "dtLfDecay";

    inline constexpr const char* latencyComp      = "latencyComp";
    inline constexpr const char* roomReverb       = "roomReverb";
    inline constexpr const char* boundedErl       = "boundedErl";
    inline constexpr const char* clockDrift       = "clockDrift";

    inline constexpr const char* nsLevel          = "nsLevel";
    inline constexpr const char* monitorNoise     = "monitorNoise";

    inline constexpr const char* agcEnabled       = "agcEnabled";
    inline constexpr const char* agcHeadroom      = "agcHeadroom";
    inline constexpr const char* agcMaxGain       = "agcMaxGain";
    inline constexpr const char* agcAdaptSpeed    = "agcAdaptSpeed";
    inline constexpr const char* agcNoiseCeiling  = "agcNoiseCeiling";
    inline constexpr const char* limiterEnabled   = "limiterEnabled";
}

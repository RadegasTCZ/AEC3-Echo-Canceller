#include "HelpText.h"

namespace aec::help
{
namespace
{
const std::map<juce::String, HelpEntry> kHelpEntries =
{
        { "lfSuppression", {
            "LF Suppression",
            "Controls how aggressively low-frequency echo is removed. Higher values mean stronger suppression of LF echo bands.",
            "Increase if low-frequency speaker bleed (rumble, bass bleed) is audible in the output. Decrease if voice sounds dull or its low end is being cut.",
            { "lfTransparency", "dtLfSuppression" }
        }},
        { "lfTransparency", {
            "LF Transparency",
            "How much LF voice content the plugin allows through during single-talk (only far-end is active). Higher = more conservative cancellation.",
            "Increase if LF content of voice is being attenuated when no playback is active. Internally clamped below LF Suppression with a small gap.",
            { "lfSuppression" }
        }},
        { "hfSuppression", {
            "HF Suppression",
            "Aggressiveness of high-frequency echo removal (above 8 kHz). Higher = stronger suppression.",
            "Increase if high-end shimmer of speaker output bleeds through. Note: AEC3 applies a single scalar gain to all HF content, derived from the minimum LF gain (architectural limitation).",
            { "hfTransparency", "dtHfSuppression" }
        }},
        { "hfTransparency", {
            "HF Transparency",
            "HF transparency floor during single-talk.",
            "Increase if high-end of voice sounds attenuated. Internally clamped below HF Suppression.",
            { "hfSuppression" }
        }},
        { "lfAttack", {
            "LF Attack",
            "How fast suppression gain can recover (increase) toward neutral. Faster attack = quicker return after echo subsides.",
            "Increase if voice cutting back in feels delayed after speaker pauses. Decrease for smoother gain transitions.",
            { "lfDecay" }
        }},
        { "lfDecay", {
            "LF Decay",
            "How fast suppression gain can decrease (attenuate) when echo is detected.",
            "Increase if echo onset isn't suppressed quickly enough. Decrease to avoid over-aggressive ducking.",
            { "lfAttack" }
        }},
        { "hfDucking", {
            "HF Ducking",
            "Reduces high-frequency content during detected echo. 0% = disabled, 100% = max ducking.",
            "Use only if HF echo is severe; aggressive ducking causes voice to sound muffled during playback.",
            { "hfEchoThreshold" }
        }},
        { "hfEchoThreshold", {
            "HF Echo Threshold",
            "Sensitivity of HF echo detection. Higher = more sensitive (detects weaker HF echo).",
            "Tune in conjunction with HF Ducking -- too sensitive and voice gets ducked unnecessarily.",
            { "hfDucking" }
        }},
        { "dtLfSuppression", {
            "DT LF Suppression",
            "LF suppression aggressiveness specifically when both ends are speaking (double-talk).",
            "Decrease if voice sounds attenuated during overlapping audio. Typically lower than LF Suppression.",
            { "lfSuppression", "dtLfTransparency" }
        }},
        { "dtLfTransparency", {
            "DT LF Transparency",
            "LF transparency during double-talk.",
            "Increase to preserve more voice content during double-talk. Internally clamped below DT LF Suppression.",
            { "dtLfSuppression" }
        }},
        { "dtHfSuppression", {
            "DT HF Suppression",
            "HF suppression during double-talk.",
            "Decrease if voice high-end is attenuated during double-talk.",
            { "hfSuppression", "dtHfTransparency" }
        }},
        { "dtHfTransparency", {
            "DT HF Transparency",
            "HF transparency during double-talk.",
            "Increase to preserve more voice high-end during overlap. Internally clamped below DT HF Suppression.",
            { "dtHfSuppression" }
        }},
        { "dtLfAttack", {
            "DT LF Attack",
            "Attack speed during double-talk (how fast gain recovers).",
            "Tune for smooth voice texture during overlapping audio.",
            { "dtLfDecay" }
        }},
        { "dtLfDecay", {
            "DT LF Decay",
            "Decay speed during double-talk (how fast gain attenuates).",
            "Tune for smooth voice texture during overlapping audio.",
            { "dtLfAttack" }
        }},
        { "latencyComp", {
            "Latency Compensation",
            "Compensates for fixed delay (in milliseconds) between mic capture and the loopback reference signal. Helps AEC3 align echo prediction with actual echo arrival.",
            "Increase if echo bleeds through despite high suppression -- the audio chain delay exceeds AEC3's auto-detection range. Wireless mic + ASIO setups typically need 20-30 ms.",
            {}
        }},
        { "roomReverb", {
            "Room Reverberance",
            "Echo tail length estimate. Larger = longer echo tail (more reverberant rooms). Range 13-40 (AEC3 minimum is 13).",
            "Increase for echoey rooms (large halls, hard surfaces). Decrease for dry rooms (small treated studios).",
            {}
        }},
        { "boundedErl", {
            "Conservative Echo Estimation",
            "When enabled, AEC3 uses conservative echo-return-loss estimates. Helps when echo levels are inconsistent or hard to characterise.",
            "Enable if AEC3 over-suppresses voice -- AEC3 sometimes overestimates echo strength in noisy environments.",
            {}
        }},
        { "clockDrift", {
            "Clock Drift Compensation",
            "Enables AEC3's drift handling for setups where mic capture clock and playback clock are not perfectly synchronised.",
            "Enable only if mic and speakers run on different sample clocks (USB mic + onboard speakers, separate audio interfaces). Enabling unnecessarily can hurt performance.",
            { "latencyComp" }
        }},
        { "nsLevel", {
            "Noise Removal",
            "Noise removal: how much steady background noise (fans, hum, room tone) is removed after echo cancelling. 12 dB is WebRTC's default; higher removes more but can make your voice sound thinner.",
            "",
            {}
        }},
        { "agcEnabled", {
            "Auto Gain",
            "Auto gain: raises or lowers your voice toward a steady level, so you stay even when you lean back or speak softly. Gentle, Normal and Strong set how hard it works; it runs after echo and noise removal and never touches the Hear modes.",
            "Try Gentle first. Move to Strong if your level still swings a lot. Switch it off if you already use a gain stage or compressor after this plugin.",
            { "limiterEnabled" }
        }},
        { "agcHeadroom", {
            "Headroom",
            "Headroom: how far below full scale the auto gain aims to keep your voice peaks. This is the closest thing to a target level: lower headroom means a louder voice, higher means quieter and safer.",
            "Raise it if you hear distortion on loud passages; lower it if your voice is still too quiet.",
            { "agcMaxGain" }
        }},
        { "agcMaxGain", {
            "Max Gain",
            "Max gain: the most the auto gain may boost your voice. A low limit keeps a very quiet mic from being turned into hiss; a high limit lets it rescue a distant or soft voice.",
            "Keep it as low as works. If background noise swells between words, lower it or the noise ceiling.",
            { "agcNoiseCeiling" }
        }},
        { "agcAdaptSpeed", {
            "Adapt Speed",
            "Adapt speed: how fast the gain follows your level, in dB per second. Fast keeps you even sooner but can make the level pump audibly; slow sounds natural but reacts late.",
            "Lower it if you hear the level pumping or breathing. Raise it if it takes too long to catch up when you move.",
            {}
        }},
        { "agcNoiseCeiling", {
            "Noise Ceiling",
            "Noise ceiling: sounds quieter than this level are treated as noise and the auto gain does not boost them. Raise it (toward -20) if room noise gets pumped up between words.",
            "Lower it (toward -80) only if your voice is so soft that the gain never opens.",
            { "agcMaxGain" }
        }},
        { "limiterEnabled", {
            "Limiter",
            "Output limiter: catches peaks just before they clip, holding the output at -1 dBFS. The LIM bar shows how many dB it is pulling down.",
            "Leave it on. With Auto gain on and the limiter off, a sudden loud sound can clip.",
            { "agcEnabled" }
        }},
        { "monitorNoise", {
            "Hear Removed",
            "Hear Removed: plays only what the noise stage takes away, so you can check it is removing noise and not your voice.",
            "",
            {}
        }}
};
}

const std::map<juce::String, HelpEntry>& all()
{
    return kHelpEntries;
}

const HelpEntry* find (const juce::String& paramId)
{
    auto it = kHelpEntries.find (paramId);
    return it == kHelpEntries.end() ? nullptr : &it->second;
}
} // namespace aec::help

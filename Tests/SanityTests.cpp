#include <JuceHeader.h>
#include <vector>
#include "../Source/PluginProcessor.h"
#include "TestSignals.h"

namespace
{
    class SanityTests final : public juce::UnitTest
    {
    public:
        SanityTests() : juce::UnitTest ("Sanity", "Sanity") {}

        void runTest() override
        {
            beginTest ("processor constructs with v6 metadata");
            {
                AEC3EchoCancellerAudioProcessor p;
                expectEquals (p.getName(), juce::String ("AEC3 Echo Canceller"));
                expectEquals (juce::String (JucePlugin_VersionString), juce::String ("6.1.0"));
                expectEquals (juce::String (JucePlugin_Manufacturer), juce::String ("RadegasTCZ"));
            }

            beginTest ("crossCorrelationLag finds a known delay");
            {
                const auto a = aec::test::whiteNoise (4800, 0.25f, 1234u);
                std::vector<float> b (a.size() + 137, 0.0f);

                for (size_t i = 0; i < a.size(); ++i)
                    b[i + 137] = a[i];

                expectEquals (aec::test::crossCorrelationLag (a, b, 400), 137);
                expectEquals (aec::test::crossCorrelationLag (a, a, 400), 0);
            }

            beginTest ("rmsDb of sine and silence");
            {
                const auto s = aec::test::sine (48000, 1000.0f, 1.0f, 48000.0);
                expectWithinAbsoluteError (aec::test::rmsDb (s.data(), (int) s.size()), -3.0103f, 0.01f);

                const std::vector<float> zeros (64, 0.0f);
                expectEquals (aec::test::rmsDb (zeros.data(), 64), -200.0f);
            }

            beginTest ("WASAPI mix format: only 32-bit float and 16-bit PCM with channels are accepted");
            {
                using W = WasapiLoopbackCapture;
                using F = W::SampleFormat;
                using Sub = W::MixFormat::Sub;

                auto plain = [] (int tag, int ch, int bits)
                {
                    W::MixFormat f;
                    f.formatTag = tag; f.channels = ch; f.bitsPerSample = bits;
                    return W::classifyMixFormat (f);
                };
                auto ext = [] (Sub sub, int ch, int bits, int valid, int cbSize = 22)
                {
                    W::MixFormat f;
                    f.formatTag = W::kFormatTagExtensible; f.channels = ch; f.bitsPerSample = bits;
                    f.extraSize = cbSize; f.validBitsPerSample = valid; f.subFormat = sub;
                    return W::classifyMixFormat (f);
                };

                expect (plain (W::kFormatTagIeeeFloat, 2, 32) == F::Float32);
                expect (plain (W::kFormatTagPcm, 2, 16) == F::Int16);
                expect (plain (W::kFormatTagPcm, 1, 16) == F::Int16);
                expect (ext (Sub::IeeeFloat, 2, 32, 32) == F::Float32);
                expect (ext (Sub::Pcm, 8, 16, 16) == F::Int16);

                expect (plain (W::kFormatTagPcm, 2, 24) == F::Unsupported, "PCM 24");
                expect (plain (W::kFormatTagPcm, 2, 32) == F::Unsupported, "PCM 32");
                expect (plain (W::kFormatTagPcm, 2, 8) == F::Unsupported, "PCM 8");
                expect (plain (W::kFormatTagIeeeFloat, 2, 64) == F::Unsupported, "float 64");
                expect (ext (Sub::Pcm, 2, 24, 24) == F::Unsupported, "EXT PCM 24");
                expect (ext (Sub::Pcm, 2, 32, 24) == F::Unsupported, "EXT PCM 24-in-32");
                expect (ext (Sub::Pcm, 2, 32, 32) == F::Unsupported, "EXT PCM 32");
                expect (ext (Sub::Pcm, 2, 16, 12) == F::Unsupported, "EXT PCM 12-in-16");
                expect (ext (Sub::IeeeFloat, 2, 64, 64) == F::Unsupported, "EXT float 64");
                expect (ext (Sub::Other, 2, 16, 16) == F::Unsupported, "EXT unknown subformat");
                expect (ext (Sub::Pcm, 2, 16, 16, 0) == F::Unsupported, "EXT without the extension");
                expect (plain (0x0055, 2, 16) == F::Unsupported, "unknown tag");

                expect (plain (W::kFormatTagPcm, 0, 16) == F::Unsupported, "PCM no channels");
                expect (plain (W::kFormatTagIeeeFloat, 0, 32) == F::Unsupported, "float no channels");
                expect (ext (Sub::IeeeFloat, 0, 32, 32) == F::Unsupported, "EXT no channels");
            }
        }
    };

    static SanityTests sanityTests;
}

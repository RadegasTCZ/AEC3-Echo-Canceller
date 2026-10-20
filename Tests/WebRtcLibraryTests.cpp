#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include "TestSignals.h"

#include "api/audio/audio_processing.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/audio/echo_canceller3_factory.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_processing/agc2/adaptive_digital_gain_controller.h"
#include "modules/audio_processing/agc2/cpu_features.h"
#include "modules/audio_processing/agc2/limiter.h"
#include "modules/audio_processing/agc2/vad_wrapper.h"
#include "modules/audio_processing/audio_buffer.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "modules/audio_processing/ns/noise_suppressor.h"
#include "modules/audio_processing/ns/ns_config.h"

namespace
{
    constexpr int kRate = 48000;
    constexpr int kFrame = kRate / 100; // 10 ms, the WebRTC APM frame size

    std::unique_ptr<webrtc::AudioBuffer> makeMonoBuffer()
    {
        return std::make_unique<webrtc::AudioBuffer> (kRate, 1, kRate, 1, kRate, 1);
    }

    class WebRtcLibraryTests final : public juce::UnitTest
    {
    public:
        WebRtcLibraryTests() : juce::UnitTest ("WebRtcLibraryTests", "WebRtcLib") {}

        void runTest() override
        {
            beginTest ("EchoCanceller3 from default config processes 100 frames of silence");
            {
                auto aec = webrtc::EchoCanceller3Factory (webrtc::EchoCanceller3Config {})
                               .Create (webrtc::CreateEnvironment(), kRate, 1, 1);
                expect (aec != nullptr);

                auto render = makeMonoBuffer();
                auto capture = makeMonoBuffer();
                const webrtc::StreamConfig sc (kRate, 1);

                std::vector<float> in ((size_t) kFrame, 0.0f), out ((size_t) kFrame, 1.0f);
                bool allFinite = true;
                float maxAbs = 0.0f;

                for (int f = 0; f < 100; ++f)
                {
                    const float* inPtr = in.data();
                    float* outPtr = out.data();

                    render->CopyFrom (&inPtr, sc);
                    render->SplitIntoFrequencyBands();
                    aec->AnalyzeRender (render.get());
                    render->MergeFrequencyBands();

                    capture->CopyFrom (&inPtr, sc);
                    aec->AnalyzeCapture (capture.get());
                    capture->SplitIntoFrequencyBands();
                    aec->ProcessCapture (capture.get(), nullptr, false);
                    capture->MergeFrequencyBands();
                    capture->CopyTo (sc, &outPtr);

                    for (float x : out)
                    {
                        allFinite = allFinite && std::isfinite (x);
                        maxAbs = juce::jmax (maxAbs, std::abs (x));
                    }
                }

                expect (allFinite, "AEC3 output contains non-finite samples");
                expectLessThan (maxAbs, 1e-6f);
            }

            beginTest ("NoiseSuppressor 12 dB lowers stationary white noise by at least 6 dB");
            {
                webrtc::NsConfig cfg;
                cfg.target_level = webrtc::NsConfig::SuppressionLevel::k12dB;
                webrtc::NoiseSuppressor ns (cfg, (size_t) kRate, 1);

                auto buf = makeMonoBuffer();
                const webrtc::StreamConfig sc (kRate, 1);

                constexpr int kFrames = 300;
                constexpr int kMeasured = 100;
                const auto in = aec::test::whiteNoise (kFrames * kFrame, 0.05f, 4242u);
                std::vector<float> out (in.size(), 0.0f);

                for (int f = 0; f < kFrames; ++f)
                {
                    const float* inPtr = in.data() + (size_t) f * kFrame;
                    float* outPtr = out.data() + (size_t) f * kFrame;

                    buf->CopyFrom (&inPtr, sc);
                    buf->SplitIntoFrequencyBands();
                    ns.Analyze (*buf);
                    ns.Process (buf.get());
                    buf->MergeFrequencyBands();
                    buf->CopyTo (sc, &outPtr);
                }

                const size_t start = (size_t) (kFrames - kMeasured) * kFrame;
                const int n = kMeasured * kFrame;
                const float inDb = aec::test::rmsDb (in.data() + start, n);
                const float outDb = aec::test::rmsDb (out.data() + start, n);
                logMessage ("  NS 12 dB: in " + juce::String (inDb, 2) + " dB, out " + juce::String (outDb, 2)
                            + " dB, reduction " + juce::String (inDb - outDb, 2) + " dB");
                expectGreaterThan (inDb - outDb, 6.0f);
            }

            beginTest ("default config satisfies enr_transparent < enr_suppress");
            {
                const webrtc::EchoCanceller3Config cfg {};
                expectLessThan (cfg.suppressor.normal_tuning.mask_lf.enr_transparent,
                                cfg.suppressor.normal_tuning.mask_lf.enr_suppress);
                expectLessThan (cfg.suppressor.normal_tuning.mask_hf.enr_transparent,
                                cfg.suppressor.normal_tuning.mask_hf.enr_suppress);
                expectLessThan (cfg.suppressor.nearend_tuning.mask_lf.enr_transparent,
                                cfg.suppressor.nearend_tuning.mask_lf.enr_suppress);
                expectLessThan (cfg.suppressor.nearend_tuning.mask_hf.enr_transparent,
                                cfg.suppressor.nearend_tuning.mask_hf.enr_suppress);
            }

            beginTest ("AGC2 Limiter keeps a +6 dBFS sine within full scale");
            {
                constexpr float kFullScale = 32768.0f;
                webrtc::ApmDataDumper dumper (0);
                webrtc::Limiter lim (&dumper, (size_t) kFrame, "Agc2");

                std::vector<float> frame ((size_t) kFrame);
                float maxAbs = 0.0f;
                bool withinScale = true;
                double phase = 0.0;
                const double inc = 2.0 * juce::MathConstants<double>::pi * 1000.0 / kRate;

                for (int f = 0; f < 200; ++f)
                {
                    for (auto& x : frame)
                    {
                        x = (float) (2.0 * kFullScale * std::sin (phase));
                        phase += inc;
                    }
                    lim.Process (webrtc::DeinterleavedView<float> (frame.data(), (size_t) kFrame, 1));
                    if (f > 10)
                        for (const float x : frame)
                        {
                            withinScale = withinScale && std::abs (x) <= kFullScale;
                            maxAbs = std::max (maxAbs, std::abs (x));
                        }
                }
                expect (withinScale);
                expectGreaterThan (maxAbs, 0.5f * kFullScale);
            }

            beginTest ("AGC2 VAD wrapper builds and returns a probability in [0,1]");
            {
                webrtc::VoiceActivityDetectorWrapper vad (1500, webrtc::GetAvailableCpuFeatures(), kRate);
                const auto noise = aec::test::whiteNoise (100 * kFrame, 0.1f * 32768.0f, 99u);
                bool inRange = true;
                for (int f = 0; f < 100; ++f)
                {
                    const float p = vad.Analyze (webrtc::DeinterleavedView<const float> (
                        noise.data() + (size_t) f * kFrame, (size_t) kFrame, 1));
                    inRange = inRange && p >= 0.0f && p <= 1.0f;
                }
                expect (inRange);
            }
        }
    };

    static WebRtcLibraryTests webRtcLibraryTests;
}

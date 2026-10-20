#include "AgcStage.h"

#include <algorithm>
#include <cmath>

#include "api/audio/audio_processing.h"
#include "api/audio/audio_view.h"
#include "api/environment/environment_factory.h"
#include "common_audio/include/audio_util.h"
#include "modules/audio_processing/agc2/adaptive_digital_gain_controller.h"
#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/agc2/cpu_features.h"
#include "modules/audio_processing/agc2/fixed_digital_level_estimator.h"
#include "modules/audio_processing/agc2/noise_level_estimator.h"
#include "modules/audio_processing/agc2/saturation_protector.h"
#include "modules/audio_processing/agc2/speech_level_estimator.h"
#include "modules/audio_processing/agc2/vad_wrapper.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"

namespace aec
{
namespace
{
constexpr float kFullScale = 32768.0f;

float fixup (float v, float def, float lo, float hi)
{
    return std::isfinite (v) ? std::clamp (v, lo, hi) : def;
}
}

AgcSettings AgcSettings::sanitised() const
{
    const AgcSettings d;
    AgcSettings s = *this;
    s.headroomDb = fixup (headroomDb, d.headroomDb, 0.0f, 20.0f);
    s.maxGainDb = fixup (maxGainDb, d.maxGainDb, 1.0f, 50.0f);
    s.adaptSpeedDbPerSec = fixup (adaptSpeedDbPerSec, d.adaptSpeedDbPerSec, 1.0f, 30.0f);
    s.noiseCeilingDbfs = fixup (noiseCeilingDbfs, d.noiseCeilingDbfs, -80.0f, -20.0f);
    return s;
}

AgcStage::AgcStage (const AgcSettings& raw, float initialGainDb)
    : dumper (std::make_unique<webrtc::ApmDataDumper> (0))
{
    const auto s = raw.sanitised();

    webrtc::AudioProcessing::Config::GainController2::AdaptiveDigital cfg;
    cfg.enabled = true;
    cfg.headroom_db = s.headroomDb;
    cfg.max_gain_db = s.maxGainDb;
    cfg.initial_gain_db = std::isfinite (initialGainDb) && initialGainDb >= 0.0f
                            ? std::clamp (initialGainDb, 0.0f, s.maxGainDb)
                            : std::min (15.0f, s.maxGainDb);
    lastGainDb = cfg.initial_gain_db;
    cfg.max_gain_change_db_per_second = s.adaptSpeedDbPerSec;
    cfg.max_output_noise_level_dbfs = s.noiseCeilingDbfs;

    const auto env = webrtc::CreateEnvironment();
    speechLevel = webrtc::SpeechLevelEstimator::Create (env.field_trials(), dumper.get(), cfg,
                                                        webrtc::kAdjacentSpeechFramesThreshold);
    vad = std::make_unique<webrtc::VoiceActivityDetectorWrapper> (webrtc::kVadResetPeriodMs,
                                                                  webrtc::GetAvailableCpuFeatures(), kSampleRate);
    noiseLevel = webrtc::CreateNoiseFloorEstimator (dumper.get());
    saturationProtector = webrtc::CreateSaturationProtector (webrtc::kSaturationProtectorInitialHeadroomDb,
                                                             webrtc::kAdjacentSpeechFramesThreshold, dumper.get());
    controller = std::make_unique<webrtc::AdaptiveDigitalGainController> (dumper.get(), cfg,
                                                                          webrtc::kAdjacentSpeechFramesThreshold);
    outputLevel = std::make_unique<webrtc::FixedDigitalLevelEstimator> (kFrameSize, dumper.get());
}
AgcStage::~AgcStage() = default;

void AgcStage::process (float* frame, float speechProbabilityOverride) noexcept
{
    float inSq = 0.0f, peak = 0.0f;
    for (int i = 0; i < kFrameSize; ++i)
    {
        const float x = frame[i] * kFullScale;
        buffer[i] = x;
        inSq += x * x;
        peak = std::max (peak, std::abs (x));
    }

    webrtc::DeinterleavedView<float> view (buffer, kFrameSize, 1);
    webrtc::DeinterleavedView<const float> constView (buffer, kFrameSize, 1);

    const float prob = speechProbabilityOverride >= 0.0f ? std::min (speechProbabilityOverride, 1.0f)
                                                         : vad->Analyze (constView);
    const float rmsDbfs = webrtc::FloatS16ToDbfs (std::sqrt (inSq / (float) kFrameSize));
    const float peakDbfs = webrtc::FloatS16ToDbfs (peak);
    const float noiseDbfs = noiseLevel->Analyze (constView);

    speechLevel->Update (rmsDbfs, prob);
    const float targetProb = speechLevel->IsTargetSpeakerActive() ? prob : 0.0f;
    const float speechDbfs = speechLevel->GetLevelDbfs();
    const bool confident = speechLevel->IsConfident();

    saturationProtector->Analyze (targetProb, peakDbfs, speechDbfs);
    const float headroomDb = saturationProtector->HeadroomDb();
    const float envelopeDbfs = webrtc::FloatS16ToDbfs (outputLevel->LastAudioLevel());

    controller->Process ({ .speech_probability = targetProb,
                           .speech_level_dbfs = speechDbfs,
                           .speech_level_reliable = confident,
                           .noise_rms_dbfs = noiseDbfs,
                           .headroom_db = headroomDb,
                           .limiter_envelope_dbfs = envelopeDbfs },
                         view);

    outputLevel->ComputeLevel (constView);

    float outSq = 0.0f;
    for (int i = 0; i < kFrameSize; ++i)
    {
        outSq += buffer[i] * buffer[i];
        frame[i] = buffer[i] / kFullScale;
    }
    if (inSq > 1.0f && outSq > 0.0f)
        lastGainDb = 10.0f * std::log10 (outSq / inSq);
}
} // namespace aec

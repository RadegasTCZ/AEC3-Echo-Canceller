cmake_minimum_required(VERSION 3.20)
project(AEC3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Paths
# Pass the source checkouts on the command line (see README.md here):
#   -DWEBRTC_SRC=<webrtc checkout>  -DABSEIL_SRC=<abseil-cpp checkout>
#   -DCHROMIUM_THIRD_PARTY_SRC=<directory containing third_party/rnnoise and third_party/pffft>
set(WEBRTC_SRC "" CACHE PATH "WebRTC source checkout (revision b58dcf8fec)")
set(ABSEIL_SRC "" CACHE PATH "abseil-cpp source checkout (revision 972e4ab4)")
if(NOT EXISTS "${WEBRTC_SRC}/modules/audio_processing/aec3/echo_canceller3.cc")
    message(FATAL_ERROR "WEBRTC_SRC must point at a WebRTC checkout")
endif()
if(NOT EXISTS "${ABSEIL_SRC}/absl/base/config.h")
    message(FATAL_ERROR "ABSEIL_SRC must point at an abseil-cpp checkout")
endif()
set(CHROMIUM_THIRD_PARTY_SRC "" CACHE PATH "Directory containing Chromium's third_party/rnnoise and third_party/pffft (revision 6eabe54fa1)")
if(NOT EXISTS "${CHROMIUM_THIRD_PARTY_SRC}/third_party/rnnoise/src/rnn_vad_weights.cc")
    message(FATAL_ERROR "CHROMIUM_THIRD_PARTY_SRC must contain third_party/rnnoise and third_party/pffft")
endif()
set(ABSEIL_INC "${ABSEIL_SRC}")
set(STUBS_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

# Include paths
include_directories(
    ${WEBRTC_SRC}
    ${ABSEIL_INC}
    ${CHROMIUM_THIRD_PARTY_SRC}
    ${STUBS_DIR}/include
)

# Global compile definitions
add_compile_definitions(
    WEBRTC_WIN
    NOMINMAX
    RTC_DISABLE_METRICS
    WEBRTC_APM_DEBUG_DUMP=0
    # Suppress MSVC warnings about deprecated POSIX names
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
)

# MSVC-specific flags
if(MSVC)
    add_compile_options(
        /W3             # Warning level 3
        /wd4244         # Conversion warnings (int/float narrowing)
        /wd4267         # size_t to int conversion
        /wd4305         # double to float truncation
        /wd4996         # Deprecated POSIX names
        /wd4100         # Unreferenced formal parameter
        /wd4127         # Conditional expression is constant
        /EHsc           # C++ exceptions
        /bigobj         # Larger object files
    )
endif()

###############################################################################
# Source file lists
###############################################################################

# --- AEC3 core (modules/audio_processing/aec3/) ---
set(AEC3_CORE_SRC
    ${WEBRTC_SRC}/modules/audio_processing/aec3/adaptive_fir_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/adaptive_fir_filter_erl.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/aec3_common.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/aec3_fft.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/aec_state.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/alignment_mixer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/api_call_jitter_metrics.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/block_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/block_delay_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/block_framer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/block_processor.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/block_processor_metrics.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/clockdrift_detector.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/coarse_filter_update_gain.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/comfort_noise_generator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/config_selector.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/decimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/dominant_nearend_detector.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/downsampled_render_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_audibility.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_canceller3.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_path_delay_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_path_variability.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_remover.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/echo_remover_metrics.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/erl_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/erle_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/fft_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/filter_analyzer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/frame_blocker.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/fullband_erle_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/matched_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/matched_filter_lag_aggregator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/moving_average_spectrum.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/multi_channel_content_detector.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/refined_filter_update_gain.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/render_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/render_delay_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/render_delay_controller.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/render_delay_controller_metrics.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/render_signal_analyzer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/residual_echo_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/reverb_decay_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/reverb_frequency_response.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/reverb_model.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/reverb_model_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/signal_dependent_erle_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/spectrum_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/stationarity_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/subband_erle_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/subband_nearend_detector.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/subtractor.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/subtractor_output.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/subtractor_output_analyzer.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/suppression_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/suppression_gain.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/transparent_mode.cc
)

# AVX2 sources (need /arch:AVX2 on MSVC)
set(AEC3_AVX2_SRC
    ${WEBRTC_SRC}/modules/audio_processing/aec3/adaptive_fir_filter_avx2.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/adaptive_fir_filter_erl_avx2.cc
    ${WEBRTC_SRC}/modules/audio_processing/aec3/fft_data_avx2.cc
    # matched_filter_avx2.cc excluded: uses __m256[index] GCC extension unsupported by MSVC
    ${WEBRTC_SRC}/modules/audio_processing/aec3/vector_math_avx2.cc
)

# --- API layer ---
set(API_SRC
    ${WEBRTC_SRC}/api/audio/echo_canceller3_config.cc
    ${WEBRTC_SRC}/api/audio/echo_canceller3_factory.cc
    ${WEBRTC_SRC}/api/audio/audio_frame.cc
    ${WEBRTC_SRC}/api/field_trials_registry.cc
    ${WEBRTC_SRC}/api/task_queue/task_queue_base.cc
)

# --- Environment ---
set(ENV_SRC
    ${WEBRTC_SRC}/api/environment/environment_factory.cc
    ${WEBRTC_SRC}/api/environment/deprecated_global_field_trials.cc
    ${WEBRTC_SRC}/api/environment/force_test_environment.cc
)

# --- Audio processing (top-level, only what AEC3 needs) ---
set(APM_SRC
    ${WEBRTC_SRC}/modules/audio_processing/audio_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/splitting_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/three_band_filter_bank.cc
    ${WEBRTC_SRC}/modules/audio_processing/logging/apm_data_dumper.cc
    ${WEBRTC_SRC}/modules/audio_processing/high_pass_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/utility/cascaded_biquad_filter.cc
    # capture_mixer (new dep of audio_buffer.h)
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/capture_mixer.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/audio_content_analyzer.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/channel_content_remixer.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/dc_levels_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/energy_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/remixing_logic.cc
    ${WEBRTC_SRC}/modules/audio_processing/capture_mixer/saturation_estimator.cc
)

# --- rtc_base ---
set(RTC_BASE_SRC
    ${WEBRTC_SRC}/rtc_base/checks.cc
    ${WEBRTC_SRC}/rtc_base/logging.cc
    ${WEBRTC_SRC}/rtc_base/race_checker.cc
    ${WEBRTC_SRC}/rtc_base/ref_counter.cc
    ${WEBRTC_SRC}/rtc_base/random.cc
    ${WEBRTC_SRC}/rtc_base/cpu_info.cc
    ${WEBRTC_SRC}/rtc_base/platform_thread_types.cc
    ${WEBRTC_SRC}/rtc_base/strings/string_builder.cc
    ${WEBRTC_SRC}/rtc_base/experiments/field_trial_parser.cc
    ${WEBRTC_SRC}/rtc_base/memory/aligned_malloc.cc
    ${WEBRTC_SRC}/rtc_base/time_utils.cc
    ${WEBRTC_SRC}/rtc_base/string_encode.cc
    ${WEBRTC_SRC}/rtc_base/string_utils.cc
    ${WEBRTC_SRC}/rtc_base/system_time.cc
)

# --- system_wrappers ---
set(SYSW_SRC
    ${WEBRTC_SRC}/system_wrappers/source/clock.cc
    # AGC2 limiter/curve call webrtc::metrics:: directly; histograms stay inactive unless Enable() is called
    ${WEBRTC_SRC}/system_wrappers/source/metrics.cc
)

# --- common_audio (generic + SSE, no MIPS/NEON) ---
set(COMMON_AUDIO_SRC
    # Root
    ${WEBRTC_SRC}/common_audio/audio_converter.cc
    ${WEBRTC_SRC}/common_audio/audio_util.cc
    ${WEBRTC_SRC}/common_audio/channel_buffer.cc
    ${WEBRTC_SRC}/common_audio/fir_filter_c.cc
    ${WEBRTC_SRC}/common_audio/fir_filter_factory.cc
    ${WEBRTC_SRC}/common_audio/fir_filter_sse.cc
    ${WEBRTC_SRC}/common_audio/real_fourier.cc
    ${WEBRTC_SRC}/common_audio/real_fourier_ooura.cc
    ${WEBRTC_SRC}/common_audio/smoothing_filter.cc
    ${WEBRTC_SRC}/common_audio/window_generator.cc
    ${WEBRTC_SRC}/common_audio/ring_buffer.c

    # Resampler (no NEON)
    ${WEBRTC_SRC}/common_audio/resampler/push_resampler.cc
    ${WEBRTC_SRC}/common_audio/resampler/push_sinc_resampler.cc
    ${WEBRTC_SRC}/common_audio/resampler/resampler.cc
    ${WEBRTC_SRC}/common_audio/resampler/sinc_resampler.cc
    ${WEBRTC_SRC}/common_audio/resampler/sinc_resampler_sse.cc

    # Signal processing (generic C, no MIPS/NEON)
    ${WEBRTC_SRC}/common_audio/signal_processing/auto_corr_to_refl_coef.c
    ${WEBRTC_SRC}/common_audio/signal_processing/auto_correlation.c
    ${WEBRTC_SRC}/common_audio/signal_processing/copy_set_operations.c
    ${WEBRTC_SRC}/common_audio/signal_processing/cross_correlation.c
    ${WEBRTC_SRC}/common_audio/signal_processing/division_operations.c
    ${WEBRTC_SRC}/common_audio/signal_processing/dot_product_with_scale.cc
    ${WEBRTC_SRC}/common_audio/signal_processing/downsample_fast.c
    ${WEBRTC_SRC}/common_audio/signal_processing/energy.c
    ${WEBRTC_SRC}/common_audio/signal_processing/filter_ar.c
    ${WEBRTC_SRC}/common_audio/signal_processing/filter_ar_fast_q12.c
    ${WEBRTC_SRC}/common_audio/signal_processing/filter_ma_fast_q12.c
    ${WEBRTC_SRC}/common_audio/signal_processing/get_hanning_window.c
    ${WEBRTC_SRC}/common_audio/signal_processing/get_scaling_square.c
    ${WEBRTC_SRC}/common_audio/signal_processing/levinson_durbin.c
    ${WEBRTC_SRC}/common_audio/signal_processing/lpc_to_refl_coef.c
    ${WEBRTC_SRC}/common_audio/signal_processing/min_max_operations.c
    ${WEBRTC_SRC}/common_audio/signal_processing/randomization_functions.c
    ${WEBRTC_SRC}/common_audio/signal_processing/refl_coef_to_lpc.c
    ${WEBRTC_SRC}/common_audio/signal_processing/resample.c
    ${WEBRTC_SRC}/common_audio/signal_processing/resample_48khz.c
    ${WEBRTC_SRC}/common_audio/signal_processing/resample_by_2.c
    ${WEBRTC_SRC}/common_audio/signal_processing/resample_by_2_internal.c
    ${WEBRTC_SRC}/common_audio/signal_processing/resample_fractional.c
    ${WEBRTC_SRC}/common_audio/signal_processing/spl_init.c
    ${WEBRTC_SRC}/common_audio/signal_processing/spl_inl.c
    ${WEBRTC_SRC}/common_audio/signal_processing/spl_sqrt.c
    ${WEBRTC_SRC}/common_audio/signal_processing/splitting_filter.c
    ${WEBRTC_SRC}/common_audio/signal_processing/vector_operations.c
    ${WEBRTC_SRC}/common_audio/signal_processing/vector_scaling_operations.c

    # Ooura FFT
    ${WEBRTC_SRC}/common_audio/third_party/ooura/fft_size_128/ooura_fft.cc
    ${WEBRTC_SRC}/common_audio/third_party/ooura/fft_size_128/ooura_fft_sse2.cc
    ${WEBRTC_SRC}/common_audio/third_party/ooura/fft_size_256/fft4g.cc

    # SPL sqrt floor
    ${WEBRTC_SRC}/common_audio/third_party/spl_sqrt_floor/spl_sqrt_floor.c

    # VAD
    ${WEBRTC_SRC}/common_audio/vad/vad.cc
    ${WEBRTC_SRC}/common_audio/vad/vad_core.c
    ${WEBRTC_SRC}/common_audio/vad/vad_filterbank.c
    ${WEBRTC_SRC}/common_audio/vad/vad_gmm.c
    ${WEBRTC_SRC}/common_audio/vad/vad_sp.c
    ${WEBRTC_SRC}/common_audio/vad/webrtc_vad.c
)

# AVX2 common_audio files
set(COMMON_AUDIO_AVX2_SRC
    ${WEBRTC_SRC}/common_audio/fir_filter_avx2.cc
    ${WEBRTC_SRC}/common_audio/resampler/sinc_resampler_avx2.cc
)

# --- Noise suppressor (modules/audio_processing/ns/, BUILD.gn target "ns") ---
set(NS_SRC
    ${WEBRTC_SRC}/modules/audio_processing/ns/fast_math.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/histograms.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/noise_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/noise_suppressor.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/ns_fft.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/prior_signal_model.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/prior_signal_model_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/quantile_noise_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/signal_model.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/signal_model_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/speech_probability_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/suppression_params.cc
    ${WEBRTC_SRC}/modules/audio_processing/ns/wiener_filter.cc
)

# --- Adaptive digital gain control 2 (modules/audio_processing/agc2/, BUILD.gn targets
#     adaptive_digital_gain_controller, gain_applier, saturation_protector, speech_level_estimator,
#     noise_level_estimator, vad_wrapper, cpu_features, fixed_digital, rnn_vad*) ---
set(AGC2_SRC
    ${WEBRTC_SRC}/modules/audio_processing/agc2/biquad_filter.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/adaptive_digital_gain_controller.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/cpu_features.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/fixed_digital_level_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/gain_applier.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/interpolated_gain_curve.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/limiter.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/limiter_db_gain_curve.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/noise_level_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/saturation_protector.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/saturation_protector_buffer.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/speech_level_estimator.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/speech_level_estimator_experimental_impl.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/speech_level_estimator_impl.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/vad_wrapper.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/auto_correlation.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/features_extraction.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/lp_residual.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/pitch_search.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/pitch_search_internal.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/rnn.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/rnn_fc.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/rnn_gru.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/spectral_features.cc
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/spectral_features_internal.cc
    ${WEBRTC_SRC}/modules/audio_processing/utility/pffft_wrapper.cc
    # Chromium third_party: RNN VAD weights and PFFFT
    ${CHROMIUM_THIRD_PARTY_SRC}/third_party/rnnoise/src/rnn_vad_weights.cc
    ${CHROMIUM_THIRD_PARTY_SRC}/third_party/pffft/src/pffft.c
)

set(AGC2_AVX2_SRC
    ${WEBRTC_SRC}/modules/audio_processing/agc2/rnn_vad/vector_math_avx2.cc
)

# --- Abseil (built as CMake subdirectory) ---
set(ABSL_PROPAGATE_CXX_STD ON)
set(ABSL_BUILD_TESTING OFF)
set(BUILD_TESTING OFF)
add_subdirectory(${ABSEIL_INC} ${CMAKE_BINARY_DIR}/abseil-cpp EXCLUDE_FROM_ALL)

# --- Stubs ---
set(STUBS_SRC
    ${STUBS_DIR}/default_task_queue_factory.cc
    ${STUBS_DIR}/rtc_event_log_null.cc
    ${STUBS_DIR}/matched_filter_avx2_stub.cc
)

###############################################################################
# Static library target
###############################################################################

add_library(AEC3 STATIC
    ${AEC3_CORE_SRC}
    ${AEC3_AVX2_SRC}
    ${NS_SRC}
    ${AGC2_SRC}
    ${AGC2_AVX2_SRC}
    ${API_SRC}
    ${ENV_SRC}
    ${APM_SRC}
    ${RTC_BASE_SRC}
    ${SYSW_SRC}
    ${COMMON_AUDIO_SRC}
    ${COMMON_AUDIO_AVX2_SRC}
    ${STUBS_SRC}
)

# Set /arch:AVX2 on AVX2-specific source files
if(MSVC)
    set_source_files_properties(
        ${AEC3_AVX2_SRC}
        ${COMMON_AUDIO_AVX2_SRC}
        ${AGC2_AVX2_SRC}
        PROPERTIES COMPILE_FLAGS "/arch:AVX2"
    )
endif()

# pffft.c needs M_PI from <math.h> on Windows (Chromium's pffft config defines this)
set_source_files_properties(
    ${CHROMIUM_THIRD_PARTY_SRC}/third_party/pffft/src/pffft.c
    PROPERTIES COMPILE_DEFINITIONS "_USE_MATH_DEFINES"
)

# mutex.h pulls in <windows.h> before <winsock2.h> unless lean-and-mean is set
set_source_files_properties(
    ${WEBRTC_SRC}/system_wrappers/source/metrics.cc
    PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN"
)

# Link abseil libraries needed by WebRTC
target_link_libraries(AEC3 PRIVATE
    absl::strings
    absl::str_format
    absl::int128
    absl::base
    absl::raw_logging_internal
    absl::algorithm
)

# Output name — AEC3.lib for Release, AEC3_debug.lib for Debug
set_target_properties(AEC3 PROPERTIES
    OUTPUT_NAME_RELEASE "AEC3"
    OUTPUT_NAME_DEBUG "AEC3_debug"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

message(STATUS "AEC3 build configured")
message(STATUS "  WebRTC source: ${WEBRTC_SRC}")
message(STATUS "  Abseil headers: ${ABSEIL_INC}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")

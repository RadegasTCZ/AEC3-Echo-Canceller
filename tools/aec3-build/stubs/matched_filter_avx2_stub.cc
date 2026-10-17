// Stub: MatchedFilterCore_AVX2 delegates to SSE2 version.
// The original matched_filter_avx2.cc uses __m256[index] which is a
// GCC/Clang extension unsupported by MSVC. The SSE2 version works fine.

#include <cstddef>
#include <span>

#include "modules/audio_processing/aec3/matched_filter.h"

namespace webrtc {
// Upstream dropped the webrtc::aec3 sub-namespace for the MatchedFilterCore_*
// kernels (declared directly in namespace webrtc in matched_filter.h).

void MatchedFilterCore_AVX2(size_t x_start_index,
                            float x2_sum_threshold,
                            float smoothing,
                            std::span<const float> x,
                            std::span<const float> y,
                            std::span<float> h,
                            bool* filters_updated,
                            float* error_sum,
                            bool compute_accumulated_error,
                            std::span<float> accumulated_error,
                            std::span<float> scratch_memory) {
  // Fall back to SSE2 implementation
  MatchedFilterCore_SSE2(x_start_index, x2_sum_threshold, smoothing,
                         x, y, h, filters_updated, error_sum,
                         compute_accumulated_error, accumulated_error,
                         scratch_memory);
}

}  // namespace webrtc

#include "WaveformPeaks.h"

#include "../audio/AudioBuffer.h"

#include <algorithm>
#include <cmath>

using namespace std;

float
WaveformPeaks::at(int row, int subrow) const {
  if (subrows_per_row_ <= 0) return 0.0f;
  auto index = row * subrows_per_row_ + subrow;
  if (index < 0 || index >= static_cast<int>(peaks_.size())) return 0.0f;
  return peaks_[static_cast<size_t>(index)];
}

void
WaveformPeaks::build(const AudioBuffer & buffer, int native_sample_rate, float in_point, float out_point, int row_count, int subrows_per_row) {
  peaks_.clear();
  subrows_per_row_ = subrows_per_row;
  if (row_count <= 0 || subrows_per_row <= 0 || native_sample_rate <= 0) return;

  auto total_frames = buffer.numberOfFrames();
  if (total_frames <= 0) return;

  // Same clamped-to-what's-actually-audible resolution
  // SampleTrackState::triggerClip() already applies for real playback,
  // in this buffer's own native rate rather than the output rate.
  auto in_frame = static_cast<int>(lround(static_cast<double>(in_point) * native_sample_rate));
  auto out_frame = total_frames - static_cast<int>(lround(static_cast<double>(out_point) * native_sample_rate));
  if (in_frame < 0) in_frame = 0;
  if (out_frame > total_frames) out_frame = total_frames;
  if (in_frame >= out_frame) {
    // Degenerate hand-edit trim - fall back to the full buffer, same as
    // triggerClip() does.
    in_frame = 0;
    out_frame = total_frames;
  }

  auto bucket_count = row_count * subrows_per_row;
  auto trimmed_frames = out_frame - in_frame;
  auto * data = buffer.getChannelData(0);
  float overall_peak = 0.0f;
  peaks_.reserve(static_cast<size_t>(bucket_count));
  for (int bucket = 0; bucket < bucket_count; bucket++) {
    // Evenly divided by bucket index, not a fixed frames-per-bucket
    // stride - the trimmed range's own frame count rarely divides evenly
    // by bucket_count, and this keeps every bucket's own span as close to
    // equal as integer rounding allows instead of concentrating the
    // remainder into one oversized final bucket.
    auto start = in_frame + static_cast<int>(static_cast<int64_t>(bucket) * trimmed_frames / bucket_count);
    auto end = in_frame + static_cast<int>(static_cast<int64_t>(bucket + 1) * trimmed_frames / bucket_count);
    float peak = 0.0f;
    for (int i = start; i < end; i++) peak = max(peak, fabsf(data[i]));
    peaks_.push_back(peak);
    overall_peak = max(overall_peak, peak);
  }
  if (overall_peak > 0.0f) {
    for (auto & p : peaks_) p /= overall_peak;
  }
}

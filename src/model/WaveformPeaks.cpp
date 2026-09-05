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
WaveformPeaks::build(const AudioBuffer & buffer, int native_sample_rate, float in_point, float out_point, int row_count, int subrows_per_row, int tempo) {
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
  peaks_.reserve(static_cast<size_t>(bucket_count));

  // A fixed, tempo-derived frames-per-subrow (mirrors ChannelConfiguration::
  // getSampleInterval()'s own "4 rows per beat" convention exactly:
  // 60 / 4 / tempo seconds per row), used whenever a real tempo is known,
  // instead of always dividing however many trimmed frames currently
  // exist evenly across bucket_count. A live take's own row_count
  // (Clip::getLength()) is a provisional window that grows ahead of real
  // time in whole-bar steps (Controller::extendRecordingSampleClipIfNeeded())
  // while real audio arrives continuously - dividing evenly would reflow
  // every already-recorded bucket's own frame range on every single frame
  // appended, and again on every bar-sized row_count jump, so the display
  // kept reshaping content that had already finished recording instead of
  // just extending it. Anchoring each bucket to a fixed interval instead
  // means only the newest buckets ever pick up new content; everything
  // earlier stays exactly where it already was. `tempo <= 0` (a
  // hand-authored or file-referencing clip with no known tempo - see
  // SampleContent::getOriginalTempo()'s own comment) falls back to the
  // even-division scheme, unchanged from before - there's no fixed
  // interval to anchor to without one.
  auto subrow_frames = tempo > 0 ? 60.0 / 4.0 / tempo * native_sample_rate / subrows_per_row : 0.0;

  for (int bucket = 0; bucket < bucket_count; bucket++) {
    int start, end;
    if (subrow_frames > 0.0) {
      start = std::min(in_frame + static_cast<int>(llround(bucket * subrow_frames)), out_frame);
      end = std::min(in_frame + static_cast<int>(llround((bucket + 1) * subrow_frames)), out_frame);
    } else {
      // Evenly divided by bucket index, not a fixed frames-per-bucket
      // stride - the trimmed range's own frame count rarely divides evenly
      // by bucket_count, and this keeps every bucket's own span as close
      // to equal as integer rounding allows instead of concentrating the
      // remainder into one oversized final bucket.
      start = in_frame + static_cast<int>(static_cast<int64_t>(bucket) * trimmed_frames / bucket_count);
      end = in_frame + static_cast<int>(static_cast<int64_t>(bucket + 1) * trimmed_frames / bucket_count);
    }
    // RMS, not the single loudest sample in the bucket - a peak detector
    // makes ordinary, unclipped audio look saturated: real content's own
    // instantaneous peak sample lands close to the clip's overall peak in
    // nearly every short window, so almost the whole shape would read as
    // near-full-height regardless of how loud that stretch actually
    // sounds. RMS instead reflects the bucket's own average energy, so a
    // single transient sample can no longer make an otherwise-quiet
    // stretch look as loud as a genuinely sustained one.
    double sum_sq = 0.0;
    for (int i = start; i < end; i++) { auto s = static_cast<double>(data[i]); sum_sq += s * s; }
    auto count = end - start;
    float rms = count > 0 ? static_cast<float>(sqrt(sum_sq / count)) : 0.0f;
    peaks_.push_back(rms);
  }
}

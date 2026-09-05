#ifndef _WAVEFORMPEAKS_H_
#define _WAVEFORMPEAKS_H_

#include <vector>

class AudioBuffer;

// A downsampled RMS amplitude envelope over a SampleTrack clip's own
// post-trim audio, row-indexed rather than time-indexed: a clip's own
// audio always spans exactly its own getLength() rows by definition
// (that's how the length was set in the first place, ChannelConfiguration::
// framesToRows()), and a future live tempo change is meant to time-stretch
// the audio itself to keep matching that same row span rather than changing
// what row a given moment falls on. build()'s own tempo parameter exists
// only to anchor each bucket to a fixed, tempo-derived frame span while a
// clip's own row window is still provisional (a live take in progress -
// see build()'s own comment) - it's not a seconds/tempo conversion this
// class keeps or exposes anywhere else, and for an already-finished clip
// (whose row_count was itself computed from that same tempo) it reaches
// the same buckets the plain "divide evenly" fallback would anyway.
// Owned by Clip (getWaveformPeaks()), which knows both its own row length
// and its SampleContent - never held directly by SampleContent, which
// knows neither.
class WaveformPeaks {
 public:
  bool empty() const { return peaks_.empty(); }
  int rowCount() const { return subrows_per_row_ > 0 ? static_cast<int>(peaks_.size()) / subrows_per_row_ : 0; }
  // How many independent time-buckets each pattern row was actually built
  // with - whatever build()'s own caller (PatternEditor, driven by
  // UIPlane::canRenderSextants()) asked for at the time, not a fixed
  // constant: quadrant glyphs need 2 (the universally-supported
  // fallback), sextants need 3 (when the terminal actually supports
  // them) - a terminal's own capabilities don't change mid-session, but
  // the cache still tracks what it was actually built for rather than
  // assuming, so a caller can detect a mismatch and rebuild.
  int subrowsPerRow() const { return subrows_per_row_; }

  // RMS amplitude (0..1, a full-scale square wave's own RMS being the
  // theoretical ceiling - real recorded content never actually reaches
  // it) for pattern row `row`'s own `subrow`th time-slice (0 <= subrow <
  // subrowsPerRow()), not normalized against this clip's own loudest
  // bucket - a faint take is meant to read as visibly faint, not stretched
  // to fill the view the same as a loud one. RMS, not the single loudest
  // sample in the bucket - see build()'s own comment for why a peak
  // detector reads wrong here. Out-of-range `row` (a looping clip's later
  // repeat, or a placed instance whose own length has drifted past what
  // its audio actually covers) is the caller's own job to wrap/clamp
  // first - this just indexes the flat array directly and returns 0.0f if
  // it's still out of bounds after that.
  float at(int row, int subrow) const;

  // Rebuilds from scratch against `buffer`'s channel 0 (SampleTrack
  // content is always mono), splitting the frame range `in_point`/
  // `out_point` (SampleContent's own trim seconds, at `native_sample_rate`)
  // into `row_count * subrows_per_row` buckets - the same
  // clamped-to-what's-actually-audible resolution
  // SampleTrackState::triggerClip() already applies for playback,
  // duplicated here rather than shared since that one resolves against
  // the *output* rate post-resample and this one deliberately stays in
  // the buffer's own native rate (this cache is audio-content-indexed,
  // entirely independent of whatever output rate happens to be running).
  //
  // `tempo` (SampleContent::getOriginalTempo()) anchors each bucket to a
  // fixed frame span (60 / 4 / tempo seconds per row, matching
  // ChannelConfiguration::getSampleInterval()'s own convention) when
  // known, rather than always dividing however many trimmed frames
  // currently exist evenly across the bucket count - see the .cpp's own
  // comment for why an always-divide-evenly scheme reflows a live take's
  // already-recorded content every time more audio arrives. `tempo <= 0`
  // (a hand-authored or file-referencing clip with no known tempo) falls
  // back to that even-division scheme instead.
  void build(const AudioBuffer & buffer, int native_sample_rate, float in_point, float out_point, int row_count, int subrows_per_row, int tempo);

 private:
  std::vector<float> peaks_;
  int subrows_per_row_ = 0;
};

#endif

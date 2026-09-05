#ifndef _SAMPLECONTENT_H_
#define _SAMPLECONTENT_H_

#include "../audio/AudioBuffer.h"
#include "WaveformPeaks.h"

#include <memory>

class ParameterSource;

// A SampleTrack clip's own audio payload - the sample-content sibling of
// Clip's own Pattern-per-track_id map (Clip::getPatternsByTrack()), and
// its own nested XML element (<clip><sample>...</sample></clip>) mirrors
// how a note-based clip's own <pattern> child already nests the same way
// - not a flat pile of attributes on <clip> itself. Bundles the buffer
// together with the metadata that only ever makes sense alongside it
// (trim points, native tempo/sample rate) rather than scattering them
// across Clip, which otherwise has no business knowing anything about
// audio at all - a Clip either owns one of these (hasSample()) or it
// doesn't, the same binary split it already had.
//
// getBuffer()'s shared_ptr, not unique_ptr: a playing voice
// (SampleTrackState's adapter, see SampleTrack.cpp) needs to keep reading
// from the buffer for the duration of a note-on independent of whatever
// the UI thread does to the owning Clip in the meantime (edit, delete,
// reload) - the same cross-thread-persistence need every other
// buffer-holding leaf voice in this codebase already has.
class SampleContent {
 public:
  const std::shared_ptr<AudioBuffer> & getBuffer() const { return buffer_; }
  void setBuffer(std::shared_ptr<AudioBuffer> buffer) { buffer_ = std::move(buffer); waveform_peaks_dirty_ = true; stretched_dirty_ = true; }

  // Trim points, in seconds, each measured as "how much to cut from that
  // end" rather than an absolute timestamp - symmetric by design, so 0.0
  // is a genuine, meaningful default for both. Resolved to actual,
  // clamped frame indices only where playback needs them
  // (SampleTrackState::triggerClip(), SampleTrack.cpp) - never stored as
  // frame indices here, since that would go stale the moment a
  // differently-sized buffer replaced this one. Hand-editable in the XML;
  // no in-app editing command in this pass.
  float getInPoint() const { return in_point_; }
  void setInPoint(float seconds) { in_point_ = seconds; waveform_peaks_dirty_ = true; stretched_dirty_ = true; }
  float getOutPoint() const { return out_point_; }
  void setOutPoint(float seconds) { out_point_ = seconds; waveform_peaks_dirty_ = true; stretched_dirty_ = true; }

  // The tempo this audio was actually captured/authored at - 0 means
  // unknown/not set, the same "don't invent a number you can't actually
  // know" convention every other 0-means-unset field here uses. A live
  // recording sets this with certainty, to the song's own tempo at the
  // moment it's captured. A file-referencing clip has no such certainty
  // available automatically - required in practice, hand-authored in the
  // same edit as the sample's own `file` reference, since it's the only
  // way playback can ever know whether (and how) to time-stretch this
  // audio to match the song's own current tempo.
  short getOriginalTempo() const { return original_tempo_; }
  void setOriginalTempo(short bpm) { original_tempo_ = bpm; waveform_peaks_dirty_ = true; stretched_dirty_ = true; }

  // The sample rate getBuffer() is actually at - may differ from the
  // project's own *current* output rate (e.g. a song recorded at 192kHz,
  // later reopened and saved under a 48kHz session). Playback
  // (SampleTrackState::triggerClip()) resamples on demand for whichever
  // rate is actually running, but never mutates getBuffer() itself to do
  // it; saving always writes the buffer at *this* rate, not the current
  // session's, so switching output rates between sessions can never
  // lossily rewrite this audio's own stored content. Never persisted in
  // the XML - a loaded file's own WAV header is already the authoritative
  // record of its rate, re-read fresh on every load; a live recording
  // just remembers whatever rate it was captured at for as long as this
  // object stays in memory.
  int getNativeSampleRate() const { return native_sample_rate_; }
  void setNativeSampleRate(int rate) { native_sample_rate_ = rate; waveform_peaks_dirty_ = true; stretched_dirty_ = true; }

  // Clip's own row-indexed RMS amplitude cache (WaveformPeaks.h) actually
  // lives here, alongside the buffer/trim points it's built from - every
  // setter above that changes what audio this would represent marks it
  // dirty directly, a real invalidation rather than a caller elsewhere
  // having to infer staleness by remembering and comparing old values.
  // `row_count`/`subrows_per_row` aren't this class's own state (a Clip's
  // row length, and a runtime terminal-capability choice, respectively) -
  // passed in fresh by the caller (Clip::getWaveformPeaks()) and compared
  // against what the cache itself already remembers being built with
  // (WaveformPeaks::rowCount()/subrowsPerRow()), rather than tracked here
  // a second time. original_tempo_ is passed straight through too - see
  // WaveformPeaks::build()'s own comment on what it's for.
  //
  // Also compared against the buffer's own current frame count, not just
  // its identity - a live take's own buffer_ is the exact same shared_ptr
  // for its whole duration (Controller::beginSampleCapture()'s own
  // comment: addToSample() appends into it in place), so none of the
  // setters above ever fire again while it's actively growing. Without
  // this, the cache only ever rebuilt when row_count happened to change
  // (extendRecordingSampleClipIfNeeded()'s own whole-bar growth steps),
  // showing a stale snapshot of an earlier, shorter buffer the rest of
  // the time and jumping discontinuously between snapshots instead of
  // tracking the take as it's actually recorded.
  const WaveformPeaks & getWaveformPeaks(int row_count, int subrows_per_row) const {
    auto frame_count = buffer_ ? buffer_->numberOfFrames() : 0;
    if (waveform_peaks_dirty_ || waveform_peaks_.rowCount() != row_count || waveform_peaks_.subrowsPerRow() != subrows_per_row ||
        frame_count != waveform_peaks_built_frame_count_) {
      waveform_peaks_ = WaveformPeaks();
      if (buffer_) waveform_peaks_.build(*buffer_, native_sample_rate_, in_point_, out_point_, row_count, subrows_per_row, original_tempo_);
      waveform_peaks_dirty_ = false;
      waveform_peaks_built_frame_count_ = frame_count;
    }
    return waveform_peaks_;
  }

  // The tempo-stretched cache SampleTrackState::triggerClip() plays
  // instead of getBuffer() whenever original_tempo_ disagrees with the
  // song's current one - unlike getWaveformPeaks() above, this class
  // can't build the cached value itself: actually stretching needs a real
  // external-library call (TimeStretcher.h, src/audio/) this model-layer
  // class must not depend on directly. So this only holds the result;
  // SampleTrackState::triggerClip() (SampleTrack.cpp) is what calls
  // TimeStretcher and stores what it built back via setStretchedBuffer()
  // below. Keyed on `song_tempo` alone, not also the output sample rate
  // the stretch was actually run at (unlike getWaveformPeaks()'s
  // row_count/subrows_per_row pair) - the project's own output rate never
  // changes mid-session, so there's nothing yet to compare against; a
  // stale entry from a different rate isn't a case that can happen today.
  // A cache miss (nothing stored yet, invalidated by a setter above, or a
  // different song_tempo than whatever's actually cached) returns nullptr
  // - the caller is expected to build and store one, not treat this as an
  // error. Returned by value, not by reference: a mismatched query must
  // never have the side effect of evicting a still-valid entry cached for
  // some *other* song_tempo (single-slot, so a later trigger at the
  // *original* tempo would otherwise force a wasted rebuild even though
  // nothing about this clip's own audio actually changed).
  std::shared_ptr<AudioBuffer> getStretchedBuffer(int song_tempo) const {
    if (stretched_dirty_ || stretched_song_tempo_ != song_tempo) return nullptr;
    return stretched_buffer_;
  }
  void setStretchedBuffer(std::shared_ptr<AudioBuffer> buffer, int song_tempo) const {
    stretched_buffer_ = std::move(buffer);
    stretched_song_tempo_ = song_tempo;
    stretched_dirty_ = false;
  }

  // Reads/writes in_point_/out_point_/original_tempo_ (<sample in="..."
  // out="..." originalTempo="...">) - native_sample_rate_ deliberately
  // excluded, same "the file's own header is already the authoritative
  // record" reasoning above; `buffer_`/the sample's own `file` reference
  // stay outside this method too - resolving them needs disk I/O and the
  // song's own directory, which Song.cpp's clip reader/writer already
  // handles directly, the same way a clip's own <pattern> child bypasses
  // Clip::loadParameters() entirely.
  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const;

 private:
  std::shared_ptr<AudioBuffer> buffer_;
  float in_point_ = 0.0f, out_point_ = 0.0f;
  short original_tempo_ = 0;
  int native_sample_rate_ = 0;

  // getWaveformPeaks()'s own lazy cache - mutable since a const read can
  // still trigger a rebuild, the same reasoning any other lazily-computed
  // cache in this codebase already has; real invalidation happens above,
  // in the setters that actually change the audio this represents.
  mutable WaveformPeaks waveform_peaks_;
  mutable bool waveform_peaks_dirty_ = true;
  // The buffer's own numberOfFrames() as of the last build - see
  // getWaveformPeaks()'s own comment on why this matters alongside
  // waveform_peaks_dirty_ above. -1 (not 0) so a genuinely empty buffer's
  // first build (frame_count 0) doesn't read as already-cached.
  mutable int waveform_peaks_built_frame_count_ = -1;

  // getStretchedBuffer()/setStretchedBuffer()'s own cache - mutable for
  // the same reason as the waveform cache above, but this class never
  // builds it itself (see getStretchedBuffer()'s own comment).
  mutable std::shared_ptr<AudioBuffer> stretched_buffer_;
  mutable int stretched_song_tempo_ = 0;
  mutable bool stretched_dirty_ = true;
};

#endif

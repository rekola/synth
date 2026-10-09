#ifndef _SAMPLECONTENT_H_
#define _SAMPLECONTENT_H_

#include "../audio/AudioBuffer.h"
#include "WaveformPeaks.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

class ParameterSource;

// A SampleTrack clip's own audio payload - the sample-content sibling of
// Clip's own Pattern (Clip::getLeafPattern()), and its own nested XML
// element (<clip><sample>...</sample></clip>) mirrors
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
  SampleContent() = default;
  // A copy shares the audio buffer and the tempo-stretch cache (so a copy
  // handed to the audio thread doesn't throw away work it has already done)
  // and keeps the same identity(); the waveform cache is the UI's own and is
  // rebuilt lazily by whoever needs it.
  SampleContent(const SampleContent & other)
      : buffer_(other.buffer_), in_point_(other.in_point_), out_point_(other.out_point_),
        original_tempo_(other.original_tempo_), native_sample_rate_(other.native_sample_rate_),
        identity_(other.identity_), stretch_cache_(other.sharedStretchCache()) { }
  SampleContent & operator=(const SampleContent & other) {
    if (this != &other) {
      buffer_ = other.buffer_;
      in_point_ = other.in_point_;
      out_point_ = other.out_point_;
      original_tempo_ = other.original_tempo_;
      native_sample_rate_ = other.native_sample_rate_;
      identity_ = other.identity_;
      stretch_cache_ = other.sharedStretchCache();
      waveform_peaks_ = WaveformPeaks();
      waveform_peaks_dirty_ = true;
      waveform_peaks_built_frame_count_ = -1;
    }
    return *this;
  }
  SampleContent(SampleContent &&) = default;
  SampleContent & operator=(SampleContent &&) = default;

  // Names this audio across copies: stable while the content is only copied,
  // new whenever it is replaced or edited. 0 for content with no buffer.
  // Playback compares these across blocks, where a pointer to a copy could
  // not be compared.
  uint64_t identity() const { return identity_; }

  const std::shared_ptr<AudioBuffer> & getBuffer() const { return buffer_; }
  void setBuffer(std::shared_ptr<AudioBuffer> buffer) { buffer_ = std::move(buffer); changed(); }

  // Rows the whole buffer spans at `tempo`, rounded up - a row is a
  // sixteenth, 4 * tempo rows a minute.
  int getRowCount(int tempo) const {
    if (!buffer_ || native_sample_rate_ <= 0) return 0;
    auto seconds = static_cast<double>(buffer_->numberOfFrames()) / native_sample_rate_;
    return static_cast<int>(std::ceil(seconds * tempo * 4 / 60.0));
  }

  // Trim points, in seconds, each measured as "how much to cut from that
  // end" rather than an absolute timestamp - symmetric by design, so 0.0
  // is a genuine, meaningful default for both. Resolved to actual,
  // clamped frame indices only where playback needs them
  // (SampleTrackState::triggerClip(), SampleTrack.cpp) - never stored as
  // frame indices here, since that would go stale the moment a
  // differently-sized buffer replaced this one. Hand-editable in the XML;
  // no in-app editing command in this pass.
  float getInPoint() const { return in_point_; }
  void setInPoint(float seconds) { in_point_ = seconds; changed(); }
  float getOutPoint() const { return out_point_; }
  void setOutPoint(float seconds) { out_point_ = seconds; changed(); }

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
  void setOriginalTempo(short bpm) { original_tempo_ = bpm; changed(); }

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
  void setNativeSampleRate(int rate) { native_sample_rate_ = rate; changed(); }

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
    if (!stretch_cache_ || stretch_cache_->song_tempo != song_tempo) return nullptr;
    return stretch_cache_->buffer;
  }
  void setStretchedBuffer(std::shared_ptr<AudioBuffer> buffer, int song_tempo) const {
    auto cache = sharedStretchCache();
    cache->buffer = std::move(buffer);
    cache->song_tempo = song_tempo;
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

  // getStretchedBuffer()/setStretchedBuffer()'s own cache - this class never
  // builds it itself (see getStretchedBuffer()'s own comment). Shared by
  // every copy of this content, and replaced (not cleared) when the content
  // changes, so a copy made before the change keeps a cache that still
  // matches its own audio.
  struct StretchCache {
    std::shared_ptr<AudioBuffer> buffer;
    int song_tempo = 0;
  };
  std::shared_ptr<StretchCache> sharedStretchCache() const {
    if (!stretch_cache_) stretch_cache_ = std::make_shared<StretchCache>();
    return stretch_cache_;
  }
  void changed() {
    waveform_peaks_dirty_ = true;
    stretch_cache_.reset();
    identity_ = buffer_ ? nextIdentity() : 0;
  }
  static uint64_t nextIdentity() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
  }

  uint64_t identity_ = 0;
  mutable std::shared_ptr<StretchCache> stretch_cache_;
};

#endif

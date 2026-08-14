#ifndef _AUDIOBUFFER_H_
#define _AUDIOBUFFER_H_

#include "ChannelConfiguration.h"

#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

// Channel presence, queried uniformly via hasChannel() - all three values
// are decided once, when an AudioBuffer is constructed, and never change
// afterward (not even by zero()/clear() - those reset sample values, not
// the buffer's shape). Main covers the whole regular/ambisonic channel
// block (0..regularChannelCount()-1, in ACN order - see
// AmbisonicEncoding.h) as a single yes/no fact rather than one enum value
// per channel (an earlier version of this file did that, up through Acn8
// at order 2 - adding Acn9..15 too, purely to keep a raw-index naming
// pattern going, wasn't worth it); its presence is derived, not stored -
// see hasChannel() - since regularChannelCount() > 0 already says
// everything a stored flag would, unambiguously. AuxA/AuxB are different:
// they're not part of that regular run at all (a buffer may carry either,
// both, or neither, independent of whether Main itself is present), and
// unlike Main their presence genuinely needs its own stored bit each -
// knowing "one aux channel is present" doesn't say which one - so they
// keep real names and a raw index derived from however many regular
// channels precede them (see indexOf()).
enum class Channel : int8_t { Main, AuxA, AuxB };

// Mutated in place constantly and deliberately throughout the render path
// (zero()/mix()/mixNamed()/assign()/assignNamed()/resize()/append(), called
// on the same instance once per audio block for every voice/track/mixer
// stage) - that in-place-accumulator pattern is the reason this class
// exists, not an oversight to be designed away. A genuinely immutable value
// type (every mutator returning a new instance instead of writing in place)
// was considered and rejected: it would mean a fresh heap allocation every
// block for every voice/track/effect stage, a real audio-thread cost with
// no bug or leak behind it to justify paying it. The actually-enforceable
// discipline is at the reference level: every function that only reads a
// buffer takes `const AudioBuffer &` (see every Mixer/SongState/
// AudioBlockEvent accessor, AlsaAudio::play, dsp/SpectrumAnalyzer::addData,
// ...) - a non-const `AudioBuffer &` parameter always means genuine,
// intentional in-place accumulation or processing (effects/*.cpp's
// applyEffect(), bus/BusEffect.h's encodeDirect(), dsp/ChorusEngine::
// process()), never an oversight.
class AudioBuffer final {
 public:
  AudioBuffer() noexcept
    : channels_(0), frames_(0), data_(0) { }
  explicit AudioBuffer(short channels, int frames, bool is_solo = false) noexcept
    : channels_(channels), frames_(frames), is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  // Regular (ambisonic) channels only, no aux - config.numberOfChannels()
  // is already the raw channel count (0 = W, 1 = Y, ... in ACN order), so
  // there's nothing left to mark present the way the old per-channel enum
  // needed.
  explicit AudioBuffer(ChannelConfiguration config, int frames, bool is_solo = false) noexcept
    : channels_(config.numberOfChannels()),
    frames_(frames),
    is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  // `regular_channels` ambisonic channels (raw indices 0..regular_channels-1,
  // in ACN order - 0 is a valid value, meaning Main is absent entirely, see
  // hasChannel()) plus AuxA and/or AuxB if the caller asks for them - e.g. a
  // leaf voice building { config.numberOfChannels() regular channels if its
  // Send Main level is > 0 else 0, AuxA present if this track sends to bus
  // A, ditto AuxB }. Aux channels always land immediately after the
  // regular channels, AuxA before AuxB (see indexOf()).
  explicit AudioBuffer(int regular_channels, bool aux_a, bool aux_b, int frames, bool is_solo = false) noexcept
    : channels_(static_cast<short>(regular_channels + (aux_a ? 1 : 0) + (aux_b ? 1 : 0))),
    frames_(frames), is_solo_(is_solo), has_aux_a_(aux_a), has_aux_b_(aux_b) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  AudioBuffer(const AudioBuffer & other) noexcept
    : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_), bpm_(other.bpm_), has_aux_a_(other.has_aux_a_), has_aux_b_(other.has_aux_b_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data_, s);
  }
  AudioBuffer(AudioBuffer && other) noexcept
    : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_), bpm_(other.bpm_), has_aux_a_(other.has_aux_a_), has_aux_b_(other.has_aux_b_) {
  }
  ~AudioBuffer() {
    free(data_);
  }
  AudioBuffer & operator=(const AudioBuffer & other) noexcept {
    if (&other != this) {
      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      bpm_ = other.bpm_;
      has_aux_a_ = other.has_aux_a_;
      has_aux_b_ = other.has_aux_b_;

      auto s = getAlignedSize(channels_ * frames_);
      auto new_data = (float *)aligned_alloc(16, s);
      memcpy(new_data, other.data_, s);

      free(data_);
      data_ = new_data;
    }
    return *this;
  }
  AudioBuffer & operator=(AudioBuffer && other) noexcept {
    if (&other != this) {
      free(data_);

      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      bpm_ = other.bpm_;
      has_aux_a_ = other.has_aux_a_;
      has_aux_b_ = other.has_aux_b_;

      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * getChannelData(int channel) { return data_ + channel * numberOfFrames(); }
  const float * getChannelData(int channel) const { return data_ + channel * numberOfFrames(); }

  // Main's presence is derived (see the Channel enum's own doc comment
  // above), not stored - always consistent with regularChannelCount(),
  // can never drift out of sync the way a separate flag could.
  bool hasChannel(Channel ch) const {
    if (ch == Channel::Main) return channels_ - auxCount() > 0;
    return ch == Channel::AuxA ? has_aux_a_ : has_aux_b_;
  }

  // Channel::Main returns a pointer to channel 0 (the first of however
  // many regular channels there are) when present, matching AuxA/AuxB's
  // own "pointer or nullptr" contract - indexOf() handles all three
  // values uniformly (Main is always channel 0).
  float * getChannel(Channel ch) {
    return hasChannel(ch) ? getChannelData(indexOf(ch)) : nullptr;
  }
  const float * getChannel(Channel ch) const {
    return hasChannel(ch) ? getChannelData(indexOf(ch)) : nullptr;
  }

  int auxCount() const { return (hasChannel(Channel::AuxA) ? 1 : 0) + (hasChannel(Channel::AuxB) ? 1 : 0); }
  int regularChannelCount() const { return channels_ - auxCount(); }

  void zero() {
    memset(data_, 0, getAlignedSize(channels_ * frames_));
  }

  void clear() {
    free(data_);
    data_ = 0;
    frames_ = 0;
  }

  short numberOfChannels() const { return channels_; }
  int size() const { return frames_; }
  int numberOfFrames() const { return frames_; }
  bool empty() const { return channels_ == 0 || frames_ == 0; }

  void resize(int new_size) {
    auto new_data = (float *)aligned_alloc(16, getAlignedSize(channels_ * new_size));
    auto frames_to_copy = frames_ < new_size ? frames_ : new_size;
    for (int j = 0; j < channels_; j++) {
      memcpy(new_data + j * new_size, data_ + j * frames_, static_cast<size_t>(frames_to_copy) * sizeof(float));
    }
    free(data_);
    data_ = new_data;
    frames_ = new_size;
  }

  void append(const AudioBuffer & other) {
    int old_frames = numberOfFrames();
    resize(old_frames + other.numberOfFrames());
    assign(other, old_frames);
  }

  void assign(const AudioBuffer & other, int position) {
    if (other.empty() || position >= numberOfFrames()) return;
    assert(channels_ == other.channels_);
    assert(position >= 0);

    if (channels_ == other.channels_) {
      if (!bpm_) bpm_ = other.bpm_;

      int n = other.numberOfFrames() < numberOfFrames() ? other.numberOfFrames() : numberOfFrames();
      if (position + n > numberOfFrames()) position = numberOfFrames() - n;

      for (int j = 0; j < channels_; j++) {
	auto other_channel_data = other.getChannelData(j);
	auto channel_data = getChannelData(j);
	for (int i = 0; i < n; i++) {
	  channel_data[i + position] = other_channel_data[i];
	}
      }
    }
  }

  // Like assign(), but for the same "other may carry AuxA/AuxB that this
  // does (or doesn't) have" tolerance mixNamed() adds to mix() - the
  // regular channels are copied unconditionally (they always match, same
  // assumption assign() already makes), and AuxA/AuxB are each copied
  // only if *both* sides have it; a side missing it is simply left alone
  // (already zeroed by the caller, e.g. before assembling chunks that
  // don't all carry the same aux channels - see InstrumentTrackState::render).
  void assignNamed(const AudioBuffer & other, int position) {
    if (other.empty() || position >= numberOfFrames()) return;
    assert(position >= 0);

    int regular = regularChannelCount();
    assert(regular == other.regularChannelCount());

    if (!bpm_) bpm_ = other.bpm_;

    int n = other.numberOfFrames() < numberOfFrames() ? other.numberOfFrames() : numberOfFrames();
    if (position + n > numberOfFrames()) position = numberOfFrames() - n;

    for (int c = 0; c < regular; c++) {
      auto src = other.getChannelData(c);
      auto dst = getChannelData(c);
      for (int i = 0; i < n; i++) dst[i + position] = src[i];
    }
    for (auto ch : { Channel::AuxA, Channel::AuxB }) {
      if (hasChannel(ch) && other.hasChannel(ch)) {
	auto src = other.getChannel(ch);
	auto dst = getChannel(ch);
	for (int i = 0; i < n; i++) dst[i + position] = src[i];
      }
    }
  }

  void mix(const AudioBuffer & other) {
    if (other.empty()) return;
    if (!bpm_) bpm_ = other.bpm_;

    int n = numberOfFrames() < other.numberOfFrames() ? numberOfFrames() : other.numberOfFrames();

    if (channels_ == other.channels_) {
      for (int i = 0; i < channels_ * n; i++) {
	data_[i] += other.data_[i];
      }
    } else if (other.channels_ == 1) {
      auto left = getChannelData(0), right = getChannelData(1);

      for (int i = 0; i < n; i++) {
	auto v = other.data_[i];
	left[i] += v;
	right[i] += v;
      }
    } else {
      assert(0);
    }
  }

  // A drop-in replacement for mix() wherever `other` may carry AuxA/AuxB
  // that `this` doesn't care about (or only partially shares), or may
  // have zero regular (Main) channels while `this` has some (e.g. a
  // child voice whose Send Main level is 0, mixing into an accumulator
  // that has Main because some other child does) - handles three regular-
  // channel cases: exact match; mono broadcast into a 2-channel target;
  // `other` has none at all (a clean no-op for the regular part) - then,
  // separately, sums AuxA/AuxB wherever *both* sides have it present. An
  // aux channel present on only one side (including a `this` that never
  // marks any aux present at all, e.g. every Mixer's own accumulator -
  // see Mixer.h) simply contributes nothing there - silently ignored
  // rather than asserting, unlike a genuine regular-channel mismatch
  // (still an assert(0), same as mix()).
  void mixNamed(const AudioBuffer & other) {
    if (other.empty()) return;
    if (!bpm_) bpm_ = other.bpm_;

    int n = numberOfFrames() < other.numberOfFrames() ? numberOfFrames() : other.numberOfFrames();
    int regular = regularChannelCount();
    int other_regular = other.regularChannelCount();

    if (other_regular > 0) {
      if (regular == other_regular) {
	for (int c = 0; c < regular; c++) {
	  auto dst = getChannelData(c);
	  auto src = other.getChannelData(c);
	  for (int i = 0; i < n; i++) dst[i] += src[i];
	}
      } else if (other_regular == 1 && regular == 2) {
	auto left = getChannelData(0), right = getChannelData(1);
	auto src = other.getChannelData(0);
	for (int i = 0; i < n; i++) {
	  auto v = src[i];
	  left[i] += v;
	  right[i] += v;
	}
      } else {
	assert(0);
      }
    }

    for (auto ch : { Channel::AuxA, Channel::AuxB }) {
      if (hasChannel(ch) && other.hasChannel(ch)) {
	auto dst = getChannel(ch);
	auto src = other.getChannel(ch);
	for (int i = 0; i < n; i++) dst[i] += src[i];
      }
    }
  }

  // No isZero()-style short-circuit any more (see the Channel enum's
  // own doc comment - there's no cheap "definitely silent" flag left to
  // check): a buffer can legitimately have zero Main channels and real,
  // meaningful AuxA/AuxB content (a fully aux-only voice), so this must
  // always scan the real sample data rather than gate on Main presence.
  std::vector<float> calculateLoudness() const {
    std::vector<float> v;
    for (int i = 0; i < channels_; i++) {
      float sum_squares = 0;
      auto channel_data = getChannelData(i);
      for (int j = 0; j < frames_; j++) {
	auto s = channel_data[j];
	sum_squares += s * s;
      }
      v.push_back(sqrtf(sum_squares));
    }
    return v;
  }

  // See calculateLoudness() above - same reasoning, always scans.
  bool isClipping() const {
    for (int i = 0; i < channels_ * frames_; i++) {
      auto v = data_[i];
      if (v < -1.0f || v > +1.0f) return true;
    }
    return false;
  }

  bool isSolo() const { return is_solo_; }
  void setSolo(bool s) { is_solo_ = s; }

  void setBpm(float bpm) { bpm_ = bpm; }
  float getBpm() const { return bpm_; }

private:
  static inline size_t getAlignedSize(int frames) { return (static_cast<size_t>(frames) * sizeof(float) + 15ull) & ~15ull; }

  // Channel::Main is always channel 0 when present. AuxA/AuxB always land
  // immediately after the regular (Main) channels, AuxA before AuxB - so
  // AuxA's raw index is just however many regular channels there are, and
  // AuxB's is one past that if AuxA is also present.
  int indexOf(Channel ch) const {
    if (ch == Channel::Main) return 0;
    int idx = regularChannelCount();
    if (ch == Channel::AuxB && has_aux_a_) idx++;
    return idx;
  }

  short channels_;
  int frames_;
  float * data_;
  bool is_solo_ = false;
  float bpm_ = 0.0f;
  bool has_aux_a_ = false, has_aux_b_ = false;
};

#endif

#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include "ChannelConfiguration.h"

#include <bitset>
#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

// Named channels a SampleData buffer's raw indices may correspond to. A
// channel's raw index is never stored explicitly - it's derived by counting
// how many *other present* channels come earlier in this declaration order
// (see SampleData::indexOf), so e.g. Mono is always raw channel 0 whenever
// present, and SendA's index depends entirely on however many regular
// channels precede it. Acn4-8 (ambisonic degree 2) are named individually,
// not left anonymous, purely so that counting-based index derivation stays
// correct at order 2 - if only W/Y/Z/X were named, SendA would be placed at
// index 4 (colliding with Acn4-8's real data at indices 4-8) instead of the
// correct index 9.
// No separate Mono channel: MONO is 0th-order ambisonics (a single
// omnidirectional/W component - see ChannelConfiguration), so a mono buffer
// just marks W present and leaves Y/Z/X absent, rather than duplicating the
// same "one omnidirectional channel" concept under two different names.
enum class Channel : int8_t {
  W, Y, Z, X,
  Acn4, Acn5, Acn6, Acn7, Acn8,
  SendA, SendB,
  Count
};

// The regular (non-send) channel set implied by a ChannelConfiguration -
// exactly what the ChannelConfiguration-based SampleData constructor marks
// present. Exposed so callers building an accumulator that also needs
// SendA/SendB (whose presence isn't something ChannelConfiguration knows or
// cares about - see SampleData's own constructors) can start from this list
// and append to it before constructing via the vector-of-Channel
// constructor.
inline std::vector<Channel> regularChannelsFor(const ChannelConfiguration & config) {
  std::vector<Channel> v { Channel::W };
  if (config.getAmbisonicOrder() >= 1) v.insert(v.end(), { Channel::Y, Channel::Z, Channel::X });
  if (config.getAmbisonicOrder() >= 2) v.insert(v.end(), { Channel::Acn4, Channel::Acn5, Channel::Acn6, Channel::Acn7, Channel::Acn8 });
  return v;
}

class SampleData final {
 public:
  SampleData() noexcept
    : channels_(0), frames_(0), data_(0) { }
  explicit SampleData(short channels, int frames, bool is_solo = false) noexcept
    : channels_(channels), frames_(frames), is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  explicit SampleData(ChannelConfiguration config, int frames, bool is_solo = false) noexcept
    : channels_(config.numberOfChannels()),
    frames_(frames),
    is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
    for (auto ch : regularChannelsFor(config)) present_.set(static_cast<size_t>(ch));
  }
  // Regular channel(s) plus whichever sends the caller decided are present
  // (see regularChannelsFor(config) above for building the list), or a
  // fixed compile-time set at a leaf voice (e.g. { Channel::W,
  // Channel::SendA }). channels_ is derived from the list's size.
  explicit SampleData(const std::vector<Channel> & channels, int frames, bool is_solo = false) noexcept
    : channels_(static_cast<short>(channels.size())), frames_(frames), is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
    for (auto ch : channels) present_.set(static_cast<size_t>(ch));
  }
  SampleData(const SampleData & other) noexcept
    : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_), present_(other.present_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data_, s);
  }
  SampleData(SampleData && other) noexcept
    : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_), present_(other.present_) {
  }
  ~SampleData() {
    free(data_);
  }
  SampleData & operator=(const SampleData & other) noexcept {
    if (&other != this) {
      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      is_zero_ = other.is_zero_;
      bpm_ = other.bpm_;
      present_ = other.present_;

      auto s = getAlignedSize(channels_ * frames_);
      auto new_data = (float *)aligned_alloc(16, s);
      memcpy(new_data, other.data_, s);

      free(data_);
      data_ = new_data;
    }
    return *this;
  }
  SampleData & operator=(SampleData && other) noexcept {
    if (&other != this) {
      free(data_);

      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      is_zero_ = other.is_zero_;
      bpm_ = other.bpm_;
      present_ = other.present_;

      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * getChannelData(int channel) { return data_ + channel * numberOfFrames(); }
  const float * getChannelData(int channel) const { return data_ + channel * numberOfFrames(); }

  bool hasChannel(Channel ch) const { return present_.test(static_cast<size_t>(ch)); }

  float * getChannel(Channel ch) { return hasChannel(ch) ? getChannelData(indexOf(ch)) : nullptr; }
  const float * getChannel(Channel ch) const { return hasChannel(ch) ? getChannelData(indexOf(ch)) : nullptr; }

  int sendCount() const { return (hasChannel(Channel::SendA) ? 1 : 0) + (hasChannel(Channel::SendB) ? 1 : 0); }

  void zero() {
    memset(data_, 0, getAlignedSize(channels_ * frames_));
    is_zero_ = true;
  }
  
  void clear() {
    free(data_);
    data_ = 0;
    frames_ = 0;
    is_zero_ = true;
  }
  
  short numberOfChannels() const { return channels_; }
  int size() const { return frames_; }
  int numberOfFrames() const { return frames_; }
  bool empty() const { return channels_ == 0 || frames_ == 0; }

  void resize(int new_size) {
    auto new_data = (float *)aligned_alloc(16, getAlignedSize(channels_ * new_size));
    auto frames_to_copy = frames_ < new_size ? frames_ : new_size;
    for (int j = 0; j < channels_; j++) {
      memcpy(new_data + j * new_size, data_ + j * frames_, frames_to_copy * sizeof(float));
    }
    free(data_);
    data_ = new_data;
    frames_ = new_size;
  }

  void append(const SampleData & other) {
    int old_frames = numberOfFrames();
    resize(old_frames + other.numberOfFrames());
    assign(other, old_frames);
  }
  
  void assign(const SampleData & other, int position) {
    if (other.empty() || position >= numberOfFrames()) return;
    assert(channels_ == other.channels_);
    assert(position >= 0);
    
    if (channels_ == other.channels_) {
      if (!other.is_zero_) is_zero_ = false;
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

  // Like assign(), but for the same "other may carry SendA/SendB that this
  // does (or doesn't) have" tolerance mixNamed() adds to mix() - the
  // regular channels are copied unconditionally (they always match, same
  // assumption assign() already makes), and SendA/SendB are each copied
  // only if *both* sides have it; a side missing it is simply left alone
  // (already zeroed by the caller, e.g. before assembling chunks that
  // don't all carry the same sends - see InstrumentTrackState::render).
  void assignNamed(const SampleData & other, int position) {
    if (other.empty() || position >= numberOfFrames()) return;
    assert(position >= 0);

    int regular = channels_ - sendCount();
    assert(regular == other.channels_ - other.sendCount());

    if (!other.is_zero_) is_zero_ = false;
    if (!bpm_) bpm_ = other.bpm_;

    int n = other.numberOfFrames() < numberOfFrames() ? other.numberOfFrames() : numberOfFrames();
    if (position + n > numberOfFrames()) position = numberOfFrames() - n;

    for (int c = 0; c < regular; c++) {
      auto src = other.getChannelData(c);
      auto dst = getChannelData(c);
      for (int i = 0; i < n; i++) dst[i + position] = src[i];
    }
    for (auto ch : { Channel::SendA, Channel::SendB }) {
      if (hasChannel(ch) && other.hasChannel(ch)) {
	auto src = other.getChannel(ch);
	auto dst = getChannel(ch);
	for (int i = 0; i < n; i++) dst[i + position] = src[i];
      }
    }
  }

  void mix(const SampleData & other) {
    if (!other.isZero()) {
      is_zero_ = false;
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
  }

  // A drop-in replacement for mix() wherever `other` may carry SendA/SendB
  // that `this` doesn't care about (or only partially shares) - handles the
  // same two regular-channel cases mix() does (exact match; mono broadcast
  // into a 2-channel target), using only the *regular* channel counts
  // (numberOfChannels() minus however many of SendA/SendB are present) to
  // decide which - then, separately, sums SendA/SendB wherever *both* sides
  // have it present. A send present on only one side (including a `this`
  // that never marks any send present at all, e.g. every Mixer's own
  // accumulator - see Mixer.h) simply contributes nothing there - silently
  // ignored rather than asserting, unlike a genuine regular-channel
  // mismatch (still an assert(0), same as mix()).
  void mixNamed(const SampleData & other) {
    if (other.isZero()) return;
    is_zero_ = false;
    if (!bpm_) bpm_ = other.bpm_;

    int n = numberOfFrames() < other.numberOfFrames() ? numberOfFrames() : other.numberOfFrames();
    int regular = channels_ - sendCount();
    int other_regular = other.channels_ - other.sendCount();

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

    for (auto ch : { Channel::SendA, Channel::SendB }) {
      if (hasChannel(ch) && other.hasChannel(ch)) {
	auto dst = getChannel(ch);
	auto src = other.getChannel(ch);
	for (int i = 0; i < n; i++) dst[i] += src[i];
      }
    }
  }

  std::vector<float> calculateLoudness() const {
    std::vector<float> v;
    for (int i = 0; i < channels_; i++) {
      if (isZero()) {
	v.push_back(0.0f);
      } else {
	float sum_squares = 0;
	auto channel_data = getChannelData(i);
	for (int j = 0; j < frames_; j++) {
	  auto s = channel_data[j];
	  sum_squares += s * s;
	}
	v.push_back(sqrtf(sum_squares));
      }
    }
    return v;
  }

  bool isClipping() const {
    if (isZero()) return false;
    
    for (int i = 0; i < channels_ * frames_; i++) {
      auto v = data_[i];
      if (v < -1.0f || v > +1.0f) return true;
    }
    return false;
  }
  
  bool isSolo() const { return is_solo_; }
  void setSolo(bool s) { is_solo_ = s; }

  void setNonZero() { is_zero_ = false; }
  bool isZero() const { return is_zero_; }  

  void setBpm(float bpm) { bpm_ = bpm; }
  float getBpm() const { return bpm_; }
  
private:
  static inline size_t getAlignedSize(int frames) { return (static_cast<size_t>(frames) * sizeof(float) + 15ull) & ~15ull; }

  // A channel's raw index is however many *other present* channels come
  // earlier in Channel's declaration order - not stored, always derived,
  // so it stays correct regardless of which subset of channels is present.
  int indexOf(Channel ch) const {
    int idx = 0;
    for (int i = 0; i < static_cast<int>(ch); i++) {
      if (present_.test(static_cast<size_t>(i))) idx++;
    }
    return idx;
  }

  short channels_;
  float bpm_ = 0.0f;
  int frames_;
  float * data_;
  bool is_solo_ = false, is_zero_ = true;
  std::bitset<static_cast<size_t>(Channel::Count)> present_;

  // ChannelData
};

#endif

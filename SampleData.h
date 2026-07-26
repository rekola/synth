#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include "ChannelConfiguration.h"

#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

// The only two channel identities a SampleData buffer ever needs a *name*
// for. Every regular (ambisonic) channel is addressed by its plain raw
// index instead (0 = W, 1 = Y, ... - see AmbisonicEncoding.h's ACN
// ordering) rather than one enum value per channel: with up to 16 of them
// at order 3, naming each individually (as an earlier version of this file
// did, up through Acn8 at order 2) added nothing a raw index doesn't
// already say just as clearly, and would have meant adding Acn9-15 here
// too purely to keep a pattern going. SendA/SendB are different - they're
// not part of that fixed, densely-packed 0..N-1 run at all (a buffer may
// carry either, both, or neither, independent of its regular channel
// count), so they still get real names and a raw index derived from
// however many regular channels precede them (see indexOf()).
enum class Channel : int8_t { SendA, SendB };

class SampleData final {
 public:
  SampleData() noexcept
    : channels_(0), frames_(0), data_(0) { }
  explicit SampleData(short channels, int frames, bool is_solo = false) noexcept
    : channels_(channels), frames_(frames), is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  // Regular (ambisonic) channels only, no sends - config.numberOfChannels()
  // is already the raw channel count (0 = W, 1 = Y, ... in ACN order), so
  // there's nothing left to mark present the way the old per-channel enum
  // needed.
  explicit SampleData(ChannelConfiguration config, int frames, bool is_solo = false) noexcept
    : channels_(config.numberOfChannels()),
    frames_(frames),
    is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  // `regular_channels` ambisonic channels (raw indices 0..regular_channels-1,
  // in ACN order) plus SendA and/or SendB if the caller asks for them -
  // e.g. a leaf voice building { config.numberOfChannels() regular channels,
  // SendA present if this track sends to bus A, ditto SendB }. Sends always
  // land immediately after the regular channels, SendA before SendB (see
  // indexOf()).
  explicit SampleData(int regular_channels, bool send_a, bool send_b, int frames, bool is_solo = false) noexcept
    : channels_(static_cast<short>(regular_channels + (send_a ? 1 : 0) + (send_b ? 1 : 0))),
    frames_(frames), is_solo_(is_solo), has_send_a_(send_a), has_send_b_(send_b) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  SampleData(const SampleData & other) noexcept
    : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_), has_send_a_(other.has_send_a_), has_send_b_(other.has_send_b_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data_, s);
  }
  SampleData(SampleData && other) noexcept
    : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_), has_send_a_(other.has_send_a_), has_send_b_(other.has_send_b_) {
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
      has_send_a_ = other.has_send_a_;
      has_send_b_ = other.has_send_b_;

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
      has_send_a_ = other.has_send_a_;
      has_send_b_ = other.has_send_b_;

      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * getChannelData(int channel) { return data_ + channel * numberOfFrames(); }
  const float * getChannelData(int channel) const { return data_ + channel * numberOfFrames(); }

  bool hasChannel(Channel ch) const { return ch == Channel::SendA ? has_send_a_ : has_send_b_; }

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

  // SendA/SendB always land immediately after the regular (ambisonic)
  // channels, SendA before SendB - so SendA's raw index is just however
  // many regular channels there are, and SendB's is one past that if
  // SendA is also present.
  int indexOf(Channel ch) const {
    int idx = channels_ - sendCount();
    if (ch == Channel::SendB && has_send_a_) idx++;
    return idx;
  }

  short channels_;
  float bpm_ = 0.0f;
  int frames_;
  float * data_;
  bool is_solo_ = false, is_zero_ = true;
  bool has_send_a_ = false, has_send_b_ = false;

  // ChannelData
};

#endif

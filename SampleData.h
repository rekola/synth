#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include "ChannelConfiguration.h"

#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>
#include <vector>

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
  }
  SampleData(const SampleData & other) noexcept
    : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data_, s);
  }
  SampleData(SampleData && other) noexcept
    : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_), is_zero_(other.is_zero_), bpm_(other.bpm_) {
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
      
      auto s = getAlignedSize(channels_ * frames_);
      data_ = (float *)aligned_alloc(16, s);
      
      memcpy(data_, other.data_, s);
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
      
      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * getChannelData(int channel) { return data_ + channel * numberOfFrames(); }
  const float * getChannelData(int channel) const { return data_ + channel * numberOfFrames(); }  

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
    for (int j = 0; j < channels_; j++) {
      memcpy(new_data + j * new_size, data_ + j * frames_, frames_ * sizeof(float));
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
  
  short channels_, bpm_ = 0;
  int frames_;
  float * data_;
  bool is_solo_ = false, is_zero_ = true;

  // ChannelData
};

#endif

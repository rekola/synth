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
  explicit SampleData() noexcept
    : channels_(0), frames_(0), data_(0), is_solo_(false) { }
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
    : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data_, s);
  }
  SampleData(SampleData && other) noexcept
    : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_) {
  }
  ~SampleData() {
    free(data_);
  }
  SampleData & operator=(const SampleData & other) noexcept {
    if (&other != this) {
      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      
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
      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * getChannelData(int channel) { return data_ + channel * size(); }
  const float * getChannelData(int channel) const { return data_ + channel * size(); }  

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
    int old_size = size();
    resize(size() + other.size());
    assign(other, old_size);
  }
  
  void assign(const SampleData & other, int position) {
    if (other.empty() || position >= size()) return;
    assert(channels_ == other.channels_);
    assert(position >= 0);
    
    if (channels_ == other.channels_) {
      int n = other.size() < size() ? other.size() : size();
      if (position + n > size()) position = size() - n;
      
      for (int j = 0; j < channels_; j++) {
	auto other_channel_data = other.getChannelData(j);
	auto channel_data = getChannelData(j);
	for (int i = 0; i < n; i++) {
	  channel_data[i + position] = other_channel_data[i];
	}
      }
    }
  }

  void mix(const SampleData & other, float volume) {    
    int n = size() < other.size() ? size() : other.size();

    if (channels_ == other.channels_) {
      for (int i = 0; i < channels_ * n; i++) {
	data_[i] += volume * other.data_[i];
      }
    } else if (other.channels_ == 1) {
      auto left = data_, right = data_ + size();
      
      for (int i = 0; i < n; i++) {
	left[i] = right[i] = other.data_[i];
      }
    } else {
      assert(0);
    }
  }

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

  bool isSolo() const { return is_solo_; }
  
private:
  static inline size_t getAlignedSize(int frames) { return (static_cast<size_t>(frames) * sizeof(float) + 15ull) & ~15ull; }
  
  short channels_;
  int frames_;
  float * data_;
  bool is_solo_;

  // ChannelData
};

#endif

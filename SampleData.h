#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include "ChannelConfiguration.h"

#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>

class SampleData final {
 public:
  explicit SampleData() : channels_(0), frames_(0), data_(0), is_solo_(false) { }
  explicit SampleData(short channels, int frames, bool is_solo = false) : channels_(channels), frames_(frames), is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  explicit SampleData(ChannelConfiguration config, int frames, bool is_solo = false)
    : channels_(config.numberOfChannels()),
    frames_(frames),
    is_solo_(is_solo) {
    data_ = (float *)aligned_alloc(16, getAlignedSize(channels_ * frames_));
  }
  SampleData(const SampleData & other) : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_) {
    auto s = getAlignedSize(channels_ * frames_);
    data_ = (float *)aligned_alloc(16, s);
    memcpy(data_, other.data(), s);
  }
  SampleData(SampleData && other) noexcept : channels_(other.channels_), frames_(other.frames_), data_(std::exchange(other.data_, nullptr)), is_solo_(other.is_solo_) {
  }
  ~SampleData() {
    free(data_);
  }
  SampleData & operator=(const SampleData & other) {
    if (&other != this) {
      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      
      auto s = getAlignedSize(channels_ * frames_);
      data_ = (float *)aligned_alloc(16, s);
      
      memcpy(data_, other.data(), s);
    }
    return *this;
  }
  SampleData & operator=(SampleData && other) {
    if (&other != this) {
      free(data_);
      
      channels_ = other.channels_;
      frames_ = other.frames_;
      is_solo_ = other.is_solo_;
      data_ = std::exchange(other.data_, nullptr);
    }
    return *this;
  }

  float * data() { return data_; }
  const float * data() const { return data_; }

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
  
  void append(const SampleData & other) {
    if (other.empty()) return;
    if (empty()) {
      channels_ = other.channels_;
    } else {
      assert(channels_ == other.channels_);
    }
    auto s = getAlignedSize(channels_ * (size() + other.size()));
    auto new_data = (float *)aligned_alloc(16, s);
    if (frames_) memcpy(new_data, data_, static_cast<size_t>(channels_ * frames_) * sizeof(float));
    free(data_);
    data_ = new_data;
    memcpy(data_ + channels_ * frames_, other.data(), static_cast<size_t>(channels_ * other.size()) * sizeof(float));
    frames_ += other.size();
  }

  void mix(const SampleData & other, float volume) {
    assert(channels_ == other.channels_);
    
    int n = size() < other.size() ? size() : other.size();
    for (int i = 0; i < channels_ * n; i++) {
      data_[i] += volume * other.data_[i];
    }
  }

  void shortenToPowerofTwo() {
    int new_size = 1;
    for ( ; new_size * 2 <= frames_; new_size *= 2) { }
    frames_ = new_size;
  }

  std::pair<float, float> calculateLoudness() const {
    if (channels_ == 1) {
      float sum_squares = 0;
      for (int i = 0; i < frames_; i++) {
	sum_squares += data_[i] * data_[i];
      }
      return std::pair(sqrtf(sum_squares), 0.0f);
    } else {
      float sum_squares_left = 0, sum_squares_right = 0;
      for (int i = 0; i < frames_; i++) {
	sum_squares_left += data_[2 * i + 0] * data_[2 * i + 0];
	sum_squares_right += data_[2 * i + 1] * data_[2 * i + 1];
      }
      return std::pair(sqrtf(sum_squares_left), sqrtf(sum_squares_right));
    }
  }

  bool isSolo() const { return is_solo_; }
  
private:
  static inline size_t getAlignedSize(int samples) { return (static_cast<size_t>(samples) * sizeof(float) + 15ull) & ~15ull; }
  
  short channels_;
  int frames_;
  float * data_;
  bool is_solo_;

  // ChannelData
};

#endif

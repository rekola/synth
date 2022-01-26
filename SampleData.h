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
  explicit SampleData(size_t channels, size_t frames, bool is_solo = false) : channels_(channels), frames_(frames), is_solo_(is_solo) {
    auto s = alignSize(channels_ * frames_ * sizeof(float));
    data_ = (float *)aligned_alloc(16, s);
    memset(data_, 0, s);
  }
  explicit SampleData(ChannelConfiguration config, size_t frames, bool is_solo = false)
    : channels_(config == ChannelConfiguration::MONO ? 1 : 2),
    frames_(frames),
    is_solo_(is_solo) {
    auto s = alignSize(channels_ * frames_ * sizeof(float));
    data_ = (float *)aligned_alloc(16, s);
    memset(data_, 0, s);    
  }
  SampleData(const SampleData & other) : channels_(other.channels_), frames_(other.frames_), is_solo_(other.is_solo_) {
    auto s = alignSize(channels_ * frames_ * sizeof(float));
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
      
      size_t s = alignSize(channels_ * frames_ * sizeof(float));
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

  void clear() {
    free(data_);
    data_ = 0;
    frames_ = 0;
  }
  
  size_t getChannels() const { return channels_; }
  size_t size() const { return frames_; }
  bool empty() const { return channels_ == 0 || frames_ == 0; }
  
  void append(const SampleData & other) {
    if (other.empty()) return;
    if (empty()) {
      channels_ = other.channels_;
    } else {
      assert(channels_ == other.channels_);
    }
    size_t s = (channels_ * (size() + other.size()) * sizeof(float) + 15) & ~15;    
    float * new_data = (float *)aligned_alloc(16, s);
    if (frames_) memcpy(new_data, data_, channels_ * frames_ * sizeof(float));
    free(data_);
    data_ = new_data;
    memcpy(data_ + channels_ * frames_, other.data(), channels_ * other.size() * sizeof(float));
    frames_ += other.size();
  }

  void mix(const SampleData & other, size_t offset = 0) {
    assert(channels_ == other.channels_);
    
    size_t n = other.size();
    if (offset + n > size()) n = size() - offset;
    for (size_t i = 0; i < channels_ * n; i++) {
      data_[offset * channels_ + i] += other.data_[i];
    }
  }

  void mix(const SampleData & other, float volume = 1.0f) {
    assert(channels_ == other.channels_);

    size_t n = size() < other.size() ? size() : other.size();
    for (size_t i = 0; i < channels_ * n; i++) {
      data_[i] += volume * other.data_[i];
    }
  }

  void shortenToPowerofTwo() {
    size_t new_size = 1;
    for ( ; new_size * 2 <= frames_; new_size *= 2) { }
    frames_ = new_size;
  }

  std::pair<float, float> calculateLoudness() const {
    if (channels_ == 1) {
      float sum_squares = 0;
      for (size_t i = 0; i < frames_; i++) {
	sum_squares += data_[i] * data_[i];
      }
      return std::pair(sqrtf(sum_squares), 0.0f);
    } else {
      float sum_squares_left = 0, sum_squares_right = 0;
      for (size_t i = 0; i < frames_; i++) {
	sum_squares_left += data_[2 * i + 0] * data_[2 * i + 0];
	sum_squares_right += data_[2 * i + 1] * data_[2 * i + 1];
      }
      return std::pair(sqrtf(sum_squares_left), sqrtf(sum_squares_right));
    }
  }

  bool isSolo() const { return is_solo_; }
  
private:
  static inline size_t alignSize(size_t size) { return (size + 15) & ~15; }
  
  size_t channels_, frames_;
  float * data_;
  bool is_solo_;
};

#endif

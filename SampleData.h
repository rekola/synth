#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include <cstring>
#include <cmath>
#include <cassert>
#include <memory>

class SampleData final {
 public:
  explicit SampleData() : channels(0), frames(0), _data(0), is_solo(false) { }
  explicit SampleData(size_t _channels, size_t _frames, bool _is_solo = false) : channels(_channels), frames(_frames), is_solo(_is_solo) {
    size_t s = (channels * frames * sizeof(float) + 15) & ~15;    
    _data = (float *)aligned_alloc(16, s);
    memset(_data, 0, s);
  }
  SampleData(const SampleData & other) : channels(other.channels), frames(other.frames), is_solo(other.is_solo) {
    size_t s = (channels * frames * sizeof(float) + 15) & ~15;    
    _data = (float *)aligned_alloc(16, s);
    memcpy(_data, other.data(), s);
  }
  SampleData(SampleData && other) noexcept : channels(other.channels), frames(other.frames), _data(std::exchange(other._data, nullptr)), is_solo(other.is_solo) {
  }
  ~SampleData() {
    free(_data);
  }
  SampleData & operator=(const SampleData & other) {
    if (&other != this) {
      channels = other.channels;
      frames = other.frames;
      is_solo = other.is_solo;
      
      size_t s = (channels * frames * sizeof(float) + 15) & ~15;    
      _data = (float *)aligned_alloc(16, s);
      
      memcpy(_data, other.data(), s);
    }
    return *this;
  }
  SampleData & operator=(SampleData && other) {
    if (&other != this) {
      free(_data);
      
      channels = other.channels;
      frames = other.frames;
      is_solo = other.is_solo;
      _data = std::exchange(other._data, nullptr);
    }
    return *this;
  }

  float * data() { return _data; }
  const float * data() const { return _data; }

  void clear() {
    free(_data);
    _data = 0;
    frames = 0;
  }
  
  size_t getChannels() const { return channels; }
  size_t size() const { return frames; }
  bool empty() const { return channels == 0 || frames == 0; }
  
  void append(const SampleData & other) {
    if (other.empty()) return;
    if (empty()) {
      channels = other.channels;
    } else {
      assert(channels == other.channels);
    }
    size_t s = (channels * (size() + other.size()) * sizeof(float) + 15) & ~15;    
    float * new_data = (float *)aligned_alloc(16, s);
    if (frames) memcpy(new_data, _data, channels * frames * sizeof(float));
    free(_data);
    _data = new_data;
    memcpy(_data + channels * frames, other.data(), channels * other.size() * sizeof(float));
    frames += other.size();
  }

  void mix(const SampleData & other, size_t offset = 0) {
    assert(channels == other.channels);
    
    size_t n = other.size();
    if (offset + n > size()) n = size() - offset;
    for (size_t i = 0; i < channels * n; i++) {
      _data[offset * channels + i] += other._data[i];
    }
  }

  void mix(const SampleData & other, float volume = 1.0f) {
    assert(channels == other.channels);

    size_t n = size() < other.size() ? size() : other.size();
    for (size_t i = 0; i < channels * n; i++) {
      _data[i] += volume * other._data[i];
    }
  }

  void shortenToPowerofTwo() {
    size_t new_size = 1;
    for ( ; new_size * 2 <= frames; new_size *= 2) { }
    frames = new_size;
  }

  std::pair<float, float> calculateLoudness() const {
    if (channels == 1) {
      float sum_squares = 0;
      for (size_t i = 0; i < frames; i++) {
	sum_squares += _data[i] * _data[i];
      }
      return std::pair(sqrtf(sum_squares), 0.0f);
    } else {
      float sum_squares_left = 0, sum_squares_right = 0;
      for (size_t i = 0; i < frames; i++) {
	sum_squares_left += _data[2 * i + 0] * _data[2 * i + 0];
	sum_squares_right += _data[2 * i + 1] * _data[2 * i + 1];
      }
      return std::pair(sqrtf(sum_squares_left), sqrtf(sum_squares_right));
    }
  }

  bool isSolo() const { return is_solo; }
  
private:
  size_t channels, frames;
  float * _data;
  bool is_solo;
};

#endif

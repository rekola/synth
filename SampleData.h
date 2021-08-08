#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include <cstring>
#include <cmath>
#include <cassert>

class SampleData {
 public:
  explicit SampleData() : channels(0), frames(0), _data(0), is_solo(false) { }
  explicit SampleData(size_t _channels, size_t _frames, bool _is_solo = false) : channels(_channels), frames(_frames), is_solo(_is_solo) {
    _data = new float[channels * frames];
    memset(_data, 0, channels * frames * sizeof(float));
  }

  SampleData(const SampleData & other) : channels(other.channels), frames(other.frames) {
    _data = new float[channels * frames];
    memcpy(_data, other.data(), channels * frames * sizeof(float));
  }
  const SampleData & operator=(const SampleData & other) {
    delete[] _data;

    channels = other.channels;
    frames = other.frames;
    
    _data = new float[channels * frames];
    memcpy(_data, other.data(), channels * frames * sizeof(float));

    return *this;
  }
  
  ~SampleData() {
    delete[] _data;
  }

  float * data() { return _data; }
  const float * data() const { return _data; }

  void clear() {
    delete[] _data;
    _data = 0;
    frames = 0;
  }
  size_t getChannels() const { return channels; }
  size_t size() const { return frames; }
  bool empty() const { return frames == 0; }
  
  void append(const SampleData & other) {
    if (other.empty()) return;
    if (empty()) {
      channels = other.channels;
    } else {
      assert(channels == other.channels);
    }
    float * new_data = new float[2 * (size() + other.size())];
    if (frames) memcpy(new_data, _data, channels * frames * sizeof(float));
    delete[] _data;
    _data = new_data;
    memcpy(_data + channels * frames, other.data(), channels * other.size() * sizeof(float));
    frames += other.size();
  }

  void mix(const SampleData & other, size_t offset = 0) {
    size_t n = other.size();
    if (offset + n > size()) n = size() - offset;
    for (size_t i = 0; i < n; i++) {
      _data[offset + i] += other._data[i];
    }
  }

  void mix(const SampleData & other, float volume = 1.0f) {
    size_t n = size() < other.size() ? size() : other.size();
    for (size_t i = 0; i < n; i++) {
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

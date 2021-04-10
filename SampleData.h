#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

#include <cstring>

class SampleData {
 public:
  SampleData() : frames(0), _data(0) { }
  SampleData(size_t _frames) : frames(_frames) {
    _data = new float[2 * frames];
    memset(_data, 0, 2 * frames * sizeof(float));
  }

  SampleData(const SampleData & other) {
    frames = other.size();
    _data = new float[2 * frames];
    memcpy(_data, other.data(), 2 * frames * sizeof(float));
  }
  const SampleData & operator=(const SampleData & other) = delete;
  
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
  size_t size() const { return frames; }

  void append(const SampleData & other) {
    if (!other.size()) return;
    
    float * new_data = new float[2 * (size() + other.size())];
    if (frames) memcpy(new_data, _data, 2 * frames * sizeof(float));
    delete[] _data;
    _data = new_data;
    memcpy(_data + 2 * frames, other.data(), 2 * other.size() * sizeof(float));
    frames += other.size();
  }
    
private:
  size_t frames;
  float * _data;
};

#endif

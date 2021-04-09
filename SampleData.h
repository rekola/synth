#ifndef _SAMPLEDATA_H_
#define _SAMPLEDATA_H_

class SampleData {
 public:
 SampleData(size_t _frames) : frames(_frames) {
   _data = new float[2 * frames];
  }
  ~SampleData() {
    delete[] _data;
  }

  float * data() { return _data; }
  const float * data() const { return _data; }

  size_t size() const { return frames; }
    
 private:
  size_t frames;
  float * _data;
};

#endif

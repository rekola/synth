#ifndef _FFT_H_
#define _FFT_H_

#include <vector>

class SampleData;

class FFT {
public:
  static std::vector<float> perform(SampleData & input, size_t channel, size_t num_bins);
};

#endif

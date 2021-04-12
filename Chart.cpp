#include "Chart.h"

#include "FFT.h"
#include "SampleData.h"

void
Chart::displayFFT(const SampleData & data, size_t channel) {
  auto [ rows, columns ] = getDim();
  if (columns > 0) {
    auto fft = FFT::perform(data, channel, 2 * columns);
    for (size_t i = 0; i < fft.size(); i++) {
      setSample(i, fft[i]);
    }
  }
}

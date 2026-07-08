#include "Chart.h"

#include "SampleData.h"

#include <fftw3.h>
#include <vector>
#include <cmath>

using namespace std;

#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))

static fftw_plan plan;
static size_t plan_for_size = 0;
static double * signal;
static fftw_complex * result;

void
Chart::displayFFT(const SampleData & data) {
  auto [ rows, columns ] = getDim();
  if (columns > 0) {
    int frames = 1;
    for ( ; frames * 2 <= data.size(); frames *= 2) { }

    if (frames != plan_for_size) {
      plan_for_size = frames;
      signal = (double*)fftw_malloc(sizeof(double) * frames);
      result = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * frames);
      plan = fftw_plan_dft_r2c_1d(frames, signal, result, FFTW_ESTIMATE);
    }
    
    auto left_buffer = data.getChannelData(0), right_buffer = data.getChannelData(1);
    for (int i = 0; i < frames; i++) {
      signal[i] = left_buffer[i] + right_buffer[i];
    }
    
    fftw_execute(plan);

    size_t num_bins = 2 * columns;
    size_t actual_data_size = frames / 2;
    float start_value = log(40), end_value = log(40 + actual_data_size);
    float bin_size = (end_value - start_value) / num_bins;
    
    vector<float> bins;
    for (int i = 0; i < num_bins; i++) bins.push_back(0);

    for (size_t i = 0; i < actual_data_size; i++) {
      // double mag = sqrt(result[i][0] * result[i][0] + result[i][1] * result[i][1]);
      double mag = Decibels(result[i][0], result[i][1]);
      size_t i2 = (size_t)((log(40 + i) - start_value) / bin_size);
      if (mag > bins[i2]) bins[i2] = mag;
      // bins[i2] += mag;
    }
    // fftw_destroy_plan(plan);
    // fftw_free(signal);
    // fftw_free(result);
    
    for (size_t i = 0; i < bins.size(); i++) {
      setSample(i, bins[bins.size() - 1 - i]);
    }
  }
}

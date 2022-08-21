#ifndef _FFT_H_
#define _FFT_H_

#include <fftw3.h>

#include <vector>
#include <cmath>

#define mag_sqrd(re,im) (re*re+im*im)
#define Decibels(re,im) ((re == 0 && im == 0) ? (0) : 10.0 * log10(double(mag_sqrd(re,im))))

class FFT {
 public:
  FFT() { }
  ~FFT() {
    if (size_) {
      fftw_destroy_plan(plan_);
      fftw_free(signal_);
      fftw_free(result_);
    }
  }

  void setSize(int size) {
    if (size_) {
      fftw_destroy_plan(plan_);
      fftw_free(signal_);
      fftw_free(result_);
    }

    size_ = size;
    
    signal_ = (double*)fftw_malloc(sizeof(double) * size_);
    result_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * size_);
    plan_ = fftw_plan_dft_r2c_1d(size_, signal_, result_, FFTW_ESTIMATE);
  }
  void reset() { current_pos_ = 0; }

  bool addData(const SampleData & data) {
    auto left_buffer = data.getChannelData(0), right_buffer = data.getChannelData(1);
    for (int i = 0; i < data.size() && current_pos_ < size_; i++) {
      signal_[current_pos_++] = left_buffer[i] + right_buffer[i];
    }
    return current_pos_ == size_;
  }

  std::vector<float> calculateFFT() {      
    fftw_execute(plan_);
    
    auto actual_data_size = size_ / 2;
    
    std::vector<float> v;
    for (int i = 0; i < actual_data_size; i++) {
      // double mag = sqrt(result[i][0] * result[i][0] + result[i][1] * result[i][1]);
      double mag = Decibels(result_[i][0], result_[i][1]);
      v.push_back(mag);
    }

    return v;
  }
  
 private:
  int size_ = 0;
  int current_pos_ = 0;
  
  fftw_plan plan_;
  double * signal_;
  fftw_complex * result_;
};

#endif

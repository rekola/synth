#ifndef _SPECTRUMANALYZER_H_
#define _SPECTRUMANALYZER_H_

#include "RealFFT.h"
#include "../audio/AudioBuffer.h"

#include <cmath>
#include <memory>
#include <vector>

// Windowless ring-buffer accumulator + magnitude-dB conversion feeding the
// live UI spectrum chart (Player.cpp). This is domain-specific shaping
// (raw-sample accumulation threshold, dB conversion) that used to live
// directly inside dsp/FFT.h's own FFT class; it now sits here, on top of
// the shared dsp/RealFFT.h wrapper, per the FFTW -> PocketFFT migration
// plan's Phase 1 (plans/magical-wondering-engelbart.md) - the raw
// transform itself shouldn't own this shaping, since it's specific to this
// one visualization, not something MagLS or a future DirAC analyzer needs.
class SpectrumAnalyzer {
 public:
  SpectrumAnalyzer() = default;

  void setSize(int size) {
    size_ = size;
    signal_.assign(static_cast<size_t>(size_), 0.0f);
    current_pos_ = 0;
    newdata_size_ = 0;
    fft_ = std::make_unique<RealFFT<float>>(static_cast<size_t>(size_));
  }

  bool addData(const AudioBuffer & data) {
    if (current_pos_ == size_) {
      for (int i = data.size(); i < size_; i++) {
        signal_[static_cast<size_t>(i - data.size())] = signal_[static_cast<size_t>(i)];
      }
      current_pos_ -= data.size();
    }
    auto left_buffer = data.getChannelData(0), right_buffer = data.getChannelData(1);
    for (int i = 0; i < data.size() && current_pos_ < size_; i++) {
      signal_[static_cast<size_t>(current_pos_++)] = left_buffer[i] + right_buffer[i];
    }

    newdata_size_ += data.size();

    return current_pos_ == size_ && 2 * newdata_size_ > size_;
  }

  void reset() { newdata_size_ = 0; }

  std::vector<float> calculateFFT() {
    auto & spectrum = fft_->forward(signal_);

    auto actual_data_size = size_ / 2;

    std::vector<float> v;
    v.reserve(static_cast<size_t>(actual_data_size));
    for (int i = 0; i < actual_data_size; i++) {
      auto re = spectrum[static_cast<size_t>(i)].real();
      auto im = spectrum[static_cast<size_t>(i)].imag();
      float mag = (re == 0.0f && im == 0.0f) ? 0.0f : 10.0f * log10f(re * re + im * im);
      v.push_back(mag);
    }

    return v;
  }

 private:
  int size_ = 0;
  int current_pos_ = 0;
  int newdata_size_ = 0;

  std::vector<float> signal_;
  std::unique_ptr<RealFFT<float>> fft_;
};

#endif

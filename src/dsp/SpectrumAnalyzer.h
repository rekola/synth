#ifndef _SPECTRUMANALYZER_H_
#define _SPECTRUMANALYZER_H_

#include "RealFFT.h"
#include "../audio/AudioBuffer.h"

#include <algorithm>
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

  // A non-positive size leaves the analyzer inert (addData() never fires).
  void setSize(int size, int sample_rate) {
    size_ = std::max(size, 0);
    sample_rate_ = sample_rate;
    signal_.assign(static_cast<size_t>(size_), 0.0f);
    current_pos_ = 0;
    newdata_size_ = 0;
    window_.assign(static_cast<size_t>(size_), 1.0f);
    window_sum_ = 0.0f;
    for (int i = 0; i < size_; i++) {
      // Periodic Hann: tames the leakage a rectangular block would smear
      // across neighbouring bins.
      window_[static_cast<size_t>(i)] = 0.5f - 0.5f * cosf(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(size_));
      window_sum_ += window_[static_cast<size_t>(i)];
    }
    fft_ = size_ > 0 ? std::make_unique<RealFFT<float>>(static_cast<size_t>(size_)) : nullptr;
  }

  // Width of one calculateFFT() bin in Hz.
  float binHz() const { return size_ > 0 ? static_cast<float>(sample_rate_) / static_cast<float>(size_) : 0.0f; }

  bool addData(const AudioBuffer & data) {
    if (size_ <= 0) return false;
    if (current_pos_ == size_) {
      for (int i = data.size(); i < size_; i++) {
        signal_[static_cast<size_t>(i - data.size())] = signal_[static_cast<size_t>(i)];
      }
      current_pos_ -= data.size();
    }
    auto left_buffer = data.getChannelData(0), right_buffer = data.getChannelData(1);
    for (int i = 0; i < data.size() && current_pos_ < size_; i++) {
      signal_[static_cast<size_t>(current_pos_++)] = 0.5f * (left_buffer[i] + right_buffer[i]);
    }

    newdata_size_ += data.size();

    return current_pos_ == size_ && 2 * newdata_size_ > size_;
  }

  void reset() { newdata_size_ = 0; }

  // Bins DC..Nyquist-ish in dBFS: a full-scale sine reads 0 dB, silence
  // the -100 dB floor.
  std::vector<float> calculateFFT() {
    if (!fft_) return {};
    windowed_.resize(signal_.size());
    for (size_t i = 0; i < signal_.size(); i++) windowed_[i] = signal_[i] * window_[i];
    auto & spectrum = fft_->forward(windowed_);

    auto actual_data_size = size_ / 2;
    float scale = 2.0f / window_sum_;

    std::vector<float> v;
    v.reserve(static_cast<size_t>(actual_data_size));
    for (int i = 0; i < actual_data_size; i++) {
      float amplitude = std::abs(spectrum[static_cast<size_t>(i)]) * scale;
      v.push_back(20.0f * log10f(std::max(amplitude, 1e-5f)));
    }

    return v;
  }

 private:
  int size_ = 0;
  int sample_rate_ = 0;
  int current_pos_ = 0;
  int newdata_size_ = 0;
  float window_sum_ = 0.0f;

  std::vector<float> signal_, windowed_, window_;
  std::unique_ptr<RealFFT<float>> fft_;
};

#endif

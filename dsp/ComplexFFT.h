#ifndef _COMPLEXFFT_H_
#define _COMPLEXFFT_H_

#include <fftw3.h>

#include <complex>
#include <vector>

// Real-signal <-> frequency-domain FFT pair for offline (non-realtime)
// precomputation - e.g. MagLS's per-bin filter solve (AmbisonicMagLSDecoder).
// Unlike dsp/FFT.h (real-to-complex-forward-only, magnitude/dB output for
// the live spectrum analyzer), this exposes the raw complex spectrum
// (forward()) and can invert a solved complex spectrum back to a real
// time-domain signal (inverse()) - both directions a solve-in-frequency-
// domain-then-convert-back pipeline needs. Both the measured HRIRs going in
// and the synthesized filters coming out are real time-domain signals, so
// r2c/c2r (not a full complex-to-complex transform) is the right, more
// efficient FFTW idiom here - only size()/2+1 complex bins (DC..Nyquist)
// are ever computed or needed; Hermitian symmetry for the rest is implicit.
class ComplexFFT {
 public:
  explicit ComplexFFT(int size) : size_(size) {
    time_ = (double *)fftw_malloc(sizeof(double) * static_cast<size_t>(size_));
    freq_ = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * static_cast<size_t>(binCount()));
    forward_plan_ = fftw_plan_dft_r2c_1d(size_, time_, freq_, FFTW_ESTIMATE);
    inverse_plan_ = fftw_plan_dft_c2r_1d(size_, freq_, time_, FFTW_ESTIMATE);
  }
  ~ComplexFFT() {
    fftw_destroy_plan(forward_plan_);
    fftw_destroy_plan(inverse_plan_);
    fftw_free(time_);
    fftw_free(freq_);
  }
  ComplexFFT(const ComplexFFT &) = delete;
  ComplexFFT & operator=(const ComplexFFT &) = delete;

  int size() const { return size_; }
  int binCount() const { return size_ / 2 + 1; }

  // `input` is zero-padded/truncated to size() samples. Returned spectrum
  // has binCount() complex bins, DC..Nyquist inclusive.
  std::vector<std::complex<float>> forward(const std::vector<float> & input) {
    for (int i = 0; i < size_; i++) {
      time_[i] = i < static_cast<int>(input.size()) ? static_cast<double>(input[static_cast<size_t>(i)]) : 0.0;
    }
    fftw_execute(forward_plan_);
    std::vector<std::complex<float>> result(static_cast<size_t>(binCount()));
    for (int i = 0; i < binCount(); i++) {
      result[static_cast<size_t>(i)] = { static_cast<float>(freq_[i][0]), static_cast<float>(freq_[i][1]) };
    }
    return result;
  }

  // `spectrum` must have binCount() complex bins (DC..Nyquist) - missing
  // bins are treated as zero. Returns size() real time-domain samples.
  // FFTW's c2r plan does not normalize its own output - divided by size()
  // here so forward()+inverse() round-trips without a residual gain.
  std::vector<float> inverse(const std::vector<std::complex<float>> & spectrum) {
    for (int i = 0; i < binCount(); i++) {
      bool has = i < static_cast<int>(spectrum.size());
      freq_[i][0] = has ? static_cast<double>(spectrum[static_cast<size_t>(i)].real()) : 0.0;
      freq_[i][1] = has ? static_cast<double>(spectrum[static_cast<size_t>(i)].imag()) : 0.0;
    }
    fftw_execute(inverse_plan_);
    std::vector<float> result(static_cast<size_t>(size_));
    for (int i = 0; i < size_; i++) result[static_cast<size_t>(i)] = static_cast<float>(time_[i] / size_);
    return result;
  }

 private:
  int size_;
  double * time_;
  fftw_complex * freq_;
  fftw_plan forward_plan_, inverse_plan_;
};

#endif

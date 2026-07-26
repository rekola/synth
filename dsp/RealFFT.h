#ifndef _REALFFT_H_
#define _REALFFT_H_

// PocketFFT's own internal plan cache (see the class doc comment below)
// and worker-thread pool are both opt-in via these defines, which must be
// set before the header is included anywhere - defining them here, next
// to the only #include of the header in this codebase, keeps that one
// requirement from becoming a footgun for whoever includes this file.
#ifndef POCKETFFT_CACHE_SIZE
#define POCKETFFT_CACHE_SIZE 8
#endif
#ifndef POCKETFFT_NO_MULTITHREADING
#define POCKETFFT_NO_MULTITHREADING
#endif

#include "../third_party/pocketfft/pocketfft_hdronly.h"

#include <complex>
#include <type_traits>
#include <vector>

// Real-signal <-> frequency-domain FFT wrapper, backed by PocketFFT
// (third_party/pocketfft/, BSD-3-Clause) - see plans/magical-wondering-
// engelbart.md for the full FFTW -> PocketFFT migration plan. Single
// shared interface for every current/planned call site (the live
// spectrum analyzer via dsp/SpectrumAnalyzer.h, MagLS's precomputation in
// AmbisonicMagLSDecoder, and the future DirAC STFT).
//
// Fixed size at construction; the internal time-/frequency-domain buffers
// are allocated once and never resized, so forward()/inverse() never
// allocate.
//
// PocketFFT's plain r2c()/c2r() free functions have no persistent plan
// object of their own to hold onto as a member the way FFTW's fftw_plan
// worked - by default (POCKETFFT_CACHE_SIZE==0) every single call rebuilds
// its internal twiddle-factor tables completely from scratch, which is
// considerably more expensive per call than FFTW's plan-once/execute-many
// model, and is a real allocation on every call, not just a lock.
// Defining POCKETFFT_CACHE_SIZE to a small nonzero value (above) makes
// PocketFFT instead maintain its own small, mutex-guarded, process-wide
// LRU cache of these tables keyed by transform length - this engine only
// ever uses a handful of distinct sizes (~4410 for the spectrum analyzer,
// ~512 for MagLS, 1024 for the planned DirAC analyzer), so 8 cache slots
// comfortably covers all of them with headroom. This turns repeat calls at
// an already-seen size back into a "mutex lock + cache hit" cost - the
// case the migration plan's audio-thread risk assessment was actually
// written against - rather than a full replan every time, which is worse.
//
// Normalization: forward() is unnormalized, matching FFTW's own r2c
// convention exactly (bit-for-bit-equivalent scaling to this codebase's
// prior direct-FFTW wrappers) - PocketFFT takes an explicit scale factor
// (`fct`) per call instead of an implicit convention, so this is just
// `fct=1` for forward(). inverse() defaults to a 1/size() scale (matching
// this class's own prior FFTW-backed inverse(), which divided by size()
// after the transform) - pass an explicit scale for an unnormalized
// inverse if some future caller needs it.
//
// Output layout: binCount() == size()/2+1 complex bins, DC..Nyquist
// inclusive - this is both FFTW's r2c/c2r layout (this codebase has never
// used FFTW's separate packed-real "halfcomplex" format) and PocketFFT's
// own r2c/c2r layout, so no bin-layout conversion was needed when this
// class's backend swapped from FFTW to PocketFFT.
template <typename T>
class RealFFT {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "RealFFT only supports float or double");

 public:
  explicit RealFFT(size_t size)
    : size_(size), shape_{size}, stride_real_{static_cast<ptrdiff_t>(sizeof(T))},
      stride_complex_{static_cast<ptrdiff_t>(sizeof(std::complex<T>))},
      time_(size, T(0)), freq_(binCount(), std::complex<T>(0, 0)) {
  }

  size_t size() const { return size_; }
  size_t binCount() const { return size_ / 2 + 1; }

  // Zero-pads/truncates `input` to size(). Returns a reference to an
  // internal buffer, valid until the next forward()/inverse() call on
  // *this* instance - copy it if it must outlive that.
  const std::vector<std::complex<T>> & forward(const std::vector<T> & input) {
    size_t n = input.size() < size_ ? input.size() : size_;
    for (size_t i = 0; i < n; i++) time_[i] = input[i];
    for (size_t i = n; i < size_; i++) time_[i] = T(0);
    pocketfft::r2c(shape_, stride_real_, stride_complex_, size_t(0), pocketfft::FORWARD,
                   time_.data(), freq_.data(), T(1));
    return freq_;
  }

  // `spectrum` must have binCount() entries - missing trailing bins are
  // treated as zero. Normalizes by 1/size(). Returns a reference with the
  // same lifetime rule as forward().
  const std::vector<T> & inverse(const std::vector<std::complex<T>> & spectrum) {
    return inverse(spectrum, T(1) / static_cast<T>(size_));
  }

  // As above, but with an explicit output scale instead of the 1/size()
  // default.
  const std::vector<T> & inverse(const std::vector<std::complex<T>> & spectrum, T scale) {
    size_t bins = binCount();
    size_t n = spectrum.size() < bins ? spectrum.size() : bins;
    for (size_t i = 0; i < n; i++) freq_[i] = spectrum[i];
    for (size_t i = n; i < bins; i++) freq_[i] = std::complex<T>(0, 0);
    pocketfft::c2r(shape_, stride_complex_, stride_real_, size_t(0), pocketfft::BACKWARD,
                   freq_.data(), time_.data(), scale);
    return time_;
  }

 private:
  size_t size_;
  pocketfft::shape_t shape_;
  pocketfft::stride_t stride_real_, stride_complex_;
  std::vector<T> time_;
  std::vector<std::complex<T>> freq_;
};

#endif

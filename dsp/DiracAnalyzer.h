#ifndef _DIRACANALYZER_H_
#define _DIRACANALYZER_H_

#include "RealFFT.h"
#include "../AudioBuffer.h"

#include <array>
#include <vector>

// DirAC-style (Directional Audio Coding) directional-analysis estimator:
// consumes blocks of the shared ambisonic bus's W/Y/Z/X channels (ACN
// order - see AmbisonicEncoding.h) and, per one of 8 log-spaced frequency
// bands, estimates the arriving sound's direction (azimuth/elevation) and
// diffuseness (0 = a single coherent point source, 1 = fully diffuse/
// uncorrelated field) - see plans/dirac-heatmap-scope.md for the design
// this implements (particularly SS1/4/5/6 for the parameters/math below).
//
// Internally runs its own 50%-overlap, Hann-windowed STFT (N=1024,
// hop=512) - independent of dsp/SpectrumAnalyzer.h's single ~10Hz
// snapshot, which sums to mono and has no window (wrong shape for a
// continuous, phase-sensitive analysis like this one). Not tied to any
// particular thread - the caller (VisualizationThread) decides where this
// runs; this class only assumes process() is called with consecutive,
// non-overlapping blocks of the same audio stream.
class DiracAnalyzer {
 public:
  static constexpr int kFFTSize = 1024;
  static constexpr int kHopSize = 512;
  static constexpr int kNumBands = 8;
  // 72, not 36 (5 degrees/bin, not 10): TerminalHeatmapChart's fallback
  // renderer resamples this grid to a fixed sub-column resolution
  // (kHeatmapWidth*2 = 62, UI.cpp) regardless of terminal size, and 36
  // source bins upsampled to 62 destination columns hit a real,
  // confirmed asymmetric-rounding bug in that resampling (see
  // TerminalUI.cpp's axisRange()) - widening the source grid past the
  // display's own resolution avoids upsampling altogether, the more
  // robust fix (axisRange() still handles upsampling correctly too, but
  // not needing to is simpler and matches elevation's own comfortably-
  // downsampled situation, 18 bins against at most 15 sub-rows).
  static constexpr int kAzimuthBins = 72;   // 5 degrees/bin
  static constexpr int kElevationBins = 18; // 10 degrees/bin
  static constexpr int kGridSize = kAzimuthBins * kElevationBins;

  // Channel order within a per-channel magnitude array - ACN, matching
  // every other array/index in this codebase (see AmbisonicEncoding.h).
  enum Channel { kW = 0, kY = 1, kZ = 2, kX = 3 };

  struct BandResult {
    float azimuth = 0.0f;      // degrees, this engine's convention (0 = front, + = right)
    float elevation = 0.0f;    // degrees (+ = up)
    float diffuseness = 0.0f;  // [0,1] - see the class doc comment
    float energy = 0.0f;       // this band's smoothed E (SS5) - the splat weight before (1-diffuseness)/diffuseness scaling
    std::array<float, 4> magnitude {}; // smoothed |W|,|Y|,|Z|,|X| (ACN order) - kept for future UI disambiguation (SS3/8), not displayed yet
  };

  explicit DiracAnalyzer(int sample_rate);

  // Feeds one block's worth of samples. `block` may have anywhere from 0
  // to 4 regular channels present (ACN order W,Y,Z,X) - fewer than 4 is
  // treated as those trailing channels being silent, the same
  // nullptr-tolerant convention decodeToStereo() uses for a MONO
  // (W-only) bus. May trigger zero, one, or several internal analysis
  // frames depending on how much new data this call contributes.
  void process(const AudioBuffer & block);

  // Monotonically increments once per internal analysis frame (i.e. once
  // per kHopSize new samples consumed, ~86Hz) - callers that only want a
  // fraction of that rate (e.g. VisualizationThread's own render-throttle,
  // plans/dirac-heatmap-scope.md SS1) diff this against a saved value
  // rather than this class trying to guess an unrelated caller's own
  // desired output rate.
  int getAnalysisFrameCount() const { return analysis_frame_count_; }

  const BandResult & getBandResult(int band) const { return bands_[static_cast<size_t>(band)]; }

  // The smoothed, splatted directional-energy grid (SS6) - kAzimuthBins
  // columns x kElevationBins rows, row-major (cell = el_bin*kAzimuthBins +
  // az_bin). Does not include the per-band diffuse haze - callers add
  // getDiffuseEnergy(band) summed and spread uniformly, per SS6's
  // rendering formula.
  const std::array<float, kGridSize> & getGrid() const { return grid_; }
  float getDiffuseEnergy(int band) const { return diffuse_energy_[static_cast<size_t>(band)]; }

 private:
  struct BandFilterState {
    float ix = 0.0f, iy = 0.0f, iz = 0.0f;           // one-pole-smoothed intensity components
    float pow_w = 0.0f, pow_y = 0.0f, pow_z = 0.0f, pow_x = 0.0f; // one-pole-smoothed per-channel power
    int bin_lo = 0, bin_hi = 0;                       // [bin_lo, bin_hi) - this band's FFT bin range
  };

  void processFrame();

  int sample_rate_;
  std::array<float, kFFTSize> hann_;
  std::array<RealFFT<float>, 4> fft_ { RealFFT<float>(kFFTSize), RealFFT<float>(kFFTSize), RealFFT<float>(kFFTSize), RealFFT<float>(kFFTSize) };
  std::array<std::vector<float>, 4> channel_buffer_; // per-channel accumulation ring, trimmed by kHopSize after each frame
  std::vector<float> windowed_; // scratch, reused across channels/frames

  float ie_alpha_;                 // one-pole coefficient for the 100ms I/E smoothing (SS1)
  float grid_attack_alpha_, grid_release_alpha_; // asymmetric ballistics coefficients (SS1)

  std::array<BandFilterState, kNumBands> bands_state_;
  std::array<BandResult, kNumBands> bands_;
  std::array<float, kNumBands> diffuse_energy_ {};
  std::array<float, kGridSize> grid_ {};
  int analysis_frame_count_ = 0;
};

#endif

#include "AmbisonicMagLSDecoder.h"
#include "SofaFileResolver.h"
#include "dsp/RealFFT.h"

#include <mysofa.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <fmt/core.h>

using namespace std;

namespace {

// MagLS's transition frequency: phase-accurate (complex) solve below this,
// magnitude-only (with phase propagated from the previous bin) above it.
// Literature/reference-implementation default - see the project's own
// plan history for why 1.5kHz specifically (roughly where interaural
// phase stops being perceptually load-bearing for localization and a
// low-order SH basis can still reproduce the sound field over a
// head-sized region).
constexpr float kTransitionFrequencyHz = 1500.0f;

// One real (K x K) linear solve, Gauss-Jordan elimination - K is at most
// kAmbisonicChannelCount (16 at order 3), so a hand-rolled solve is simpler
// and less risky than pulling in a linear-algebra dependency for this one
// small, fixed-size problem. Solves A*x = b in place; A is destroyed.
// Returns false (leaves x untouched) if A is singular.
bool solveLinearSystem(vector<vector<double>> & a, vector<double> & x) {
  int n = static_cast<int>(a.size());
  vector<int> perm(static_cast<size_t>(n));
  for (int i = 0; i < n; i++) perm[static_cast<size_t>(i)] = i;

  for (int col = 0; col < n; col++) {
    int pivot = col;
    double best = fabs(a[static_cast<size_t>(col)][static_cast<size_t>(col)]);
    for (int row = col + 1; row < n; row++) {
      double v = fabs(a[static_cast<size_t>(row)][static_cast<size_t>(col)]);
      if (v > best) { best = v; pivot = row; }
    }
    if (best < 1e-12) return false;
    if (pivot != col) {
      swap(a[static_cast<size_t>(pivot)], a[static_cast<size_t>(col)]);
      swap(x[static_cast<size_t>(pivot)], x[static_cast<size_t>(col)]);
    }
    double inv_pivot = 1.0 / a[static_cast<size_t>(col)][static_cast<size_t>(col)];
    for (int row = 0; row < n; row++) {
      if (row == col) continue;
      double factor = a[static_cast<size_t>(row)][static_cast<size_t>(col)] * inv_pivot;
      if (factor == 0.0) continue;
      for (int c = col; c < n; c++) a[static_cast<size_t>(row)][static_cast<size_t>(c)] -= factor * a[static_cast<size_t>(col)][static_cast<size_t>(c)];
      x[static_cast<size_t>(row)] -= factor * x[static_cast<size_t>(col)];
    }
  }
  for (int col = 0; col < n; col++) x[static_cast<size_t>(col)] /= a[static_cast<size_t>(col)][static_cast<size_t>(col)];
  return true;
}

// Inverts a real, symmetric, positive-definite K x K matrix via K solves of
// A*x = e_i (one per identity column) - fine for K<=16, not meant to scale
// beyond that.
vector<vector<double>> invertMatrix(const vector<vector<double>> & m) {
  int n = static_cast<int>(m.size());
  vector<vector<double>> inv(static_cast<size_t>(n), vector<double>(static_cast<size_t>(n), 0.0));
  for (int col = 0; col < n; col++) {
    auto a = m;
    vector<double> x(static_cast<size_t>(n), 0.0);
    x[static_cast<size_t>(col)] = 1.0;
    if (!solveLinearSystem(a, x)) return {}; // singular - caller must check emptiness
    for (int row = 0; row < n; row++) inv[static_cast<size_t>(row)][static_cast<size_t>(col)] = x[static_cast<size_t>(row)];
  }
  return inv;
}

int nextPowerOfTwo(int n) {
  int p = 1;
  while (p < n) p *= 2;
  return p;
}

} // namespace

AmbisonicMagLSDecoder::AmbisonicMagLSDecoder(int ambisonic_channels, int outSampleRate)
  : Mixer(2, outSampleRate), ambisonic_channels_(ambisonic_channels), buffer_(static_cast<short>(ambisonic_channels), 0) {
  auto sofa_path = findDefaultSofaFile();
  if (sofa_path.empty()) return;

  int filterlength = 0, err = 0;
  auto * easy = mysofa_open(sofa_path.c_str(), static_cast<float>(outSampleRate), &filterlength, &err);
  if (!easy || err != 0 || filterlength <= 0) {
    if (easy) mysofa_close(easy);
    return;
  }

  fmt::print(stderr, "Using SOFA file {} for MagLS binaural decoding\n", sofa_path);

  // (a) Enumerate the FULL measurement grid (every one of the dataset's own
  // M directions, not a small virtual-speaker subset - the key
  // architectural departure from AmbisonicBinauralMixer) and fetch each
  // direction's own raw (uninterpolated) HRIR pair, already resampled to
  // this engine's output rate by mysofa_open() above. SourcePosition is
  // Cartesian (confirmed against the actual bundled SOFA file), same x/y/z
  // convention mysofa_getfilter_float already uses elsewhere in this
  // codebase.
  struct GridPoint {
    float azimuth, elevation;
    vector<float> left_ir, right_ir;
    int left_delay, right_delay;
  };
  unsigned int m_count = easy->hrtf->M;
  vector<GridPoint> grid;
  grid.reserve(m_count);
  int max_delay = 0;
  constexpr float kDeg2Rad = static_cast<float>(M_PI) / 180.0f;
  for (unsigned int i = 0; i < m_count; i++) {
    float x = easy->hrtf->SourcePosition.values[i * 3 + 0];
    float y = easy->hrtf->SourcePosition.values[i * 3 + 1];
    float z = easy->hrtf->SourcePosition.values[i * 3 + 2];
    float r = sqrtf(x * x + y * y + z * z);
    if (r < 1e-6f) continue; // degenerate position, skip

    GridPoint gp;
    gp.elevation = asinf(std::clamp(z / r, -1.0f, 1.0f)) / kDeg2Rad;
    float sofa_azimuth = atan2f(y, x);
    gp.azimuth = -sofa_azimuth / kDeg2Rad; // this engine: +az = right; SOFA: +az = left

    gp.left_ir.resize(static_cast<size_t>(filterlength));
    gp.right_ir.resize(static_cast<size_t>(filterlength));
    float delay_left = 0.0f, delay_right = 0.0f;
    mysofa_getfilter_float_nointerp(easy, x, y, z, gp.left_ir.data(), gp.right_ir.data(), &delay_left, &delay_right);
    gp.left_delay = max(0, static_cast<int>(lroundf(delay_left)));
    gp.right_delay = max(0, static_cast<int>(lroundf(delay_right)));
    max_delay = max({ max_delay, gp.left_delay, gp.right_delay });

    grid.push_back(std::move(gp));
  }
  mysofa_close(easy);

  int m = static_cast<int>(grid.size());
  int k = ambisonic_channels_;
  if (m < k) return; // not enough measurements to solve - leave not ready

  // FFT size: comfortably covers the (delay-shifted) filter with headroom
  // against circular-wrap artifacts from the frequency-domain processing
  // below - derived from what's actually in the dataset, not a hardcoded
  // assumption about filter length.
  int fft_size = nextPowerOfTwo(2 * (filterlength + max_delay));
  RealFFT<float> fft(static_cast<size_t>(fft_size));
  int num_bins = static_cast<int>(fft.binCount());

  // (c) Build the real SH matrix Y (m x k) on the same grid - reuses the
  // shared, order-agnostic, deliberately-unweighted computeAmbisonicGains()
  // directly; this is the only place this class needs any SH math at all,
  // and why it works unchanged at order 1/2/3.
  vector<vector<float>> y(static_cast<size_t>(m), vector<float>(static_cast<size_t>(k)));
  for (int i = 0; i < m; i++) {
    auto gains = computeAmbisonicGains(SphericalPosition{ grid[static_cast<size_t>(i)].azimuth, grid[static_cast<size_t>(i)].elevation, 1.0f });
    for (int c = 0; c < k; c++) y[static_cast<size_t>(i)][static_cast<size_t>(c)] = gains[static_cast<size_t>(c)];
  }

  // (c continued) Y^T Y (k x k, real, uniform per-direction weighting -
  // the dense/near-uniform grid this dataset provides doesn't need a
  // density-compensating weight to be well-conditioned) and its inverse,
  // computed once - this, not the per-bin solve itself, is the only matrix
  // inversion this class ever does.
  vector<vector<double>> yty(static_cast<size_t>(k), vector<double>(static_cast<size_t>(k), 0.0));
  for (int i = 0; i < m; i++) {
    for (int r = 0; r < k; r++) {
      for (int c = 0; c < k; c++) {
        yty[static_cast<size_t>(r)][static_cast<size_t>(c)] += static_cast<double>(y[static_cast<size_t>(i)][static_cast<size_t>(r)]) * y[static_cast<size_t>(i)][static_cast<size_t>(c)];
      }
    }
  }
  auto yty_inv = invertMatrix(yty);
  if (yty_inv.empty()) return; // singular - grid/order combination can't be solved

  // Y_pinv = (Y^T Y)^-1 Y^T (k x m, real) - the actual per-bin "solve" is
  // just this fixed matrix applied to whatever complex measurement vector
  // is being fit that bin, computed once and reused for every bin and both
  // ears (Y_pinv depends only on the grid and the order, not on frequency).
  vector<vector<float>> y_pinv(static_cast<size_t>(k), vector<float>(static_cast<size_t>(m)));
  for (int r = 0; r < k; r++) {
    for (int i = 0; i < m; i++) {
      double sum = 0.0;
      for (int c = 0; c < k; c++) sum += yty_inv[static_cast<size_t>(r)][static_cast<size_t>(c)] * y[static_cast<size_t>(i)][static_cast<size_t>(c)];
      y_pinv[static_cast<size_t>(r)][static_cast<size_t>(i)] = static_cast<float>(sum);
    }
  }

  // (b) FFT every grid direction's (delay-shifted) HRIR pair once, up
  // front - each ear's per-direction spectrum is reused across every bin
  // of the per-bin solve below.
  vector<vector<complex<float>>> h_left(static_cast<size_t>(m)), h_right(static_cast<size_t>(m));
  for (int i = 0; i < m; i++) {
    auto & gp = grid[static_cast<size_t>(i)];
    vector<float> shifted_left(static_cast<size_t>(fft_size), 0.0f), shifted_right(static_cast<size_t>(fft_size), 0.0f);
    for (size_t s = 0; s < gp.left_ir.size(); s++) {
      size_t d = static_cast<size_t>(gp.left_delay) + s;
      if (d < shifted_left.size()) shifted_left[d] = gp.left_ir[s];
    }
    for (size_t s = 0; s < gp.right_ir.size(); s++) {
      size_t d = static_cast<size_t>(gp.right_delay) + s;
      if (d < shifted_right.size()) shifted_right[d] = gp.right_ir[s];
    }
    h_left[static_cast<size_t>(i)] = fft.forward(shifted_left);
    h_right[static_cast<size_t>(i)] = fft.forward(shifted_right);
  }

  channel_filters_.resize(static_cast<size_t>(k));

  // (d) Per-bin solve, one ear at a time. See maglsSolveEar() below (kept
  // as a local lambda since it closes over y_pinv/y/grid/h_*).
  auto solveEar = [&](const vector<vector<complex<float>>> & h) {
    vector<vector<complex<float>>> spectrum(static_cast<size_t>(k), vector<complex<float>>(static_cast<size_t>(num_bins)));
    vector<complex<float>> prev_a(static_cast<size_t>(k), complex<float>(0.0f, 0.0f));

    for (int bin = 0; bin < num_bins; bin++) {
      float freq_hz = static_cast<float>(bin) * static_cast<float>(getOutSampleRate()) / static_cast<float>(fft_size);
      bool below_transition = freq_hz <= kTransitionFrequencyHz;

      // Effective measurement vector for this bin - the raw measured
      // complex value below the transition; above it, the true magnitude
      // with phase borrowed from what the *previous* bin's already-solved
      // filter would itself produce at each direction (not the raw
      // measured phase) - this recursive phase propagation is what avoids
      // the comb-filtering/discontinuity a naive magnitude-only fit would
      // introduce. Ported from the reference MagLS control flow (see the
      // project's plan history), not derived from scratch.
      vector<complex<float>> h_eff(static_cast<size_t>(m));
      for (int i = 0; i < m; i++) {
        complex<float> measured = h[static_cast<size_t>(i)][static_cast<size_t>(bin)];
        if (below_transition || bin == 0) {
          h_eff[static_cast<size_t>(i)] = measured;
          continue;
        }
        complex<float> predicted(0.0f, 0.0f);
        for (int c = 0; c < k; c++) predicted += y[static_cast<size_t>(i)][static_cast<size_t>(c)] * prev_a[static_cast<size_t>(c)];
        float predicted_mag = abs(predicted);
        float measured_mag = abs(measured);
        if (predicted_mag < 1e-12f) {
          h_eff[static_cast<size_t>(i)] = measured; // degenerate - fall back to raw phase
        } else {
          h_eff[static_cast<size_t>(i)] = predicted * (measured_mag / predicted_mag);
        }
      }

      // a[bin] = Y_pinv * h_eff - the entire "solve" for this bin, both
      // below and above the transition (only h_eff's construction differs
      // between the two cases above).
      vector<complex<float>> a(static_cast<size_t>(k), complex<float>(0.0f, 0.0f));
      for (int r = 0; r < k; r++) {
        complex<float> sum(0.0f, 0.0f);
        for (int i = 0; i < m; i++) sum += y_pinv[static_cast<size_t>(r)][static_cast<size_t>(i)] * h_eff[static_cast<size_t>(i)];
        a[static_cast<size_t>(r)] = sum;
      }

      // Diffuse-field covariance constraint (plan §5): rescale this bin's
      // solved filter so its predicted direction-averaged energy matches
      // the *true* measured direction-averaged energy at this bin, rather
      // than trusting the per-direction fit's aggregate alone - improves
      // timbre especially at low order, where SH truncation error (and so
      // the unconstrained fit's aggregate spectral-balance gap) is
      // largest. A single scalar per bin, applied after the solve, not a
      // joint/simultaneous constraint.
      double predicted_energy = 0.0, true_energy = 0.0;
      for (int i = 0; i < m; i++) {
        complex<float> predicted(0.0f, 0.0f);
        for (int c = 0; c < k; c++) predicted += y[static_cast<size_t>(i)][static_cast<size_t>(c)] * a[static_cast<size_t>(c)];
        predicted_energy += static_cast<double>(norm(predicted));
        true_energy += static_cast<double>(norm(h[static_cast<size_t>(i)][static_cast<size_t>(bin)]));
      }
      if (predicted_energy > 1e-20) {
        float scale = static_cast<float>(sqrt(true_energy / predicted_energy));
        for (auto & v : a) v *= scale;
      }

      for (int c = 0; c < k; c++) spectrum[static_cast<size_t>(c)][static_cast<size_t>(bin)] = a[static_cast<size_t>(c)];
      prev_a = a;
    }
    return spectrum;
  };

  auto spectrum_left = solveEar(h_left);
  auto spectrum_right = solveEar(h_right);

  // (e) Inverse-transform each channel's solved spectrum back to time
  // domain, circular-shift to make it causal (the raw IFFT output is
  // centered/non-causal - shifting by half the FFT size is the standard
  // fix), and taper both edges with a short window to control the ringing
  // the transition-frequency solve-strategy change and the finite FFT size
  // both introduce.
  auto toTimeDomain = [&](const vector<complex<float>> & spectrum) {
    auto time = fft.inverse(spectrum);
    vector<float> shifted(time.size());
    int half = fft_size / 2;
    for (int i = 0; i < fft_size; i++) shifted[static_cast<size_t>(i)] = time[static_cast<size_t>((i + half) % fft_size)];
    int taper = max(1, fft_size / 16);
    for (int i = 0; i < taper; i++) {
      float w = 0.5f - 0.5f * cosf(static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(taper));
      shifted[static_cast<size_t>(i)] *= w;
      shifted[static_cast<size_t>(fft_size - 1 - i)] *= w;
    }
    return shifted;
  };

  // Level neutrality (plan §9): normalize so the solved filter set's own
  // diffuse-field gain is unity, computed purely from the filters and grid
  // just solved - no external calibration constant, no historical
  // reference point (unlike AmbisonicBinauralMixer's gain_trim_, which
  // this mechanism deliberately doesn't share, since a fixed-filter-pair
  // architecture has no meaning for "speaker count" at all). This is what
  // makes an A/B comparison against the virtual-speaker decoder fair and
  // non-loudness-biased. Computed directly from the frequency-domain
  // spectra (mean over grid directions, mean over frequency bins, of the
  // squared magnitude of the encoded/decoded response) - NOT from the
  // time-domain filters (a plain sum of time samples only captures a
  // filter's DC/zero-frequency value, nowhere near its true broadband
  // energy).
  auto diffuseGain = [&](const vector<vector<complex<float>>> & spectrum) {
    double sum_sq = 0.0;
    for (auto & gp : grid) {
      auto gains = computeAmbisonicGains(SphericalPosition{ gp.azimuth, gp.elevation, 1.0f });
      for (int bin = 0; bin < num_bins; bin++) {
        complex<float> response(0.0f, 0.0f);
        for (int c = 0; c < k; c++) response += gains[static_cast<size_t>(c)] * spectrum[static_cast<size_t>(c)][static_cast<size_t>(bin)];
        sum_sq += static_cast<double>(norm(response));
      }
    }
    return sqrt(sum_sq / (static_cast<double>(grid.size()) * static_cast<double>(num_bins)));
  };
  float left_gain = static_cast<float>(diffuseGain(spectrum_left));
  float right_gain = static_cast<float>(diffuseGain(spectrum_right));
  if (left_gain > 1e-12f) {
    for (auto & spec : spectrum_left) for (auto & v : spec) v /= left_gain;
  }
  if (right_gain > 1e-12f) {
    for (auto & spec : spectrum_right) for (auto & v : spec) v /= right_gain;
  }

  for (int c = 0; c < k; c++) {
    channel_filters_[static_cast<size_t>(c)].left = toTimeDomain(spectrum_left[static_cast<size_t>(c)]);
    channel_filters_[static_cast<size_t>(c)].right = toTimeDomain(spectrum_right[static_cast<size_t>(c)]);
  }
  left_tail_.assign(static_cast<size_t>(fft_size), 0.0f);
  right_tail_.assign(static_cast<size_t>(fft_size), 0.0f);
  ready_ = true;
}

AmbisonicMagLSDecoder::~AmbisonicMagLSDecoder() { }

void
AmbisonicMagLSDecoder::reset() {
  buffer_.zero();
}

void
AmbisonicMagLSDecoder::accumulate(const SampleData & input) {
  if (buffer_.numberOfFrames() != input.numberOfFrames()) {
    buffer_ = SampleData(static_cast<short>(ambisonic_channels_), input.numberOfFrames());
    buffer_.zero();
  }
  buffer_.mixNamed(input);
}

SampleData
AmbisonicMagLSDecoder::encode() {
  int frames = buffer_.numberOfFrames();
  size_t tail_len = left_tail_.size();
  size_t acc_size = static_cast<size_t>(frames) + tail_len;

  left_acc_.resize(acc_size);
  right_acc_.resize(acc_size);
  for (size_t i = 0; i < tail_len; i++) {
    left_acc_[i] = left_tail_[i];
    right_acc_[i] = right_tail_[i];
  }
  fill(left_acc_.begin() + static_cast<long>(tail_len), left_acc_.end(), 0.0f);
  fill(right_acc_.begin() + static_cast<long>(tail_len), right_acc_.end(), 0.0f);

  int regular = min(static_cast<int>(buffer_.numberOfChannels()), ambisonic_channels_);
  // Each ambisonic channel convolves directly against its own precomputed
  // filter pair - no intermediate per-speaker mono signal at all, unlike
  // AmbisonicBinauralMixer's decode-then-convolve loop.
  for (int c = 0; c < regular; c++) {
    auto channel_data = buffer_.getChannelData(c);
    auto & left_ir = channel_filters_[static_cast<size_t>(c)].left;
    auto & right_ir = channel_filters_[static_cast<size_t>(c)].right;
    for (int i = 0; i < frames; i++) {
      float v = channel_data[i];
      if (v == 0.0f) continue;
      for (size_t t = 0; t < left_ir.size(); t++) left_acc_[static_cast<size_t>(i) + t] += v * left_ir[t];
      for (size_t t = 0; t < right_ir.size(); t++) right_acc_[static_cast<size_t>(i) + t] += v * right_ir[t];
    }
  }

  SampleData out(getOutChannels(), frames);
  auto out_left = out.getChannelData(0), out_right = out.getChannelData(1);
  for (int i = 0; i < frames; i++) {
    float l = left_acc_[static_cast<size_t>(i)];
    float r = right_acc_[static_cast<size_t>(i)];
    if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
    if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
    out_left[i] = l;
    out_right[i] = r;
  }
  out.setNonZero();

  for (size_t i = 0; i < tail_len; i++) {
    left_tail_[i] = left_acc_[static_cast<size_t>(frames) + i];
    right_tail_[i] = right_acc_[static_cast<size_t>(frames) + i];
  }

  return out;
}

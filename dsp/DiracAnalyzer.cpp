#include "DiracAnalyzer.h"

#include <cmath>

using namespace std;

namespace {
// The 8 log-spaced band edges in Hz (plans/dirac-heatmap-scope.md SS1) -
// 9 edges bounding 8 bands.
constexpr float kBandEdgesHz[DiracAnalyzer::kNumBands + 1] = {
  20.0f, 150.0f, 300.0f, 600.0f, 1200.0f, 2400.0f, 4800.0f, 9600.0f, 20000.0f
};

// One-pole low-pass coefficient for time constant tau (seconds) at a
// sample interval of dt (seconds) - alpha such that
// smoothed += alpha*(raw-smoothed) tracks a step input with that time
// constant.
float onePoleAlpha(float tau, float dt) {
  return 1.0f - expf(-dt / tau);
}
}

DiracAnalyzer::DiracAnalyzer(int sample_rate) : sample_rate_(sample_rate) {
  for (int i = 0; i < kFFTSize; i++) {
    hann_[static_cast<size_t>(i)] = 0.5f * (1.0f - cosf(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(kFFTSize - 1)));
  }
  windowed_.resize(kFFTSize);
  for (auto & buf : channel_buffer_) buf.reserve(kFFTSize * 2);

  float hop_dt = static_cast<float>(kHopSize) / static_cast<float>(sample_rate_);
  ie_alpha_ = onePoleAlpha(0.1f, hop_dt);           // SS1: per-band I/E smoothing, tau~100ms
  grid_attack_alpha_ = onePoleAlpha(0.015f, hop_dt);  // SS1: grid ballistics, attack tau~15ms
  // SS1 originally specified release tau~400ms, but UI.cpp's brightness
  // mapping is log1p(displayed)/log1p(running_max) - because log1p heavily
  // compresses large values, a cell that has decayed to even ~1% of its
  // peak (nominally "gone" after a few time constants) can still map to a
  // large fraction of full displayed brightness, stretching the *visible*
  // fade time to several times the nominal exponential tau. Confirmed too
  // slow ("takes ages") watching it live; shortened release tau to make
  // the perceived fade land in well under a second instead of ~2s+.
  grid_release_alpha_ = onePoleAlpha(0.15f, hop_dt);  // release tau~150ms

  int bin_count = kFFTSize / 2 + 1;
  for (int b = 0; b < kNumBands; b++) {
    auto lo = static_cast<int>(lroundf(kBandEdgesHz[b] * static_cast<float>(kFFTSize) / static_cast<float>(sample_rate_)));
    auto hi = static_cast<int>(lroundf(kBandEdgesHz[b + 1] * static_cast<float>(kFFTSize) / static_cast<float>(sample_rate_)));
    if (lo < 0) lo = 0;
    if (hi > bin_count) hi = bin_count;
    if (hi <= lo) hi = lo + 1 <= bin_count ? lo + 1 : bin_count; // never an empty band
    bands_state_[static_cast<size_t>(b)].bin_lo = lo;
    bands_state_[static_cast<size_t>(b)].bin_hi = hi;
  }
}

void
DiracAnalyzer::process(const SampleData & block) {
  int n = block.numberOfFrames();
  int regular = block.regularChannelCount();
  if (regular > 4) regular = 4;

  for (int c = 0; c < 4; c++) {
    auto & buf = channel_buffer_[static_cast<size_t>(c)];
    size_t old_size = buf.size();
    buf.resize(old_size + static_cast<size_t>(n));
    if (c < regular) {
      auto src = block.getChannelData(c);
      for (int i = 0; i < n; i++) buf[old_size + static_cast<size_t>(i)] = src[i];
    } else {
      for (int i = 0; i < n; i++) buf[old_size + static_cast<size_t>(i)] = 0.0f;
    }
  }

  while (channel_buffer_[0].size() >= static_cast<size_t>(kFFTSize)) {
    processFrame();
    for (auto & buf : channel_buffer_) {
      buf.erase(buf.begin(), buf.begin() + kHopSize);
    }
  }
}

void
DiracAnalyzer::processFrame() {
  // ACN order: 0=W, 1=Y, 2=Z, 3=X (AmbisonicEncoding.h).
  array<const vector<complex<float>> *, 4> spectra;
  for (int c = 0; c < 4; c++) {
    auto & src = channel_buffer_[static_cast<size_t>(c)];
    for (int i = 0; i < kFFTSize; i++) windowed_[static_cast<size_t>(i)] = src[static_cast<size_t>(i)] * hann_[static_cast<size_t>(i)];
    spectra[static_cast<size_t>(c)] = &fft_[static_cast<size_t>(c)].forward(windowed_);
  }
  auto & W = *spectra[kW];
  auto & Y = *spectra[kY];
  auto & Z = *spectra[kZ];
  auto & X = *spectra[kX];

  array<float, kGridSize> frame_grid {};

  for (int b = 0; b < kNumBands; b++) {
    auto & st = bands_state_[static_cast<size_t>(b)];

    // SS5: raw per-band intensity (I = Re{conj(W)*[X,Y,Z]}) and per-channel
    // power, each summed per-bin within the band (never sum complex bins
    // together first - see the class doc comment on why).
    float raw_ix = 0.0f, raw_iy = 0.0f, raw_iz = 0.0f;
    float raw_pow_w = 0.0f, raw_pow_y = 0.0f, raw_pow_z = 0.0f, raw_pow_x = 0.0f;
    for (int k = st.bin_lo; k < st.bin_hi; k++) {
      auto w = W[static_cast<size_t>(k)];
      auto y = Y[static_cast<size_t>(k)];
      auto z = Z[static_cast<size_t>(k)];
      auto x = X[static_cast<size_t>(k)];
      raw_ix += (conj(w) * x).real();
      raw_iy += (conj(w) * y).real();
      raw_iz += (conj(w) * z).real();
      raw_pow_w += norm(w);
      raw_pow_y += norm(y);
      raw_pow_z += norm(z);
      raw_pow_x += norm(x);
    }

    // SS1: one-pole smoothing of the raw per-band estimates (tau~100ms),
    // independent of the grid's own attack/release ballistics below.
    st.ix += ie_alpha_ * (raw_ix - st.ix);
    st.iy += ie_alpha_ * (raw_iy - st.iy);
    st.iz += ie_alpha_ * (raw_iz - st.iz);
    st.pow_w += ie_alpha_ * (raw_pow_w - st.pow_w);
    st.pow_y += ie_alpha_ * (raw_pow_y - st.pow_y);
    st.pow_z += ie_alpha_ * (raw_pow_z - st.pow_z);
    st.pow_x += ie_alpha_ * (raw_pow_x - st.pow_x);

    // SS5: E = 1/2*(|W|^2+|X|^2+|Y|^2+|Z|^2); the 1/2 is load-bearing for
    // this engine's unity-gain-W SN3D convention (see the design plan) -
    // omitting it makes every dry, correctly-positioned source read as
    // ~50% diffuse instead of ~0%.
    float E = 0.5f * (st.pow_w + st.pow_x + st.pow_y + st.pow_z);
    float I_mag = sqrtf(st.ix * st.ix + st.iy * st.iy + st.iz * st.iz);
    constexpr float kEps = 1e-12f;
    float diffuseness = 1.0f - I_mag / (E > kEps ? E : kEps);
    if (diffuseness < 0.0f) diffuseness = 0.0f;
    if (diffuseness > 1.0f) diffuseness = 1.0f;

    float az_rad = atan2f(st.iy, st.ix);
    float el_rad = atan2f(st.iz, sqrtf(st.ix * st.ix + st.iy * st.iy));
    constexpr float kRad2Deg = 180.0f / static_cast<float>(M_PI);

    auto & result = bands_[static_cast<size_t>(b)];
    result.azimuth = az_rad * kRad2Deg;
    result.elevation = el_rad * kRad2Deg;
    result.diffuseness = diffuseness;
    result.energy = E;
    result.magnitude[kW] = sqrtf(st.pow_w);
    result.magnitude[kY] = sqrtf(st.pow_y);
    result.magnitude[kZ] = sqrtf(st.pow_z);
    result.magnitude[kX] = sqrtf(st.pow_x);

    diffuse_energy_[static_cast<size_t>(b)] = E * diffuseness;

    // SS6: bilinear-splat this band's directional energy into the nearest
    // grid cells - azimuth wraps (mod kAzimuthBins), elevation clamps at
    // the poles (no wraparound).
    float directional_energy = E * (1.0f - diffuseness);
    float az_pos = (result.azimuth + 180.0f) * static_cast<float>(kAzimuthBins) / 360.0f;
    float el_pos = (result.elevation + 90.0f) / 10.0f;
    if (el_pos < 0.0f) el_pos = 0.0f;
    if (el_pos > static_cast<float>(kElevationBins) - 1e-4f) el_pos = static_cast<float>(kElevationBins) - 1e-4f;

    // Shift both from bin-*edge* to bin-*center* coordinates before the
    // floor/frac split below - bin i's center sits at az_pos/el_pos == i+0.5,
    // not i, so without this shift a source sitting exactly on a bin edge
    // (e.g. elevation=0, exactly between an even elevation-bin count's two
    // middle rows) floors to frac=0 and dumps 100% of its energy into a
    // single cell instead of splitting evenly across its two nearest bins -
    // a real, confirmed bug (ambisonic_directions.xml's elevation=0 sweep
    // landed entirely in one row instead of splitting across the two
    // middle rows). Elevation is re-clamped at 0 after the shift since it
    // doesn't wrap (no bin -1 to fall into); azimuth needs no such clamp
    // since it wraps via modulo below.
    az_pos -= 0.5f;
    el_pos -= 0.5f;
    if (el_pos < 0.0f) el_pos = 0.0f;

    int az0 = static_cast<int>(floorf(az_pos));
    float frac_az = az_pos - static_cast<float>(az0);
    az0 = ((az0 % kAzimuthBins) + kAzimuthBins) % kAzimuthBins;
    int az1 = (az0 + 1) % kAzimuthBins;

    int el0 = static_cast<int>(floorf(el_pos));
    if (el0 < 0) el0 = 0;
    if (el0 > kElevationBins - 1) el0 = kElevationBins - 1;
    float frac_el = el_pos - static_cast<float>(el0);
    int el1 = el0 + 1 < kElevationBins ? el0 + 1 : el0;

    float w00 = (1.0f - frac_az) * (1.0f - frac_el);
    float w10 = frac_az * (1.0f - frac_el);
    float w01 = (1.0f - frac_az) * frac_el;
    float w11 = frac_az * frac_el;

    frame_grid[static_cast<size_t>(el0 * kAzimuthBins + az0)] += w00 * directional_energy;
    frame_grid[static_cast<size_t>(el0 * kAzimuthBins + az1)] += w10 * directional_energy;
    frame_grid[static_cast<size_t>(el1 * kAzimuthBins + az0)] += w01 * directional_energy;
    frame_grid[static_cast<size_t>(el1 * kAzimuthBins + az1)] += w11 * directional_energy;
  }

  // SS1/6: asymmetric attack/release ballistics applied per grid cell,
  // every analysis frame.
  for (int cell = 0; cell < kGridSize; cell++) {
    float raw = frame_grid[static_cast<size_t>(cell)];
    float & smoothed = grid_[static_cast<size_t>(cell)];
    float alpha = raw > smoothed ? grid_attack_alpha_ : grid_release_alpha_;
    smoothed += alpha * (raw - smoothed);
    // A one-pole decay only asymptotes toward 0 and, in float32, ends up
    // pinned at some tiny nonzero (often denormal) value forever once a
    // cell has ever carried real energy - once alpha*(raw-smoothed)
    // underflows relative to smoothed's own magnitude, the update above
    // becomes a no-op. Snap to exact 0 once release has brought a cell
    // this close, both so "silent" really means silent again for any
    // consumer checking it (a real, confirmed bug: the heatmap widget's
    // own "idle" background color needed a matching epsilon downstream
    // instead of relying on this ever reaching exact 0) and to avoid
    // denormal-float slowdowns on architectures without FTZ/DAZ.
    if (smoothed > 0.0f && smoothed < 1e-6f) smoothed = 0.0f;
  }

  analysis_frame_count_++;
}

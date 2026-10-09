#ifndef _SINUSOIDBANK_H_
#define _SINUSOIDBANK_H_

#include <vector>
#include <cstddef>

// One decaying sinusoid: the engine's input. `group` says which output row
// it is summed into (a string, a body mode, ...).
struct PartialSpec {
  float frequency_hz;
  float amplitude;
  float alpha; // decay, nepers/second: amplitude(t) = amplitude * exp(-alpha * t)
  float phase; // radians
  int group;
};

// A bank of independently-decaying sinusoids for one note, the engine
// behind <additive> (Additive.h/AdditiveVoice.h). What the partials are is
// the model's business (AdditiveModel.h); this class only runs them. A fresh
// bank is built at note-on and lives as long as its voice.
//
// Each partial is a coupled-form recursive oscillator, y[n] = coeff*y[n-1] -
// y[n-2] with coeff = 2*cos(w), so there is no table or per-sample sinf().
// Its accumulated numerical drift is negligible because the partial's own
// amplitude decays long before it could be heard. The state is flat parallel
// arrays so the update is one auto-vectorizable pass.
//
// Partials are stored grouped, each group padded to a whole number of lane
// groups, and render() writes one row per group. A partial above Nyquist is
// never added; one that has decayed 90 dB below its own start is culled from
// its group (checked once per render() call, swap-with-last, since decay is
// monotonic and a culled partial never returns).
class SinusoidBank {
 public:
  SinusoidBank(const std::vector<PartialSpec> & specs, float sample_rate);

  // Adds the output of group g into rows[g * stride + 0 .. frames) (it mixes,
  // never zeroes). `stride` is at least `frames`.
  void render(float * rows, size_t stride, int frames);

  int groupCount() const { return static_cast<int>(groups_.size()); }
  bool isActive() const { return active_total_ > 0; }

  // Test-only: partials still being computed (post Nyquist skip and cull).
  int getActivePartialCountForTest() const { return active_total_; }
  // Test-only: the current amplitude of the n-th still-active partial, in
  // group order then construction order within a group, or 0 out of range.
  float getPartialAmplitudeForTest(int index) const;

 private:
  struct Group {
    size_t begin = 0;  // first slot, a multiple of the lane count
    size_t padded = 0; // slots, a multiple of the lane count
    int active = 0;
  };

  void cullDecayedPartials();

  std::vector<Group> groups_;
  std::vector<float> coeff_;      // 2*cos(2*pi*f/sr)
  std::vector<float> y1_, y2_;    // y[n-1], y[n-2]
  std::vector<float> amp_;        // current envelope amplitude
  std::vector<float> decay_mult_; // per-sample amplitude multiplier
  std::vector<float> min_amp_;    // -90dB-relative-to-start cull threshold
  int active_total_ = 0;
};

#endif

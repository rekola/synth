#ifndef _OVERTONEPROFILE_H_
#define _OVERTONEPROFILE_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

// How loud each overtone of one voice is (0Hxx), counted by number and
// independent of the voice's pitch. It is held as layers: each raises every
// `step`-th overtone by kRaiseDb times its amount (0 to 1), and an overtone
// several layers raise gets their sum in dB. One layer at a time is the
// target and rises; the rest fall at the same rate and are dropped at 0. The
// amounts together never pass 1, so no overtone rises above kRaiseDb.
//
// Like a pitch glide it moves a chunk at a time: advance() gives the amounts
// at the coming chunk's first and last sample, and instruments ramp between
// them, linearly in dB.
class OvertoneProfile {
 public:
  // How far a fully raised overtone is above its normal level.
  static constexpr double kRaiseDb = 12.0;
  // Layers at once; a further one replaces the lowest that is falling.
  static constexpr int kMaxLayers = 4;
  // Comb terms (combTerms()) at most: the non-empty subsets of the layers.
  static constexpr int kMaxTerms = (1 << kMaxLayers) - 1;

  struct Layer {
    int step = 0;                  // raises overtones step, 2*step, ...
    double now = 0.0;              // amount where the profile is
    double start = 0.0, end = 0.0; // amounts at the coming chunk's first and last sample
  };

  // Aims at every `step`-th overtone; a step under 2 aims at the plain
  // profile (every overtone shared, or none).
  void setTarget(int step) {
    engaged_ = true;
    target_ = step >= 2 ? step : 0;
    if (target_ == 0) return;
    for (int i = 0; i < count_; i++)
      if (layers_[i].step == target_) return;
    if (count_ == kMaxLayers) {
      int lowest = 0;
      for (int i = 1; i < count_; i++)
        if (layers_[i].now < layers_[lowest].now) lowest = i;
      removeLayer(lowest);
    }
    layers_[count_++] = Layer{target_, 0.0, 0.0, 0.0};
  }

  // The coming chunk of `frames`: the target's layer rises and the others
  // fall by `per_frame` of the whole change a frame (0 holds them).
  void advance(double per_frame, int frames) {
    const double delta = per_frame * frames;
    for (int i = 0; i < count_; i++) {
      Layer & layer = layers_[i];
      layer.start = layer.now;
      if (delta > 0.0) layer.now = layer.step == target_ ? std::min(1.0, layer.now + delta) : std::max(0.0, layer.now - delta);
      layer.end = layer.now;
    }
    dropSilentLayers();
  }

  // Lands on the target at once.
  void jump() {
    for (int i = 0; i < count_; i++) {
      Layer & layer = layers_[i];
      layer.now = layer.start = layer.end = layer.step == target_ ? 1.0 : 0.0;
    }
    dropSilentLayers();
  }

  // A command has aimed it at least once.
  bool engaged() const { return engaged_; }
  // Some overtone is raised somewhere in the coming chunk.
  bool raised() const { return count_ > 0; }
  int target() const { return target_; }
  int layerCount() const { return count_; }
  const Layer & layer(int i) const { return layers_[i]; }

  // Amount (0 to 1) of overtone `n` (1 is the fundamental) at the coming
  // chunk's first sample, or its last with `at_end`.
  double amount(int n, bool at_end = false) const {
    double sum = 0.0;
    for (int i = 0; i < count_; i++)
      if (n % layers_[i].step == 0) sum += at_end ? layers_[i].end : layers_[i].start;
    return std::min(1.0, sum);
  }
  // The gain of an overtone raised by `amount`.
  static double raiseGain(double amount) { return std::pow(10.0, kRaiseDb * amount / 20.0); }
  double gain(int n, bool at_end = false) const { return raiseGain(amount(n, at_end)); }

  // The profile as a sum of combs, for an instrument that cannot set its
  // partials one by one: overtone n's gain is the product of (1 + c) over the
  // layers raising it (c being the layer's gain less 1), which expands to the
  // sum, over each set of those layers, of their c's product, a term raising
  // every lcm-th overtone. Terms whose lcm passes `max_step` raise nothing
  // that sounds and are left out. Returns the number of terms.
  struct Term {
    int step;      // the lcm of the layers' steps
    unsigned mask; // which layers, by index
  };
  int combTerms(std::array<Term, kMaxTerms> & out, long max_step) const {
    int n = 0;
    for (unsigned mask = 1; mask < (1u << count_); mask++) {
      long step = 1;
      for (int i = 0; i < count_ && step <= max_step; i++)
        if (mask & (1u << i)) step = std::lcm(step, static_cast<long>(layers_[i].step));
      if (step <= max_step) out[static_cast<size_t>(n++)] = Term{static_cast<int>(step), mask};
    }
    return n;
  }

  // Per-sample gain of each layer across the coming chunk of `frames`,
  // linear in dB: begin() at its first sample, next() steps a sample.
  class Ramp {
   public:
    void begin(const OvertoneProfile & profile, int frames) {
      count_ = profile.layerCount();
      for (int i = 0; i < count_; i++) {
        const auto & layer = profile.layer(i);
        gain_[i] = raiseGain(layer.start);
        ratio_[i] = frames > 0 ? std::pow(raiseGain(layer.end) / gain_[i], 1.0 / frames) : 1.0;
      }
    }
    void next() {
      for (int i = 0; i < count_; i++) gain_[i] *= ratio_[i];
    }
    double layerGain(int i) const { return gain_[i]; }
    // The coefficient of a comb term: the product of its layers' c.
    double coefficient(unsigned mask) const {
      double c = 1.0;
      for (int i = 0; i < count_; i++)
        if (mask & (1u << i)) c *= gain_[i] - 1.0;
      return c;
    }

   private:
    int count_ = 0;
    double gain_[kMaxLayers] = {}, ratio_[kMaxLayers] = {};
  };

 private:
  void removeLayer(int i) {
    for (int k = i; k + 1 < count_; k++) layers_[k] = layers_[k + 1];
    count_--;
  }
  // A layer that is not the target and stays at 0 through the chunk.
  void dropSilentLayers() {
    for (int i = 0; i < count_;) {
      const Layer & layer = layers_[i];
      if (layer.step != target_ && layer.start <= 0.0 && layer.end <= 0.0)
        removeLayer(i);
      else
        i++;
    }
  }

  Layer layers_[kMaxLayers] = {};
  int count_ = 0;
  int target_ = 0;
  bool engaged_ = false;
};

#endif

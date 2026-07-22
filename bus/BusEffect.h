#ifndef _BUSEFFECT_H_
#define _BUSEFFECT_H_

// Common base for the shared send bus's effects (SendBusProcessor owns one
// FDNReverb and one ChorusEngine, both fed by a cross-track SendA/SendB
// mono sum and persisting for the whole playback session - unlike a
// regular per-track Effect, which is created fresh per track/note).
// Standardizes construction (sample rate) and the "always process every
// block, even when the mono input is silent" contract every bus effect
// needs so its own internal tail/modulation state stays continuous across
// blocks - implementations should never skip work just because a block's
// input happens to be silent.
class BusEffect {
 public:
  explicit BusEffect(int sampleRate) : sampleRate_(sampleRate) { }
  virtual ~BusEffect() = default;

  virtual void process(const float * monoInput, int frames) = 0;

 protected:
  int getSampleRate() const { return sampleRate_; }

 private:
  int sampleRate_;
};

#endif

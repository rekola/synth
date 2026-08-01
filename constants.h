#ifndef _DEFAULTS_H_
#define _DEFAULTS_H_

namespace constants {
  // The lower this block size is the more accurate the effects are.
  // Increasing the value significantly lowers the CPU usage of the voice rendering.
  // If LFO affects the low-pass filter it can be hearable even as low as 8.
  constexpr int RENDER_EFFECTSAMPLEBLOCK { 64 };
  constexpr int DEFAULT_VELOCITY { 0x40 };
  // Below this, a releasing voice (SoundFontVoice::render(),
  // EnvelopeFilterState::applyEffect() - never ATTACK/DECAY/SUSTAIN of a
  // still-held note) is treated as effectively silent and freed early via
  // the same path a naturally-completed release already uses, rather than
  // left to keep rendering an inaudible tail for its full authored
  // release time. A conservative first-pass value, not acoustically
  // final - a higher (less negative) floor such as -40dB may be
  // reasonable too, since masking from many simultaneously stacked
  // voices can make -40dB genuinely inaudible even though it might be
  // marginally perceptible from a single isolated voice.
  constexpr float SILENCE_KILL_FLOOR_DB { -60.0f };
};

#endif

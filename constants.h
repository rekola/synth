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

  // Floor-reflection defaults (see InstrumentVoice.h/Song.h) - named once
  // here rather than repeated as literals in ChannelConfiguration.h's
  // field initializers, Song.h's field initializers, and Song::
  // loadParameters()'s getFloat()/getBool() fallback arguments, which
  // must all agree (a song with no explicit attribute falls back to
  // Song's own field default; a ChannelConfiguration built without ever
  // loading a Song - tests, offline tools - falls back to its own field
  // default; both need to be the same number).
  constexpr float DEFAULT_EAR_HEIGHT { 1.7f };
  constexpr bool DEFAULT_FLOOR_REFLECTION_ENABLED { true };
  constexpr float DEFAULT_FLOOR_REFLECTION_STRENGTH { 0.4f };
  constexpr float DEFAULT_GROUND_ABSORPTION { 0.3f };

  // Per-row subdivision for tick-based pattern effect commands (currently
  // just 2Lxx/2Rxx azimuth slide - see SongState::scheduleAzimuthSlide()) -
  // the row's own duration is split into this many evenly-spaced steps,
  // each firing one incremental change. Not a "speed" setting a song can
  // adjust - fixed, the same way the row itself is the only other unit of
  // pattern timing in this engine.
  constexpr int TICKS_PER_ROW { 12 };
};

#endif

#ifndef _HAZE_H_
#define _HAZE_H_

#include "BusEffect.h"
#include "SaturatorShape.h"
#include "../ambisonic/AmbisonicDiffuseEncoder.h"
#include "../dsp/Biquad.h"
#include "../dsp/HalfbandFilter.h"
#include "../dsp/FilterType.h"

#include <array>
#include <string>
#include <vector>

// The three pre-delay divisions Haze offers (bus/Haze.h) - a snapped-to-
// one-of-three choice, not a continuous row-fraction the way
// MultiTapDelay's baseRows is: the prompt this implements gives exactly
// these three named divisions, tuned so the 5-35ms window they resolve to
// at working tempos sits in the precedence/fusion range pre-delay is
// there for (see Haze's own class comment) - an arbitrary continuous
// value has no comparable guarantee. Each fraction is of a WHOLE NOTE
// (the usual "1/64-note"-style delay-time convention), not of a row - see
// recomputePredelaySamples() in the .cpp for why a row-fraction (a row is
// already a short 16th-note-ish subdivision) would be far too short.
enum class HazePreDelayDivision { OneOver256 = 0, OneOver128, OneOver64 };

static inline const std::string to_string(HazePreDelayDivision division) {
  switch (division) {
  case HazePreDelayDivision::OneOver256: return "1/256";
  case HazePreDelayDivision::OneOver128: return "1/128";
  case HazePreDelayDivision::OneOver64: return "1/64";
  }
  return "1/128";
}

static inline HazePreDelayDivision parseHazePreDelayDivision(const std::string & text) {
  if (text == "1/256") return HazePreDelayDivision::OneOver256;
  if (text == "1/64") return HazePreDelayDivision::OneOver64;
  return HazePreDelayDivision::OneOver128;
}

// Named parameter sets showcasing Haze - same shape as FDNReverbPreset/
// MultiTapDelayPreset/GranularPreset (see those for the exact idiom: a
// preset resolves every attribute's default, an individually-specified
// attribute still overrides it). DEFAULT ("glue") is itself a named,
// described preset - the one a bare `<haze/>`, or an explicit
// preset="default", resolves to - not just an unnamed fallback;
// to_string() still maps it to "" so a default-preset instance round-trips
// quietly, matching every other implicit-default attribute this class
// writes.
enum class HazePreset { DEFAULT = 0, BODY, CRUNCH, SLAP, HASH, AIR };

static inline const std::string to_string(HazePreset preset) {
  switch (preset) {
  case HazePreset::DEFAULT: return "";
  case HazePreset::BODY: return "body";
  case HazePreset::CRUNCH: return "crunch";
  case HazePreset::SLAP: return "slap";
  case HazePreset::HASH: return "hash";
  case HazePreset::AIR: return "air";
  }
  return "";
}

// "Haze" - the ambisonic bus saturator, named for what it returns: a fully
// diffuse, tempo-delayed bed rather than a localized source - see
// plans/drum-bus-saturator.md for the full design. Unlike FDNReverb/
// MultiTapDelay/GranularCloud (point-source tap producers), it reports
// zero taps and instead uses BusEffect's direct-channel path
// (encodeDirect()) via a private AmbisonicDiffuseEncoder instance (wired
// in a later milestone - see the plan).
//
// DSP chain (process()): drive gain -> pre-distortion bandpass (HPF+LPF
// biquads) -> oversample (HalfbandFilter cascade, 4x for tanh/asym/
// softclip, 8x for fold) -> waveshaper (with bias) -> decimate -> DC
// block -> post tilt EQ (shelving biquad pair) -> static auto-gain
// compensation -> trim. That result is this effect's own pre-delay,
// pre-encode mono signal, kept in coreOutput_ - also what
// getChainSendSum() returns (BusEffect.h's default sums taps, which this
// effect has none of). Tempo-synced pre-delay (setRowDuration()/
// getPredelayedMono() below) is a second, separate stage reading from
// that same coreOutput_ - a chain send from this slot deliberately still
// carries the pre-delay *tap point*, i.e. the undelayed signal (same
// shape as every other BusEffect's chain send: it's meant to feed the
// other slot's own processing, not to duplicate this slot's own spatial
// treatment onto it).
class Haze : public BusEffect {
 public:
  explicit Haze(int sampleRate);

  // driveDb: 0-36, pre-shaper gain. bias: 0-1, DC offset into the shaper
  // (raises even-order content - see applyShape() in the .cpp). hpfHz/
  // lpfHz: pre-distortion bandpass, 20-1000Hz / 1000-16000Hz. tiltDb:
  // +-12, post-shaper spectral tilt pivoting at 1kHz. trimDb: +-12,
  // applied after the static auto-gain compensation. predelay: one of the
  // three named divisions (HazePreDelayDivision above) - resolved to
  // actual seconds together with whatever setRowDuration() last supplied
  // (see recomputePredelaySamples() in the .cpp), the same "two
  // independently-settable inputs, either can update without needing the
  // other's current value" shape MultiTapDelay's own baseRows/
  // setRowDuration() split uses. diffusion: 0-1, read by encodeDirect()
  // below - 0 = W-only/centered, 1 = fully isotropic (see
  // AmbisonicDiffuseEncoder.h's own doc comment for the per-degree taper
  // in between). Never allocates beyond what the constructor already
  // sized (the oversample/decimate HalfbandFilter cascade's own delay
  // lines are fixed-size regardless of `shape`, and the pre-delay line is
  // sized once to its own 40ms ceiling) - safe to call at any time,
  // including mid-playback.
  void setParameters(float driveDb, SaturatorShape shape, float bias, float hpfHz, float lpfHz, float tiltDb, float trimDb, HazePreDelayDivision predelay, float diffusion);

  // The song's current tempo resolved to seconds-per-row
  // (ChannelConfiguration::getRowDuration()) - see setParameters()'s own
  // comment for why this is separate. Recomputes the pre-delay line's
  // read length, clamped to roughly 4-40ms regardless of what the
  // division/tempo combination would otherwise resolve to, so an extreme
  // tempo degrades gracefully (no comb filtering, no audible slapback)
  // rather than producing either. Never reallocates - the delay buffer is
  // sized once, at construction, to the 40ms ceiling - so this is safe to
  // call at any time, including mid-playback.
  void setRowDuration(float rowDurationSeconds) override;

  // Processes `frames` samples of mono input (the send-B sum) through the
  // full DSP chain into coreOutput_, then through the pre-delay line into
  // predelayed_, retrievable via getChainSendSum()/getPredelayedMono()
  // respectively until the next process() call. Always runs, even for
  // silent input, so the filter/oversampler/DC-blocker/delay-line state
  // stays continuous across blocks.
  void process(const float * input, int frames) override;

  // No point-source taps - see the class comment above.
  int getNumTaps() const override { return 0; }
  const float * getTap(int) const override { return nullptr; } // never called: getNumTaps() == 0
  SphericalPosition getTapDirection(int) const override { return SphericalPosition{}; } // never called

  // Overridden because BusEffect's default sums getTap(0..getNumTaps()),
  // which is empty for this effect - this is instead the effect's own
  // pre-delay, pre-encode mono signal (coreOutput_), so a chain send from
  // this slot still carries real content (see BusEffect.h's own doc
  // comment on getChainSendSum()).
  void getChainSendSum(float * out, int frames) const override;

  // The tempo-delayed signal (coreOutput_ read back through the pre-delay
  // line) - what a later milestone's encodeDirect() diffuse-encodes into
  // the shared bus. Exposed as its own accessor (rather than only being
  // readable via encodeDirect()) so the pre-delay stage is independently
  // testable before the diffuse encoder is wired in.
  void getPredelayedMono(float * out, int frames) const;

  // Read back the (clamped) resolved pre-delay length, in samples at this
  // instance's own sample rate - used by tests and by storeParameters()'s
  // deviation-only comparison, not by process() (which reads
  // predelaySamples_ directly).
  int getPredelaySamples() const { return predelaySamples_; }

  // Diffuse-encodes the pre-delayed signal (getPredelayedMono()) into
  // busAmbisonic's regular channels at getWetLevel() - see BusEffect.h's
  // own doc comment on encodeDirect() for why this effect uses this path
  // instead of the ordinary point-source tap loop.
  void encodeDirect(AudioBuffer & busAmbisonic, int frames) override;

  // <haze> element's own attributes: "preset" plus drive/shape/bias/hpf/
  // lpf/tilt/trim/predelay/diffusion, each deviation-only against
  // whatever the resolved preset (or, absent one, this constructor's own
  // tuned defaults - see Haze.cpp's presetValues()) implies - wet/
  // chainSend are handled generically by BusEffect::loadParameters()/
  // storeParameters(), called first. Same idiom as MultiTapDelay's own
  // loadParameters()/storeParameters() - see that class for the exact
  // shape being mirrored here.
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Read back the (clamped) values setParameters() last stored - used
  // only for deviation-only project-file saving (storeParameters()
  // above), not by any DSP code here (which works from the already-
  // derived driveLinear_/autoGainCompensation_/etc. instead).
  float getDriveDb() const { return rawDriveDb_; }
  SaturatorShape getShape() const { return shape_; }
  float getBias() const { return bias_; }
  float getHpf() const { return rawHpfHz_; }
  float getLpf() const { return rawLpfHz_; }
  float getTilt() const { return rawTiltDb_; }
  float getTrim() const { return rawTrimDb_; }
  HazePreDelayDivision getPredelayDivision() const { return predelayDivision_; }
  float getDiffusion() const { return diffusion_; }
  HazePreset getPreset() const { return preset_; }

 private:
  static constexpr int kMaxOversampleStages = 3; // 8x, for `fold`

  Biquad<float> hpf_, lpf_;
  Biquad<float> tiltLow_, tiltHigh_;

  std::array<HalfbandFilter, kMaxOversampleStages> upStages_;
  std::array<HalfbandFilter, kMaxOversampleStages> downStages_;

  float dcBlockX1_ = 0.0f, dcBlockY1_ = 0.0f;

  SaturatorShape shape_ = SaturatorShape::Tanh;
  float bias_ = 0.0f;
  float driveLinear_ = 1.0f;
  float autoGainCompensation_ = 1.0f;
  float trimLinear_ = 1.0f;
  int oversampleStages_ = 2;

  // Raw (clamped) inputs to setParameters(), cached purely so the public
  // getDriveDb()/getHpf()/getLpf()/getTilt()/getTrim() getters above can
  // read them back - the DSP itself only ever consumes the already-
  // derived driveLinear_/autoGainCompensation_/biquad coefficients/etc.,
  // never these fields directly (same convention as FDNReverb's/
  // MultiTapDelay's own raw* members).
  float rawDriveDb_ = 0.0f, rawHpfHz_ = 0.0f, rawLpfHz_ = 0.0f, rawTiltDb_ = 0.0f, rawTrimDb_ = 0.0f;

  float diffusion_ = 1.0f;
  HazePreset preset_ = HazePreset::DEFAULT;

  // instanceSalt 0 - reserved for Haze specifically; a future reverb/
  // granular-cloud port to this same encoder (see
  // AmbisonicDiffuseEncoder.h's own doc comment) would use a different
  // one so the two never draw correlated delay-length sets.
  AmbisonicDiffuseEncoder diffuseEncoder_;

  // Scratch buffers, resized (grow-only) as process() sees larger frame
  // counts - never reallocated on the audio thread once warmed up, same
  // convention as every other BusEffect's own scratch buffers.
  std::vector<float> bandpassed_;
  std::array<std::vector<float>, kMaxOversampleStages> oversampled_; // [0] = 2x, [1] = 4x, [2] = 8x
  std::vector<float> coreOutput_;

  // Pre-delay: single delay line, buffer sized once (constructor) to
  // kMaxPredelaySeconds regardless of the live (clamped 4-40ms) length in
  // use - same "buffer sized to the ceiling, live length just moves the
  // read offset within it" shape as FDNReverb's/MultiTapDelay's own delay
  // lines.
  static constexpr float kMaxPredelaySeconds = 0.040f;
  static constexpr float kMinPredelaySeconds = 0.004f;
  std::vector<float> predelayBuffer_;
  int predelayWritePos_ = 0;
  int predelaySamples_ = 1;
  std::vector<float> predelayed_;

  // Cached inputs to recomputePredelaySamples() (the .cpp) - either can be
  // updated independently without needing the other's current value, same
  // reasoning as MultiTapDelay's baseRows_/rowDurationSeconds_ split.
  HazePreDelayDivision predelayDivision_ = HazePreDelayDivision::OneOver128;
  float rowDurationSeconds_ = 60.0f / 4.0f / 90.0f; // matches Song's own default 90bpm tempo

  void recomputePredelaySamples();
};

#endif

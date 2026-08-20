#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <cctype>
#include <string_view>

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  void loadParameters(const ParameterSource & input) {
    Instrument::loadParameters(input);
    from_ = input.getText("from");
  }

  void storeParameters(ParameterSource & output) const {
    Instrument::storeParameters(output);
    if (!from_.empty()) output.set("from", from_);
  }

  const std::string & getFrom() const { return from_; }
  void setFrom(std::string from) { from_ = std::move(from); }

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}, bool needs_decorrelation = false) const override {
    detune *= getHarmonic();
    detune /= getSubharmonic();

    auto voice = concrete_instrument_->playNote(channel_config, position, frequency, detune, velocity, note_value, sends, note_coord, needs_decorrelation);

    // don't pass velocity, position, or sends to children - a modulator
    // doesn't produce audible output of its own that should reach a bus
    // (see SendLevels.h's own doc comment for why SendLevels{} - not sends
    // - is the correct value here, not just an inert placeholder). note_coord/
    // needs_decorrelation still forward unchanged - see Oscillator::playNote()'s
    // identical note.
    for (auto & child : getChildren()) {
      auto modulator = child->playNote(channel_config, SphericalPosition{}, frequency, detune, 1.0, note_value, SendLevels{}, note_coord, needs_decorrelation);
      if (modulator) voice->addChild(child->getInternalId(), std::move(modulator));
    }

    return voice;
  }

  const char * getElementName() const override { return "instrument"; }

  // Pure delegation, same shape as playNote()'s own forward above - this
  // node has no opinion of its own about extent, whatever it resolves to
  // (an SF2 preset, the built-in Oscillator, ...) does. concrete_instrument_
  // can be null before prepare() has run once.
  float getDefaultExtent() const override {
    return concrete_instrument_ ? concrete_instrument_->getDefaultExtent() : 0.0f;
  }

  // Two-step resolution, per docs/instrument-paths.md: an exact literal
  // match (native names, or a not-yet-migrated literal string) first, since
  // that's a stronger signal than a taxonomy-path walk-up would be; only
  // once that's missed does from get resolved as a dotted path at all
  // (registerPath()'s registry, walked up and defaulted by resolvePath()).
  // Both are plain misses-return-nullptr lookups - the fallback to
  // getDefaultInstrument() happens exactly once, here, after both attempts,
  // not folded into either lookup itself (see InstrumentProvider's own note
  // on why the old getInstrumentByName() couldn't support this).
  void prepare(const InstrumentProvider & provider) override {
    auto resolved = provider.tryGetByLiteralName(getFrom());
    if (!resolved) resolved = provider.resolvePath(getFrom());
    concrete_instrument_ = resolved ? resolved : provider.getDefaultInstrument();
  }

  // What a UI should show for this instrument - never persisted (see
  // Track::getDisplayName()'s own doc comment). Preference order: an
  // explicit user-assigned name; else from's last path segment, prettified;
  // else, once resolved, the concrete instrument's own registered name
  // (already human-readable for a native SF2 preset, e.g. "Glockenspiel" -
  // stripped of InstrumentProvider's "native:" namespace prefix, since that
  // prefix exists to keep the registry unambiguous, not to be shown to a
  // user); else a last-resort placeholder for the not-yet-prepare()d case.
  std::string getDisplayName() const override {
    if (!getName().empty()) return getName();
    if (!from_.empty()) return prettifyPathSegment(from_);
    if (concrete_instrument_) {
      const auto & native_name = concrete_instrument_->getName();
      constexpr std::string_view kNativePrefix = "native:";
      if (native_name.compare(0, kNativePrefix.size(), kNativePrefix) == 0) {
	return native_name.substr(kNativePrefix.size());
      }
      return native_name;
    }
    return "(instrument)";
  }

 private:
  // getDisplayName()'s path-segment fallback: "piano.acoustic.grand" ->
  // "Grand", "piano.acoustic.upright.honkyTonk" -> "Honky Tonk" - the last
  // dotted segment, camelCase split into words, first letter capitalized.
  // A display nicety, not a parser - doesn't need to handle every possible
  // taxonomy path perfectly (docs/instrument-paths.md's paths are
  // lowerCamelCase segments by convention, so this covers the common
  // case), just read better than the raw segment.
  static std::string prettifyPathSegment(const std::string & path) {
    auto dot = path.rfind('.');
    std::string segment = (dot == std::string::npos) ? path : path.substr(dot + 1);
    if (segment.empty()) return segment;

    std::string result;
    for (size_t i = 0; i < segment.size(); i++) {
      char c = segment[i];
      if (i > 0 && isupper(static_cast<unsigned char>(c)) && islower(static_cast<unsigned char>(segment[i - 1]))) {
	result += ' ';
      }
      result += (i == 0) ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
    }
    return result;
  }

  std::string from_;
  std::shared_ptr<Instrument> concrete_instrument_;
};

#endif

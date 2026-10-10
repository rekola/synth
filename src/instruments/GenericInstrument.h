#ifndef _GENERICINSTRUMENT_H_
#define _GENERICINSTRUMENT_H_

#include "Instrument.h"
#include "InstrumentProvider.h"
#include "SF2GeneratorTable.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"
#include "../model/NoteCoordinate.h"

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class GenericInstrument : public Instrument {
 public:
  GenericInstrument() { }

  void loadParameters(const ParameterSource & input) {
    Instrument::loadParameters(input);
    from_ = input.get<std::string>("from");
  }

  void storeParameters(ParameterSource & output) const {
    Instrument::storeParameters(output);
    if (!from_.empty()) output.set("from", from_);
  }

  const std::string & getFrom() const { return from_; }
  void setFrom(std::string from) { from_ = std::move(from); }

  // <generator> children (SF2 generator overrides) - element children, not
  // attributes, so unlike `from` above these are parsed/written by
  // Song.cpp's parseChildTrack()/storeChildTrack() directly (the same
  // treatment PercussionTrack's own <lane> children get), not
  // through loadParameters()/storeParameters(). Two containers, matching
  // SF2GeneratorTable.h's own split: a recognized generator name resolves
  // to its id and lands in generator_overrides_, keyed by that id (the
  // form prepare()/cloneWithOverrides() actually consume); an unrecognized
  // one is preserved verbatim, by name, in unknown_generator_overrides_ -
  // round-tripped losslessly on save but never applied by any backend
  // (see SF2GeneratorTable.h's own doc comment on why unknown names aren't
  // rejected).
  void addGeneratorOverride(SF2Generator id, float value) { generator_overrides_[id] = value; }
  void addUnknownGeneratorOverride(std::string name, float value) { unknown_generator_overrides_.emplace_back(std::move(name), value); }
  const std::unordered_map<SF2Generator, float> & getGeneratorOverrides() const { return generator_overrides_; }
  const std::vector<std::pair<std::string, float>> & getUnknownGeneratorOverrides() const { return unknown_generator_overrides_; }

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, SpatialMode spatial_mode, Tuning tuning, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const override {
    return concrete_instrument_->playNote(channel_config, position, spatial_mode, tuning, detune, velocity, note_value, sends, note_coord);
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
  //
  // generator_overrides_ is applied here too, once, rather than at
  // playNote() time - see Instrument::cloneWithOverrides()'s own doc
  // comment for why overrides are load-time configuration, not a per-note
  // parameter. Skipped entirely when there are no overrides to apply, so a
  // pool slot with no <generator> children keeps sharing the provider's
  // one canonical instance exactly as before - no extra allocation, no
  // extra SongObject id consumed.
  void prepare(const InstrumentProvider & provider) override {
    std::shared_ptr<Track> resolved = provider.tryGetByLiteralName(getFrom());
    if (!resolved) resolved = provider.resolvePath(getFrom());
    resolved_from_name_ = resolved != nullptr;
    if (!resolved) resolved = provider.getDefaultInstrument();

    // cloneWithOverrides() is Instrument-only (SF2 generator overrides
    // never apply to a hand-built <envelope>-wrapped library entry, which
    // registerPath() can now hold - see its own doc comment) - a
    // dynamic_cast miss here just means "this backend/shape doesn't
    // support generator overrides," the same silent-ignore contract
    // cloneWithOverrides() itself already documents for a nullptr return.
    if (!generator_overrides_.empty()) {
      if (auto * as_instrument = dynamic_cast<Instrument *>(resolved.get())) {
	auto clone = as_instrument->cloneWithOverrides(generator_overrides_);
	if (clone) resolved = std::move(clone);
      }
    }

    concrete_instrument_ = resolved;
  }

  // What a UI should show for this instrument - never persisted (see
  // Track::getDisplayName()'s own doc comment). Preference order: an
  // explicit user-assigned name; else, once `from` resolved to a real
  // instrument, that instrument's own registered name (a SoundFont's
  // preset name, e.g. "Glockenspiel" - stripped of InstrumentProvider's
  // "native:" namespace prefix, since that prefix exists to keep the
  // registry unambiguous, not to be shown to a user); else `from`'s last
  // path segment, prettified (also what an unresolved `from` shows, rather
  // than the generic fallback instrument's name); else a last-resort
  // placeholder for the not-yet-prepare()d case.
  std::string getDisplayName() const override {
    if (!getName().empty()) return getName();
    if (resolved_from_name_ && concrete_instrument_ && !concrete_instrument_->getName().empty()) {
      const auto & resolved_name = concrete_instrument_->getName();
      constexpr std::string_view kNativePrefix = "native:";
      if (resolved_name.compare(0, kNativePrefix.size(), kNativePrefix) == 0) return resolved_name.substr(kNativePrefix.size());
      return resolved_name;
    }
    if (!from_.empty()) return prettifyPathSegment(from_);
    if (concrete_instrument_) return concrete_instrument_->getName();
    return "(instrument)";
  }

 private:
  // getDisplayName()'s path-segment fallback: "piano.acoustic.grand" ->
  // "Grand", "piano.acoustic.upright.honky-tonk" -> "Honky Tonk" - the last
  // dotted segment, hyphen-separated words, first letter of each
  // capitalized. A display nicety, not a parser.
  static std::string prettifyPathSegment(const std::string & path) {
    auto dot = path.rfind('.');
    std::string segment = (dot == std::string::npos) ? path : path.substr(dot + 1);

    std::string result;
    bool word_start = true;
    for (char c : segment) {
      if (c == '-') {
	result += ' ';
	word_start = true;
      } else {
	result += word_start ? static_cast<char>(toupper(static_cast<unsigned char>(c))) : c;
	word_start = false;
      }
    }
    return result;
  }

  std::string from_;
  // `from_` named a real instrument (rather than falling back to the default one).
  bool resolved_from_name_ = false;
  std::unordered_map<SF2Generator, float> generator_overrides_;
  std::vector<std::pair<std::string, float>> unknown_generator_overrides_;
  std::shared_ptr<Track> concrete_instrument_;
};

#endif

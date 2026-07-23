#ifndef _SONG_H_
#define _SONG_H_

#include "StatefulSongObject.h"
#include "Track.h"
#include "Section.h"
#include "Pattern.h"
#include "bus/BusEffectRegistry.h"

#include <memory>
#include <vector>

class InstrumentProvider;
class Mixer;

class Song : public StatefulSongObject {
 public:
  Song(Tuning tuning = Tuning::TET12, short key = -1, float randomization_factor = 0.01f)
    : tuning_(tuning), key_note_number_(key), randomization_factor_(randomization_factor) {
    resetBusToDefaults();
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;

  Tuning getTuning() const { return tuning_; }
  void setTuning(Tuning tuning) { tuning_ = tuning; }

  short getKey() const { return key_note_number_; }
  void setKey(int key) { key_note_number_ = key; }
  
  float getRandomizationFactor() const { return randomization_factor_; }
  void setRandomizationFactor(float f) { randomization_factor_ = f; }
  
  short getTempo() const { return bpm_; }
  void setTempo(short bpm) { bpm_ = bpm; }

  // The shared 2-slot send bus (bus/SendBusProcessor.h) - slot 0 = A,
  // slot 1 = B, matching SendBusProcessor::kSlotA/kSlotB. Each slot's
  // BusEffect instance here is real (never null - even an empty slot
  // holds a NullBusEffect, bus/BusEffectRegistry.h) but exists purely to
  // own/(de)serialize its own parameters via BusEffect::loadParameters()/
  // storeParameters() - constructed at an arbitrary placeholder sample
  // rate (Song::open() has no access to the real device sample rate) and
  // never process()'d. SongState::initialize() constructs the *real*,
  // correctly-sample-rated instances the audio thread actually uses,
  // round-tripping a slot's parameters through a MemoryParameterSource
  // into them - a Song-held BusEffect and a SongState-held BusEffect for
  // the same slot are two different objects, never a shared/aliased one.
  // Defaults to slot 0 = reverb, slot 1 = delay (resetBusToDefaults(),
  // called from the constructor and from loadParameters() before parsing
  // any <bus> element) - the compiled-in default bus, per the
  // project-file plan's "no <bus> element at all -> compiled defaults"
  // rule.
  BusEffect & getBusSlot(int slot) { return slot == 0 ? *bus_slot_a_ : *bus_slot_b_; }
  const BusEffect & getBusSlot(int slot) const { return slot == 0 ? *bus_slot_a_ : *bus_slot_b_; }
  BusEffectKind getBusSlotKind(int slot) const { return slot == 0 ? bus_slot_a_kind_ : bus_slot_b_kind_; }

  // Replaces a slot's occupant entirely - constructs a fresh, default-
  // valued instance of `kind` via the registry (at this Song's own
  // placeholder sample rate). Used by the <bus> loading path in Song.cpp;
  // also available for a future editing UI (out of scope for now - see
  // the load-time-only slot-configuration plan).
  void setBusSlotKind(int slot, BusEffectKind kind);

  void resetBusToDefaults() {
    setBusSlotKind(0, BusEffectKind::Reverb);
    setBusSlotKind(1, BusEffectKind::Delay);
  }

  void incVersion() { version_++; }
  int getVersion() const { return version_; }

  const std::vector<Section> & getSections() const { return sections_; }
  const std::vector<Pattern> & getPatterns() const { return patterns_; }
  const Pattern & getPattern(int i) const { return i >= 0 && i < static_cast<int>(patterns_.size()) ? patterns_[i] : empty_pattern_; }
  Pattern & getPattern(int i) { return i >= 0 && i < static_cast<int>(patterns_.size()) ? patterns_[i] : empty_pattern_; }

  std::pair<int, int> normalizePosition(int pattern_idx, int row_idx) const {
    while (pattern_idx < static_cast<int>(patterns_.size())) {
      auto & pattern = patterns_[pattern_idx];
      if (row_idx < pattern.getNumRows()) {
	break;
      } else {
	pattern_idx++;
	row_idx -= pattern.getNumRows();
      }
    }
    return std::pair(pattern_idx, row_idx);
  }
  
  Section & addSection(Section section) {
    incVersion();
    sections_.push_back(std::move(section));
    return sections_.back();
  }

  Section & addSection() { return addSection(Section()); }

  Pattern & addPattern(Pattern pattern) {
    incVersion();
    patterns_.push_back(std::move(pattern));
    return patterns_.back();
  }

  Pattern & addPattern(int rows) { return addPattern(Pattern(rows)); }
    
  const std::vector<std::unique_ptr<Track> > & getInstruments() const { return instruments_; }
  const Track & getInstrument(int i) const { return *(instruments_[i]); }
  void addInstrument(std::unique_ptr<Track> i) {
    instruments_.push_back(std::move(i));
    incVersion();
  }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  std::vector<std::unique_ptr<Track> > & getTracks() { return tracks_; }
  const std::vector<std::unique_ptr<Track> > & getTracks() const { return tracks_; }

  Track & addTrack(std::unique_ptr<Track> track) {
    tracks_.push_back(std::move(track));
    incVersion();
    return *(tracks_.back());
  }

  const Track * getTrackByInternalId(int id) const {
    for (auto & track : getTracks()) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackByInternalId(int id) {
    for (auto & track : getTracks()) {
      auto r = track->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const Track * getTrackById(std::string_view id) const {
    for (auto & track : getTracks()) {
      auto r = track->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getTrackById(std::string_view id) {
    for (auto & track : getTracks()) {
      auto r = track->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  int getTrackDepth() const {
    int max_depth = 0;
    for (auto & track : getTracks()) {
      auto d = track->getDepth();
      if (d > max_depth) max_depth = d;
    }
    return max_depth;
  }

  // Flattens the track tree into the leaf tracks (INSTRUMENT_CONTROL/
  // PERCUSSION_CONTROL/SAMPLE) that are actually addressable as "a track" -
  // Group/effect-chain wrapper tracks are skipped over, not listed
  // themselves. The single canonical definition of "the addressable track
  // list and its order" - every caller that needs to resolve a track by
  // position (PatternEditor's columns, a Launchpad device's assigned
  // track) uses this same one, so they can never quietly diverge.
  std::vector<int> getRootTrackIds() const;

private:
  Tuning tuning_ = Tuning::TET12;
  short key_note_number_ = 0;
  float randomization_factor_ = 0.0f;
  int bpm_ = 90;

  std::unique_ptr<BusEffect> bus_slot_a_, bus_slot_b_;
  BusEffectKind bus_slot_a_kind_ = BusEffectKind::Reverb;
  BusEffectKind bus_slot_b_kind_ = BusEffectKind::Delay;

  int version_ = 1;

  std::vector<std::unique_ptr<Track> > instruments_;
  std::vector<std::unique_ptr<Track> > tracks_;
  std::vector<Section> sections_;
  std::vector<Pattern> patterns_;
  
  static inline Pattern empty_pattern_;
};

#endif


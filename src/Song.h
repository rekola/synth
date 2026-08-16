#ifndef _SONG_H_
#define _SONG_H_

#include "StatefulSongObject.h"
#include "Track.h"
#include "Scene.h"
#include "bus/BusEffectRegistry.h"
#include "constants.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

class InstrumentProvider;
class Mixer;

class Song : public StatefulSongObject {
 public:
  Song(Tuning tuning = Tuning::TET12, short key = -1)
    : tuning_(tuning), key_note_number_(key) {
    resetBusToDefaults();
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;

  Tuning getTuning() const { return tuning_; }
  void setTuning(Tuning tuning) { tuning_ = tuning; }

  short getKey() const { return key_note_number_; }
  void setKey(int key) { key_note_number_ = key; }
    
  short getTempo() const { return bpm_; }
  void setTempo(short bpm) { bpm_ = bpm; }

  // Every pattern in the song shares this one row count (<song
  // patternRows="N">) - there is no per-Pattern length any more. Changing
  // it reshapes every pattern in the song at once, hence incVersion().
  int getPatternLength() const { return pattern_length_; }
  void setPatternLength(int rows) { pattern_length_ = rows; incVersion(); }

  // Floor-reflection parameters (see InstrumentVoice.h) - fixed for the
  // whole song, not live-editable (no live control path exists for any
  // of these). getEarHeight() is clamped to [0.1, 50] meters at load time
  // (setEarHeight() below) - the lower bound guards against a degenerate
  // zero-height listener, the upper is an engineering ceiling (a taller
  // listener turns the reflection into an increasingly obvious slapback/
  // canyon echo rather than a fusion cue - a legitimate, if unusual,
  // effect, not something to forbid outright).
  float getEarHeight() const { return ear_height_; }
  void setEarHeight(float h) { ear_height_ = h < 0.1f ? 0.1f : (h > 50.0f ? 50.0f : h); }

  bool getFloorReflectionEnabled() const { return floor_reflection_enabled_; }
  void setFloorReflectionEnabled(bool e) { floor_reflection_enabled_ = e; }

  float getFloorReflectionStrength() const { return floor_reflection_strength_; }
  void setFloorReflectionStrength(float s) { floor_reflection_strength_ = s; }

  float getGroundAbsorption() const { return ground_absorption_; }
  void setGroundAbsorption(float a) { ground_absorption_ = a; }

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

  const std::vector<Scene> & getScenes() const { return scenes_; }
  const Scene & getScene(int i) const { return i >= 0 && i < static_cast<int>(scenes_.size()) ? scenes_[static_cast<size_t>(i)] : empty_scene_; }
  Scene & getScene(int i) { return i >= 0 && i < static_cast<int>(scenes_.size()) ? scenes_[static_cast<size_t>(i)] : empty_scene_; }

  // Clamps `target` so it can't leave the pattern `current` falls in -
  // used by the UI-thread edit cursor (Controller::moveEditPosition()/
  // setEditPosition(), only ever called while stopped) and the audio
  // thread's own handling of the MOVE_POSITION/SET_POSITION events those
  // push (Player::handlePlaybackControlEvent()), so both sides derive the
  // identical clamped result independently instead of one trusting a
  // value computed by the other across the thread boundary - the same
  // "self-clamp on both sides" pattern SongState::movePosition()/
  // setPosition() already use for the plain "never go negative" clamp.
  // Real playback's own row-by-row advance (SongState::renderBlock()) never
  // goes through this - only stopped-transport cursor navigation does,
  // which is what keeps a selection from silently spanning two patterns.
  int clampRowToCurrentPattern(int current, int target) const {
    auto len = getPatternLength();
    if (len <= 0) return std::max(0, target);
    auto pattern_start = (std::max(0, current) / len) * len;
    return std::clamp(target, pattern_start, pattern_start + len - 1);
  }

  std::pair<int, int> normalizePosition(int pattern_idx, int row_idx) const {
    auto len = getPatternLength();
    if (len > 0 && row_idx >= len) {
      pattern_idx += row_idx / len;
      row_idx %= len;
    }
    return std::pair(pattern_idx, row_idx);
  }
  
  Scene & addScene(Scene scene) {
    incVersion();
    scenes_.push_back(std::move(scene));
    return scenes_.back();
  }

  Scene & addScene() { return addScene(Scene()); }

  const std::vector<std::unique_ptr<Track> > & getInstruments() const { return instruments_; }
  const Track & getInstrument(int i) const { return *(instruments_[static_cast<size_t>(i)]); }
  void addInstrument(std::unique_ptr<Track> i) {
    instruments_.push_back(std::move(i));
    incVersion();
  }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  std::vector<std::unique_ptr<Track> > & getTracks() { return tracks_; }
  const std::vector<std::unique_ptr<Track> > & getTracks() const { return tracks_; }

  // See tracks_mutex_'s own comment - SongState::renderBlock() locks this to
  // take a quick snapshot of the current tracks before rendering them.
  std::mutex & getTracksMutex() const { return *tracks_mutex_; }

  Track & addTrack(std::unique_ptr<Track> track) {
    if (track->getId().empty()) track->setId(generateUniqueTrackId());
    std::lock_guard<std::mutex> guard(*tracks_mutex_);
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
  int bpm_ = 90;
  int pattern_length_ = 64;
  float ear_height_ = constants::DEFAULT_EAR_HEIGHT;
  bool floor_reflection_enabled_ = constants::DEFAULT_FLOOR_REFLECTION_ENABLED;
  float floor_reflection_strength_ = constants::DEFAULT_FLOOR_REFLECTION_STRENGTH;
  float ground_absorption_ = constants::DEFAULT_GROUND_ABSORPTION;

  std::unique_ptr<BusEffect> bus_slot_a_, bus_slot_b_;
  BusEffectKind bus_slot_a_kind_ = BusEffectKind::Reverb;
  BusEffectKind bus_slot_b_kind_ = BusEffectKind::Delay;

  int version_ = 1;

  std::vector<std::unique_ptr<Track> > instruments_;
  std::vector<std::unique_ptr<Track> > tracks_;

  // A track's own textual id (SongObject::getId()) is the only thing a
  // <note>/<command> element can reference it by that survives a
  // save/reload round trip - its raw internal id is just a runtime
  // counter, reassigned fresh every time a Track object is constructed,
  // so a note left referencing one is unresolvable the moment the file is
  // reopened (see Song.cpp's trackReferenceText()/resolveTrackReference()).
  // addTrack() below (the single place every track, new or loaded, enters
  // tracks_) gives an id-less track this instead of leaving it to fall
  // back to that same ugly, unstably-large raw internal id in the pattern
  // editor's own track heading. Tried in increasing order starting from 1
  // rather than deriving straight from the track's own internal id, so
  // these actually read as a small, per-song sequence instead of
  // inheriting whatever arbitrary process-wide count SongObject's shared
  // id counter happens to be at.
  std::string generateUniqueTrackId() const {
    for (int n = 1; ; n++) {
      auto candidate = "track" + std::to_string(n);
      if (!getTrackById(candidate)) return candidate;
    }
  }

  // Guards tracks_'s structural shape (addTrack() below is its only
  // mutator today) - SongState::renderBlock() runs on the audio thread and
  // reads tracks_ concurrently with the UI thread calling addTrack()
  // (PatternEditor/LaunchpadManager's various "add track" commands can
  // fire at any time, including while playing), and a push_back can
  // reallocate the vector's backing storage - a render() call
  // mid-iteration when that happens would hold a dangling iterator into
  // freed memory. Every other getTracks()-reading call site is
  // UI-thread-only, hence never concurrent with addTrack() (also always
  // UI-thread) and needs no lock of its own - see SongState::renderBlock()'s
  // own comment for the one call site that does. mutable so a const
  // Song& (SongState::renderBlock()'s own parameter type) can still lock it.
  // Heap-allocated (rather than a plain std::mutex member) solely so Song
  // itself stays move-constructible - std::mutex has neither a copy nor a
  // move constructor, which would otherwise implicitly delete Song's own
  // (tests/RenderTests.cpp's loadFixture() and similar move a freshly-
  // loaded Song out of a local variable); production code never moves a
  // Song (Controller always holds one behind a shared_ptr), so a moved-
  // from Song's now-null pointer is never dereferenced in practice.
  mutable std::unique_ptr<std::mutex> tracks_mutex_ = std::make_unique<std::mutex>();
  std::vector<Scene> scenes_;

  static inline Scene empty_scene_;
};

#endif


#ifndef _SONG_H_
#define _SONG_H_

#include "SongObject.h"
#include "Track.h"
#include "MasterTrack.h"
#include "InstrumentPool.h"
#include "Section.h"
#include "Clip.h"
#include "Version.h"
#include "../bus/BusEffectRegistry.h"
#include "../util/constants.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

class InstrumentProvider;
class Mixer;

class Song : public SongObject {
 public:
  Song(Tuning tuning = Tuning::TET31, short key = -1);

  Tuning getTuning() const { return tuning_; }
  void setTuning(Tuning tuning) { tuning_ = tuning; }

  // What tuning a Note::getValue() on `track` actually means: a GM
  // percussion key for a PercussionTrack (resolves to raw GM note
  // identity, not a pitch, whether or not it has any lanes - see
  // PercussionTrack.h), this song's own tuning otherwise. The single
  // shared definition of this check - Song.cpp's <pattern>
  // reader/writer and PatternEditor's own clipboard both need it
  // (comparing two tracks' tunings is how each of those refuses a
  // cross-tuning copy/paste, since the same raw integer means a different
  // kind of value under a different tuning).
  Tuning getTuningForTrack(const Track & track) const {
    return track.getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : tuning_;
  }

  short getKey() const { return key_note_number_; }
  void setKey(int key) { key_note_number_ = key; }
    
  short getTempo() const { return bpm_; }
  void setTempo(short bpm) { bpm_ = bpm; }

  // The number of rows `section` actually spans - its own getLengthBars()
  // converted to rows via getRowsPerBar(). The one place "how long is
  // this section" is computed; every call site that used to read the old
  // song-wide getPatternLength() as a stand-in for "the length of
  // whichever section is in play" calls this instead.
  int getEffectiveSectionLength(const Section & section) const { return section.getLengthBars() * getRowsPerBar(); }
  // Convenience overload for a call site that only has an index, not
  // already holding a Section& - getSection()'s own out-of-range sentinel
  // (empty_section_) has a real length_bars_ of its own (Section's usual
  // compiled default, same as any other section), so an out-of-range index
  // here still returns something sane rather than 0.
  int getEffectiveSectionLength(int section_idx) const { return getEffectiveSectionLength(getSection(section_idx)); }

  // The shared quantization grid (<song rowsPerBar="N">) both the
  // Launchpad Session view (LaunchpadManager::triggerClipStep())
  // and PatternEditor's own bar-boundary highlight measure against -
  // independent of a section's own length (a section can span many bars;
  // this is how many rows make just one of them). Default 16 matches this
  // codebase's own fixed "a row is a 16th note" convention
  // (ChannelConfiguration::getRowDuration()), so the default is an
  // ordinary 4/4 bar without inventing a second tempo-adjacent constant.
  int getRowsPerBar() const { return rows_per_bar_; }
  void setRowsPerBar(int rows) { rows_per_bar_ = rows > 0 ? rows : 1; }

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
  // placeholder sample rate). Used by the <bus> loading path in Song.cpp,
  // and by Controller::setBusEffectKind() (the "Set Bus Effect A/B..."
  // menu items, UI.cpp) - this only ever updates this model-side instance,
  // never an already-initialize()'d SongState's own live one (see
  // SongState::setBusEffectKind() for that half).
  void setBusSlotKind(int slot, BusEffectKind kind);

  void resetBusToDefaults() {
    setBusSlotKind(0, BusEffectKind::Reverb);
    setBusSlotKind(1, BusEffectKind::Delay);
  }

  void incVersion() { version_.incMajor(); }
  // A consumer that only cares about *structural* change (SongStructure
  // rebuilds, PatternEditor's own full-grid redraw trigger) reads this
  // instead of getVersion(), so it doesn't pay for every keystroke.
  int getMajorVersion() const { return version_.getMajor(); }

  // Note/command/velocity/delay content edits (PatternEditor.cpp's own
  // row_edited sites) - kept apart from incVersion() so structural-only
  // consumers aren't disturbed by them.
  void incMinorVersion() { version_.incMinor(); }
  int getMinorVersion() const { return version_.getMinor(); }

  // Both counters together, for a consumer that needs to know "did
  // anything at all change" (Controller::hasUnsavedChanges()).
  Version getVersion() const { return version_; }

  // The currently selected track - a single shared value belonging to
  // this Song itself, not to any one caller's own view of it, so every
  // caller sharing this Song agrees on it with no separate bookkeeping.
  // Not part of loadParameters()/storeParameters() - purely a runtime
  // editing concern, same as version_ itself. -1 means unset (no track
  // selected yet, e.g. a brand new Song).
  int getCurrentTrackId() const { return current_track_id_; }
  void setCurrentTrackId(int track_id) { current_track_id_ = track_id; }

  const std::vector<Section> & getSections() const { return sections_; }
  const Section & getSection(int i) const { return i >= 0 && i < static_cast<int>(sections_.size()) ? sections_[static_cast<size_t>(i)] : empty_section_; }
  Section & getSection(int i) { return i >= 0 && i < static_cast<int>(sections_.size()) ? sections_[static_cast<size_t>(i)] : empty_section_; }

  // getSection()'s own write-intent counterpart: grows sections_ (via addSection(),
  // repeated as needed) up to and including index i, so the caller always
  // gets back a real, distinct Section rather than getSection()'s shared,
  // process-wide empty_section_ sentinel for an out-of-range index - writing
  // into that sentinel would silently alias every other out-of-range
  // position in the whole process together, not persist as real song
  // content at all. Reserved for call sites about to *write* (note entry,
  // annotation edit, paste, insert-row, ...) - getSection() stays the one to
  // use for anything read-only (rendering, copy), which must never grow
  // the song just from being looked at. i < 0 is defensive-only (no caller
  // should ever pass one) and falls back to the same sentinel getSection()
  // would.
  Section & getOrCreateSection(int i) {
    if (i < 0) return empty_section_;
    while (static_cast<int>(sections_.size()) <= i) addSection();
    return sections_[static_cast<size_t>(i)];
  }

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
    auto floored = std::max(0, current);
    auto [ section_idx, row_in_section ] = normalizePosition(0, floored);
    auto len = getEffectiveSectionLength(section_idx);
    if (len <= 0) return std::max(0, target);
    auto pattern_start = floored - row_in_section;
    return std::clamp(target, pattern_start, pattern_start + len - 1);
  }

  // Turns a flat row count (row_idx, possibly spanning many sections) into
  // (section_idx, row_in_section) - a section-by-section cumulative walk, not a
  // uniform div/mod, since each section can have its own length. pattern_idx
  // is the section to start the walk from (almost always 0, a flat absolute
  // row - see toAbsoluteRow() below for the exact inverse). Contract:
  // row_idx >= 0 on entry, true at every call site. getEffectiveSectionLength()
  // for an out-of-range pattern_idx keeps returning a real (sentinel)
  // value forever, so this reproduces the old "run off the end into an
  // infinite series of same-length virtual sections" behavior exactly - a
  // caller that stops once pattern_idx reaches the real section count
  // (e.g. PatternEditor's own section-boundary walk) keeps working
  // unchanged.
  std::pair<int, int> normalizePosition(int pattern_idx, int row_idx) const {
    while (row_idx >= 0) {
      auto len = getEffectiveSectionLength(pattern_idx);
      if (len <= 0 || row_idx < len) break;
      row_idx -= len;
      pattern_idx++;
    }
    return { pattern_idx, row_idx };
  }

  // The exact inverse of normalizePosition(0, ...): the flat absolute row
  // for (section_idx, row_in_section) - every section before section_idx, summed,
  // plus row_in_section. The one centralized place for what several call
  // sites used to hand-roll independently as "section_idx * (the song-wide
  // pattern length) + row".
  int toAbsoluteRow(int section_idx, int row_in_section) const {
    int absolute = 0;
    for (int i = 0; i < section_idx; i++) absolute += getEffectiveSectionLength(i);
    return absolute + row_in_section;
  }

  Section & addSection(Section section) {
    incVersion();
    sections_.push_back(std::move(section));
    return sections_.back();
  }

  Section & addSection() { return addSection(Section()); }

  // The song's own flat, per-track clip list - each track's own reusable
  // Clips, available to trigger live or assign into a section from the
  // Launchpad's session/launch view, unconnected to any one section
  // position (and, once actually placed as an instance, the shared
  // content behind that placement - editing it through any instance
  // updates every other one immediately). Every caller here still
  // addresses a clip by (track_id, vector index) - it's what's physically
  // meaningful to a pad row or a single hex digit - but a *placed
  // instance* stores a clip's own stable id instead (Clip.h's own
  // comment on why); Song::addClip() is what actually assigns one.
  // Grouped by track already (rather than one flat list filtered per
  // lookup) since "this track's own clips, in order" is the only way
  // anything ever needs to read this back (Session view's own rows).
  const std::vector<Clip> & getClips(int track_id) const {
    auto it = clips_by_track_.find(track_id);
    return it != clips_by_track_.end() ? it->second : empty_clips_;
  }

  // Mutable counterpart, for editing a clip's own content in place
  // (ArrangementOps.h's own resolveEditTarget()).
  std::vector<Clip> & getClips(int track_id) {
    return clips_by_track_[track_id];
  }

  // Takes an already-built Clip (its own getLeafTrackId() says which
  // track's list it joins) rather than a bare Pattern - a clip's full/
  // eventual form is one Pattern per relevant track_id, not just the
  // leaf track's own (nested Effect automation, still unbuilt), so the
  // caller is the one place that needs to know how many Patterns went
  // into it, not this method.
  Clip & addClip(Clip clip) {
    auto & clips = clips_by_track_[clip.getLeafTrackId()];
    // Same "assign one if it doesn't already have one" convention
    // addTrack() uses (see generateUniqueTrackId()) - a clip loaded from
    // hand-written XML can already have picked its own id, same as a
    // hand-written <track id="...">; one created here at runtime (or
    // loaded from a song saved before clip ids existed at all) doesn't.
    if (clip.getId().empty()) clip.setId(generateUniqueClipId());
    clips.push_back(std::move(clip));
    incVersion();
    return clips.back();
  }

  // Mirrors generateUniqueTrackId() below - unique across every track's
  // own clip list, not just the one a new clip is about to join, same
  // "one id namespace for the whole song" convention a track's own id
  // already uses.
  std::string generateUniqueClipId() const {
    for (int n = 1; ; n++) {
      auto candidate = "clip" + std::to_string(n);
      bool taken = false;
      for (auto & [ track_id, clips ] : clips_by_track_) {
        for (auto & clip : clips) {
          if (clip.getId() == candidate) { taken = true; break; }
        }
        if (taken) break;
      }
      if (!taken) return candidate;
    }
  }

  // A section has no id of its own by default (unlike a track or a clip,
  // Song::addSection() never assigns one) - most sections never need one, so
  // most songs never carry the extra XML noise. Only once a section first
  // needs a stable identity of its own (ArrangementOps.cpp's own
  // mergeClipToBackground(), the moment a SampleTrack background bed's
  // sidecar file needs a name that survives the section later being
  // reordered/another section inserted ahead of it - an ordinal position
  // isn't stable across that, see Song.cpp's own
  // sampleBackgroundSidecarPath() comment) does anything call this and
  // assign the result via Section::setId(), mirroring generateUniqueClipId()
  // above - same "assign lazily, once, on first real need" convention.
  std::string generateUniqueSectionId() const {
    for (int n = 1; ; n++) {
      auto candidate = "section" + std::to_string(n);
      bool taken = false;
      for (auto & section : sections_) {
        if (section.getId() == candidate) { taken = true; break; }
      }
      if (!taken) return candidate;
    }
  }

  void addInstrument(std::unique_ptr<Track> i) {
    instrument_pool_.addInstrument(std::move(i));
    incVersion();
  }

  // Erases pool slot `index` (InstrumentPool::removeInstrument()) and
  // reindexes every InstrumentTrack::instrument_id_ in the tree that
  // pointed past it (decremented by one, since every later slot just
  // shifted down) or *at* it (set to -1, InstrumentPool::getByIndex()'s
  // own "nothing authored" sentinel - the instrument that track was using
  // is simply gone, the same as a never-assigned one; PercussionTrack is
  // untouched either way, since it sources from the pool's own default
  // kit, never a per-track instrument_id_ - see InstrumentPool.h's own
  // class comment). A no-op for an out-of-range index.
  void removeInstrument(int index);

  // The single way to reach the instrument list - callers wanting just
  // the indexed list go through InstrumentPool::getInstruments()/
  // getInstrument() from here rather than Song exposing its own
  // delegating shortcuts for them; this also carries the pool's own
  // resolved default drum kit (InstrumentPool::getDefaultKitInstrument())
  // for the consumers that render an actual note and need both - see
  // InstrumentPool.h's own class comment.
  const InstrumentPool & getInstrumentPool() const { return instrument_pool_; }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  // The tree parent of every top-level track - see this class's own
  // header comment. Never null.
  Track & getMasterTrack() { return *master_track_; }
  const Track & getMasterTrack() const { return *master_track_; }

  // See tracks_mutex_'s own comment - SongState::renderBlock() locks this to
  // take a quick snapshot of the current tracks before rendering them.
  std::mutex & getTracksMutex() const { return *tracks_mutex_; }

  // after_track_id: if >= 0 and it names a track actually in the tree,
  // the new track lands as its immediate sibling (wherever that track's
  // own real parent is - Track::insertChildAfter()), next to whatever the
  // artist currently has selected rather than always at the very end.
  // -1 (the default) keeps plain "append under the master" - what a
  // caller with no cursor to speak of wants (LaunchpadManager's auto-
  // grow-to-pressed-column loops, this Song's own initial construction).
  Track & addTrack(std::unique_ptr<Track> track, int after_track_id = -1) {
    if (track->getId().empty()) track->setId(generateUniqueTrackId());
    std::lock_guard<std::mutex> guard(*tracks_mutex_);
    auto * ref = track.get();
    if (after_track_id < 0 || !master_track_->insertChildAfter(after_track_id, track)) {
      master_track_->addChild(std::move(track));
    }
    incVersion();
    return *ref;
  }

  // Removes the track (root, or nested inside a <group>) whose internal id
  // is `id`. Returns false, doing nothing, if `id` doesn't resolve to any
  // track any more - callers should treat "already gone" the same as
  // "successfully gone", not as an error. Delegates to master_track_'s own
  // removeChildByInternalId(), which only ever erases from a children_
  // vector - the master itself is never anyone's child, so `id` naming the
  // master can structurally never remove it; no separate guard needed.
  // Does *not* guard against
  // removing the last remaining root track - PatternEditor::render() and
  // several sibling call sites index getRootTrackIds()[cursor.track] with
  // no bounds check at all (docs/known_bugs.md's zero-root-tracks entry),
  // so a caller that can reach zero root tracks this way must refuse
  // before ever getting here, the way PatternEditor's "delete-track"
  // command does.
  bool removeTrack(int id) {
    std::lock_guard<std::mutex> guard(*tracks_mutex_);
    if (master_track_->removeChildByInternalId(id)) {
      incVersion();
      return true;
    }
    return false;
  }

  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Every id SongStructure hands a column to, in column order - a leaf
  // track (INSTRUMENT_CONTROL/PERCUSSION_CONTROL/SAMPLE), a
  // per-track Effect wrapper, and the master track's own trailing column,
  // but never a plain Group (a pure pass-through with no column of its
  // own - see SongStructure::visit()). What PatternEditor's own columns
  // are built from - a caller that instead wants only the tracks a note
  // can actually land on (a Launchpad pad, an instrument picker) needs
  // getPlayableTrackIds() below, not this.
  std::vector<int> getRootTrackIds() const;

  // getRootTrackIds() filtered down to color-eligible tracks (every
  // LeafTrack - SongStructure::visit()'s own dynamic_cast check) - real
  // instruments/percussion/drum-machine/sample tracks a note can actually
  // land on, excluding both the master track and any per-track Effect
  // wrapper column. A Launchpad pad landing on the master's own column
  // (or an Effect's) has no note to trigger, so every position-addressed
  // Launchpad call site (a device's assigned track, the auto-grow-to-
  // pressed-column loops) uses this, not getRootTrackIds() - mirrors
  // ArrangementGrid::getVisibleTrackIds()'s own identical filter, which
  // exists for the same reason.
  std::vector<int> getPlayableTrackIds() const;

private:
  Tuning tuning_ = Tuning::TET31;
  short key_note_number_ = 0;
  int bpm_ = 90;
  int rows_per_bar_ = 16;
  float ear_height_ = constants::DEFAULT_EAR_HEIGHT;
  bool floor_reflection_enabled_ = constants::DEFAULT_FLOOR_REFLECTION_ENABLED;
  float floor_reflection_strength_ = constants::DEFAULT_FLOOR_REFLECTION_STRENGTH;
  float ground_absorption_ = constants::DEFAULT_GROUND_ABSORPTION;

  std::unique_ptr<BusEffect> bus_slot_a_, bus_slot_b_;
  BusEffectKind bus_slot_a_kind_ = BusEffectKind::Reverb;
  BusEffectKind bus_slot_b_kind_ = BusEffectKind::Delay;

  Version version_;
  int current_track_id_ = -1;

  InstrumentPool instrument_pool_;
  // The tree parent of every top-level track - see getMasterTrack()'s own
  // comment. Never null; a track can no longer *not* have a parent, which
  // is what makes "exactly one master, can't be removed" true by
  // construction rather than by a guard check (see removeTrack()'s own
  // comment).
  std::unique_ptr<Track> master_track_ = std::make_unique<MasterTrack>();

  // A track's own textual id (SongObject::getId()) is the only thing a
  // <note>/<command> element can reference it by that survives a
  // save/reload round trip - its raw internal id is just a runtime
  // counter, reassigned fresh every time a Track object is constructed,
  // so a note left referencing one is unresolvable the moment the file is
  // reopened (see Song.cpp's trackReferenceText()/resolveTrackReference()).
  // addTrack() above (the single place every track, new or loaded, enters
  // master_track_'s own children) gives an id-less track this instead of
  // leaving it to fall back to that same ugly, unstably-large raw internal
  // id in the pattern editor's own track heading. Tried in increasing
  // order starting from 1 rather than deriving straight from the track's
  // own internal id, so these actually read as a small, per-song sequence
  // instead of inheriting whatever arbitrary process-wide count
  // SongObject's shared id counter happens to be at. Never collides with
  // master_track_'s own reserved "master" id (constructor, above).
  std::string generateUniqueTrackId() const {
    for (int n = 1; ; n++) {
      auto candidate = "track" + std::to_string(n);
      if (!master_track_->getChildById(candidate)) return candidate;
    }
  }

  // The master's own parameters (currently just "collapsed") come from
  // the <tracks> element itself - see MasterTrack.h's own comment on why
  // it's never a discrete element of its own. loadParameters() resets the
  // id along with everything else (SongObject::loadParameters()), so this
  // re-asserts the reserved one every time - called both from the
  // constructor (an empty source, for a track never loaded from a file at
  // all) and from open() (the real <tracks> element).
  void loadMasterTrackParameters(const ParameterSource & input) {
    master_track_->loadParameters(input);
    master_track_->setId("master");
  }

  // Guards master_track_'s own children (addTrack()/removeTrack() above
  // are its only mutators) - SongState::renderBlock() runs on the audio
  // thread and reads them concurrently with the UI thread calling
  // addTrack() (PatternEditor/LaunchpadManager's various "add track"
  // commands can fire at any time, including while playing), and a
  // push_back can reallocate the vector's backing storage - a render()
  // call mid-iteration when that happens would hold a dangling iterator
  // into freed memory. Every other read of master_track_'s children is
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
  std::vector<Section> sections_;
  std::unordered_map<int, std::vector<Clip> > clips_by_track_;

  static inline Section empty_section_;
  static inline std::vector<Clip> empty_clips_;
};

// The sidecar .wav path a SampleTrack clip's own audio reads from/writes
// to - `<song-stem>.samples/<clip-id>.wav`, sibling to the song file
// itself. `song_filename` is relative or absolute exactly like
// Song::open()/save()'s own `filename` parameter; `clip_id` alone already
// names the file unambiguously (Song::generateUniqueClipId() is unique
// across the whole song, not just one track's own clip list). Shared by
// Song.cpp's own clip reader/writer and ArrangementOps.cpp's deleteClip()
// (orphan sidecar cleanup), so the naming convention can't drift between
// the two.
std::string sampleSidecarPath(const std::string & song_filename, const std::string & clip_id);

#endif


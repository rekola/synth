#ifndef _SONG_H_
#define _SONG_H_

#include "SongObject.h"
#include "Track.h"
#include "MasterTrack.h"
#include "InstrumentPool.h"
#include "CompiledTracks.h"
#include "TrackCompiler.h"
#include "Arrangement.h"
#include "Clip.h"
#include "ArrangementView.h"
#include "ClipView.h"
#include "PlaybackContent.h"
#include "SampleStore.h"
#include "SongSchema.h"
#include "../doc/Document.h"
#include "../doc/UndoHistory.h"
#include "Scale.h"
#include "BarGrid.h"
#include "SceneName.h"
#include "TimeSignature.h"
#include "Swing.h"
#include "Version.h"
#include "../bus/BusEffectRegistry.h"
#include "../util/constants.h"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class InstrumentProvider;
class Mixer;
class TrackCompiler;

class Song : public SongObject {
 public:
  Song(Tuning tuning = Tuning::EDO31, short key = -1);

  Tuning getTuning() const { return static_cast<Tuning>(read(songschema::kTuning)); }
  void setTuning(Tuning tuning) { write("set tuning", songschema::kTuning, static_cast<int>(tuning)); }

  // What tuning a Note::getValue() on `track` actually means: a GM
  // percussion key for a PercussionTrack (resolves to raw GM note
  // identity, not a pitch - see
  // PercussionTrack.h), this song's own tuning otherwise. The single
  // shared definition of this check - Song.cpp's <pattern>
  // reader/writer and PatternEditor's own clipboard both need it
  // (comparing two tracks' tunings is how each of those refuses a
  // cross-tuning copy/paste, since the same raw integer means a different
  // kind of value under a different tuning).
  Tuning getTuningForTrack(const Track & track) const {
    return track.getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : getTuning();
  }

  short getKey() const { return static_cast<short>(read(songschema::kKey)); }
  void setKey(int key) { write("set key", songschema::kKey, static_cast<int>(static_cast<short>(key))); }

  Scale getScale() const { return static_cast<Scale>(read(songschema::kScale)); }
  void setScale(Scale scale) { write("set scale", songschema::kScale, static_cast<int>(scale)); }

  // `count` ascending offsets from the tonic, starting at scale-degree
  // index `start_index` (0 = the tonic itself; negative or arbitrarily
  // large both work, descending/ascending into neighboring octaves as
  // needed). Register-agnostic - a caller with an actual register to place
  // these at (the Launchpad's own per-device octave setting) adds that
  // itself. Values keep climbing past the octave boundary rather than
  // wrapping back below the tonic, so the list is always strictly
  // ascending. getScale()'s own degrees (Scale.h's scaleDegreeNames(), in
  // Note::stringToKey()'s note-name syntax) are each resolved against the
  // active tuning and added to the song's tonic pitch class (getKey()), so
  // the same degree list is correct under every tuning. The degree list is
  // treated as repeating one octave higher every full cycle through it;
  // this assumes each named scale spans exactly one octave, true of every
  // scale here today. Scale::NONE falls back to the plain chromatic scale
  // - every step ascending from the tonic. Empty only for
  // Tuning::PERCUSSION (no interval structure to have degrees of).
  // With `major_if_none`, Scale::NONE reads as the major scale instead of
  // chromatic.
  std::vector<int> getScaleDegreesWindow(int start_index, int count, bool major_if_none = false) const;

  short getTempo() const { return static_cast<short>(read(songschema::kTempo)); }
  void setTempo(short bpm) { write("set tempo", songschema::kTempo, static_cast<int>(bpm)); }

  // A scene is a row of every track's clip list, identified by its
  // position there, with an optional name, tempo and time signature.
  // Launching it sets the tempo as the song tempo and the time signature
  // as the transport's bars (ClipPlayer::launchScene()).
  const std::string & getSceneName(int scene) const;
  // 0 for none.
  int getSceneTempo(int scene) const;
  // Numerator 0 for none.
  TimeSignature getSceneTimeSignature(int scene) const;
  // The bar and beat length the scene is shown and edited in: its own time
  // signature, else the one the transport is counting in (which a scene
  // without one plays in).
  int getSceneBarRows(int scene) const {
    auto signature = getSceneTimeSignature(scene);
    return (signature.isSet() ? signature : getRunningTimeSignature()).rowsPerBar();
  }
  int getSceneBeatRows(int scene) const {
    auto signature = getSceneTimeSignature(scene);
    return (signature.isSet() ? signature : getRunningTimeSignature()).rowsPerBeat();
  }
  void setSceneName(int scene, std::string name);
  void setSceneTempo(int scene, int bpm);
  void setSceneTimeSignature(int scene, TimeSignature signature);
  // Sets a scene from typed text: a "90 BPM" and a "3/4" in it become the
  // tempo and time signature (the rest the name); with none, they stay as
  // they were.
  void setSceneFromText(int scene, const std::string & text) {
    auto parsed = scenename::extract(text);
    setSceneName(scene, parsed.name);
    if (parsed.has_tempo) setSceneTempo(scene, parsed.tempo);
    if (parsed.has_time_signature) setSceneTimeSignature(scene, {parsed.numerator, parsed.denominator});
  }

  // How late the second eighth of every pair plays (swing.h), in percent of
  // the pair: 50 straight, about 67 triplet swing. Applied at playback to
  // everything scheduled, never baked into note data. Callers editing it
  // live do it inside a Song::Edit, which is how the audio thread notices.
  int getSwing() const { return read(songschema::kSwing); }
  void setSwing(int percent) { write("set swing", songschema::kSwing, swing::clamp(percent)); }

  // ---- Bars and time signatures. A row is a sixteenth note.
  //
  // The song's own time signature (<song timeSignature="3/4">, 4/4 unless
  // set): the arrangement counts its bars in it from row 0 - its grid, bar
  // accents, where a clip is placed. Callers editing it do it inside a
  // Song::Edit.
  TimeSignature getTimeSignature() const { return { read(songschema::kTimeNumerator), read(songschema::kTimeDenominator) }; }
  void setTimeSignature(TimeSignature signature);
  BarGrid getArrangementBars() const { return {getTimeSignature(), 0}; }

  // The signature a launched scene set, counted from the bar it launched
  // on (saved as transportTimeSignature/transportBarOrigin). The audio
  // thread owns it (SongState::queueSceneChange()) and the UI thread's copy
  // here is mirrored from its snapshots (Controller::
  // receivePlaybackSnapshot()), so it can trail by a frame. What playback
  // and Live launching count in (the bar a queued launch waits for, the
  // metronome, take lengths) while it is active.
  RunningBars getRunningBars() const;
  void setRunningBars(RunningBars running);
  void clearRunningBars() { setRunningBars({}); }
  // The signature a scene without one plays in.
  TimeSignature getRunningTimeSignature() const {
    auto running = getRunningBars();
    return running.isActive() ? running.signature : getTimeSignature();
  }

  // The bars in force at `row`, as the UI thread knows them.
  BarGrid getBarsAt(int row) const { return barsAt(getTimeSignature(), getRunningBars(), row); }
  int barStartAtOrBefore(int row) const { return getBarsAt(row).barStart(row); }
  int rowInBar(int row) const { return getBarsAt(row).rowInBar(row); }
  bool isBarStart(int row) const { return rowInBar(row) == 0; }
  int barRowsAt(int row) const { return getBarsAt(row).barRows(); }
  int beatRowsAt(int row) const { return getBarsAt(row).beatRows(); }
  int nextBarStart(int row) const { return getBarsAt(row).nextBarStart(row); }

  // Whether a live clip take snaps each press and release to the nearest
  // row as it's recorded. Off (the default) records the raw sub-row timing
  // in the note's delay instead; quantizeClip() can clean it up afterward.
  bool getRecordQuantize() const { return read(songschema::kRecordQuantize); }
  void setRecordQuantize(bool enabled) { write("set record quantize", songschema::kRecordQuantize, enabled); }
  // `absolute_row` as a musical position, "bar.beat.sixteenth", each
  // 1-based, in the bars the transport counts in - what the transport
  // shows, and anything else that names a position.
  std::string formatPosition(int absolute_row) const;

  // Locators: named moments of the whole song ("chorus starts here", a
  // chord's name) rather than of any one track's content, keyed by
  // absolute row - shown in the pattern editor's locator column
  // (<locators><locator row="N">name</locator>).
  const std::string & getLocator(int row) const;
  // An empty name removes the locator.
  void setLocator(int row, std::string name);
  std::map<int, std::string> getLocators() const;

  // Floor-reflection parameters (see InstrumentVoice.h) - fixed for the
  // whole song, not live-editable (no live control path exists for any
  // of these). getEarHeight() is clamped to [0.1, 50] meters at load time
  // (setEarHeight() below) - the lower bound guards against a degenerate
  // zero-height listener, the upper is an engineering ceiling (a taller
  // listener turns the reflection into an increasingly obvious slapback/
  // canyon echo rather than a fusion cue - a legitimate, if unusual,
  // effect, not something to forbid outright).
  float getEarHeight() const { return read(songschema::kEarHeight); }
  void setEarHeight(float h) { write("set ear height", songschema::kEarHeight, h < 0.1f ? 0.1f : (h > 50.0f ? 50.0f : h)); }

  bool getFloorReflectionEnabled() const { return read(songschema::kFloorReflection); }
  void setFloorReflectionEnabled(bool e) { write("set floor reflection", songschema::kFloorReflection, e); }

  float getFloorReflectionStrength() const { return read(songschema::kFloorReflectionStrength); }
  void setFloorReflectionStrength(float s) { write("set floor reflection strength", songschema::kFloorReflectionStrength, s); }

  float getGroundAbsorption() const { return read(songschema::kGroundAbsorption); }
  void setGroundAbsorption(float a) { write("set ground absorption", songschema::kGroundAbsorption, a); }

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
  const BusEffect & getBusSlot(int slot) const { return *tracks_->bus[slot == 0 ? 0 : 1].effect; }
  BusEffectKind getBusSlotKind(int slot) const { return tracks_->bus[slot == 0 ? 0 : 1].kind; }

  // Replaces a slot's occupant with a default-valued `kind`, or with
  // `parameters` (a loaded effect of that kind) when given. Only the
  // model's half: an already-running SongState's own live effect is changed
  // by SongState::setBusEffectKind().
  void setBusSlotKind(int slot, BusEffectKind kind, const BusEffect * parameters = nullptr);

  void resetBusToDefaults() {
    setBusSlotKind(0, BusEffectKind::Reverb);
    setBusSlotKind(1, BusEffectKind::Delay);
  }

  // A consumer that only cares about *structural* change (SongStructure
  // rebuilds, PatternEditor's own full-grid redraw trigger) reads this
  // instead of getVersion(), so it doesn't pay for every keystroke.
  int getMajorVersion() const { return version_.getMajor(); }

  // Note/command/velocity/delay content edits (Edit::Kind::CONTENT) are
  // counted apart from structural ones, so structural-only consumers aren't
  // disturbed by them.
  int getMinorVersion() const { return version_.getMinor(); }

  // One user action's worth of mutations. Opens before the first write and
  // bumps the version once, when the outermost scope closes (so the audio
  // thread and the widgets see the finished edit, never half of it). Scopes
  // nest freely: the inner ones only widen the outer one's kind. This is the
  // only way to bump the version, so a write path without a scope is easy
  // to spot.
  //
  // CONTENT is a note/command/velocity/delay edit (the minor counter);
  // STRUCTURE is anything else (the major counter), and wins when scopes of
  // both kinds nest.
  // How many edits of history a song keeps.
  static constexpr size_t kJournalLimit = 10000;
  static constexpr size_t kJournalSlack = 2000;

  class Edit {
   public:
    enum class Kind { CONTENT, STRUCTURE };
    // USER is something the user did and can undo. SYNC follows the audio
    // thread (a scene launch setting the tempo, a glide landing in the
    // model): journaled, but never undone on its own.
    enum class Origin { USER, SYNC };

    Edit(Song & song, const char * label, Kind kind = Kind::STRUCTURE, Origin origin = Origin::USER) : song_(song) {
      if (song_.edit_depth_++ == 0) {
	song_.edit_label_ = label;
	song_.edit_kind_ = kind;
	song_.edit_wrote_ = false;
	song_.doc_->begin(label, origin == Origin::USER);
      } else if (kind == Kind::STRUCTURE) {
	song_.edit_kind_ = Kind::STRUCTURE;
      }
    }
    ~Edit() {
      if (!discarded_) song_.edit_wrote_ = true;
      if (--song_.edit_depth_ > 0) return;
      song_.doc_->commit();
      // History is bounded; trimming in batches keeps the cost of freeing
      // what falls off (Document::collectGarbage()) rare.
      if (song_.doc_->journal().size() > kJournalLimit + kJournalSlack) {
	song_.doc_->trimJournal(kJournalLimit);
	song_.doc_->collectGarbage();
      }
      if (!song_.edit_wrote_) return;
      song_.recompileTracksIfChanged();
      // Published before the version moves, so a reader that sees the new
      // version finds the new content.
      if (song_.content_published_mode_) song_.publishContent();
      if (song_.edit_kind_ == Kind::STRUCTURE) song_.version_.incMajor();
      else song_.version_.incMinor();
    }
    Edit(const Edit &) = delete;
    Edit & operator=(const Edit &) = delete;

    // This scope wrote nothing after all (a no-op press): it adds nothing to
    // the version bump. The outermost scope still bumps if any scope inside
    // it wrote.
    void discard() { discarded_ = true; }
    // The write turned out to be structural.
    void escalate() { song_.edit_kind_ = Kind::STRUCTURE; }

   private:
    Song & song_;
    bool discarded_ = false;
  };
  bool inEdit() const { return edit_depth_ > 0; }

  // Undo and redo (doc/UndoHistory.h). Both return false when there is
  // nothing to do, and always while a live take is open (the take is one
  // undo step, taken when it ends).
  bool canUndo() const { return !doc_->inGroup() && history_.canUndo(*doc_); }
  bool canRedo() const { return !doc_->inGroup() && history_.canRedo(*doc_); }
  bool undo();
  // Where the last undo or redo changed the song: the track, and for an
  // arrangement note or placement its row, so the cursor can follow. Empty
  // fields (-1) when nothing it touched has a place.
  struct EditPlace { int track_id = -1; int row = -1; };
  EditPlace lastUndoPlace() const;
  bool redo();

  // ---- What the audio thread reads (PlaybackContent.h).
  //
  // Published mode (a song a Controller owns, rendered by the real-time
  // player): the content is rebuilt and published when an outermost
  // Song::Edit closes, so the audio thread only ever sees finished edits.
  // Otherwise (the default - a song built and rendered on one thread, as
  // the offline renderer and tests do) readContent() copies the model
  // fresh on every call, so a write needs no Edit to be heard.
  void setContentPublished(bool published) {
    content_published_mode_ = published;
    if (published) publishContent();
  }
  bool isContentPublished() const { return content_published_mode_; }
  // The document the song's state lives in (the undo layer's and tests' way
  // in; application code goes through the accessors).
  doc::Document & document() { bindImplicitScope(); return *doc_; }
  const doc::Document & document() const { bindImplicitScope(); return *doc_; }
  // The song-level values playback reads, as of now.
  SongScalars scalars() const;
  // Copies the arrangement and clips into a new PlaybackContent and
  // publishes it. UI thread.
  void publishContent() const;
  // Audio thread: the latest published content, valid for the Reader's
  // lifetime.
  ContentPublisher::Reader readContent() const {
    if (!content_published_mode_) publishContent();
    return ContentPublisher::Reader(*content_publisher_);
  }
  // Fixes the content as it is now and stops rebuilding it per read; for a
  // render that runs on one thread and does not edit the song meanwhile.
  void pinContent() const {
    publishContent();
    content_published_mode_ = true;
  }
  // True if the published content matches the model - false means a write
  // reached the model without closing a Song::Edit.
  void unpinContent() const { content_published_mode_ = false; }
  bool publishedContentIsCurrent() const;

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

  // The one timeline every track's arrangement content lives on.
  // A handle on the document's arrangement (see ArrangementView.h).
  ArrangementView getArrangement() const { return ArrangementView(context(), arrangement_node_); }

  // Rows the arrangement can address: row keys are 16-bit.
  static constexpr int kMaxArrangementRows = 65536;

  // Where the arrangement's content ends, rounded up to whole bars: its
  // last note or command, a clip placed there playing its length once, a
  // stop (at its own row), a background bed or a locator. 0 when empty.
  int getArrangementLength() const;

  // `target` clamped to the rows the arrangement can address - the
  // UI-thread edit cursor and the audio thread's own handling of the
  // position events it sends both apply this, so both sides derive the
  // same result.
  static int clampArrangementRow(int target) { return std::clamp(target, 0, kMaxArrangementRows - 1); }

  // The song's own flat, per-track clip list - each track's own reusable
  // Clips, available to trigger live or place in the arrangement from the
  // Launchpad's Live View, unconnected to any one position
  // (and, once actually placed as an instance, the shared
  // content behind that placement - editing it through any instance
  // updates every other one immediately). Every caller here still
  // addresses a clip by (track_id, vector index) - it's what's physically
  // meaningful to a pad row or a single hex digit - but a *placed
  // instance* stores a clip's own stable id instead (Clip.h's own
  // comment on why); Song::addClip() is what actually assigns one.
  // Grouped by track already (rather than one flat list filtered per
  // lookup) since "this track's own clips, in order" is the only way
  // anything ever needs to read this back (Live View's own rows).
  ClipList getClips(int track_id) const { return ClipList(context(), track_id); }

  // Every track that has a clip list (empty or not), by id.
  std::vector<int> clipTrackIds() const;

  // How many clip rows (scenes) are in use: the longest clip list across
  // tracks, not counting empty slots at its end.
  int getUsedSceneCount() const;

  // Takes an already-built Clip (its own getLeafTrackId() says which
  // track's list it joins) rather than a bare Pattern - a clip's full/
  // eventual form is one Pattern per relevant track_id, not just the
  // leaf track's own (nested Effect automation, still unbuilt), so the
  // caller is the one place that needs to know how many Patterns went
  // into it, not this method. A clip that has no id yet gets one (see
  // generateUniqueTrackId()) - a clip loaded from hand-written XML can
  // already have picked its own, one created here at runtime doesn't.
  ClipView addClip(Clip clip);

  // Places (or reuses, if one already exists) a clip at exactly this
  // index within track_id's own clip list - never retargeted to
  // "whatever the next unused position happens to be" the way addClip()
  // above always lands at the end. Scenes don't all need every track
  // populated (some instrument might genuinely not be needed for a given
  // scene), so an index arriving ahead of the list's current length pads
  // every position in between with a fresh, empty clip rather than
  // treating the gap as an error to collapse away - a hand-authored
  // `<clip/>` in the song XML is exactly this same "nothing here, on
  // purpose" state, not a special sentinel. A filler gets no id of its
  // own (unlike addClip()) - nothing ever needs to address a slot nobody
  // has actually used yet by a stable identity; whatever eventually gives
  // it real content is the one place that needs to assign it one, the
  // same "assign one if it doesn't already have one" convention addClip()
  // itself already follows. Whichever clip already sits at `index`
  // (freshly padded or not) is returned as-is, content untouched -
  // resetting it for a fresh take is the caller's own job
  // (Controller::ensureClipRecordingClip()).
  ClipView ensureClipAt(int track_id, int index);

  // Mirrors generateUniqueTrackId() below - unique across every track's
  // own clip list, not just the one a new clip is about to join, same
  // "one id namespace for the whole song" convention a track's own id
  // already uses.
  std::string generateUniqueClipId() const;

  void addInstrument(std::unique_ptr<Track> i);

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
  const InstrumentPool & getInstrumentPool() const { return *tracks_->pool; }

  bool open(const std::string & filename, const InstrumentProvider & provider);
  void save(const std::string & filename) const;

  // The tree parent of every top-level track - see this class's own
  // header comment. Never null. The objects are the compiled form of the
  // document's track nodes: read-only, shared with the audio thread, and
  // replaced by new ones when a track is edited, so look one up again after
  // an edit rather than keeping it (edits go through editTrack() and the
  // other mutators below).
  const Track & getMasterTrack() const { return *tracks_->master; }
  // This thread's current compiled tracks, for something that has to keep
  // them alive.
  std::shared_ptr<const CompiledTracks> compiledTracks() const { return tracks_; }

  // after_track_id: if >= 0 and it names a track actually in the tree,
  // the new track lands as its immediate sibling (wherever that track's
  // own real parent is), next to whatever the artist currently has
  // selected rather than always at the very end. -1 (the default) keeps
  // plain "append under the master" - what a caller with no cursor to
  // speak of wants (LaunchpadManager's auto-grow-to-pressed-column loops,
  // this Song's own initial construction). Returns the compiled track
  // (the object that was passed in, now shared and read-only).
  const Track & addTrack(std::unique_ptr<Track> track, int after_track_id = -1);

  // Removes the track (root, or nested inside a <group>) whose internal id
  // is `id`. Returns false, doing nothing, if `id` doesn't resolve to any
  // track any more - callers should treat "already gone" the same as
  // "successfully gone", not as an error. The master itself is never
  // anyone's child, so `id` naming it can structurally never remove it.
  // Does *not* guard against removing the last remaining root track -
  // PatternEditor::render() and several sibling call sites index
  // getRootTrackIds()[cursor.track] with no bounds check at all
  // (docs/known_bugs.md's zero-root-tracks entry), so a caller that can
  // reach zero root tracks this way must refuse before ever getting here,
  // the way PatternEditor's "delete-track" command does.
  bool removeTrack(int id);

  // Changes a track's own settings (mute, sends, position, name, ...): `edit`
  // is run on a scratch copy of the track and whatever it changed is written
  // to the track's node. Sub-tracks are not touched. False if `track_id`
  // names no track.
  bool editTrack(int track_id, const std::function<void(Track &)> & edit);

  // Compiles the document's track nodes into new objects now, preparing
  // instruments that need it with `provider`. An edit does this by itself
  // when it closes; Song::open() calls it with the provider at hand.
  void compileTracks(const InstrumentProvider * provider = nullptr);

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
  // The song-level state lives in the document, on the root node (scalars)
  // and its scenes/locators slots; these accessors are the typed view of it.
  // A write opens its own Edit, which joins the caller's if there is one.
  template <typename T>
  T read(const doc::Prop<T> & prop) const { return doc::get(*doc_, doc_->root(), prop); }
  template <typename T>
  void write(const char * label, const doc::Prop<T> & prop, T value) {
    Edit edit(*this, label);
    doc::set(*doc_, doc_->root(), prop, value);
  }
  int sceneCount() const;
  void clearScenes();
  doc::NodeId sceneNode(int scene) const;
  doc::NodeId ensureSceneNode(int scene);  // kNoNode for a negative scene
  size_t locatorIndex(int row) const;      // first locator at or after `row`
  mutable std::unique_ptr<doc::Document> doc_ = std::make_unique<doc::Document>();
  // The audio behind the score's "sample" nodes. A pointer so views can keep
  // theirs when the Song moves.
  mutable std::unique_ptr<SampleStore> samples_ = std::make_unique<SampleStore>();
  doc::NodeId arrangement_node_ = doc::kNoNode;
  // Makes a write that arrives with no Edit open an Edit of its own, so it
  // is journaled and published like any other.
  struct ImplicitEdit : doc::Document::ImplicitScope {
    Song * song = nullptr;
    std::optional<Edit> edit;
    void begin() override { edit.emplace(*song, "edit"); }
    void end() override { edit.reset(); }
  };
  doc::UndoHistory history_;
  mutable std::unique_ptr<ImplicitEdit> implicit_edit_ = std::make_unique<ImplicitEdit>();
  // What the score's views are built from; also (re)binds the implicit
  // edit to this Song, so a moved Song keeps working.
  ScoreContext context() const {
    bindImplicitScope();
    return { doc_.get(), samples_.get() };
  }
  void bindImplicitScope() const {
    implicit_edit_->song = const_cast<Song *>(this);
    doc_->setImplicitScope(implicit_edit_.get());
  }
  // The arrangement and clips as plain values, for the published copy.
  std::unique_ptr<PlaybackContent> compileContent() const;

  Version version_;
  mutable bool content_published_mode_ = false;
  mutable std::unique_ptr<ContentPublisher> content_publisher_ = std::make_unique<ContentPublisher>();
  int edit_depth_ = 0;
  const char * edit_label_ = "";
  Edit::Kind edit_kind_ = Edit::Kind::STRUCTURE;
  bool edit_wrote_ = false;
  int current_track_id_ = -1;

  // The tracks, instrument pool and bus live in the document as nodes
  // (tracknodes::, TrackNodes.h); tracks_ is what they compile to, replaced
  // whenever a node under these roots has changed.
  // Where they sit under the root, read from the document each time (undo
  // can put a different node in a slot).
  doc::NodeId masterNode() const;
  doc::NodeId poolNode() const;
  doc::NodeId busNode(int slot) const;
  std::shared_ptr<TrackCompiler> compiler_;
  std::shared_ptr<const CompiledTracks> tracks_;
  TrackCompiler::Stamp tracks_stamp_;
  uint64_t tracks_generation_ = 0;
  void recompileTracksIfChanged();
  doc::NodeId trackNode(int track_id) const;  // kNoNode if there is no such track
  doc::NodeId insertTrackNode(const std::shared_ptr<Track> & track, doc::NodeId parent, const std::string & slot, size_t index);

  // A track's own textual id (SongObject::getId()) is the only thing a
  // <note>/<command> element can reference it by that survives a
  // save/reload round trip - its raw internal id is just a runtime
  // counter, reassigned fresh every time a Track object is constructed,
  // so a note left referencing one is unresolvable the moment the file is
  // reopened (see Song.cpp's trackReferenceText()/resolveTrackReference()).
  // addTrack() above (the single place every track, new or loaded, enters
  // the tree) gives an id-less track this instead of
  // leaving it to fall back to that same ugly, unstably-large raw internal
  // id in the pattern editor's own track heading. Tried in increasing
  // order starting from 1 rather than deriving straight from the track's
  // own internal id, so these actually read as a small, per-song sequence
  // instead of inheriting whatever arbitrary process-wide count
  // SongObject's shared id counter happens to be at. Never collides with
  // the master track's own reserved "master" id.
  std::string generateUniqueTrackId() const;
  std::string generateUniqueInstrumentId() const;
};

// The sidecar .wav path one layer of a SampleTrack clip's own audio reads
// from/writes to - `<song-stem>.samples/<clip-id>.wav` for layer 0,
// `<song-stem>.samples/<clip-id>_<layer_index + 1>.wav` for every later
// overdub layer, sibling to the song file itself. `song_filename` is
// relative or absolute exactly like Song::open()/save()'s own `filename`
// parameter; `clip_id` alone already names layer 0's own file
// unambiguously (Song::generateUniqueClipId() is unique across the whole
// song, not just one track's own clip list) - layer 0 keeps the plain,
// suffix-less name a single-layer clip has always used, so an existing
// song's sidecar files don't get renamed out from under it the moment
// this function gained multi-layer support. Used by Song.cpp's own clip
// reader/writer.
std::string sampleSidecarPath(const std::string & song_filename, const std::string & clip_id, int layer_index);

#endif


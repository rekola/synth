#ifndef _ARRANGEMENT_H_
#define _ARRANGEMENT_H_

#include "Pattern.h"
#include "SampleContent.h"
#include "VisibleTrackInfo.h"

#include <map>
#include <string>
#include <vector>
#include <unordered_map>

// The song's arrangement: one continuous timeline of per-track content
// keyed by absolute row. Three kinds live at a row: instance events
// (instances_by_track_id_ - which Clip, if any, starts playing there, or an
// explicit stop; ArrangementOps.h), a track's own inline Pattern
// (patterns_by_track_id_ - its note/command content, never shared, unlike
// a Clip's), and a SampleTrack's background bed
// (sample_backgrounds_by_track_id_ - audio from row 0 on). Notes about a
// moment in the song are its locators (Song::getLocators()).
//
// The row+track_id-keyed accessors delegate to the track's Pattern,
// creating it on first use; getPatternsByTrack() is for the callers that
// need every track's content at once.
class Arrangement {
 public:
  // Resolves `row` against `track_id`'s own Pattern length (Pattern.h's
  // own getEffectiveRow() comment - a Pattern shorter than
  // `context_length` repeats). A caller with a raw, on-screen/playback
  // row and only a track_id (not already holding that track's own
  // Pattern reference, e.g. PatternEditor.cpp's note-entry call sites,
  // LaunchpadManager.cpp's own) calls this once before reading/writing
  // through this class's own row+track_id-keyed wrappers below - a track
  // with no Pattern here yet resolves to `row` unchanged (an absent
  // Pattern's implicit length_ is 0, same as an explicit one).
  int getEffectiveRow(int track_id, int row, int context_length) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getEffectiveRow(row, context_length) : row;
  }

  void setNotes(int row, int track_id, const std::vector<Note> & n) {
    patterns_by_track_id_[track_id].setNotes(row, n);
  }

  void setNote(int row, int track_id, int note_column, Note note) {
    patterns_by_track_id_[track_id].setNote(row, note_column, note);
  }

  int pushNote(int row, int track_id, Note note) {
    return patterns_by_track_id_[track_id].pushNote(row, note);
  }

  void clearNotes(int row, int track_id) {
    auto it = patterns_by_track_id_.find(track_id);
    if (it != patterns_by_track_id_.end()) it->second.clearNotes(row);
  }

  void deleteNote(int row, int track_id, int column) {
    auto it = patterns_by_track_id_.find(track_id);
    if (it != patterns_by_track_id_.end()) it->second.deleteNote(row, column);
  }

  // Shifts just this one track's own Pattern (notes and command together,
  // Pattern::insertRow()'s own contract), leaving every other track
  // untouched.
  void insertRowForTrack(int track_id, int row, int num_rows) {
    patterns_by_track_id_[track_id].insertRow(row, num_rows);
  }

  const Note & getNote(int row, int track_id, int note_column) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getNote(row, note_column) : empty_note;
  }

  const std::vector<Note> & getNotes(int row, int track_id) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getNotes(row) : empty_notes;
  }

  // Column-0 shorthand - see Pattern::setCommand(row, Command)'s own
  // comment for why every existing (single-command) call site keeps this
  // bare form rather than needing an explicit column argument.
  void setCommand(int row, int track_id, Command command) {
    patterns_by_track_id_[track_id].setCommand(row, command);
  }

  void setCommand(int row, int track_id, int command_column, Command command) {
    patterns_by_track_id_[track_id].setCommand(row, command_column, command);
  }

  int pushCommand(int row, int track_id, Command command) {
    return patterns_by_track_id_[track_id].pushCommand(row, command);
  }

  const Command & getCommand(int row, int track_id) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getCommand(row) : empty_command;
  }

  const Command & getCommand(int row, int track_id, int command_column) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getCommand(row, command_column) : empty_command;
  }

  const std::vector<Command> & getCommandsAt(int row, int track_id) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getCommandsAt(row) : empty_commands;
  }

  void getTrackInformation(std::unordered_map<int, VisibleTrackInfo> & track_info) const {
    for (auto & [ track_id, pattern ] : patterns_by_track_id_) {
      pattern.updateSubtrackInfo(track_info[track_id]);
    }
  }

  // Raw per-track access - see this class's own doc comment above for why
  // this exists alongside the row+track_id-keyed wrappers rather than
  // instead of them.
  const std::unordered_map<int, Pattern> & getPatternsByTrack() const { return patterns_by_track_id_; }
  std::unordered_map<int, Pattern> & getPatternsByTrack() { return patterns_by_track_id_; }

  // Replaces track_id's whole Pattern in one shot (a deep copy - Pattern
  // is a plain value) - for callers swapping in an entire pattern at once,
  // rather than the row+track_id-keyed wrappers above, which only ever
  // touch one row at a time.
  void setPatternForTrack(int track_id, Pattern pattern) {
    patterns_by_track_id_[track_id] = std::move(pattern);
  }

  // The arrangement layer - instance events, one per (track, row) where
  // something was actually placed. An instance event is a tracker-idiom
  // *start* event, not a span: either a real clip (its own id - Clip.h's
  // own comment on why an id, not its ordinal position in the track's
  // clip list) or an explicit stop ("OFF") - "instantiate nothing," not a
  // separate kind of object. Lives here, not on Pattern - this doesn't
  // belong to any one track's own Pattern. An empty string (never
  // actually stored - only ever a getInstance() return value, this
  // class's own empty_string sentinel below) means no event was placed at
  // that exact row at all.
  //
  // kStopInstance/kNoInstance are ArrangementOps.h's own resolveInstanceAt()
  // - not this storage layer's - the *resolved* answer at some row is
  // still a real clip's current position in Song::getClips(track_id) (an
  // int, what a pad row or a hex digit actually addresses), an explicit
  // stop, or nothing at all; these two sentinels stand in for the latter
  // two there, distinct from a real (always >= 0) position.
  static constexpr int kStopInstance = -1;
  static constexpr int kNoInstance = -2;

  void setInstance(int track_id, int row, const std::string & clip_id) {
    instances_by_track_id_[track_id][static_cast<unsigned short>(row)] = clip_id;
  }

  void clearInstance(int track_id, int row) {
    auto it = instances_by_track_id_.find(track_id);
    if (it != instances_by_track_id_.end()) it->second.erase(static_cast<unsigned short>(row));
  }

  const std::string & getInstance(int track_id, int row) const {
    auto it = instances_by_track_id_.find(track_id);
    if (it == instances_by_track_id_.end()) return empty_string;
    auto it2 = it->second.find(static_cast<unsigned short>(row));
    return it2 != it->second.end() ? it2->second : empty_string;
  }

  // Raw per-track access, same reasoning as getPatternsByTrack() above - placement's own clearing logic and
  // playback's own row resolution both need "every instance event on
  // this one track, in row order" at once, not a single (row, track)
  // cell at a time. Ordered (std::map, not this class's usual
  // unordered_map) because both of those actually are range queries -
  // "the most recent event at or before row R" (resolution), "every
  // event at or after row R" (clearing) - not just point lookups by
  // exact row the way notes/commands above always are.
  const std::map<unsigned short, std::string> & getInstancesForTrack(int track_id) const {
    auto it = instances_by_track_id_.find(track_id);
    return it != instances_by_track_id_.end() ? it->second : empty_instances_;
  }

  // Every track that has any instance events at all, for Song.cpp's own
  // XML writer to walk - same shape/reasoning as getPatternsByTrack()
  // above.
  const std::unordered_map<int, std::map<unsigned short, std::string> > & getInstancesByTrack() const { return instances_by_track_id_; }

  // A SampleTrack's own background bed - the sample-content sibling of
  // patterns_by_track_id_ above, but raw mixed audio starting at row 0:
  // content merged into the timeline (ArrangementOps.h's own
  // mergeClipToBackground()), summed against whatever else plays over it.
  // Unlike a Clip's own SampleContent, this one is never trimmed
  // (trimming already happened before whatever merge wrote it) and never
  // loops - both fields simply stay at their unset defaults; only the
  // buffer/native sample rate are ever written. Read-only lookup returns
  // nullptr when this track has no real background audio yet (absent
  // entirely, or present with no buffer - the same "not really there"
  // test Clip::hasSample() uses), so a mere read never has to special-
  // case a freshly-default-constructed, still-empty SampleContent.
  const SampleContent * getSampleBackgroundContent(int track_id) const {
    auto it = sample_backgrounds_by_track_id_.find(track_id);
    return it != sample_backgrounds_by_track_id_.end() && it->second.getBuffer() ? &it->second : nullptr;
  }

  // The write-intent counterpart - creates track_id's own (still-empty)
  // entry on first use, the same lazy-creation-on-write shape
  // patterns_by_track_id_'s own row/command setters already have via
  // operator[].
  SampleContent & getOrCreateSampleBackgroundContent(int track_id) { return sample_backgrounds_by_track_id_[track_id]; }

  // Raw per-track access, same reasoning as getPatternsByTrack()/
  // getInstancesByTrack() above - Song.cpp's own XML writer and
  // SongState.h's own scheduler both need "every track with a background
  // bed here" at once, not a single track_id at a time.
  const std::unordered_map<int, SampleContent> & getSampleBackgroundsByTrack() const { return sample_backgrounds_by_track_id_; }

private:
  std::unordered_map<int, Pattern> patterns_by_track_id_;
  std::unordered_map<int, std::map<unsigned short, std::string> > instances_by_track_id_;
  std::unordered_map<int, SampleContent> sample_backgrounds_by_track_id_;

  static inline Note empty_note;
  static inline std::vector<Note> empty_notes;
  static inline Command empty_command;
  static inline std::vector<Command> empty_commands;
  static inline std::string empty_string;
  static inline std::map<unsigned short, std::string> empty_instances_;
};

#endif

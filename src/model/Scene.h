#ifndef _SCENE_H_
#define _SCENE_H_

#include "SongObject.h"
#include "Pattern.h"
#include "VisibleTrackInfo.h"

#include <map>
#include <string>
#include <vector>
#include <unordered_map>

// One point in the song: the Pattern (Pattern.h - one track's own note/
// command content) each track that has anything here uses, plus this
// scene's own row-keyed annotations. Annotations are a note about a moment
// in the song ("chorus starts here"), not about any one track's musical
// content, so they live here rather than on Pattern - nothing about them
// interacts with per-track content at all, they're just keyed by row.
//
// Every row+track_id-keyed accessor here (setNote/getNote/setCommand/
// getCommand/...) is a thin wrapper delegating to the right per-track
// Pattern, creating it on first use - this is what lets PatternEditor.cpp/
// PatternBlockOps.cpp address "row R, track T" directly without needing to
// know that a track's content is actually its own separate Pattern object
// underneath. getPatternsByTrack() is the escape hatch for the two call
// sites (SongState.h's note scheduler, Song.cpp's XML writer) that
// genuinely need "every track's content here" at once rather than one
// (row, track) cell at a time - iterating the real per-track map directly,
// rather than synthesizing a row->track_id->notes view that would just
// have to rebuild the same grouping this class already does.
class Scene : public SongObject {
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

  // Whole-row, not single-track: every track's own Pattern (notes and
  // command alike - Pattern::insertRow() shifts both together) shifts in
  // lockstep, plus this scene's own row-keyed annotation. A row is one
  // moment in the whole song, not a per-track thing, so shifting it here
  // has to mean all of it moving together or the tracks would drift out
  // of alignment with each other - unlike insertRowForTrack() below,
  // which deliberately shifts just one track's own content and leaves
  // every other track's own alignment with the song's own row numbers
  // exactly as it was.
  void insertRow(int row, int num_rows) {
    for (auto & [ track_id, pattern ] : patterns_by_track_id_) pattern.insertRow(row, num_rows);
    for (int i = num_rows - 1; i > row; i--) shiftAnnotation(i, i - 1);
    annotations_.erase(static_cast<unsigned short>(row));
  }

  void deleteRow(int row, int num_rows) {
    for (auto & [ track_id, pattern ] : patterns_by_track_id_) pattern.deleteRow(row, num_rows);
    for (int i = row; i < num_rows - 1; i++) shiftAnnotation(i, i + 1);
    annotations_.erase(static_cast<unsigned short>(num_rows - 1));
  }

  // Single-track counterpart of insertRow() above - shifts just this one
  // track's own Pattern (notes and command together, Pattern::
  // insertRow()'s own contract), leaving every other track and the row's
  // own annotation (not this one track's own content) untouched.
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

  void setCommand(int row, int track_id, Command command) {
    patterns_by_track_id_[track_id].setCommand(row, command);
  }

  const Command & getCommand(int row, int track_id) const {
    auto it = patterns_by_track_id_.find(track_id);
    return it != patterns_by_track_id_.end() ? it->second.getCommand(row) : empty_command;
  }

  void getTrackInformation(std::unordered_map<int, VisibleTrackInfo> & track_info) const {
    for (auto & [ track_id, pattern ] : patterns_by_track_id_) {
      pattern.updateSubtrackInfo(track_info[track_id]);
    }
  }

  void setAnnotation(int row, std::string a) {
    annotations_[static_cast<unsigned short>(row)] = std::move(a);
  }

  const std::string & getAnnotation(int row) const {
    auto it = annotations_.find(static_cast<unsigned short>(row));
    return it != annotations_.end() ? it->second : empty_string;
  }

  const std::unordered_map<unsigned short, std::string> & getAnnotations() const { return annotations_; }

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
  // separate kind of object. Lives here, not on Pattern, for the same
  // reason annotations do (Scene's own class comment) - this doesn't
  // belong to any one track's own Pattern either. An empty string (never
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

  // Raw per-track access, same reasoning as getPatternsByTrack()/
  // getAnnotations() above - placement's own clearing logic and
  // playback's own row resolution both need "every instance event on
  // this one track, in row order" at once, not a single (row, track)
  // cell at a time. Ordered (std::map, not this class's usual
  // unordered_map) because both of those actually are range queries -
  // "the most recent event at or before row R" (resolution), "every
  // event at or after row R" (clearing) - not just point lookups by
  // exact row the way notes/commands/annotations above always are.
  const std::map<unsigned short, std::string> & getInstancesForTrack(int track_id) const {
    auto it = instances_by_track_id_.find(track_id);
    return it != instances_by_track_id_.end() ? it->second : empty_instances_;
  }

  // Every track that has any instance events at all, for Song.cpp's own
  // XML writer to walk - same shape/reasoning as getPatternsByTrack()
  // above.
  const std::unordered_map<int, std::map<unsigned short, std::string> > & getInstancesByTrack() const { return instances_by_track_id_; }

private:
  // insertRow()/deleteRow()'s own annotation-shifting step - same "copy if
  // present, else erase rather than store an explicit empty string" shape
  // as Pattern::shiftCommand(), so a row with no annotation stays absent
  // from annotations_ rather than accumulating empty entries.
  void shiftAnnotation(int dst_row, int src_row) {
    auto it = annotations_.find(static_cast<unsigned short>(src_row));
    if (it != annotations_.end()) annotations_[static_cast<unsigned short>(dst_row)] = it->second;
    else annotations_.erase(static_cast<unsigned short>(dst_row));
  }

  std::unordered_map<int, Pattern> patterns_by_track_id_;
  std::unordered_map<unsigned short, std::string> annotations_;
  std::unordered_map<int, std::map<unsigned short, std::string> > instances_by_track_id_;

  static inline Note empty_note;
  static inline std::vector<Note> empty_notes;
  static inline Command empty_command;
  static inline std::string empty_string;
  static inline std::map<unsigned short, std::string> empty_instances_;
};

#endif

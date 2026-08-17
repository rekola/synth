#ifndef _SCENE_H_
#define _SCENE_H_

#include "SongObject.h"
#include "Pattern.h"
#include "VisibleTrackInfo.h"

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
  // lockstep, plus this scene's own row-keyed annotation - matching Emacs's
  // own kill-line/C-k, which acts on the whole line regardless of any
  // narrower selection. A row is one moment in the whole song, not a
  // per-track thing, so "insert/delete a row" has to mean all of it moving
  // together or the tracks would drift out of alignment with each other.
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

  static inline Note empty_note;
  static inline std::vector<Note> empty_notes;
  static inline Command empty_command;
  static inline std::string empty_string;
};

#endif

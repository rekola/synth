#ifndef _ARRANGEMENTVIEW_H_
#define _ARRANGEMENTVIEW_H_

#include "Arrangement.h"
#include "ClipView.h"
#include "PatternView.h"
#include "ScoreContext.h"
#include "ScoreSchema.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// The arrangement as it is in the document: the live counterpart of the
// value class Arrangement, which it replaces for anything that edits or
// displays the song. A handle, not content - see PatternView. Reads return
// values; a write to a track that has no pattern yet creates one, as the
// value class does.
class ArrangementView {
 public:
  ArrangementView() = default;
  ArrangementView(ScoreContext context, doc::NodeId node) : context_(context), node_(node) { }

  bool valid() const { return context_.document->get(node_) != nullptr; }
  doc::NodeId node() const { return node_; }
  // Reads like the pointer to an arrangement it replaces - see PatternView.
  ArrangementView * operator->() { return this; }
  const ArrangementView * operator->() const { return this; }

  // ---- A track's own (background) pattern.
  int getEffectiveRow(int track_id, int row, int context_length) const;
  void setNotes(int row, int track_id, const std::vector<Note> & notes) { patternFor(track_id).setNotes(row, notes); }
  void setNote(int row, int track_id, int note_column, Note note) { patternFor(track_id).setNote(row, note_column, note); }
  int pushNote(int row, int track_id, Note note) { return patternFor(track_id).pushNote(row, note); }
  void clearNotes(int row, int track_id);
  void deleteNote(int row, int track_id, int column);
  void insertRowForTrack(int track_id, int row, int num_rows) { patternFor(track_id).insertRow(row, num_rows); }
  Note getNote(int row, int track_id, int note_column) const;
  std::vector<Note> getNotes(int row, int track_id) const;
  void setCommand(int row, int track_id, Command command) { patternFor(track_id).setCommand(row, command); }
  void setCommand(int row, int track_id, int command_column, Command command) { patternFor(track_id).setCommand(row, command_column, command); }
  int pushCommand(int row, int track_id, Command command) { return patternFor(track_id).pushCommand(row, command); }
  Command getCommand(int row, int track_id) const;
  Command getCommand(int row, int track_id, int command_column) const;
  std::vector<Command> getCommandsAt(int row, int track_id) const;
  void getTrackInformation(std::unordered_map<int, VisibleTrackInfo> & track_info) const;

  // The track's pattern, created if it has none yet / invalid if it has none.
  PatternView patternFor(int track_id);
  PatternView findPattern(int track_id) const;
  // Every track that has a pattern, with its handle.
  std::map<int, PatternView> getPatternsByTrack() const;
  void setPatternForTrack(int track_id, const Pattern & pattern) { patternFor(track_id).assign(pattern); }

  // ---- Placed clips: which clip starts where, or an explicit stop.
  static constexpr int kStopInstance = Arrangement::kStopInstance;
  static constexpr int kNoInstance = Arrangement::kNoInstance;
  void setInstance(int track_id, int row, const std::string & clip_id);
  void clearInstance(int track_id, int row);
  const std::string & getInstance(int track_id, int row) const;
  std::map<unsigned short, std::string> getInstancesForTrack(int track_id) const;
  std::map<int, std::map<unsigned short, std::string> > getInstancesByTrack() const;

  // ---- A SampleTrack's background bed.
  const SampleContent * getSampleBackgroundContent(int track_id) const;
  SampleLayerView sampleBackground(int track_id) const;
  // Makes `content` the track's bed (replacing any).
  void setSampleBackground(int track_id, const SampleContent & content);
  std::map<int, SampleContent> getSampleBackgroundsByTrack() const;

  // The plain value, for the published copy.
  Arrangement toArrangement() const;

  // A detached "arrangement" node with its empty slots.
  static doc::NodeId create(doc::Document & document);

 private:
  const std::vector<doc::NodeId> & ids(const char * slot) const;
  size_t patternIndex(int track_id) const;      // first pattern at or after `track_id`
  size_t backgroundIndex(int track_id) const;
  size_t instanceIndex(int track_id, int row) const;

  ScoreContext context_;
  doc::NodeId node_ = doc::kNoNode;
};

#endif

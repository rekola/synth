#ifndef _SCENEPATTERNSOURCE_H_
#define _SCENEPATTERNSOURCE_H_

#include "PatternSource.h"

#include <string>
#include <unordered_map>

class Controller;
class Song;

// Clip editing, one scene at a time: blocks are scenes - clip-list index k
// across every track - and a cell shows that track's own clip at k, rows
// counted from the clip's start. A scene is as long as its longest clip;
// shorter looping clips repeat, one-shots end. Empty scenes follow the
// used ones (sceneCount()), to create clips in. The cursor is this
// source's own, independent of the transport and of every playhead, and
// remembered per buffer. Effect commands and block operations act on the
// clips too (SceneGrid).
class ScenePatternSource : public PatternSource {
 public:
  explicit ScenePatternSource(Controller & controller) : controller_(controller) { }

  RowAddress cursor() const override;
  void moveCursor(int delta_rows) override;
  void setCursor(RowAddress address);

  RowAddress normalize(int block, int row) const override;
  int blockCount() const override;
  int blockLength(int block) const override;

  ReadTarget read(int track_id, RowAddress address) const override;
  EditTarget edit(int track_id, RowAddress address) override;
  std::unique_ptr<const PatternGrid> readGrid(RowAddress anchor) const override;
  std::unique_ptr<PatternGrid> editGrid(RowAddress anchor, bool create) override;
  // A scene has one source per track: its clip.
  std::pair<int, int> sourceRows(int, RowAddress anchor) const override { return { 0, blockLength(anchor.block) - 1 }; }

  void collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const override;

  const Section * annotations(int) const override { return nullptr; }
  Section * annotations(int, bool) override { return nullptr; }

  void insertRow(int track_id, RowAddress address) override;
  bool hasInstance(int, RowAddress) const override { return false; }
  bool stopInstance(int, RowAddress) override { return false; }
  const SampleContent * sampleBackground(int, int) const override { return nullptr; }
  std::optional<int> playheadRow(int track_id, int block) const override;

  bool showsClipIndirection() const override { return false; }
  bool hasAnnotations() const override { return false; }
  bool cursorFollowsTransport() const override { return false; }

  // Scenes shown: every used one plus one empty one to create clips in,
  // never fewer than a Launchpad grid's 8 rows. Shared with the clip grid,
  // so both always offer the same scenes.
  static int sceneCount(const Song & song);

  // Where each track's launched clip is playing: its scene and row.
  struct Playhead { int scene; int row; };
  void setPlayheads(std::unordered_map<int, Playhead> playheads) { playheads_ = std::move(playheads); }

 private:
  // Always the active buffer's song - it changes when the buffer does.
  Song & song() const;
  RowAddress clamp(RowAddress address) const;

  Controller & controller_;
  // Keyed by buffer name.
  mutable std::unordered_map<std::string, RowAddress> cursors_;
  std::unordered_map<int, Playhead> playheads_;
};

#endif

#ifndef _SCENEPATTERNSOURCE_H_
#define _SCENEPATTERNSOURCE_H_

#include "PatternSource.h"

#include <string>
#include <unordered_map>

class Controller;
class Song;

// Clip editing: blocks are scenes - clip-list index k across every track -
// and a cell shows that track's own clip at k, rows counted from the
// clip's start. A scene is as long as its longest clip; shorter looping
// clips repeat, one-shots end. Empty scenes follow the used ones
// (sceneCount()), to create clips in.
//
// Each track has its own position - a scene and a row in it. A track
// playing a launched clip is at its playhead, which can't be moved; a
// stopped one is wherever it was left (where playback left it, or where
// it was moved to), remembered per buffer. The cursor is the cursor
// track's position, and every other track's rows are shown relative to
// it: the editor's row that is the cursor row shows each track at its own
// position, the next row each track's next row, and so on, so one screen
// row can show different scenes in different columns. When the cursor is
// moved, every track that isn't playing moves along by as much, so the
// stopped columns stay put while the cursor row moves across them; a
// playhead never moves another track. A playing track other than the
// cursor's shows its playhead on a line of its own, offset from the
// cursor row, which stays put as the cursor moves - the playing column
// scrolls under that line instead. Effect commands and block operations
// resolve each track's rows the same way.
class ScenePatternSource : public PatternSource {
 public:
  explicit ScenePatternSource(Controller & controller) : controller_(controller) { }

  RowAddress cursor() const override;
  void moveCursor(int delta_rows) override;
  void setCursorTrack(int track_id) override;
  RowAddress trackCursor(int track_id) const override { return advance(cursor(), offset(track_id)); }
  bool cursorLocked() const override { return isPlaying(cursor_track_id_); }
  std::optional<int> trackBlock(int track_id) const override { return position(track_id).block; }
  // Moves the cursor track (or `track_id`) to `address`, unless it's
  // playing.
  void setCursor(RowAddress address) { setTrackPosition(cursor_track_id_, address); }
  void setTrackPosition(int track_id, RowAddress address);

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

  std::optional<int> locatorRow(RowAddress) const override { return std::nullopt; }

  void insertRow(int track_id, RowAddress address) override;
  bool hasInstance(int, RowAddress) const override { return false; }
  bool stopInstance(int, RowAddress) override { return false; }
  const SampleContent * sampleBackground(int, int) const override { return nullptr; }
  // A playing track's playhead is always at its position, shown on its
  // own line (trackCursor()).
  std::optional<int> playheadRow(int track_id, int block) const override;

  bool showsClipIndirection() const override { return false; }
  bool hasAnnotations() const override { return false; }
  bool cursorFollowsTransport() const override { return false; }

  // Scenes shown: every used one plus one empty one to create clips in,
  // never fewer than a Launchpad grid's 8 rows. Shared with the clip grid,
  // so both always offer the same scenes.
  static int sceneCount(const Song & song);

  // Where each track's launched clip is playing: its scene and row. A
  // track whose playhead goes away stays as it's shown - its position the
  // row shown at the cursor row.
  struct Playhead { int scene; int row; };
  void setPlayheads(std::unordered_map<int, Playhead> playheads);
  // Brings every playing track's playhead line (other than the cursor
  // track's) back within `margin` rows of the edges of the `rows` rows
  // starting at `top`, when it has left them. True if any moved.
  bool keepPlayheadsVisible(RowAddress top, int rows, int margin);

  // `address`, a row of the editor (the cursor track's), as `track_id`'s
  // own: its position moved by as many rows as `address` is from the
  // cursor. Before the first scene the row is negative (of scene 0); past
  // the last one the block is blockCount().
  RowAddress trackAddress(int track_id, RowAddress address) const override;

 private:
  // Always the active buffer's song - it changes when the buffer does.
  Song & song() const;
  RowAddress clamp(RowAddress address) const;
  bool isPlaying(int track_id) const;
  RowAddress position(int track_id) const;
  std::unordered_map<int, RowAddress> & positions() const;
  // Moves every stopped track but the cursor's by `rows`, keeping each
  // one within the scenes.
  void moveStoppedTracks(int rows);
  // A playing track's playhead line, in rows from the cursor row; 0 for
  // any other track.
  int offset(int track_id) const;

  Controller & controller_;
  int cursor_track_id_ = -1;
  // Stopped tracks' positions, per buffer name.
  mutable std::unordered_map<std::string, std::unordered_map<int, RowAddress>> positions_;
  std::unordered_map<int, Playhead> playheads_;
  std::unordered_map<int, int> offsets_;
};

#endif

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
// track's position. Every other track shows its position on a line of its
// own, offset from the cursor row (per buffer too), and its other rows
// around that line, so one screen row can show different scenes in
// different columns. Moving the cursor by hand takes every stopped track
// along, each on its own line, so the stopped columns stay put while their
// lines move with the cursor row; a playing track's line stays put. As a
// track plays - the cursor track too - its line moves down with its
// playhead, and every other line stays where it is, until the view
// scrolls within its margin. Effect commands and block operations resolve
// each track's rows the same way.
class ScenePatternSource : public PatternSource {
 public:
  explicit ScenePatternSource(Controller & controller) : controller_(controller) { }

  RowAddress cursor() const override;
  void moveCursor(int delta_rows) override;
  void setCursorTrack(int track_id) override;
  RowAddress trackCursor(int track_id) const override { return advance(cursor(), offset(track_id)); }
  // Only while the transport runs: paused, moving the cursor moves every
  // launched clip's playhead instead.
  bool cursorLocked() const override;
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
  // Every track's position is shown on its own line (trackCursor()).
  std::optional<int> positionRow(int track_id, int block) const override;

  bool showsClipIndirection() const override { return false; }
  bool hasLocators() const override { return false; }
  bool cursorFollowsTransport() const override { return false; }

  // Scenes shown: every used one plus one empty one to create clips in,
  // never fewer than a Launchpad grid's 8 rows. Shared with the clip grid,
  // so both always offer the same scenes.
  static int sceneCount(const Song & song);

  // Where each track's launched clip is playing: its scene and row. A
  // track whose playhead goes away stays where it left it, on its line.
  // `elapsed` is the rows since the clip started, unwrapped, for a looping one
  // (-1 otherwise): its line keeps moving forward through a loop.
  struct Playhead { int scene; int row; int elapsed = -1; bool looping = false; };
  void setPlayheads(std::unordered_map<int, Playhead> playheads);
  // Keeps every stopped track but the cursor's where it is on screen as
  // the view scrolls `rows` down.
  void holdStoppedTracks(int rows);
  // Brings every track's line (other than the cursor track's) back within
  // `margin` rows of the edges of the `rows` rows starting at `top`, when
  // it has left them. True if any moved.
  bool keepTrackLinesVisible(RowAddress top, int rows, int margin);

  // `address`, a row of the editor (the cursor track's), as `track_id`'s
  // own: its position moved by as many rows as `address` is from the
  // cursor. Before the first scene the row is negative (of scene 0); past
  // the last one the block is blockCount().
  //
  // A track playing a looping clip is periodic instead: rows past the end
  // of its scene are the start of the same scene again (and rows before
  // its start the end), so its line carries on through the loop.
  RowAddress trackAddress(int track_id, RowAddress address) const override;
  bool isOtherLoopPass(int track_id, RowAddress address) const override;
  // How far the cursor track's position jumped back (or forward) as its
  // loop wrapped, beyond the rows it played; reset by the call. The view
  // moves by the same so the line carries on down the screen.
  int takeCursorJump() { auto jump = cursor_jump_; cursor_jump_ = 0; return jump; }

 private:
  // Always the active buffer's song - it changes when the buffer does.
  Song & song() const;
  RowAddress clamp(RowAddress address) const;
  bool isPlaying(int track_id) const;
  // The row of its scene a playhead is at: a looping clip's elapsed rows
  // wrap by the scene, not the clip, so a shorter clip plays on into its
  // dimmed repeats until the whole scene loops.
  int sceneRow(const Playhead & playhead) const;
  bool isLooping(int track_id) const;
  // `track_id`'s position moved by `address`'s distance from the cursor
  // (its line's offset taken off), not yet wrapped by a loop.
  int rowsFromPosition(int track_id, RowAddress address) const;
  RowAddress position(int track_id) const;
  std::unordered_map<int, RowAddress> & positions() const;
  // Moves every stopped track but the cursor's by `rows`, keeping each
  // one within the scenes.
  void moveStoppedTracks(int rows);
  // Moves every track's line but the cursor track's by `rows`.
  void moveOtherLines(int rows);
  // A track's line, in rows from the cursor row; 0 for the cursor track.
  int offset(int track_id) const;
  std::unordered_map<int, int> & offsets() const;

  Controller & controller_;
  int cursor_track_id_ = -1;
  int cursor_jump_ = 0;
  // Stopped tracks' positions, per buffer name.
  mutable std::unordered_map<std::string, std::unordered_map<int, RowAddress>> positions_;
  std::unordered_map<int, Playhead> playheads_;
  // Each track's line (offset()), per buffer name.
  mutable std::unordered_map<std::string, std::unordered_map<int, int>> offsets_;
};

#endif

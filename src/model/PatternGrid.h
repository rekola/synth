#ifndef _PATTERNGRID_H_
#define _PATTERNGRID_H_

#include "ArrangementView.h"
#include "ClipView.h"
#include "PatternView.h"

#include <string>
#include <utility>

class Song;

// Note/command content addressed by (track id, raw row) - what
// PatternBlockOps reads and writes. Each implementation decides which
// Pattern holds a cell and which of that Pattern's own rows it is (a
// Pattern shorter than its context repeats), so the same block operations
// work on the arrangement's background as well as on clip content.
class PatternGrid {
 public:
  virtual ~PatternGrid() = default;

  // The Pattern holding (track_id, row), or an invalid handle when nothing
  // is stored for that track yet. `pattern_row` is set either way.
  virtual PatternView find(int track_id, int row, int & pattern_row) const = 0;
  // Like find(), but creates the Pattern if needed. Invalid only when the
  // cell can't be written at all (a read-only grid).
  virtual PatternView obtain(int track_id, int row, int & pattern_row) = 0;

  // Where the cell's effect command lives: the same Pattern as its notes,
  // unless an implementation keeps commands elsewhere.
  virtual PatternView findCommands(int track_id, int row, int & pattern_row) const { return find(track_id, row, pattern_row); }
  virtual PatternView obtainCommands(int track_id, int row, int & pattern_row) { return obtain(track_id, row, pattern_row); }
};

// The arrangement's own background Patterns (never its clip instances).
// Built over a const Song it is read-only: obtain() returns an invalid
// handle.
class ArrangementBackgroundGrid : public PatternGrid {
 public:
  explicit ArrangementBackgroundGrid(ArrangementView arrangement, bool writable = true) : arrangement_(arrangement), writable_(writable) { }

  PatternView find(int track_id, int row, int & pattern_row) const override;
  PatternView obtain(int track_id, int row, int & pattern_row) override;

 private:
  ArrangementView arrangement_;
  bool writable_;
};

// The arrangement as the pattern editor shows it, anchored at
// `anchor_row`: each track's notes come from whatever supplies that track
// at the anchor row - the Launchpad-focused clip, a placed clip instance,
// or the track's own background - for as long as the same source supplies
// it; rows where another source takes over have no Pattern (so a block
// operation never spills from a clip into the background or another
// clip). Effect commands always come from the background, where playback
// reads them. Built over a const Song, it's read-only.
class ArrangementRegionGrid : public PatternGrid {
 public:
  ArrangementRegionGrid(Song & song, int anchor_row, std::string focused_clip_id);
  ArrangementRegionGrid(const Song & song, int anchor_row, std::string focused_clip_id);

  PatternView find(int track_id, int row, int & pattern_row) const override;
  PatternView obtain(int track_id, int row, int & pattern_row) override;
  PatternView findCommands(int track_id, int row, int & pattern_row) const override { return background_.find(track_id, row, pattern_row); }
  PatternView obtainCommands(int track_id, int row, int & pattern_row) override { return background_.obtain(track_id, row, pattern_row); }

  // The rows [first, last] around the anchor row over which `track_id`'s
  // notes keep coming from the same source.
  std::pair<int, int> sourceRows(int track_id) const;

 private:
  enum class SourceKind { BACKGROUND, INSTANCE, FOCUSED };
  struct Source {
    SourceKind kind = SourceKind::BACKGROUND;
    int clip_index = -1, start_row = 0;
    bool operator==(const Source & other) const { return kind == other.kind && clip_index == other.clip_index && start_row == other.start_row; }
  };
  Source sourceAt(int track_id, int row) const;
  // The clip Pattern `source` supplies, and `row`'s row within it; invalid
  // for the background or a clip holding sample audio.
  PatternView clipPattern(int track_id, const Source & source, int row, int & pattern_row) const;

  const Song & read_song_;
  Song * write_song_;
  ArrangementBackgroundGrid background_;
  int anchor_row_;
  std::string focused_clip_id_;
};

// One scene: the clips at clip-list index `scene` across tracks, rows
// counted from each clip's own start. A looping clip repeats; a one-shot
// has nothing past its own length. An unused slot (or one holding sample
// audio) has no Pattern to find; obtain() creates a looping clip there
// ("Clip N", N the scene's 1-based number), `length` rows long (the
// scene's own length), and lengthens a one-shot to the bar holding `row`.
// Built over a const Song, it's read-only.
class SceneGrid : public PatternGrid {
 public:
  SceneGrid(Song & song, int scene, int length)
    : read_(song), write_(&song), scene_(scene), length_(length) { }
  SceneGrid(const Song & song, int scene, int length)
    : read_(song), write_(nullptr), scene_(scene), length_(length) { }

  PatternView find(int track_id, int row, int & pattern_row) const override;
  PatternView obtain(int track_id, int row, int & pattern_row) override;

  // The clip at this scene on `track_id` when it has content (notes or
  // sample audio), else an invalid handle.
  ClipView clipFor(int track_id) const;
  // The row of `clip`'s own Pattern that plays at scene row `row`, or -1
  // past a one-shot's end.
  static int clipRow(const ClipView & clip, int row);

 private:
  const Song & read_;
  Song * write_;
  int scene_;
  int length_;
};

#endif

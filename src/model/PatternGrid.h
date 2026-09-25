#ifndef _PATTERNGRID_H_
#define _PATTERNGRID_H_

#include <string>
#include <utility>

class Pattern;
class Arrangement;
class Song;
class Clip;

// Note/command content addressed by (track id, raw row) - what
// PatternBlockOps reads and writes. Each implementation decides which
// Pattern holds a cell and which of that Pattern's own rows it is (a
// Pattern shorter than its context repeats), so the same block operations
// work on the arrangement's background as well as on clip content.
class PatternGrid {
 public:
  virtual ~PatternGrid() = default;

  // The Pattern holding (track_id, row), or nullptr when nothing is stored
  // for that track yet. `pattern_row` is set either way.
  virtual const Pattern * find(int track_id, int row, int & pattern_row) const = 0;
  virtual Pattern * find(int track_id, int row, int & pattern_row) = 0;
  // Like find(), but creates the Pattern if needed. nullptr only when the
  // cell can't be written at all.
  virtual Pattern * obtain(int track_id, int row, int & pattern_row) = 0;

  // Where the cell's effect command lives: the same Pattern as its notes,
  // unless an implementation keeps commands elsewhere.
  virtual const Pattern * findCommands(int track_id, int row, int & pattern_row) const { return find(track_id, row, pattern_row); }
  virtual Pattern * findCommands(int track_id, int row, int & pattern_row) { return find(track_id, row, pattern_row); }
  virtual Pattern * obtainCommands(int track_id, int row, int & pattern_row) { return obtain(track_id, row, pattern_row); }
};

// The arrangement's own background Patterns (never its clip instances).
// Built over a const Arrangement, it's read-only: the writable find() and
// obtain() return nullptr.
class ArrangementBackgroundGrid : public PatternGrid {
 public:
  explicit ArrangementBackgroundGrid(Arrangement & arrangement) : read_(arrangement), write_(&arrangement) { }
  explicit ArrangementBackgroundGrid(const Arrangement & arrangement) : read_(arrangement), write_(nullptr) { }

  const Pattern * find(int track_id, int row, int & pattern_row) const override;
  Pattern * find(int track_id, int row, int & pattern_row) override;
  Pattern * obtain(int track_id, int row, int & pattern_row) override;

 private:
  const Arrangement & read_;
  Arrangement * write_;
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

  const Pattern * find(int track_id, int row, int & pattern_row) const override;
  Pattern * find(int track_id, int row, int & pattern_row) override;
  Pattern * obtain(int track_id, int row, int & pattern_row) override;
  const Pattern * findCommands(int track_id, int row, int & pattern_row) const override { return background_.find(track_id, row, pattern_row); }
  Pattern * findCommands(int track_id, int row, int & pattern_row) override { return background_.find(track_id, row, pattern_row); }
  Pattern * obtainCommands(int track_id, int row, int & pattern_row) override { return background_.obtain(track_id, row, pattern_row); }

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
  // The clip Pattern `source` supplies, and `row`'s row within it; nullptr
  // for the background or a clip holding sample audio.
  const Pattern * clipPattern(int track_id, const Source & source, int row, int & pattern_row) const;

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

  const Pattern * find(int track_id, int row, int & pattern_row) const override;
  Pattern * find(int track_id, int row, int & pattern_row) override;
  Pattern * obtain(int track_id, int row, int & pattern_row) override;

  // The clip at this scene on `track_id` when it has content (notes or
  // sample audio), else nullptr.
  const Clip * clipFor(int track_id) const;
  // The row of `clip`'s own Pattern that plays at scene row `row`, or -1
  // past a one-shot's end.
  static int clipRow(const Clip & clip, int row);

 private:
  const Song & read_;
  Song * write_;
  int scene_;
  int length_;
};

#endif

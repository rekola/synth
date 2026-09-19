#ifndef _PATTERNGRID_H_
#define _PATTERNGRID_H_

class Pattern;
class Section;
class Song;
class Clip;

// Note/command content addressed by (track id, raw row) - what
// PatternBlockOps reads and writes. Each implementation decides which
// Pattern holds a cell and which of that Pattern's own rows it is (a
// Pattern shorter than its context repeats), so the same block operations
// work on a section's background as well as on clip content.
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
};

// A section's own background Patterns (never its clip instances), rows
// resolved against `context_length`. Built over a const Section, it's
// read-only: the writable find() and obtain() return nullptr.
class SectionBackgroundGrid : public PatternGrid {
 public:
  SectionBackgroundGrid(Section & section, int context_length)
    : read_(section), write_(&section), context_length_(context_length) { }
  SectionBackgroundGrid(const Section & section, int context_length)
    : read_(section), write_(nullptr), context_length_(context_length) { }

  const Pattern * find(int track_id, int row, int & pattern_row) const override;
  Pattern * find(int track_id, int row, int & pattern_row) override;
  Pattern * obtain(int track_id, int row, int & pattern_row) override;

 private:
  const Section & read_;
  Section * write_;
  int context_length_;
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

#ifndef _PATTERNGRID_H_
#define _PATTERNGRID_H_

class Pattern;
class Section;

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

#endif

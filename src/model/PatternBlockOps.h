#ifndef _PATTERNBLOCKOPS_H_
#define _PATTERNBLOCKOPS_H_

#include "BarGrid.h"
#include "../dsp/NoiseGenerator.h"
#include "../model/Note.h"
#include "../model/Command.h"
#include "../instruments/Tuning.h"

#include <functional>
#include <string>
#include <vector>

class Song;
class Clip;
class PatternGrid;

struct PatternBlockCell {
  std::vector<Note> notes;
  Command command;
  int note_offset = 0; // for note-column-scoped cells: which note column `notes[0]` is
};

// [row_offset][track_offset], row-major within the copied rectangle.
using PatternBlock = std::vector<std::vector<PatternBlockCell> >;

// Captures notes and effect command for each (row, track) in the inclusive
// range [row_lo, row_hi] x track_ids[track_lo..track_hi]. Rows are raw;
// `grid` maps each to the Pattern row that actually holds it (a shorter
// Pattern repeats - Pattern.h's own getEffectiveRow() comment), so a
// range straddling a repeat reads back what's actually playing rather
// than blank, unreachable rows. The same goes for every function here.
PatternBlock copyPatternBlock(const PatternGrid & grid, int row_lo, int row_hi,
			     const std::vector<int> & track_ids, int track_lo, int track_hi);

// Clears notes and effect command for the same range.
void clearPatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		       const std::vector<int> & track_ids, int track_lo, int track_hi);

// Writes `block` into `grid` starting at (target_row, track_ids[target_track]),
// clipping any cells whose target row or track falls outside [0, num_rows)/
// track_ids bounds.
void pastePatternBlock(PatternGrid & grid, const PatternBlock & block, int num_rows,
		       int target_row, const std::vector<int> & track_ids, int target_track);

// What a just-intonation correction is measured from: the song's EDO and
// key (Song::getKey(), a full note value of which only the pitch class counts).
struct IntonationContext {
  Tuning tuning = Tuning::EDO12;
  int key = 0;
};

// How many pitched notes an apply touched, and how many of those had another
// fx that the correction replaced.
struct TuningSummary {
  int notes = 0;
  int replaced = 0;
};

// Transposes (up if `up`, else down) every note in the same range, except
// any track `is_percussion` reports true for. A percussion track's
// Note::getValue() selects which drum sound plays (a MIDI key), not a
// pitch - transposing it would silently swap to a different, unrelated
// drum instead of "transposing" anything, so those tracks are skipped
// entirely within the range rather than shifting their notes.
// With `retune`, a note that carried a tuning correction gets a fresh one for
// its new pitch (a corrected note stays corrected, and now fits its new place
// against the key); notes without one are left untuned.
void transposePatternBlock(PatternGrid & grid, int row_lo, int row_hi,
			   const std::vector<int> & track_ids, int track_lo, int track_hi, bool up,
			   const std::function<bool(int track_id)> & is_percussion,
			   const IntonationContext * retune = nullptr);

// How far humanize moves a note: its velocity by up to +-`velocity` (kept
// within 1..127, so a note never turns into an off) and its delay later by
// up to `delay` (0..255 of a row; a delay can't be negative, so the shift
// only ever pushes a note late).
struct HumanizeAmount {
  int velocity = 12;
  int delay = 32;
};

// Randomizes velocity and delay of every sounding note in the same range.
// Offs and aftertouch are left alone. Percussion included: only the
// feel changes, not which drum plays.
void humanizePatternBlock(PatternGrid & grid, int row_lo, int row_hi,
			  const std::vector<int> & track_ids, int track_lo, int track_hi,
			  const HumanizeAmount & amount, NoiseGenerator & rng);

// Single-track, note-column-scoped siblings of the above: operate on just
// notes [note_lo, note_hi] of one track, leaving other note columns and the
// track's effect Command untouched (PatternEditor's SelectionScope::
// NOTE_COLUMN - mixing a note-column selection with the effect column
// escalates to a whole-track operation instead, so there's no
// include-the-command variant of this family any more).
PatternBlock copyPatternBlockNotes(const PatternGrid & grid, int row_lo, int row_hi,
				   int track_id, int note_lo, int note_hi);
void clearPatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			    int track_id, int note_lo, int note_hi);
// `is_percussion`: same reasoning as transposePatternBlock() above, but a
// plain bool here (not a predicate) since this operates on exactly one
// already-known track_id, not a range.
void transposePatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
				int track_id, int note_lo, int note_hi, bool up, bool is_percussion,
				const IntonationContext * retune = nullptr);
void humanizePatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			       int track_id, int note_lo, int note_hi,
			       const HumanizeAmount & amount, NoiseGenerator & rng);
// Sets the just-intonation correction (JustIntonation.h) of every pitched
// note in the range, replacing whatever fx it held; a note that needs no
// correction gets +00, which marks it as tuned. Offs, aftertouch and any
// track `is_percussion` reports true for (there is no pitch to correct) are
// skipped.
TuningSummary applyJustIntonationBlock(PatternGrid & grid, int row_lo, int row_hi,
				       const std::vector<int> & track_ids, int track_lo, int track_hi,
				       const IntonationContext & context,
				       const std::function<bool(int track_id)> & is_percussion);
TuningSummary applyJustIntonationBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
					    int track_id, int note_lo, int note_hi,
					    const IntonationContext & context, bool is_percussion);
// Removes the tuning correction (whatever produced it) from every note in
// the range, leaving its other fx alone. Returns how many notes had one.
int clearTuningCorrectionBlock(PatternGrid & grid, int row_lo, int row_hi,
			       const std::vector<int> & track_ids, int track_lo, int track_hi);
int clearTuningCorrectionBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
				    int track_id, int note_lo, int note_hi);

// The same operations over the whole song: every track's background pattern
// and every clip's pattern, each once. Callers open the Song::Edit.
TuningSummary applyJustIntonationToSong(Song & song);
int clearTuningCorrectionsInSong(Song & song);
// Moves every pitched note a step and the song's key with them, so the notes
// keep their place against the key and their corrections stay as they were
// (retuned ones come out the same).
void transposeSong(Song & song, bool up);
void humanizeSong(Song & song, const HumanizeAmount & amount, NoiseGenerator & rng);

// Merges `block` into `grid` starting at (target_row, track_id, target_note_offset),
// leaving note columns outside that range untouched (unlike pastePatternBlock,
// which replaces a cell's whole note vector).
void pastePatternBlockNotes(PatternGrid & grid, const PatternBlock & block, int num_rows,
			    int target_row, int track_id, int target_note_offset);

// Single-track, effect-Command-only siblings, for PatternEditor's
// SelectionScope::COMMAND (the cursor confined to just the effect column -
// no note data is read or written by any of these). Note::isDefined() etc.
// has no equivalent here since Command has no "empty" special case beyond
// its own default-constructed all-dashes value.
std::vector<Command> copyPatternBlockCommand(const PatternGrid & grid, int row_lo, int row_hi, int track_id);
void clearPatternBlockCommand(PatternGrid & grid, int row_lo, int row_hi, int track_id);
void pastePatternBlockCommand(PatternGrid & grid, const std::vector<Command> & block, int num_rows,
			      int target_row, int track_id);

// Extracts a track's own content from [row_lo, row_hi] into a new,
// standalone Clip - for `copy-to-clip`. Returns a whole Clip, not just a
// Pattern, for its id/name/loop/length (SongObject/Clip.h's own fields) -
// a bare Pattern has none of those. row_lo's own bar becomes the new Clip's row 0, so
// a selection that doesn't start on a bar boundary comes back
// front-padded (rest before the first real note) rather than shifting
// every note's phase-within-a-bar once the clip gets placed somewhere
// else later. The new Clip's own length (Clip::getLength(), not its leaf
// Pattern's - see Clip.h) is rounded up to the next whole bar past
// row_hi the same implicit way (back-padded) - a bar-aligned length is
// what lets a later placement land cleanly on the grid.
Clip extractClip(const PatternGrid & grid, int track_id, int row_lo, int row_hi, const BarGrid & bars);

// Row-only siblings of the copy/clear/paste families above, for
// PatternEditor's SelectionScope::LOCATOR - the song's locators
// (Song::getLocators()), keyed by row alone, so unlike every other family
// here there's no track_id/note range involved at all. Rows count from
// `first_row`, the song's own row the block starts at.
std::vector<std::string> copyPatternBlockLocators(const Song & song, int first_row, int row_lo, int row_hi);
void clearPatternBlockLocators(Song & song, int first_row, int row_lo, int row_hi);
void pastePatternBlockLocators(Song & song, int first_row, const std::vector<std::string> & block, int num_rows,
			       int target_row);

#endif

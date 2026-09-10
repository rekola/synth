#ifndef _PATTERNBLOCKOPS_H_
#define _PATTERNBLOCKOPS_H_

#include "../model/Note.h"
#include "../model/Command.h"

#include <functional>
#include <string>
#include <vector>

class Section;
class Clip;

struct PatternBlockCell {
  std::vector<Note> notes;
  Command command;
  int note_offset = 0; // for note-column-scoped cells: which note column `notes[0]` is
};

// [row_offset][track_offset], row-major within the copied rectangle.
using PatternBlock = std::vector<std::vector<PatternBlockCell> >;

// Captures notes and effect command for each (row, track) in the inclusive
// range [row_lo, row_hi] x track_ids[track_lo..track_hi]. `context_length`
// is the song's own pattern length, passed through to Section::
// getEffectiveRow() for each (row, track) pair - a track whose own
// Pattern is shorter reads/clears/transposes its real, repeating row
// (Pattern.h's own getEffectiveRow() comment), not the raw one, so a
// range straddling its length boundary reads back what's actually
// playing rather than blank, unreachable rows.
PatternBlock copyPatternBlock(const Section & section, int row_lo, int row_hi,
			     const std::vector<int> & track_ids, int track_lo, int track_hi, int context_length);

// Clears notes and effect command for the same range.
void clearPatternBlock(Section & section, int row_lo, int row_hi,
		       const std::vector<int> & track_ids, int track_lo, int track_hi, int context_length);

// Writes `block` into `section` starting at (target_row, track_ids[target_track]),
// clipping any cells whose target row or track falls outside [0, num_rows)/
// track_ids bounds.
void pastePatternBlock(Section & section, const PatternBlock & block, int num_rows,
		       int target_row, const std::vector<int> & track_ids, int target_track);

// Transposes (up if `up`, else down) every note in the same range, except
// any track `is_percussion` reports true for. A percussion track's
// Note::getValue() selects which drum sound plays (a MIDI key), not a
// pitch - transposing it would silently swap to a different, unrelated
// drum instead of "transposing" anything, so those tracks are skipped
// entirely within the range rather than shifting their notes.
void transposePatternBlock(Section & section, int row_lo, int row_hi,
			   const std::vector<int> & track_ids, int track_lo, int track_hi, bool up,
			   const std::function<bool(int track_id)> & is_percussion, int context_length);

// Single-track, note-column-scoped siblings of the above: operate on just
// notes [note_lo, note_hi] of one track, leaving other note columns and the
// track's effect Command untouched (PatternEditor's SelectionScope::
// NOTE_COLUMN - mixing a note-column selection with the effect column
// escalates to a whole-track operation instead, so there's no
// include-the-command variant of this family any more).
PatternBlock copyPatternBlockNotes(const Section & section, int row_lo, int row_hi,
				   int track_id, int note_lo, int note_hi, int context_length);
void clearPatternBlockNotes(Section & section, int row_lo, int row_hi,
			    int track_id, int note_lo, int note_hi, int context_length);
// `is_percussion`: same reasoning as transposePatternBlock() above, but a
// plain bool here (not a predicate) since this operates on exactly one
// already-known track_id, not a range.
void transposePatternBlockNotes(Section & section, int row_lo, int row_hi,
				int track_id, int note_lo, int note_hi, bool up, bool is_percussion, int context_length);
// Merges `block` into `section` starting at (target_row, track_id, target_note_offset),
// leaving note columns outside that range untouched (unlike pastePatternBlock,
// which replaces a cell's whole note vector).
void pastePatternBlockNotes(Section & section, const PatternBlock & block, int num_rows,
			    int target_row, int track_id, int target_note_offset);

// Single-track, effect-Command-only siblings, for PatternEditor's
// SelectionScope::COMMAND (the cursor confined to just the effect column -
// no note data is read or written by any of these). Note::isDefined() etc.
// has no equivalent here since Command has no "empty" special case beyond
// its own default-constructed all-dashes value.
std::vector<Command> copyPatternBlockCommand(const Section & section, int row_lo, int row_hi, int track_id, int context_length);
void clearPatternBlockCommand(Section & section, int row_lo, int row_hi, int track_id, int context_length);
void pastePatternBlockCommand(Section & section, const std::vector<Command> & block, int num_rows,
			      int target_row, int track_id);

// Extracts a track's own content from [row_lo, row_hi] into a new,
// standalone Clip - for `copy-to-clip`. Returns a whole Clip, not just a
// Pattern, even though only its leaf entry (getLeafPattern()) is
// populated here: a clip's full/eventual form is one Pattern per
// relevant track_id, not just the leaf track's own (nested Effect
// automation, still unbuilt) - shaping the extraction this way now means
// adding those other entries later is purely additive, not a second
// return-type change. row_lo's own bar becomes the new Clip's row 0, so
// a selection that doesn't start on a bar boundary comes back
// front-padded (rest before the first real note) rather than shifting
// every note's phase-within-a-bar once the clip gets placed somewhere
// else later. The new Clip's own length (Clip::getLength(), not its leaf
// Pattern's - see Clip.h) is rounded up to the next whole bar past
// row_hi the same implicit way (back-padded) - a bar-aligned length is
// what lets a later placement land cleanly on the grid.
Clip extractClip(const Section & section, int track_id, int row_lo, int row_hi, int rows_per_bar, int context_length);

// Row-only siblings of the copy/clear/paste families above, for
// PatternEditor's SelectionScope::ANNOTATION - a Section's annotations are
// keyed by row alone (see Section.h's own comment on why they live there
// rather than on Pattern), so unlike every other family here there's no
// track_id/note range involved at all.
std::vector<std::string> copyPatternBlockAnnotations(const Section & section, int row_lo, int row_hi);
void clearPatternBlockAnnotations(Section & section, int row_lo, int row_hi);
void pastePatternBlockAnnotations(Section & section, const std::vector<std::string> & block, int num_rows,
				  int target_row);

#endif

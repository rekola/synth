#ifndef _PATTERNBLOCKOPS_H_
#define _PATTERNBLOCKOPS_H_

#include "../model/Note.h"
#include "../model/Command.h"

#include <functional>
#include <string>
#include <vector>

class Scene;

struct PatternBlockCell {
  std::vector<Note> notes;
  Command command;
  int note_offset = 0; // for note-column-scoped cells: which note column `notes[0]` is
};

// [row_offset][track_offset], row-major within the copied rectangle.
using PatternBlock = std::vector<std::vector<PatternBlockCell> >;

// Captures notes and effect command for each (row, track) in the inclusive
// range [row_lo, row_hi] x track_ids[track_lo..track_hi].
PatternBlock copyPatternBlock(const Scene & scene, int row_lo, int row_hi,
			     const std::vector<int> & track_ids, int track_lo, int track_hi);

// Clears notes and effect command for the same range.
void clearPatternBlock(Scene & scene, int row_lo, int row_hi,
		       const std::vector<int> & track_ids, int track_lo, int track_hi);

// Writes `block` into `scene` starting at (target_row, track_ids[target_track]),
// clipping any cells whose target row or track falls outside [0, num_rows)/
// track_ids bounds.
void pastePatternBlock(Scene & scene, const PatternBlock & block, int num_rows,
		       int target_row, const std::vector<int> & track_ids, int target_track);

// Transposes (up if `up`, else down) every note in the same range, except
// any track `is_percussion` reports true for. A percussion track's
// Note::getValue() selects which drum sound plays (a MIDI key), not a
// pitch - transposing it would silently swap to a different, unrelated
// drum instead of "transposing" anything, so those tracks are skipped
// entirely within the range rather than shifting their notes.
void transposePatternBlock(Scene & scene, int row_lo, int row_hi,
			   const std::vector<int> & track_ids, int track_lo, int track_hi, bool up,
			   const std::function<bool(int track_id)> & is_percussion);

// Single-track, note-column-scoped siblings of the above: operate on just
// notes [note_lo, note_hi] of one track, leaving other note columns and the
// track's effect Command untouched (PatternEditor's SelectionScope::
// NOTE_COLUMN - mixing a note-column selection with the effect column
// escalates to a whole-track operation instead, so there's no
// include-the-command variant of this family any more).
PatternBlock copyPatternBlockNotes(const Scene & scene, int row_lo, int row_hi,
				   int track_id, int note_lo, int note_hi);
void clearPatternBlockNotes(Scene & scene, int row_lo, int row_hi,
			    int track_id, int note_lo, int note_hi);
// `is_percussion`: same reasoning as transposePatternBlock() above, but a
// plain bool here (not a predicate) since this operates on exactly one
// already-known track_id, not a range.
void transposePatternBlockNotes(Scene & scene, int row_lo, int row_hi,
				int track_id, int note_lo, int note_hi, bool up, bool is_percussion);
// Merges `block` into `scene` starting at (target_row, track_id, target_note_offset),
// leaving note columns outside that range untouched (unlike pastePatternBlock,
// which replaces a cell's whole note vector).
void pastePatternBlockNotes(Scene & scene, const PatternBlock & block, int num_rows,
			    int target_row, int track_id, int target_note_offset);

// Single-track, effect-Command-only siblings, for PatternEditor's
// SelectionScope::COMMAND (the cursor confined to just the effect column -
// no note data is read or written by any of these). Note::isDefined() etc.
// has no equivalent here since Command has no "empty" special case beyond
// its own default-constructed all-dashes value.
std::vector<Command> copyPatternBlockCommand(const Scene & scene, int row_lo, int row_hi, int track_id);
void clearPatternBlockCommand(Scene & scene, int row_lo, int row_hi, int track_id);
void pastePatternBlockCommand(Scene & scene, const std::vector<Command> & block, int num_rows,
			      int target_row, int track_id);

// Row-only siblings of the above, for PatternEditor's SelectionScope::
// ANNOTATION - a Scene's annotations are keyed by row alone (see Scene.h's
// own comment on why they live there rather than on Pattern), so unlike
// every other family here there's no track_id/note range involved at all.
std::vector<std::string> copyPatternBlockAnnotations(const Scene & scene, int row_lo, int row_hi);
void clearPatternBlockAnnotations(Scene & scene, int row_lo, int row_hi);
void pastePatternBlockAnnotations(Scene & scene, const std::vector<std::string> & block, int num_rows,
				  int target_row);

#endif

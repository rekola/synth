#ifndef _SELECTIONSCOPE_H_
#define _SELECTIONSCOPE_H_

// What a selection actually resolves to - see SelectionBounds.h and
// PatternEditor::getEffectiveSelectionBounds() - and, in turn, what a
// ClipboardEntry (ClipboardEntry.h) holds after a kill/copy. NOTE_COLUMN
// covers one voice slot or a contiguous run of several (still excluding
// the effect column); mixing a note column with the effect column, or
// spanning more than one track, both escalate to TRACK. ANNOTATION means
// the cursor has moved past the grid entirely onto the current row's
// annotation slot (GridPosition::scope) - nothing on the grid is selected
// then. EVERYTHING is what a selection escalates to when one end is on
// the annotation and the other is on a real track - there's no such thing
// as selecting "some tracks plus the annotation," so growing between the
// two covers the whole row instead (every track, and the annotation).
// No kill-region/kill-ring-save/transpose-region-* command does anything
// with ANNOTATION or EVERYTHING yet (annotations have no kill/copy/paste
// support in this first pass).
enum class SelectionScope { TRACK, NOTE_COLUMN, COMMAND, ANNOTATION, EVERYTHING };

#endif

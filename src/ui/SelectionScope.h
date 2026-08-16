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
// kill-region/kill-ring-save/yank support ANNOTATION (one row-keyed string
// per row - see PatternBlockOps.h's copy/clear/pastePatternBlockAnnotations)
// and EVERYTHING (both that and a whole-row PatternBlock, TRACK's own
// capture, acted on together - see ClipboardEntry.h's own comment).
// transpose-region-* does nothing with either: Scene's annotation text has
// no numeric/transposable semantics, and while EVERYTHING's PatternBlock
// half technically has transposable notes, there's no single mark/point
// gesture that reaches EVERYTHING without also touching the annotation, so
// treating it like TRACK there would silently transpose notes a user
// positioned no differently than for a no-op ANNOTATION-only selection.
enum class SelectionScope { TRACK, NOTE_COLUMN, COMMAND, ANNOTATION, EVERYTHING };

#endif

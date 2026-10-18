#ifndef _SELECTIONSCOPE_H_
#define _SELECTIONSCOPE_H_

// What a selection actually resolves to - see SelectionBounds.h and
// PatternEditor::getEffectiveSelectionBounds() - and, in turn, what a
// ClipboardEntry (ClipboardEntry.h) holds after a kill/copy. NOTE_COLUMN
// covers one voice slot or a contiguous run of several (still excluding
// the effect column); mixing a note column with the effect column, or
// spanning more than one track, both escalate to TRACK. LOCATOR means
// the cursor has moved past the grid entirely onto the current row's
// locator slot (GridPosition::scope) - nothing on the grid is selected
// then. EVERYTHING is what a selection escalates to when one end is on
// the locator and the other is on a real track - there's no such thing
// as selecting "some tracks plus the locator," so growing between the
// two covers the whole row instead (every track, and the locator).
// kill-region/kill-ring-save/yank support LOCATOR (one row-keyed string
// per row - see PatternBlockOps.h's copy/clear/pastePatternBlockLocators)
// and EVERYTHING (both that and a whole-row PatternBlock, TRACK's own
// capture, acted on together - see ClipboardEntry.h's own comment).
// transpose-region-* does nothing with either: a locator's name has
// no numeric/transposable semantics, and while EVERYTHING's PatternBlock
// half technically has transposable notes, there's no single mark/point
// gesture that reaches EVERYTHING without also touching the locator, so
// treating it like TRACK there would silently transpose notes a user
// positioned no differently than for a no-op LOCATOR-only selection.
// SCENES and ARRANGEMENT are the whole song at once (mark-whole-buffer), by the
// view it is made in: every clip, or the arrangement's backgrounds and the
// clips it places. The single-block region model cannot express either: it
// covers only what supplies a track at the anchor row. Only commands that
// have a whole-song form act on them (apply/clear tuning corrections,
// transpose, humanize); kill, copy and yank refuse.
enum class SelectionScope { TRACK,
                            NOTE_COLUMN,
                            COMMAND,
                            LOCATOR,
                            EVERYTHING,
                            SCENES,
                            ARRANGEMENT };

inline bool isWholeSong(SelectionScope scope) {
  return scope == SelectionScope::SCENES || scope == SelectionScope::ARRANGEMENT;
}

#endif

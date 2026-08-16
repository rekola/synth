#ifndef _CLIPBOARDENTRY_H_
#define _CLIPBOARDENTRY_H_

#include "PatternBlockOps.h"
#include "SelectionScope.h"
#include "../model/Command.h"

#include <string>
#include <vector>

// A single kill's worth of clipboard content, self-describing via scope so
// yank() doesn't need a separate side channel to know how to paste it back -
// see PatternEditor's kill-region/kill-ring-save/yank. Exactly one of
// cells/commands is ever populated, per scope: cells for TRACK (every note
// column plus the row's Command) and NOTE_COLUMN (just the selected
// note-slot range, same PatternBlock shape), commands for COMMAND (one
// Command per row, independent of any note data). annotations is populated
// for ANNOTATION (one row-keyed string per row, independent of any track at
// all - see Scene.h's own comment on why annotations live there) *and*
// alongside cells for EVERYTHING (a selection spanning every track plus the
// annotation - see SelectionScope.h) - the only scope where two of these
// fields are ever both non-empty at once, since EVERYTHING really is two
// captures (a whole-row PatternBlock, same shape as TRACK's, plus the same
// per-row annotation text ANNOTATION captures) glued to the same row range.
// Its own file (rather than nested inside PatternEditor) so a future
// kill-ring - several of these, cycled through via an Emacs-style M-y/
// yank-pop - is just std::vector<ClipboardEntry> plus a rotation index
// somewhere, not a restructuring of how one entry stores itself.
struct ClipboardEntry {
  SelectionScope scope = SelectionScope::TRACK;
  PatternBlock cells;
  std::vector<Command> commands;
  std::vector<std::string> annotations;
};

#endif

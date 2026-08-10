#ifndef _CLIPBOARDENTRY_H_
#define _CLIPBOARDENTRY_H_

#include "PatternBlockOps.h"
#include "SelectionScope.h"
#include "Command.h"

#include <vector>

// A single kill's worth of clipboard content, self-describing via scope so
// yank() doesn't need a separate side channel to know how to paste it back -
// see PatternEditor's kill-region/kill-ring-save/yank. Only one of
// cells/commands is ever populated, per scope: cells for TRACK (every note
// column plus the row's Command) and NOTE_COLUMN (just the selected
// note-slot range, same PatternBlock shape), commands for COMMAND (one
// Command per row, independent of any note data). Its own file (rather
// than nested inside PatternEditor) so a future kill-ring - several of
// these, cycled through via an Emacs-style M-y/yank-pop - is just
// std::vector<ClipboardEntry> plus a rotation index somewhere, not a
// restructuring of how one entry stores itself.
struct ClipboardEntry {
  SelectionScope scope = SelectionScope::TRACK;
  PatternBlock cells;
  std::vector<Command> commands;
};

#endif

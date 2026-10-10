#include "PatternBlockOps.h"

#include "../model/Arrangement.h"
#include "../model/Song.h"
#include "../model/Clip.h"
#include "PatternGrid.h"
#include "JustIntonation.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace std;

namespace {

vector<Note> notesAt(const PatternGrid & grid, int track_id, int row) {
  int pattern_row;
  auto pattern = grid.find(track_id, row, pattern_row);
  return pattern ? pattern->getNotes(pattern_row) : vector<Note>();
}

Command commandAt(const PatternGrid & grid, int track_id, int row) {
  int pattern_row;
  auto pattern = grid.findCommands(track_id, row, pattern_row);
  return pattern ? pattern->getCommand(pattern_row) : Command();
}

void humanizeNote(Note & note, const HumanizeAmount & amount, NoiseGenerator & rng) {
  if (!note.isDefined() || note.isOff() || note.isAftertouch()) return;
  auto velocity = note.getVelocity() + static_cast<int>(lround(rng.next() * static_cast<float>(amount.velocity)));
  auto delay = note.getDelay() + static_cast<int>(lround((rng.next() + 1.0f) * 0.5f * static_cast<float>(amount.delay)));
  note.setVelocity(static_cast<short>(clamp(velocity, 1, 127)));
  note.setDelay(static_cast<short>(clamp(delay, 0, 255)));
}


bool isPitched(const Note & note) {
  return note.isDefined() && !note.isOff() && !note.isAftertouch();
}

// Moves a note and, when `retune` is given and it carried a tuning
// correction, gives it the one that fits its new pitch.
void transposeNote(Note & note, int delta, const IntonationContext * retune) {
  bool tuned = retune && note.hasTuningCorrection();
  note.transpose(delta);
  if (tuned) note.setTuningCorrection(just_intonation::correctionCentsForNote(retune->tuning, note.getValue(), retune->key));
}

void tuneNote(Note & note, const IntonationContext & context, TuningSummary & summary) {
  if (!isPitched(note)) return;
  if (note.hasFx() && !note.hasTuningCorrection()) summary.replaced++;
  note.setTuningCorrection(just_intonation::correctionCentsForNote(context.tuning, note.getValue(), context.key));
  summary.notes++;
}

bool untuneNote(Note & note) {
  if (!note.hasTuningCorrection()) return false;
  note.clearFx();
  return true;
}

// Calls fn(track_id, pattern) for every pattern that holds notes: each
// track's background, then each clip once.
template <class Fn>
void forEachSongPattern(Song & song, Fn fn) {
  for (auto & [ track_id, pattern ] : song.getArrangement()->getPatternsByTrack()) fn(track_id, pattern);
  for (auto track_id : song.clipTrackIds()) {
    for (auto clip : song.getClips(track_id)) fn(track_id, clip.getLeafPattern());
  }
}

// Calls fn(notes) on each row's notes; fn returns whether it changed them.
template <class Fn>
void forEachNoteRow(PatternView pattern, Fn fn) {
  auto rows = pattern.getNotesByRow();
  for (auto & [ row, notes ] : rows) {
    auto copy = notes;
    if (fn(copy)) pattern.setNotes(row, copy);
  }
}

IntonationContext contextOf(const Song & song) {
  return { song.getTuning(), song.getKey() };
}

bool isPercussionTrack(const Song & song, int track_id) {
  auto * track = song.getMasterTrack().getChildByInternalId(track_id);
  return track && song.getTuningForTrack(*track) == Tuning::PERCUSSION;
}

}

PatternBlock
copyPatternBlock(const PatternGrid & grid, int row_lo, int row_hi,
		 const vector<int> & track_ids, int track_lo, int track_hi) {
  PatternBlock block;

  for (int row = row_lo; row <= row_hi; row++) {
    vector<PatternBlockCell> row_cells;
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[static_cast<size_t>(t)];
      PatternBlockCell cell;
      cell.notes = notesAt(grid, track_id, row);
      cell.command = commandAt(grid, track_id, row);
      row_cells.push_back(move(cell));
    }
    block.push_back(move(row_cells));
  }

  return block;
}

void
clearPatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		  const vector<int> & track_ids, int track_lo, int track_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[static_cast<size_t>(t)];
      int pattern_row;
      if (auto pattern = grid.find(track_id, row, pattern_row)) pattern->clearNotes(pattern_row);
      if (auto pattern = grid.findCommands(track_id, row, pattern_row)) pattern->setCommand(pattern_row, Command());
    }
  }
}

void
transposePatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		      const vector<int> & track_ids, int track_lo, int track_hi, bool up,
		      const std::function<bool(int track_id)> & is_percussion,
		      const IntonationContext * retune) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      auto track_id = track_ids[static_cast<size_t>(t)];
      if (is_percussion(track_id)) continue;
      int pattern_row;
      auto pattern = grid.find(track_id, row, pattern_row);
      if (!pattern) continue;
      auto notes = pattern->getNotes(pattern_row);
      if (notes.empty()) continue; // don't materialize a real entry in the sparse notes_ map
      for (auto & note : notes) transposeNote(note, up ? 1 : -1, retune);
      pattern->setNotes(pattern_row, notes);
    }
  }
}

void
humanizePatternBlock(PatternGrid & grid, int row_lo, int row_hi,
		     const vector<int> & track_ids, int track_lo, int track_hi,
		     const HumanizeAmount & amount, NoiseGenerator & rng) {
  for (int row = row_lo; row <= row_hi; row++) {
    for (int t = track_lo; t <= track_hi; t++) {
      humanizePatternBlockNotes(grid, row, row, track_ids[static_cast<size_t>(t)], 0,
				numeric_limits<int>::max(), amount, rng);
    }
  }
}

void
pastePatternBlock(PatternGrid & grid, const PatternBlock & block, int num_rows,
		  int target_row, const vector<int> & track_ids, int target_track) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    for (size_t track_offset = 0; track_offset < row_cells.size(); track_offset++) {
      int t = target_track + static_cast<int>(track_offset);
      if (t < 0 || t >= static_cast<int>(track_ids.size())) continue;

      auto track_id = track_ids[static_cast<size_t>(t)];
      auto & cell = row_cells[track_offset];
      int pattern_row;
      if (auto pattern = grid.obtain(track_id, row, pattern_row)) pattern->setNotes(pattern_row, cell.notes);
      if (auto pattern = grid.obtainCommands(track_id, row, pattern_row)) pattern->setCommand(pattern_row, cell.command);
    }
  }
}

PatternBlock
copyPatternBlockNotes(const PatternGrid & grid, int row_lo, int row_hi,
		      int track_id, int note_lo, int note_hi) {
  PatternBlock block;

  auto width = note_hi - note_lo + 1;

  for (int row = row_lo; row <= row_hi; row++) {
    auto full_notes = notesAt(grid, track_id, row);
    PatternBlockCell cell;
    cell.note_offset = note_lo;
    // Always the full requested width, not just however many notes this
    // particular row actually had defined - pastePatternBlockNotes() only
    // writes as many positions as cell.notes has, so a short vector here
    // (a row sparser than the widest row in the range) left the
    // destination's own stale content in place at the gap instead of
    // overwriting it with the blank the source row actually had there.
    // Note()'s default constructor is exactly that "undefined" value.
    cell.notes.resize(static_cast<size_t>(width));
    auto size = static_cast<int>(full_notes.size());
    for (int i = 0; i < width; i++) {
      auto src_index = note_lo + i;
      if (src_index < size) cell.notes[static_cast<size_t>(i)] = full_notes[static_cast<size_t>(src_index)];
    }
    block.push_back({move(cell)});
  }

  return block;
}

void
clearPatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
		       int track_id, int note_lo, int note_hi) {
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    for (int i = note_lo; i <= note_hi; i++) {
      pattern->deleteNote(pattern_row, i);
    }
  }
}

void
transposePatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			   int track_id, int note_lo, int note_hi, bool up, bool is_percussion,
			   const IntonationContext * retune) {
  if (is_percussion) return;
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto notes = pattern->getNotes(pattern_row);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) transposeNote(notes[static_cast<size_t>(i)], up ? 1 : -1, retune);
    // setNotes()'s vector<Note> move-assign into the unordered_map, fully
    // inlined down from here, trips a known GCC false positive
    // (-Wfree-nonheap-object misattributing the vector's heap buffer as a
    // non-heap pointer) - not a real dangling-pointer bug.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
}

void
humanizePatternBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			  int track_id, int note_lo, int note_hi,
			  const HumanizeAmount & amount, NoiseGenerator & rng) {
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto notes = pattern->getNotes(pattern_row);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) humanizeNote(notes[static_cast<size_t>(i)], amount, rng);
    // Same GCC false positive as transposePatternBlockNotes().
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
}

TuningSummary
applyJustIntonationBlock(PatternGrid & grid, int row_lo, int row_hi,
			 const vector<int> & track_ids, int track_lo, int track_hi,
			 const IntonationContext & context,
			 const std::function<bool(int track_id)> & is_percussion) {
  TuningSummary summary;
  for (int t = track_lo; t <= track_hi; t++) {
    auto track_id = track_ids[static_cast<size_t>(t)];
    if (is_percussion(track_id)) continue;
    auto part = applyJustIntonationBlockNotes(grid, row_lo, row_hi, track_id, 0, numeric_limits<int>::max(), context, false);
    summary.notes += part.notes;
    summary.replaced += part.replaced;
  }
  return summary;
}

TuningSummary
applyJustIntonationBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
			      int track_id, int note_lo, int note_hi,
			      const IntonationContext & context, bool is_percussion) {
  TuningSummary summary;
  if (is_percussion) return summary;
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto notes = pattern->getNotes(pattern_row);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) tuneNote(notes[static_cast<size_t>(i)], context, summary);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
  return summary;
}

int
clearTuningCorrectionBlock(PatternGrid & grid, int row_lo, int row_hi,
			   const vector<int> & track_ids, int track_lo, int track_hi) {
  int cleared = 0;
  for (int t = track_lo; t <= track_hi; t++) {
    cleared += clearTuningCorrectionBlockNotes(grid, row_lo, row_hi, track_ids[static_cast<size_t>(t)], 0, numeric_limits<int>::max());
  }
  return cleared;
}

int
clearTuningCorrectionBlockNotes(PatternGrid & grid, int row_lo, int row_hi,
				int track_id, int note_lo, int note_hi) {
  int cleared = 0;
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto notes = pattern->getNotes(pattern_row);
    if (notes.empty()) continue;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    int here = 0;
    for (int i = note_lo; i <= hi; i++) here += untuneNote(notes[static_cast<size_t>(i)]) ? 1 : 0;
    if (here == 0) continue;
    cleared += here;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
  return cleared;
}

TuningSummary
applyJustIntonationToSong(Song & song) {
  TuningSummary summary;
  auto context = contextOf(song);
  forEachSongPattern(song, [&](int track_id, PatternView pattern) {
    if (isPercussionTrack(song, track_id)) return;
    forEachNoteRow(pattern, [&](vector<Note> & notes) {
      auto before = summary.notes;
      for (auto & note : notes) tuneNote(note, context, summary);
      return summary.notes != before;
    });
  });
  return summary;
}

int
clearTuningCorrectionsInSong(Song & song) {
  int cleared = 0;
  forEachSongPattern(song, [&](int, PatternView pattern) {
    forEachNoteRow(pattern, [&](vector<Note> & notes) {
      bool changed = false;
      for (auto & note : notes) {
        if (untuneNote(note)) { changed = true; cleared++; }
      }
      return changed;
    });
  });
  return cleared;
}

void
transposeSong(Song & song, bool up) {
  int delta = up ? 1 : -1;
  int key = song.getKey();
  if (key + delta < 0) return; // the key would leave the note range
  IntonationContext context { song.getTuning(), key + delta };
  forEachSongPattern(song, [&](int track_id, PatternView pattern) {
    if (isPercussionTrack(song, track_id)) return;
    forEachNoteRow(pattern, [&](vector<Note> & notes) {
      if (notes.empty()) return false;
      for (auto & note : notes) transposeNote(note, delta, &context);
      return true;
    });
  });
  song.setKey(key + delta);
}

void
humanizeSong(Song & song, const HumanizeAmount & amount, NoiseGenerator & rng) {
  forEachSongPattern(song, [&](int, PatternView pattern) {
    forEachNoteRow(pattern, [&](vector<Note> & notes) {
      if (notes.empty()) return false;
      for (auto & note : notes) humanizeNote(note, amount, rng);
      return true;
    });
  });
}

void
pastePatternBlockNotes(PatternGrid & grid, const PatternBlock & block, int num_rows,
		       int target_row, int track_id, int target_note_offset) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;

    auto & row_cells = block[row_offset];
    if (row_cells.empty()) continue;
    auto & cell = row_cells[0];
    int pattern_row;
    auto pattern = grid.obtain(track_id, row, pattern_row);
    if (!pattern) continue;
    for (size_t i = 0; i < cell.notes.size(); i++) {
      pattern->setNote(pattern_row, target_note_offset + static_cast<int>(i), cell.notes[i]);
    }
  }
}

vector<Command>
copyPatternBlockCommand(const PatternGrid & grid, int row_lo, int row_hi, int track_id) {
  vector<Command> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(commandAt(grid, track_id, row));
  return block;
}

void
clearPatternBlockCommand(PatternGrid & grid, int row_lo, int row_hi, int track_id) {
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    if (auto pattern = grid.findCommands(track_id, row, pattern_row)) pattern->setCommand(pattern_row, Command());
  }
}

void
pastePatternBlockCommand(PatternGrid & grid, const vector<Command> & block, int num_rows,
			 int target_row, int track_id) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    int pattern_row;
    if (auto pattern = grid.obtainCommands(track_id, row, pattern_row)) pattern->setCommand(pattern_row, block[row_offset]);
  }
}

Clip extractClip(const PatternGrid & grid, int track_id, int row_lo, int row_hi, const BarGrid & bars) {
  auto bar_start = bars.barStart(row_lo);
  auto length = std::max(1, bars.roundUpToBar(row_hi + 1) - bar_start);

  Clip clip(track_id);
  clip.setLength(length);
  auto & pattern = clip.getLeafPattern();
  for (int row = row_lo; row <= row_hi; row++) {
    auto notes = notesAt(grid, track_id, row);
    if (!notes.empty()) pattern.setNotes(row - bar_start, notes);
    auto command = commandAt(grid, track_id, row);
    if (command.isDefined()) pattern.setCommand(row - bar_start, command);
  }
  return clip;
}

vector<string>
copyPatternBlockLocators(const Song & song, int first_row, int row_lo, int row_hi) {
  vector<string> block;
  for (int row = row_lo; row <= row_hi; row++) block.push_back(song.getLocator(first_row + row));
  return block;
}

void
clearPatternBlockLocators(Song & song, int first_row, int row_lo, int row_hi) {
  for (int row = row_lo; row <= row_hi; row++) song.setLocator(first_row + row, "");
}

void
pastePatternBlockLocators(Song & song, int first_row, const vector<string> & block, int num_rows, int target_row) {
  for (size_t row_offset = 0; row_offset < block.size(); row_offset++) {
    int row = target_row + static_cast<int>(row_offset);
    if (row < 0 || row >= num_rows) continue;
    song.setLocator(first_row + row, block[row_offset]);
  }
}

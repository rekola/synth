#include "PatternBlockOps.h"

#include "../model/Arrangement.h"
#include "../model/Song.h"
#include "../model/Clip.h"
#include "PatternGrid.h"
#include "JustIntonation.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <set>

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

// (row, column) -> the correction in cents of a pitched note.
using CorrectionMap = map<pair<int, int>, int>;

// The first row of the chord window `row` is in.
int windowStart(const IntonationContext & context, int row) {
  int index = context.bars.barIndex(row);
  int first = index - (((index % kChordWindowBars) + kChordWindowBars) % kChordWindowBars);
  return context.bars.barStartRow(first);
}

// The chord-aware correction of every pitched note of `pattern`: the notes
// that start in a window and the ones still held into it are one chord, its
// lowest note the bass the others are tuned above.
CorrectionMap chordCorrections(const PatternView & pattern, const IntonationContext & context) {
  CorrectionMap result;
  int edo = edoStepsFor(context.tuning);
  if (edo <= 0) return result;

  // Each column's events in row order: a note value, or -1 for an off.
  map<int, vector<pair<int, int> > > columns;
  set<int> windows;
  for (auto & [row, notes] : pattern.getNotesByRow()) {
    for (size_t column = 0; column < notes.size(); column++) {
      auto & note = notes[column];
      if (isPitched(note)) {
        columns[static_cast<int>(column)].push_back({row, note.getValue()});
        windows.insert(windowStart(context, row));
      } else if (note.isOff()) {
        columns[static_cast<int>(column)].push_back({row, -1});
      }
    }
  }

  for (int start : windows) {
    int end = context.bars.barStartRow(context.bars.barIndex(start) + kChordWindowBars);
    int bass = INT_MAX;
    for (auto & [column, events] : columns) {
      auto it = lower_bound(events.begin(), events.end(), make_pair(start, INT_MIN));
      if (it != events.begin() && prev(it)->second >= 0) bass = min(bass, prev(it)->second); // held into the window
      for (auto i = it; i != events.end() && i->first < end; ++i) {
        if (i->second >= 0) bass = min(bass, i->second);
      }
    }
    for (auto & [column, events] : columns) {
      for (auto i = lower_bound(events.begin(), events.end(), make_pair(start, INT_MIN)); i != events.end() && i->first < end; ++i) {
        if (i->second >= 0) result[{i->first, column}] = just_intonation::correctionCentsInChord(edo, i->second, bass, context.key);
      }
    }
  }
  return result;
}

// Writes `corrections` onto the pitched notes of `pattern_rows` in note
// columns [note_lo, note_hi]; with `only_tuned`, only onto notes that already
// carry one.
void writeCorrections(PatternView pattern, const set<int> & pattern_rows, const CorrectionMap & corrections,
                      int note_lo, int note_hi, bool only_tuned, TuningSummary & summary) {
  for (int row : pattern_rows) {
    auto notes = pattern.getNotes(row);
    if (notes.empty()) continue;
    bool changed = false;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) {
      auto & note = notes[static_cast<size_t>(i)];
      if (!isPitched(note) || (only_tuned && !note.hasTuningCorrection())) continue;
      auto found = corrections.find({row, i});
      if (found == corrections.end()) continue;
      if (note.hasFx() && !note.hasTuningCorrection()) summary.replaced++;
      note.setTuningCorrection(found->second);
      summary.notes++;
      changed = true;
    }
    if (!changed) continue;
      // setNotes()'s vector<Note> move-assign trips a known GCC false
      // positive (-Wfree-nonheap-object) once fully inlined.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern.setNotes(row, notes);
#pragma GCC diagnostic pop
  }
}

// Tunes the notes of one track in rows [row_lo, row_hi] and note columns
// [note_lo, note_hi]. The chord context comes from each row's whole pattern.
TuningSummary tuneTrackRows(PatternGrid & grid, int row_lo, int row_hi, int track_id, int note_lo, int note_hi,
                            const IntonationContext & context, bool only_tuned) {
  map<doc::NodeId, pair<PatternView, set<int> > > patterns;
  for (int row = row_lo; row <= row_hi; row++) {
    int pattern_row;
    auto pattern = grid.find(track_id, row, pattern_row);
    if (!pattern) continue;
    auto & entry = patterns[pattern.node()];
    entry.first = pattern;
    entry.second.insert(pattern_row);
  }
  TuningSummary summary;
  for (auto & [node, entry] : patterns) {
    writeCorrections(entry.first, entry.second, chordCorrections(entry.first, context), note_lo, note_hi, only_tuned, summary);
  }
  return summary;
}

bool untuneNote(Note & note) {
  if (!note.hasTuningCorrection()) return false;
  note.clearFx();
  return true;
}

// Every pattern that holds notes, each once: each track's background, then
// each clip. A clip is in the arrangement when some instance places it.
struct SongPattern {
  int track_id;
  PatternView pattern;
  bool in_scenes;      // a clip
  bool in_arrangement; // a background, or a clip that is placed
};

vector<SongPattern> songPatterns(Song & song) {
  vector<SongPattern> all;
  for (auto & [track_id, pattern] : song.getArrangement()->getPatternsByTrack()) {
    all.push_back({track_id, pattern, false, true});
  }
  auto instances = song.getArrangement()->getInstancesByTrack();
  for (auto track_id : song.clipTrackIds()) {
    set<string> placed;
    auto it = instances.find(track_id);
    if (it != instances.end()) {
      for (auto & [row, clip_id] : it->second) {
        if (!clip_id.empty()) placed.insert(clip_id);
      }
    }
    for (auto clip : song.getClips(track_id)) {
      all.push_back({track_id, clip.getLeafPattern(), true, !clip.getId().empty() && placed.count(clip.getId()) > 0});
    }
  }
  return all;
}

bool inScope(const SongPattern & entry, SongScope scope) {
  return scope == SongScope::SCENES ? entry.in_scenes : entry.in_arrangement;
}

// Calls fn(track_id, pattern) for every pattern the scope covers.
template <class Fn>
void forEachSongPattern(Song & song, SongScope scope, Fn fn) {
  for (auto & entry : songPatterns(song)) {
    if (inScope(entry, scope)) fn(entry.track_id, entry.pattern);
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
  return {song.getTuning(), song.getKey(), song.getArrangementBars()};
}

bool isPercussionTrack(const Song & song, int track_id) {
  auto * track = song.getMasterTrack().getChildByInternalId(track_id);
  return track && song.getTuningForTrack(*track) == Tuning::PERCUSSION;
}

// Tunes every pitched note of every pattern of the song (percussion tracks
// excepted); with `only_tuned`, only the ones that already carry a correction.
TuningSummary tuneSongPatterns(Song & song, SongScope scope, const IntonationContext & context, bool only_tuned) {
  TuningSummary summary;
  forEachSongPattern(song, scope, [&](int track_id, PatternView pattern) {
    if (isPercussionTrack(song, track_id)) return;
    auto corrections = chordCorrections(pattern, context);
    set<int> rows;
    for (auto & [position, cents] : corrections) rows.insert(position.first);
    writeCorrections(pattern, rows, corrections, 0, numeric_limits<int>::max(), only_tuned, summary);
  });
  return summary;
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
      for (auto & note : notes) note.transpose(up ? 1 : -1);
      pattern->setNotes(pattern_row, notes);
    }
  }
  if (!retune) return;
  for (int t = track_lo; t <= track_hi; t++) {
    auto track_id = track_ids[static_cast<size_t>(t)];
    if (!is_percussion(track_id)) tuneTrackRows(grid, row_lo, row_hi, track_id, 0, numeric_limits<int>::max(), *retune, true);
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
    for (int i = note_lo; i <= hi; i++) notes[static_cast<size_t>(i)].transpose(up ? 1 : -1);
      // setNotes()'s vector<Note> move-assign into the unordered_map, fully
      // inlined down from here, trips a known GCC false positive
      // (-Wfree-nonheap-object misattributing the vector's heap buffer as a
      // non-heap pointer) - not a real dangling-pointer bug.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
    pattern->setNotes(pattern_row, notes);
#pragma GCC diagnostic pop
  }
  if (retune) tuneTrackRows(grid, row_lo, row_hi, track_id, note_lo, note_hi, *retune, true);
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
  if (is_percussion) return {};
  return tuneTrackRows(grid, row_lo, row_hi, track_id, note_lo, note_hi, context, false);
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
applyJustIntonationToSong(Song & song, SongScope scope) {
  return tuneSongPatterns(song, scope, contextOf(song), false);
}

int clearTuningCorrectionsInSong(Song & song, SongScope scope) {
  int cleared = 0;
  forEachSongPattern(song, scope, [&](int, PatternView pattern) {
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

void transposeSong(Song & song, SongScope scope, bool up) {
  int delta = up ? 1 : -1;
  int key = song.getKey();
  // The key goes with the notes only when no pitched note is left behind.
  bool move_key = true;
  for (auto & entry : songPatterns(song)) {
    if (inScope(entry, scope) || isPercussionTrack(song, entry.track_id)) continue;
    if (!entry.pattern.getNotesByRow().empty()) move_key = false;
  }
  if (move_key && key + delta < 0) return; // the key would leave the note range
  forEachSongPattern(song, scope, [&](int track_id, PatternView pattern) {
    if (isPercussionTrack(song, track_id)) return;
    forEachNoteRow(pattern, [&](vector<Note> & notes) {
      if (notes.empty()) return false;
      for (auto & note : notes) note.transpose(delta);
      return true;
    });
  });
  // The notes (and the key, when it moves) have changed place together; a
  // tuned note gets the correction for its new place in its chord.
  int new_key = move_key ? key + delta : key;
  IntonationContext context{song.getTuning(), new_key, song.getArrangementBars()};
  tuneSongPatterns(song, scope, context, true);
  if (move_key) song.setKey(new_key);
}

void humanizeSong(Song & song, SongScope scope, const HumanizeAmount & amount, NoiseGenerator & rng) {
  forEachSongPattern(song, scope, [&](int, PatternView pattern) {
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

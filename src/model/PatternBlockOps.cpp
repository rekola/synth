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
  if (!note.isNoteOn()) return;
  auto velocity = note.getVelocity() + static_cast<int>(lround(rng.next() * static_cast<float>(amount.velocity)));
  auto delay = note.getDelay() + static_cast<int>(lround((rng.next() + 1.0f) * 0.5f * static_cast<float>(amount.delay)));
  note.setVelocity(static_cast<short>(clamp(velocity, 0, velocity::kMax)));
  note.setDelay(static_cast<short>(clamp(delay, 0, 255)));
}


bool isPitched(const Note & note) {
  return note.isNoteOn();
}

bool untuneNote(Note & note) {
  if (!note.hasTuningCorrection()) return false;
  note.clearFx();
  return true;
}

// Writes the gamut corrections onto the pitched notes of `pattern_rows` in note
// columns [note_lo, note_hi]; with `only_tuned`, only onto notes that already
// carry one. A key with no gamut note gets +00, which marks it as tuned. A
// scale with no gamut tunes nothing: an apply writes nothing, and a retune
// (`only_tuned`) removes the corrections it finds.
void writeCorrections(PatternView pattern, const set<int> & pattern_rows, const IntonationContext & context,
                      int note_lo, int note_hi, bool only_tuned, TuningSummary & summary) {
  bool tempered = context.scale.empty();
  if (tempered && !only_tuned) return;
  for (int row : pattern_rows) {
    auto notes = pattern.getNotes(row);
    if (notes.empty()) continue;
    bool changed = false;
    auto hi = min(note_hi, static_cast<int>(notes.size()) - 1);
    for (int i = note_lo; i <= hi; i++) {
      auto & note = notes[static_cast<size_t>(i)];
      if (!isPitched(note) || (only_tuned && !note.hasTuningCorrection())) continue;
      if (tempered) {
        if (untuneNote(note)) {
          summary.notes++;
          changed = true;
        }
        continue;
      }
      if (note.hasFx() && !note.hasTuningCorrection()) summary.replaced++;
      note.setTuningCorrection(context.scale.forNote(note.getValue()).value_or(0));
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
// [note_lo, note_hi].
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
    writeCorrections(entry.first, entry.second, context, note_lo, note_hi, only_tuned, summary);
  }
  return summary;
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
    set<int> rows;
    for (auto & [row, notes] : pattern.getNotesByRow()) rows.insert(row);
    writeCorrections(pattern, rows, context, 0, numeric_limits<int>::max(), only_tuned, summary);
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
  return tuneSongPatterns(song, scope, intonationContextOf(song), false);
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
  // tuned note gets the correction for its new place against the key.
  int new_key = move_key ? key + delta : key;
  IntonationContext context{song.getTuning(), new_key,
                            just_intonation::scaleCorrectionsFor(song.getTuning(), song.getScale(), new_key)};
  tuneSongPatterns(song, scope, context, true);
  if (move_key) song.setKey(new_key);
}

// The step of an EDO of `edo` steps nearest `cents`; an exact tie takes the
// lower one.
static int nearestStep(double cents, int edo) {
  return static_cast<int>(std::ceil(cents * edo / 1200.0 - 0.5 - 1e-9));
}

Note convertNoteTuning(const Note & note, Tuning from, Tuning to, int from_anchor, int to_anchor) {
  int from_edo = edoStepsFor(from), to_edo = edoStepsFor(to);
  if (from == to || from_edo <= 0 || to_edo <= 0 || note.getValue() < 0) return note;
  double interval = (note.getValue() - from_anchor) * 1200.0 / from_edo + note.getTuningCorrectionCents();
  int steps = nearestStep(interval, to_edo);
  auto value = std::max(to_anchor + steps, 0);
  Note moved(value, note.getVelocity(), note.getDelay());
  moved.setFx(note.getFx());
  if (note.hasTuningCorrection()) moved.setTuningCorrection(static_cast<int>(std::lround(interval - steps * 1200.0 / to_edo)));
  return moved;
}

int convertTuningAnchor(int value, Tuning from, Tuning to) {
  int from_edo = edoStepsFor(from), to_edo = edoStepsFor(to);
  if (from_edo <= 0 || to_edo <= 0) return value;
  return nearestStep(value * 1200.0 / from_edo, to_edo);
}

void convertBlockTuning(PatternBlock & block, size_t track_offset, Tuning from, Tuning to) {
  for (auto & row : block) {
    if (track_offset >= row.size()) continue;
    for (auto & note : row[track_offset].notes) note = convertNoteTuning(note, from, to, 0, 0);
  }
}

void convertClipTuning(Clip & clip, Tuning from, Tuning to) {
  auto & pattern = clip.getLeafPattern();
  auto rows = pattern.getNotesByRow();
  for (auto & [row, notes] : rows) {
    auto converted = notes;
    for (auto & note : converted) note = convertNoteTuning(note, from, to, 0, 0);
    pattern.setNotes(row, converted);
  }
}

int convertSongTuning(Song & song, Tuning to) {
  Tuning from = song.getTuning();
  if (from == to || edoStepsFor(from) <= 0 || edoStepsFor(to) <= 0) return 0;
  // The key moves to its nearest step and every note keeps its interval to
  // it, so the song's harmony is kept rather than the exact concert pitch.
  int key = song.getKey();
  int anchor = std::max(key, 0);
  int new_anchor = convertTuningAnchor(anchor, from, to);
  int converted = 0;
  for (auto & entry : songPatterns(song)) {
    if (isPercussionTrack(song, entry.track_id)) continue;
    forEachNoteRow(entry.pattern, [&](vector<Note> & notes) {
      bool changed = false;
      for (auto & note : notes) {
        if (note.getValue() < 0) continue; // aftertouch
        note = convertNoteTuning(note, from, to, anchor, new_anchor);
        changed = true;
        converted++;
      }
      return changed;
    });
  }
  song.setTuning(to);
  if (key >= 0) song.setKey(new_anchor);
  return converted;
}

int retuneSong(Song & song) {
  auto context = intonationContextOf(song);
  TuningSummary summary;
  for (auto & entry : songPatterns(song)) {
    if (isPercussionTrack(song, entry.track_id)) continue;
    set<int> rows;
    for (auto & [row, notes] : entry.pattern.getNotesByRow()) rows.insert(row);
    writeCorrections(entry.pattern, rows, context, 0, numeric_limits<int>::max(), true, summary);
  }
  return summary.notes;
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

IntonationContext
intonationContextOf(const Song & song) {
  return {song.getTuning(), song.getKey(),
          just_intonation::scaleCorrectionsFor(song.getTuning(), song.getScale(), song.getKey())};
}

void tuneNewNote(const Song & song, int track_id, Note & note) {
  if (!isPitched(note) || isPercussionTrack(song, track_id)) return;
  auto scale = just_intonation::scaleCorrectionsFor(song.getTuning(), song.getScale(), song.getKey());
  if (auto cents = scale.forNote(note.getValue())) note.setTuningCorrection(*cents);
}

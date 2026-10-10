#include "TestFramework.h"

#include "../src/model/Clip.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/PatternBlockOps.h"
#include "../src/model/PatternGrid.h"
#include "../src/model/Song.h"

#include <vector>

using namespace std;

namespace {

const int kKey = 31 * 5; // C5 in 31-EDO

IntonationContext context() { return { Tuning::EDO31, kKey }; }

bool never(int) { return false; }

}

TEST(apply_just_intonation_tunes_every_pitched_note_and_skips_what_has_no_pitch) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = { 10, 20 };

  arrangement.setNote(0, 10, 0, Note(kKey, 100));      // C
  arrangement.setNote(0, 10, 1, Note(kKey + 7, 100));  // D#
  arrangement.setNote(0, 10, 2, Note(kKey + 13, 100)); // F
  Note off(kKey, 0);
  arrangement.setNote(1, 10, 0, off);
  Note panned(kKey + 4, 100);
  panned.setFx("P80");
  arrangement.setNote(2, 10, 0, panned);
  arrangement.setNote(0, 20, 0, Note(36, 100)); // a drum

  auto summary = applyJustIntonationBlock(grid, 0, 2, track_ids, 0, 1, context(), [](int id) { return id == 20; });

  auto row0 = arrangement.getNotes(0, 10);
  CHECK(row0[0].getFx() == "+00"); // tuned, nothing to correct
  CHECK(row0[1].getFx() == "-04"); // 7/6
  CHECK(row0[2].getFx() == "-05"); // 4/3
  CHECK(!arrangement.getNotes(1, 10)[0].hasFx()); // an off has no pitch
  CHECK(arrangement.getNotes(2, 10)[0].getFx() == "+0A"); // 11/10, replacing the pan
  CHECK(!arrangement.getNotes(0, 20)[0].hasFx()); // percussion is never tuned
  CHECK(summary.notes == 4);
  CHECK(summary.replaced == 1);
}

TEST(apply_just_intonation_is_repeatable_and_note_column_scoped) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(kKey + 7, 100));
  arrangement.setNote(0, 10, 1, Note(kKey + 13, 100));

  applyJustIntonationBlockNotes(grid, 0, 0, 10, 1, 1, context(), false);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx()); // column 0 is outside the range
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "-05");

  auto again = applyJustIntonationBlockNotes(grid, 0, 0, 10, 0, 1, context(), false);
  CHECK(again.notes == 2);
  CHECK(again.replaced == 0); // an earlier correction is not "another fx"
  CHECK(arrangement.getNotes(0, 10)[0].getFx() == "-04");
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "-05");

  CHECK(applyJustIntonationBlockNotes(grid, 0, 0, 10, 0, 1, context(), true).notes == 0); // percussion
}

TEST(clear_tuning_correction_removes_only_the_correction) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = { 10 };
  Note tuned(kKey + 7, 100), panned(kKey + 4, 100);
  tuned.setTuningCorrection(-4);
  panned.setFx("PFF");
  arrangement.setNote(0, 10, 0, tuned);
  arrangement.setNote(0, 10, 1, panned);

  CHECK(clearTuningCorrectionBlock(grid, 0, 0, track_ids, 0, 0) == 1);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "PFF");
  CHECK(clearTuningCorrectionBlockNotes(grid, 0, 0, 10, 0, 1) == 0);
}

TEST(transposing_a_tuned_note_retunes_it_against_the_key) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = { 10 };
  arrangement.setNote(0, 10, 0, Note(kKey, 100));
  arrangement.setNote(0, 10, 1, Note(kKey + 7, 100));
  arrangement.setNote(0, 10, 2, Note(kKey + 13, 100));
  applyJustIntonationBlock(grid, 0, 0, track_ids, 0, 0, context(), never);

  auto ctx = context();
  for (int i = 0; i < 18; i++) transposePatternBlock(grid, 0, 0, track_ids, 0, 0, true, never, &ctx);

  auto notes = arrangement.getNotes(0, 10);
  CHECK(notes[0].getValue() == kKey + 18); // C became G ...
  CHECK(notes[0].getTuningCorrectionCents() == 5); // ... and is the key's perfect fifth
  CHECK(notes[1].getTuningCorrectionCents() == just_intonation::correctionCentsFor(31, 25));
  CHECK(notes[2].getTuningCorrectionCents() == just_intonation::correctionCentsFor(31, 31)); // back to the tonic

  // Without a context the corrections stay where they were.
  transposePatternBlock(grid, 0, 0, track_ids, 0, 0, true, never);
  CHECK(arrangement.getNotes(0, 10)[0].getTuningCorrectionCents() == 5);
}

TEST(transposing_leaves_an_untuned_note_untuned) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(kKey + 7, 100));
  auto ctx = context();
  transposePatternBlockNotes(grid, 0, 0, 10, 0, 0, true, false, &ctx);
  CHECK(arrangement.getNotes(0, 10)[0].getValue() == kKey + 8);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());
}

TEST(the_whole_song_is_tuned_cleared_and_transposed_with_its_key) {
  Song song(Tuning::EDO31, kKey);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  song.getArrangement().setNote(0, track_id, 0, Note(kKey + 7, 100));

  Clip clip(track_id);
  clip.setLength(4);
  clip.getLeafPattern().setNote(0, 0, Note(kKey + 13, 100));
  clip.getLeafPattern().setNote(2, 0, Note(kKey + 18, 100));
  auto clip_view = song.addClip(move(clip));
  Clip second(track_id);
  second.setLength(4);
  second.getLeafPattern().setNote(0, 0, Note(kKey + 4, 100));
  song.addClip(move(second));

  auto summary = applyJustIntonationToSong(song);
  CHECK(summary.notes == 4); // the background and both clips, each note once
  CHECK(song.getArrangement()->getNotes(0, track_id)[0].getFx() == "-04");
  CHECK(clip_view.getLeafPattern().getNote(0, 0).getFx() == "-05");
  CHECK(clip_view.getLeafPattern().getNote(2, 0).getFx() == "+05");

  // Moving the notes and the key together leaves the corrections as they were.
  transposeSong(song, true);
  CHECK(song.getKey() == kKey + 1);
  CHECK(song.getArrangement()->getNotes(0, track_id)[0].getValue() == kKey + 8);
  CHECK(song.getArrangement()->getNotes(0, track_id)[0].getFx() == "-04");
  CHECK(clip_view.getLeafPattern().getNote(2, 0).getValue() == kKey + 19);
  CHECK(clip_view.getLeafPattern().getNote(2, 0).getFx() == "+05");
  auto again = applyJustIntonationToSong(song);
  CHECK(again.replaced == 0);
  CHECK(clip_view.getLeafPattern().getNote(2, 0).getFx() == "+05");

  CHECK(clearTuningCorrectionsInSong(song) == 4);
  CHECK(!song.getArrangement()->getNotes(0, track_id)[0].hasFx());
  CHECK(!clip_view.getLeafPattern().getNote(0, 0).hasFx());
}

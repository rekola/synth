#include "TestFramework.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/Clip.h"
#include "../src/model/ClipView.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/PercussionTrack.h"
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

namespace {

// A background note and two clips on one track; the first clip is placed in
// the arrangement, the second is not.
struct ScopedSong {
  Song song{Tuning::EDO31, kKey};
  int track_id;
  ClipView placed, unplaced;

  explicit ScopedSong(bool background = true) {
    track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    if (background) song.getArrangement().setNote(0, track_id, 0, Note(kKey + 7, 100));
    Clip clip(track_id);
    clip.setLength(4);
    clip.getLeafPattern().setNote(0, 0, Note(kKey + 13, 100));
    clip.getLeafPattern().setNote(2, 0, Note(kKey + 18, 100));
    placed = song.addClip(move(clip));
    Clip second(track_id);
    second.setLength(4);
    second.getLeafPattern().setNote(0, 0, Note(kKey + 4, 100));
    unplaced = song.addClip(move(second));
    placeClipInstance(song, track_id, 16, 0); // clear of the background note
  }
  string background() { return string(song.getArrangement()->getNotes(0, track_id)[0].getFx()); }
};

} // namespace

TEST(scenes_tune_every_clip_placed_or_not_and_leave_the_background) {
  ScopedSong s;
  auto summary = applyJustIntonationToSong(s.song, SongScope::SCENES);
  CHECK(summary.notes == 3);
  CHECK(s.placed.getLeafPattern().getNote(0, 0).getFx() == "-05");
  CHECK(s.placed.getLeafPattern().getNote(2, 0).getFx() == "+05");
  CHECK(s.unplaced.getLeafPattern().getNote(0, 0).hasTuningCorrection());
  CHECK(!s.song.getArrangement()->getNotes(0, s.track_id)[0].hasFx());
}

TEST(the_arrangement_tunes_the_background_and_the_clips_it_places) {
  ScopedSong s;
  auto summary = applyJustIntonationToSong(s.song, SongScope::ARRANGEMENT);
  CHECK(summary.notes == 3);
  CHECK(s.background() == "-04");
  CHECK(s.placed.getLeafPattern().getNote(2, 0).getFx() == "+05");
  CHECK(!s.unplaced.getLeafPattern().getNote(0, 0).hasFx());
}

TEST(a_clip_placed_twice_is_tuned_once) {
  ScopedSong s(false);
  placeClipInstance(s.song, s.track_id, 32, 0);
  auto summary = applyJustIntonationToSong(s.song, SongScope::ARRANGEMENT);
  CHECK(summary.notes == 2);
  CHECK(s.placed.getLeafPattern().getNote(0, 0).getFx() == "-05");
}

TEST(clearing_takes_the_scope_too) {
  ScopedSong s;
  applyJustIntonationToSong(s.song, SongScope::SCENES);
  applyJustIntonationToSong(s.song, SongScope::ARRANGEMENT);
  CHECK(clearTuningCorrectionsInSong(s.song, SongScope::ARRANGEMENT) == 3);
  CHECK(!s.placed.getLeafPattern().getNote(0, 0).hasFx());
  CHECK(s.unplaced.getLeafPattern().getNote(0, 0).hasTuningCorrection());
  CHECK(clearTuningCorrectionsInSong(s.song, SongScope::SCENES) == 1);
}

TEST(the_key_moves_with_a_transpose_only_when_nothing_pitched_is_left_behind) {
  // Notes outside the scope (here the background) keep their place against the key.
  ScopedSong partial;
  applyJustIntonationToSong(partial.song, SongScope::SCENES);
  transposeSong(partial.song, SongScope::SCENES, true);
  CHECK(partial.song.getKey() == kKey);
  CHECK(partial.placed.getLeafPattern().getNote(0, 0).getValue() == kKey + 14);
  CHECK(partial.placed.getLeafPattern().getNote(0, 0).getTuningCorrectionCents() == just_intonation::correctionCentsFor(31, 14));
  CHECK(partial.song.getArrangement()->getNotes(0, partial.track_id)[0].getValue() == kKey + 7);

  // With only clips, Live View's select-all covers everything: the key goes along.
  ScopedSong clips_only(false);
  applyJustIntonationToSong(clips_only.song, SongScope::SCENES);
  transposeSong(clips_only.song, SongScope::SCENES, true);
  CHECK(clips_only.song.getKey() == kKey + 1);
  CHECK(clips_only.placed.getLeafPattern().getNote(2, 0).getValue() == kKey + 19);
  CHECK(clips_only.placed.getLeafPattern().getNote(2, 0).getFx() == "+05");
  CHECK(clips_only.unplaced.getLeafPattern().getNote(0, 0).getValue() == kKey + 5);
  CHECK(applyJustIntonationToSong(clips_only.song, SongScope::SCENES).replaced == 0);
}

namespace {

// A note value above the key, in 31-EDO.
int above(int steps) { return kKey + steps; }

} // namespace

TEST(an_arpeggio_is_tuned_as_one_chord) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  // D, F, A one after the other in a single column, all in the first bar.
  arrangement.setNote(0, 10, 0, Note(above(5), 100));
  arrangement.setNote(2, 10, 0, Note(above(13), 100));
  arrangement.setNote(4, 10, 0, Note(above(23), 100));
  // The next bar holds a lone G.
  arrangement.setNote(16, 10, 0, Note(above(18), 100));

  applyJustIntonationBlockNotes(grid, 0, 31, 10, 0, 0, context(), false);

  CHECK(arrangement.getNotes(0, 10)[0].getTuningCorrectionCents() == just_intonation::correctionCentsFor(31, 5));
  CHECK(arrangement.getNotes(2, 10)[0].getTuningCorrectionCents() == just_intonation::correctionCentsInChord(31, 13, 5, 0));
  CHECK(arrangement.getNotes(4, 10)[0].getTuningCorrectionCents() == just_intonation::correctionCentsInChord(31, 23, 5, 0));
  CHECK(arrangement.getNotes(2, 10)[0].getTuningCorrectionCents() != just_intonation::correctionCentsFor(31, 13));
  // Another bar is another chord: G stands alone, tuned from the key.
  CHECK(arrangement.getNotes(16, 10)[0].getTuningCorrectionCents() == just_intonation::correctionCentsFor(31, 18));
}

TEST(a_held_note_is_part_of_the_next_bars_chord_until_it_is_turned_off) {
  auto tune = [](bool turn_off) {
    Song song(Tuning::EDO31, kKey);
    auto arrangement = song.getArrangement();
    ArrangementBackgroundGrid grid(arrangement);
    arrangement.setNote(0, 10, 0, Note(above(5), 100)); // D, held on in column 0 ...
    if (turn_off) arrangement.setNote(8, 10, 0, Note(0, 0));
    arrangement.setNote(16, 10, 1, Note(above(13), 100)); // ... when F starts in the next bar
    applyJustIntonationBlockNotes(grid, 0, 31, 10, 0, 1, context(), false);
    return arrangement.getNotes(16, 10)[1].getTuningCorrectionCents();
  };
  CHECK(tune(false) == just_intonation::correctionCentsInChord(31, 13, 5, 0)); // F above the held D
  CHECK(tune(true) == just_intonation::correctionCentsFor(31, 13));            // F alone
}

TEST(a_region_tunes_only_its_own_notes_against_the_whole_chord) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(above(5), 100));
  arrangement.setNote(2, 10, 0, Note(above(13), 100));
  arrangement.setNote(4, 10, 0, Note(above(23), 100));

  auto summary = applyJustIntonationBlockNotes(grid, 2, 2, 10, 0, 0, context(), false);

  CHECK(summary.notes == 1);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());
  CHECK(!arrangement.getNotes(4, 10)[0].hasFx());
  // F was tuned above the D that is outside the region.
  CHECK(arrangement.getNotes(2, 10)[0].getTuningCorrectionCents() == just_intonation::correctionCentsInChord(31, 13, 5, 0));
}

TEST(a_percussion_track_is_left_alone) {
  Song song(Tuning::EDO31, kKey);
  auto track_id = song.addTrack(make_unique<PercussionTrack>()).getInternalId();
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, track_id, 0, Note(36, 100));
  vector<int> track_ids = {track_id};

  auto summary = applyJustIntonationBlock(grid, 0, 3, track_ids, 0, 0, context(), [](int) { return true; });
  CHECK(summary.notes == 0);
  CHECK(!arrangement.getNotes(0, track_id)[0].hasFx());
  CHECK(applyJustIntonationToSong(song, SongScope::ARRANGEMENT).notes == 0);
  CHECK(!arrangement.getNotes(0, track_id)[0].hasFx());
}

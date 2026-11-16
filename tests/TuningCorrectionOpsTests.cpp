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

#include <map>
#include <vector>

using namespace std;

namespace {

const int kKey = 31 * 5; // C5 in 31-EDO

// 31-EDO in C major: the duodene's corrections.
IntonationContext context() { return {Tuning::EDO31, kKey, just_intonation::scaleCorrectionsFor(Tuning::EDO31, Scale::MAJOR, kKey)}; }

bool never(int) { return false; }

}

TEST(apply_just_intonation_tunes_every_pitched_note_and_skips_what_has_no_pitch) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = { 10, 20 };

  arrangement.setNote(0, 10, 0, Note(kKey, 100));      // C
  arrangement.setNote(0, 10, 1, Note(kKey + 8, 100));  // E♭
  arrangement.setNote(0, 10, 2, Note(kKey + 13, 100)); // F
  auto off = Note::off();
  arrangement.setNote(1, 10, 0, off);
  Note panned(kKey + 10, 100);
  panned.setFx("P80");
  arrangement.setNote(2, 10, 0, panned);
  arrangement.setNote(0, 20, 0, Note(36, 100)); // a drum

  auto summary = applyJustIntonationBlock(grid, 0, 2, track_ids, 0, 1, context(), [](int id) { return id == 20; });

  auto row0 = arrangement.getNotes(0, 10);
  CHECK(row0[0].getFx() == "+00"); // tuned, nothing to correct
  CHECK(row0[1].getFx() == "+5F"); // 6/5
  CHECK(row0[2].getFx() == "-53"); // 4/3
  CHECK(!arrangement.getNotes(1, 10)[0].hasFx()); // an off has no pitch
  CHECK(arrangement.getNotes(2, 10)[0].getFx() == "-0D"); // 5/4, replacing the pan
  CHECK(!arrangement.getNotes(0, 20)[0].hasFx()); // percussion is never tuned
  CHECK(summary.notes == 4);
  CHECK(summary.replaced == 1);
}

TEST(apply_just_intonation_is_repeatable_and_note_column_scoped) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(kKey + 8, 100));
  arrangement.setNote(0, 10, 1, Note(kKey + 13, 100));

  applyJustIntonationBlockNotes(grid, 0, 0, 10, 1, 1, context(), false);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx()); // column 0 is outside the range
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "-53");

  auto again = applyJustIntonationBlockNotes(grid, 0, 0, 10, 0, 1, context(), false);
  CHECK(again.notes == 2);
  CHECK(again.replaced == 0); // an earlier correction is not "another fx"
  CHECK(arrangement.getNotes(0, 10)[0].getFx() == "+5F");
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "-53");

  CHECK(applyJustIntonationBlockNotes(grid, 0, 0, 10, 0, 1, context(), true).notes == 0); // percussion
}

TEST(clear_tuning_correction_removes_only_the_correction) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = { 10 };
  Note tuned(kKey + 8, 100), panned(kKey + 4, 100);
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
  arrangement.setNote(0, 10, 1, Note(kKey + 8, 100));
  arrangement.setNote(0, 10, 2, Note(kKey + 13, 100));
  applyJustIntonationBlock(grid, 0, 0, track_ids, 0, 0, context(), never);

  auto ctx = context();
  for (int i = 0; i < 18; i++) transposePatternBlock(grid, 0, 0, track_ids, 0, 0, true, never, &ctx);

  auto notes = arrangement.getNotes(0, 10);
  CHECK(notes[0].getValue() == kKey + 18); // C became G ...
  CHECK(notes[0].getTuningCorrectionCents() == 83.0 / 16);  // ... and is the key's perfect fifth
  CHECK(notes[1].getTuningCorrectionCents() == 178.0 / 16); // E♭ became B♭: 9/5
  CHECK(notes[2].getTuningCorrectionCents() == 0);  // F became the tonic

  // Without a context the corrections stay where they were.
  transposePatternBlock(grid, 0, 0, track_ids, 0, 0, true, never);
  CHECK(arrangement.getNotes(0, 10)[0].getTuningCorrectionCents() == 83.0 / 16);
}

TEST(transposing_a_tuned_note_in_a_scale_without_a_gamut_removes_its_correction) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  vector<int> track_ids = {10};
  arrangement.setNote(0, 10, 0, Note(kKey + 8, 100));
  applyJustIntonationBlock(grid, 0, 0, track_ids, 0, 0, context(), never);
  CHECK(arrangement.getNotes(0, 10)[0].hasTuningCorrection());

  IntonationContext tempered{Tuning::EDO31, kKey};
  transposePatternBlock(grid, 0, 0, track_ids, 0, 0, true, never, &tempered);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());
  CHECK(applyJustIntonationBlock(grid, 0, 0, track_ids, 0, 0, tempered, never).notes == 0);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());
}

TEST(transposing_leaves_an_untuned_note_untuned) {
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(kKey + 8, 100));
  auto ctx = context();
  transposePatternBlockNotes(grid, 0, 0, 10, 0, 0, true, false, &ctx);
  CHECK(arrangement.getNotes(0, 10)[0].getValue() == kKey + 9);
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
    song.setScale(Scale::MAJOR);
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
  CHECK(s.placed.getLeafPattern().getNote(0, 0).getFx() == "-53");
  CHECK(s.placed.getLeafPattern().getNote(2, 0).getFx() == "+53");
  CHECK(s.unplaced.getLeafPattern().getNote(0, 0).hasTuningCorrection());
  CHECK(!s.song.getArrangement()->getNotes(0, s.track_id)[0].hasFx());
}

TEST(the_arrangement_tunes_the_background_and_the_clips_it_places) {
  ScopedSong s;
  auto summary = applyJustIntonationToSong(s.song, SongScope::ARRANGEMENT);
  CHECK(summary.notes == 3);
  CHECK(s.background() == "+00"); // no gamut note has this key
  CHECK(s.placed.getLeafPattern().getNote(2, 0).getFx() == "+53");
  CHECK(!s.unplaced.getLeafPattern().getNote(0, 0).hasFx());
}

TEST(a_clip_placed_twice_is_tuned_once) {
  ScopedSong s(false);
  placeClipInstance(s.song, s.track_id, 32, 0);
  auto summary = applyJustIntonationToSong(s.song, SongScope::ARRANGEMENT);
  CHECK(summary.notes == 2);
  CHECK(s.placed.getLeafPattern().getNote(0, 0).getFx() == "-53");
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
  CHECK(partial.placed.getLeafPattern().getNote(0, 0).getFx() == "+00"); // F moved to a key no gamut note has
  CHECK(partial.song.getArrangement()->getNotes(0, partial.track_id)[0].getValue() == kKey + 7);

  // With only clips, Live View's select-all covers everything: the key goes along.
  ScopedSong clips_only(false);
  applyJustIntonationToSong(clips_only.song, SongScope::SCENES);
  transposeSong(clips_only.song, SongScope::SCENES, true);
  CHECK(clips_only.song.getKey() == kKey + 1);
  CHECK(clips_only.placed.getLeafPattern().getNote(2, 0).getValue() == kKey + 19);
  CHECK(clips_only.placed.getLeafPattern().getNote(2, 0).getFx() == "+53");
  CHECK(clips_only.unplaced.getLeafPattern().getNote(0, 0).getValue() == kKey + 5);
  CHECK(applyJustIntonationToSong(clips_only.song, SongScope::SCENES).replaced == 0);
}

namespace {

// A note value above the key, in 31-EDO.
int above(int steps) { return kKey + steps; }

} // namespace

TEST(a_note_is_tuned_the_same_whatever_sounds_with_it) {
  // D F A as a chord, and each alone in a bar of its own.
  Song song(Tuning::EDO31, kKey);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, 10, 0, Note(above(5), 100));
  arrangement.setNote(0, 10, 1, Note(above(13), 100));
  arrangement.setNote(0, 10, 2, Note(above(23), 100));
  arrangement.setNote(16, 10, 0, Note(above(5), 100));
  arrangement.setNote(32, 10, 0, Note(above(13), 100));
  arrangement.setNote(48, 10, 0, Note(above(23), 100));

  applyJustIntonationBlockNotes(grid, 0, 63, 10, 0, 2, context(), false);

  CHECK(arrangement.getNotes(0, 10)[0].getFx() == "+A6"); // 9/8
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "-53"); // 4/3
  CHECK(arrangement.getNotes(0, 10)[2].getFx() == "-5F"); // 5/3: D-A is 40/27, a comma flat of a fifth
  CHECK(arrangement.getNotes(16, 10)[0].getFx() == "+A6");
  CHECK(arrangement.getNotes(32, 10)[0].getFx() == "-53");
  CHECK(arrangement.getNotes(48, 10)[0].getFx() == "-5F");
}

TEST(a_region_tunes_only_its_own_notes) {
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
  CHECK(arrangement.getNotes(2, 10)[0].getFx() == "-53");
}

TEST(a_scale_without_a_gamut_writes_nothing) {
  Song song(Tuning::EDO31, kKey);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(kKey + 13, 100));
  for (auto scale : {Scale::NONE, Scale::RAST, Scale::HIJAZ}) {
    song.setScale(scale);
    CHECK(intonationContextOf(song).scale.empty());
    CHECK(applyJustIntonationToSong(song, SongScope::ARRANGEMENT).notes == 0);
    CHECK(!arrangement.getNotes(0, track_id)[0].hasFx());
  }
  song.setScale(Scale::MAJOR);
  CHECK(applyJustIntonationToSong(song, SongScope::ARRANGEMENT).notes == 1);
  song.setScale(Scale::RAST);
  CHECK(retuneSong(song) == 1); // the tuned note loses its correction
  CHECK(!arrangement.getNotes(0, track_id)[0].hasFx());
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

TEST(in_a_just_scale_every_note_gets_its_scale_ratio_whatever_its_chord) {
  // F A C E, all otonal notes.
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::OTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  arrangement.setNote(0, track_id, 0, Note(kKey + 13, 100));      // F
  arrangement.setNote(0, track_id, 1, Note(kKey + 23, 100));      // A
  arrangement.setNote(0, track_id, 2, Note(kKey + 31, 100));      // C
  arrangement.setNote(0, track_id, 3, Note(kKey + 31 + 10, 100)); // E
  vector<int> track_ids = {track_id};

  applyJustIntonationToSong(song, SongScope::ARRANGEMENT);
  auto notes = arrangement.getNotes(0, track_id);
  CHECK(notes[0].getFx() == "-53"); // 4/3
  CHECK(notes[1].getFx() == "-5F"); // 5/3
  CHECK(notes[2].getFx() == "+00");
  CHECK(notes[3].getFx() == "-0D"); // 5/4
}

TEST(a_borrowed_gamut_note_is_tuned_in_a_song_of_the_other_scale) {
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::OTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  Note e_flat(Note::stringToKey(Tuning::EDO31, "E♭5"), 100);
  tuneNewNote(song, track_id, e_flat);
  CHECK(e_flat.getFx() == "+5F"); // 6/5, though E♭ is not an otonal note

  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(kKey, 100));
  arrangement.setNote(0, track_id, 1, Note(kKey + 8, 100));
  arrangement.setNote(0, track_id, 2, Note(kKey + 18, 100));
  applyJustIntonationToSong(song, SongScope::ARRANGEMENT);
  CHECK(arrangement.getNotes(0, track_id)[1].getFx() == "+5F");
}

TEST(a_new_note_of_a_just_scale_is_written_with_its_correction) {
  Song song(Tuning::EDO31, kKey);
  auto pitched = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto drums = song.addTrack(make_unique<PercussionTrack>()).getInternalId();

  Note note(kKey + 7, 100);
  tuneNewNote(song, pitched, note);
  CHECK(!note.hasFx()); // no scale: no gamut, untuned
  song.setScale(Scale::RAST);
  tuneNewNote(song, pitched, note);
  CHECK(!note.hasFx()); // a maqam is tempered

  song.setScale(Scale::MAJOR);
  Note major_note(kKey + 18, 100);
  tuneNewNote(song, pitched, major_note);
  CHECK(major_note.getFx() == "+53"); // 3/2: a major song's live notes are tuned too

  song.setScale(Scale::OTONAL);
  tuneNewNote(song, pitched, note);
  CHECK(note.getFx() == "-42"); // 7/6
  Note utonal(kKey + 8, 100);
  tuneNewNote(song, pitched, utonal);
  CHECK(utonal.getFx() == "+5F"); // 6/5, borrowed from the utonal side of the gamut
  Note outside(kKey + 2, 100);
  tuneNewNote(song, pitched, outside);
  CHECK(!outside.hasFx());
  Note drum(kKey + 7, 100);
  tuneNewNote(song, drums, drum);
  CHECK(!drum.hasFx());
  auto off = Note::off();
  tuneNewNote(song, pitched, off);
  CHECK(!off.hasFx());
}

TEST(changing_the_scale_retunes_the_tuned_notes_and_leaves_the_rest) {
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::OTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(kKey + 13, 100));      // F
  arrangement.setNote(0, track_id, 1, Note(kKey + 23, 100));      // A
  arrangement.setNote(0, track_id, 2, Note(kKey + 31, 100));      // C
  arrangement.setNote(0, track_id, 3, Note(kKey + 31 + 10, 100)); // E
  Clip clip(track_id);
  clip.setLength(4);
  clip.getLeafPattern().setNote(0, 0, Note(kKey + 10, 100)); // E, in a clip nothing places
  auto unplaced = song.addClip(move(clip));
  applyJustIntonationToSong(song, SongScope::ARRANGEMENT);
  applyJustIntonationToSong(song, SongScope::SCENES);
  auto untuned = unplaced.getLeafPattern();
  untuned.setNote(2, 0, Note(kKey + 18, 100));
  CHECK(arrangement.getNotes(0, track_id)[3].getFx() == "-0D"); // 5/4, the scale's ratio

  song.setScale(Scale::MAJOR);
  CHECK(retuneSong(song) == 5);
  CHECK(arrangement.getNotes(0, track_id)[3].getFx() == "-0D"); // 5/4 in the duodene too
  CHECK(unplaced.getLeafPattern().getNote(0, 0).hasTuningCorrection());
  CHECK(!unplaced.getLeafPattern().getNote(2, 0).hasFx()); // an untuned note stays untuned

  song.setScale(Scale::RAST); // no gamut: the corrections go
  CHECK(retuneSong(song) == 5);
  CHECK(!arrangement.getNotes(0, track_id)[3].hasFx());
  CHECK(!unplaced.getLeafPattern().getNote(0, 0).hasFx());
}

TEST(transposing_a_just_scale_song_keeps_its_scale_ratios) {
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::OTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  Clip clip(track_id);
  clip.setLength(4);
  clip.getLeafPattern().setNote(0, 0, Note(kKey + 13, 100));      // F
  clip.getLeafPattern().setNote(0, 1, Note(kKey + 23, 100));      // A
  clip.getLeafPattern().setNote(0, 2, Note(kKey + 31, 100));      // C
  clip.getLeafPattern().setNote(0, 3, Note(kKey + 31 + 10, 100)); // E
  auto view = song.addClip(move(clip));
  applyJustIntonationToSong(song, SongScope::SCENES);

  transposeSong(song, SongScope::SCENES, true); // only clips: the key goes along
  CHECK(song.getKey() == kKey + 1);
  CHECK(view.getLeafPattern().getNote(0, 3).getFx() == "-0D"); // still 5/4
  CHECK(view.getLeafPattern().getNote(0, 0).getFx() == "-53");
}

namespace {

// The steps above the key of the notes on row 0, after a conversion.
vector<int> stepsAboveKey(const Song & song, PatternView pattern) {
  vector<int> steps;
  for (auto & note : pattern.getNotes(0)) steps.push_back(note.getValue() - song.getKey());
  return steps;
}

} // namespace

TEST(converting_a_septimal_song_to_53_edo_keeps_its_ratios_without_tuning) {
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::UTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(kKey + 25, 100)); // A♯↑, 7:4
  arrangement.setNote(0, track_id, 1, Note(kKey + 6, 100));  // E𝄫, 8:7
  arrangement.setNote(0, track_id, 2, Note(kKey + 24, 100)); // B𝄫, 12:7
  CHECK(convertSongTuning(song, Tuning::EDO53) == 3);
  CHECK(song.getTuning() == Tuning::EDO53);
  CHECK(song.getKey() == 53 * 5); // C stays C
  CHECK((stepsAboveKey(song, arrangement.findPattern(track_id)) == vector<int>{43, 10, 41}));
  CHECK(!arrangement.getNotes(0, track_id)[0].hasFx()); // an untuned note stays untuned
}

TEST(converting_a_tuned_chord_keeps_its_gamut_intervals) {
  Song song(Tuning::EDO31, kKey);
  song.setScale(Scale::MAJOR);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(kKey + 5, 100));  // D
  arrangement.setNote(0, track_id, 1, Note(kKey + 13, 100)); // F
  arrangement.setNote(0, track_id, 2, Note(kKey + 23, 100)); // A
  applyJustIntonationToSong(song, SongScope::ARRANGEMENT);
  convertSongTuning(song, Tuning::EDO53);
  auto steps = stepsAboveKey(song, arrangement.findPattern(track_id));
  CHECK(steps.size() == 3);
  if (steps.size() != 3) return;
  CHECK(steps[1] - steps[0] == 13); // 32:27: the duodene's D-F
  CHECK(steps[2] - steps[0] == 30); // 40:27: its D-A, a comma flat of 3:2
  for (auto & note : arrangement.getNotes(0, track_id)) {
    CHECK(note.hasTuningCorrection() && std::abs(note.getTuningCorrectionCents()) <= 11.2);
  }
}

TEST(converting_tuned_notes_there_and_back_is_the_identity) {
  const int key = 53 * 5;
  Song song(Tuning::EDO53, key);
  song.setScale(Scale::OTONAL);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  Clip clip(track_id);
  clip.setLength(4);
  int column = 0;
  for (int steps : {0, 9, 12, 17, 22, 31, 39, 43, 48}) clip.getLeafPattern().setNote(0, column++, Note(key + steps, 100));
  auto view = song.addClip(move(clip));
  applyJustIntonationToSong(song, SongScope::SCENES);
  auto before = view.getLeafPattern().getNotes(0);
  convertSongTuning(song, Tuning::EDO31);
  convertSongTuning(song, Tuning::EDO53);
  auto after = view.getLeafPattern().getNotes(0);
  CHECK(song.getKey() == key);
  CHECK(after.size() == before.size());
  for (size_t i = 0; i < before.size() && i < after.size(); i++) {
    CHECK(after[i].getValue() == before[i].getValue());
    CHECK(after[i].getFx() == before[i].getFx());
  }
}

TEST(converting_a_tie_takes_the_lower_step_and_leaves_percussion_alone) {
  const int key = 60;
  Song song(Tuning::EDO12, key);
  auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  auto drums_id = song.addTrack(make_unique<PercussionTrack>()).getInternalId();
  auto arrangement = song.getArrangement();
  arrangement.setNote(0, track_id, 0, Note(key + 6, 100)); // the tritone, halfway between two 31-EDO steps
  arrangement.setNote(0, drums_id, 0, Note(38, 100));
  convertSongTuning(song, Tuning::EDO31);
  CHECK(arrangement.getNotes(0, track_id)[0].getValue() == 31 * 5 + 15); // F♯, not G♭
  CHECK(arrangement.getNotes(0, drums_id)[0].getValue() == 38);
}

TEST(a_copied_block_converts_one_track_by_pitch_anchored_at_c) {
  PatternBlock block(1);
  block[0].resize(2);
  block[0][0].notes = {Note(31 * 5 + 25, 100), Note(31 * 5, 100)}; // A♯4 (7:4), C4
  block[0][1].notes = {Note(31 * 5 + 25, 100)};                    // another track: left alone
  Note tuned(31 * 5 + 10, 100);                                    // E4, tuned to 5:4
  tuned.setTuningCorrection(-1);
  block[0][0].notes.push_back(tuned);
  convertBlockTuning(block, 0, Tuning::EDO31, Tuning::EDO53);
  CHECK(block[0][0].notes[0].getValue() == 53 * 5 + 43);
  CHECK(block[0][0].notes[1].getValue() == 53 * 5); // C stays C
  CHECK(block[0][0].notes[2].getValue() == 53 * 5 + 17);
  CHECK(block[0][0].notes[2].hasTuningCorrection()); // what is left of 5:4 after 53-EDO's E
  CHECK(std::abs(block[0][0].notes[2].getTuningCorrectionCents()) <= 2);
  CHECK(!block[0][0].notes[0].hasFx());
  CHECK(block[0][1].notes[0].getValue() == 31 * 5 + 25);
}

TEST(a_copied_clip_converts_by_pitch) {
  Clip clip(1);
  clip.setLength(4);
  clip.getLeafPattern().setNote(0, 0, Note(53 * 5 + 31, 100)); // G4
  clip.getLeafPattern().setNote(2, 0, Note(53 * 5 + 9, 100));  // D4
  convertClipTuning(clip, Tuning::EDO53, Tuning::EDO12);
  CHECK(clip.getLeafPattern().getNote(0, 0).getValue() == 60 + 7);
  CHECK(clip.getLeafPattern().getNote(2, 0).getValue() == 60 + 2);
}

namespace {

// A one-track song in `tuning`, scale `scale` and key `key` with a note at each of `steps` above the key (one per column of row 0).
struct TunedSong {
  Song song;
  int track_id;
  TunedSong(Tuning tuning, Scale scale, int key, const vector<int> & steps) : song(tuning, key) {
    song.setScale(scale);
    track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    auto arrangement = song.getArrangement();
    for (size_t i = 0; i < steps.size(); i++) arrangement.setNote(0, track_id, static_cast<int>(i), Note(key + steps[i], 100));
    applyJustIntonationToSong(song, SongScope::ARRANGEMENT);
  }
  vector<Note> notes() { return song.getArrangement().getNotes(0, track_id); }
};

} // namespace

TEST(in_53_edo_a_c_otonal_song_gets_e_plus_17_and_a_sharp_up_minus_4c) {
  const int key = 53 * 5;
  TunedSong s(Tuning::EDO53, Scale::OTONAL, key, {17, 43});
  CHECK(s.notes()[0].getFx() == "+17"); // 5/4
  CHECK(s.notes()[1].getFx() == "-4C"); // 7/4
}

TEST(the_two_ratios_on_key_2_get_different_corrections_by_scale) {
  const int key = 53 * 5;
  CHECK(TunedSong(Tuning::EDO53, Scale::OTONAL, key, {2}).notes()[0].getFx() == "-99"); // 49/48
  CHECK(TunedSong(Tuning::EDO53, Scale::UTONAL, key, {2}).notes()[0].getFx() == "-A5"); // 50/49
}

TEST(in_12_edo_a_c_otonal_song_gets_a_fine_e_and_a_coarse_a_sharp) {
  const int key = 60;
  TunedSong s(Tuning::EDO12, Scale::OTONAL, key, {4, 10});
  CHECK(s.notes()[0].getFx() == "-DB"); // 5/4: 13.69 cents flat
  CHECK(!s.notes()[0].hasCoarseTuningCorrection());
  // Key 10 is shared by 7/4 and 9/5, both of the otonal scale; the one nearer
  // the key (9/5, 17.6 cents sharp) takes it, which the fine range does not reach.
  CHECK(s.notes()[1].getFx() == ">12");
  CHECK(s.notes()[1].hasCoarseTuningCorrection());
  // 7/4 itself, 31.17 cents flat of the key, is written coarse as well.
  Note seven_four(key + 10, 100);
  CHECK(seven_four.setTuningCorrection(1200.0 * std::log2(7.0 / 4.0) - 1000.0));
  CHECK(seven_four.getFx() == "<1F");
}

TEST(every_note_of_the_otonal_and_utonal_scales_sounds_within_a_32nd_of_a_cent_of_its_ratio) {
  for (auto tuning : {Tuning::EDO53, Tuning::EDO31}) {
    int edo = edoStepsFor(tuning);
    for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
      auto just = justScale(scale);
      if (!just.has_value()) {
        CHECK(false);
        continue;
      }
      const auto gamut = just.value().gamut;
      vector<int> steps;
      vector<double> ratios;
      for (auto i : scaleGamutIndices(scale)) {
        steps.push_back(((gamut[i].ratio.steps(edo) % edo) + edo) % edo);
        ratios.push_back(gamut[i].ratio.cents());
      }
      CHECK(steps.size() == 18);
      TunedSong s(tuning, scale, edo * 5, steps);
      auto notes = s.notes();
      CHECK(notes.size() == steps.size());
      for (size_t i = 0; i < steps.size() && i < notes.size(); i++) {
        double sounding = steps[i] * 1200.0 / edo + notes[i].getTuningCorrectionCents();
        CHECK(std::fabs(sounding - ratios[i]) <= 1.0 / 32 + 1e-9);
      }
    }
  }
}

TEST(clear_tuning_correction_removes_fine_and_coarse_corrections) {
  const int key = 60;
  TunedSong s(Tuning::EDO12, Scale::OTONAL, key, {4, 10, 7});
  auto arrangement = s.song.getArrangement();
  Note panned(key + 2, 100);
  panned.setFx("PFF");
  arrangement.setNote(0, s.track_id, 3, panned);
  CHECK(s.notes()[0].hasTuningCorrection() && !s.notes()[0].hasCoarseTuningCorrection());
  CHECK(s.notes()[1].hasCoarseTuningCorrection());
  CHECK(clearTuningCorrectionsInSong(s.song, SongScope::ARRANGEMENT) == 3);
  for (int i = 0; i < 3; i++) CHECK(!s.notes()[static_cast<size_t>(i)].hasFx());
  CHECK(s.notes()[3].getFx() == "PFF");
}

TEST(a_correction_beyond_255_cents_leaves_the_note_untuned_and_is_counted) {
  const int key = 60;
  // A context whose key 4 asks for 300 cents, key 5 for 0.5.
  just_intonation::ScaleCorrections scale;
  scale.edo = 12;
  scale.key = key % 12;
  scale.cents[4] = 300.0;
  scale.present[4] = true;
  scale.cents[5] = 0.5;
  scale.present[5] = true;
  IntonationContext ctx{Tuning::EDO12, key, scale};

  Song song(Tuning::EDO12, key);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  Note far(key + 4, 100), near(key + 5, 100), previously_tuned(key + 4, 100), panned(key + 4, 100);
  previously_tuned.setTuningCorrection(2.0);
  panned.setFx("P80");
  arrangement.setNote(0, 10, 0, far);
  arrangement.setNote(0, 10, 1, near);
  arrangement.setNote(1, 10, 0, previously_tuned);
  arrangement.setNote(2, 10, 0, panned);
  auto summary = applyJustIntonationBlockNotes(grid, 0, 2, 10, 0, 1, ctx, false);
  CHECK(summary.notes == 1);
  CHECK(summary.untuned == 3);
  CHECK(!arrangement.getNotes(0, 10)[0].hasFx());         // never clamped to 255
  CHECK(arrangement.getNotes(0, 10)[1].getFx() == "+08"); // 0.5 cent
  CHECK(!arrangement.getNotes(1, 10)[0].hasFx());         // the stale correction goes
  CHECK(arrangement.getNotes(2, 10)[0].getFx() == "P80"); // other fx is not destroyed
  CHECK(summary.replaced == 0);
}

TEST(a_new_note_is_written_with_the_corrections_quantized_value) {
  // Live, recorded and typed notes agree with what a playing live note sounds at.
  for (auto [tuning, key] : {pair{Tuning::EDO12, 60}, pair{Tuning::EDO31, 155}, pair{Tuning::EDO53, 265}}) {
    Song song(tuning, key);
    song.setScale(Scale::OTONAL);
    auto track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
    auto corrections = just_intonation::scaleCorrectionsFor(tuning, Scale::OTONAL, key);
    for (int step = 0; step < edoStepsFor(tuning); step++) {
      Note note(key + step, 100);
      tuneNewNote(song, track_id, note);
      auto exact = corrections.forNote(key + step);
      CHECK(note.hasTuningCorrection() == exact.has_value());
      if (!exact) continue;
      auto quantized = Note::quantizeTuningCorrection(*exact);
      CHECK(quantized.has_value());
      CHECK(note.getTuningCorrectionCents() == quantized.value_or(1e9));
      CHECK(std::fabs(note.getTuningCorrectionCents() - *exact) <= (note.hasCoarseTuningCorrection() ? 0.5 : 1.0 / 32) + 1e-9);
    }
  }
}

TEST(converting_a_tuned_note_to_another_tuning_keeps_its_pitch_to_a_32nd_of_a_cent) {
  Note tuned(4, 100);
  tuned.setTuningCorrection(-13.6860); // 5/4 against 12-EDO's E: written -DB, 13.6875
  double before = 400.0 + tuned.getTuningCorrectionCents();
  auto moved = convertNoteTuning(tuned, Tuning::EDO12, Tuning::EDO53, 0, 0);
  CHECK(moved.hasTuningCorrection());
  CHECK(std::fabs(moved.getValue() * 1200.0 / 53 + moved.getTuningCorrectionCents() - before) <= 1.0 / 32 + 1e-9);
}

TEST(converting_a_coarse_corrected_note_keeps_its_pitch_to_half_a_cent) {
  Note tuned(10, 100);
  tuned.setTuningCorrection(-31.0); // 7/4 against 12-EDO's B-flat: written <1F
  CHECK(tuned.hasCoarseTuningCorrection());
  double before = 1000.0 + tuned.getTuningCorrectionCents();
  auto moved = convertNoteTuning(tuned, Tuning::EDO12, Tuning::EDO53, 0, 0);
  CHECK(moved.hasTuningCorrection());
  CHECK(std::fabs(moved.getValue() * 1200.0 / 53 + moved.getTuningCorrectionCents() - before) <= 1.0 / 32 + 1e-9);
}

#include "TestFramework.h"

#include "Sf2Fixture.h"
#include "../src/model/Song.h"
#include "../src/model/Note.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/Additive.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/PadSynth.h"
#include "../src/instruments/SoundFont.h"
#include "../src/instruments/WaveformType.h"
#include "../src/effects/EnvelopeFilter.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/SphericalPosition.h"
#include "../src/model/SendLevels.h"
#include "../src/state/EnvelopeState.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/state/RenderContext.h"
#include "../src/util/Velocity.h"

#include <cmath>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace sf2fixture;

namespace {

// Frames a voice takes to end after an OFF with release velocity `lift7`,
// rendered in blocks of 64 after the note has been held for a while.
int framesToEnd(VoiceState & voice, int lift7) {
  for (int i = 0; i < 8; i++) voice.render(512);
  voice.setReleaseVelocity(lift7);
  voice.stopNote();
  int frames = 0;
  while (voice.isActive() && frames < 44100 * 60) {
    voice.render(64);
    frames += 64;
  }
  return frames;
}

// A one-second-release envelope around `child`, played and released.
int envelopeReleaseFrames(std::shared_ptr<Track> child, int lift7) {
  EnvelopeFilter filter;
  MemoryParameterSource params;
  params.set("attack", 0.0f);
  params.set("decay", 0.0f);
  params.set("sustain", 1.0f);
  params.set("release", 1.0f);
  filter.loadParameters(params);
  filter.addChild(std::move(child));
  ChannelConfiguration config(44100, 1);
  auto voice = filter.playNote(config, SphericalPosition{}, SpatialMode::AUTO, Tuning::EDO12, 1.0f, 0.8f, 60, SendLevels{}, NoteCoordinate{});
  return framesToEnd(*voice, lift7);
}

void checkReleaseScaling(const std::function<int(int)> & frames_for) {
  const float base = static_cast<float>(frames_for(64));
  CHECK(base > 1000.0f);
  CHECK_NEAR(static_cast<float>(frames_for(0)) / base, 4.0f, 0.1f);
  CHECK_NEAR(static_cast<float>(frames_for(127)) / base, 0.2551f, 0.03f);
}

} // namespace

TEST(velocity_converts_between_7_bits_and_a_level) {
  CHECK(velocity::floatTo7(0.0f) == 0);
  CHECK(velocity::floatTo7(1.0f) == 127);
  CHECK(velocity::floatTo7(2.0f) == 127);
  for (int v = 0; v <= 127; v++) CHECK(velocity::floatTo7(velocity::toFloat(v)) == v);
}

TEST(release_time_factor_follows_the_lift) {
  CHECK(velocity::releaseTimeFactor(64) == 1.0f);
  CHECK_NEAR(velocity::releaseTimeFactor(0), 4.0f, 1e-5f);
  CHECK_NEAR(velocity::releaseTimeFactor(127), 0.2551f, 1e-3f);
  CHECK_NEAR(velocity::releaseTimeFactor(96), 0.5f, 1e-5f);
}

TEST(sf2_release_seconds_add_timecents_within_the_spec_range) {
  CHECK(velocity::sf2ReleaseSeconds(0.7f, 64) == 0.7f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(1.0f, 0), 4.0f, 1e-4f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(1.0f, 127), 0.2551f, 1e-3f);
  CHECK(velocity::sf2ReleaseSeconds(0.0f, 0) == 0.0f); // a pinned instant release stays so
  // The range of the release generators: -12000 to 8000 timecents.
  CHECK_NEAR(velocity::sf2ReleaseSeconds(60.0f, 0), std::exp2(8000.0f / 1200.0f), 0.01f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(0.002f, 127), std::exp2(-12000.0f / 1200.0f), 1e-6f);
}

TEST(a_note_is_8_bytes_and_keeps_its_kind) {
  static_assert(sizeof(Note) == 8);
  CHECK(!Note().isDefined());
  Note on(60, 100, 3);
  CHECK(on.isNoteOn() && !on.isOff() && !on.isAftertouch());
  CHECK(on.getVelocity() == 100);
  CHECK(on.getDelay() == 3);
  auto off = Note::off();
  CHECK(off.isOff() && !off.isNoteOn());
  CHECK(off.getVelocity() == constants::NEUTRAL_VELOCITY);
  CHECK(Note::off(0).isOff());
  CHECK(Note::off(0).getVelocity() == 0);
  CHECK(Note::aftertouch(5).isAftertouch());
  CHECK(Note(60).getVelocity() == constants::NEUTRAL_VELOCITY);
  CHECK(Note("off", Tuning::EDO12).isOff());
  CHECK(Note("C-4", Tuning::EDO12).isNoteOn());
  // Out-of-range ints are held to the range, not wrapped through a narrower type.
  Note held(60, 100);
  held.setVelocity(300);
  CHECK(held.getVelocity() == 127);
  held.setVelocity(65536 + 5);
  CHECK(held.getVelocity() == 127);
  held.setVelocity(-70000);
  CHECK(held.getVelocity() == 0);
  CHECK(Note(60, 65536 + 5).getVelocity() == 127);
  CHECK(Note::off(1000).getVelocity() == 127);
  CHECK(Note(60, 100, 65536 + 5).getDelay() == 255);
  // No value: aftertouch. No velocity: an empty cell, or fx only.
  CHECK(Note(-1, 50).isAftertouch() && !Note(-1, 50).isNoteOn());
  Note empty;
  CHECK(!empty.isDefined() && !empty.isAftertouch() && empty.getValue() == -1);
  empty.setVelocity(0); // the type is the value's: this stays an empty cell
  CHECK(!empty.isDefined());
  // The type lives in the value: the top three values are aftertouch, OFF and undefined.
  CHECK(Note(60, 10).isNoteOn() && Note::off(10).getValue() == -1 && Note::aftertouch(10).getValue() == -1);
  for (auto note : {Note(0, 3), Note(123456, 100, 9), Note::off(7, 2), Note::aftertouch(0, 4), Note()}) {
    auto back = Note::fromCode(note.toCode(), note.getVelocity(), note.getDelay());
    CHECK(back.isNoteOn() == note.isNoteOn() && back.isOff() == note.isOff() && back.isAftertouch() == note.isAftertouch());
    CHECK(back.isDefined() == note.isDefined() && back.getValue() == note.getValue() && back.getVelocity() == note.getVelocity());
  }
  Note high(1 << 24, 5);
  CHECK(high.isNoteOn()); // the largest values are held below the three that mean other things
}

TEST(a_velocity_0_note_on_is_a_real_note_that_starts_silent) {
  Note note(60, 0);
  CHECK(note.isDefined());
  CHECK(note.isNoteOn());
  CHECK(!note.isOff());
  CHECK(!note.isAftertouch());
  CHECK(note.getVelocity() == 0);
  CHECK(note.getVelocityAsFloat() == 0.0f);
  // A voice started on it is alive, and silent.
  OscillatorVoice voice(ChannelConfiguration(44100), SphericalPosition{}, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, note.getVelocityAsFloat(), 42);
  CHECK(voice.isActive());
  CHECK(voice.getLoudness() == 0.0f);
  // And it is not an OFF to the event the player builds.
  TrackEvent event(0, Tuning::EDO12, note.getVelocityAsFloat(), note.getValue());
  CHECK(!event.isOff());
  CHECK(!event.isAftertouch());
}

TEST(a_velocity_0_note_on_renders_silent_without_being_an_off) {
  auto render_peak = [](int velocity_value) {
    Song song(Tuning::EDO12);
    song.addInstrument(std::make_unique<Oscillator>(WaveformType::SINE));
    auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
    song.getArrangement().setNote(0, track.getInternalId(), 0, Note(60, static_cast<short>(velocity_value)));
    song.getArrangement().setNote(8, track.getInternalId(), 0, Note::off());
    auto result = renderSongOffline(song, ChannelConfiguration(44100, 1));
    float peak = 0.0f;
    for (auto v : result.interleaved) peak = std::max(peak, std::fabs(v));
    return peak;
  };
  auto silent = render_peak(0);
  CHECK(silent == 0.0f);
  CHECK(render_peak(100) > 0.0f);
}

TEST(envelope_release_scales_with_the_lift) {
  EnvelopeState envelope(44100, Envelope(0.0f, 0.0f, 1.0f, 1.0f), 60, 100, true);
  auto frames = [&](int lift7) {
    EnvelopeState e = envelope;
    e.beginRelease(e.parameters.release_ * velocity::releaseTimeFactor(lift7));
    int n = 0;
    while (!e.isDone() && n < 44100 * 20) {
      e.process(1);
      n++;
    }
    return n;
  };
  CHECK(std::abs(frames(64) - 44100) <= 1);
  CHECK(std::abs(frames(0) - 4 * 44100) <= 2);
  CHECK_NEAR(static_cast<float>(frames(127)), 0.2551f * 44100.0f, 20.0f);
}

TEST(user_instrument_release_scales_with_the_lift) {
  checkReleaseScaling([](int lift7) { return envelopeReleaseFrames(std::make_shared<Oscillator>(WaveformType::SINE), lift7); });
}

TEST(additive_release_scales_with_the_lift) {
  checkReleaseScaling([](int lift7) { return envelopeReleaseFrames(std::make_shared<Additive>(), lift7); });
}

TEST(padsynth_release_scales_with_the_lift) {
  checkReleaseScaling([](int lift7) { return envelopeReleaseFrames(std::make_shared<PadSynth>(), lift7); });
}

TEST(soundfont_release_scales_with_the_lift) {
  // One looping region with a one-second volume-envelope release (0 timecents).
  std::vector<PresetSpec> presets = {
      {"Held", 89, {GenSpec{54, 1}, GenSpec{38, 0}}, {}},
  };
  auto fixture_path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_sf2_release_fixture.sf2").string();
  writeMinimalSf2(fixture_path, presets);
  SoundFont sf(fixture_path);
  checkReleaseScaling([&](int lift7) {
    auto instrument = sf.createInstrument(0);
    ChannelConfiguration config(44100, 1);
    auto voice = instrument->playNote(config, SphericalPosition{}, SpatialMode::AUTO, Tuning::EDO12, 1.0f, 0.8f, 60, SendLevels{}, NoteCoordinate{});
    return framesToEnd(*voice, lift7);
  });
}

TEST(an_off_event_carries_its_release_velocity) {
  RenderContext context(ChannelConfiguration(44100));
  context.addPendingOff(1, 0, 0, Tuning::EDO12, velocity::toFloat(20));
  auto & event = context.getPendingEvents(1)[0][0];
  CHECK(event.isOff());
  CHECK(velocity::floatTo7(event.getVelocity()) == 20);
}

namespace {

std::string slurp(const std::string & path) {
  std::ifstream in(path);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}

} // namespace

TEST(velocities_lifts_and_aftertouch_survive_a_save_and_reload) {
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_round_trip_scratch.xml").string();
  Song song(Tuning::EDO12);
  auto track_to_add = std::make_unique<InstrumentTrack>(0);
  track_to_add->setId("t");
  auto & track = song.addTrack(std::move(track_to_add));
  auto id = track.getInternalId();
  song.getArrangement().setNote(0, id, 0, Note(60, 0));
  song.getArrangement().setNote(1, id, 0, Note::off(0));
  song.getArrangement().setNote(2, id, 0, Note(60, 100));
  song.getArrangement().setNote(3, id, 0, Note::off());
  song.getArrangement().setNote(4, id, 0, Note::off(127, 9));
  song.getArrangement().setNote(5, id, 0, Note::aftertouch(90, 7));
  song.getArrangement().setNote(6, id, 0, Note::aftertouch(0));
  song.getArrangement().setNote(7, id, 0, Note::aftertouch(constants::NEUTRAL_VELOCITY));
  Note fx_only;
  fx_only.setFx("P80");
  song.getArrangement().setNote(8, id, 0, fx_only);
  song.save(path);

  auto text = slurp(path);
  CHECK(text.find("\u00b7") == std::string::npos); // no placeholder dots in the file

  InstrumentProvider provider;
  Song reloaded(Tuning::EDO12);
  CHECK(reloaded.open(path, provider));
  auto reloaded_track = reloaded.getMasterTrack().getChildById("t");
  CHECK(reloaded_track != nullptr);
  if (!reloaded_track) return;
  auto reloaded_id = reloaded_track->getInternalId();
  auto at = [&](int row) { return reloaded.getArrangement().getNote(row, reloaded_id, 0); };
  CHECK(at(0).isNoteOn() && at(0).getVelocity() == 0);
  CHECK(at(1).isOff() && at(1).getVelocity() == 0);
  CHECK(at(2).isNoteOn() && at(2).getVelocity() == 100);
  CHECK(at(3).isOff() && at(3).getVelocity() == constants::NEUTRAL_VELOCITY);
  CHECK(at(4).isOff() && at(4).getVelocity() == 127 && at(4).getDelay() == 9);
  CHECK(at(5).isAftertouch() && at(5).getVelocity() == 90 && at(5).getDelay() == 7);
  CHECK(at(6).isAftertouch() && at(6).getVelocity() == 0);
  CHECK(at(7).isAftertouch() && at(7).getVelocity() == constants::NEUTRAL_VELOCITY);
  CHECK(!at(8).isDefined() && at(8).getFx() == "P80"); // fx only stays fx only
  CHECK(text.find("AFT") == std::string::npos);
}

TEST(a_song_file_off_without_a_velocity_has_the_neutral_lift) {
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_plain_off_scratch.xml").string();
  Song song(Tuning::EDO12);
  auto track_to_add = std::make_unique<InstrumentTrack>(0);
  track_to_add->setId("t");
  auto & track = song.addTrack(std::move(track_to_add));
  song.getArrangement().setNote(4, track.getInternalId(), 0, Note::off());
  song.save(path);
  auto text = slurp(path);
  CHECK(text.find("OFF") != std::string::npos);
  CHECK(text.find("velocity") == std::string::npos); // neutral is left out
}

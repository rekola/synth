#include "TestFramework.h"

#include "Sf2Fixture.h"
#include "../src/model/Song.h"
#include "../src/model/Note.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/Additive.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/Oscillator.h"
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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
  CHECK_NEAR(static_cast<float>(frames_for(127)) / base, 0.2658f, 0.03f);
}

} // namespace

TEST(velocity_scales_between_7_and_16_bits) {
  CHECK(velocity::from7(0) == 0);
  CHECK(velocity::from7(64) == 0x8000); // the centre stays the centre
  CHECK(velocity::from7(127) == 0xFFFF);
  for (int v = 0; v <= 127; v++) CHECK(velocity::to7(velocity::from7(v)) == v);
  for (int v = 1; v <= 127; v++) CHECK(velocity::from7(v) > velocity::from7(v - 1));
  CHECK(velocity::from7(500) == 0xFFFF);
  CHECK(velocity::from7(-3) == 0);
  CHECK(velocity::to7(0xFFFF) == 127);
  CHECK(velocity::floatTo7(0.5f) == 64);
  CHECK(velocity::floatTo7(1.0f) == 127);
  CHECK(velocity::floatTo7(0.0f) == 0);
}

TEST(release_time_factor_follows_the_lift) {
  CHECK(velocity::releaseTimeFactor(64) == 1.0f);
  CHECK_NEAR(velocity::releaseTimeFactor(0), 4.0f, 1e-5f);
  CHECK_NEAR(velocity::releaseTimeFactor(127), 0.2658f, 1e-3f);
  CHECK_NEAR(velocity::releaseTimeFactor(96), 0.5f, 1e-5f);
}

TEST(sf2_release_seconds_add_timecents_within_the_spec_range) {
  CHECK(velocity::sf2ReleaseSeconds(0.7f, 64) == 0.7f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(1.0f, 0), 4.0f, 1e-4f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(1.0f, 127), 0.2658f, 1e-3f);
  CHECK(velocity::sf2ReleaseSeconds(0.0f, 0) == 0.0f); // a pinned instant release stays so
  // The range of the release generators: -12000 to 8000 timecents.
  CHECK_NEAR(velocity::sf2ReleaseSeconds(60.0f, 0), std::exp2(8000.0f / 1200.0f), 0.01f);
  CHECK_NEAR(velocity::sf2ReleaseSeconds(0.002f, 127), std::exp2(-12000.0f / 1200.0f), 1e-6f);
}

TEST(a_note_is_at_most_16_bytes_and_keeps_its_kind) {
  static_assert(sizeof(Note) <= 16);
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
  Note wide(60, 100);
  wide.setVelocity16(0x1234);
  CHECK(wide.getVelocity16() == 0x1234);
  CHECK(wide.getVelocity() == 0x1234 >> 9);
}

TEST(a_velocity_0_note_on_is_a_real_quiet_note) {
  Note note(60, 0);
  CHECK(note.isDefined());
  CHECK(note.isNoteOn());
  CHECK(!note.isOff());
  CHECK(!note.isAftertouch());
  CHECK(note.getVelocity() == 0);
  CHECK(note.getVelocityAsFloat() > 0.0f); // sounds, at the minimum level
  CHECK_NEAR(note.getVelocityAsFloat(), velocity::kMinSoundingLevel, 1e-6f);
  // And it is not an OFF to the event the player builds.
  TrackEvent event(0, Tuning::EDO12, note.getVelocityAsFloat(), note.getValue());
  CHECK(!event.isOff());
  CHECK(!event.isAftertouch());
}

TEST(a_velocity_0_note_on_starts_a_voice_when_rendered) {
  auto render_peak = [](int velocity_value) {
    Song song(Tuning::EDO12);
    auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
    song.getArrangement().setNote(0, track.getInternalId(), 0, Note(60, static_cast<short>(velocity_value)));
    song.getArrangement().setNote(8, track.getInternalId(), 0, Note::off());
    auto result = renderSongOffline(song, ChannelConfiguration(44100, 1));
    float peak = 0.0f;
    for (auto v : result.interleaved) peak = std::max(peak, std::fabs(v));
    return peak;
  };
  auto quiet = render_peak(0);
  CHECK(quiet > 0.0f);
  CHECK(render_peak(100) > quiet);
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
  CHECK_NEAR(static_cast<float>(frames(127)), 0.2658f * 44100.0f, 20.0f);
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
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_sf2_release_fixture.sf2").string();
  writeMinimalSf2(path, presets);
  SoundFont sf(path);
  checkReleaseScaling([&](int lift7) {
    auto instrument = sf.createInstrument(0);
    ChannelConfiguration config(44100, 1);
    auto voice = instrument->playNote(config, SphericalPosition{}, SpatialMode::AUTO, Tuning::EDO12, 1.0f, 0.8f, 60, SendLevels{}, NoteCoordinate{});
    return framesToEnd(*voice, lift7);
  });
}

TEST(an_off_event_carries_its_release_velocity) {
  RenderContext context(ChannelConfiguration(44100));
  context.addPendingOff(1, 0, 0, Tuning::EDO12, velocity::toFloat(velocity::from7(20)));
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

TEST(velocities_and_lifts_survive_a_save_and_reload) {
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_round_trip_scratch.xml").string();
  Song song(Tuning::EDO12);
  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto id = track.getInternalId();
  Note wide(62, 100);
  wide.setVelocity16(0x1234);
  Note wide_off = Note::off(64, 0, 0);
  wide_off.setVelocity16(0x0777);
  song.getArrangement().setNote(0, id, 0, Note(60, 0));
  song.getArrangement().setNote(1, id, 0, Note::off(0));
  song.getArrangement().setNote(2, id, 0, Note(60, 100));
  song.getArrangement().setNote(3, id, 0, Note::off());
  song.getArrangement().setNote(4, id, 0, wide);
  song.getArrangement().setNote(5, id, 0, wide_off);
  song.save(path);

  InstrumentProvider provider;
  Song reloaded(Tuning::EDO12);
  CHECK(reloaded.open(path, provider));
  auto at = [&](int row) { return reloaded.getArrangement().getNote(row, id, 0); };
  CHECK(at(0).isNoteOn() && at(0).getVelocity() == 0);
  CHECK(at(1).isOff() && at(1).getVelocity() == 0);
  CHECK(at(2).isNoteOn() && at(2).getVelocity() == 100);
  CHECK(at(3).isOff() && at(3).getVelocity() == constants::NEUTRAL_VELOCITY);
  CHECK(at(4).getVelocity16() == 0x1234);
  CHECK(at(5).isOff() && at(5).getVelocity16() == 0x0777);
}

TEST(an_older_file_off_without_a_lift_gets_the_neutral_one) {
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "velocity_legacy_scratch.xml").string();
  Song song(Tuning::EDO12);
  auto & track = song.addTrack(std::make_unique<InstrumentTrack>(0));
  auto id = track.getInternalId();
  song.getArrangement().setNote(0, id, 0, Note(60, 100));
  song.getArrangement().setNote(4, id, 0, Note::off());
  song.save(path);

  // Older files marked every OFF with velocity="0".
  auto text = slurp(path);
  auto at = text.find(">OFF</note>");
  CHECK(at != std::string::npos);
  text.insert(at, " velocity=\"0\"");
  std::ofstream(path) << text;

  InstrumentProvider provider;
  Song reloaded(Tuning::EDO12);
  CHECK(reloaded.open(path, provider));
  auto off = reloaded.getArrangement().getNote(4, id, 0);
  CHECK(off.isOff());
  CHECK(off.getVelocity() == constants::NEUTRAL_VELOCITY);
}

#include "TestFramework.h"

#include "../src/instruments/OscillatorVoice.h"
#include "../src/playback/TrackEvent.h"
#include "../src/state/RenderContext.h"
#include "../src/ambisonic/SphericalPosition.h"

TEST(oscillator_voice_reports_note_value_and_velocity_loudness) {
  ChannelConfiguration config(44100);
  OscillatorVoice voice(config, SphericalPosition{}, 1.0f, WaveformType::SINE, 1.0f, 0.5f);

  CHECK(!voice.isActive());
  CHECK(voice.getNoteValue() == -1);

  voice.playNote(440.0f, 0.6f, 42);

  CHECK(voice.isActive());
  CHECK(voice.getNoteValue() == 42);
  // No EnvelopeFilter wrapper: loudness is the constant velocity-derived
  // gain baked in at note-on (see InstrumentVoice::playNote), not a
  // decaying envelope - OscillatorVoice has none of its own.
  CHECK_NEAR(voice.getLoudness(), 0.6f, 0.01f);

  voice.render(64); // holding the note shouldn't change its loudness
  CHECK_NEAR(voice.getLoudness(), 0.6f, 0.01f);

  voice.stopNote();
  CHECK(!voice.isActive());
}

TEST(track_event_and_render_context_carry_note_value) {
  TrackEvent on(0, 440.0f, 0.8f, 60);
  CHECK(on.getNoteValue() == 60);
  CHECK(!on.isOff());

  TrackEvent off(0, 0.0f, 0.0f); // default note_value, matching an off event
  CHECK(off.isOff());
  CHECK(off.getNoteValue() == -1);

  RenderContext context(ChannelConfiguration(44100));
  context.addPendingEvent(/*track_id*/ 1, /*frame*/ 0, /*id*/ 0, 440.0f, 0.8f, 60);
  auto & events = context.getPendingEvents(1);
  CHECK(events.size() == 1);
  CHECK(events[0].size() == 1);
  CHECK(events[0][0].getNoteValue() == 60);
}

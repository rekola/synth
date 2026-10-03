#include "TestFramework.h"

#include "../src/Controller.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/model/ArrangementOps.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Pattern.h"
#include "../src/model/Song.h"
#include "../src/playback/MidiNoteInput.h"
#include "../src/playback/PlaybackControlEvent.h"

#include <vector>

using namespace std;

namespace {

struct Fixture {
  ChannelConfiguration config{44100, 1};
  Controller controller{config};
  int track_id = 0;

  Fixture() {
    controller.switchToBuffer(controller.freshBufferName());
    track_id = controller.getSong().addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
  }

  // Drains the playback queue into (type, column) pairs.
  vector<pair<PlaybackControlEvent::Type, int>> drain() {
    vector<pair<PlaybackControlEvent::Type, int>> out;
    auto & queue = controller.getPlaybackEventQueue();
    while (queue.hasEvents()) {
      auto event = queue.pop();
      if (auto * pce = dynamic_cast<PlaybackControlEvent *>(event.get())) out.push_back({ pce->getType(), pce->getParameter2() });
    }
    return out;
  }

  MidiNoteInput::TargetResolver resolver(int row) {
    return [this, row](int id) { return resolveEditTarget(controller.getSong(), id, row); };
  }
};

} // namespace

// Voice slots are reused by lowest free index, so releasing notes in a
// different order than they were pressed never hands two held notes the
// same slot.
TEST(midi_note_input_columns_survive_non_lifo_release) {
  Fixture f;
  MidiNoteInput input;
  MidiNoteInput::Options options;
  auto columns = [&](short note, MidiEvent::Type type) {
    input.handle(MidiEvent(type, note, 100), f.controller, f.track_id, options, f.resolver(0));
    auto events = f.drain();
    CHECK(events.size() == 1);
    return events.empty() ? -1 : events[0].second;
  };

  CHECK(columns(60, MidiEvent::NOTE_ON) == 0);
  CHECK(columns(64, MidiEvent::NOTE_ON) == 1);
  CHECK(columns(67, MidiEvent::NOTE_ON) == 2);
  CHECK(columns(60, MidiEvent::NOTE_OFF) == 0); // first released, not last
  CHECK(columns(72, MidiEvent::NOTE_ON) == 0); // takes the freed slot, no collision with 64/67
  CHECK(columns(64, MidiEvent::NOTE_OFF) == 1);
  CHECK(columns(67, MidiEvent::NOTE_OFF) == 2);
  CHECK(input.hasHeldNotes());
  CHECK(columns(72, MidiEvent::NOTE_OFF) == 0);
  CHECK(!input.hasHeldNotes());
}

TEST(midi_note_input_release_of_unknown_note_is_ignored) {
  Fixture f;
  MidiNoteInput input;
  input.handle(MidiEvent(MidiEvent::NOTE_OFF, 60, 0), f.controller, f.track_id, {}, f.resolver(0));
  CHECK(f.drain().empty());
}

TEST(midi_note_input_writes_into_the_resolved_pattern_only_when_asked) {
  Fixture f;
  MidiNoteInput input;
  auto target = [&]() { return resolveEditTarget(f.controller.getSong(), f.track_id, 4); };

  MidiNoteInput::Options options;
  CHECK(!input.handle(MidiEvent(MidiEvent::NOTE_ON, 60, 100), f.controller, f.track_id, options, f.resolver(4)));
  auto t = target();
  CHECK(t.pattern->getNotes(t.effective_row).empty() || !t.pattern->getNotes(t.effective_row)[0].isDefined());
  input.handle(MidiEvent(MidiEvent::NOTE_ON, 60, 0), f.controller, f.track_id, options, f.resolver(4));

  options.write = true;
  CHECK(input.handle(MidiEvent(MidiEvent::NOTE_ON, 62, 100), f.controller, f.track_id, options, f.resolver(4)));
  t = target();
  CHECK(t.pattern->getNotes(t.effective_row)[0].isDefined());
}

// A velocity-0 note-on is a note-off, not a new note.
TEST(midi_note_input_velocity_zero_note_on_releases) {
  Fixture f;
  MidiNoteInput input;
  input.handle(MidiEvent(MidiEvent::NOTE_ON, 60, 100), f.controller, f.track_id, {}, f.resolver(0));
  f.drain();
  input.handle(MidiEvent(MidiEvent::NOTE_ON, 60, 0), f.controller, f.track_id, {}, f.resolver(0));
  auto events = f.drain();
  CHECK(events.size() == 1);
  CHECK(!events.empty() && events[0].first == PlaybackControlEvent::STOP_NOTE);
  CHECK(!input.hasHeldNotes());
}

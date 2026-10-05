#include "MidiNoteInput.h"

#include "PlaybackControlEvent.h"
#include "../Controller.h"
#include "../instruments/Tuning.h"
#include "../model/Note.h"
#include "../model/Song.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {

// The song-tuning note nearest in pitch to a 12-EDO MIDI note number.
int nearestNoteValue(Tuning tuning, int midi_note) {
  if (tuning == Tuning::TET12) return midi_note;
  int best = 0;
  float best_diff = 1e6f, f = getFrequencyFor(Tuning::TET12, midi_note);
  for (int i = 0; i < 255; i++) {
    float diff = fabsf(f - getFrequencyFor(tuning, i));
    if (diff < best_diff) {
      best = i;
      best_diff = diff;
    }
  }
  return best;
}

} // namespace

int
MidiNoteInput::freeColumn(int track_id) const {
  int column = 0;
  while (any_of(held_.begin(), held_.end(), [&](auto & kv) { return kv.second.track_id == track_id && kv.second.column == column; })) {
    column++;
  }
  return column;
}

vector<int>
MidiNoteInput::heldTrackIds() const {
  vector<int> ids;
  for (auto & [ note, held ] : held_) {
    if (find(ids.begin(), ids.end(), held.track_id) == ids.end()) ids.push_back(held.track_id);
  }
  return ids;
}

bool
MidiNoteInput::handle(const MidiEvent & ev, Controller & controller, int track_id, const Options & options, const TargetResolver & resolve) {
  auto & queue = controller.getPlaybackEventQueue();
  auto & song = controller.getSong();
  auto buffer = controller.getActiveBufferName();

  // Channel-wide, not tied to a note (ev.getNote() is unused).
  if (ev.getType() == MidiEvent::CHANNEL_PRESSURE) {
    queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CHANNEL_PRESSURE, buffer, track_id, ev.getVelocity()));
    return false;
  }

  auto held_it = held_.find(ev.getNote());
  auto current_delay = options.delay;
  bool is_off = ev.getType() == MidiEvent::NOTE_OFF || (ev.getType() == MidiEvent::NOTE_ON && ev.getVelocity() == 0);

  if (is_off) {
    if (held_it == held_.end()) return false;
    // Released on the track it was played on, even if the selection moved.
    auto held = held_it->second;
    held_.erase(held_it);
    controller.endNotePressure(held.track_id, held.column);
    queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, buffer, held.track_id, held.column));
    if (!options.write) return false;
    auto target = resolve(held.track_id);
    target.pattern->setNote(target.effective_row, held.column, Note(0, 0, current_delay));
    return true;
  }

  int note_value = nearestNoteValue(song.getTuning(), ev.getNote());

  if (ev.getType() == MidiEvent::NOTE_ON) {
    // A repeated note-on for a held note retriggers its own voice slot.
    int column = held_it != held_.end() ? held_it->second.column : freeColumn(track_id);
    held_[ev.getNote()] = { column, track_id };
    controller.endNotePressure(track_id, column);
    if (controller.isMonitoring(track_id)) {
      queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, buffer, track_id, column, note_value, ev.getVelocity()));
    }
    if (!options.write) return false;
    auto target = resolve(track_id);
    target.pattern->setNote(target.effective_row, column, Note(note_value, ev.getVelocity(), current_delay));
    song.incMinorVersion();
    return true;
  }

  // NOTE_PRESSURE
  if (held_it == held_.end()) return false;
  int column = held_it->second.column;
  int held_track_id = held_it->second.track_id;
  auto row = controller.getPlaybackInfo().getAbsolutePosition();
  Controller::PressureWriter write_row;
  if (options.pressure_follows_transport) {
    write_row = [&controller, held_track_id, column](int r, short p) { controller.applyNotePressure(r, held_track_id, column, p, 0); };
  }
  auto transport_row = [&controller]() {
    auto & info = controller.getPlaybackInfo();
    return info.isPlaying() ? info.getAbsolutePosition() : -1;
  };
  auto pressure = controller.notePressure(row, held_track_id, column, static_cast<short>(ev.getVelocity()), current_delay, write_row, transport_row);
  queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::NOTE_PRESSURE, buffer, held_track_id, column, note_value, pressure));
  if (!options.pressure_follows_transport) return false;
  song.incMinorVersion();
  return true;
}

#include "ArpeggiatorState.h"

#include <algorithm>
#include <cmath>

using namespace std;

void
ArpeggiatorState::noteOn(int column, const Track & instrument, float frequency, float velocity, int note_value, float /*start_phase*/) {
  bool was_empty = held_notes_.empty();

  instrument_ = &instrument;

  auto it = find_if(held_notes_.begin(), held_notes_.end(), [&](const HeldNote & n) { return n.id == column; });
  if (it != held_notes_.end()) {
    it->frequency = frequency;
    it->velocity = velocity;
    it->note_value = note_value;
  } else {
    held_notes_.push_back({ column, frequency, velocity, note_value });
  }

  rebuildStepPool();

  // A fresh keypress on an otherwise-silent chord restarts the pattern
  // from step 0, triggered immediately on the next render() call rather
  // than after a full step interval of silence.
  if (was_empty) {
    step_index_ = -1;
    samples_until_next_step_ = 0;
  }
}

void
ArpeggiatorState::noteOff(int column) {
  held_notes_.erase(remove_if(held_notes_.begin(), held_notes_.end(), [&](const HeldNote & n) { return n.id == column; }), held_notes_.end());
  // Rebuilding even when now empty just clears step_pool_/step_index_
  // harmlessly - render() only ever schedules a new step while
  // held_notes_ is non-empty, so no new voices get triggered from here on;
  // whatever's already sounding (and any still-pending gate deadlines)
  // simply keeps running to completion.
  rebuildStepPool();
}

void
ArpeggiatorState::notePressure(int column, float velocity) {
  auto it = find_if(held_notes_.begin(), held_notes_.end(), [&](const HeldNote & n) { return n.id == column; });
  if (it != held_notes_.end()) it->velocity = velocity;
}

void
ArpeggiatorState::rebuildStepPool() {
  step_pool_.clear();

  int octaves = std::max(0, arp_.getOctaves());
  for (auto & note : held_notes_) {
    for (int o = 0; o <= octaves; o++) {
      step_pool_.push_back({ note.frequency * powf(2.0f, (float)o), note.velocity, note.note_value });
    }
  }
  sort(step_pool_.begin(), step_pool_.end(), [](const Step & a, const Step & b) { return a.frequency < b.frequency; });

  if (step_pool_.empty()) step_index_ = -1;
  else if (step_index_ >= (int)step_pool_.size()) step_index_ = (int)step_pool_.size() - 1;
}

void
ArpeggiatorState::advanceIndex(int pool_size) {
  if (pool_size <= 1) { step_index_ = 0; return; }

  switch (arp_.getMode()) {
  case Arpeggiator::DOWN:
    step_index_ = (step_index_ - 1 + pool_size) % pool_size;
    break;
  case Arpeggiator::UP_DOWN:
    step_index_ += direction_;
    if (step_index_ >= pool_size) { step_index_ = pool_size - 2; direction_ = -1; }
    else if (step_index_ < 0) { step_index_ = 1; direction_ = 1; }
    break;
  case Arpeggiator::UP:
  default:
    step_index_ = (step_index_ + 1) % pool_size;
    break;
  }
}

void
ArpeggiatorState::triggerNextStep() {
  int n = (int)step_pool_.size();
  if (n == 0 || !instrument_) { samples_until_next_step_ = stepLengthSamples(); return; }

  if (step_index_ < 0) {
    // "Down" starts from the top of the pool (the note it wraps back to),
    // matching the same "descending wraps to the top" convention used for
    // every later step - every other mode starts at the bottom.
    if (arp_.getMode() == Arpeggiator::DOWN) { step_index_ = n - 1; direction_ = -1; }
    else { step_index_ = 0; direction_ = 1; }
  } else {
    advanceIndex(n);
  }

  auto & step = step_pool_[(size_t)step_index_];

  // Same extent-default resolution as InstrumentTrackState::render()'s own
  // pattern-playback note-on path and Player.cpp's live-note path -
  // position_.extent < 0 means "not authored on this track" (see
  // InstrumentTrack::getExtent()), resolved to the instrument's own family
  // default here too.
  auto resolved_position = getPosition();
  if (resolved_position.extent < 0.0f) resolved_position.extent = instrument_->getDefaultExtent();

  auto voice = instrument_->playNote(getChannelConfiguration(), resolved_position, step.frequency, 1.0f, step.velocity, -getRandF(), step.note_value, getSends());
  int voice_id = next_voice_id_++;
  addVoice(voice_id, move(voice));
  pending_gates_.push_back({ voice_id, gateLengthSamples() });

  samples_until_next_step_ = stepLengthSamples();
}

void
ArpeggiatorState::closeElapsedGates() {
  for (auto it = pending_gates_.begin(); it != pending_gates_.end(); ) {
    if (it->samples_remaining <= 0) {
      stopVoices(it->voice_id);
      it = pending_gates_.erase(it);
    } else {
      ++it;
    }
  }
}

int
ArpeggiatorState::stepLengthSamples() const {
  int tempo = std::max(1, (int)(bpm_ + 0.5f));
  int rows = std::max(1, arp_.getNoteDuration());
  return std::max(1, getChannelConfiguration().getSampleInterval(tempo) * rows);
}

int
ArpeggiatorState::gateLengthSamples() const {
  int tempo = std::max(1, (int)(bpm_ + 0.5f));
  int rows = std::max(1, arp_.getGate());
  return std::max(1, getChannelConfiguration().getSampleInterval(tempo) * rows);
}

AudioBuffer
ArpeggiatorState::render(int frames, const vector<unique_ptr<Track> > & instruments, RenderContext & context) {
  // Cheap and unconditional so tempo stays current even while
  // stopped/auditioning (see the Run section of CLAUDE.md/plans/
  // arpeggiator.md - tempo is fixed for a song's whole lifetime today, but
  // this stays correct if that ever changes). Everything else - the
  // pending-events/pending-azimuth chunked dispatch, including its own
  // per-chunk calls to renderVoices(int) below - is unchanged, inherited
  // behavior: a pattern-authored note on this track still plays directly
  // rather than arpeggiating (Phase 2, not yet wired - see
  // plans/arpeggiator.md), but is never silently dropped, and
  // pending-events bookkeeping is never leaked.
  bpm_ = context.getBpm();
  return InstrumentTrackState::render(frames, instruments, context);
}

AudioBuffer
ArpeggiatorState::renderVoices(int frames) {
  // Sample-accurate, chunked the same way InstrumentTrackState::
  // render(frames, instruments, context) already chunks around pending
  // events - a step/gate boundary can land mid-block, and smearing it to
  // the block boundary would reintroduce the exact kind of jitter that
  // chunking elsewhere in this codebase exists to avoid. Since that outer
  // method calls this override once per its own pending-events/azimuth
  // sub-chunk, the two timelines compose correctly for free via ordinary
  // virtual dispatch, without this class needing to know anything about
  // pending events itself.
  vector<pair<int, AudioBuffer> > chunks;
  int i = 0;
  while (i < frames) {
    int render_size = frames - i;

    if (!held_notes_.empty()) {
      if (step_index_ < 0 || samples_until_next_step_ <= 0) triggerNextStep();
      render_size = std::min(render_size, samples_until_next_step_);
    }
    for (auto & gate : pending_gates_) {
      if (gate.samples_remaining > 0) render_size = std::min(render_size, gate.samples_remaining);
    }
    if (render_size < 1) render_size = 1;

    // Explicitly base-qualified: mixes voices_ (this class's own step
    // voices, added via addVoice() above) exactly like a plain
    // InstrumentTrackState does, without re-entering this override.
    chunks.emplace_back(i, InstrumentTrackState::renderVoices(render_size));
    i += render_size;

    if (!held_notes_.empty()) samples_until_next_step_ -= render_size;
    for (auto & gate : pending_gates_) {
      if (gate.samples_remaining > 0) gate.samples_remaining -= render_size;
    }
    closeElapsedGates();
  }

  bool has_main = false, has_aux_a = false, has_aux_b = false;
  for (auto & [ pos, s ] : chunks) {
    has_main = has_main || s.hasChannel(Channel::Main);
    has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
    has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
  }
  AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames);
  data.zero();
  for (auto & [ pos, s ] : chunks) data.assignNamed(s, pos);

  return data;
}

void
ArpeggiatorState::clear() {
  InstrumentTrackState::clear();
  held_notes_.clear();
  step_pool_.clear();
  pending_gates_.clear();
  step_index_ = -1;
  samples_until_next_step_ = 0;
}

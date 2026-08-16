#include "ArpeggiatorState.h"

#include <algorithm>
#include <cmath>

using namespace std;

void
ArpeggiatorState::noteOn(int column, const Track & instrument, float frequency, float velocity, int note_value, NoteOrigin origin, const NoteCoordinate & note_coord) {
  bool was_empty = held_notes_.empty();

  instrument_ = &instrument;
  note_coord_ = note_coord;

  auto it = find_if(held_notes_.begin(), held_notes_.end(), [&](const HeldNote & n) { return n.id == column; });
  if (it != held_notes_.end()) {
    it->frequency = frequency;
    it->velocity = velocity;
    it->note_value = note_value;
  } else {
    held_notes_.push_back({ column, frequency, velocity, note_value });
  }

  rebuildStepPool();

  // endPatternRow() (below) makes its own, whole-row decision from this
  // once every note-on/off call for the current pattern row has been
  // applied - see its own comment. Never populated for a LIVE note-on
  // (endPatternRow() is never even called from that path).
  if (origin == NoteOrigin::PATTERN) touched_columns_this_row_.push_back(column);

  // A chord going from empty to non-empty always restarts the pattern
  // from step 0 - but *when* the first step actually fires differs by
  // origin. A LIVE onset defers the trigger by chordCollectWindowSamples():
  // a hand-played chord's several near-simultaneous key/pad presses arrive
  // at genuinely different sample times, so triggering on the very first
  // one would start the pattern on whichever note the player happened to
  // land on first rather than the pitch-sorted note the mode actually
  // starts on (the lowest for UP, the highest for DOWN - see
  // triggerNextStep()). step_index_ stays -1 for the whole window (further
  // LIVE notes arriving before it elapses just join the pool via
  // rebuildStepPool() above, was_empty now false, without re-arming it),
  // so triggerNextStep() still treats the eventual first trigger as a
  // fresh pick over whatever the pool has collected by then. Some
  // imprecision here is acceptable - a live take is a real-time process
  // with no authored "correct" instant to hit exactly.
  //
  // A PATTERN onset instead goes through resyncIfNothingRinging() (see its
  // own comment): normally immediate, same as a pattern row's several note
  // columns already landing in one batch before any render() call reaches
  // this track (see InstrumentTrackState::render()'s pending-events loop)
  // - but never at the cost of cutting a step that's still actually
  // sounding. Song playback has to be exactly correct, not just usually
  // close - a still-ringing step's own voice is left alone either way;
  // only *when* the next trigger is allowed to fire is what's decided
  // here.
  if (was_empty) {
    if (origin == NoteOrigin::LIVE) {
      step_index_ = -1;
      samples_until_next_step_ = chordCollectWindowSamples();
    } else {
      resyncIfNothingRinging();
    }
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
ArpeggiatorState::endPatternRow() {
  // True when every note currently in held_notes_ was refreshed by this
  // row's own note-on calls (touched_columns_this_row_, populated by
  // noteOn() above) - i.e. the row restated the whole chord, not just part
  // of it. Also true, vacuously, when held_notes_ is empty (a note-off-only
  // row that emptied the chord out, or simply a row with no events for
  // this track at all reaching here via some future caller) - harmless,
  // since renderVoices()'s own trigger check is gated on
  // !held_notes_.empty() first, so the resync set below is never actually
  // read before some future noteOn()'s own was_empty branch overwrites it
  // anyway.
  bool full_replace = all_of(held_notes_.begin(), held_notes_.end(), [&](const HeldNote & n) {
    return find(touched_columns_this_row_.begin(), touched_columns_this_row_.end(), n.id) != touched_columns_this_row_.end();
  });

  // A row that only refreshed *some* columns (dropping one note and
  // replacing it with another while the rest keep sustaining from an
  // earlier row) leaves the step clock alone instead - restarting the
  // whole cycle over one voice's own change would be audible and wrong.
  if (full_replace) resyncIfNothingRinging();

  touched_columns_this_row_.clear();
}

// The one place every PATTERN/transport-driven resync point in this class
// (noteOn()'s was_empty branch for PATTERN, endPatternRow()'s full-row
// replace, resyncPlayhead()) funnels through, rather than each forcing
// step_index_/samples_until_next_step_ directly. A note that has already
// started playing is never cut short to make room for a resync - once a
// step's voice is triggered, closeElapsedGates() is the only thing that
// ever gets to stop it, on its own originally-scheduled deadline.
//
// When pending_gates_ is empty (nothing currently holding a step's gate
// open), the resync happens right here, immediately. When it isn't, this
// simply does nothing beyond the samples_until_next_step_ clamp below -
// the resync request itself is not remembered or applied later.
//
// An earlier version *did* remember it (a resync_pending_ flag, applied by
// the next trigger once safe, rather than whenever that trigger's own
// normal schedule said to) - tried and reverted: it introduced its own
// real, reported drift between the arp and the song, since applying a
// deferred resync always meant a fresh restart at the pool's own first
// index, landing at an unpredictable moment relative to the song
// (whenever the still-ringing step it had been waiting on finally closed)
// rather than a moment that actually meant anything to the row grid -
// most noticeable with endPatternRow()'s own full-chord-replace, which can
// fire on every row of an ordinary sustained chord and so kept queuing up
// fresh, unrelated restarts. Simply doing nothing when something's ringing
// is the safer default: this class doesn't have enough information (no
// notion of the pattern's own note-event history, only whatever the most
// recent row said) to always aim a deferred resync somewhere sensible, so
// it doesn't try - see plans/arpeggiator-timing-fixes.md.
//
// Cutting the ringing step short so an immediate resync could always
// happen right away was tried too, even earlier, and also reverted: the
// old step's own voice and the freshly re-triggered one would briefly
// sound at once, most audibly when the old and new chords happen to share
// a note (e.g. the same root, changed voicing above it) - that root then
// briefly played twice. Never mangling an already-sounding note is not
// negotiable for song playback.
//
// samples_until_next_step_ still needs a closer look even in the
// do-nothing case, rather than just being left alone: for
// endPatternRow()'s full-row-replace and resyncPlayhead()'s own calls
// here, held_notes_ has been continuously non-empty the whole time (a
// chord replace/a transport resync only reach this while a chord is still
// held), so renderVoices()'s own per-frame countdown has kept it live and
// accurate - it's already >= every pending gate's own samples_remaining in
// every case that matters (equal in the legato gate == noteDuration case a
// still-open gate here almost always means, smaller than samples_remaining
// whenever a gate genuinely outlives its own step - see PendingGate's own
// comment - which only makes the clamp below a no-op either way). But
// noteOn()'s was_empty branch can also reach this - a chord re-held while
// an earlier release's own tail is still open (pending_gates_ non-empty
// despite held_notes_ having *just* gone from empty back to held) - and
// there samples_until_next_step_ has been sitting frozen and stale ever
// since the chord last emptied (renderVoices() never touches it while
// held_notes_ is empty), so it could already be small enough - even
// non-positive - to satisfy the *normal* samples_until_next_step_ <= 0
// trigger check in renderVoices() before the still-open tail's own gate
// actually closes, doubling it exactly like an unguarded immediate resync
// would have. Clamping it up to at least the longest remaining pending
// gate guarantees that check can't fire before every currently-ringing
// step has actually closed, regardless of which path got here.
void
ArpeggiatorState::resyncIfNothingRinging() {
  if (pending_gates_.empty()) {
    step_index_ = -1;
    samples_until_next_step_ = 0;
    return;
  }

  for (auto & gate : pending_gates_) {
    samples_until_next_step_ = std::max(samples_until_next_step_, gate.samples_remaining);
  }
}

void
ArpeggiatorState::resyncPlayhead() {
  resyncIfNothingRinging();
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

  // next_voice_id_ doubles as this step's own instance discriminator
  // (note_coord_.withInstance()) - it's already exactly "a fresh id per
  // triggered step," the same identity withInstance() wants, so no
  // separate step counter is needed. Taken before the playNote() call
  // (rather than incremented after, as addVoice()/pending_gates_ below
  // only strictly need) so the coordinate this step's own voice - and
  // hence its InstrumentVoice-derived start phase - is built from can
  // actually use it.
  int voice_id = next_voice_id_++;
  auto voice = instrument_->playNote(getChannelConfiguration(), resolved_position, step.frequency, 1.0f, step.velocity, step.note_value, getSends(), note_coord_.withInstance(voice_id));
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

int
ArpeggiatorState::chordCollectWindowSamples() const {
  // Fixed real time, deliberately not tempo-relative like stepLengthSamples()/
  // gateLengthSamples() above - this is about how long a hand-played chord
  // takes to physically land, not about song timing. 30ms is short enough
  // that a live chord's first step still feels instant, long enough to
  // catch a normal hand roll before commiting to whichever key happened to
  // register first - see noteOn()'s own comment.
  constexpr float kChordCollectWindowSeconds = 0.03f;
  return std::max(1, (int)(kChordCollectWindowSeconds * getChannelConfiguration().getAudioOutSampleRate() + 0.5f));
}

AudioBuffer
ArpeggiatorState::render(int frames, const vector<unique_ptr<Track> > & instruments, RenderContext & context) {
  // Cheap and unconditional so tempo stays current even while
  // stopped/auditioning (tempo is fixed for a song's whole lifetime today,
  // but this stays correct if that ever changes). Everything else - the
  // pending-events/pending-azimuth chunked dispatch, including its own
  // per-chunk calls to renderVoices(int) below - is unchanged, inherited
  // behavior: a pattern-authored note on this track reaches noteOn()'s
  // override the same way a live one does (see its own comment on
  // NoteOrigin), so it's never silently dropped, and pending-events
  // bookkeeping is never leaked.
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
      // samples_until_next_step_ alone decides *when* the next trigger
      // fires - not step_index_ < 0 as well, even though that's also true
      // for a fresh onset (noteOn()'s was_empty branch, endPatternRow(),
      // resyncPlayhead()): a LIVE onset deliberately leaves step_index_ at
      // -1 for a whole chord-collect window (see noteOn()'s own comment)
      // while samples_until_next_step_ counts down to it, and an OR here
      // would trigger immediately regardless, defeating the window
      // entirely. triggerNextStep() itself still treats step_index_ < 0 as
      // "pick a fresh first index" whenever it does fire.
      if (samples_until_next_step_ <= 0) triggerNextStep();
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
  touched_columns_this_row_.clear();
  step_index_ = -1;
  samples_until_next_step_ = 0;
}

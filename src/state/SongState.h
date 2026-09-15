#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "../model/Song.h"
#include "../model/ArrangementOps.h"
#include "TrackState.h"
#include "InstrumentTrackState.h"
#include "SampleTrackState.h"
#include "../instruments/Tuner.h"
#include "RenderContext.h"
#include "../model/NoteCoordinate.h"
#include "../ambisonic/Mixer.h"
#include "../bus/SendBusProcessor.h"
#include "../bus/BusEffectRegistry.h"
#include "MemoryParameterSource.h"
#include "../model/SongStructure.h"
#include "../util/constants.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

class SongState : public TrackState {
 public:
  explicit SongState(ChannelConfiguration channel_config) : TrackState(channel_config), render_context_(channel_config), send_bus_(channel_config) { }

  // Load-time slot instantiation: for each of Song's two slots (Song.h's
  // getBusSlot()/getBusSlotKind(), placeholder-sample-rate instances that
  // exist only to own their own parameters), construct a *fresh*,
  // correctly-sample-rated BusEffect via the registry and round-trip the
  // Song slot's parameters into it through a MemoryParameterSource -
  // deviation-only storeParameters() writes only what differs from that
  // type's own construction defaults, and loadParameters() falls back to
  // the (identical, since both were built from the same registry factory)
  // construction default for anything not written, so the round-trip is
  // exact without needing per-type dispatch here. Installed into
  // send_bus_ via setSlotEffect() - setBusEffectKind() below is this same
  // installation, minus the parameter round-trip, invoked again later at
  // runtime instead of only here at load time.
  void initialize(const Song & song) {
    tempo_ = song.getTempo();
    render_context_.setBpm(tempo_);
    song_structure_ = SongStructure(song);
    song_structure_version_ = song.getMajorVersion();

    // Floor-reflection parameters (ChannelConfiguration.h) - pushed into
    // this already-constructed instance's own stored copy the same way
    // main.cpp's setAudioOutSampleRate()/setAmbisonicOrder() calls
    // finalize a fresh one, since every playNote() call already threads
    // a ChannelConfiguration all the way down to voice construction.
    auto & mutable_config = getMutableChannelConfiguration();
    mutable_config.setEarHeight(song.getEarHeight());
    mutable_config.setFloorReflectionEnabled(song.getFloorReflectionEnabled());
    mutable_config.setFloorReflectionStrength(song.getFloorReflectionStrength());
    mutable_config.setGroundAbsorption(song.getGroundAbsorption());

    int real_sample_rate = getChannelConfiguration().getAudioOutSampleRate();
    float row_duration = getChannelConfiguration().getRowDuration(tempo_);

    for (int slot = 0; slot < 2; slot++) {
      auto & descriptor = findBusEffectDescriptor(song.getBusSlotKind(slot));
      auto effect = descriptor.factory(real_sample_rate);

      MemoryParameterSource params;
      song.getBusSlot(slot).storeParameters(params);
      effect->loadParameters(params);

      effect->setRowDuration(row_duration); // no-op except for MultiTapDelay

      send_bus_.setSlotEffect(slot, std::move(effect));
    }
  }

  // Runtime slot reconfiguration, unlike initialize()'s "load-time-only"
  // per-slot construction above: swaps this slot's live BusEffect for a
  // fresh, default-parameter instance of `kind`, at this SongState's own
  // already-resolved sample rate/row duration. No MemoryParameterSource
  // round-trip from the Song model here (unlike initialize()'s loop) -
  // this always starts a slot at its type's construction defaults, the
  // same state Controller::setBusEffectKind() puts the model side into via
  // Song::setBusSlotKind() right before pushing the PlaybackControlEvent
  // (SET_BUS_EFFECT) that reaches this method - so there's nothing
  // authored yet to round-trip. Only ever called from
  // Player::handlePlaybackControlEvent(), i.e. the audio thread's own
  // single-threaded event-draining loop, the same thread renderBlock()
  // (and thus send_bus_.process()) itself always runs on - no lock needed.
  void setBusEffectKind(int slot, BusEffectKind kind) {
    auto & descriptor = findBusEffectDescriptor(kind);
    auto effect = descriptor.factory(getChannelConfiguration().getAudioOutSampleRate());
    effect->setRowDuration(getChannelConfiguration().getRowDuration(tempo_)); // no-op except for MultiTapDelay
    send_bus_.setSlotEffect(slot, std::move(effect));
  }

  // Exposed purely so tests can verify setBusEffectKind() (and
  // initialize()'s own per-slot construction) actually reached the live
  // send_bus_ slot, the same "debug accessor added purely for tests"
  // precedent GranularCloud's own *ForTest() accessors establish.
  const BusEffect & getSlotEffectForTest(int slot) const { return send_bus_.getSlotEffect(slot); }

  // Named distinctly from TrackState::render's 3-arg overload (rather than
  // overloading render() itself), the same reasoning as InstrumentTrackState::
  // renderVoices - a different-shaped render() in a derived class hides
  // rather than overrides the base one, which -Woverloaded-virtual flags as
  // likely-accidental; giving it its own name makes the relationship (or
  // lack of one) explicit instead.
  // reset_mixer: true (the default, and every existing single-SongState
  // call site - tests, OfflineRenderer) resets `mixer` itself before
  // rendering into it, matching this method's own long-standing behavior.
  // Player.cpp's multi-buffer render loop passes false: `mixer` is shared
  // across every live buffer's own SongState that block (see the per-buffer
  // editing/playback-state plan's Part B), so only the caller's own single
  // reset() before the whole loop may run, or a later buffer's render
  // would wipe out an earlier one's.
  void renderBlock(int frames, const Song & song, Mixer & mixer, bool reset_mixer = true) {
    if (reset_mixer) mixer.reset();

    // Rebuilt whenever song.getMajorVersion() has moved on since the last
    // build, not just once in initialize() - adding/removing/reordering a
    // track while this SongState is already playing, or between a stop
    // and a resume, is ordinary usage, not an edge case. Keyed on
    // getMajorVersion() specifically, not getMinorVersion() - a note/command
    // edit alone must not trigger this.
    if (song_structure_version_ != song.getMajorVersion()) {
      std::lock_guard<std::mutex> guard(song.getTracksMutex());
      song_structure_ = SongStructure(song);
      song_structure_version_ = song.getMajorVersion();
    }

    // Snapshotting the raw Track* pointers under Song::getTracksMutex()
    // rather than holding it for this whole method - see that mutex's own
    // comment on why one is needed at all - keeps the lock held only as
    // long as a quick pointer copy takes, not for however long actually
    // rendering every track takes; a track added by the UI thread after
    // the snapshot is taken just isn't heard until next block, same as
    // a track added between two blocks outright. Safe against a track
    // added *during* the rest of this method reusing/reallocating one of
    // these pointers out from under it too, since track deletion doesn't
    // exist yet - every Track this snapshot points to lives at a fixed
    // address for the rest of the process once addTrack() returns.
    //
    // SongState is itself a TrackState, and every top-level track is
    // registered as *its own* TreeNode child (getState() below - cheap
    // once cached, so doing this every block, not just once, is what
    // picks up a track added mid-playback) - so the inherited
    // renderChildren() further down sums them the same solo-aware way any
    // other multi-child node (a Group, an Effect with several children)
    // already sums its own, with no separate master-track state object
    // needed to own that relationship. Registered here, before the
    // scheduling loop below rather than right before renderChildren() -
    // the transition-detection stop for a non-SampleTrack
    // (dynamic_cast<InstrumentTrackState *>(getChildByInternalId(track_id))
    // below) is called synchronously, straight off that lookup, unlike an
    // ordinary note event or a SampleTrack clip start/stop (both queued
    // into render_context_, read back whenever their own track finally
    // renders, so neither cares when its child was actually created); a
    // track whose state doesn't exist yet at that lookup point would
    // otherwise silently skip the stop entirely.
    std::vector<Track *> track_snapshot;
    {
      std::lock_guard<std::mutex> guard(song.getTracksMutex());
      track_snapshot.reserve(song.getMasterTrack().getChildren().size());
      for (auto & track : song.getMasterTrack().getChildren()) track_snapshot.push_back(track.get());
    }
    for (auto * track : track_snapshot) track->getState(*this, song_structure_);

    // Set (never merely assigned - see below) the moment the transport
    // (re)starts (isPlaying() flips false -> true), and stays set across
    // as many renderBlock() calls as it takes to actually reach the next
    // real scheduling point (getSamplePos() == 0 below) - resuming
    // mid-row (sample_pos_ left wherever a mid-row pause landed, when the
    // cursor was never moved afterward - setPosition()/movePosition()
    // both reset it to 0, but a plain pause/resume in place does not) can
    // take more than one block to get there, especially against a real-
    // time engine's own small period size. A plain local recomputed fresh
    // from was_playing_ every call - the first, reverted shape here -
    // silently lost this obligation the instant a block boundary landed
    // before the next row did: was_playing_ had already latched to `true`
    // by the end of that same call, so the very next call's own
    // recomputation read `false` again, with nothing ever having
    // consumed the `true` in between. The SampleTrack scheduling below
    // uses this to force a fresh trigger, correctly offset into whatever
    // instance the playhead now lands in, regardless of whether this
    // SongState's own last-seen clip_index already happens to match (its
    // own comment has the full reasoning).
    if (isPlaying() && !was_playing_) pending_resume_retrigger_ = true;
    // The mirror case: the very first call after the transport *stops*
    // (isPlaying() flips true -> false). A SampleTrack voice has no
    // pause-awareness of its own - only start/stop - so left alone it
    // would just keep sounding straight through the pause (every
    // TrackState keeps rendering regardless of isPlaying(), see this
    // method's own doc comment); a hard stop would click instead. Queued
    // through RenderContext at frame 0 of this block, the same short
    // natural release a superseding trigger already uses
    // (SampleTrackEvent's own comment) - unlike every other queued stop
    // here, there's no later, more-precise frame to land on: nothing
    // schedules while stopped, so "as soon as this is known" already is
    // frame 0. Unconditional over every SampleTrack rather than only
    // ones this SongState currently believes are active - stopVoices() is
    // a safe no-op against a voice that isn't sounding, and every track
    // is already a direct child of the master track (no nesting to walk
    // to find one some other way). Both of a SampleTrack's own voices
    // (SampleTrackState's own doc comment) get one each - the background
    // bed has no pause-awareness of its own either.
    if (!isPlaying() && was_playing_) {
      for (auto * track : track_snapshot) {
	if (track->getType() == TrackType::SAMPLE) {
	  render_context_.addPendingSampleStop(track->getInternalId(), 0, false);
	  render_context_.addPendingSampleStop(track->getInternalId(), 0, true);
	}
      }
    }
    was_playing_ = isPlaying();

    if (isPlaying()) {
      for (int i = 0; i < frames; i++) {
	// recording_muted_: a live-hold recording session (Launchpad/
	// keyboard auto-play-while-held - see LaunchpadManager::
	// onRowAdvanced()/PatternEditor::onRowAdvanced() and their own
	// SET_RECORDING_MUTE push) still needs the transport genuinely
	// advancing rows in real time underneath (that's what makes the
	// whole-row-clear feature's timing correct), but must not let the
	// song's own already-recorded pattern content spawn new voices
	// while doing so - otherwise old, not-yet-cleared notes at rows
	// the playhead sweeps through get audibly triggered before the
	// UI thread's own (necessarily reactive, and thus slightly
	// delayed) clear catches up. Skipping just this scheduling step
	// leaves the position-advance logic below completely untouched,
	// and doesn't touch the entirely separate PLAY_NOTE/STOP_NOTE/
	// NOTE_PRESSURE path the live performance itself is heard
	// through - so recording mute is inaudible for anything the
	// player is actually doing, only for the song's own old content.
	if (getSamplePos() == 0 && !recording_muted_) {
	  // pending_resume_retrigger_ only actually describes the very first
	  // row scheduled after a (re)start - once one row here has
	  // consumed it, every later one (whether later in this same
	  // renderBlock() call, which can span more than one row - an
	  // offline render's own larger block size, or just a short row at
	  // a fast tempo - or in a later call entirely) is genuinely
	  // continuing forward playback, not resuming. Consumed into a
	  // per-row copy and cleared immediately so only this row ever sees
	  // it as true.
	  bool row_just_resumed_playback = pending_resume_retrigger_;
	  pending_resume_retrigger_ = false;

	  auto [ section_idx, row_idx ] = getRelativePosition(song);
	  auto & section = song.getSection(section_idx);

	  // A lossless re-encoding of the note's authored (section, row)
	  // position into one plain row count (NoteCoordinate.h's own doc
	  // comment on why) - Song::toAbsoluteRow() sums every earlier
	  // section's own effective length, since each can be a different
	  // size. Computed once per row here, not inside NoteCoordinate
	  // itself - it has no business knowing this invariant.
	  int absolute_row = song.toAbsoluteRow(section_idx, row_idx);

	  // Every track that has either background content or an arrangement
	  // instance in this section, each its own Pattern now (see Section.h's
	  // own comment) - was one shared row->track_id->notes/commands
	  // lookup on the old flat class this section used to be. The
	  // arrangement layer can cover a track with no background Pattern
	  // of its own at all, so this is the union of both, not just
	  // getPatternsByTrack()'s own keys. A SampleTrack's own background
	  // bed (Section::getSampleBackgroundsByTrack()) joins the same union -
	  // a hand-authored song can carry one with no <pattern>/<arrangement>
	  // content on that track at all.
	  std::unordered_set<int> scheduled_track_ids;
	  for (auto & [ track_id, track_pattern ] : section.getPatternsByTrack()) scheduled_track_ids.insert(track_id);
	  for (auto & [ track_id, track_instances ] : section.getInstancesByTrack()) {
	    if (!track_instances.empty()) scheduled_track_ids.insert(track_id);
	  }
	  for (auto & [ track_id, background ] : section.getSampleBackgroundsByTrack()) {
	    if (background.getBuffer()) scheduled_track_ids.insert(track_id);
	  }

	  for (auto track_id : scheduled_track_ids) {
	    // What's actually active here: a real clip's own leaf Pattern,
	    // read at the row relative to when that instance started
	    // (wrapped by the clip's own length, not this track's
	    // background Pattern's own - Clip.h's own comment on why those
	    // are different fields now), if the arrangement layer covers
	    // this row; this track's own background Pattern otherwise
	    // (ArrangementOps.h's own resolveInstanceAt()). An explicit
	    // stop resolves to neither - silence, nothing plays here. None
	    // of this applies to a SampleTrack's own background bed, handled
	    // entirely separately below - it isn't part of this resolution
	    // at all.
	    const Pattern * active_pattern = nullptr;
	    int effective_row = row_idx;

	    auto active = resolveInstanceAt(song, section, track_id, row_idx);

	    bool is_sample_track = false;
	    {
	      auto track = song.getMasterTrack().getChildByInternalId(track_id);
	      is_sample_track = track && track->getType() == TrackType::SAMPLE;
	    }

	    // A SampleTrack's own background bed (Section::
	    // getSampleBackgroundContent(), ArrangementOps.h's own
	    // mergeClipToBackground()) plays continuously for as long as
	    // this section provides one, entirely independent of whatever the
	    // clip/arrangement layer below resolves to at this row: real
	    // audio genuinely mixes, so a clip placed on top of the bed
	    // doesn't mask it the way an instance masks a note track's own
	    // background Pattern (SampleTrackState's own doc comment on why
	    // these are two separate, simultaneous voices rather than one
	    // masking the other). Tracked by comparing this row's resolved
	    // SampleContent pointer against the previous row's - identity,
	    // not value, since a real bed is one stable object for as long
	    // as this section stays the current one, and changes the instant a
	    // different section (a different bed, or none at all) is entered.
	    if (is_sample_track) {
	      auto * background = section.getSampleBackgroundContent(track_id);
	      auto last_it = last_background_by_track_.find(track_id);
	      auto * previous_background = last_it == last_background_by_track_.end() ? nullptr : last_it->second;

	      if (previous_background && previous_background != background) {
		render_context_.addPendingSampleStop(track_id, i, true);
	      }
	      // row_idx == 0: retriggered fresh on every entry into this
	      // section, even a repeat of the same one (a pattern break/loop
	      // landing back on its own row 0) - the bed has no looping
	      // concept of its own (Section::getSampleBackgroundContent()'s
	      // own comment), so re-entering the section it belongs to is what
	      // stands in for a lap boundary here.
	      if (background && (background != previous_background || row_idx == 0 || row_just_resumed_playback)) {
		auto start_offset_frames = row_idx * getChannelConfiguration().getSampleInterval(tempo_);
		render_context_.addPendingSampleStart(track_id, i, background, start_offset_frames, true);
	      }
	      last_background_by_track_[track_id] = background;

	      // Same "a one-shot's own real audio can outlast the section"
	      // protection the clip layer below has - the bed's own real
	      // length was baked in at whatever tempo was current at merge
	      // time, so it can end up longer than this section's own
	      // row-driven span under a since-changed tempo.
	      if (background && row_idx == song.getEffectiveSectionLength(section) - 1) {
		render_context_.addPendingSampleStop(track_id, i + getChannelConfiguration().getSampleInterval(tempo_), true);
	      }
	    }

	    // The clip/arrangement layer - a real clip instance that was
	    // active as of the *previous* row this loop checked, and no
	    // longer is (a swap to a different clip, an explicit stop, or
	    // falling through to the background/silence) - fires the
	    // instrument's own natural release unconditionally, exactly once
	    // at the row termination actually lands on, rather than leaving
	    // whatever was sounding to ring out on its own indefinitely
	    // (correct only by accident for a clip whose own content happens
	    // to be short one-shots; wrong for anything sustained/looping -
	    // a looping clip has no natural end of its own to rely on at
	    // all). Redundant-safe against a clip whose own content already
	    // ends with an explicit note-off - firing this unconditionally
	    // costs nothing extra there. A SampleTrack routes this through
	    // RenderContext instead of calling InstrumentTrackState::
	    // stopAllVoices() directly (the same one Session view's own
	    // explicit stops already use for every other track type) - see
	    // SampleTrackEvent's own comment for why: an abrupt stop here
	    // would land wherever this row happens to fall in the current
	    // render block, not the exact sample the transition is actually
	    // due on.
	    int previous_clip_index = Section::kNoInstance;
	    {
	      auto last_it = last_active_clip_index_by_track_.find(track_id);
	      previous_clip_index = last_it == last_active_clip_index_by_track_.end() ? Section::kNoInstance : last_it->second;
	      if (previous_clip_index >= 0 && previous_clip_index != active.clip_index) {
		if (is_sample_track) {
		  render_context_.addPendingSampleStop(track_id, i, false);
		} else {
		  auto * track_state = dynamic_cast<InstrumentTrackState *>(getChildByInternalId(track_id));
		  if (track_state) track_state->stopAllVoices();
		}
	      }
	      last_active_clip_index_by_track_[track_id] = active.clip_index;
	    }

	    if (active.clip_index >= 0) {
	      auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];

	      // A SampleTrack's own clip is raw audio, not a Pattern to read
	      // notes from - queued as a RenderContext start exactly on the
	      // row it becomes active (the transition-out stop above already
	      // covers ending it when something else supersedes it), and
	      // again on every later row that starts a new lap, for a
	      // looping clip - the same "a fresh voice each lap, not one
	      // voice looping internally" shape LaunchpadManager::
	      // fireOrTriggerClipStep() already uses for Session-view
	      // triggering (its own step % length == 0), mirrored here via
	      // the row grid instead of that clock's own step count. See
	      // SampleTrackState::triggerClip()'s own comment for why this is
	      // the right place for lap timing to live, not the voice itself.
	      if (is_sample_track) {
		auto rows_since_start = row_idx - active.start_row;
		auto loop_rows = clip.getLength() > 0 ? clip.getLength() : 1;
		// How far into the *current* lap this row actually is - 0 at
		// every ordinary trigger point (a fresh transition always
		// lands on the instance's own leading row; a new lap is
		// defined as landing exactly on a lap boundary), and only
		// ever nonzero when row_just_resumed_playback below is what's
		// really forcing this trigger: the playhead was positioned
		// (or simply left sitting) somewhere past the instance's own
		// leading row while stopped, and playback resumed without
		// ever passing back through that leading row.
		auto rows_into_lap = clip.isLooping() ? rows_since_start % loop_rows : rows_since_start;
		bool is_new_lap = clip.isLooping() && rows_since_start > 0 && rows_into_lap == 0;
		// row_just_resumed_playback forces a fresh trigger here too,
		// even when active.clip_index already equals
		// previous_clip_index - this SongState's own bookkeeping can't
		// tell "the transport has been sitting on this same active
		// instance the whole time it was stopped" apart from "nothing
		// was ever playing to begin with," and only the former should
		// actually start audio. Redundant-safe against a genuine
		// transition landing on the very same row a resume does -
		// stopVoices() inside triggerClip() below already handles
		// being called more than once.
		if (active.clip_index != previous_clip_index || is_new_lap || row_just_resumed_playback) {
		  auto start_offset_frames = rows_into_lap * getChannelConfiguration().getSampleInterval(tempo_);
		  render_context_.addPendingSampleStart(track_id, i, &clip.getSampleContent(), start_offset_frames, false);
		}

		// A one-shot clip's own real audio can outlast the section
		// it's placed in - the transition-detection stop above only
		// actually fires once something *else* (a different clip,
		// or nothing at all) is found at this same track's
		// position, which never happens on a single, endlessly-
		// looping section that keeps re-finding this same instance
		// unchanged. Queuing an explicit stop at this section's own
		// last row means it always stops exactly when the section
		// does, regardless of whether anything ever "transitions"
		// away from it - a release, not a hard cut, so the tail
		// end of the section doesn't click. Meaningless for a looping
		// clip - its own next lap's re-trigger already supersedes
		// whatever's still sounding, the same as any other
		// transition.
		if (!clip.isLooping() && row_idx == song.getEffectiveSectionLength(section) - 1) {
		  render_context_.addPendingSampleStop(track_id, i + getChannelConfiguration().getSampleInterval(tempo_), false);
		}
		continue;
	      }

	      active_pattern = &clip.getLeafPattern();
	      auto length = clip.getLength() > 0 ? clip.getLength() : 1;
	      effective_row = active_pattern->getEffectiveRow(row_idx - active.start_row, length);
	    } else if (active.clip_index == Section::kNoInstance) {
	      auto it = section.getPatternsByTrack().find(track_id);
	      if (it != section.getPatternsByTrack().end()) {
		active_pattern = &it->second;
		// A Pattern shorter than this section's own effective length
		// repeats - see Pattern.h's own getEffectiveRow() comment.
		effective_row = active_pattern->getEffectiveRow(row_idx, song.getEffectiveSectionLength(section));
	      }
	    }
	    if (!active_pattern) continue;

	    auto & notes = active_pattern->getNotes(effective_row);
	    auto track = song.getMasterTrack().getChildByInternalId(track_id);
	    auto tuning = track ? song.getTuningForTrack(*track) : song.getTuning();

	    for (size_t j = 0; j < notes.size(); j++) {
	      if (notes[j].isDefined()) {
		auto & note = notes[j];
		float frequency = 0.0f, velocity = 0.0f;
		if (note.isAftertouch()) {
		  velocity = note.getVelocityAsFloat();
		} else if (!note.isOff()) {
		  frequency = Tuner::getFrequency(tuning, note);
		  velocity = note.getVelocityAsFloat();
		}
		auto delay_samples = int(note.getDelayAsFloat() * getChannelConfiguration().getSampleInterval(tempo_));
		int note_value = (note.isAftertouch() || note.isOff()) ? -1 : note.getValue();
		render_context_.addPendingEvent(track_id, i + delay_samples, int(j), frequency, velocity, note_value, NoteCoordinate(song_structure_.getOrdinalFor(track_id), absolute_row, int(j)));
	      }
	    }

	    // Commands always come from the section's own background pattern
	    // - never a Clip's own leaf pattern, even while that clip is
	    // what's supplying this row's notes above. A Clip's own Command
	    // column is deliberately not considered here (or anywhere else
	    // yet - PatternEditor doesn't show it, and nothing writes into
	    // it as automation): every command lives at the track/section
	    // level, one shared place regardless of which clip (if any)
	    // happens to be playing there, matching how live-recorded
	    // automation is meant to survive independent of clip placement.
	    // Every column at this row is processed, not just one - a row
	    // can carry more than one concurrently-recordable command now
	    // (Pattern::setCommand(row, command_column, Command)'s own
	    // comment).
	    auto background_it = section.getPatternsByTrack().find(track_id);
	    if (background_it != section.getPatternsByTrack().end()) {
	      auto background_row = background_it->second.getEffectiveRow(row_idx, song.getEffectiveSectionLength(section));
	      for (auto & command : background_it->second.getCommandsAt(background_row)) {
		if (!command.isDefined()) continue;
		if (command.isPatternBreak()) {
		  pending_break_ = true;
		  pending_break_row_ = command.getBreakDestinationRow();
		} else if (command.isAzimuthSlide()) {
		  scheduleAzimuthSlide(track_id, i, command.getAzimuthSlidePerTick());
		} else if (command.isVolumeSet() || command.isSendASet() || command.isSendBSet() || command.isAzimuthSet()) {
		  // 0Lxx/0Fxx/0Mxx/0Pxx - an absolute set, applied the instant
		  // this row starts (unlike the slide commands above, there's
		  // no per-tick ramp to schedule - see Command::
		  // getSendSetLinear()/getAzimuthSetDegrees()'s own comments on
		  // why these are a Set, not a Slide). setSendMain()/setSendA()/
		  // setSendB()/setAzimuth() already reach every already-active
		  // voice too, the same live-knob path Controller::
		  // setTrackSendA()/setTrackAzimuth()/etc. use - a recorded
		  // automation move and a live Launchpad press land on the
		  // exact same mechanism.
		  auto * leaf_state = dynamic_cast<LeafTrackState *>(getChildByInternalId(track_id));
		  if (leaf_state) {
		    if (command.isVolumeSet()) leaf_state->setSendMain(command.getSendSetLinear());
		    else if (command.isSendASet()) leaf_state->setSendA(command.getSendSetLinear());
		    else if (command.isSendBSet()) leaf_state->setSendB(command.getSendSetLinear());
		    else leaf_state->setAzimuth(command.getAzimuthSetDegrees());
		  }
		} else if (command.isVolumeGlide() || command.isSendAGlide() || command.isSendBGlide()) {
		  // YMxy/YAxy/YBxy - reproduces a recorded Launchpad fader press's
		  // own real (wall-clock) glide, not just its final value - the
		  // same LeafTrackState::glideSendMain()/A()/B() ramp a live press
		  // starts server-side now (Controller::glideTrackSendA()/etc.),
		  // just started here at this row instead of from a live event.
		  // getGlideDurationSeconds() is real seconds regardless of
		  // tempo_, converted to frames via this buffer's own actual
		  // sample rate, same as Player.cpp's own GLIDE_TRACK_SEND_*
		  // handling does for a live press.
		  auto * leaf_state = dynamic_cast<LeafTrackState *>(getChildByInternalId(track_id));
		  if (leaf_state) {
		    int glide_frames = static_cast<int>(std::lround(command.getGlideDurationSeconds() * getChannelConfiguration().getAudioOutSampleRate()));
		    if (command.isVolumeGlide()) leaf_state->glideSendMain(command.getGlideTargetDb(), glide_frames);
		    else if (command.isSendAGlide()) leaf_state->glideSendA(command.getGlideTargetDb(), glide_frames);
		    else leaf_state->glideSendB(command.getGlideTargetDb(), glide_frames);
		  }
		} else if (command.isAzimuthGlide()) {
		  // YZxy - azimuth's own equivalent of the three above, started
		  // through LeafTrackState::glideAzimuth() (which picks its own
		  // travel direction - see that method's own comment) rather
		  // than glideSendMain()/A()/B().
		  auto * leaf_state = dynamic_cast<LeafTrackState *>(getChildByInternalId(track_id));
		  if (leaf_state) {
		    int glide_frames = static_cast<int>(std::lround(command.getGlideDurationSeconds() * getChannelConfiguration().getAudioOutSampleRate()));
		    leaf_state->glideAzimuth(command.getAzimuthGlideTargetDegrees(), glide_frames);
		  }
		}
	      }
	    }
	  }
	}
	
	auto remaining = samplesUntilNextRow();
	if (i + remaining <= frames) {
	  i += remaining;
	  if (pending_break_) {
	    pending_break_ = false;
	    jumpToPatternBreak(song, pending_break_row_);
	  } else {
	    movePosition(1);
	  }
	} else {
	  // The next row boundary doesn't fall within this block - advance by
	  // however many samples are actually left in it (frames - i), not by
	  // a full `frames` again. Reusing `frames` here double-counted the
	  // `i` samples already consumed earlier in this same loop (e.g. by a
	  // previous row transition partway through the block), advancing
	  // sample_pos_ too far and drifting note timing later in the song -
	  // by design a row's sample-length is essentially never an exact
	  // multiple of the block size, so this fires on nearly every block
	  // that contains (or follows) a row transition.
	  moveForwardSamples(frames - i);
	  break;
	}
      }
    }
    
    // Unconditional, regardless of whether the song currently has any
    // instruments loaded (not guarded behind
    // !song.getInstrumentPool().getInstruments().empty() -
    // that guard used to skip this whole block, including every
    // mixer.accumulate() call below, whenever a song had zero instruments,
    // e.g. a freshly created song. Each track's own render() already
    // produces a correctly frame-sized (just channel-empty) buffer when it
    // has no valid instrument (InstrumentTrackState::render()), and
    // mixer.accumulate() already tolerates zero-channel input just fine
    // (AudioBuffer::mixNamed()) - so skipping the call entirely, rather
    // than just naturally accumulating nothing, was the actual bug: it left
    // the mixer's own internal accumulator buffer stuck at its pristine,
    // zero-frame construction-time state for the rest of the process,
    // since nothing else ever gives it a real frame count otherwise. Every
    // subsequent mixer.encode() call then produced a genuinely empty
    // (0-frame) master output every single block - indistinguishable from
    // AudioBlockEvent.h's own "empty buffer" terminate-sentinel convention,
    // so VisualizationThread mistook the very first-ever rendered block for
    // its own shutdown signal and stopped consuming its queue for the rest
    // of the process, which eventually deadlocked UI::start()'s own
    // shutdown push once its queue filled up - a real, reproducible
    // freeze-on-quit for any song with no instruments loaded (confirmed via
    // direct instrumentation, not guessed at).
    if (aux_a_sum_.numberOfFrames() != frames) aux_a_sum_ = AudioBuffer(1, frames);
    if (aux_b_sum_.numberOfFrames() != frames) aux_b_sum_ = AudioBuffer(1, frames);
    aux_a_sum_.zero();
    aux_b_sum_.zero();

    // Every top-level track was already registered as *its own* TreeNode
    // child of this SongState above (track_snapshot's own comment) - the
    // inherited renderChildren() sums them the same solo-aware way any
    // other multi-child node (a Group, an Effect with several children)
    // already sums its own, with no separate master-track state object
    // needed to own that relationship. This is the "mixes all the
    // ambisonic channels from the child tracks" master was built for; the
    // master model track itself stays a purely structural fact (XML, the
    // pattern editor's column, addressability for its own effect-command
    // Pattern) with no mirrored state-tree node of its own to keep in
    // sync. As a side effect this also fixed solo never having been
    // enforced *across* top-level tracks - an old per-track loop here
    // once accumulated every one of them into `mixer` unconditionally,
    // bypassing renderChildren()'s solo handling entirely.
    auto data = renderChildren(frames, song.getInstrumentPool(), render_context_, getChannelConfiguration());
    mixer.accumulate(data);

    if (auto * a = data.getChannel(Channel::AuxA)) {
      auto dst = aux_a_sum_.getChannelData(0);
      for (int i = 0; i < frames; i++) dst[i] += a[i];
    }
    if (auto * b = data.getChannel(Channel::AuxB)) {
      auto dst = aux_b_sum_.getChannelData(0);
      for (int i = 0; i < frames; i++) dst[i] += b[i];
    }

    // The send bus's own output is always ambisonic-shaped (see
    // SendBusProcessor) and the top-level mixer is guaranteed to be one
    // too now (BasicMixer is retired) - so this is a plain, unconditional
    // accumulate, no decode step. The isAmbisonic() guard isn't really a
    // conceptual "is this truly ambisonic" question - it exists solely so
    // the one synthetic top-level MONO config a Compressor regression
    // test constructs directly (bypassing MixerFactory) skips the send
    // bus entirely, rather than handing a genuine 1-channel
    // ambisonic_channels_ buffer to encodeStereoAsPoints(), which asserts
    // out.numberOfChannels() >= 2 on shape alone.
    if (getChannelConfiguration().isAmbisonic()) {
      // Always processed, even when both sums are silent, so the shared
      // reverb tail/chorus modulation stay continuous across blocks (see
      // SendBusProcessor.h).
      send_bus_.process(aux_a_sum_, aux_b_sum_, frames);
      mixer.accumulate(send_bus_.getBusAmbisonic());
    }

    render_context_.updateFrameOffset(-frames);
  }

#if 0
  int getTickInterval() const {
    return getSampleInterval() / 12;
  }
#endif
  
  bool isPlaying() const { return is_playing_; }
  void setIsPlaying(bool b) { is_playing_ = b; }

  // Snapshots getPositionEditSeq() at the moment playback stops
  // (Player.cpp's PlaybackControlEvent::STOP, right after
  // setIsPlaying(false)) - resyncPlayheadAfterStop() below compares
  // against this to tell "resumed from exactly where it paused" apart from
  // "the playhead moved while stopped" (a seek, or the pattern editor's
  // own cursor navigation - both drive SET_POSITION/MOVE_POSITION, both
  // bump position_edit_seq_ the same way real per-row playback advance
  // already does - see getPositionEditSeq()'s own comment).
  void notePlaybackStopped() { position_edit_seq_at_stop_ = position_edit_seq_; }

  // Called once per stopped-to-playing transition (Player.cpp's
  // PlaybackControlEvent::PLAY, right after setIsPlaying(true)) - see
  // TrackState::resyncPlayhead()'s own comment for why a track's internal
  // clock needs this at all. Only actually resyncs if the position changed
  // while stopped: resuming from exactly the same spot the transport was
  // paused at needs no correction - nothing about the row grid's own
  // timeline changed, so whatever phase a track's internal clock already
  // drifted to from having kept rendering through the stopped interval
  // (confirmed intentional - see SongState::renderBlock()'s own comment)
  // is fine left alone. Forcing it unconditionally on every resume made
  // even a plain pause/resume at the same row yank a still-cycling
  // arpeggiator back to its first step, which is only actually correct
  // when resuming genuinely means "start this phrase over" - i.e. the
  // playhead itself moved during the stop.
  void resyncPlayheadAfterStop() {
    if (position_edit_seq_ != position_edit_seq_at_stop_) resyncPlayhead();
  }

  // See render()'s own comment on recording_muted_'s use - set/cleared by
  // PlaybackControlEvent::SET_RECORDING_MUTE, pushed by LaunchpadManager/
  // PatternEditor alongside their own auto-play-while-held engage/
  // disengage (Controller::togglePlaying()) calls.
  void setRecordingMuted(bool b) { recording_muted_ = b; }

  // The playback position is a raw row count accumulated across however
  // long the previous song played; it means nothing against a different
  // song's (likely much shorter) pattern list, so it must be reset whenever
  // the underlying Song is swapped out from under this state.
  void resetPosition() { sample_pos_ = 0; absolute_pos_ = 0; }

  int getAbsolutePosition() const { return absolute_pos_; }
  int getSamplePos() const { return sample_pos_; }
    
  void moveForwardSamples(int n = 1) {
    auto sinterval = getChannelConfiguration().getSampleInterval(tempo_);

    for (int i = 0; i < n; i++) {
      sample_pos_++;
      
      if (sample_pos_ == sinterval) {
	movePosition(1);
      }
    }
  }

  std::pair<int, int> getRelativePosition(const Song & song) const {
    return song.normalizePosition(0, absolute_pos_);
  }

  int samplesUntilNextRow() const {
    auto sinterval = getChannelConfiguration().getSampleInterval(tempo_);
    return sample_pos_ == 0 ? sinterval : sinterval - sample_pos_;
  }
  
  void movePosition(int n_rows) {
    sample_pos_ = 0;
    if (n_rows >= 0 || absolute_pos_ + n_rows >= 0) {
      absolute_pos_ += n_rows;
    } else {
      absolute_pos_ = 0;
    }
    position_edit_seq_++;
  }

  // Absolute counterpart to movePosition() above - jumps to an exact row
  // (PlaybackInfo::getAbsolutePosition()'s own units) regardless of
  // whatever absolute_pos_ has drifted to by the time this actually
  // processes. Needed anywhere a UI-thread snapshot decides "land one row
  // past what I just saw": that snapshot is already stale by some
  // unknown amount by the time this event reaches the audio thread (this
  // one keeps rendering in real time the whole time such an event is in
  // flight), so a *relative* movePosition(1) issued from the UI thread
  // moves relative to whatever position has since drifted to, not
  // relative to the row the UI thread actually saw - occasionally
  // landing a row or more further than intended. An absolute target
  // sidesteps that entirely. See LaunchpadManager/PatternEditor's own
  // auto-stop-after-recording code for the concrete case this fixed.
  void setPosition(int absolute_row) {
    sample_pos_ = 0;
    absolute_pos_ = absolute_row < 0 ? 0 : absolute_row;
    position_edit_seq_++;
  }

  // Same as setPosition() above, but stamps getPositionEditSeq() with an
  // exact externally-tracked value instead of merely incrementing by one -
  // used by Player::stateFor() to seed a freshly constructed SongState
  // with however many MOVE_POSITION/SET_POSITION control events already
  // accumulated for this buffer before it had a SongState at all (see
  // Player.h's pending_positions_ comment). A plain setPosition() there
  // would always stamp exactly 1 regardless of that count, leaving this
  // counter behind wherever Controller's own per-buffer edit counter
  // already was.
  void setPositionWithEditSeq(int absolute_row, int edit_seq) {
    sample_pos_ = 0;
    absolute_pos_ = absolute_row < 0 ? 0 : absolute_row;
    position_edit_seq_ = edit_seq;
  }

  // ZBxx (Command::isPatternBreak()) landing spot: row `dest_row` of the
  // pattern *after* whichever one `absolute_pos_` currently falls in -
  // used in place of movePosition(1) at the one place a row ever
  // completes (render()'s own row-boundary check above), so the rest of
  // the current pattern is simply never reached. Landing past the last
  // pattern behaves exactly like normal end-of-song run-off - nothing
  // special-cased.
  void jumpToPatternBreak(const Song & song, int dest_row) {
    auto section_idx = song.normalizePosition(0, std::max(0, absolute_pos_)).first;
    auto next_section_idx = section_idx + 1;
    auto len = song.getEffectiveSectionLength(next_section_idx);
    if (len <= 0) { movePosition(1); return; }
    auto row = dest_row < 0 ? 0 : (dest_row >= len ? len - 1 : dest_row);
    setPosition(song.toAbsoluteRow(next_section_idx, row));
  }

  // YLxx/YRxx (Command::isAzimuthSlide()) - spreads constants::TICKS_PER_ROW
  // evenly-spaced nudges of `delta_per_tick` degrees across the row
  // currently starting at block-relative sample offset `row_start` (the
  // same block-relative numbering render()'s own note scheduling just
  // above already uses for its i+delay_samples offsets), each consumed by
  // InstrumentTrackState::render()'s chunked loop via RenderContext's
  // pending-azimuth-tick timeline. A tick landing beyond this block's own
  // remaining frames is simply carried forward by updateFrameOffset()
  // below, same as a note event scheduled near a block boundary already
  // is - so a command near the end of a block still applies all its
  // ticks, just spread across this call and the next.
  void scheduleAzimuthSlide(int track_id, int row_start, float delta_per_tick) {
    int row_samples = getChannelConfiguration().getSampleInterval(tempo_);
    int tick_interval = std::max(1, row_samples / constants::TICKS_PER_ROW);
    for (int tick = 1; tick <= constants::TICKS_PER_ROW; tick++) {
      int offset = tick * tick_interval;
      if (offset >= row_samples) break;
      render_context_.addPendingAzimuthTick(track_id, row_start + offset, delta_per_tick);
    }
  }

  // Bumped by every movePosition()/setPosition() call - lets a PlaybackInfo
  // snapshot (see Player::createPlaybackEvent()) declare how many
  // position-editing control events it reflects. Controller::
  // moveEditPosition()/setEditPosition() (the sole callers that ever push
  // MOVE_POSITION/SET_POSITION - see their own comments) bump a matching
  // local counter and compare it against this value on every incoming
  // snapshot, so a snapshot generated by the audio thread's own periodic
  // per-block render (state_.renderBlock() runs on every audio callback
  // regardless of play state) *before* it got around to draining a
  // just-pushed control event can't clobber a more recent local prediction
  // with a stale one - without this, that stale snapshot briefly winning
  // the race (arriving at the UI thread after the local update) made the
  // pattern-editor cursor visibly jump back to the old row and then jump
  // forward again once a caught-up snapshot arrived.
  int getPositionEditSeq() const { return position_edit_seq_; }
  
  int getTempo() const { return tempo_; }

  // Raw, pre-send-bus-processing per-block sums (mono) - used by the UI's
  // raw-channel volume meter to show AuxA/AuxB levels before they're
  // folded into the shared reverb/chorus wet signal.
  const AudioBuffer & getAuxASum() const { return aux_a_sum_; }
  const AudioBuffer & getAuxBSum() const { return aux_b_sum_; }

  // Rebuilt whenever song.getMajorVersion() has moved on since the last build
  // (see renderBlock()) - adding/removing/reordering a track while this
  // SongState is already playing, or between a stop and a resume, is
  // ordinary usage, not an edge case. Player.cpp's live-note dispatch
  // reuses this same instance rather than building its own, so a track's
  // live and pattern-driven NoteCoordinates always agree on its ordinal.
  const SongStructure & getSongStructure() const { return song_structure_; }

private:
  int tempo_ = 0;
  bool is_playing_ = false;
  bool recording_muted_ = false;
  int sample_pos_ = 0, absolute_pos_ = 0;
  int position_edit_seq_ = 0;
  int position_edit_seq_at_stop_ = -1; // see notePlaybackStopped()/resyncPlayheadAfterStop()
  bool pending_break_ = false; // ZBxx seen on the row currently completing
  int pending_break_row_ = 0;
  RenderContext render_context_;
  SendBusProcessor send_bus_;
  AudioBuffer aux_a_sum_, aux_b_sum_;
  SongStructure song_structure_;
  int song_structure_version_ = -1; // never equals a real song.getMajorVersion() until initialize()/renderBlock() runs
  // track_id -> the clip index (or Section::kNoInstance/kStopInstance)
  // resolveInstanceAt() returned for that track the last time this row's
  // own scheduling ran - renderBlock()'s own note-scheduling loop compares
  // this against each row's freshly-resolved value to detect a real
  // clip's own termination and fire its natural release exactly once, not
  // every row while a stop instance stays in effect.
  std::unordered_map<int, int> last_active_clip_index_by_track_;
  // track_id -> the SampleContent* (or nullptr) Section::
  // getSampleBackgroundContent() returned for that track the last time
  // this row's own scheduling ran - the background bed's own, entirely
  // separate transition-tracking counterpart to
  // last_active_clip_index_by_track_ above, compared by identity (a real
  // bed is one stable object for as long as its own section stays current)
  // to detect a section boundary (a different bed, or none at all) and
  // retrigger/release accordingly.
  std::unordered_map<int, const SampleContent *> last_background_by_track_;
  // renderBlock()'s own resume/pause-release detection - the previous
  // call's isPlaying(), compared against the current one.
  bool was_playing_ = false;
  // Set the instant a resume is detected, cleared only once actually
  // consumed at a real scheduling point - persists across as many
  // renderBlock() calls as that takes (see its own set-site comment for
  // why a plain per-call local isn't enough).
  bool pending_resume_retrigger_ = false;
};
  
#endif

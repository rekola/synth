#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "../model/Song.h"
#include "../model/ArrangementOps.h"
#include "TrackState.h"
#include "InstrumentTrackState.h"
#include "SampleTrackState.h"
#include "RenderContext.h"
#include "SessionTrackInfo.h"
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
#include <map>
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
  // The master track's own send levels (Track::getSends()) - Send Main
  // scales the dry mix, Send A/B the send bus's two returns; see
  // renderBlock().
  void setMasterSendMain(float linear) { master_sends_.main = linear; }
  void setMasterSendA(float linear) { master_sends_.a = linear; }
  void setMasterSendB(float linear) { master_sends_.b = linear; }

  // The master track's output level - the song's dry mix plus the send
  // bus's returns, after the master's own levels - measured on the
  // omnidirectional W channel like every track's (TrackInfo).
  float getMasterMeterValue() const { return master_meter_value_; }
  bool isMasterClipping() const { return master_clipping_; }

  void initialize(const Song & song) {
    tempo_ = song.getTempo();
    synced_song_tempo_ = tempo_;
    song_signature_ = song.getTimeSignature();
    running_bars_ = song.getRunningBars();
    swing_ = song.getSwing();
    master_sends_ = song.getMasterTrack().getSends();
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

  // A tempo edit while playing: everything derived from the tempo is
  // refreshed - the row length, the tempo the arpeggiators and sample
  // tracks read, and the bus effects' tempo-synced times. The row already
  // in flight just ends sooner or later (samplesUntilNextRow()); a sample
  // clip already sounding keeps the stretch it was triggered with.
  void applyTempo(int bpm) {
    if (bpm <= 0 || bpm == tempo_) return;
    tempo_ = bpm;
    render_context_.setBpm(static_cast<float>(tempo_));
    auto row_duration = getChannelConfiguration().getRowDuration(tempo_);
    for (int slot = 0; slot < 2; slot++) send_bus_.getSlotEffect(slot).setRowDuration(row_duration);
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

  // Every command at `pattern_row` of `pattern`, for `track_id`, starting
  // `frame_offset` frames into this block. Every column at the row is
  // processed, not just one - a row can carry more than one
  // concurrently-recordable command (Pattern::setCommand(row,
  // command_column, Command)'s own comment).
  // `breaks_only` applies nothing but a pattern break - for a track
  // Session view has taken over, whose arrangement automation doesn't
  // apply while the song's flow still does.
  void applyRowCommands(const Pattern & pattern, int pattern_row, int track_id, int frame_offset, bool breaks_only = false) {
    for (auto & command : pattern.getCommandsAt(pattern_row)) {
	if (!command.isDefined()) continue;
	if (breaks_only && !command.isPatternBreak()) continue;
        if (command.isPatternBreak()) {
          pending_break_ = true;
          pending_break_row_ = command.getBreakRow();
        } else if (command.isRetrigger()) {
          scheduleRetrigger(track_id, frame_offset, command.getRetriggerIntervalTicks(), command.getRetriggerVolumeCode());
        } else if (command.isAzimuthSlide()) {
          scheduleAzimuthSlide(track_id, frame_offset, command.getAzimuthSlidePerTick());
        } else if (command.isVolumeSet() || command.isAzimuthSet()) {
          // 0Lxx/0Pxx - an absolute set, applied the instant
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
      swing_ = song.getSwing();
      song_signature_ = song.getTimeSignature();
      // Only a tempo the song itself changed is applied: a scene launch has
      // set this one ahead of the song's copy (queueSceneChange()), and an
      // unrelated edit mustn't put it back.
      if (song.getTempo() != synced_song_tempo_) {
        synced_song_tempo_ = song.getTempo();
        applyTempo(synced_song_tempo_);
      }
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
    if (isPlaying() && !was_playing_) {
      pending_resume_retrigger_ = true;
      session_start_clock_ = session_clock_;
    }
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
    // bed has no pause-awareness of its own either. Instrument voices are
    // left as they are, a launched clip's included.
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
      // i only ever advances by a whole row's remaining samples: each row
      // starts on the exact frame the previous one ended.
      for (int i = 0; i < frames; ) {
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
	if (getSamplePos() == 0) advanceSessionTracks(song, i);
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

	  auto & arrangement = song.getArrangement();
	  int row_idx = absolute_pos_;
	  // Anything but the row after the last one scheduled - a seek, a
	  // pattern break or a restart - lands mid-content.
	  bool position_jumped = row_idx != last_scheduled_row_ + 1;
	  last_scheduled_row_ = row_idx;

	  // Every track that has background content, an arrangement instance
	  // or a background bed - the arrangement layer can cover a track with
	  // no background Pattern of its own at all, so this is the union.
	  std::unordered_set<int> scheduled_track_ids;
	  for (auto & [ track_id, track_pattern ] : arrangement.getPatternsByTrack()) scheduled_track_ids.insert(track_id);
	  for (auto & [ track_id, track_instances ] : arrangement.getInstancesByTrack()) {
	    if (!track_instances.empty()) scheduled_track_ids.insert(track_id);
	  }
	  for (auto & [ track_id, background ] : arrangement.getSampleBackgroundsByTrack()) {
	    if (background.getBuffer()) scheduled_track_ids.insert(track_id);
	  }
	  // Tracks Session view has taken over play their launched clip
	  // (or nothing) instead of whatever the arrangement has here.
	  for (auto & [ track_id, session_track ] : session_tracks_) {
	    if (session_track.isTakenOver()) scheduled_track_ids.insert(track_id);
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
	    bool from_clip = false; // active_pattern is a placed clip's own, not the background
	    int effective_row = row_idx;

	    // A taken-over track's launched clip counts its rows from its
	    // launch on the session clock; an arrangement instance from the
	    // row it was placed at.
	    auto session_it = session_tracks_.find(track_id);
	    bool taken_over = session_it != session_tracks_.end() && session_it->second.isTakenOver();
	    // Swing is keyed on the beat grid the row sits on: the transport row,
	    // or the session clock for a launched clip (launches land on bars, so
	    // the two agree) - never the clip's own row.
	    int swing_row = taken_over ? session_clock_ : row_idx;
	    ActiveInstance active{Arrangement::kStopInstance};
	    int rows_since_start = 0;
	    if (taken_over) {
	      if (session_it->second.clip_index >= 0) {
		active.clip_index = session_it->second.clip_index;
		rows_since_start = session_clock_ - session_it->second.launch_clock;
	      }
	    } else {
	      active = resolveInstanceAt(song, track_id, row_idx);
	      rows_since_start = row_idx - active.start_row;
	    }

	    bool is_sample_track = false;
	    {
	      auto track = song.getMasterTrack().getChildByInternalId(track_id);
	      is_sample_track = track && track->getType() == TrackType::SAMPLE;
	    }

	    // A SampleTrack's own background bed (Arrangement::
	    // getSampleBackgroundContent(), ArrangementOps.h's own
	    // mergeClipToBackground()) plays from row 0 on, entirely
	    // independent of whatever the clip/arrangement layer below
	    // resolves to at this row: real audio genuinely mixes, so a clip
	    // placed on top of the bed doesn't mask it the way an instance
	    // masks a note track's own background Pattern (SampleTrackState's
	    // own doc comment on why these are two separate, simultaneous
	    // voices rather than one masking the other). Tracked by comparing
	    // this row's resolved SampleContent pointer against the previous
	    // row's - identity, not value: the bed changes when it's replaced
	    // or the track is taken over.
	    if (is_sample_track) {
	      auto * background = taken_over ? nullptr : arrangement.getSampleBackgroundContent(track_id);
	      auto last_it = last_background_by_track_.find(track_id);
	      auto * previous_background = last_it == last_background_by_track_.end() ? nullptr : last_it->second;

	      if (previous_background && previous_background != background) {
		render_context_.addPendingSampleStop(track_id, i, true);
	      }
	      // Retriggered at this row's offset into the bed whenever playback
	      // lands here other than by advancing one row.
	      if (background && (background != previous_background || position_jumped || row_just_resumed_playback)) {
		auto start_offset_frames = row_idx * getChannelConfiguration().getSampleInterval(tempo_);
		render_context_.addPendingSampleStart(track_id, i, background, start_offset_frames, true);
	      }
	      last_background_by_track_[track_id] = background;
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
	    // costs nothing extra there. Queued through RenderContext at this
	    // row's own frame, for either track type, so the release lands on
	    // the exact sample the transition is due on, not wherever this
	    // row happens to fall in the current render block - and ahead of
	    // the row's own new notes, queued after it.
	    int previous_clip_index = Arrangement::kNoInstance;
	    {
	      auto last_it = last_active_clip_index_by_track_.find(track_id);
	      previous_clip_index = last_it == last_active_clip_index_by_track_.end() ? Arrangement::kNoInstance : last_it->second;
	      if (previous_clip_index >= 0 && previous_clip_index != active.clip_index) {
		if (is_sample_track) render_context_.addPendingSampleStop(track_id, i, false);
		else render_context_.addPendingStopAll(track_id, i);
		last_notes_.erase(track_id);
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
		  // getMixedContent(), not getSampleContent() - an overdubbed
		  // clip triggered from the arrangement timeline has to sound
		  // every layer, the same composite SampleTrackState::
		  // triggerClip()'s own Session-view path already plays (see
		  // its own comment for why this is never computed here).
		  render_context_.addPendingSampleStart(track_id, i, &clip.getMixedContent(), start_offset_frames, false);
		}

		// A one-shot clip's own real audio can outlast its length -
		// an explicit stop after its last row ends it there, a
		// release rather than a hard cut. A looping clip's next lap
		// supersedes whatever's still sounding instead.
		if (!clip.isLooping() && rows_since_start == loop_rows - 1) {
		  render_context_.addPendingSampleStop(track_id, i + getChannelConfiguration().getSampleInterval(tempo_), false);
		}
		continue;
	      }

	      active_pattern = &clip.getLeafPattern();
	      from_clip = true;
	      auto length = clip.getLength() > 0 ? clip.getLength() : 1;
	      effective_row = active_pattern->getEffectiveRow(rows_since_start, length);
	    } else if (active.clip_index == Arrangement::kNoInstance) {
	      auto it = arrangement.getPatternsByTrack().find(track_id);
	      if (it != arrangement.getPatternsByTrack().end()) {
		active_pattern = &it->second;
		effective_row = active_pattern->getEffectiveRow(row_idx, 0);
	      }
	    }
	    if (!active_pattern) continue;

	    auto & notes = active_pattern->getNotes(effective_row);
	    auto track = song.getMasterTrack().getChildByInternalId(track_id);
	    auto tuning = track ? song.getTuningForTrack(*track) : song.getTuning();

	    for (size_t j = 0; j < notes.size(); j++) {
	      if (notes[j].isDefined()) {
		auto & note = notes[j];
		float velocity = note.isOff() ? 0.0f : note.getVelocityAsFloat();
		auto delay_samples = int((note.getDelayAsFloat() + swing::offsetRows(swing_row, swing_)) * getChannelConfiguration().getSampleInterval(tempo_));
		int note_value = (note.isAftertouch() || note.isOff()) ? -1 : note.getValue();
		render_context_.addPendingEvent(track_id, i + delay_samples, int(j), tuning, velocity, note_value, NoteCoordinate(song_structure_.getOrdinalFor(track_id), row_idx, int(j)));
		if (note.isOff()) last_notes_[track_id].erase(int(j));
		else if (note_value >= 0 && velocity > 0.0f) last_notes_[track_id][int(j)] = LastNote { int(j), tuning, velocity, note_value, row_idx };
	      }
	    }

	    // Commands come from the track's own background pattern - one
	    // shared place regardless of which clip (if any) plays there, so
	    // recorded automation survives independent of clip placement -
	    // and then from a placed clip's own pattern, so a clip's own
	    // command wins where both set the same thing on the same row.
	    auto background_it = arrangement.getPatternsByTrack().find(track_id);
	    if (background_it != arrangement.getPatternsByTrack().end()) {
	      auto background_row = background_it->second.getEffectiveRow(row_idx, 0);
              applyRowCommands(background_it->second, background_row, track_id, i, taken_over);
            }
            if (from_clip) applyRowCommands(*active_pattern, effective_row, track_id, i);
          }
	}
	
	auto remaining = samplesUntilNextRow();
	if (remaining <= 0) break; // a degenerate tempo - no rows to advance through
	if (i + remaining <= frames) {
	  i += remaining;
	  session_clock_++;
	  if (pending_break_) {
	    pending_break_ = false;
            jumpToNextBar(song, pending_break_row_);
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
    // The master's Send Main scales the dry mix, not the aux sends feeding
    // the send bus - its returns have their own levels (below).
    scaleRegularChannels(data, master_sends_.main);
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
      send_bus_.process(aux_a_sum_, aux_b_sum_, frames, master_sends_.a, master_sends_.b);
      mixer.accumulate(send_bus_.getBusAmbisonic());
    }

    measureMasterOutput(data, frames);
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
      
      // >=, not ==: a tempo change can shorten the row below what has
      // already elapsed in it.
      if (sample_pos_ >= sinterval) {
	movePosition(1);
      }
    }
  }

  int samplesUntilNextRow() const {
    auto sinterval = getChannelConfiguration().getSampleInterval(tempo_);
    return sample_pos_ == 0 ? sinterval : std::max(1, sinterval - sample_pos_);
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

  // ZBxx (Command::isPatternBreak()): in place of movePosition(1) when the
  // row completes, go to row `row_in_bar` of the next bar (the bar's last
  // row at most). The session clock doesn't follow, so a launched clip
  // keeps its own place.
  void jumpToNextBar(const Song & song, int row_in_bar) {
    auto next_bar = barsAt(absolute_pos_).nextBarStart(absolute_pos_);
    setPosition(next_bar + std::min(row_in_bar, barsAt(next_bar).barRows() - 1));
  }

  // 0Rxy (Command::isRetrigger()) - re-fires every note still playing on
  // the track every `interval_ticks` ticks across the row starting at
  // block-relative sample offset `row_start`, velocity per `volume_code`.
  // Same-id events retrigger the voice in place.
  void scheduleRetrigger(int track_id, int row_start, int interval_ticks, int volume_code) {
    auto it = last_notes_.find(track_id);
    if (it == last_notes_.end() || interval_ticks <= 0) return;
    int row_samples = getChannelConfiguration().getSampleInterval(tempo_);
    int tick_interval = std::max(1, row_samples / constants::TICKS_PER_ROW);
    for (auto & [ column, last ] : it->second) {
      float velocity = last.velocity;
      for (int tick = interval_ticks; tick < constants::TICKS_PER_ROW; tick += interval_ticks) {
        int offset = tick * tick_interval;
        if (offset >= row_samples) break;
        velocity = Command::retriggerVelocityStep(last.velocity, velocity, volume_code);
        if (velocity <= 0.0f) break;
        render_context_.addPendingEvent(track_id, row_start + offset, static_cast<short>(column), last.tuning, velocity, last.note_value,
                                        NoteCoordinate(song_structure_.getOrdinalFor(track_id), last.row, last.column));
      }
    }
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

  // Session view: queues `target` for `track_id` - a clip index to launch,
  // SessionTrackInfo::kSilent to stop, kArrangement to follow the
  // arrangement again, or kNothingQueued to cancel - taking effect at the
  // start of the transport's next bar. `seq` is reported back in
  // getSessionSeq().
  void queueSessionChange(int track_id, int target, int seq) {
    ::queueSessionChange(session_tracks_, track_id, target);
    session_seq_ = std::max(session_seq_, seq);
  }
  // Session view: a launched scene's tempo (0: none) and time signature
  // (unset: none), or `clear_running` to hand the bars back to the song's,
  // taking effect with the clips - on the next bar, or at the first row
  // played when `immediate` (a stopped transport). The tempo is this
  // thread's own from then on and the signature's bars count from that row;
  // the UI mirrors both from the snapshot (getSceneSeq()). A new call
  // replaces one still waiting.
  void queueSceneChange(int tempo, TimeSignature signature, bool clear_running, bool immediate, int seq) {
    pending_scene_ = { tempo > 0 || signature.isSet() || clear_running, tempo, signature, clear_running, immediate, seq };
    if (!pending_scene_.active) scene_seq_ = std::max(scene_seq_, seq);
  }
  int getSceneSeq() const { return scene_seq_; }
  const RunningBars & getRunningBars() const { return running_bars_; }
  void setRunningBars(RunningBars running) { running_bars_ = running; }
  // The bars in force at `row` on this thread.
  BarGrid barsAt(int row) const { return ::barsAt(song_signature_, running_bars_, row); }

  // Stops every launched clip now, releasing its voices, and forgets
  // everything queued - on a transport stop too (seq -1 for that).
  void silenceSession(int seq) {
    for (auto & [ track_id, session_track ] : session_tracks_) {
      if (session_track.clip_index >= 0) releaseSessionTrack(track_id, 0);
    }
    silenceSessionTracks(session_tracks_);
    session_seq_ = std::max(session_seq_, seq);
  }
  // Moves every launched clip's playhead by `delta` rows (a paused
  // transport's cursor move); same sequence number rule as above.
  void shiftSession(int delta, int seq) {
    shiftSessionTracks(session_tracks_, session_clock_, delta);
    // Resume at the start of the new row, not partway through the one
    // the pause landed in.
    sample_pos_ = 0;
    session_seq_ = std::max(session_seq_, seq);
  }
  const SessionTracks & getSessionTracks() const { return session_tracks_; }
  int getSessionClock() const { return session_clock_; }
  // The session clock when the transport last started.
  int getSessionStartClock() const { return session_start_clock_; }
  int getSessionSeq() const { return session_seq_; }

private:
  int tempo_ = 0;
  int swing_ = swing::kStraight;
  bool is_playing_ = false;
  bool recording_muted_ = false;
  int sample_pos_ = 0, absolute_pos_ = 0;
  int position_edit_seq_ = 0;
  int position_edit_seq_at_stop_ = -1; // see notePlaybackStopped()/resyncPlayheadAfterStop()
  // The notes still playing per track (last note-on per column), what 0Rxy retriggers.
  struct LastNote { int column; Tuning tuning; float velocity; int note_value; int row; };
  std::unordered_map<int, std::map<int, LastNote>> last_notes_; // track -> note column
  bool pending_break_ = false; // ZBxx seen on the row currently completing
  int pending_break_row_ = 0;
  RenderContext render_context_;
  SendBusProcessor send_bus_;
  AudioBuffer aux_a_sum_, aux_b_sum_;
  SongStructure song_structure_;
  SendLevels master_sends_;
  float master_meter_value_ = -1.0f;
  bool master_clipping_ = false;

  static void scaleRegularChannels(AudioBuffer & buffer, float gain) {
    if (gain == 1.0f) return;
    auto frames = buffer.numberOfFrames();
    for (int c = 0; c < buffer.regularChannelCount(); c++) {
      auto data = buffer.getChannelData(c);
      for (int i = 0; i < frames; i++) data[i] *= gain;
    }
  }

  // The master's output on W: the dry mix plus, when there is one, the
  // send bus's returns - see getMasterMeterValue().
  void measureMasterOutput(const AudioBuffer & dry, int frames) {
    const float * w_dry = dry.getChannel(Channel::Main);
    const float * w_bus = getChannelConfiguration().isAmbisonic() ? send_bus_.getBusAmbisonic().getChannelData(0) : nullptr;
    float sum_squares = 0.0f;
    bool clipping = false;
    for (int i = 0; i < frames; i++) {
      float v = (w_dry ? w_dry[i] : 0.0f) + (w_bus ? w_bus[i] : 0.0f);
      sum_squares += v * v;
      clipping = clipping || v < -1.0f || v > 1.0f;
    }
    master_meter_value_ = frames > 0 ? std::sqrt(sum_squares / static_cast<float>(frames)) : 0.0f;
    master_clipping_ = clipping;
  }

  // Session view's launched clips and stops, per track (see
  // queueSessionChange()), and the clock they play on: rows played,
  // advancing with the transport but never jumping with it, so a seek or
  // a pattern break doesn't move a launched clip.
  struct PendingScene {
    bool active = false;
    int tempo = 0;
    TimeSignature signature;
    bool clear_running = false;
    bool immediate = false;
    int seq = 0;
  };
  PendingScene pending_scene_;
  int scene_seq_ = 0;
  TimeSignature song_signature_{ 4, 4 };
  RunningBars running_bars_;
  int synced_song_tempo_ = 0; // the song tempo last applied from the song itself

  void applyPendingScene() {
    if (pending_scene_.tempo > 0) applyTempo(pending_scene_.tempo);
    if (pending_scene_.signature.isSet()) running_bars_ = { pending_scene_.signature, absolute_pos_ };
    else if (pending_scene_.clear_running) running_bars_ = {};
    scene_seq_ = std::max(scene_seq_, pending_scene_.seq);
    pending_scene_ = {};
  }

  SessionTracks session_tracks_;
  int session_clock_ = 0;
  int session_start_clock_ = 0;
  int session_seq_ = 0;

  // At the start of every row played: ends a launched one-shot that has
  // played through, and on the first row of a bar applies whatever is
  // queued.
  void advanceSessionTracks(const Song & song, int frame) {
    // A launched scene's tempo and signature take effect on the bar (or
    // the first row played, from a stopped transport) the clips launch on.
    if (pending_scene_.active && (pending_scene_.immediate || barsAt(absolute_pos_).rowInBar(absolute_pos_) == 0)) applyPendingScene();
    bool bar_start = barsAt(absolute_pos_).rowInBar(absolute_pos_) == 0;
    for (auto it = session_tracks_.begin(); it != session_tracks_.end(); ) {
      auto track_id = it->first;
      auto & session_track = it->second;
      auto & clips = song.getClips(track_id);
      if (session_track.clip_index >= 0) {
	bool gone = session_track.clip_index >= static_cast<int>(clips.size());
	bool finished = false;
	if (!gone) {
	  auto & clip = clips[static_cast<size_t>(session_track.clip_index)];
	  finished = !clip.isLooping() && session_clock_ - session_track.launch_clock >= std::max(1, clip.getLength());
	}
	if (gone || finished) {
	  releaseSessionTrack(track_id, frame);
	  session_track.clip_index = SessionTrackInfo::kSilent;
	}
      }
      if (bar_start && session_track.queued != SessionTrackInfo::kNothingQueued) {
	auto target = session_track.queued;
	// An empty slot has nothing to launch.
	if (target >= 0 && (target >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(target)].isEmpty())) target = SessionTrackInfo::kSilent;
	releaseSessionTrack(track_id, frame);
	session_track.clip_index = target;
	session_track.launch_clock = session_clock_;
	session_track.queued = SessionTrackInfo::kNothingQueued;
      }
      if (session_track.isIdle()) it = session_tracks_.erase(it);
      else ++it;
    }
  }

  // Whatever `track_id` was sounding gives way - a launch, stop or return
  // to the arrangement taking effect - through its natural release, and
  // the next row starts it afresh rather than as a continuation.
  void releaseSessionTrack(int track_id, int frame) {
    auto * track_state = getChildByInternalId(track_id);
    if (dynamic_cast<SampleTrackState *>(track_state)) {
      render_context_.addPendingSampleStop(track_id, frame, false);
      render_context_.addPendingSampleStop(track_id, frame, true);
    } else if (dynamic_cast<InstrumentTrackState *>(track_state)) {
      render_context_.addPendingStopAll(track_id, frame);
    }
    last_active_clip_index_by_track_.erase(track_id);
    last_background_by_track_.erase(track_id);
    last_notes_.erase(track_id);
  }

  int song_structure_version_ = -1; // never equals a real song.getMajorVersion() until initialize()/renderBlock() runs
  // track_id -> the clip index (or Arrangement::kNoInstance/kStopInstance)
  // resolveInstanceAt() returned for that track the last time this row's
  // own scheduling ran - renderBlock()'s own note-scheduling loop compares
  // this against each row's freshly-resolved value to detect a real
  // clip's own termination and fire its natural release exactly once, not
  // every row while a stop instance stays in effect.
  std::unordered_map<int, int> last_active_clip_index_by_track_;
  // track_id -> the SampleContent* (or nullptr) Arrangement::
  // getSampleBackgroundContent() returned for that track the last time
  // this row's own scheduling ran - the background bed's own, entirely
  // separate transition-tracking counterpart to
  // last_active_clip_index_by_track_ above, compared by identity to
  // detect a replaced bed (or none at all) and retrigger/release
  // accordingly.
  std::unordered_map<int, const SampleContent *> last_background_by_track_;
  // The row scheduled last, to tell advancing one row from a jump.
  int last_scheduled_row_ = -2;
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

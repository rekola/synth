#ifndef _INSTRUMENTTRACKSTATE_H_
#define _INSTRUMENTTRACKSTATE_H_

#include "LeafTrackState.h"
#include "RenderContext.h"
#include "../model/NoteCoordinate.h"
#include "../instruments/Tuning.h"
#include "../model/InstrumentPool.h" // getInstrumentSource() below indexes into it directly - needs
                    // the complete type, not just TrackState.h's own forward declaration.

#include <algorithm>
#include <cmath>

// The LeafTrackState (see its own doc comment) for a pool-resolved, pitched
// instrument: on top of the plain voices_/mute/solo/sends/azimuth
// bookkeeping every leaf track shares, this adds instrument_id_ resolution
// and everything that follows from notes actually having pitch/identity -
// note columns doubling as a chord, per-note aftertouch/channel pressure,
// identity-based retrigger cutoff, SF2 exclusive-class choking. SampleTrackState
// (raw one-shot sample playback, no pitch/identity/chord concept at all)
// is LeafTrackState's other direct subclass, not this one.
class InstrumentTrackState : public LeafTrackState {
public:
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, const SendLevels & sends)
    : LeafTrackState(channel_config, solo, muted, track_id, position, sends), instrument_id_(instrument_id) { }

  // Which Track this state's notes should play through - resolved once per
  // render()/live note-on call (Player.cpp's PLAY_NOTE/NOTE_PRESSURE
  // handling calls this directly too, on whatever InstrumentPool it has in
  // scope), not once per individual note - every note in one call
  // currently shares one instrument. Default: this track's own
  // instrument_id_ pool index. PercussionTrackState (PercussionTrack.cpp,
  // local to it) overrides this to ignore instrument_id_ entirely and
  // return `instruments`'s own getDefaultKitInstrument() instead - see its
  // own class comment for why it has no pool index of its own to resolve.
  virtual const Track * getInstrumentSource(const InstrumentPool & instruments) const {
    return instruments.getByIndex(instrument_id_);
  }

  void setSpatialMode(SpatialMode mode) { spatial_mode_ = mode; }

  AudioBuffer render(int frames, const InstrumentPool & instruments, RenderContext & context) override {
    clearFinishedVoices();

    // A stop or seek ends the slides: voices keep the pitch they have, and
    // no later note inherits a half-done glide.
    if (context.getTransportEpoch() != slide_epoch_) {
      slide_epoch_ = context.getTransportEpoch();
      for (auto & [column, rate] : glide_rates_) rate.frames_left = 0; // voices hold the pitch they have
      if (!pitch_slides_.empty()) {
        completed_slide_cents_ = slidePosition(0);
        pitch_slides_.clear();
        slide_ramp_pending_ = true; // one more call flattens the voices' ramp
      }
    }

    // Render each chunk (processing note-on/off events as they come due)
    // without committing to a final shape up front - a voice triggered
    // mid-block can carry a send no earlier chunk this block had. Collect
    // the chunks and decide the accumulator's real shape only once every
    // chunk is known, from what actually came back.
    std::vector<std::pair<int, AudioBuffer> > chunks;

    auto instrument = getInstrumentSource(instruments);
    if (instrument) {
      auto & pending_events = context.getPendingEvents(getTrackId());
      auto & pending_azimuth = context.getPendingAzimuthTicks(getTrackId());
      auto & pending_slides = context.getPendingPitchSlides(getTrackId());
      auto & pending_glides = context.getPendingPitchGlides(getTrackId());

      for (int i = 0; i < frames; ) {
	int render_size = frames - i;
        // The song's pitch here: a note started in this chunk starts at it.
        global_cents_ = context.globalPitchCents(i);
        // Note events and azimuth-slide ticks are two independent timelines
	// (see RenderContext.h) that can land on different frames within
	// the same block, so each only asserts that `i` never overshoots
	// its own next entry - not that every stop lands exactly on this
	// source's entry, which the other source's own boundary can now
	// force too.
	if (!pending_events.empty()) {
	  auto it = pending_events.begin();
	  assert(i <= it->first);
	  if (i == it->first) {
	    for (auto & ev : it->second) {
	      if (ev.isStopAll()) {
		stopAllVoices();
	      } else if (ev.isAftertouch()) {
		notePressure(ev.getId(), ev.getVelocity());
	      } else if (ev.isOff()) {
		noteOff(ev.getId());
	      } else {
                // ev.getNoteCoordinate(): the note's own stable coordinate,
                // built once at SongState.h's scheduling loop from its
                // authored (track, row, column) position - what
                // InstrumentVoice's own constructor derives this note's start
                // phase from, decorrelating repeated/unison identical
                // oscillators so they don't comb-filter when summed. The
                // live-audition path (Player.cpp) calls the same noteOn().
                note_override_ = ev.getNoteOverride();
                // A 0Gxx row glides the column's sounding note instead of
                // starting this one; with nothing sounding it plays as usual.
                if (!(note_override_.has_glide && startGlide(ev.getId(), ev.getTuning(), ev.getNoteValue()))) {
                  noteOn(ev.getId(), *instrument, ev.getTuning(), ev.getVelocity(), ev.getNoteValue(), ev.getNoteCoordinate());
                }
                note_override_ = {};
	      }
	    }
	    it = pending_events.erase(it);
          }
	  if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
	}

	if (!pending_azimuth.empty()) {
	  auto it = pending_azimuth.begin();
	  assert(i <= it->first);
	  if (i == it->first) {
	    adjustAzimuth(it->second);
	    it = pending_azimuth.erase(it);
	  }
	  if (it != pending_azimuth.end() && it->first - i < render_size) render_size = it->first - i;
	}

        if (!pending_slides.empty()) {
          auto it = pending_slides.begin();
          assert(i <= it->first);
          if (i == it->first) {
            for (auto & slide : it->second) {
              if (slide.frames > 0 && slide.cents != 0.0f) pitch_slides_.push_back({static_cast<double>(slide.cents), slide.frames, 0});
            }
            it = pending_slides.erase(it);
          }
          if (it != pending_slides.end() && it->first - i < render_size) render_size = it->first - i;
        }

        if (!pending_glides.empty()) {
          auto it = pending_glides.begin();
          assert(i <= it->first);
          if (i == it->first) {
            for (auto & glide : it->second) {
              // Column -1 is every column with a voice; a later entry for one
              // column overrides it.
              if (glide.column >= 0) {
                setGlideRate(glide.column, glide);
              } else {
                for (auto & [column, voices] : voices_) setGlideRate(column, glide);
              }
            }
            it = pending_glides.erase(it);
          }
          if (it != pending_glides.end() && it->first - i < render_size) render_size = it->first - i;
        }
        for (auto & [column, rate] : glide_rates_)
          if (rate.frames_left > 0) render_size = std::min(render_size, rate.frames_left);

        // A running slide ends exactly where its row does, so cut the chunk
        // there: the voices interpolate over a whole chunk.
        for (auto & slide : pitch_slides_) render_size = std::min(render_size, slide.length - slide.elapsed);
        render_size = context.nextGlobalPitchBreak(i, i + render_size) - i;

        advancePitchSlides(render_size, global_cents_, context.globalPitchCents(i + render_size));
        advancePitchGlides(render_size);
        chunks.emplace_back(i, renderVoices(render_size));
        i += render_size;
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & [ pos, s ] : chunks) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.setBpm(context.getBpm());
    data.zero();
    for (auto & [ pos, s ] : chunks) data.assignNamed(s, pos);
    applyPerformanceAudio(data);

    // getSends()/getPosition().azimuth are already this block's own
    // post-glide values - every chunk's own renderVoices() call above
    // already advanced them via advanceSendRamps()/advanceAzimuthRamp() -
    // so this overwrite of that call's own TrackInfo (real RMS/clipping
    // now known for the whole block, not just one chunk) still carries
    // the live values forward correctly.
    setTrackInfo(TrackInfo( isActive(), data.isClipping(), data.calculateMainRMS(), getSends().main, getSends().a, getSends().b, getPosition().azimuth, true ));

    // Where a note auditioned before the next block starts.
    global_cents_ = context.globalPitchCents(frames);
    return data;
  }

  // Note-on, shared by both a track's pattern-driven note-on (render(frames,
  // instruments, context)'s pending-events loop above) and live audition
  // (Player::handlePlaybackControlEvent()'s PLAY_NOTE case, shared by
  // Kitty-keyboard note entry and Launchpad NOTES/step-grid presses) -
  // `instrument` is whatever this track's own instrument_id_ resolves to
  // (the caller already looked it up to reach this class in the first
  // place). `note_coord` identifies this note event for HashField purposes
  // (NoteCoordinate.h) - threaded through rather than fixed here since the
  // two callers build it differently (see each call site's own comment).
  void noteOn(int column, const Track & instrument, Tuning tuning, float velocity, int note_value, const NoteCoordinate & note_coord) {
    retriggerVoices(column, note_value);

    // position_.extent < 0 means "not authored on this track" (see
    // LeafTrack::getExtent()) - resolve it to the assigned
    // instrument's own family default (Track::getDefaultExtent(), 0 for
    // anything without one) once, here, before the position ever reaches
    // playNote()/SoundFontInstrument.
    auto resolved_position = getPosition();
    if (note_override_.has_azimuth) resolved_position.azimuth = note_override_.azimuth;
    if (note_override_.has_extent) resolved_position.extent = note_override_.extent;
    if (resolved_position.extent < 0.0f) resolved_position.extent = instrument.getDefaultExtent();

    float detune = note_override_.has_detune ? std::exp2(note_override_.detune_cents / 1200.0f) : 1.0f;
    // The song's pitch (standard pitch and YUxx/YDxx) comes after every
    // pitch of the note's own; a voice's later slide ramps are measured from
    // here (the setPitchRamp() below), so they carry its changes.
    if (global_cents_ != 0.0) detune *= static_cast<float>(std::exp2(std::clamp(global_cents_, -24000.0, 24000.0) / 1200.0));
    auto voice = instrument.playNote(getChannelConfiguration(), resolved_position, spatial_mode_, tuning, detune, velocity, note_value, getSends(), note_coord);
    chokeExclusiveClasses(*voice);
    // Later slide ramps (the track's and the song's) are measured from here.
    double origin = slidePosition(0) + global_cents_;
    voice->setPitchRamp(origin, origin);
    addVoice(column, move(voice));
    // A later glide in this column is measured from this note.
    glide_anchors_[column] = {static_cast<double>(getFrequencyFor(tuning, note_value)), note_override_.has_detune ? note_override_.detune_cents : 0.0f};
  }

  // noteOn() with a per-note override, as a pattern note's fx gives one.
  void noteOn(int column, const Track & instrument, Tuning tuning, float velocity, int note_value, const NoteCoordinate & note_coord,
              const NoteOverride & note_override) {
    note_override_ = note_override;
    noteOn(column, instrument, tuning, velocity, note_value, note_coord);
    note_override_ = {};
  }

  // Live-audition polyphonic aftertouch (Player::handlePlaybackControlEvent()'s
  // NOTE_PRESSURE case) - the counterpart to noteOn() above.
  void notePressure(int column, float velocity) { applyAftertouch(column, velocity); }

  void applyAftertouch(int column, float aftertouch) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isActive()) voice->applyAftertouch(aftertouch);
    }

    // A Track here plays the role a MIDI channel plays on a real
    // multitimbral synth - so this column's own poly pressure also feeds
    // the track-wide channel-pressure value (broadcastChannelPressure()),
    // derived as the max across every currently-held column so a held
    // chord gets one shared vibrato/filter depth rather than uneven
    // per-note wobble.
    column_pressure_[column] = aftertouch;
    broadcastChannelPressure();
  }

  // Recomputes the max of every currently-held column's poly pressure and
  // pushes it to every active voice in every column (not just the column
  // that changed) - see applyAftertouch() above. Also called from
  // stopVoices()/stopAllVoices() overrides below, since releasing a column
  // can lower the max for the notes still held.
  void broadcastChannelPressure() {
    float max_pressure = 0.0f;
    for (auto & [ column, pressure ] : column_pressure_) {
      max_pressure = std::max(max_pressure, pressure);
    }
    pushChannelPressureToVoices(max_pressure);
  }

  // Real MIDI/hardware channel pressure (as opposed to the per-note poly
  // pressure aggregated into a channel-pressure-equivalent by
  // broadcastChannelPressure() above) - applied directly, with no column
  // bookkeeping, since the source already reports a single track-wide
  // value rather than something that needs a per-column max. Deliberately
  // does not touch column_pressure_, so it can't be stale-overwritten by
  // a later broadcastChannelPressure() call from an unrelated poly-pressure
  // column update - the two input kinds are mutually exclusive on real
  // hardware (see LaunchpadChannelPressureEvent), so this simple
  // last-write-wins is never actually contended in practice.
  void applyRealChannelPressure(float pressure) {
    pushChannelPressureToVoices(pressure);
  }

  // LeafTrackState::stopVoices()'s own voice release, plus this class's own
  // pressure bookkeeping on top - without this, a hard-pressed note's stale
  // pressure would keep inflating the max (broadcastChannelPressure())
  // after it releases and a new, softer note starts. SampleTrackState has
  // no equivalent need (no column_pressure_ concept at all), so this stays
  // here rather than on the shared base.
  void stopVoices(int column) {
    LeafTrackState::stopVoices(column);
    column_pressure_.erase(column);
    broadcastChannelPressure();
  }

  // Ends live-audition note `column` (Player::handlePlaybackControlEvent()'s
  // STOP_NOTE case, shared by Kitty-keyboard note entry and Launchpad
  // NOTES/step-grid presses).
  void noteOff(int column) { stopVoices(column); }

  // LeafTrackState::stopAllVoices()'s own whole-track release, plus this
  // class's own pressure bookkeeping on top (see stopVoices() above for why
  // that stays here rather than on the shared base).
  void stopAllVoices() override {
    LeafTrackState::stopAllVoices();
    column_pressure_.clear();
    broadcastChannelPressure();
  }

  // Identity-based retrigger cutoff: a new note-on whose identity (31-EDO
  // step for pitched tracks, MIDI note number for percussion - already
  // resolved into `note_value` by the caller; see TrackEvent::getNoteValue()/
  // PlaybackControlEvent's midi_note parameter) exactly matches a
  // still-sounding voice anywhere in this track gets that prior voice
  // fast-released (VoiceState::fastRelease() - a short ~10ms release via
  // the existing envelope machinery, never a hard cut) rather than left to
  // ring out its full authored SF2 release tail, which is what let
  // long-release GM patches pile up voices under rapid retriggering. It's
  // masked by the new attack either way, so this is inaudible - the only
  // effect is reclaiming the voice. Exact integer equality only: a 31-EDO
  // chord's notes have different values and never self-match.
  //
  // A column whose *current* occupant has a different identity still gets
  // the old natural-release behavior (voice->stopNote(), unchanged) - a
  // chord note being replaced by a different note should ring out under
  // the new one, not cut off.
  //
  // Scans every column, not just `column`: nothing prevents the same
  // identity appearing in a different note-column of the same track (a
  // doubled unison chord note, or two near-simultaneous presses landing in
  // different chord slots), so a same-identity match can legitimately be
  // more than one voice - every match is handled, not just the first.
  void retriggerVoices(int column, int note_value) {
    for (auto & [ col, voices ] : voices_) {
      for (auto & voice : voices) {
        if (!voice->isActive()) continue;
        if (voice->getNoteValue() == note_value) {
          voice->fastRelease();
        } else if (col == column) {
          voice->stopNote();
        }
      }
    }

    // Same reasoning as stopVoices() above - a note being replaced
    // shouldn't leave its stale pressure inflating the track-wide max.
    column_pressure_.erase(column);
    broadcastChannelPressure();
  }

  // Distinct from retriggerVoices() above: matches on the new voice's own
  // SF2 exclusive class (region.group, gen 57 ExclusiveClass), not note
  // identity - two hi-hat regions choke each other precisely because
  // they're *different* MIDI keys sharing the *same* class, which
  // identity-based matching would never catch. VoiceState::
  // getExclusiveClasses() is empty for every non-SF2 instrument, so this
  // is a no-op there. Scoped to this whole track (every column, not just
  // the new note's own), since exclusive-class values are only meaningful
  // within one preset/instrument, and a track holds exactly one
  // instrument. Called after the new voice is constructed (needs its own
  // regions' class set) and before it's added to voices_, so it can never
  // choke its own sibling regions.
  //
  // A voice can legitimately already have been given a normal stopNote()
  // by retriggerVoices() above (different identity) and then also get
  // fastRelease()'d here (shared class) - that's correct precedence, not
  // double-cutting: exclusive-class choke is the stricter rule and should
  // win over "let it ring" whenever both apply, exactly like a closed
  // hi-hat choking an open one despite their different pitches. Both
  // fastRelease() and stopNote() are safe to call more than once on the
  // same voice (see SoundFontVoice::fastRelease()'s isDone() guard), so
  // this needs no bookkeeping shared with retriggerVoices().
  void chokeExclusiveClasses(const VoiceState & new_voice) {
    auto classes = new_voice.getExclusiveClasses();
    if (classes.empty()) return;

    for (auto & [ col, voices ] : voices_) {
      for (auto & voice : voices) {
        if (!voice->isActive()) continue;
        auto voice_classes = voice->getExclusiveClasses();
        bool shares_class = std::any_of(classes.begin(), classes.end(), [&](int c) {
          return std::find(voice_classes.begin(), voice_classes.end(), c) != voice_classes.end();
        });
        if (shares_class) voice->fastRelease();
      }
    }
  }

protected:
  // Shared by broadcastChannelPressure() (poly-pressure-derived) and
  // applyRealChannelPressure() (real MIDI channel pressure) above - both
  // ultimately just push one value to every currently active voice.
  void pushChannelPressureToVoices(float pressure) {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) voice->applyChannelPressure(pressure);
      }
    }
  }

private:
 // 0Gxx: points the column's newest sounding voice at the note's pitch, in
 // cents from the pitch that voice started at. False when none sounds.
 bool startGlide(int column, Tuning tuning, int note_value) {
   auto anchor = glide_anchors_.find(column);
   auto voices = voices_.find(column);
   if (anchor == glide_anchors_.end() || voices == voices_.end()) return false;
   VoiceState * target = nullptr;
   for (auto & voice : voices->second)
     if (voice->isActive()) target = voice.get();
   if (!target) return false;
   const float fine = note_override_.has_detune ? note_override_.detune_cents : 0.0f;
   const double cents = 1200.0 * std::log2(static_cast<double>(getFrequencyFor(tuning, note_value)) / anchor->second.hz) + static_cast<double>(fine - anchor->second.fine_cents);
   target->setPitchGlideTarget(cents);
   glide_in_use_ = true;
   setGlideRate(column, {column, note_override_.glide_cents_per_frame, note_override_.glide_frames, note_override_.glide_instant});
   return true;
 }

 // A column's glide rate from now on; an instant one lands the column's
 // voices on their targets at once.
 void setGlideRate(int column, const RenderContext::PitchGlide & glide) {
   glide_rates_[column] = {glide.cents_per_frame, glide.frames, glide.instant};
   if (!glide.instant) return;
   auto voices = voices_.find(column);
   if (voices == voices_.end()) return;
   for (auto & voice : voices->second)
     if (voice->isActive()) voice->jumpPitchGlide();
 }

 // Moves every voice toward its glide target for the coming chunk (the row's
 // rate while a 0Gxx row runs, else holding), and ends the bookkeeping once
 // nothing sounds.
 void advancePitchGlides(int frames) {
   if (!glide_in_use_) return;
   bool any = false;
   for (auto & [column, voices] : voices_) {
     auto found = glide_rates_.find(column);
     const bool running = found != glide_rates_.end() && found->second.frames_left > 0 && !found->second.instant;
     const double rate = running ? static_cast<double>(found->second.cents_per_frame) : 0.0;
     for (auto & voice : voices) {
       if (!voice->isActive()) continue;
       voice->advancePitchGlide(rate, frames);
       any = true;
     }
   }
   for (auto & [column, rate] : glide_rates_) rate.frames_left = std::max(0, rate.frames_left - frames);
   if (!any) glide_in_use_ = false;
 }

 struct GlideAnchor {
   double hz;
   float fine_cents;
 };
 std::unordered_map<int, GlideAnchor> glide_anchors_; // per column: the note its newest voice began on
 struct GlideRate {
   float cents_per_frame = 0.0f;
   int frames_left = 0;
   bool instant = false;
 };
 std::unordered_map<int, GlideRate> glide_rates_; // per column: how its glide moves, while its row lasts
 bool glide_in_use_ = false;

 // The slide position (cents) `extra` frames into the coming chunk: the
 // finished slides' amounts plus each running one's share by its absolute
 // place in its row.
 double slidePosition(int extra) const {
   double cents = completed_slide_cents_;
   for (auto & slide : pitch_slides_) {
     int elapsed = std::min(slide.elapsed + extra, slide.length);
     cents += slide.cents * static_cast<double>(elapsed) / static_cast<double>(slide.length);
   }
   return cents;
 }

 // Hands each sounding voice the slide position at the chunk's first and
 // last sample (the end being the next chunk's start), the song's pitch
 // (`global_start`/`global_end`, cents) added, then moves on.
 void advancePitchSlides(int frames, double global_start, double global_end) {
   const bool active = !pitch_slides_.empty() || global_end != 0.0;
   if (active || slide_ramp_pending_ || global_start != 0.0) {
     double start = slidePosition(0) + global_start, end = slidePosition(frames) + global_end;
     for (auto & [column, voices] : voices_) {
       for (auto & voice : voices)
         if (voice->isActive()) voice->setPitchRamp(start, end);
     }
   }
   slide_ramp_pending_ = active;
   for (auto & slide : pitch_slides_) slide.elapsed += frames;
   for (auto & slide : pitch_slides_)
     if (slide.elapsed >= slide.length) completed_slide_cents_ += slide.cents;
   pitch_slides_.erase(std::remove_if(pitch_slides_.begin(), pitch_slides_.end(), [](const PitchSlideRamp & s) { return s.elapsed >= s.length; }), pitch_slides_.end());
 }

 struct PitchSlideRamp {
   double cents;
   int length;
   int elapsed;
 };
 std::vector<PitchSlideRamp> pitch_slides_;
 double completed_slide_cents_ = 0.0;
 double global_cents_ = 0.0;       // the song's pitch (RenderContext::globalPitchCents()) where rendering is
 int slide_epoch_ = 0;             // the transport epoch the slides were last checked against
 bool slide_ramp_pending_ = false; // the last chunk had a slide running: voices still hold its end

 int instrument_id_;
 SpatialMode spatial_mode_ = SpatialMode::AUTO;
 std::unordered_map<int, float> column_pressure_;
 // The note being started by render() right now, for noteOn() to read.
 NoteOverride note_override_;
};

#endif

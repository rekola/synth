#ifndef _INSTRUMENTTRACKSTATE_H_
#define _INSTRUMENTTRACKSTATE_H_

#include "TrackState.h"
#include "TrackEvent.h"
#include "SampleData.h"
#include "RenderContext.h"
#include "SphericalPosition.h"
#include "SendLevels.h"

#include <algorithm>

class InstrumentTrackState : public TrackState {
public:
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, const SendLevels & sends)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), instrument_id_(instrument_id), position_(position), sends_(sends) { }

  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    clearFinishedVoices();

    // Render each chunk (processing note-on/off events as they come due)
    // without committing to a final shape up front - a voice triggered
    // mid-block can carry a send no earlier chunk this block had. Collect
    // the chunks and decide the accumulator's real shape only once every
    // chunk is known, from what actually came back.
    std::vector<std::pair<int, SampleData> > chunks;

    if (instrument_id_ >= 0 && instrument_id_ < instruments.size()) {
      auto & instrument = instruments[instrument_id_];
      auto & pending_events = context.getPendingEvents(track_id_);
      auto & pending_azimuth = context.getPendingAzimuthTicks(track_id_);

      for (int i = 0; i < frames; ) {
	int render_size = frames - i;
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
	      if (ev.isAftertouch()) {
		applyAftertouch(ev.getId(), ev.getVelocity());
	      } else if (ev.isOff()) {
		stopVoices(ev.getId());
	      } else {
		retriggerVoices(ev.getId(), ev.getNoteValue());
		// position_.extent < 0 means "not authored on this track" (see
		// InstrumentTrack::getExtent()) - resolve it to the assigned
		// instrument's own family default (Track::getDefaultExtent(),
		// 0 for anything without one) once, here, before the position
		// ever reaches playNote()/NoteMultiplier/SoundFontInstrument.
		auto resolved_position = position_;
		if (resolved_position.extent < 0.0f) resolved_position.extent = instrument->getDefaultExtent();
		auto voice = instrument->playNote(getChannelConfiguration(), resolved_position, ev.getFrequency(), 1.0f, ev.getVelocity(), -getRandF(), ev.getNoteValue(), sends_);
		chokeExclusiveClasses(*voice);
		addVoice(ev.getId(), move(voice));
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

	chunks.emplace_back(i, render(render_size));
	i += render_size;
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & [ pos, s ] : chunks) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    SampleData data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.setBpm(context.getBpm());
    data.zero();
    for (auto & [ pos, s ] : chunks) data.assignNamed(s, pos);

    setTrackInfo(TrackInfo( isActive(), data.isClipping() ));

    return data;
  }

  SampleData render(int frames) override {
    // Render every active voice first (still calling render() even when
    // muted, so envelopes/LFOs keep advancing - only mixing is skipped),
    // then decide this track's own accumulator shape from what actually
    // came back rather than a separate non-rendering prediction.
    std::vector<SampleData> rendered;
    bool is_active = false;

    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) {
	  auto s = voice->render(frames);
	  is_active = true;
	  if (!isMuted()) rendered.push_back(std::move(s));
	}
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    SampleData data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.zero();

    // Every voice now spatially encodes itself directly, using its own
    // position, to its own real (never reduced) ChannelConfiguration - see
    // InstrumentVoice::encodePosition() - so a voice's rendered output
    // always already matches this accumulator's shape exactly; no
    // per-voice dispatch is needed, just a plain mix.
    for (auto & s : rendered) data.mixNamed(s);

    setTrackInfo(TrackInfo( is_active, data.isClipping() ));

    return data;
  }

  void addVoice(int column, std::unique_ptr<TrackState> voice) {
    voices_[column].push_back(std::move(voice));
  }
  
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
  // stopVoices() below, since releasing the hardest-pressed column can
  // lower the max for the notes still held.
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

  void stopVoices(int column) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isActive()) voice->stopNote();
    }

    // Without this, a hard-pressed note's stale pressure would keep
    // inflating the max (broadcastChannelPressure()) after it releases and
    // a new, softer note starts.
    column_pressure_.erase(column);
    broadcastChannelPressure();
  }

  // Identity-based retrigger cutoff: a new note-on whose identity (31-EDO
  // step for pitched tracks, MIDI note number for percussion - already
  // resolved into `note_value` by the caller; see TrackEvent::getNoteValue()/
  // PlaybackControlEvent's midi_note parameter) exactly matches a
  // still-sounding voice anywhere in this track gets that prior voice
  // fast-released (TrackState::fastRelease() - a short ~10ms release via
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
  // identity-based matching would never catch. TrackState::
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
  void chokeExclusiveClasses(const TrackState & new_voice) {
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

  void clear() override {
    TrackState::clear();
    voices_.clear();
  }

  bool isActive() const override {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) return true;
      }
    }
    return false;
  }

  int getVoiceCount() const override {
    int n = TrackState::getVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getVoiceCount();
      }
    }
    return n;
  }
  
  int getAllocatedVoiceCount() const override {
    int n = TrackState::getAllocatedVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getAllocatedVoiceCount();
      }
    }
    return n;
  }

  void getAllActiveVoices(std::unordered_map<int, std::vector<ActiveVoiceInfo> > & out) const override {
    std::vector<ActiveVoiceInfo> own;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) own.push_back({ voice->getNoteValue(), voice->getLoudness() });
      }
    }
    if (!own.empty()) out[track_id_] = std::move(own);
    TrackState::getAllActiveVoices(out);
  }

  // Live control changes, pushed from the UI thread via PlaybackControlEvent
  // (SET_TRACK_MUTED/SOLO/SEND_A/SEND_B/SEND_MAIN - see Player::handleEvent) and
  // applied directly to this already-running state, unlike the constructor
  // argument above which only seeds the initial value at song load. Renamed
  // from setMuted/setSolo's old protected-only visibility (this class had no
  // way to receive a live update before) - stopVoices() above sets the
  // precedent for a public real-time control entry point.
  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

  // Send Main/A/B all push into every already-active voice too, not just
  // future notes (unlike setAzimuth() below - see adjustAzimuth() there
  // for the general reasoning: sends_ isn't read fresh from anywhere but
  // this voice's own construction otherwise). Reuses the same TrackState::
  // adjust*() virtual-recursion mechanism adjustAzimuth() does (so a
  // multi-region SoundFontInstrument group's real leaf voices are all
  // reached too), just carrying an absolute value instead of a per-tick
  // delta - there's no tick-scheduled slide command for sends the way
  // there is for azimuth, these are live knobs (Launchpad/UI Send rows),
  // not a pattern effect. See TrackState::adjustSendMain()/adjustSendA()/
  // adjustSendB().
  void setSendMain(float s) {
    sends_.main = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendMain(s);
    }
  }
  void setSendA(float s) {
    sends_.a = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendA(s);
    }
  }
  void setSendB(float s) {
    sends_.b = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendB(s);
    }
  }

  // The live-knob path (Launchpad/UI Pan row, via Controller::
  // setTrackAzimuth()) - unlike Send Main/A/B just above, this one is
  // deliberately still "only future notes pick it up": already-playing
  // voices keep whatever position they were constructed with
  // (InstrumentVoice's own encodePosition() bakes it in once too). A live
  // voice-reaching azimuth push does exist (adjustAzimuth() below), but
  // it's driven only by the 2Lxx/2Rxx tick-scheduled slide command, not by
  // this knob.
  void setAzimuth(float a) { position_.azimuth = a; }
  float getAzimuth() const { return position_.azimuth; }

  // 2Lxx/2Rxx azimuth slide (Command::isAzimuthSlide(), scheduled per-tick
  // by SongState::scheduleAzimuthSlide(), consumed above in this class's
  // own chunked render() loop) - deliberately the opposite of setAzimuth()
  // above: it reaches every already-active voice too, not just future
  // notes, since the whole point of a slide command is to audibly move
  // whatever is currently sounding. TrackState::adjustAzimuth()'s default
  // recursion (overridden by InstrumentVoice - see its own comment) makes
  // this correct even for a multi-region SoundFontInstrument group.
  void adjustAzimuth(float delta) override {
    position_.azimuth += delta;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustAzimuth(delta);
    }
  }

protected:
  static inline bool is_not_playing(const std::unique_ptr<TrackState> & voice) { return !voice->isActive(); }

  void clearFinishedVoices() {
    for (auto & [ id, voices ] : voices_) {
      voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());
    }
  }

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
  bool solo_, muted_;
  int track_id_, instrument_id_;
  SphericalPosition position_;
  SendLevels sends_;

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
  std::unordered_map<int, float> column_pressure_;
};

#endif

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
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, float portamento, const SendLevels & sends)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), instrument_id_(instrument_id), position_(position), portamento_(portamento), sends_(sends) { }

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

      for (int i = 0; i < frames; ) {
	int render_size = frames - i;
	if (!pending_events.empty()) {
	  auto it = pending_events.begin();
	  assert(i <= it->first);
	  assert(i == 0 || i == it->first);
	  if (i == it->first) {
	    for (auto & ev : it->second) {
	      if (ev.isAftertouch()) {
		applyAftertouch(ev.getId(), ev.getVelocity());
	      } else if (ev.isOff()) {
		stopVoices(ev.getId());
	      } else {
		bool portamento_done = false;
		if (portamento_ >= 0.0f) {
		  auto it = voices_.find(ev.getId());
		  if (it != voices_.end()) {
		    for (auto & voice : it->second) {
		      if (voice->isActive()) {
			voice->playNote(ev.getFrequency(), ev.getVelocity(), ev.getNoteValue());
			portamento_done = true;
		      }
		    }
		  }
		}
		if (!portamento_done) {
		  stopVoices(ev.getId());
		  auto voice = instrument->playNote(getChannelConfiguration(), position_, ev.getFrequency(), 1.0f, ev.getVelocity(), -getRandF(), ev.getNoteValue(), sends_);
		  addVoice(ev.getId(), move(voice));
		}
	      }
	    }
	    it = pending_events.erase(it);
	  }
	  if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
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

  // Only notes triggered after the change pick up the new value (playNote()
  // above reads sends_ directly) - already-sounding voices keep whatever
  // was baked into them at their own construction, the same "static once
  // baked" behavior as every other effect parameter in this codebase (e.g.
  // the shared reverb/delay bus parameters). See SendLevels.h.
  void setSendA(float s) { sends_.a = s; }
  void setSendB(float s) { sends_.b = s; }
  void setSendMain(float s) { sends_.main = s; }

  // Same "only future notes pick it up" caveat as SendA/SendB above -
  // already-playing voices keep whatever position they were constructed
  // with (InstrumentVoice's own encodePosition() bakes it in once too).
  void setAzimuth(float a) { position_.azimuth = a; }

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
  float portamento_;
  SendLevels sends_;

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
  std::unordered_map<int, float> column_pressure_;
};

#endif

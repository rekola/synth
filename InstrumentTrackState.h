#ifndef _INSTRUMENTTRACKSTATE_H_
#define _INSTRUMENTTRACKSTATE_H_

#include "TrackState.h"
#include "TrackEvent.h"
#include "SampleData.h"
#include "RenderContext.h"
#include "SphericalPosition.h"

#include <algorithm>

class InstrumentTrackState : public TrackState {
public:
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, float portamento, float send_a, float send_b)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), instrument_id_(instrument_id), position_(position), portamento_(portamento), send_a_(send_a), send_b_(send_b) { }

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
		  auto voice = instrument->playNote(getChannelConfiguration(), position_, ev.getFrequency(), 1.0f, ev.getVelocity(), -getRandF(), ev.getNoteValue(), send_a_, send_b_);
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

    bool has_send_a = false, has_send_b = false;
    for (auto & [ pos, s ] : chunks) {
      has_send_a = has_send_a || s.hasChannel(Channel::SendA);
      has_send_b = has_send_b || s.hasChannel(Channel::SendB);
    }
    auto channels = regularChannelsFor(getChannelConfiguration());
    if (has_send_a) channels.push_back(Channel::SendA);
    if (has_send_b) channels.push_back(Channel::SendB);

    SampleData data(channels, frames, isSolo());
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

    bool has_send_a = false, has_send_b = false;
    for (auto & s : rendered) {
      has_send_a = has_send_a || s.hasChannel(Channel::SendA);
      has_send_b = has_send_b || s.hasChannel(Channel::SendB);
    }
    auto channels = regularChannelsFor(getChannelConfiguration());
    if (has_send_a) channels.push_back(Channel::SendA);
    if (has_send_b) channels.push_back(Channel::SendB);

    SampleData data(channels, frames, isSolo());
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
  }

  void stopVoices(int column) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isActive()) voice->stopNote();
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

protected:
  static inline bool is_not_playing(const std::unique_ptr<TrackState> & voice) { return !voice->isActive(); }

  void clearFinishedVoices() {
    for (auto & [ id, voices ] : voices_) {
      voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());
    }
  }

  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

private:
  bool solo_, muted_;
  int track_id_, instrument_id_;
  SphericalPosition position_;
  float portamento_;
  float send_a_, send_b_;

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
};

#endif

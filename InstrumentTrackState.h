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
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, const SphericalPosition & position, float portamento)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), instrument_id_(instrument_id), position_(position), portamento_(portamento) { }
  
  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    clearFinishedVoices();

    SampleData data(getChannelConfiguration(), frames, isSolo());
    data.setBpm(context.getBpm());
    
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
		  auto voice = instrument->playNote(getChannelConfiguration(), position_, ev.getFrequency(), 1.0f, ev.getVelocity(), -getRandF(), ev.getNoteValue());
		  addVoice(ev.getId(), move(voice));
		}
	      }
	    }
	    it = pending_events.erase(it);
	  }
	  if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
	}     

	data.assign(render(render_size), i);
	i += render_size;
      }
    }

    setTrackInfo(TrackInfo( isActive(), data.isClipping() ));

    return data;
  }

  SampleData render(int frames) override {
    SampleData data(getChannelConfiguration(), frames, isSolo());
    data.zero();

    bool is_active = false;
    bool is_ambisonic = getChannelConfiguration().getType() == ChannelConfiguration::AMBISONIC;

    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) {
	  auto s = voice->render(frames);
	  if (!isMuted()) {
	    // Same mismatch-encode rule as TrackState::render(int frames)'s
	    // generic default (see AmbisonicEncoding.h) - voices_ is a
	    // separate storage structure from the generic children_ map, so
	    // this loop needs its own copy of the logic rather than reusing
	    // the base class's.
	    if (is_ambisonic && s.numberOfChannels() < data.numberOfChannels()) {
	      if (!s.isZero()) {
		positional_mixer_.encode(voice.get(), s, voice->getPosition(), data);
		data.setNonZero();
	      }
	    } else {
	      data.mix(s);
	    }
	  }
	  is_active = true;
	}
      }
    }

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
      for (auto & voice : voices) {
	if (is_not_playing(voice)) positional_mixer_.remove(voice.get());
      }
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

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
  PositionalMixer positional_mixer_;
};

#endif

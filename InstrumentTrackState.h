#ifndef _INSTRUMENTTRACKSTATE_H_
#define _INSTRUMENTTRACKSTATE_H_

#include "TrackEvent.h"
#include "Instrument.h"
#include "SampleData.h"
#include "RenderContext.h"

class InstrumentTrackState : public TrackState {
public:
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, int instrument_id, float azimuth, float portamento)
    : TrackState(channel_config, solo, muted), track_id_(track_id), instrument_id_(instrument_id), azimuth_(azimuth), portamento_(portamento) { }
  
  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    clearFinishedVoices();

    SampleData data(getChannelConfiguration(), frames, isSolo());
        
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
		      if (voice->isPlaying()) {
			voice->playNote(ev.getFrequency(), ev.getVelocity());
			portamento_done = true;
		      }
		    }
		  }		    
		}
		if (!portamento_done) {
		  stopVoices(ev.getId());
		  auto voice = instrument->playNote(getChannelConfiguration(), azimuth_, ev.getFrequency(), 1.0f, ev.getVelocity(), getRandF());
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
    
    return data;
  }

  SampleData render(int frames) override {
    SampleData data(getChannelConfiguration(), frames, isSolo());
    data.zero();

    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isPlaying()) {
	  data.mix(voice->render(frames), 1.0f);
	}
      }
    }
        
    return data;    
  }

  void addVoice(int column, std::unique_ptr<TrackState> voice) {
    voices_[column].push_back(std::move(voice));
  }
  
  void applyAftertouch(int column, float aftertouch) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isPlaying()) voice->applyAftertouch(aftertouch);
    }
  }

  void stopVoices(int column) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isPlaying()) voice->stopNote();
    }
  }

  void clear() override {
    TrackState::clear();
    voices_.clear();
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

protected:
  static inline bool is_not_playing(const std::unique_ptr<TrackState> & voice) { return !voice->isPlaying(); }

  void clearFinishedVoices() {
    for (auto & [ id, voices ] : voices_) {
      voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());      
    }
  }
  
private:
  int track_id_, instrument_id_;
  float azimuth_;
  float portamento_;

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
};

#endif

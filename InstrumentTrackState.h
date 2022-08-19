
#ifndef _INSTRUMENTTRACKSTATE_H_
#define _INSTRUMENTTRACKSTATE_H_

#include "TrackEvent.h"
#include "Instrument.h"
#include "SampleData.h"
#include "RenderContext.h"

class InstrumentTrackState : public TrackState {
public:
  explicit InstrumentTrackState(const ChannelConfiguration & channel_config, int track_id, int instrument_id, float azimuth, bool is_solo)
    : TrackState(channel_config), track_id_(track_id), instrument_id_(instrument_id), azimuth_(azimuth), is_solo_(is_solo) { }
  
  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) override {
    clearFinishedVoices();

    SampleData data(getChannelConfiguration(), 0, is_solo_);

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
		bool legato_done = false;
		if (is_legato_) {
		  auto it = voices_.find(ev.getId());
		  if (it != voices_.end()) {
		    for (auto & voice : it->second) {
		      if (voice->isPlaying()) {
			voice->playNote(ev.getFrequency(), ev.getVelocity());
			legato_done = true;
		      }
		    }
		  }		    
		}
		if (!legato_done) {
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
	
	data.append(render(render_size));
	
	i += render_size;
      }
    }
    
    return data;
  }

  SampleData render(int frames) override {
    SampleData data;

    if (frames > 0) {
      for (auto & [ column, voices ] : voices_) {
	for (auto & voice : voices) {
	  if (voice->isPlaying()) {
	    if (data.empty()) {
	      data = voice->render(frames);
	    } else {
	      data.mix(voice->render(frames), 1.0f);
	    }
	  }
	}
      }
      
      if (data.empty()) {
	data = SampleData(getChannelConfiguration(), frames, is_solo_);
	data.zero();
      }
    }
    
    return data;    
  }

  TrackState & addVoice(int column, std::unique_ptr<TrackState> voice) {
    auto v = voice.get();
    voices_[column].push_back(std::move(voice));
    return *v;
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
  bool is_solo_;
  bool is_legato_ = false;

  std::unordered_map<int, std::vector<std::unique_ptr<TrackState> > > voices_;
};

#endif

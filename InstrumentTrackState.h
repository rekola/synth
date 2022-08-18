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
	      } else {
		stopVoices(ev.getId());
		if (!ev.isOff()) {
		  auto voice = instrument->playNote(getChannelConfiguration(), azimuth_, ev.getFrequency(), ev.getVelocity());
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
      for (auto & [ id, child ] : getVoices()) {
	if (child->isPlaying()) {
	  if (data.empty()) {
	    data = child->render(frames);
	  } else {
	    data.mix(child->render(frames), 1.0f);
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

  TrackState & addVoice(int identifier, std::unique_ptr<TrackState> voice) {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), is_not_playing), voices_.end());
    voices_.emplace_back(identifier, std::move(voice));
    return *(voices_.back().second);
  }
  
  void applyAftertouch(int column, float aftertouch) {
    for (auto & [ id, voice ] : voices_) {
      if (column == id && voice->isPlaying()) {
	voice->applyAftertouch(aftertouch);
      }
    }
  }

  void stopVoices(int column) {
    for (auto & [id, child] : voices_) {
      if (column == id && child->isPlaying()) {
	child->stopNote();
      }
    }
  }

  void clear() override {
    TrackState::clear();
    voices_.clear();
  }
  
  int getVoiceCount() const override {
    int n = TrackState::getVoiceCount();
    for (auto & [ id, voice ] : voices_) {
      n += voice->getVoiceCount();
    }
    return n;
  }
  
  int getAllocatedVoiceCount() const override {
    int n = TrackState::getAllocatedVoiceCount();
    for (auto & [ id, voice ] : voices_) {
      n += voice->getAllocatedVoiceCount();
    }
    return n;
  }

  const std::vector<std::pair<int, std::unique_ptr<TrackState> > > & getVoices() const { return voices_; }
  std::vector<std::pair<int, std::unique_ptr<TrackState> > > & getVoices() { return voices_; }

protected:
  static inline bool is_not_playing(const std::pair<int, std::unique_ptr<TrackState> > & a) { return a.first >= 0 && !a.second->isPlaying(); }

private:
  int track_id_, instrument_id_;
  float azimuth_;
  bool is_solo_;

  std::vector<std::pair<int, std::unique_ptr<TrackState> > > voices_;
};

#endif

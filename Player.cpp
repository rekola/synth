#include "Player.h"
#include "AudioAPI.h"
#include "Controller.h"
#include "Logger.h"
#include "Tuner.h"
#include "InstrumentTrack.h"

#include "LogEvent.h"
#include "PlaybackEvent.h"
#include "RecordEvent.h"
#include "PlaybackControlEvent.h"

#include "HRFT.h"
#include "BasicMixer.h"
#include "InstrumentTrackState.h"
#include "FFT.h"

using namespace std;

class EventLogger : public Logger {
 public:
  EventLogger(EventQueue * _event_queue) : event_queue(_event_queue) { }

  void log(std::string s) override {
    event_queue->push(make_unique<LogEvent>(std::move(s)));
  }

private:
  EventQueue * event_queue;
};

void
Player::handlePlaybackControlEvent(PlaybackControlEvent & ev) {
  auto & song = controller_->getSong();

  switch (ev.getType()) {
  case PlaybackControlEvent::PLAY_NOTE:
  case PlaybackControlEvent::NOTE_PRESSURE:
    {
      auto track_id = ev.getParameter1();
      auto column = ev.getParameter2();
      auto midi_note = ev.getParameter3();
      auto midi_velocity = ev.getParameter4();
      
      auto track = song.getTrackByInternalId(track_id);
      if (track && (track->getType() == TrackType::INSTRUMENT_CONTROL ||
		    track->getType() == TrackType::PERCUSSION_CONTROL
		    )) {
	auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	
	if (instrument_track.getInstrumentId() < song.getInstruments().size()) {
	  auto & instrument = song.getInstrument(instrument_track.getInstrumentId());
	  auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getRenderContext().getTrackState(instrument_track.getInternalId()));

	  if (track_state) {
	    auto [ pattern_idx, row_idx ] = state_.getRelativePosition(song);
	    auto & pattern = song.getPattern(pattern_idx);
	    
	    Tuner tuner;
	    
	    if (ev.getType() == PlaybackControlEvent::PLAY_NOTE) {
	      auto tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	      Note note(midi_note, midi_velocity);
	      auto frequency = tuner.getFrequency(tuning, song.getKey(), note);
	      
	      track_state->stopVoices(column);
	      auto voice = instrument.playNote(state_.getChannelConfiguration(), instrument_track.getAzimuth(), frequency, 1.0f, note.getVelocityAsFloat(), 0.0f);
	      track_state->addVoice(column, move(voice));
	    } else {
	      track_state->applyAftertouch(column, midi_velocity / 127.0f);
	    }
	  }
	}
      }
    }
    break;
    
  case PlaybackControlEvent::TERMINATE:
    terminate_ = true;
    break;

  case PlaybackControlEvent::PLAY:
    state_.setIsPlaying(true);
    break;
    
  case PlaybackControlEvent::STOP:
    state_.setIsPlaying(false);
    break;
    
  case PlaybackControlEvent::MOVE_POSITION:
    state_.movePosition(ev.getParameter1());
    break;
    
  case PlaybackControlEvent::CLEAR_VOICES:
    {
      auto track_state = state_.getRenderContext().getTrackState(ev.getParameter1());
      if (track_state) track_state->clear();
    }
    break;
    
  case PlaybackControlEvent::STOP_NOTE:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getRenderContext().getTrackState(ev.getParameter1()));
      if (track_state) track_state->stopVoices(ev.getParameter2());
    }      
    break;            
  }
}

void
Player::play(AudioAPI & audio) {
  fft_.setSize(4 * audio.getFrameCount());
    
  EventLogger logger(&(controller_->getUIEventQueue()));
  
  auto & event_queue = controller_->getPlaybackEventQueue();
      
  size_t num_playback_desc = audio.getPlaybackDescriptors().size();
  size_t num_capture_desc = audio.getCaptureDescriptors().size();
  
  size_t num_descriptors = 1 + num_playback_desc + num_capture_desc;

  auto descriptors = std::make_unique<pollfd[]>(num_descriptors);

  descriptors[0].fd = event_queue.getPollFd();
  descriptors[0].events = POLLIN;
  
  for (size_t i = 0; i < num_playback_desc; i++) {
    descriptors[1 + i] = audio.getPlaybackDescriptors()[i];
  }

  for (size_t i = 0; i < num_capture_desc; i++) {
    descriptors[1 + num_playback_desc + i] = audio.getCaptureDescriptors()[i];
  }

  audio.startRecording();

  auto & song = controller_->getSong();
  auto mixer = createMixer(audio.numberOfChannels(), audio.getFrequency(), song.getMixerType());
  
  while ( !terminate_ ) {
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    auto event = event_queue.pop();
	    handleEvent(*event);
	    while ( event_queue.hasEvents() ) {
	      auto event = event_queue.pop();	      
	      handleEvent(*event);
	    }
	    auto ev = createPlaybackEvent(song, state_);
	    controller_->getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    state_.render(audio.getFrameCount(), song, *mixer);
	    auto master = mixer->encode(song.getVolume());
	    audio.play(master, logger);
	    
	    auto ev = createPlaybackEvent(song, state_);
	    ev->setLoudness(master.calculateLoudness());
	    if (fft_.addData(master)) {
	      ev->setFFT(fft_.calculateFFT());
	      fft_.reset();
	    }
	    	    
	    controller_->getUIEventQueue().push(move(ev));
	  } else if (i - 1 - num_playback_desc < num_capture_desc) {
	    auto data = audio.record(logger);
	    controller_->getUIEventQueue().push(make_unique<RecordEvent>(data));
	  }	  
	}
      }
    }
  }
}

std::unique_ptr<PlaybackEvent>
Player::createPlaybackEvent(const Song & song, const SongState & state) {
  auto [ pattern_idx, row_idx ] = state.getRelativePosition(song);

  PlaybackInfo info;
  info.setIsPlaying(state.isPlaying());
  info.setOutSampleRate(state.getChannelConfiguration().getAudioOutSampleRate());
  info.setSampleInterval(song.getSampleInterval(state.getChannelConfiguration().getAudioOutSampleRate()));
  info.setSamplePos(state.getSamplePos());
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setAbsolutePos(state.getAbsolutePosition());
  info.setVoiceCount(state.getVoiceCount());
  info.setAllocatedVoiceCount(state.getAllocatedVoiceCount());

  for (auto & [ track_id, state ] : state.getRenderContext().getTrackStates()) {
    info.setTrackInfo(track_id, state->getInfo());
  }
  
  return make_unique<PlaybackEvent>(info);
}


std::unique_ptr<Mixer>
Player::createMixer(short out_channels, int outSampleRate, MixerType type) {
  switch (type) {
  case MixerType::HRFT: return make_unique<HRFT>(out_channels, outSampleRate);
  case MixerType::BASIC: return make_unique<BasicMixer>(out_channels, outSampleRate);
  default:
    assert(0);
    return unique_ptr<Mixer>(nullptr);
  }
}

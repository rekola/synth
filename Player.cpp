#include "Player.h"
#include "AudioAPI.h"
#include "Controller.h"
#include "Logger.h"
#include "Tuner.h"
#include "InstrumentTrack.h"

#include "LogEvent.h"
#include "PlaybackEvent.h"
#include "RecordEvent.h"

using namespace std;

class EventLogger : public Logger {
 public:
  EventLogger(EventQueue * _event_queue) : event_queue(_event_queue) { }

  void log(std::string s) override {
    event_queue->push(make_unique<LogEvent>(s));
  }

private:
  EventQueue * event_queue;
};

void
Player::handlePlaybackControlEvent(PlaybackControlEvent & ev) {
  auto & song = controller->getSong();

  switch (ev.getType()) {
  case PlaybackControlEvent::PLAY_NOTE:
    {
      auto track_id = ev.getParameter1();
      auto column = ev.getParameter2();
      auto midi_note = ev.getParameter3();
      
      auto * track = song.getChildById(track_id);
      if (track && track->getType() == Track::INSTRUMENT_TRACK) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	
	if (instrument_track.getInstrumentId() < song.getInstruments().size()) {
	  auto & instrument = song.getInstrument(instrument_track.getInstrumentId());
	  auto & track_voices = state.getTrackVoices(track_id);
	
	  auto [ pattern_idx, row_idx ] = state.getRelativePosition(song);
	  auto & pattern = song.getPattern(pattern_idx);
	
	  Tuner tuner;
	  Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
	  Note note(midi_note);
	  
	  int key = pattern.getKey() >= 0 ? pattern.getKey() : song.getKey();
	  float frequency = tuner.getFrequency(tuning, key, note);
	  instrument.playNote(column, frequency, note.getVelocity() / 127.0f, 0.0f, instrument_track.getDetune(), track_voices);
	}
      }
    }
    break;
  case PlaybackControlEvent::TERMINATE:
    {
      terminate = true;
    }
    break;
  default:
    break;
  }

  state.handleEvent(ev);
}

void
Player::play(AudioAPI & audio) {
  EventLogger logger(&(controller->getUIEventQueue()));
  
  auto & event_queue = controller->getPlaybackEventQueue();
      
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

  auto & song = controller->getSong();
  
  while ( !terminate ) {
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
	    auto ev = createPlaybackEvent(song, state);
	    controller->getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    auto data = song.render(audio.getFrameCount(), state);
	    audio.play(data, logger);
	    
	    auto ev = createPlaybackEvent(song, state);
	    ev->setData(data);
	    ev->setLoudness(data.calculateLoudness());
	    
	    controller->getUIEventQueue().push(move(ev));
	  } else {
	    auto data = audio.record(logger);
	    controller->getUIEventQueue().push(make_unique<RecordEvent>(data));
	  }
	}
      }
    }
  }
}

std::unique_ptr<PlaybackEvent>
Player::createPlaybackEvent(const Song & song, SongState & state) {
  auto [ pattern_idx, row_idx ] = state.getRelativePosition(song);

  PlaybackInfo info;
  info.is_playing = state.isPlaying();
  info.outSampleRate = state.getOutSampleRate();
  info.sample_interval = state.getSampleInterval(song);
  info.sample_pos = state.getSamplePos();
  info.pattern_idx = pattern_idx;
  info.row_idx = row_idx;
  info.absolute_pos = state.getAbsolutePosition();
  info.voice_count = state.getVoiceCount();
  info.allocated_voice_count = state.getAllocatedVoiceCount();

  for (auto & [ track_id, state ] : state.getTrackStates()) {
    info.setTrackInfo(track_id, state->getInfo());
  }
  
  return make_unique<PlaybackEvent>(info);
}

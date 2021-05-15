#include "Player.h"
#include "PlaybackEvent.h"
#include "AudioAPI.h"
#include "Controller.h"
#include "SongState.h"

using namespace std;

void
Player::play(Logger & logger, Controller & controller, AudioAPI & audio) {
  auto & event_queue = controller.getPlaybackEventQueue();
    
  SongState state(audio.getFrequency());
  // controller.setSongState(state);
  
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

  auto & song = controller.getSong();
  
  while ( 1 ) {
    if (poll(descriptors.get(), num_descriptors, 1000) > 0) {
      for (size_t i = 0; i < num_descriptors; i++) {
	auto & d = descriptors[i];
	if (d.revents) {
	  if (i == 0) {
	    auto event = event_queue.pop();
	    state.handleEvent(*event);
	    while ( event_queue.hasEvents() ) {
	      auto event = event_queue.pop();	      
	      state.handleEvent(*event);
	    }
	    auto ev = createPlaybackEvent(song, state);
	    controller.getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    auto data = song.render(audio.getFrameCount(), state);
	    audio.play(data, logger);
	    auto ev = createPlaybackEvent(song, state);
	    ev->setData(data);
	    ev->setLoudness(data.calculateLoudness());
	    controller.getUIEventQueue().push(move(ev));
	  } else {
	    auto data = audio.record(logger);
#if 0
	    if (getController().isRecording()) {
	      setStatus(format("recorded {} frames", data.size()));
	      getController().addToSample(data);
	    }
#endif
	  }
	}
      }
    }
  }
}

std::unique_ptr<PlaybackEvent>
Player::createPlaybackEvent(const Song & song, SongState & state) {
  PlaybackInfo info;
  info.is_playing = true;
  info.outSampleRate = state.getOutSampleRate();
  info.sample_interval = state.getSampleInterval(song);
  info.sample_pos = state.getSamplePos();
  info.track_pos = state.getTrackPosition();
  info.pattern_pos = state.getPatternPosition();
  info.absolute_pos = state.getAbsolutePosition();
  info.voice_count = state.getVoiceCount();
  info.allocated_voice_count = state.getAllocatedVoiceCount();
  
  return make_unique<PlaybackEvent>(info);
}

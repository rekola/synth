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
#include "AudioBlockEvent.h"

#include "MixerFactory.h"
#include "InstrumentTrackState.h"

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
	  auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(instrument_track.getInternalId()));

	  if (track_state) {
	    auto [ pattern_idx, row_idx ] = state_.getRelativePosition(song);
	    auto & pattern = song.getPattern(pattern_idx);

	    if (ev.getType() == PlaybackControlEvent::PLAY_NOTE) {
	      auto tuning = track->getType() == TrackType::PERCUSSION_CONTROL ? Tuning::PERCUSSION : song.getTuning();
	      Note note(midi_note, midi_velocity);
	      auto frequency = Tuner::getFrequency(tuning, note);

	      track_state->stopVoices(column);
	      auto voice = instrument.playNote(state_.getChannelConfiguration(), instrument_track.getPosition(), frequency, 1.0f, note.getVelocityAsFloat(), 0.0f, note.getValue(), instrument_track.getSends());
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

  case PlaybackControlEvent::SET_POSITION:
    state_.setPosition(ev.getParameter1());
    break;

  case PlaybackControlEvent::CLEAR_VOICES:
    state_.removeChild(ev.getParameter1());
    break;
    
  case PlaybackControlEvent::STOP_NOTE:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->stopVoices(ev.getParameter2());
    }
    break;

  case PlaybackControlEvent::SET_RECORDING_MUTE:
    state_.setRecordingMuted(ev.getParameter1() != 0);
    break;

  case PlaybackControlEvent::SET_TRACK_MUTED:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setMuted(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SOLO:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSolo(ev.getParameter2() != 0);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_A:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendA(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_B:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendB(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_SEND_MAIN:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setSendMain(ev.getParameter2() / 1000.0f);
    }
    break;

  case PlaybackControlEvent::SET_TRACK_AZIMUTH:
    {
      auto track_state = dynamic_cast<InstrumentTrackState*>(state_.getChildByInternalId(ev.getParameter1()));
      if (track_state) track_state->setAzimuth(ev.getParameter2() / 10.0f);
    }
    break;

  case PlaybackControlEvent::SONG_CHANGED:
    song_changed_ = true;
    break;

  case PlaybackControlEvent::MIXER_CHANGED:
    mixer_changed_ = true;
    break;
  }
}

void
Player::play(AudioAPI & audio) {
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

  auto * song = &controller_->getSong();
  state_.initialize(*song);
  auto mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());

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
	    if (song_changed_) {
	      // current_song was reassigned on the UI thread (new-song/open-
	      // song); this event's pop() (guarded by the same mutex as the
	      // UI thread's push()) is what makes that reassignment safe to
	      // observe here - re-fetching without it would be a data race.
	      song = &controller_->getSong();
	      state_.clear();
	      state_.resetPosition(); // old song's row count means nothing against the new song's patterns
	      state_.initialize(*song);
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      song_changed_ = false;
	    }
	    if (mixer_changed_) {
	      mixer = createMixer(controller_->getChannelConfiguration(), controller_->getMixerType(), controller_->getUseLegacyBinaural());
	      mixer_changed_ = false;
	    }
	    auto ev = createPlaybackEvent(*song, state_);
	    controller_->getUIEventQueue().push(move(ev));
	  } else if (i - 1 < num_playback_desc) {
	    state_.render(audio.getFrameCount(), *song, *mixer);
	    auto master = mixer->encode();
	    audio.play(master, logger);

	    auto ev = createPlaybackEvent(*song, state_);

	    // Raw, pre-mixdown per-channel loudness for the UI's volume meter -
	    // the ambisonic bus (whatever regular channel count is active),
	    // then always AuxA/AuxB last (see SongState::render()'s
	    // aux_a_sum_/aux_b_sum_).
	    auto channel_loudness = mixer->getRawBus().calculateLoudness();

	    // Meter legend - each *character* lines up with one meter *column*
	    // (2 samples/braille-column), so the label reads as an actual
	    // legend for the bars beneath it rather than just a compact tag:
	    // the "A" for AuxA/AuxB is always placed at the exact column
	    // index where the aux channels themselves start (padded with
	    // spaces to get there), never just appended to the end of the
	    // text. There is no plain-stereo config any more
	    // (ChannelConfiguration::STEREO was removed - every config is MONO
	    // or AMBISONIC), so there's no "2 regular channels" case to label
	    // here.
	    //
	    // "M" always marks where the Main (regular/ambisonic) channels
	    // start, "A" where AuxA/AuxB start - never "A" for both meanings
	    // in the same label. mono+aux (1 regular -> padded to 2 -> 1 col,
	    // then aux -> col 1): "M" (mono) + "A" (aux, col 1) = "MA".
	    // Order-1 ambisonic (4 regular -> 2 cols, then aux -> col 2): "M4"
	    // (Main, 4 channels) + "A" (col 2) = "M4A". Order-2 (9 regular,
	    // odd -> padded to 10 -> 5 cols, then aux -> col 5): "M1-9" + " "
	    // (col 4) + "A" (col 5) = "M1-9 A". Order-3 (16 regular, even ->
	    // 8 cols, then aux -> col 8): "M1-16" + 3 spaces (cols 5-7) + "A"
	    // (col 8) = "M1-16   A".
	    switch (channel_loudness.size()) {
	    case 1: ev->setMeterLabel("MA"); break;
	    case 4: ev->setMeterLabel("M4A"); break;
	    case 9: ev->setMeterLabel("M1-9 A"); break;
	    case 16: ev->setMeterLabel("M1-16   A"); break;
	    default: ev->setMeterLabel(""); break;
	    }

	    // Pad to an even count before appending AuxA/AuxB - the braille
	    // meter packs 2 samples per character cell, so the aux channels
	    // only land together in the *same* cell (rather than the last
	    // regular channel pairing with AuxA, leaving AuxB alone) when they
	    // start at an even index. Order-2 ambisonic (9, odd) needs this;
	    // stereo (2) and order-1 ambisonic (4) are already even.
	    if (channel_loudness.size() % 2 == 1) channel_loudness.push_back(0.0f);

	    auto aux_a = state_.getAuxASum().calculateLoudness();
	    auto aux_b = state_.getAuxBSum().calculateLoudness();
	    channel_loudness.insert(channel_loudness.end(), aux_a.begin(), aux_a.end());
	    channel_loudness.insert(channel_loudness.end(), aux_b.begin(), aux_b.end());
	    ev->setChannelLoudness(std::move(channel_loudness));

	    controller_->getUIEventQueue().push(move(ev));

	    // Hand master and the raw bus's first up to 4 channels (W/Y/Z/X,
	    // ACN order) off to VisualizationThread - its own dedicated
	    // thread, decoupled from both this real-time audio thread and the
	    // UI thread (see VisualizationThread.h) - for spectrum-FFT and
	    // DirAC directional analysis respectively; neither belongs on
	    // this thread. master is moved, not copied (it's already this
	    // block's own independently-owned SampleData, straight from
	    // mixer->encode() above); the raw-bus channels genuinely are
	    // copied, since mixer->getRawBus() is a reference into the
	    // mixer's own persistent buffer, overwritten next block.
	    auto & raw_bus = mixer->getRawBus();
	    int dirac_channels = raw_bus.numberOfChannels() < 4 ? raw_bus.numberOfChannels() : 4;
	    SampleData dirac_bus(static_cast<short>(dirac_channels), raw_bus.numberOfFrames());
	    for (int c = 0; c < dirac_channels; c++) {
	      auto src = raw_bus.getChannelData(c);
	      auto dst = dirac_bus.getChannelData(c);
	      for (int i = 0; i < raw_bus.numberOfFrames(); i++) dst[i] = src[i];
	    }
	    controller_->getVisualizationQueue().push(make_unique<AudioBlockEvent>(move(master), move(dirac_bus)));
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
  info.setSampleInterval(state.getChannelConfiguration().getSampleInterval(state.getTempo()));
  info.setSamplePos(state.getSamplePos());
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setAbsolutePos(state.getAbsolutePosition());
  info.setVoiceCount(state.getVoiceCount());
  info.setAllocatedVoiceCount(state.getAllocatedVoiceCount());

  std::unordered_map<int, TrackInfo> effect_info;
  state.getAllTrackInfo(effect_info);
  info.setTrackInfo(move(effect_info));

  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active_voices;
  state.getAllActiveVoices(active_voices);
  info.setActiveVoices(move(active_voices));

  return make_unique<PlaybackEvent>(info);
}

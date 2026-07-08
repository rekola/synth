#include "AlsaAudio.h"

#include "Logger.h"
#include "SampleData.h"

#include <cstdio>
#include <fmt/core.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define PCM_DEVICE "default"

using namespace std;

AlsaAudio::~AlsaAudio() {
  if (pcm_handle) {
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
  }
  if (capture_handle) {
    snd_pcm_close(capture_handle);
  }
}

static size_t initialize_alsa_dev(Logger & logger, snd_pcm_t * handle, int rate, short channels, bool capture) {
  int r;
  
  // Allocate parameters object and fill it with default values
  snd_pcm_hw_params_t * hw_params;
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(handle, hw_params);

  // Set parameters
  if ((r = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    logger.log(string("ERROR: Can't set interleaved mode: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_FLOAT_LE)) < 0) {
    logger.log(string("ERROR: Can't set format: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_channels(handle, hw_params, channels)) < 0) {
    logger.log(string("ERROR: Can't set channels number: ") + snd_strerror(r));
    return 0;
  }

  unsigned int actual_rate = rate;
  if ((r = snd_pcm_hw_params_set_rate_near(handle, hw_params, &actual_rate, 0)) < 0) {
    logger.log(string("ERROR: Can't set rate: ") + snd_strerror(r));
    return 0;
  }

  unsigned int min_periods;
  int dir;
  
  if ((r = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0) {
    logger.log(string("ERROR: Can't get min periods: ") + snd_strerror(r));
    return 0;
  }

  if ((r = snd_pcm_hw_params_set_periods(handle, hw_params, min_periods > 2 ? min_periods : 2, 0)) < 0) {
    logger.log(string("ERROR: Failed to set periods: ") + snd_strerror(r));
    return 0;
  }

  snd_pcm_uframes_t min_period_size;
  if ((r = snd_pcm_hw_params_get_period_size_min(hw_params, &min_period_size, &dir)) < 0) {
    logger.log(string("ERROR: Failed to get minimum period size: ") + snd_strerror(r));    
    return 0;
  }

  int wanted_period = 1024; // 4096;
  if (!capture || 1) {
    if ((r = snd_pcm_hw_params_set_period_size(handle, hw_params, min_period_size > wanted_period ? min_period_size : wanted_period, 0)) < 0) {
      logger.log(string("ERROR: Failed to set period size: ") + snd_strerror(r));
      return 0;
    }
  }
    
  // Write parameters
  if ((r = snd_pcm_hw_params(handle, hw_params)) < 0) {
    logger.log(string("ERROR: Can't set hardware parameters: ") + snd_strerror(r));
    return 0;
  }

#if 1
  snd_pcm_sw_params_t * sw_params;
  snd_pcm_sw_params_alloca(&sw_params);
  snd_pcm_sw_params_current(handle, sw_params);
  snd_pcm_sw_params_set_avail_min(handle, sw_params, wanted_period);
#endif
  // snd_pcm_hw_params_get_period_time(params, &tmp, NULL);
  
  snd_pcm_uframes_t frames;
  snd_pcm_hw_params_get_period_size(hw_params, &frames, 0);
  
  return frames;
}

void
AlsaAudio::initialize(Logger & logger) {
  int r;

  // Open the PCM device in playback mode. Without this, there is nothing
  // useful this class can do, so give up entirely on failure.
  if ((r = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    logger.log(string("ERROR: Can't open PCM device for playback: ") + snd_strerror(r));
    return;
  }

  output_frames = initialize_alsa_dev(logger, pcm_handle, getFrequency(), numberOfChannels(), false);
  if (!output_frames) return;

  // Capture (used for sampling/recording) is optional: a machine without a
  // capture device (or without permission to open one) should still be able
  // to play songs.
  if ((r = snd_pcm_open(&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    logger.log(string("WARNING: Can't open PCM device for capture, recording disabled: ") + snd_strerror(r));
    capture_handle = nullptr;
  } else {
    input_frames = initialize_alsa_dev(logger, capture_handle, getFrequency(), 1, true);
    if (!input_frames) {
      logger.log("WARNING: Can't configure capture device, recording disabled");
      snd_pcm_close(capture_handle);
      capture_handle = nullptr;
    }
  }

#if 0
  unsigned int rate;
  if ((r = snd_pcm_hw_params_get_rate(hw_params, &rate, 0)) < 0) {
    logger.log(string("ERROR: Failed to get rate: ") + snd_strerror(r));
    return;
  }

  if (rate != getFrequency()) {
    logger.log("Changing frequency to " + to_string(rate));
    setFrequency(rate);
  }

  unsigned int channels;
  if ((r = snd_pcm_hw_params_get_channels(hw_params, &channels)) < 0) {
    logger.log(string("ERROR: Failed to get channels: ") + snd_strerror(r));
    return;
  }
#endif

  if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    logger.log("Error opening ALSA sequencer");
    return;
  }
  snd_seq_set_client_name(seq_handle, "musiceditor");
  if (snd_seq_create_simple_port(seq_handle, "musiceditor",
				 SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
				 SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
    logger.log("Error creating sequencer port");
    exit(1);
  }

  auto status = string("Playback: name = ") + string(snd_pcm_name(pcm_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(pcm_handle)));
  if (capture_handle) {
    status += string(" Capture: name = ") + string(snd_pcm_name(capture_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(capture_handle)));
  }
  logger.log(status);

  setPlaybackDescriptors(getPollDescriptors(pcm_handle));
  if (capture_handle) setCaptureDescriptors(getPollDescriptors(capture_handle));
  setMidiCaptureDescriptors(getMidiPollDescriptors(seq_handle));
}

std::vector<pollfd>
AlsaAudio::getPollDescriptors(snd_pcm_t * handle) {
  size_t nfds = snd_pcm_poll_descriptors_count(handle);
  struct pollfd * pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (nfds + 1));
    
  if (snd_pcm_poll_descriptors(handle, pfds, nfds) < 0) {
    fmt::print(stderr, "Error getting descriptor\n");
    exit(1);
  }

  vector<pollfd> r;
  for (size_t i = 0; i < nfds; i++) {
    r.push_back(pfds[i]);
  }
  return r;
}

std::vector<pollfd>
AlsaAudio::getMidiPollDescriptors(snd_seq_t * handle) {
  size_t nfds = snd_seq_poll_descriptors_count(handle, POLLIN);

  struct pollfd * pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (nfds + 1));

  if (snd_seq_poll_descriptors(handle, pfds, nfds, POLLIN) < 0) {
    fmt::print(stderr, "Error getting descriptor\n");
    exit(1);
  }

  vector<pollfd> r;
  for (size_t i = 0; i < nfds; i++) {
    r.push_back(pfds[i]);
  }
  return r;
}

void
AlsaAudio::play(const SampleData & data, Logger & logger) {
  auto numChannels = data.numberOfChannels();
  auto tmp_data = unique_ptr<float[]>(new float[data.size() * numChannels]);
  auto tmp_ptr = tmp_data.get();

  for (int j = 0; j < numChannels; j++) {
    auto channel_data = data.getChannelData(j);
    for (int i = 0; i < data.size(); i++) {
      tmp_ptr[i * numChannels + j] = channel_data[i];
    }
  }

  int r;
  if ((r = snd_pcm_writei(pcm_handle, tmp_ptr, data.size())) == -EPIPE) {
    logger.log("XRUN.");
    snd_pcm_prepare(pcm_handle);
  } else if (r < 0) {
    logger.log(string("ERROR. Can't write to PCM device. ") + snd_strerror(r));
  }
}

SampleData
AlsaAudio::record(Logger & logger) {
  if (!capture_handle) return SampleData();

  startRecording();

  auto frames = snd_pcm_avail_update(capture_handle);
  SampleData data(1, frames);

  if (frames) {
    int r;
    if ((r = snd_pcm_readi(capture_handle, data.getChannelData(0), frames)) == -EPIPE) {
      logger.log("XRUN.(2)");
      snd_pcm_prepare(capture_handle);
    } else if (r < 0) {
      logger.log(string("ERROR. Can't read PCM device. ") + snd_strerror(r));
    }
  }

  return data;
}

void
AlsaAudio::startRecording() {
  if (!capture_handle) return;

  if (!recording_started) {
    int r;
    if ((r = snd_pcm_start(capture_handle)) < 0) {
      // logger.log(string("ERROR. Failed to start recording: ") + snd_strerror(r));
      // return SampleData();
      exit(1);
    }
    recording_started = true;
  }
}

void
AlsaAudio::stopRecording() {
  
}

vector<MidiEvent>
AlsaAudio::recordMIDI() {
  vector<MidiEvent> r;

  do {
    snd_seq_event_t *ev;
    snd_seq_event_input(seq_handle, &ev);

    switch (ev->type) {
    case SND_SEQ_EVENT_SYSTEM:
      break;
    case SND_SEQ_EVENT_RESULT:
      break;
    case SND_SEQ_EVENT_KEYPRESS:
      // cerr << "aftertouch: note " << ev->data.note.note << ", vel = " << ev->data.note.velocity << endl;
      r.push_back(MidiEvent(MidiEvent::NOTE_PRESSURE, ev->data.note.note, ev->data.note.velocity));
      break;
    case SND_SEQ_EVENT_CHANPRESS:
      break;
    case SND_SEQ_EVENT_PITCHBEND:
#if 0
      pitch = (double)ev->data.control.value / 8192.0;
#endif
      break;
    case SND_SEQ_EVENT_CONTROLLER:
#if 0
      if (ev->data.control.param == 1) {
	modulation = (double)ev->data.control.value / 10.0;
      }
#endif
      break;
    case SND_SEQ_EVENT_NOTEON:
      r.push_back(MidiEvent(MidiEvent::NOTE_ON, ev->data.note.note, ev->data.note.velocity));
      break;        
    case SND_SEQ_EVENT_NOTEOFF:
      r.push_back(MidiEvent(MidiEvent::NOTE_OFF, ev->data.note.note, 0));
      break;
    }

    snd_seq_free_event(ev);
  } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
  
  return r;
}

#include "AlsaAudio.h"

#include "UIBase.h"
#include "SampleData.h"

#include <stdio.h>

#include <iostream>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define PCM_DEVICE "default"

using namespace std;

AlsaAudio::~AlsaAudio() {
  if (pcm_handle) {
    snd_pcm_drain(pcm_handle);
    snd_pcm_close(pcm_handle);
  }
}

void
AlsaAudio::initialize(UIBase & ui) {
  unsigned int rate = getFrequency();
  int r;

  // Open the PCM device in playback mode
  if ((r = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    ui.setStatus(string("ERROR: Can't open PCM device: ") + snd_strerror(r));
    return;
  }

  // Allocate parameters object and fill it with default values
  snd_pcm_hw_params_t * hw_params;
  snd_pcm_hw_params_alloca(&hw_params);
  snd_pcm_hw_params_any(pcm_handle, hw_params);

  // Set parameters
  if ((r = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    ui.setStatus(string("ERROR: Can't set interleaved mode: ") + snd_strerror(r));
    return;
  }

  if ((r = snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_FLOAT_LE)) < 0) {
    ui.setStatus(string("ERROR: Can't set format: ") + snd_strerror(r));
    return;
  }

  if ((r = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, getChannels())) < 0) {
    ui.setStatus(string("ERROR: Can't set channels number: ") + snd_strerror(r));
    return;
  }

  if ((r = snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0)) < 0) {
    ui.setStatus(string("ERROR: Can't set rate: ") + snd_strerror(r));
    return;
  }

  if (rate != getFrequency()) {
    ui.setStatus("Changing frequency to " + to_string(rate));
    setFrequency(rate);
  }

  unsigned int min_periods;
  int dir;
  
  if ((r = snd_pcm_hw_params_get_periods_min(hw_params, &min_periods, &dir)) < 0) {
    ui.setStatus(string("ERROR: Can't get min periods: ") + snd_strerror(r));
    return;
  }

  if ((r = snd_pcm_hw_params_set_periods(pcm_handle, hw_params, min_periods > 2 ? min_periods : 2, 0)) < 0) {
    ui.setStatus(string("ERROR: Failed to set periods: ") + snd_strerror(r));
    return;
  }

  snd_pcm_uframes_t min_period_size;
  if ((r = snd_pcm_hw_params_get_period_size_min(hw_params, &min_period_size, &dir)) < 0) {
    ui.setStatus(string("ERROR: Failed to get minimum period size: ") + snd_strerror(r));    
    return;
  }

  int wanted_period = 4096;
  
  if ((r = snd_pcm_hw_params_set_period_size(pcm_handle, hw_params, min_period_size > wanted_period ? min_period_size : wanted_period, 0)) < 0) {
    ui.setStatus(string("ERROR: Failed to set period size: ") + snd_strerror(r));
    return;
  }

  // Write parameters
  if ((r = snd_pcm_hw_params(pcm_handle, hw_params)) < 0) {
    ui.setStatus(string("ERROR: Can't set hardware parameters: ") + snd_strerror(r));
    return;
  }

  unsigned int channels;
  if ((r = snd_pcm_hw_params_get_channels(hw_params, &channels)) < 0) {
    ui.setStatus(string("ERROR: Failed to get channels: ") + snd_strerror(r));
    return;
  }

  ui.setStatus(string("PCM: name = ") + string(snd_pcm_name(pcm_handle)) + string(", state = ") + string(snd_pcm_state_name(snd_pcm_state(pcm_handle))) + string(", channels = ") + to_string(channels));
  
  // snd_pcm_hw_params_get_rate(hw_params, &tmp, 0);
  // printf("rate: %d bps\n", tmp);

  snd_pcm_sw_params_t * sw_params;
  snd_pcm_sw_params_alloca(&sw_params);
  snd_pcm_sw_params_current(pcm_handle, sw_params);
  snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, wanted_period);
  
  snd_pcm_uframes_t frames;
  snd_pcm_hw_params_get_period_size(hw_params, &frames, 0);

  buffer_size = frames * getChannels() * sizeof(float);

  // snd_pcm_hw_params_get_period_time(params, &tmp, NULL);

  size_t nfds = snd_pcm_poll_descriptors_count(pcm_handle);
  struct pollfd * pfds = (struct pollfd *)alloca(sizeof(struct pollfd) * (nfds + 1));
    
  if (snd_pcm_poll_descriptors(pcm_handle, pfds, nfds) < 0) {
    cerr << "Error getting descriptor\n";
    exit(1);
  }

  for (size_t i = 0; i < nfds; i++) {
    addPollDescriptor(pfds[i]);
  }
}

void
AlsaAudio::play(SampleData & data, UIBase & ui) {
  int r;
  if ((r = snd_pcm_writei(pcm_handle, data.data(), data.size())) == -EPIPE) {
    ui.setStatus("XRUN.");
    snd_pcm_prepare(pcm_handle);
  } else if (r < 0) {
    ui.setStatus(string("ERROR. Can't write to PCM device. ") + snd_strerror(r));
  }
}

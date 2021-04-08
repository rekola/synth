#include "AlsaAudio.h"

#include "Synth.h"

#include <alsa/asoundlib.h>
#include <stdio.h>

#include <iostream>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define PCM_DEVICE "default"

using namespace std;

void
AlsaAudio::start(Synth & synth) {
  unsigned int pcm, tmp, dir;
  snd_pcm_t *pcm_handle;
  snd_pcm_hw_params_t *params;
  snd_pcm_uframes_t frames;

  unsigned int rate = getFrequency();

  /* Open the PCM device in playback mode */
  if ((pcm = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    printf("ERROR: Can't open \"%s\" PCM device. %s\n", PCM_DEVICE, snd_strerror(pcm));
  }

  /* Allocate parameters object and fill it with default values*/
  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(pcm_handle, params);

  /* Set parameters */
  if ((pcm = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    printf("ERROR: Can't set interleaved mode. %s\n", snd_strerror(pcm));
  }

  if ((pcm = snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_FLOAT_LE)) < 0) {
    printf("ERROR: Can't set format. %s\n", snd_strerror(pcm));
  }

  if ((pcm = snd_pcm_hw_params_set_channels(pcm_handle, params, getChannels())) < 0) {
    printf("ERROR: Can't set channels number. %s\n", snd_strerror(pcm));
  }

  if ((pcm = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0)) < 0) {
    printf("ERROR: Can't set rate. %s\n", snd_strerror(pcm));
  }

  if (rate != getFrequency()) {
    cerr << "changing frequency to " << rate << endl;
    setFrequency(rate);
  }

  /* Write parameters */
  if ((pcm = snd_pcm_hw_params(pcm_handle, params)) < 0) {
    printf("ERROR: Can't set hardware parameters. %s\n", snd_strerror(pcm));
  }

  /* Resume information */
  printf("PCM name: '%s'\n", snd_pcm_name(pcm_handle));
  printf("PCM state: %s\n", snd_pcm_state_name(snd_pcm_state(pcm_handle)));

  snd_pcm_hw_params_get_channels(params, &tmp);
  printf("channels: %i ", tmp);

  if (tmp == 1) {
    printf("(mono)\n");
  } else if (tmp == 2) {
    printf("(stereo)\n");
  }

  snd_pcm_hw_params_get_rate(params, &tmp, 0);
  printf("rate: %d bps\n", tmp);

  /* Allocate buffer to hold single period */
  snd_pcm_hw_params_get_period_size(params, &frames, 0);

  size_t buff_size = frames * getChannels() * sizeof(float);
  float * buff = (float *)malloc(buff_size);
  
  snd_pcm_hw_params_get_period_time(params, &tmp, NULL);

  while ( 1 ) {
    synth.play(buff, frames);
    
    if ((pcm = snd_pcm_writei(pcm_handle, buff, frames)) == -EPIPE) {
      printf("XRUN.\n");
      snd_pcm_prepare(pcm_handle);
    } else if (pcm < 0) {
      printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(pcm));
    }
  }
  
  snd_pcm_drain(pcm_handle);
  snd_pcm_close(pcm_handle);
  free(buff);
}

#ifndef _SYNTH_H_

#include "Track.h"
#include "Channel.h"
#include "Instrument.h"

#include <memory>

#define MIDINOTES 128
// #define MAXOUTBUF 44100

class SampleData;

class Synth {
 public:
  Synth(int samplerate, unsigned char *track);
  
  SampleData play(size_t frames);
  float gettime() const {
    return (float)samplepos / srate;
  }

  bool togglePlayback() {
    is_playing = !is_playing;
    return is_playing;
  }
  bool isPlaying() const { return is_playing; }

protected:

private:
  bool is_playing = true;
  
  float freqtab[MIDINOTES];
  
  float mastervol = 1.0;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  float fscaler = 1.0;

  unsigned char bpm, trkcnt;
  int sinterval, samplepos = 0, ptrnpos = 0, srate, loops = 0;
  size_t trkpos = 0, trkmaxlen = 0;
  
  // global delay parameters
  int delay1, delay2;
  float fd1, fd2, delaymix1, delaymix2;

  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Track> trk;
  std::vector<Channel> patt;
};

 #endif
 

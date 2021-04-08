#ifndef _SYNTH_H_

#include "Track.h"
#include "Pattern.h"
#include "Instrument.h"

#include <memory>

#define NOTEDOMAIN (float)1/4

#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f
#define MIDINOTES 128
#define MAXOUTBUF 44100

#define MAXPATT 50

class Synth {
 public:
  Synth(int samplerate, unsigned char *track);
  
  void play(float * out, size_t frames);
  float gettime() const {
    return (float)samplepos / srate;
  }

protected:

private:
  float freqtab[MIDINOTES];
  
  float mastervol;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  float fscaler;

  unsigned char bpm, trkcnt;
  int sinterval, samplepos = 0, ptrnpos = 0, trkpos = 0, srate, loops = 0;
  size_t trkmaxlen = 0;
  
  // global delay parameters
  int delay1, delay2;
  float fd1, fd2, delaymix1, delaymix2;

  std::vector<std::unique_ptr<Instrument> > instruments;
  std::vector<Track> trk;
  std::vector<Pattern> patt;
};

 #endif
 

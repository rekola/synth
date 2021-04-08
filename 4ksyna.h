#ifndef _SYNTH_H_

#include "Track.h"
#include "Pattern.h"

#define NOTEDOMAIN (float)1/4

#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f
#define WAVESIZE 1024
#define MIDINOTES 128
#define MAXOUTBUF 44100

#define MAXPATT 50

#define SINE 0
#define SAW 1
#define SQUARE 2
#define NOISE 3 // metallic noise
#define NOISE2 4 // real noise

class Synth {
 public:
  Synth(int samplerate, unsigned char *track);
  
  short play(short *out, int len);
  float gettime() const {
    return (float)samplepos / srate;
  }

protected:

private:
  float waves[4][WAVESIZE], freqtab[MIDINOTES];
  float bufl[MAXOUTBUF], bufr[MAXOUTBUF];
  
  float mastervol;
  float gvol = 1.0; // (or 1.0 / trkcnt)
  float fscaler;

  unsigned char bpm, trkcnt;
  int sinterval, samplepos = 0, ptrnpos = 0, trkpos = 0, srate, loops = 0;
  size_t trkmaxlen = 0;
  
  // global delay parameters
  int delay1, delay2;
  float fd1, fd2, delaymix1, delaymix2;

  std::vector<Track> trk;
  std::vector<Pattern> patt;
};

 #endif
 

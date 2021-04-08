/*
  TODO:
  -modulation
  -note slide
  -better exponential(?) ADSR
  -optimization
*/

#include "Synth.h"
#include "track.h"
#include "SDLAudio.h"
#include "AlsaAudio.h"

#include <cmath>
#include <iostream>
#include <cassert>

using namespace std;

Synth::Synth(int samplerate, unsigned char *track) {
  cerr << "initializing synth\n";
  
  fscaler = (float)WAVESIZE / samplerate;
  float k = 1.059463094359f;	// 12th root of 2
  float a = 8.1757989156f;	// C

  for (int i = 0; i < MIDINOTES; i++) {
    freqtab[i] = (float)a;
    a *= k;
  }

  for (int i = 0; i < WAVESIZE; i++) {
    waves[int(WaveformType::SINE)][i] = sinf(i * 2.0 * M_PI / (float)WAVESIZE);
    waves[int(WaveformType::SAW)][i] = -1.0 + fmod(1.0 + 2.0 * i / (float)WAVESIZE, 2.0);
    waves[int(WaveformType::SQUARE)][i] = (i < WAVESIZE / 2) ? -1.0 : 1.0;
    waves[int(WaveformType::NOISE)][i] = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
  }

  bpm = *track++;
  mastervol = (float)(*track++) / 127;
  delay1 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  delay2 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  fd1 = (float)(*track++) / 255;
  fd2 = (float)(*track++) / 255;
  delaymix1 = (float)(*track++) / 255;
  delaymix2 = (float)(*track++) / 255;

  int ptrncnt = *track++;
  for (int i = 0; i < ptrncnt; i++) {
    Pattern pattern;
    pattern.instrument.type = (WaveformType)*track++;
    pattern.a = (*track++) * 44100 * 5 / 255;
    pattern.d = (*track++) * 44100 * 5 / 255;
    pattern.s = (float)(*track++) / 255;
    pattern.r = (*track++) * 44100 * 5 / 255;
    pattern.vol = (float)(*track++) / 128;
    pattern.flags = *track++;
    pattern.detune = (float)((*track++) - 127) / 512;
    pattern.pan = (float)(*track++) / 255;
    pattern.fcut = (float)(*track++) / 255;
    pattern.fres = (float)(*track++) / 63;

    while (1) {
      unsigned char val = *track++;
      if (val == 255) break;
      pattern.addNote(val);
    }

    patt.push_back(pattern);
  }

  int trkcnt = *track++;
  for (int i = 0; i < trkcnt; i++) {
    Track t;
    while (1) {
      unsigned char val = *track++;
      if (val == 255) break;
      
      t.addPattern(val);
    }
    if (t.size() > trkmaxlen) trkmaxlen = t.size();
    
    trk.push_back(t);
  }

  float tnote = (float)60 / bpm * NOTEDOMAIN * 2;
  sinterval = (int)(tnote * samplerate);
  srate = samplerate;
}

short
Synth::play(short *out, int len) {
  len >>= 1;

  long mask = WAVESIZE - 1;

  for (int i = 0; i < len; i++) {
    bufl[i] = bufr[i] = 0;
    int chk = 0;
    if (samplepos % sinterval == 0) chk = 1;
    if (chk) {
      for (int k = 0; k < trk.size(); k++) {
	int j = trk[k].getPattern(trkpos);
	if (j == 255) continue;

	assert(j >= 0 && j < patt.size());
	auto & pattern = patt[j];
	unsigned char note_data = pattern.getNote(ptrnpos);
	pattern.playNote(note_data, freqtab, fscaler);	
      }
    }
    
    for (int k = 0; k < trk.size(); k++) {
      int j = trk[k].getPattern(trkpos);
      if (j == 255) continue;

      assert(j >= 0 && j < patt.size());
      auto & pattern = patt[j];

      float adsrvol = pattern.updateADSR();      

      float ss;
      if (pattern.instrument.type == WaveformType::NOISE2) {
	ss = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
      } else {
	ss = waves[int(pattern.instrument.type)][(long)pattern.fphase & mask];
      }

      ss = pattern.filtersample(ss);

      ss *= pattern.vol * adsrvol * gvol;
      if (pattern.acc) ss *= ACCENTAMT;
      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      float ssl = ss * sqrtf(1.0 - pattern.pan);
      float ssr = ss * sqrtf(pattern.pan);

      if (pattern.flags & DELAYTRACK) pattern.delaysample(delaymix1, delaymix2, fd1, delay1, fd2, delay2, &ssl, &ssr);

      bufl[i] += ssl;
      bufr[i] += ssr;

      pattern.fphase += pattern.freq;
      if (pattern.fphase > mask) pattern.fphase = 0;
    }

    if (chk) {
      ptrnpos++;
      if (ptrnpos >= PATTLEN) {
	ptrnpos = 0;
	trkpos++;
	if (trkpos >= trkmaxlen - 1) {
	  trkpos = 0;
	  // samplepos = 0;
	  loops++;
	}
      }
    }
    samplepos++;
  }

  for (int i = 0; i < len; i += 2) {
    float ls = bufl[i] * mastervol * VOLGAIN;
    float rs = bufr[i] * mastervol * VOLGAIN;

    if (ls > 1.0) ls = 1.0;
    else if (ls < -1.0) ls = -1.0;
    if (rs > 1.0) rs = 1.0;
    else if (rs < -1.0) rs = -1.0;

    out[i] = (short)(ls * 32000);
    out[i + 1] = (short)(rs * 32000);
  }

  return 1;
}

int main(int argc, char *argv[]) {
#if 0
  SDLAudio audio(44100, 2);
#else
  AlsaAudio audio(44100, 2);
#endif
    
  Synth synth(audio.getFrequency(), tr);
  audio.start(synth);
  
  return 0;
}

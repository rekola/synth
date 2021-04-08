#include "Synth.h"

#include "BasicInstrument.h"

#include <cmath>
#include <iostream>
#include <cassert>

#define NOTEDOMAIN (float)1/4
#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f

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
    int instrument_id = (int)instruments.size();
    
    WaveformType type = (WaveformType)*track++;
    int a = (*track++) * 44100 * 5 / 255;
    int d = (*track++) * 44100 * 5 / 255;
    float s = (float)(*track++) / 255;
    int r = (*track++) * 44100 * 5 / 255;
    float vol = (float)(*track++) / 128;
    unsigned char flags = *track++;
    float detune = (float)((*track++) - 127) / 512;
    float pan = (float)(*track++) / 255;
    float fcut = (float)(*track++) / 255;
    float fres = (float)(*track++) / 63;

    auto instrument = make_unique<BasicInstrument>(type);
    instrument->setADSR(a, d, s,r);
    instrument->setVolume(vol);
    instrument->setDetune(detune);
    instrument->setPan(pan);
    instrument->setFilter(fcut, fres);
    instrument->setFlags(flags);
    instruments.push_back(move(instrument));
    						   
    Channel pattern;
    pattern.instrument_id = instrument_id;
    
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

void
Synth::play(float * out, size_t frames) {
  for (int i = 0; i < frames; i++) {
    float left = 0, right = 0;
    
    int chk = 0;
    if (samplepos % sinterval == 0) chk = 1;
    if (chk) {
      for (int k = 0; k < trk.size(); k++) {
	int j = trk[k].getPattern(trkpos);
	if (j == 255) continue;

	assert(j >= 0 && j < patt.size());
	auto & pattern = patt[j];
	auto & instrument = instruments[pattern.instrument_id];
	unsigned char note_data = pattern.getNote(ptrnpos);
	pattern.playNote(note_data, freqtab, fscaler, instrument->getDetune());
      }
    }
    
    for (int k = 0; k < trk.size(); k++) {
      int j = trk[k].getPattern(trkpos);
      if (j == 255) continue;

      assert(j >= 0 && j < patt.size());
      auto & pattern = patt[j];
      
      
      auto & instrument = instruments[pattern.instrument_id];
      float adsrvol = pattern.updateADSR(*instrument);
      float ss = instrument->getSample(pattern.fphase);
      
      ss = pattern.filtersample(ss, *instrument);

      ss *= instrument->getVolume() * adsrvol * gvol;
      if (pattern.acc) ss *= ACCENTAMT;
      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      float ssl = ss * sqrtf(1.0 - instrument->getPan());
      float ssr = ss * sqrtf(instrument->getPan());

      if (instrument->getFlags() & DELAYTRACK) pattern.delaysample(delaymix1, delaymix2, fd1, delay1, fd2, delay2, &ssl, &ssr);

      left += ssl;
      right += ssr;

      pattern.fphase += pattern.freq;
      // if (pattern.fphase > mask) pattern.fphase = 0;
    }

    left *= mastervol * VOLGAIN;
    right *= mastervol * VOLGAIN;

    if (left > 1.0) left = 1.0;
    else if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    else if (right < -1.0) right = -1.0;

    out[2 * i] = left;
    out[2 * i + 1] = right;

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
}

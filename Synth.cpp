#include "Synth.h"

#include "BasicInstrument.h"
#include "FileInstrument.h"
#include "SampleData.h"

#include <cmath>
#include <cassert>

#define NOTEDOMAIN (float)1/4
#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f

using namespace std;

Synth::Synth(int samplerate, unsigned char *track) {
  
  // string drone
  auto i0 = make_unique<BasicInstrument>(WaveformType::SAW);
  i0->setADSR(255, 64, 63, 0);
  i0->setVolume(20);
  i0->setFlags(HPFILTER);
  i0->setDetune(127);
  i0->setPan(10);
  i0->setFilter(0, 5);
  song.addInstrument(move(i0));

  // string drone
  auto i1 = make_unique<BasicInstrument>(WaveformType::SAW);
  i1->setADSR(255, 64, 63, 0);
  i1->setVolume(20);
  i1->setFlags(HPFILTER);
  i1->setDetune(120);
  i1->setPan(127);
  i1->setFilter(0, 5);
  song.addInstrument(move(i1));

  // string drone
  auto i2 = make_unique<BasicInstrument>(WaveformType::SAW);
  i2->setADSR(255, 64, 63, 0);
  i2->setVolume(20);
  i2->setFlags(HPFILTER);
  i2->setDetune(134);
  i2->setPan(247);
  i2->setFilter(0, 5);
  song.addInstrument(move(i2));

  // bass drum 
  auto i3 = make_unique<BasicInstrument>(WaveformType::SINE);
  i3->setADSR(0, 15, 0, 0);
  i3->setVolume(200);
  i3->setDetune(130);
  i3->setPan(127);
  i3->setFilter(255, 0);
  song.addInstrument(move(i3));

  // hihat (closed)
  auto i4 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i4->setADSR(0, 8, 0, 0);
  i4->setVolume(64);
  i4->setFlags(DELAYTRACK);
  i4->setDetune(127);
  i4->setPan(127);
  i4->setFilter(190, 128);
  song.addInstrument(move(i4));

  // hihat (open)
  auto i5 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i5->setADSR(0, 13, 0, 0);
  i5->setVolume(30);
  i5->setDetune(127);
  i5->setPan(127);
  i5->setFilter(255, 0);
  song.addInstrument(move(i5));

  // unused
  auto i6 = make_unique<BasicInstrument>(WaveformType::SAW);
  i6->setADSR(0, 25, 0, 0);
  i6->setVolume(128);
  i6->setDetune(127);
  i6->setPan(127);
  i6->setFilter(255, 0);
  song.addInstrument(move(i6));

  // bass
  auto i7 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i7->setADSR(0, 15, 0, 0);
  i7->setVolume(40);
  i7->setDetune(125);
  i7->setPan(64);
  i7->setFilter(200, 20);
  song.addInstrument(move(i7));

  // hihat (closed)
#if 0
  auto i8 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i8->setADSR(0, 3, 0, 0);
  i8->setVolume(63);
  i8->setDetune(127);
  i8->setPan(217);
  i8->setFilter(255, 0);  
#else
  auto i8 = make_unique<FileInstrument>("./samples/Closed-Hi-Hat-1.wav");
  // i8->setADSR(0, 3, 0, 0);
  // i8->setVolume(63);
  // i8->setDetune(127);
  i8->setPan(217);
  // i8->setFilter(255, 0);  
#endif
  song.addInstrument(move(i8));
  
  // snare like
  auto i9 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i9->setADSR(0, 15, 0, 0);
  i9->setVolume(40);
  i9->setFlags(DELAYTRACK);
  i9->setDetune(127);
  i9->setPan(127);
  i9->setFilter(255, 0);
  song.addInstrument(move(i9));

  // bass
  auto i10 = make_unique<BasicInstrument>(WaveformType::SAW);
  i10->setADSR(0, 30, 0, 0);
  i10->setVolume(60);
  i10->setFlags(DELAYTRACK);
  i10->setDetune(127);
  i10->setPan(127);
  i10->setFilter(100, 0);
  song.addInstrument(move(i10));

  // bass
  auto i11 = make_unique<BasicInstrument>(WaveformType::SAW);
  i11->setADSR(0, 20, 0, 0);
  i11->setVolume(128);
  i11->setFlags(DELAYTRACK);
  i11->setDetune(127);
  i11->setPan(128);
  i11->setFilter(63, 128);
  song.addInstrument(move(i11));

  // bass
  auto i12 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i12->setADSR(0, 14, 0, 0);
  i12->setVolume(40);
  i12->setDetune(129);
  i12->setPan(190);
  i12->setFilter(200, 20);
  song.addInstrument(move(i12));

  // bass drum
  auto i13 = make_unique<BasicInstrument>(WaveformType::SINE);
  i13->setADSR(0, 8, 0, 0);
  i13->setVolume(200);
  i13->setDetune(127);
  i13->setPan(127);
  i13->setFilter(244, 0);
  song.addInstrument(move(i13));

  // snare like
  auto i14 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i14->setADSR(0, 5, 0, 0);
  i14->setVolume(240);
  i14->setDetune(127);
  i14->setPan(37);
  i14->setFilter(150, 255);
  song.addInstrument(move(i14));

  song.bpm = *track++;
  song.mastervol = (float)(*track++) / 127;
  
  delay1 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  delay2 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  fd1 = (float)(*track++) / 255;
  fd2 = (float)(*track++) / 255;
  delaymix1 = (float)(*track++) / 255;
  delaymix2 = (float)(*track++) / 255;
  
  int ptrncnt = *track++;
  
  for (int i = 0; i < ptrncnt; i++) {
    Sequence sequence;
    sequence.setInstrumentId(*track++);
    
    while (1) {
      int val = *track++;
      if (val == 255) break;
      sequence.addNote(val);
    }
    
    song.addSequence(sequence);
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
    
    song.addTrack(t);
  }

  float tnote = (float)60 / song.bpm * NOTEDOMAIN * 2;
  sinterval = (int)(tnote * samplerate);
  srate = samplerate;
}

SampleData
Synth::play(size_t frames) {
  SampleData data(frames);
  float * out = data.data();
    
  int solo_instrument = -1;
  for (size_t i = 0; i < song.getInstruments().size(); i++) {
    if (song.getInstrument(i).getSolo()) solo_instrument = i;
  }
  
  for (int i = 0; i < frames; i++) {
    float left = 0, right = 0;
    
    bool chk = false;
    if (is_playing) {
      if (samplepos % sinterval == 0) chk = 1;
      if (chk) {
	for (int k = 0; k < song.getTracks().size(); k++) {
	  int j = song.getTracks()[k].getPattern(trkpos);
	  if (j == 255) continue;
	  
	  assert(j >= 0 && j < song.getSequences().size());
	  auto & pattern = song.getSequences()[j];
	  auto & instrument = song.getInstrument(pattern.getInstrumentId());
	  instrument.playNote(pattern.getNote(ptrnpos));	
	}
      }
      samplepos++;
    }
    
    for (int k = 0; k < song.getTracks().size(); k++) {
      int j = song.getTracks()[k].getPattern(trkpos);
      if (j == 255) continue;

      assert(j >= 0 && j < song.getSequences().size());
      auto & pattern = song.getSequences()[j];
            
      auto & instrument = song.getInstrument(pattern.getInstrumentId());
      float adsrvol = instrument.updateADSR();
      float ss = instrument.getSample();
      
      ss = instrument.filtersample(ss);

      ss *= instrument.getVolume() * adsrvol * song.gvol;
      if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;
      
      if (instrument.acc) ss *= ACCENTAMT;
      
      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      float ssl = ss * sqrtf(1.0 - instrument.getPan());
      float ssr = ss * sqrtf(instrument.getPan());

      if (instrument.getFlags() & DELAYTRACK) instrument.delaysample(delaymix1, delaymix2, fd1, delay1, fd2, delay2, &ssl, &ssr);

      left += ssl;
      right += ssr;

      instrument.fphase += instrument.freq;
    }

    left *= song.mastervol * VOLGAIN;
    right *= song.mastervol * VOLGAIN;

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
	}
      }
    }
  }

  return data;
}

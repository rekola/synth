#include "Controller.h"

#include "Song.h"
#include "BasicInstrument.h"
#include "FMInstrument.h"
#include "FileInstrument.h"

#include "Distortion.h"

#include "track.h"

#include <cassert>
#include <iostream>

using namespace std;

Controller::Controller() {
  auto song = make_shared<Song>();
  
  auto i0 = make_unique<BasicInstrument>(WaveformType::SAW);
  i0->setName("drone1");
  i0->setADSR(255, 64, 63, 0);
  i0->setVolume(20);
  i0->setPan(10);
  i0->setFilter(0, 5, true);
  song->addInstrument(move(i0));

  auto i1 = make_unique<BasicInstrument>(WaveformType::SAW);
  i1->setName("drone2");
  i1->setADSR(255, 64, 63, 0);
  i1->setVolume(20);
  i1->setDetune(120);
  i1->setFilter(0, 5, true);
  song->addInstrument(move(i1));

  auto i2 = make_unique<BasicInstrument>(WaveformType::SAW);
  i2->setName("drone3");
  i2->setADSR(255, 64, 63, 0);
  i2->setVolume(20);
  i2->setDetune(134);
  i2->setPan(247);
  i2->setFilter(0, 5, true);
  song->addInstrument(move(i2));

  auto i3 = make_unique<BasicInstrument>(WaveformType::SINE);
  i3->setName("bass drum");
  i3->setADSR(0, 15, 0, 0);
  i3->setVolume(200);
  i3->setDetune(130);
  i3->setFilter(255, 0);
  song->addInstrument(move(i3));

  auto i4 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i4->setName("hihat closed");
  i4->setADSR(0, 8, 0, 0);
  i4->setVolume(64);
  i4->setFlags(DELAYTRACK);
  i4->setFilter(190, 128);
  song->addInstrument(move(i4));

  auto i5 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i5->setName("hihat open");
  i5->setADSR(0, 13, 0, 0);
  i5->setVolume(30);
  i5->setFilter(255, 0);
  song->addInstrument(move(i5));

  auto i6 = make_unique<BasicInstrument>(WaveformType::SAW);
  i6->setName("unused");
  i6->setADSR(0, 25, 0, 0);
  i6->setVolume(128);
  i6->setFilter(255, 0);
  song->addInstrument(move(i6));

  auto i7 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i7->setName("bass");
  i7->setADSR(0, 15, 0, 0);
  i7->setVolume(40);
  i7->setDetune(125);
  i7->setPan(64);
  i7->setFilter(200, 20);
  song->addInstrument(move(i7));

#if 0
  auto i8 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i8->setName("hihat closed");
  i8->setADSR(0, 3, 0, 0);
  i8->setVolume(63);
  i8->setPan(217);
  i8->setFilter(255, 0);  
#else
  auto i8 = make_unique<FileInstrument>("./samples/Closed-Hi-Hat-1.wav");
  i8->setName("hihat closed");
  // i8->setADSR(0, 3, 0, 0);
  // i8->setVolume(63);
  i8->setPan(217);
  // i8->setFilter(255, 0);  
#endif
  song->addInstrument(move(i8));
  
  auto i9 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i9->setName("snare");
  i9->setADSR(0, 15, 0, 0);
  i9->setVolume(40);
  i9->setFlags(DELAYTRACK);
  i9->setFilter(255, 0);
  song->addInstrument(move(i9));

  auto i10 = make_unique<BasicInstrument>(WaveformType::SAW);
  i10->setName("bass");
  i10->setADSR(0, 30, 0, 0);
  i10->setVolume(60);
  i10->setFlags(DELAYTRACK);
  i10->setFilter(100, 0);
  song->addInstrument(move(i10));

  auto i11 = make_unique<BasicInstrument>(WaveformType::SAW);
  i11->setName("bass");
  i11->setADSR(0, 20, 0, 0);
  i11->setVolume(128);
  i11->setFlags(DELAYTRACK);
  i11->setFilter(63, 128);
  song->addInstrument(move(i11));

  auto i12 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i12->setName("bass");
  i12->setADSR(0, 14, 0, 0);
  i12->setVolume(40);
  i12->setDetune(129);
  i12->setPan(190);
  i12->setFilter(200, 20);
  song->addInstrument(move(i12));

  auto i13 = make_unique<BasicInstrument>(WaveformType::SINE);
  i13->setName("bass drum");
  i13->setADSR(0, 8, 0, 0);
  i13->setVolume(200);
  i13->setFilter(244, 0);
  song->addInstrument(move(i13));

  auto i14 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i14->setName("snare");
  i14->setADSR(0, 5, 0, 0);
  i14->setVolume(240);
  i14->setPan(37);
  i14->setFilter(150, 255);
  song->addInstrument(move(i14));
  
  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 0.1f);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0, 10);
  oboe->setVolume(100);
  oboe->setPan(37);
  oboe->setTranspose(12);
  // oboe->setFilter(100, 30);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.3f));
  song->addInstrument(move(oboe));

  auto harpsichord = make_unique<FMInstrument>(7.8, 3, 5);
  harpsichord->setName("harpsichord");
  harpsichord->setADSR(2, 15, 0, 10);
  harpsichord->setVolume(240);
  harpsichord->setPan(37);
  harpsichord->setTranspose(24);
  // harpsichord->setFilter(200, 20);
  // harpsichord->setFlags(HPFILTER);
  song->addInstrument(move(harpsichord));

  auto bell = make_unique<FMInstrument>(3.5, 7, 9);
  bell->setName("bell");
  bell->setADSR(2, 15, 0, 10);
  bell->setVolume(240);
  bell->setPan(37);
  // bell->setFilter(200, 20);
  // bell->setFlags(HPFILTER);
  song->addInstrument(move(bell));

// Bell 3.5 7 9 0 0.01 0.2 0.3 1.5

  const unsigned char * track = tr;
  
  song->bpm = *track++;
  song->mastervol = (float)(*track++) / 127;
  
  song->delay1 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  song->delay2 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
  song->fd1 = (float)(*track++) / 255;
  song->fd2 = (float)(*track++) / 255;
  song->delaymix1 = (float)(*track++) / 255;
  song->delaymix2 = (float)(*track++) / 255;
  
  int ptrncnt = *track++;
  vector<Sequence> available_sequences;
  for (int i = 0; i < ptrncnt; i++) {
    Sequence sequence;
    sequence.setInstrumentId(*track++);
    
    while (1) {
      int val = *track++;
      if (val == 255) break;
      sequence.addNote(val);
    }

    available_sequences.push_back(sequence);
  }

  size_t max_sequence_length = 0;
  vector<vector<int> > sequence_vectors;
  int trkcnt = *track++;
  for (int i = 0; i < trkcnt; i++) {
    vector<int> seqs;
    while (1) {
      size_t val = *track++;
      if (val == 255) break;
      assert(val < available_sequences.size());
      seqs.push_back(val);
    }
    
    sequence_vectors.push_back(seqs);
    if (sequence_vectors.size() > max_sequence_length) max_sequence_length = sequence_vectors.size();
  }

  for (size_t i = 0; i < max_sequence_length; i++) {
    Section section;
    for (size_t j = 0; j < sequence_vectors.size(); j++) {
      auto & sequences = sequence_vectors[j];
      if (i < sequences.size()) {
	auto id = sequences[i];
	assert(id >= 0 && id < available_sequences.size());
	section.addSequence(available_sequences[id]);
      }
    }
    song->addSection(section);
  }

  current_song = song;
}

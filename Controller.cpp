#include "Controller.h"

#include "Song.h"
#include "BasicInstrument.h"
#include "FMInstrument.h"
#include "FileInstrument.h"
#include "Chorus.h"
#include "Distortion.h"
#include "Reverb.h"
#include "Delay.h"

#include "track.h"

#include <cassert>
#include <iostream>

using namespace std;

Controller::Controller() {
  auto song = make_shared<Song>();

  const unsigned char * song_data = tr;
  
  song->bpm = *song_data++;
  song->mastervol = (float)(*song_data++) / 127;
  
  int delay1 = (int)(MAXDELAYSAMPLES * ((float)(*song_data++) / 255));
  float fd1 = (float)(*song_data++) / 255;
  float delaymix1 = (float)(*song_data++) / 255;

  auto i0 = make_unique<BasicInstrument>(WaveformType::SAW);
  i0->setName("drone1");
  i0->setADSR(255, 64, 0.25f, 0);
  i0->setVolume(0.15f);
  i0->setPan(0.04f);
  i0->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i0));

  auto i1 = make_unique<BasicInstrument>(WaveformType::SAW);
  i1->setName("drone2");
  i1->setADSR(255, 64, 0.25f, 0);
  i1->setVolume(0.15f);
  i1->setDetune(120);
  i1->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i1));

  auto i2 = make_unique<BasicInstrument>(WaveformType::SAW);
  i2->setName("drone3");
  i2->setADSR(255, 64, 0.25f, 0);
  i2->setVolume(0.15f);
  i2->setDetune(134);
  i2->setPan(0.97f);
  i2->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i2));

  auto i3 = make_unique<BasicInstrument>(WaveformType::SINE);
  i3->setName("bass drum");
  i3->setADSR(0, 15, 0.0f, 0);
  i3->setVolume(1.56f);
  i3->setDetune(130);
  // i3->setFilter(1.0f, 0);
  song->addInstrument(move(i3));

  auto i4 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i4->setName("hihat closed");
  i4->setADSR(0, 8, 0.0f, 0);
  i4->setVolume(0.5f);
  i4->setFilter(190 / 255.0f, 128 / 63.0f);
  i4->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i4));

  auto i5 = make_unique<BasicInstrument>(WaveformType::NOISE2);
  i5->setName("hihat open");
  i5->setADSR(0, 13, 0.0f, 0);
  i5->setVolume(0.23f);
  // i5->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i5));

  auto i6 = make_unique<BasicInstrument>(WaveformType::SAW);
  i6->setName("unused");
  i6->setADSR(0, 25, 0.0f, 0);
  // i6->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i6));

  auto i7 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i7->setName("bass");
  i7->setADSR(0, 15, 0.0f, 0);
  i7->setVolume(0.31f);
  i7->setDetune(125);
  i7->setPan(0.25f);
  i7->setFilter(200 / 255.0f, 20 / 63.0f);
  song->addInstrument(move(i7));

#if 0
  auto i8 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i8->setName("hihat closed");
  i8->setADSR(0, 3, 0.0f, 0);
  i8->setVolume(0.5f);
  i8->setPan(0.85f);
  // i8->setFilter(1.0f, 0.0f);  
#else
  auto i8 = make_unique<FileInstrument>("./samples/Closed-Hi-Hat-1.wav");
  i8->setName("hihat closed");
  // i8->setADSR(0, 3, 0.0f, 0);
  // i8->setVolume(0.5f);
  i8->setPan(0.85f);
  // i8->setFilter(1.0f, 0.0f);  
#endif
  song->addInstrument(move(i8));
  
  auto i9 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i9->setName("snare");
  i9->setADSR(0, 15, 0.0f, 0);
  i9->setVolume(0.31f);
  i9->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  // i9->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i9));

  auto i10 = make_unique<BasicInstrument>(WaveformType::SAW);
  i10->setName("bass");
  i10->setADSR(0, 30, 0.0f, 0);
  i10->setVolume(0.47f);
  i10->setFilter(100 / 255.0f, 0);
  i10->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i10));

  auto i11 = make_unique<BasicInstrument>(WaveformType::SAW);
  i11->setName("bass");
  i11->setADSR(0, 20, 0.0f, 0);
  i11->setFilter(63 / 255.0f, 128 / 63.0f);
  i11->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i11));

  auto i12 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i12->setName("bass");
  i12->setADSR(0, 14, 0.0f, 0);
  i12->setVolume(0.31f);
  i12->setDetune(129);
  i12->setPan(0.75f);
  i12->setFilter(200 / 255.0f, 20 / 63.0f);
  song->addInstrument(move(i12));

  auto i13 = make_unique<BasicInstrument>(WaveformType::SINE);
  i13->setName("bass drum");
  i13->setADSR(0, 8, 0.0f, 0);
  i13->setVolume(1.56f);
  i13->setFilter(244 / 255.0f, 0);
  song->addInstrument(move(i13));

  auto i14 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i14->setName("snare");
  i14->setADSR(0, 5, 0.0f, 0);
  i14->setVolume(1.88f);
  i14->setPan(0.15f);
  i14->setFilter(150 / 255.0f, 255 / 63.0f);
  song->addInstrument(move(i14));
  
  int ptrncnt = *song_data++;
  vector<Track> available_tracks;
  for (int i = 0; i < ptrncnt; i++) {
    Track track;
    track.setInstrumentId(*song_data++);
    
    for (size_t j = 0; ; j++) {
      int val = *song_data++;
      if (val == 255) break;
      Note note(val & 0x7f, (val & 0x80) != 0);
      track.setNote(j, note);
    }

    available_tracks.push_back(track);
  }

  size_t max_track_length = 0;
  vector<vector<int> > track_vectors;
  int trkcnt = *song_data++;
  for (int i = 0; i < trkcnt; i++) {
    vector<int> seqs;
    while (1) {
      size_t val = *song_data++;
      if (val == 255) break;
      assert(val < available_tracks.size());
      seqs.push_back(val);
    }
    
    track_vectors.push_back(seqs);
#if 0
    if (track_vectors.size() > max_track_length) max_track_length = track_vectors.size();
#else
    if (seqs.size() > max_track_length) max_track_length = seqs.size();
#endif
  }

  for (size_t i = 0; i < max_track_length; i++) {
    Section section;
    for (size_t j = 0; j < track_vectors.size(); j++) {
      auto & tracks = track_vectors[j];
      if (i < tracks.size()) {
	auto id = tracks[i];
	assert(id >= 0 && id < (int)available_tracks.size());
	section.addTrack(available_tracks[id]);
      }
    }
    assert(!section.empty());
    song->addSection(section);
  }

  current_song = song;
}

void
Controller::createNewSong() {
  auto song = make_shared<Song>();

  auto epiano = make_unique<BasicInstrument>(WaveformType::SAW);
  epiano->setName("Electric Piano");
  epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setFilter(63, 128);
  song->addInstrument(move(epiano));

  auto test = make_unique<FMInstrument>(0, 1, 1, 2.01);
  test->setName("test");
  test->setADSR(2, 15, 0.0f, 10);
  test->setVolume(0.78f);
  test->setPan(0.15f);
  // test->setTranspose(12);
  test->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // test->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  test->setFilter(100, 30);
  song->addInstrument(move(test));
  
  auto oboe = make_unique<FMInstrument>(0.7, 3, 4, 0.1f);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.0f, 10);
  oboe->setVolume(0.78f);
  oboe->setPan(0.15f);
  oboe->setTranspose(12);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(44100, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));

  auto harpsichord = make_unique<FMInstrument>(7.8, 3, 5);
  harpsichord->setName("harpsichord");
  harpsichord->setADSR(2, 15, 0.0f, 10);
  harpsichord->setPan(0.15f);
  harpsichord->setTranspose(24);
  // harpsichord->setFilter(200, 20);
  // harpsichord->setFlags(HPFILTER);
  song->addInstrument(move(harpsichord));

  auto bell = make_unique<FMInstrument>(3.5, 7, 9);
  bell->setName("bell");
  bell->setADSR(2, 15, 0.0f, 10);
  bell->setPan(0.15f);
  // bell->setFilter(200, 20);
  // bell->setFlags(HPFILTER);
  song->addInstrument(move(bell));

// Bell 3.5 7 9 0 0.01 0.2 0.3 1.5

  Track track;
  
  Section section;
  section.addTrack(track);
  song->addSection(section);  

  current_song = song;
}

bool
Controller::sendCommand(const std::string & cmd) {
  if (cmd == "new-song") {
    createNewSong();
  } else {
    return false;
  }
  return true;
}

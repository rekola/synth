#include "Controller.h"

#include "Song.h"
#include "BasicInstrument.h"
#include "FMInstrument.h"
#include "FileInstrument.h"
#include "Chorus.h"
#include "Distortion.h"
#include "Reverb.h"
#include "Delay.h"

#include "default_song.h"

#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace std;

void
Controller::loadDemo() {
  auto song = make_shared<Song>();

  const unsigned char * song_data = tr;
  
  song->bpm = *song_data++;
  song->mastervol = (float)(*song_data++) / 127;
  
  int delay1 = (int)(MAXDELAYSAMPLES * ((float)(*song_data++) / 255));
  float fd1 = (float)(*song_data++) / 255;
  float delaymix1 = (float)(*song_data++) / 255;

  auto i0 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i0->setName("drone1");
  i0->setADSR(255, 64, 0.25f, 0);
  i0->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i0));

  auto i1 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i1->setName("drone2");
  i1->setADSR(255, 64, 0.25f, 0);
  i1->setDetune(120);
  i1->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i1));

  auto i2 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i2->setName("drone3");
  i2->setADSR(255, 64, 0.25f, 0);
  i2->setDetune(134);
  i2->setFilter(0, 5 / 63.0f, true);
  song->addInstrument(move(i2));

  auto i3 = make_unique<BasicInstrument>(BasicInstrument::SINE);
  i3->setName("bass drum");
  i3->setADSR(0, 15, 0.0f, 0);
  i3->setDetune(130);
  // i3->setFilter(1.0f, 0);
  song->addInstrument(move(i3));

  auto i4 = make_unique<BasicInstrument>(BasicInstrument::NOISE2);
  i4->setName("hihat closed");
  i4->setADSR(0, 8, 0.0f, 0);
  i4->setFilter(190 / 255.0f, 128 / 63.0f);
  i4->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i4));

  auto i5 = make_unique<BasicInstrument>(BasicInstrument::NOISE2);
  i5->setName("hihat open");
  i5->setADSR(0, 13, 0.0f, 0);
  // i5->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i5));

  auto i6 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i6->setName("unused");
  i6->setADSR(0, 25, 0.0f, 0);
  // i6->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i6));

  auto i7 = make_unique<BasicInstrument>(BasicInstrument::SQUARE);
  i7->setName("bass");
  i7->setADSR(0, 15, 0.0f, 0);
  i7->setDetune(125);
  i7->setFilter(200 / 255.0f, 20 / 63.0f);
  song->addInstrument(move(i7));

#if 0
  auto i8 = make_unique<BasicInstrument>(BasicInstrument::NOISE);
  i8->setName("hihat closed");
  i8->setADSR(0, 3, 0.0f, 0);
#else
  auto i8 = make_unique<FileInstrument>("./samples/Closed-Hi-Hat-1.wav");
  i8->setName("hihat closed");
  // i8->setADSR(0, 3, 0.0f, 0);
#endif
  song->addInstrument(move(i8));
  
  auto i9 = make_unique<BasicInstrument>(BasicInstrument::NOISE);
  i9->setName("snare");
  i9->setADSR(0, 15, 0.0f, 0);
  i9->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  // i9->setFilter(1.0f, 0.0f);
  song->addInstrument(move(i9));

  auto i10 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i10->setName("bass");
  i10->setADSR(0, 30, 0.0f, 0);
  i10->setFilter(100 / 255.0f, 0);
  i10->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i10));

  auto i11 = make_unique<BasicInstrument>(BasicInstrument::SAW);
  i11->setName("bass");
  i11->setADSR(0, 20, 0.0f, 0);
  i11->setFilter(63 / 255.0f, 128 / 63.0f);
  i11->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i11));

  auto i12 = make_unique<BasicInstrument>(BasicInstrument::SQUARE);
  i12->setName("bass");
  i12->setADSR(0, 14, 0.0f, 0);
  i12->setDetune(129);
  i12->setFilter(200 / 255.0f, 20 / 63.0f);
  song->addInstrument(move(i12));

  auto i13 = make_unique<BasicInstrument>(BasicInstrument::SINE);
  i13->setName("bass drum");
  i13->setADSR(0, 8, 0.0f, 0);
  i13->setFilter(244 / 255.0f, 0);
  song->addInstrument(move(i13));

  auto i14 = make_unique<BasicInstrument>(BasicInstrument::NOISE);
  i14->setName("snare");
  i14->setADSR(0, 5, 0.0f, 0);
  i14->setFilter(150 / 255.0f, 255 / 63.0f);
  song->addInstrument(move(i14));
  
  int ptrncnt = *song_data++;

  unordered_map<unsigned short, unordered_map<unsigned short, Note> > track_notes;
  for (int i = 0; i < ptrncnt; i++) {
    Track track;
    track.setInstrumentId(*song_data++);
    track.setPan(*song_data++ / 255.0f);
    track.setVolume(*song_data++ / 127.0f);
    song->addTrack(track);
    
    for (size_t j = 0; ; j++) {
      int val = *song_data++;
      if (val == 255) break;
      int midi_note = val & 0x7f;
      bool has_accent = val & 0x80;
      track_notes[i][j] = Note(midi_note, has_accent ? 0x60 : 0x3f);
    }
  }

  size_t max_track_length = 0;
  vector<vector<int> > track_vectors;
  int trkcnt = *song_data++;
  for (int i = 0; i < trkcnt; i++) {
    vector<int> seqs;
    while (1) {
      size_t val = *song_data++;
      if (val == 255) break;
      // assert(val < available_tracks.size());
      seqs.push_back(val);
    }
    
    track_vectors.push_back(seqs);
    if (seqs.size() > max_track_length) max_track_length = seqs.size();
  }

  for (size_t i = 0; i < max_track_length; i++) {
    Pattern pattern;
    for (size_t j = 0; j < track_vectors.size(); j++) {
      auto & tracks = track_vectors[j];
      if (i < tracks.size()) {
	auto track_id = tracks[i];
	// assert(id >= 0 && id < (int)available_tracks.size());
	// pattern.addTrack(available_tracks[id]);
	for (size_t k = 0; k < 32; k++) {
	  Note note = track_notes[track_id][k];
	  if (note.isDefined()) pattern.setNote(k, track_id, 0, note);
	  else {
	    cerr << "note missing: pattern=" << i << ", track = " << track_id << ", row = " << k << endl;
	  }
	}
      }
    }
    song->addPattern(pattern);
  }

  current_song = song;
}

void
Controller::createNewSong() {
  auto song = make_shared<Song>();

  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 0.1f);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  oboe->setTranspose(24);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(44100, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));

  auto epiano = make_unique<BasicInstrument>(BasicInstrument::SAW);
  epiano->setName("Electric Piano");
  epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setFilter(63, 128);
  song->addInstrument(move(epiano));

  auto test = make_unique<FMInstrument>(0, 1, 1, 2.01);
  test->setName("test");
  test->setADSR(2, 15, 0.0f, 10);
  // test->setTranspose(12);
  test->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // test->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  test->setFilter(100, 30);
  song->addInstrument(move(test));
  
  auto harpsichord = make_unique<FMInstrument>(7.8, 3, 5);
  harpsichord->setName("harpsichord");
  harpsichord->setADSR(2, 15, 0.0f, 10);
  harpsichord->setTranspose(24);
  // harpsichord->setFilter(200, 20);
  // harpsichord->setFlags(HPFILTER);
  song->addInstrument(move(harpsichord));

  auto bell = make_unique<FMInstrument>(3.5, 7, 9);
  bell->setName("bell");
  bell->setADSR(2, 15, 0.0f, 10);
  // bell->setFilter(200, 20);
  // bell->setFlags(HPFILTER);
  song->addInstrument(move(bell));

// Bell 3.5 7 9 0 0.01 0.2 0.3 1.5
  
  Pattern pattern;
  song->addPattern(pattern);  
  song->addTrack();
  song->addTrack();
  song->addTrack();
  song->addTrack();
  
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

#include "Controller.h"

#include "Song.h"
#include "BasicInstrument.h"
#include "FMInstrument.h"
#include "FileInstrument.h"
#include "SoundFont.h"
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
Controller::loadDemo3() {
  auto song = make_shared<Song>();
  song->bpm = 90;
  song->mastervol = 1.0f;

#if 1
  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 0.1f);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  oboe->setTranspose(24);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(44100, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));
#else
  auto fluid = make_unique<SoundFont>("FluidR3_GM.sf2");
  // auto fluid = make_unique<SoundFontInstrument>("Tabla.sf2", 0);
  // auto fluid = make_unique<SoundFontInstrument>("FatBoy-v0.790.sf2", 6);
  // auto fluid = make_unique<SoundFontInstrument>("Musyng.sfpack", 0);
  song->addInstruments(*fluid);
#endif
  
  song->addTrack();
  
  Pattern pattern(128);  
  pattern.setNote(0, 0, 0, Note(5, 4, 0x3f)); // C-4
  pattern.setNote(0, 0, 1, Note(9, 8, 0x3f)); // C-4
  pattern.setNote(0, 0, 2, Note(1, 1, 0x3f)); // C-4
  song->addPattern(pattern);  
    
  current_song = song;    
}

void
Controller::loadDemo2() {
  auto song = make_shared<Song>();

  song->bpm = 90;
  song->mastervol = 1.0f;

#if 0
  auto fluid = make_unique<SoundFont>("FluidR3_GM.sf2");
  song->addInstrument(fluid->createInstrument(0));
#else
  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 0.1f);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  oboe->setTranspose(24);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(44100, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));

  auto epiano = make_unique<BasicInstrument>(WaveformType::SAW);
  epiano->setName("Electric Piano");
  epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setFilter(63 / 255.0f, 128 / 63.0f);
  song->addInstrument(move(epiano));
#endif

  song->addTrack();
  
  Pattern pattern(256);

  pattern.setAnnotation(0, "neutral minor tetrad");

  // pattern.setNote(0, 0, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 1, 0, Note(3, 2, 0x3f)); // G-4
  pattern.setNote(0, 2, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 3, 0, Note(11, 10, 0x3f)); // ?
  // pattern.setNote(0, 4, 0, Note(2, 1, 0x3f)); // C-5
  pattern.setNote(0, 5, 0, Note(3, 2, 0x3f)); // G-4
  pattern.setNote(0, 6, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 7, 0, Note(1, 1, 0x3f)); // C-4
  
  pattern.setNote(0, 10, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 11, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 12, 0, Note(3, 2, 0x3f)); // G-4
  pattern.setNote(0, 13, 0, Note(2, 1, 0x3f)); // C-5

  // pattern.setNote(0, 16, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 17, 0, Note(11, 10, 0x3f)); // ?
  pattern.setNote(0, 18, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 19, 0, Note(3, 2, 0x3f)); // G-4
  // pattern.setNote(0, 20, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 20, 1, Note(11, 10, 0x3f)); // ?
  pattern.setNote(0, 20, 2, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 20, 3, Note(3, 2, 0x3f)); // G-4
  // pattern.setNote(0, 20, 4, Note(2, 1, 0x3f)); // C-5

  // pattern.setNote(0, 22, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 23, 0, Note(12, 11, 0x3f)); // ?
  pattern.setNote(0, 24, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 25, 0, Note(3, 2, 0x3f)); // G-4
  // pattern.setNote(0, 26, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 26, 1, Note(12, 11, 0x3f)); // ?
  pattern.setNote(0, 26, 2, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 26, 3, Note(3, 2, 0x3f)); // G-4
  // pattern.setNote(0, 26, 4, Note(2, 1, 0x3f)); // C-5

  pattern.setAnnotation(30, "7-limit triads");
  
  pattern.setNote(0, 30, 0, Note(1, 1, 0x3f)); // C-4
  // pattern.setNote(0, 30, 1, Note(7, 6, 0x3f)); // ?
  pattern.setNote(0, 30, 2, Note(7, 5, 0x3f)); // F
  // pattern.setNote(0, 30, 3, Note(2, 1, 0x3f)); // C-5

  // pattern.setNote(0, 32, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 32, 1, Note(8, 7, 0x3f)); // ?
  pattern.setNote(0, 32, 2, Note(7, 5, 0x3f)); // F
  // pattern.setNote(0, 32, 3, Note(2, 1, 0x3f)); // C-5

  // pattern.setNote(0, 34, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 34, 1, Note(8, 7, 0x3f)); // ?
  pattern.setNote(0, 34, 2, Note(4, 3, 0x3f)); // F
  //pattern.setNote(0, 34, 3, Note(2, 1, 0x3f)); // C-5
  
  // pattern.setNote(0, 36, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 36, 1, Note(7, 6, 0x3f)); // ?
  // pattern.setNote(0, 36, 2, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 36, 3, Note(9, 5, 0x3f)); //
  // pattern.setNote(0, 34, 4, Note(2, 1, 0x3f)); // C-5
    
  pattern.setNote(0, 40, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 41, 0, Note(7, 6, 0x3f)); // ?
  pattern.setNote(0, 42, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 43, 0, Note(2, 1, 0x3f)); // C-5

  pattern.setNote(0, 44, 0, Note(7*1, 6*1, 0x3f)); // C-4
  pattern.setNote(0, 45, 0, Note(7*8, 6*7, 0x3f)); // ?
  pattern.setNote(0, 46, 0, Note(7*4, 6*3, 0x3f)); // F
  pattern.setNote(0, 47, 0, Note(7*2, 6*1, 0x3f)); // C-5

  pattern.setNote(0, 48, 0, Note(4*1, 3*1, 0x3f)); // C-4
  pattern.setNote(0, 49, 0, Note(4*7, 3*6, 0x3f)); // ?
  pattern.setNote(0, 50, 0, Note(4*4, 3*3, 0x3f)); // F
  pattern.setNote(0, 51, 0, Note(4*2, 3*1, 0x3f)); // C-5

  pattern.setNote(0, 52, 0, Note(7*1, 6*1, 0x3f)); // C-4
  pattern.setNote(0, 53, 0, Note(7*4, 6*3, 0x3f)); // F
  pattern.setNote(0, 54, 0, Note(7*7, 6*4, 0x3f)); // ?
  pattern.setNote(0, 55, 0, Note(7*2, 6*1, 0x3f)); // C-5

  pattern.setAnnotation(60, "7-limit supermajor minor7 scale");

  pattern.setNote(0, 60, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 61, 0, Note(8, 7, 0x3f)); // ?
  pattern.setNote(0, 62, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 63, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 64, 0, Note(3, 2, 0x3f)); // G
  pattern.setNote(0, 65, 0, Note(8, 5, 0x3f)); // Ab
  pattern.setNote(0, 66, 0, Note(9, 5, 0x3f)); // 
  pattern.setNote(0, 67, 0, Note(2, 1, 0x3f)); // C-5

  pattern.setAnnotation(70, "harmonic minor scale");

  pattern.setNote(0, 70, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 71, 0, Note(9, 8, 0x3f)); // D
  pattern.setNote(0, 72, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 73, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 74, 0, Note(3, 2, 0x3f)); // G
  pattern.setNote(0, 75, 0, Note(8, 5, 0x3f)); // Ab
  pattern.setNote(0, 76, 0, Note(7, 4, 0x3f)); // 
  pattern.setNote(0, 77, 0, Note(2, 1, 0x3f)); // C-5

  pattern.setAnnotation(80, "melodic minor scale");

  pattern.setNote(0, 80, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 81, 0, Note(9, 8, 0x3f)); // D
  pattern.setNote(0, 82, 0, Note(6, 5, 0x3f)); // Eb
  pattern.setNote(0, 83, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 84, 0, Note(3, 2, 0x3f)); // G
  pattern.setNote(0, 85, 0, Note(8, 5, 0x3f)); // Ab
  pattern.setNote(0, 86, 0, Note(9, 5, 0x3f)); // 
  pattern.setNote(0, 87, 0, Note(2, 1, 0x3f)); // C-5

  pattern.setAnnotation(90, "subminor pentatonic scale");

  pattern.setNote(0, 90, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 91, 0, Note(7, 6, 0x3f)); // subminor third
  pattern.setNote(0, 92, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 93, 0, Note(3, 2, 0x3f)); // G
  pattern.setNote(0, 94, 0, Note(9, 5, 0x3f)); // 
  pattern.setNote(0, 95, 0, Note(2, 1, 0x3f)); // C-5

  pattern.setAnnotation(100, "septimal minor pentatonic scale");

  pattern.setNote(0, 100, 0, Note(1, 1, 0x3f)); // C-4
  pattern.setNote(0, 101, 0, Note(6, 5, 0x3f)); // minor thirds
  pattern.setNote(0, 102, 0, Note(4, 3, 0x3f)); // F
  pattern.setNote(0, 103, 0, Note(3, 2, 0x3f)); // G
  pattern.setNote(0, 104, 0, Note(7, 4, 0x3f)); // 
  pattern.setNote(0, 105, 0, Note(2, 1, 0x3f)); // C-5


  pattern.setNote(0, 112, 0, Note(7, 6, 0x3f));
  pattern.setNote(0, 112, 1, Note(4, 3, 0x3f));

  pattern.setNote(0, 114, 0, Note(8, 7, 0x3f));
  pattern.setNote(0, 114, 1, Note(4, 3, 0x3f));

  pattern.setNote(0, 116, 0, Note(7, 6, 0x3f));
  pattern.setNote(0, 116, 1, Note(1, 1, 0x3f));

  pattern.setNote(0, 118, 0, Note(8, 7, 0x3f));
  pattern.setNote(0, 118, 1, Note(1, 1, 0x3f));

  pattern.setNote(0, 120, 0, Note(7, 6, 0x3f));
  pattern.setNote(0, 120, 1, Note(3, 2, 0x3f));

  pattern.setNote(0, 122, 0, Note(8, 7, 0x3f));
  pattern.setNote(0, 122, 1, Note(3, 2, 0x3f));

  pattern.setNote(0, 124, 0, Note(7, 6, 0x3f));
  pattern.setNote(0, 124, 1, Note(5, 4, 0x3f));

  pattern.setNote(0, 126, 0, Note(8, 7, 0x3f));
  pattern.setNote(0, 126, 1, Note(5, 4, 0x3f));

  pattern.setNote(0, 128, 0, Note(7, 6, 0x3f));
  pattern.setNote(0, 128, 1, Note(6, 5, 0x3f));

  pattern.setNote(0, 130, 0, Note(8, 7, 0x3f));
  pattern.setNote(0, 130, 1, Note(6, 5, 0x3f));


  pattern.setNote(0, 140, 0, Note(1, 1, 0x3f));
  pattern.setNote(0, 140, 1, Note(6, 5, 0x3f));
  pattern.setNote(0, 140, 2, Note(3, 2, 0x3f));

  pattern.setNote(0, 142, 0, Note(1, 1, 0x3f));
  pattern.setNote(0, 142, 1, Note(6, 5, 0x3f));
  pattern.setNote(0, 142, 2, Note(21, 15, 0x3f));

  pattern.setNote(0, 144, 0, Note(8*1, 7*1, 0x3f));
  pattern.setNote(0, 144, 1, Note(8*6, 7*5, 0x3f));
  pattern.setNote(0, 144, 2, Note(8*3, 7*2, 0x3f));

  pattern.setNote(0, 146, 0, Note(7*1, 6*1, 0x3f));
  pattern.setNote(0, 146, 1, Note(7*6, 6*5, 0x3f));
  pattern.setNote(0, 146, 2, Note(7*3, 6*2, 0x3f));

  pattern.setNote(0, 148, 0, Note(7*1, 6*1, 0x3f));
  pattern.setNote(0, 148, 1, Note(7*7, 6*6, 0x3f));
  pattern.setNote(0, 148, 2, Note(7*4, 6*3, 0x3f));


  pattern.setNote(0, 152, 0, Note(1, 1, 0x3f));
  pattern.setNote(0, 152, 1, Note(3, 2, 0x3f));
  pattern.setNote(0, 152, 2, Note(21, 12, 0x3f));

  
  song->addPattern(pattern);  
    
  current_song = song;  
}

void
Controller::loadDemo() {
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
  i0->setFilter(0, 0.08f, true);
  song->addInstrument(move(i0));

  auto i1 = make_unique<BasicInstrument>(WaveformType::SAW);
  i1->setName("drone2");
  i1->setADSR(255, 64, 0.25f, 0);
  i1->setDetune(120);
  i1->setFilter(0, 0.08f, true);
  song->addInstrument(move(i1));

  auto i2 = make_unique<BasicInstrument>(WaveformType::SAW);
  i2->setName("drone3");
  i2->setADSR(255, 64, 0.25f, 0);
  i2->setDetune(134);
  i2->setFilter(0, 0.08f, true);
  song->addInstrument(move(i2));

  auto i3 = make_unique<BasicInstrument>(WaveformType::SINE);
  i3->setName("bass drum");
  i3->setADSR(0, 15, 0.0f, 0);
  i3->setDetune(130);
  song->addInstrument(move(i3));

  auto i4 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i4->setName("hihat closed");
  i4->setADSR(0, 8, 0.0f, 0);
  i4->setFilter(0.75f, 2.0f);
  i4->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i4));

  auto i5 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i5->setName("hihat open");
  i5->setADSR(0, 13, 0.0f, 0);
  song->addInstrument(move(i5));

  auto i6 = make_unique<BasicInstrument>(WaveformType::SAW);
  i6->setName("unused");
  i6->setADSR(0, 25, 0.0f, 0);
  song->addInstrument(move(i6));

  auto i7 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i7->setName("bass");
  i7->setADSR(0, 15, 0.0f, 0);
  i7->setDetune(125);
  i7->setFilter(0.78f, 0.32f);
  song->addInstrument(move(i7));

#if 0
  auto i8 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i8->setName("hihat closed");
  i8->setADSR(0, 3, 0.0f, 0);
#else
  auto i8 = make_unique<FileInstrument>("./samples/Closed-Hi-Hat-1.wav");
  i8->setName("hihat closed");
  // i8->setADSR(0, 3, 0.0f, 0);
#endif
  song->addInstrument(move(i8));
  
  auto i9 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i9->setName("snare");
  i9->setADSR(0, 15, 0.0f, 0);
  i9->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i9));

  auto i10 = make_unique<BasicInstrument>(WaveformType::SAW);
  i10->setName("bass");
  i10->setADSR(0, 30, 0.0f, 0);
  i10->setFilter(0.4f, 0.0f);
  i10->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i10));

  auto i11 = make_unique<BasicInstrument>(WaveformType::SAW);
  i11->setName("bass");
  i11->setADSR(0, 20, 0.0f, 0);
  i11->setFilter(0.25f, 2.0f);
  i11->addEffect(make_unique<Delay>(delay1, fd1, delaymix1));
  song->addInstrument(move(i11));

  auto i12 = make_unique<BasicInstrument>(WaveformType::SQUARE);
  i12->setName("bass");
  i12->setADSR(0, 14, 0.0f, 0);
  i12->setDetune(129);
  i12->setFilter(0.78f, 0.32f);
  song->addInstrument(move(i12));

  auto i13 = make_unique<BasicInstrument>(WaveformType::SINE);
  i13->setName("bass drum");
  i13->setADSR(0, 8, 0.0f, 0);
  i13->setFilter(0.96f, 0.0f);
  song->addInstrument(move(i13));

  auto i14 = make_unique<BasicInstrument>(WaveformType::NOISE);
  i14->setName("snare");
  i14->setADSR(0, 5, 0.0f, 0);
  i14->setFilter(0.6f, 4.0f);
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
	for (size_t k = 0; k < 32; k++) {
	  Note note = track_notes[track_id][k];
	  if (note.isDefined()) pattern.setNote(track_id, k, 0, note);
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

  auto epiano = make_unique<BasicInstrument>(WaveformType::SAW);
  epiano->setName("Electric Piano");
  epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setFilter(63 / 255.0f, 128 / 63.0f);
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
  // harpsichord->setFilter(200, 20, true);
  song->addInstrument(move(harpsichord));

  auto bell = make_unique<FMInstrument>(3.5, 7, 9);
  bell->setName("bell");
  bell->setADSR(2, 15, 0.0f, 10);
  // bell->setFilter(200, 20, true);
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

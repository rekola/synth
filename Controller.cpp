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
#include "Synth.h"

#include "default_song.h"

#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace std;

void
Controller::loadDemo4() {
  auto song = make_shared<Song>(Tuning::TET31, 0);
  song->setTempo(220);
  auto sampleRate = synth->getSampleRate();

  auto fluid = make_unique<SoundFont>(sampleRate, "data/FluidR3_GM.sf2");
  // auto instrument = fluid->createInstrument(10);
  // instrument->addEffect(make_unique<Distortion>(Distortion::ZEROES, 0.1, 0.0));
  song->addInstruments(*fluid);

  auto & track = song->getMasterTrack().addChild();
  track.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track.setVolume(0.5f);
  track.setElevation(50);
  track.setAzimuth(30);
  track.setInstrumentId(0);

  auto & pattern = song->addPattern(24);  
  pattern.setNote(0, 0, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 0, 1, Note("Eb4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 0, 2, Note("G-4", 0x3f, Tuning::TET31));

  pattern.setNote(0, 2, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 3, 0, Note("G-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 6, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 8, 0, Note("Eb4", 0x3f, Tuning::TET31));

  pattern.setNote(0, 12, 0, Note("F-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 14, 0, Note("Cx4", 0x3f, Tuning::TET31));

  pattern.setNote(0, 17, 0, Note("C-4", 0x3f, Tuning::TET31));

  auto & pattern2 = song->addPattern(24);  
  pattern2.setNote(0, 0, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 0, 1, Note("D#4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 0, 2, Note("F-4", 0x3f, Tuning::TET31));

  pattern2.setNote(0, 2, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 3, 0, Note("G-4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 6, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 8, 0, Note("D#4", 0x3f, Tuning::TET31));

  pattern2.setNote(0, 12, 0, Note("F-4", 0x3f, Tuning::TET31));
  pattern2.setNote(0, 14, 0, Note("G-4", 0x3f, Tuning::TET31));

  pattern2.setNote(0, 17, 0, Note("C-4", 0x3f, Tuning::TET31));

  current_song = song;
}

void
Controller::loadDemo3() {
  auto song = make_shared<Song>(Tuning::TET31, 0);
  song->setTempo(220);
  auto sampleRate = synth->getSampleRate();

#if 0
  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 4);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->setFilter(100, 30);
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  song->addInstrument(move(oboe));
#else
  auto fluid = make_unique<SoundFont>(sampleRate, "data/FluidR3_GM.sf2");
  // auto instrument = fluid->createInstrument(10);
  // instrument->addEffect(make_unique<Distortion>(Distortion::ZEROES, 0.1, 0.0));
  song->addInstruments(*fluid);
#endif

  // int melody_instrument = 10;
  int melody_instrument = 45;
  
  auto & track = song->getMasterTrack().addChild();
  track.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track.setVolume(0.5f);
  track.setElevation(50);
  track.setAzimuth(30);
  track.setInstrumentId(melody_instrument);

  auto & track2 = song->getMasterTrack().addChild();
  track2.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track2.setVolume(0.5f);
  track2.setElevation(0);
  track2.setAzimuth(0);
  track2.setInstrumentId(melody_instrument);
  
  auto & track3 = song->getMasterTrack().addChild();
  track3.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track3.setVolume(0.5f);
  track3.setElevation(-40);
  track3.setAzimuth(-30);
  track3.setInstrumentId(melody_instrument);

  auto & track4 = song->getMasterTrack().addChild();
  track4.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  track4.setVolume(0.5f);
  track4.setElevation(90);
  track4.setAzimuth(0);
  track4.setInstrumentId(33);

  auto & track5 = song->getMasterTrack().addChild();
  track5.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track5.setVolume(0.5f);
  track5.setElevation(-20);
  track5.setAzimuth(15);
  track5.setInstrumentId(160);

  auto & track6 = song->getMasterTrack().addChild();
  track6.addEffect(make_unique<Reverb>(sampleRate, Reverb::STADIUM));
  // track6.setVolume(0.3f);
  track6.setElevation(90);
  track6.setAzimuth(0);
  track6.setInstrumentId(160);

  auto & pattern0 = song->addPattern(4);
  pattern0.setNote(3, 0, 0, Note("G-2", 0x50, Tuning::TET31));

  auto & pattern = song->addPattern(64);  
  pattern.setNote(0, 0, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 0, 1, Note("Eb4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 0, 2, Note("G-4", 0x3f, Tuning::TET31));

  pattern.setNote(3, 0, 0, Note("C-3", 0x3f, Tuning::TET31));
  pattern.setNote(4, 0, 0, Note("E-4", 0x3f, Tuning::TET31));

  // pattern.setNote(2, 3, 0, Note("C♭4", 0x3f, Tuning::TET31));
  pattern.setNote(1, 4, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(2, 6, 0, Note("D-4", 0x3f, Tuning::TET31));
  pattern.setNote(3, 6, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(4, 8, 0, Note("E-4", 0x3f, Tuning::TET31));
  pattern.setNote(2, 12, 0, Note("Eb4", 0x3f, Tuning::TET31));
  pattern.setNote(3, 12, 0, Note("Eb3", 0x3f, Tuning::TET31));

  pattern.setNote(0, 16, 0, Note("D-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 16, 1, Note("F-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 16, 2, Note("Ab4", 0x3f, Tuning::TET31));  
  pattern.setNote(3, 16, 0, Note("D-3", 0x3f, Tuning::TET31));

  pattern.setNote(4, 16, 0, Note("E-4", 0x3f, Tuning::TET31));

  pattern.setNote(1, 20, 0, Note("D-4", 0x3f, Tuning::TET31));
  pattern.setNote(2, 22, 0, Note("F-4", 0x3f, Tuning::TET31));
  pattern.setNote(4, 24, 0, Note("E-4", 0x3f, Tuning::TET31));
  pattern.setNote(2, 26, 0, Note("Eb4", 0x3f, Tuning::TET31));
  pattern.setNote(1, 28, 0, Note("E𝄫4", 0x3f, Tuning::TET31));
  pattern.setNote(3, 28, 0, Note("E𝄫3", 0x3f, Tuning::TET31));
  // pattern.setNote(5, 28, 0, Note("E𝄫3", 0x3f, Tuning::TET31));

  pattern.setNote(0, 30, 0, Note("C-4", 0x3f, Tuning::TET31));

  pattern.setNote(0, 32, 0, Note("C-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 32, 1, Note("Eb4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 32, 2, Note("G-4", 0x3f, Tuning::TET31));
  // pattern.setNote(0, 32, 2, Note("Bb4", 0x3f, Tuning::TET31));
  // pattern.setNote(0, 32, 1, Note("A𝄫4", 0x3f, Tuning::TET31));
  // pattern.setNote(0, 32, 1, Note("F-4", 0x3f, Tuning::TET31));
  // pattern.setNote(0, 32, 2, Note("G-4", 0x3f, Tuning::TET31));
  // pattern.setNote(0, 32, 2, Note("A♭4", 0x3f, Tuning::TET31)); // 4:3
  
  pattern.setNote(3, 32, 0, Note("Eb3", 0x3f, Tuning::TET31));
  pattern.setNote(4, 32, 0, Note("E-4", 0x3f, Tuning::TET31));
  // pattern.setNote(5, 32, 0, Note("Eb3", 0x3f, Tuning::TET31));

  pattern.setNote(0, 36, 0, Note("Eb4", 0x3f, Tuning::TET31));
  // pattern.setNote(1, 38, 0, Note("A𝄫4", 0x3f, Tuning::TET31));
  pattern.setNote(1, 38, 0, Note("G-4", 0x3f, Tuning::TET31));
  pattern.setNote(4, 40, 0, Note("E-4", 0x3f, Tuning::TET31));
  pattern.setNote(2, 44, 0, Note("A♭4", 0x3f, Tuning::TET31));
  // pattern.setNote(2, 44, 0, Note("G-4", 0x3f, Tuning::TET31));
  // pattern.setNote(1, 48, 0, Note("A𝄫4", 0x3f, Tuning::TET31));
  pattern.setNote(1, 48, 0, Note("G-4", 0x3f, Tuning::TET31));
  pattern.setNote(4, 48, 0, Note("E-4", 0x3f, Tuning::TET31));

  current_song = song;    
}

void
Controller::loadDemo2() {
  auto song = make_shared<Song>(Tuning::TET31, 0); // Key of C

  auto sampleRate = synth->getSampleRate();

#if 0
  auto fluid = make_unique<SoundFont>(sampleRate, "data/FluidR3_GM.sf2");
  auto instrument = fluid->createInstrument(2);
  // instrument->addEffect(make_unique<Distortion>(Distortion::ZEROES, 0.1, 0.0));  
  song->addInstrument(move(instrument));
#else
  auto epiano = make_unique<BasicInstrument>(WaveformType::SAW);
  epiano->setName("Electric Piano");
  epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setFilter(63 / 255.0f, 128 / 63.0f);
  song->addInstrument(move(epiano));

  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 4);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(sampleRate, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));  
#endif

  auto & track = song->getMasterTrack().addChild();
  // track.addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // track.addEffect(make_unique<Filter>(63 / 255.0f, 128 / 63.0f, false));
  track.addEffect(make_unique<Reverb>(sampleRate, Reverb::DARK));
  track.setElevation(-30);
  track.setAzimuth(15);
  track.setVolume(0.5f);

  Pattern pattern(256);

  pattern.setNote(0, 0, 0, Note(155, 0x2f)); // C-4
  pattern.setNote(0, 0, 1, Note(163, 0x2f)); // Eb4
  pattern.setNote(0, 0, 2, Note(173, 0x2f)); // G-4

  pattern.setNote(0, 2, 0, Note(155, 0x2f)); // C-4
  pattern.setNote(0, 2, 1, Note(163, 0x2f)); // Eb4
  pattern.setNote(0, 2, 2, Note(170, 0x2f)); // F#4

  pattern.setNote(0, 4, 0, Note(155, 0x2f)); // C-4
  pattern.setNote(0, 4, 1, Note(161, 0x2f)); // Ebb4 (8:7)
  pattern.setNote(0, 4, 2, Note(168, 0x2f)); // F-4

  pattern.setNote(0, 6, 0, Note(155, 0x2f));
  pattern.setNote(0, 6, 1, Note(161, 0x2f));
  pattern.setNote(0, 6, 2, Note(168, 0x2f));


  pattern.setAnnotation(14, "neutral minor tetrad");
  pattern.setNote(0, 14, 0, Note(155, 0x2f));
  pattern.setNote(0, 14, 1, Note(173, 0x2f));
  pattern.setNote(0, 14, 2, Note(163, 0x2f));
  pattern.setNote(0, 14, 3, Note(159, 0x2f));

  pattern.setNote(0, 16, 0, Note(155, 0x2f));  


  pattern.setNote(0, 30, 0, Note(155)); // C-4
  pattern.setNote(0, 31, 0, Note(162)); // D♯4
  pattern.setNote(0, 32, 0, Note(168)); // F-4
  pattern.setNote(0, 33, 0, Note(168 + 7)); //

  pattern.setNote(0, 34, 0, Note(162)); // D#4
  pattern.setNote(0, 35, 0, Note(168)); // F-4
  pattern.setNote(0, 36, 0, Note(175)); // G#4
  pattern.setNote(0, 37, 0, Note(193)); // D#5

  pattern.setNote(0, 38, 0, Note(168)); // F-4
  pattern.setNote(0, 39, 0, Note(175)); // G#4
  pattern.setNote(0, 40, 0, Note(181)); // B♭4 
  pattern.setNote(0, 41, 0, Note(199)); // F-5

  pattern.setNote(0, 42, 0, Note(162)); // D#4
  pattern.setNote(0, 43, 0, Note(168)); // F-4
  pattern.setNote(0, 44, 0, Note(187)); // D𝄫5
  pattern.setNote(0, 45, 0, Note(187 - 1)); //
  
  pattern.setAnnotation(55, "3-limit scale");
  pattern.setNote(0, 56, 0, Note(155)); // C-4 1:1
  pattern.setNote(0, 57, 0, Note(160)); // D-4 9:8
  pattern.setNote(0, 58, 0, Note(168)); // F-4 4:3
  pattern.setNote(0, 59, 0, Note(173)); // G-4 3:2
  pattern.setNote(0, 60, 0, Note(181)); // Bb4 1+4:5
  pattern.setNote(0, 61, 0, Note(186)); // C-5 2:1

  pattern.setAnnotation(70, "5-limit scale (Major)");
  pattern.setNote(0, 70, 0, Note(155)); // C-4	10:10
  pattern.setNote(0, 71, 0, Note(160)); // D-4	9:8
  pattern.setNote(0, 72, 0, Note(165)); // E-4	5:4
  pattern.setNote(0, 73, 0, Note(168)); // F-4	4:3
  pattern.setNote(0, 74, 0, Note(173)); // G-4	1+1:2
  pattern.setNote(0, 75, 0, Note(178)); // A-4  1+2:3
  // pattern.setNote(0, 76, 0, Note(183)); // B-4	1+7:8
  pattern.setNote(0, 76, 0, Note(181));
  pattern.setNote(0, 77, 0, Note(186)); // C-5	1:1

  pattern.setAnnotation(80, "5-limit scale (Minor)");
  pattern.setNote(0, 80, 0, Note(155)); // C-4  10:10
  pattern.setNote(0, 81, 0, Note(160)); // D-4  9:8
  pattern.setNote(0, 82, 0, Note(163)); // Eb4  6:5
  pattern.setNote(0, 83, 0, Note(168)); // F-4	4:3
  pattern.setNote(0, 84, 0, Note(173)); // G-4	1+1:2
  pattern.setNote(0, 85, 0, Note(176)); // Ab4	1+3:5 ?
  pattern.setNote(0, 86, 0, Note(181)); // Bb4  1+4:5 ?
  pattern.setNote(0, 87, 0, Note(186)); // C-5  

  pattern.setAnnotation(90, "Sad scale");
  pattern.setNote(0, 90, 0, Note("C-4", 0x3f, Tuning::TET31)); // C-4
  pattern.setNote(0, 91, 0, Note("E𝄫4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 92, 0, Note("D#4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 93, 0, Note("F-4", 0x3f, Tuning::TET31));
  pattern.setNote(0, 97, 0, Note("C-5", 0x3f, Tuning::TET31));

  #if 0
  pattern.setAnnotation(100, "7-limit scale");
  pattern.setNote(0, 100, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 101, 0, Note(158)); // D♭4  16:15		3
  pattern.setNote(0, 102, 0, Note(161)); // E𝄫4  8:7		3
  pattern.setNote(0, 103, 0, Note(163)); // E♭4  6:5		2
  pattern.setNote(0, 104, 0, Note(165)); // E-4  5:4		2
  pattern.setNote(0, 105, 0, Note(168)); // F-4  4:3		3      		(middle)
  pattern.setNote(0, 106, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 107, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNote(0, 108, 0, Note(180)); // A#4  1+3:4		2
  pattern.setNote(0, 109, 0, Note(181)); // Bb4  1+4:5		1
  pattern.setNote(0, 110, 0, Note(182)); // A𝄪4  1+5:6	        1
  pattern.setNote(0, 111, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNote(0, 112, 0, Note(185)); // B#4  1+17:18	1
  pattern.setNote(0, 113, 0, Note(186)); // C-5  2:1		1
#endif
  
  pattern.setAnnotation(100, "7-limit scale (5 tone)");
  pattern.setNote(0, 101, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 102, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNote(0, 103, 0, Note(168)); // F-4  4:3		6
  pattern.setNote(0, 104, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 105, 0, Note(181)); // Bb4  1+4:5		8
  pattern.setNote(0, 106, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(108, "7-limit scale (6 tone)");
  pattern.setNote(0, 109, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 110, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNote(0, 111, 0, Note(168)); // F-4  4:3		6
  pattern.setNote(0, 112, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 113, 0, Note(181)); // Bb4 
  pattern.setNote(0, 114, 0, Note(183)); // B-4 	       
  pattern.setNote(0, 115, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(120, "7-limit scale (7 tone a)");
  pattern.setNote(0, 120, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 121, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNote(0, 122, 0, Note(168)); // F-4  4:3		6
  pattern.setNote(0, 123, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 124, 0, Note(180)); // A-4 
  pattern.setNote(0, 125, 0, Note(182)); //
  pattern.setNote(0, 126, 0, Note(184)); // Cb4
  pattern.setNote(0, 127, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(130, "7-limit scale (7 tone b)");
  pattern.setNote(0, 130, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 131, 0, Note(160)); // D-4  9:8		5
  pattern.setNote(0, 132, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNote(0, 133, 0, Note(168)); // F-4  4:3		6      	 
  pattern.setNote(0, 134, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 135, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNote(0, 136, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNote(0, 137, 0, Note(186)); // C-5  2:1		3
  
  pattern.setAnnotation(140, "7-limit scale (8 tone)");
  pattern.setNote(0, 140, 0, Note(155)); // C-4  1:1		0
  pattern.setNote(0, 141, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNote(0, 142, 0, Note(161)); // D#4  7:6	     	3 
  pattern.setNote(0, 143, 0, Note(165)); // E-4  5:4		4
  pattern.setNote(0, 144, 0, Note(168)); // F-4  4:3		3
  pattern.setNote(0, 145, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 146, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNote(0, 147, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNote(0, 148, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(150, "7-limit scale (10 tone)");
  pattern.setNote(0, 150, 0, Note(155)); // C-4  1:1		0
  // pattern.setNote(0, 141, 0, Note(156)); // Dbb4 36:35
  pattern.setNote(0, 151, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNote(0, 152, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNote(0, 153, 0, Note(162)); // D#4  7:6	     	2
  // 163 = 6:5
  pattern.setNote(0, 154, 0, Note(165)); // E-4  5:4		3
  pattern.setNote(0, 155, 0, Note(168)); // F-4  4:3		3      	 
  pattern.setNote(0, 156, 0, Note(173)); // G-4	 3:2		5
  pattern.setNote(0, 157, 0, Note(178)); // A-4  5:3		5
  // pattern.setNote(0, 147, 0, Note(180)); // A#4  1+3:4		3
  // pattern.setNote(0, 148, 0, Note(181)); // Bb4  1+4:5 		3     
  // pattern.setNote(0, 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNote(0, 158, 0, Note(181)); // Bb4  9:5		3
  pattern.setNote(0, 159, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNote(0, 150, 0, Note(184)); // C♭4  1+10:11	2
  // pattern.setNote(0, 150, 0, Note(185)); // B#4  1+17:18  
  pattern.setNote(0, 160, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(162, "7-limit scale (8 tone b)");
  pattern.setNote(0, 162, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 163, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNote(0, 164, 0, Note(162)); // D#4  7:6	     	2 
  pattern.setNote(0, 165, 0, Note(168)); // F-4  4:3		6
  pattern.setNote(0, 166, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 167, 0, Note(180)); // A-4 
  pattern.setNote(0, 168, 0, Note(182)); //
  pattern.setNote(0, 169, 0, Note(184)); // Cb4
  pattern.setNote(0, 170, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(180, "7-limit scale (11 tone )");
  pattern.setNote(0, 180, 0, Note(155)); // C-4  1:1		0
  // pattern.setNote(0, 141, 0, Note(156)); // Dbb4 36:35
  // 157 C#4
  pattern.setNote(0, 181, 0, Note(158)); // D♭4  16:15	        3
  // 159
  pattern.setNote(0, 182, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  // pattern.setNote(0, 153, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNote(0, 183, 0, Note(163)); // 6:5
  // 164
  pattern.setNote(0, 184, 0, Note(165)); // E-4  5:4		3
  // 166
  // 167
  pattern.setNote(0, 185, 0, Note(168)); // F-4  4:3		3
  // 169 11/8, 15/11, 26/19
  // 170 7/5, 45/32, 25/18
  // 171 10/7, 64/45, 36/25
  // 172
  pattern.setNote(0, 186, 0, Note(173)); // G-4	 3:2		5
  // 174
  // 175
  // 176
  // 177
  pattern.setNote(0, 187, 0, Note(178)); // A-4  1+2:3		5
  // 179
  // pattern.setNote(0, 147, 0, Note(180)); // A#4  1+3:4		3
  pattern.setNote(0, 188, 0, Note(181)); // Bb4  9:5		3
  // pattern.setNote(0, 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNote(0, 189, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNote(0, 190, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNote(0, 190, 0, Note(185)); // B#4  1+17:18  
  pattern.setNote(0, 191, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(200, "neutral minor scale (a)");
  pattern.setNote(0, 200, 0, Note(155)); // C   0
  pattern.setNote(0, 201, 0, Note(159)); // Cx4 4
  pattern.setNote(0, 202, 0, Note(163)); // Eb4 4
  pattern.setNote(0, 203, 0, Note(173)); // G-4 10
  pattern.setNote(0, 204, 0, Note(178)); // A-4
  pattern.setNote(0, 205, 0, Note(181)); // Bb4
  pattern.setNote(0, 206, 0, Note(186)); // C-5 13

  pattern.setAnnotation(210, "neutral minor scale (b)");
  pattern.setNote(0, 210, 0, Note(155)); // C   0
  pattern.setNote(0, 211, 0, Note(159)); // Cx4 4
  pattern.setNote(0, 212, 0, Note(163)); // Eb4 4
  pattern.setNote(0, 213, 0, Note(168)); // F-4 10
  pattern.setNote(0, 214, 0, Note(173)); // G-4 10
  pattern.setNote(0, 215, 0, Note(178)); // A-4
  pattern.setNote(0, 216, 0, Note(181)); // Bb4
  pattern.setNote(0, 217, 0, Note(186)); // C-5 13

#if 0
  pattern.setAnnotation(210, "7-limit scale ()");
  pattern.setNote(0, 211, 0, Note(155)); // C-4  1:1		0 
  pattern.setNote(0, 212, 0, Note(161)); // Ebb4  8:7	     	7
  pattern.setNote(0, 213, 0, Note(168)); // F-4  4:3		6
  pattern.setNote(0, 214, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNote(0, 215, 0, Note(181)); // Bb4 
  pattern.setNote(0, 216, 0, Note(183)); // B-4 	       
  pattern.setNote(0, 217, 0, Note(186)); // C-5  2:1		5
#endif
  
#if 0
  pattern.setAnnotation(165, "7-limit 7-note scale");
  pattern.setNote(0, 165, 0, Note(155, 0x2f)); // C-4
  pattern.setNote(0, 166, 0, Note(158)); // D♭4
  pattern.setNote(0, 167, 0, Note(161)); // E𝄫4 (8:7)
  pattern.setNote(0, 168, 0, Note(168)); // F-4
  pattern.setNote(0, 169, 0, Note(173)); // G-4
  pattern.setNote(0, 170, 0, Note(181)); // Bb-4
  pattern.setNote(0, 171, 0, Note(183)); // B-4
  pattern.setNote(0, 172, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setNote(0, 108, 0, Note(171)); // G♭4	 10:7	3
  pattern.setNote(0, 107, 0, Note(174)); // A𝄫4  32:21	3
  pattern.setNote(0, 108, 0, Note(177)); // A♭4  8:5	3
  pattern.setNote(0, 109, 0, Note(180)); // B𝄫4  1+5:7	3
  pattern.setNote(0, 110, 0, Note(183)); // A𝄪4  64:35	3
  pattern.setNote(0, 111, 0, Note(186)); // C-5  2:1	3
#endif
  
#if 0  
  pattern.setAnnotation(98, "Building a scale");
  pattern.setNote(0, 98, 0, Note(155, 0x30)); // C-4
  // pattern.setNote(0, 98, 1, Note(162, 0x30)); //
  // pattern.setNote(0, 98, 2, Note(168, 0x30)); // F-4

  // pattern.setNote(0, 99, 0, Note(160)); // (160 is possible)
  pattern.setNote(0, 99, 0, Note(162)); // D#4 (7:6)
  pattern.setNote(0, 100, 0, Note(168)); // F-4
  pattern.setNote(0, 101, 0, Note(173)); // G-4
  pattern.setNote(0, 102, 0, Note(181)); // (181 is possible)
  pattern.setNote(0, 103, 0, Note(183)); // 
  pattern.setNote(0, 104, 0, Note(185)); // 
  pattern.setNote(0, 105, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setAnnotation(50, "Harmonic seventh chord");
  pattern.setNote(0, 50, 0, Note(155)); // C
  pattern.setNote(0, 50, 1, Note(165)); // E
  pattern.setNote(0, 50, 2, Note(173)); // G
  pattern.setNote(0, 50, 3, Note(180)); // A♯

  pattern.setNote(0, 55, 0, Note(155)); // C
  pattern.setNote(0, 55, 1, Note(164)); // D𝄪4
  pattern.setNote(0, 55, 2, Note(173)); // G

  pattern.setNote(0, 60, 0, Note(155)); // C
  pattern.setNote(0, 60, 1, Note(165)); // E
  pattern.setNote(0, 60, 2, Note(172)); // F𝄪4
  pattern.setNote(0, 60, 3, Note(179)); // B𝄫4
#endif
  
#if 0

  pattern.setNote(0, 144, 0, Note(8*1, 7*1));
  pattern.setNote(0, 144, 1, Note(8*6, 7*5));
  pattern.setNote(0, 144, 2, Note(8*3, 7*2));

  pattern.setNote(0, 146, 0, Note(7*1, 6*1));
  pattern.setNote(0, 146, 1, Note(7*6, 6*5));
  pattern.setNote(0, 146, 2, Note(7*3, 6*2));

  pattern.setNote(0, 148, 0, Note(7*1, 6*1));
  pattern.setNote(0, 148, 1, Note(7*7, 6*6));
  pattern.setNote(0, 148, 2, Note(7*4, 6*3));

  pattern.setNote(0, 152, 0, Note(1, 1));
  pattern.setNote(0, 152, 1, Note(3, 2));
  pattern.setNote(0, 152, 2, Note(21, 12));
#endif
  
#if 0

#if 0
  // pattern.setNote(0, 0, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 1, 0, Note(3, 2)); // G-4
  pattern.setNote(0, 2, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 3, 0, Note(11, 10)); // ?
  // pattern.setNote(0, 4, 0, Note(2, 1)); // C-5
  pattern.setNote(0, 5, 0, Note(3, 2)); // G-4
  pattern.setNote(0, 6, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 7, 0, Note(1, 1)); // C-4
  
  pattern.setNote(0, 10, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 11, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 12, 0, Note(3, 2)); // G-4
  pattern.setNote(0, 13, 0, Note(2, 1)); // C-5

  // pattern.setNote(0, 16, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 17, 0, Note(11, 10)); // ?
  pattern.setNote(0, 18, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 19, 0, Note(3, 2)); // G-4
  // pattern.setNote(0, 20, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 20, 1, Note(11, 10)); // ?
  pattern.setNote(0, 20, 2, Note(6, 5)); // Eb
  pattern.setNote(0, 20, 3, Note(3, 2)); // G-4
  // pattern.setNote(0, 20, 4, Note(2, 1)); // C-5

  // pattern.setNote(0, 22, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 23, 0, Note(12, 11)); // ?
  pattern.setNote(0, 24, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 25, 0, Note(3, 2)); // G-4
  // pattern.setNote(0, 26, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 26, 1, Note(12, 11)); // ?
  pattern.setNote(0, 26, 2, Note(6, 5)); // Eb
  pattern.setNote(0, 26, 3, Note(3, 2)); // G-4
  // pattern.setNote(0, 26, 4, Note(2, 1)); // C-5

  pattern.setAnnotation(30, "7-limit triads");
  
  pattern.setNote(0, 30, 0, Note(1, 1)); // C-4
  // pattern.setNote(0, 30, 1, Note(7, 6)); // ?
  pattern.setNote(0, 30, 2, Note(7, 5)); // F
  // pattern.setNote(0, 30, 3, Note(2, 1)); // C-5

  // pattern.setNote(0, 32, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 32, 1, Note(8, 7)); // ?
  pattern.setNote(0, 32, 2, Note(7, 5)); // F
  // pattern.setNote(0, 32, 3, Note(2, 1)); // C-5

  // pattern.setNote(0, 34, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 34, 1, Note(8, 7)); // ?
  pattern.setNote(0, 34, 2, Note(4, 3)); // F
  //pattern.setNote(0, 34, 3, Note(2, 1)); // C-5
  
  // pattern.setNote(0, 36, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 36, 1, Note(7, 6)); // ?
  // pattern.setNote(0, 36, 2, Note(4, 3)); // F
  pattern.setNote(0, 36, 3, Note(9, 5)); //
  // pattern.setNote(0, 34, 4, Note(2, 1)); // C-5
    
  pattern.setAnnotation(60, "7-limit supermajor minor7 scale");

  pattern.setNote(0, 60, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 61, 0, Note(8, 7)); // ?
  pattern.setNote(0, 62, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 63, 0, Note(4, 3)); // F
  pattern.setNote(0, 64, 0, Note(3, 2)); // G
  pattern.setNote(0, 65, 0, Note(8, 5)); // Ab
  pattern.setNote(0, 66, 0, Note(9, 5)); // 
  pattern.setNote(0, 67, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(70, "harmonic minor scale");

  pattern.setNote(0, 70, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 71, 0, Note(9, 8)); // D
  pattern.setNote(0, 72, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 73, 0, Note(4, 3)); // F
  pattern.setNote(0, 74, 0, Note(3, 2)); // G
  pattern.setNote(0, 75, 0, Note(8, 5)); // Ab
  pattern.setNote(0, 76, 0, Note(7, 4)); // 
  pattern.setNote(0, 77, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(80, "melodic minor scale");

  pattern.setNote(0, 80, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 81, 0, Note(9, 8)); // D
  pattern.setNote(0, 82, 0, Note(6, 5)); // Eb
  pattern.setNote(0, 83, 0, Note(4, 3)); // F
  pattern.setNote(0, 84, 0, Note(3, 2)); // G
  pattern.setNote(0, 85, 0, Note(8, 5)); // Ab
  pattern.setNote(0, 86, 0, Note(9, 5)); // 
  pattern.setNote(0, 87, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(90, "subminor pentatonic scale");

  pattern.setNote(0, 90, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 91, 0, Note(7, 6)); // subminor third
  pattern.setNote(0, 92, 0, Note(4, 3)); // F
  pattern.setNote(0, 93, 0, Note(3, 2)); // G
  pattern.setNote(0, 94, 0, Note(9, 5)); // 
  pattern.setNote(0, 95, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(100, "septimal minor pentatonic scale");

  pattern.setNote(0, 100, 0, Note(1, 1)); // C-4
  pattern.setNote(0, 101, 0, Note(6, 5)); // minor thirds
  pattern.setNote(0, 102, 0, Note(4, 3)); // F
  pattern.setNote(0, 103, 0, Note(3, 2)); // G
  pattern.setNote(0, 104, 0, Note(7, 4)); // 
  pattern.setNote(0, 105, 0, Note(2, 1)); // C-5


  pattern.setNote(0, 112, 0, Note(7, 6));
  pattern.setNote(0, 112, 1, Note(4, 3));

  pattern.setNote(0, 114, 0, Note(8, 7));
  pattern.setNote(0, 114, 1, Note(4, 3));

  pattern.setNote(0, 116, 0, Note(7, 6));
  pattern.setNote(0, 116, 1, Note(1, 1));

  pattern.setNote(0, 118, 0, Note(8, 7));
  pattern.setNote(0, 118, 1, Note(1, 1));

  pattern.setNote(0, 120, 0, Note(7, 6));
  pattern.setNote(0, 120, 1, Note(3, 2));

  pattern.setNote(0, 122, 0, Note(8, 7));
  pattern.setNote(0, 122, 1, Note(3, 2));

  pattern.setNote(0, 124, 0, Note(7, 6));
  pattern.setNote(0, 124, 1, Note(5, 4));

  pattern.setNote(0, 126, 0, Note(8, 7));
  pattern.setNote(0, 126, 1, Note(5, 4));

  pattern.setNote(0, 128, 0, Note(7, 6));
  pattern.setNote(0, 128, 1, Note(6, 5));

  pattern.setNote(0, 130, 0, Note(8, 7));
  pattern.setNote(0, 130, 1, Note(6, 5));

#endif
#endif
  
  song->addPattern(pattern);  
    
  current_song = song;  
}

void
Controller::loadDemo() {
  auto song = make_shared<Song>(Tuning::TET12, 0);

  const unsigned char * song_data = tr;
  
  song->setTempo(*song_data++);
  song->setMasterVolume((*song_data++) / 127.0f);
  
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
  i1->setFilter(0, 0.08f, true);
  song->addInstrument(move(i1));

  auto i2 = make_unique<BasicInstrument>(WaveformType::SAW);
  i2->setName("drone3");
  i2->setADSR(255, 64, 0.25f, 0);
  i2->setFilter(0, 0.08f, true);
  song->addInstrument(move(i2));

  auto i3 = make_unique<BasicInstrument>(WaveformType::SINE);
  i3->setName("bass drum");
  i3->setADSR(0, 15, 0.0f, 0);
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
    track.setDetune((*song_data++ - 127) / 512.0);
    track.setAzimuth(*song_data++ / 360.0f - 180.0);
    float volume = *song_data++ / 127.0f;
    track.setVolume(volume);
    if (volume > 1.0f) {
      track.addEffect(make_unique<Distortion>(Distortion::CLIP, 1.0f, 0.0f));
    }
    song->getMasterTrack().addChild(track);
    
    for (size_t j = 0; ; j++) {
      int val = *song_data++;
      if (val == 255) break;
      int midi_note = val & 0x7f;
      bool has_accent = val & 0x80;
      if (midi_note != 0) track_notes[i][j] = Note(midi_note, has_accent ? 0x60 : 0x3f);
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

  auto sampleRate = synth->getSampleRate();

  auto oboe = make_unique<FMInstrument>(0.7, 1, 3, 4);
  oboe->setName("oboe");
  oboe->setADSR(2, 15, 0.5f, 10);
  // oboe->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // oboe->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // oboe->addEffect(make_unique<Reverb>(sampleRate, Reverb::DEFAULT)); // LARGEROOM1));
  // oboe->setFilter(100, 30);
  song->addInstrument(move(oboe));

  auto epiano = make_unique<BasicInstrument>(WaveformType::SAW);
  epiano->setName("Electric Piano");
  // epiano->setADSR(0, 20, 0.0f, 0);
  epiano->setADSR(0, 80, 0.0f, 0);
  epiano->setFilter(63 / 255.0f, 128 / 63.0f);
  song->addInstrument(move(epiano));

  auto test = make_unique<FMInstrument>(0, 1, 1);
  test->setName("test");
  test->setADSR(2, 15, 0.0f, 10);
  // test->addEffect(make_unique<Distortion>(Distortion::CLIP, 0.5f));
  // test->addEffect(make_unique<Chorus>(5.0f, 0.0f));
  test->setFilter(100, 30);
  song->addInstrument(move(test));
  
  auto harpsichord = make_unique<FMInstrument>(7.8, 3, 5, 4);
  harpsichord->setName("harpsichord");
  harpsichord->setADSR(2, 15, 0.0f, 10);
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
  song->getMasterTrack().addChild();
  
  current_song = song;
}

bool
Controller::sendCommand(const std::string & cmd) {  
  if (cmd == "new-song") {
    createNewSong();
  } else if (cmd == "save-song") {
    current_song->save("tmp.xml");
  } else {
    return false;
  }
  return true;
}

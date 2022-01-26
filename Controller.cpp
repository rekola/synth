#include "Controller.h"

#include "Song.h"
#include "SoundFont.h"
#include "InstrumentTrack.h"
#include "PlaybackControlEvent.h"

#include <cassert>
#include <iostream>
#include <unordered_map>

using namespace std;

Controller::Controller(ChannelConfiguration _channel_config) : channel_config(_channel_config) {
  instrument_provider.loadSoundFont("data/FluidR3_GM.sf2");
  instrument_provider.loadSoundFont("data/Essential Keys-sforzando-v9.6.sf2", false);
}

void
Controller::loadDemo2() {
  auto song = make_shared<Song>(Tuning::TET31, 0); // Key of C

#if 0
  auto fluid = make_unique<SoundFont>("data/Essential Keys-sforzando-v9.6.sf2");
  song->addInstruments(*fluid);
#endif
  
  auto & track = song->addChild(make_unique<InstrumentTrack>(0, 0));
  // track.addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // track.addEffect(make_unique<Filter>(63 / 255.0f, 128 / 63.0f, false));
  track.setVolume(0.1f);

  Pattern pattern(256);

  pattern.setNoteSwapped(0, 0, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(0, 0, 1, Note(163, 0x2f)); // Eb4
  pattern.setNoteSwapped(0, 0, 2, Note(173, 0x2f)); // G-4

  pattern.setNoteSwapped(0, 2, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(0, 2, 1, Note(163, 0x2f)); // Eb4
  pattern.setNoteSwapped(0, 2, 2, Note(170, 0x2f)); // F#4

  pattern.setNoteSwapped(0, 4, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(0, 4, 1, Note(161, 0x2f)); // Ebb4 (8:7)
  pattern.setNoteSwapped(0, 4, 2, Note(168, 0x2f)); // F-4

  pattern.setNoteSwapped(0, 6, 0, Note(155, 0x2f));
  pattern.setNoteSwapped(0, 6, 1, Note(161, 0x2f));
  pattern.setNoteSwapped(0, 6, 2, Note(168, 0x2f));


  pattern.setAnnotation(14, "neutral minor tetrad");
  pattern.setNoteSwapped(0, 14, 0, Note(155, 0x2f));
  pattern.setNoteSwapped(0, 14, 1, Note(173, 0x2f));
  pattern.setNoteSwapped(0, 14, 2, Note(163, 0x2f));
  pattern.setNoteSwapped(0, 14, 3, Note(159, 0x2f));

  pattern.setNoteSwapped(0, 16, 0, Note(155, 0x2f));  


  pattern.setNoteSwapped(0, 30, 0, Note(155)); // C-4
  pattern.setNoteSwapped(0, 31, 0, Note(162)); // D♯4
  pattern.setNoteSwapped(0, 32, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 33, 0, Note(168 + 7)); //

  pattern.setNoteSwapped(0, 34, 0, Note(162)); // D#4
  pattern.setNoteSwapped(0, 35, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 36, 0, Note(175)); // G#4
  pattern.setNoteSwapped(0, 37, 0, Note(193)); // D#5

  pattern.setNoteSwapped(0, 38, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 39, 0, Note(175)); // G#4
  pattern.setNoteSwapped(0, 40, 0, Note(181)); // B♭4 
  pattern.setNoteSwapped(0, 41, 0, Note(199)); // F-5

  pattern.setNoteSwapped(0, 42, 0, Note(162)); // D#4
  pattern.setNoteSwapped(0, 43, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 44, 0, Note(187)); // D𝄫5
  pattern.setNoteSwapped(0, 45, 0, Note(187 - 1)); //
  
  pattern.setAnnotation(55, "3-limit scale");
  pattern.setNoteSwapped(0, 56, 0, Note(155)); // C-4 1:1
  pattern.setNoteSwapped(0, 57, 0, Note(160)); // D-4 9:8
  pattern.setNoteSwapped(0, 58, 0, Note(168)); // F-4 4:3
  pattern.setNoteSwapped(0, 59, 0, Note(173)); // G-4 3:2
  pattern.setNoteSwapped(0, 60, 0, Note(181)); // Bb4 1+4:5
  pattern.setNoteSwapped(0, 61, 0, Note(186)); // C-5 2:1

  pattern.setAnnotation(70, "5-limit scale (Major)");
  pattern.setNoteSwapped(0, 70, 0, Note(155)); // C-4	10:10
  pattern.setNoteSwapped(0, 71, 0, Note(160)); // D-4	9:8
  pattern.setNoteSwapped(0, 72, 0, Note(165)); // E-4	5:4
  pattern.setNoteSwapped(0, 73, 0, Note(168)); // F-4	4:3
  pattern.setNoteSwapped(0, 74, 0, Note(173)); // G-4	1+1:2
  pattern.setNoteSwapped(0, 75, 0, Note(178)); // A-4  1+2:3
  // pattern.setNoteSwapped(0, 76, 0, Note(183)); // B-4	1+7:8
  pattern.setNoteSwapped(0, 76, 0, Note(181));
  pattern.setNoteSwapped(0, 77, 0, Note(186)); // C-5	1:1

  pattern.setAnnotation(80, "5-limit scale (Minor)");
  pattern.setNoteSwapped(0, 80, 0, Note(155)); // C-4  10:10
  pattern.setNoteSwapped(0, 81, 0, Note(160)); // D-4  9:8
  pattern.setNoteSwapped(0, 82, 0, Note(163)); // Eb4  6:5
  pattern.setNoteSwapped(0, 83, 0, Note(168)); // F-4	4:3
  pattern.setNoteSwapped(0, 84, 0, Note(173)); // G-4	1+1:2
  pattern.setNoteSwapped(0, 85, 0, Note(176)); // Ab4	1+3:5 ?
  pattern.setNoteSwapped(0, 86, 0, Note(181)); // Bb4  1+4:5 ?
  pattern.setNoteSwapped(0, 87, 0, Note(186)); // C-5  

  pattern.setAnnotation(90, "Sad scale");
  pattern.setNoteSwapped(0, 90, 0, Note("C-4", 0x40, 0, Tuning::TET31)); // C-4
  pattern.setNoteSwapped(0, 91, 0, Note("E𝄫4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(0, 92, 0, Note("D#4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(0, 93, 0, Note("F-4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(0, 97, 0, Note("C-5", 0x40, 0, Tuning::TET31));

  #if 0
  pattern.setAnnotation(100, "7-limit scale");
  pattern.setNoteSwapped(0, 100, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 101, 0, Note(158)); // D♭4  16:15		3
  pattern.setNoteSwapped(0, 102, 0, Note(161)); // E𝄫4  8:7		3
  pattern.setNoteSwapped(0, 103, 0, Note(163)); // E♭4  6:5		2
  pattern.setNoteSwapped(0, 104, 0, Note(165)); // E-4  5:4		2
  pattern.setNoteSwapped(0, 105, 0, Note(168)); // F-4  4:3		3      		(middle)
  pattern.setNoteSwapped(0, 106, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 107, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(0, 108, 0, Note(180)); // A#4  1+3:4		2
  pattern.setNoteSwapped(0, 109, 0, Note(181)); // Bb4  1+4:5		1
  pattern.setNoteSwapped(0, 110, 0, Note(182)); // A𝄪4  1+5:6	        1
  pattern.setNoteSwapped(0, 111, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNoteSwapped(0, 112, 0, Note(185)); // B#4  1+17:18	1
  pattern.setNoteSwapped(0, 113, 0, Note(186)); // C-5  2:1		1
#endif
  
  pattern.setAnnotation(100, "7-limit scale (5 tone)");
  pattern.setNoteSwapped(0, 101, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 102, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(0, 103, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(0, 104, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 105, 0, Note(181)); // Bb4  1+4:5		8
  pattern.setNoteSwapped(0, 106, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(108, "7-limit scale (6 tone)");
  pattern.setNoteSwapped(0, 109, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 110, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(0, 111, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(0, 112, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 113, 0, Note(181)); // Bb4 
  pattern.setNoteSwapped(0, 114, 0, Note(183)); // B-4 	       
  pattern.setNoteSwapped(0, 115, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(120, "7-limit scale (7 tone a)");
  pattern.setNoteSwapped(0, 120, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 121, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(0, 122, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(0, 123, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 124, 0, Note(180)); // A-4 
  pattern.setNoteSwapped(0, 125, 0, Note(182)); //
  pattern.setNoteSwapped(0, 126, 0, Note(184)); // Cb4
  pattern.setNoteSwapped(0, 127, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(130, "7-limit scale (7 tone b)");
  pattern.setNoteSwapped(0, 130, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 131, 0, Note(160)); // D-4  9:8		5
  pattern.setNoteSwapped(0, 132, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNoteSwapped(0, 133, 0, Note(168)); // F-4  4:3		6      	 
  pattern.setNoteSwapped(0, 134, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 135, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(0, 136, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNoteSwapped(0, 137, 0, Note(186)); // C-5  2:1		3
  
  pattern.setAnnotation(140, "7-limit scale (8 tone)");
  pattern.setNoteSwapped(0, 140, 0, Note(155)); // C-4  1:1		0
  pattern.setNoteSwapped(0, 141, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNoteSwapped(0, 142, 0, Note(161)); // D#4  7:6	     	3 
  pattern.setNoteSwapped(0, 143, 0, Note(165)); // E-4  5:4		4
  pattern.setNoteSwapped(0, 144, 0, Note(168)); // F-4  4:3		3
  pattern.setNoteSwapped(0, 145, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 146, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(0, 147, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNoteSwapped(0, 148, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(150, "7-limit scale (10 tone)");
  pattern.setNoteSwapped(0, 150, 0, Note(155)); // C-4  1:1		0
  // pattern.setNoteSwapped(0, 141, 0, Note(156)); // Dbb4 36:35
  pattern.setNoteSwapped(0, 151, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNoteSwapped(0, 152, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNoteSwapped(0, 153, 0, Note(162)); // D#4  7:6	     	2
  // 163 = 6:5
  pattern.setNoteSwapped(0, 154, 0, Note(165)); // E-4  5:4		3
  pattern.setNoteSwapped(0, 155, 0, Note(168)); // F-4  4:3		3      	 
  pattern.setNoteSwapped(0, 156, 0, Note(173)); // G-4	 3:2		5
  pattern.setNoteSwapped(0, 157, 0, Note(178)); // A-4  5:3		5
  // pattern.setNoteSwapped(0, 147, 0, Note(180)); // A#4  1+3:4		3
  // pattern.setNoteSwapped(0, 148, 0, Note(181)); // Bb4  1+4:5 		3     
  // pattern.setNoteSwapped(0, 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNoteSwapped(0, 158, 0, Note(181)); // Bb4  9:5		3
  pattern.setNoteSwapped(0, 159, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNoteSwapped(0, 150, 0, Note(184)); // C♭4  1+10:11	2
  // pattern.setNoteSwapped(0, 150, 0, Note(185)); // B#4  1+17:18  
  pattern.setNoteSwapped(0, 160, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(162, "7-limit scale (8 tone b)");
  pattern.setNoteSwapped(0, 162, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 163, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNoteSwapped(0, 164, 0, Note(162)); // D#4  7:6	     	2 
  pattern.setNoteSwapped(0, 165, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(0, 166, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 167, 0, Note(180)); // A-4 
  pattern.setNoteSwapped(0, 168, 0, Note(182)); //
  pattern.setNoteSwapped(0, 169, 0, Note(184)); // Cb4
  pattern.setNoteSwapped(0, 170, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(180, "7-limit scale (11 tone)");
  pattern.setNoteSwapped(0, 180, 0, Note(155)); // C-4  1:1		0
  // pattern.setNoteSwapped(0, 141, 0, Note(156)); // Dbb4 36:35
  // 157 C#4
  pattern.setNoteSwapped(0, 181, 0, Note(158)); // D♭4  16:15	        3
  // 159
  pattern.setNoteSwapped(0, 182, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  // pattern.setNoteSwapped(0, 153, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNoteSwapped(0, 183, 0, Note(163)); // 6:5
  // 164
  pattern.setNoteSwapped(0, 184, 0, Note(165)); // E-4  5:4		3
  // 166
  // 167
  pattern.setNoteSwapped(0, 185, 0, Note(168)); // F-4  4:3		3
  // 169 11/8, 15/11, 26/19
  // 170 7/5, 45/32, 25/18
  // 171 10/7, 64/45, 36/25
  // 172
  pattern.setNoteSwapped(0, 186, 0, Note(173)); // G-4	 3:2		5
  // 174
  // 175
  // 176
  // 177
  pattern.setNoteSwapped(0, 187, 0, Note(178)); // A-4  1+2:3		5
  // 179
  // pattern.setNoteSwapped(0, 147, 0, Note(180)); // A#4  1+3:4		3
  pattern.setNoteSwapped(0, 188, 0, Note(181)); // Bb4  9:5		3
  // pattern.setNoteSwapped(0, 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNoteSwapped(0, 189, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNoteSwapped(0, 190, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNoteSwapped(0, 190, 0, Note(185)); // B#4  1+17:18  
  pattern.setNoteSwapped(0, 191, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(200, "neutral minor scale (a)");
  pattern.setNoteSwapped(0, 200, 0, Note(155)); // C   0
  pattern.setNoteSwapped(0, 201, 0, Note(159)); // Cx4 4
  pattern.setNoteSwapped(0, 202, 0, Note(163)); // Eb4 4
  pattern.setNoteSwapped(0, 203, 0, Note(173)); // G-4 10
  pattern.setNoteSwapped(0, 204, 0, Note(178)); // A-4
  pattern.setNoteSwapped(0, 205, 0, Note(181)); // Bb4
  pattern.setNoteSwapped(0, 206, 0, Note(186)); // C-5 13

  pattern.setAnnotation(210, "neutral minor scale (b)");
  pattern.setNoteSwapped(0, 210, 0, Note(155)); // C   0
  pattern.setNoteSwapped(0, 211, 0, Note(159)); // Cx4 4
  pattern.setNoteSwapped(0, 212, 0, Note(163)); // Eb4 4
  pattern.setNoteSwapped(0, 213, 0, Note(168)); // F-4 10
  pattern.setNoteSwapped(0, 214, 0, Note(173)); // G-4 10
  pattern.setNoteSwapped(0, 215, 0, Note(178)); // A-4
  pattern.setNoteSwapped(0, 216, 0, Note(181)); // Bb4
  pattern.setNoteSwapped(0, 217, 0, Note(186)); // C-5 13

#if 0
  pattern.setAnnotation(210, "7-limit scale ()");
  pattern.setNoteSwapped(0, 211, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(0, 212, 0, Note(161)); // Ebb4  8:7	     	7
  pattern.setNoteSwapped(0, 213, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(0, 214, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(0, 215, 0, Note(181)); // Bb4 
  pattern.setNoteSwapped(0, 216, 0, Note(183)); // B-4 	       
  pattern.setNoteSwapped(0, 217, 0, Note(186)); // C-5  2:1		5
#endif
  
#if 0
  pattern.setAnnotation(165, "7-limit 7-note scale");
  pattern.setNoteSwapped(0, 165, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(0, 166, 0, Note(158)); // D♭4
  pattern.setNoteSwapped(0, 167, 0, Note(161)); // E𝄫4 (8:7)
  pattern.setNoteSwapped(0, 168, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 169, 0, Note(173)); // G-4
  pattern.setNoteSwapped(0, 170, 0, Note(181)); // Bb-4
  pattern.setNoteSwapped(0, 171, 0, Note(183)); // B-4
  pattern.setNoteSwapped(0, 172, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setNoteSwapped(0, 108, 0, Note(171)); // G♭4	 10:7	3
  pattern.setNoteSwapped(0, 107, 0, Note(174)); // A𝄫4  32:21	3
  pattern.setNoteSwapped(0, 108, 0, Note(177)); // A♭4  8:5	3
  pattern.setNoteSwapped(0, 109, 0, Note(180)); // B𝄫4  1+5:7	3
  pattern.setNoteSwapped(0, 110, 0, Note(183)); // A𝄪4  64:35	3
  pattern.setNoteSwapped(0, 111, 0, Note(186)); // C-5  2:1	3
#endif
  
#if 0  
  pattern.setAnnotation(98, "Building a scale");
  pattern.setNoteSwapped(0, 98, 0, Note(155, 0x30)); // C-4
  // pattern.setNoteSwapped(0, 98, 1, Note(162, 0x30)); //
  // pattern.setNoteSwapped(0, 98, 2, Note(168, 0x30)); // F-4

  // pattern.setNoteSwapped(0, 99, 0, Note(160)); // (160 is possible)
  pattern.setNoteSwapped(0, 99, 0, Note(162)); // D#4 (7:6)
  pattern.setNoteSwapped(0, 100, 0, Note(168)); // F-4
  pattern.setNoteSwapped(0, 101, 0, Note(173)); // G-4
  pattern.setNoteSwapped(0, 102, 0, Note(181)); // (181 is possible)
  pattern.setNoteSwapped(0, 103, 0, Note(183)); // 
  pattern.setNoteSwapped(0, 104, 0, Note(185)); // 
  pattern.setNoteSwapped(0, 105, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setAnnotation(50, "Harmonic seventh chord");
  pattern.setNoteSwapped(0, 50, 0, Note(155)); // C
  pattern.setNoteSwapped(0, 50, 1, Note(165)); // E
  pattern.setNoteSwapped(0, 50, 2, Note(173)); // G
  pattern.setNoteSwapped(0, 50, 3, Note(180)); // A♯

  pattern.setNoteSwapped(0, 55, 0, Note(155)); // C
  pattern.setNoteSwapped(0, 55, 1, Note(164)); // D𝄪4
  pattern.setNoteSwapped(0, 55, 2, Note(173)); // G

  pattern.setNoteSwapped(0, 60, 0, Note(155)); // C
  pattern.setNoteSwapped(0, 60, 1, Note(165)); // E
  pattern.setNoteSwapped(0, 60, 2, Note(172)); // F𝄪4
  pattern.setNoteSwapped(0, 60, 3, Note(179)); // B𝄫4
#endif
  
#if 0

  pattern.setNoteSwapped(0, 144, 0, Note(8*1, 7*1));
  pattern.setNoteSwapped(0, 144, 1, Note(8*6, 7*5));
  pattern.setNoteSwapped(0, 144, 2, Note(8*3, 7*2));

  pattern.setNoteSwapped(0, 146, 0, Note(7*1, 6*1));
  pattern.setNoteSwapped(0, 146, 1, Note(7*6, 6*5));
  pattern.setNoteSwapped(0, 146, 2, Note(7*3, 6*2));

  pattern.setNoteSwapped(0, 148, 0, Note(7*1, 6*1));
  pattern.setNoteSwapped(0, 148, 1, Note(7*7, 6*6));
  pattern.setNoteSwapped(0, 148, 2, Note(7*4, 6*3));

  pattern.setNoteSwapped(0, 152, 0, Note(1, 1));
  pattern.setNoteSwapped(0, 152, 1, Note(3, 2));
  pattern.setNoteSwapped(0, 152, 2, Note(21, 12));
#endif
  
#if 0

#if 0
  // pattern.setNoteSwapped(0, 0, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 1, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(0, 2, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 3, 0, Note(11, 10)); // ?
  // pattern.setNoteSwapped(0, 4, 0, Note(2, 1)); // C-5
  pattern.setNoteSwapped(0, 5, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(0, 6, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 7, 0, Note(1, 1)); // C-4
  
  pattern.setNoteSwapped(0, 10, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 11, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 12, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(0, 13, 0, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(0, 16, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 17, 0, Note(11, 10)); // ?
  pattern.setNoteSwapped(0, 18, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 19, 0, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(0, 20, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 20, 1, Note(11, 10)); // ?
  pattern.setNoteSwapped(0, 20, 2, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 20, 3, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(0, 20, 4, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(0, 22, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 23, 0, Note(12, 11)); // ?
  pattern.setNoteSwapped(0, 24, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 25, 0, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(0, 26, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 26, 1, Note(12, 11)); // ?
  pattern.setNoteSwapped(0, 26, 2, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 26, 3, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(0, 26, 4, Note(2, 1)); // C-5

  pattern.setAnnotation(30, "7-limit triads");
  
  pattern.setNoteSwapped(0, 30, 0, Note(1, 1)); // C-4
  // pattern.setNoteSwapped(0, 30, 1, Note(7, 6)); // ?
  pattern.setNoteSwapped(0, 30, 2, Note(7, 5)); // F
  // pattern.setNoteSwapped(0, 30, 3, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(0, 32, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 32, 1, Note(8, 7)); // ?
  pattern.setNoteSwapped(0, 32, 2, Note(7, 5)); // F
  // pattern.setNoteSwapped(0, 32, 3, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(0, 34, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 34, 1, Note(8, 7)); // ?
  pattern.setNoteSwapped(0, 34, 2, Note(4, 3)); // F
  //pattern.setNoteSwapped(0, 34, 3, Note(2, 1)); // C-5
  
  // pattern.setNoteSwapped(0, 36, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 36, 1, Note(7, 6)); // ?
  // pattern.setNoteSwapped(0, 36, 2, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 36, 3, Note(9, 5)); //
  // pattern.setNoteSwapped(0, 34, 4, Note(2, 1)); // C-5
    
  pattern.setAnnotation(60, "7-limit supermajor minor7 scale");

  pattern.setNoteSwapped(0, 60, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 61, 0, Note(8, 7)); // ?
  pattern.setNoteSwapped(0, 62, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 63, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 64, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(0, 65, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(0, 66, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(0, 67, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(70, "harmonic minor scale");

  pattern.setNoteSwapped(0, 70, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 71, 0, Note(9, 8)); // D
  pattern.setNoteSwapped(0, 72, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 73, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 74, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(0, 75, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(0, 76, 0, Note(7, 4)); // 
  pattern.setNoteSwapped(0, 77, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(80, "melodic minor scale");

  pattern.setNoteSwapped(0, 80, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 81, 0, Note(9, 8)); // D
  pattern.setNoteSwapped(0, 82, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(0, 83, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 84, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(0, 85, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(0, 86, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(0, 87, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(90, "subminor pentatonic scale");

  pattern.setNoteSwapped(0, 90, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 91, 0, Note(7, 6)); // subminor third
  pattern.setNoteSwapped(0, 92, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 93, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(0, 94, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(0, 95, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(100, "septimal minor pentatonic scale");

  pattern.setNoteSwapped(0, 100, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(0, 101, 0, Note(6, 5)); // minor thirds
  pattern.setNoteSwapped(0, 102, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(0, 103, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(0, 104, 0, Note(7, 4)); // 
  pattern.setNoteSwapped(0, 105, 0, Note(2, 1)); // C-5


  pattern.setNoteSwapped(0, 112, 0, Note(7, 6));
  pattern.setNoteSwapped(0, 112, 1, Note(4, 3));

  pattern.setNoteSwapped(0, 114, 0, Note(8, 7));
  pattern.setNoteSwapped(0, 114, 1, Note(4, 3));

  pattern.setNoteSwapped(0, 116, 0, Note(7, 6));
  pattern.setNoteSwapped(0, 116, 1, Note(1, 1));

  pattern.setNoteSwapped(0, 118, 0, Note(8, 7));
  pattern.setNoteSwapped(0, 118, 1, Note(1, 1));

  pattern.setNoteSwapped(0, 120, 0, Note(7, 6));
  pattern.setNoteSwapped(0, 120, 1, Note(3, 2));

  pattern.setNoteSwapped(0, 122, 0, Note(8, 7));
  pattern.setNoteSwapped(0, 122, 1, Note(3, 2));

  pattern.setNoteSwapped(0, 124, 0, Note(7, 6));
  pattern.setNoteSwapped(0, 124, 1, Note(5, 4));

  pattern.setNoteSwapped(0, 126, 0, Note(8, 7));
  pattern.setNoteSwapped(0, 126, 1, Note(5, 4));

  pattern.setNoteSwapped(0, 128, 0, Note(7, 6));
  pattern.setNoteSwapped(0, 128, 1, Note(6, 5));

  pattern.setNoteSwapped(0, 130, 0, Note(8, 7));
  pattern.setNoteSwapped(0, 130, 1, Note(6, 5));

#endif
#endif
  
  song->addPattern(pattern);  
    
  current_song = song;  
}

void
Controller::createNewSong() {
  auto song = make_shared<Song>();
  
  song->addChild(make_unique<InstrumentTrack>(-1, 0));
  song->addPattern(64);
  
  current_song = song;
}

bool
Controller::openSong(string filename) {  
  auto song = make_shared<Song>();
  if (!song->open(filename, instrument_provider)) {
    return false;
  }

  current_song = song;
  return true;
}

bool
Controller::sendCommand(const std::string & cmd) {  
  if (cmd == "new-song") {
    createNewSong();
  } else if (cmd == "save-song") {
    current_song->save("tmp.xml");
  } else if (cmd == "add-filter") {

  } else if (cmd == "transpose-down") {
    auto & info = getPlaybackInfo();
    auto & pattern = current_song->getPattern(info.getPatternIndex());
    pattern.transposeDown();
    current_song->incVersion();
  } else if (cmd == "transpose-up") {
    auto & info = getPlaybackInfo();
    auto & pattern = current_song->getPattern(info.getPatternIndex());
    pattern.transposeUp();
    current_song->incVersion();    
  } else {
    return false;
  }
  return true;
}

bool
Controller::togglePlaying() {
  auto info = getPlaybackInfo();
  info.is_playing = !info.is_playing;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(info.is_playing ? PlaybackControlEvent::PLAY : PlaybackControlEvent::STOP));
  setPlaybackInfo(info);
  return info.is_playing;
}

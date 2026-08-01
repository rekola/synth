#include "Controller.h"

#include "Song.h"
#include "SoundFont.h"
#include "InstrumentTrack.h"
#include "PlaybackControlEvent.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <filesystem>
#include <cstdlib>

#include <fmt/core.h>

using namespace std;

// Find a General MIDI SoundFont. Priority: the project-local data/ override,
// then well-known GM fonts by name (user dirs before system dirs;
// default-GM.sf2 is the Ubuntu alternatives-managed default), and finally the
// largest .sf2 found anywhere in the searched directories.
static string
findDefaultSoundFont() {
  namespace fs = std::filesystem;
  error_code ec;

  if (fs::is_regular_file("data/FluidR3_GM.sf2", ec)) return "data/FluidR3_GM.sf2";

  vector<fs::path> dirs;
  if (auto home = getenv("HOME")) {
    dirs.push_back(fs::path(home) / ".local/share/soundfonts");
    dirs.push_back(fs::path(home) / ".local/share/sounds/sf2");
  }
  dirs.push_back("/usr/share/soundfonts");
  dirs.push_back("/usr/share/sounds/sf2");

  const char * preferred[] = {
    "FluidR3_GM.sf2",
    "default-GM.sf2",
    "MuseScore_General.sf2",
    "GeneralUser GS.sf2",
    "TimGM6mb.sf2",
  };
  for (auto name : preferred) {
    for (auto & dir : dirs) {
      auto p = dir / name;
      if (fs::is_regular_file(p, ec)) return p.string();
    }
  }

  fs::path best;
  uintmax_t best_size = 0;
  for (auto & dir : dirs) {
    for (auto & entry : fs::directory_iterator(dir, ec)) {
      if (entry.path().extension() != ".sf2") continue;
      auto size = fs::file_size(entry.path(), ec);
      if (!ec && size > best_size) {
	best_size = size;
	best = entry.path();
      }
    }
  }
  return best.string();
}

Controller::Controller(ChannelConfiguration _channel_config) : channel_config(_channel_config) {
  auto soundfont = findDefaultSoundFont();
  if (!soundfont.empty()) {
    fmt::print(stderr, "Using SoundFont {}\n", soundfont);
    instrument_provider.loadSoundFont(soundfont);
  } else {
    fmt::print(stderr, "No GM SoundFont found; only built-in instruments available\n");
  }
  error_code ec;
  if (std::filesystem::is_regular_file("data/Essential Keys-sforzando-v9.6.sf2", ec)) {
    instrument_provider.loadSoundFont("data/Essential Keys-sforzando-v9.6.sf2", false);
  }

  // MixerFactory falls back to AMBISONIC_STEREO at actual mixer-
  // construction time if no SOFA file resolves (or libmysofa isn't
  // compiled in), so defaulting to AMBISONIC_BINAURAL here is safe even
  // when that fallback will immediately kick in - and harmless for a MONO
  // config too, since MixerFactory never attempts binaural for MONO
  // regardless of this setting.
#ifdef SYNTH_HAVE_LIBMYSOFA
  mixer_type_ = MixerType::AMBISONIC_BINAURAL;
#else
  mixer_type_ = MixerType::AMBISONIC_STEREO;
#endif
}

void
Controller::loadDemo2() {
  auto song = make_shared<Song>(Tuning::TET31, 0); // Key of C

#if 0
  auto fluid = make_unique<SoundFont>("data/Essential Keys-sforzando-v9.6.sf2");
  song->addInstruments(*fluid);
#endif
  
  auto & track = song->addTrack(make_unique<InstrumentTrack>(0));
  // track.addEffect(make_unique<Chorus>(5.0f, 0.0f));
  // track.addEffect(make_unique<Filter>(63 / 255.0f, 128 / 63.0f, false));

  Pattern pattern(256);

  pattern.setNoteSwapped(track.getInternalId(), 0, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 0, 1, Note(163, 0x2f)); // Eb4
  pattern.setNoteSwapped(track.getInternalId(), 0, 2, Note(173, 0x2f)); // G-4

  pattern.setNoteSwapped(track.getInternalId(), 2, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 2, 1, Note(163, 0x2f)); // Eb4
  pattern.setNoteSwapped(track.getInternalId(), 2, 2, Note(170, 0x2f)); // F#4

  pattern.setNoteSwapped(track.getInternalId(), 4, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 4, 1, Note(161, 0x2f)); // Ebb4 (8:7)
  pattern.setNoteSwapped(track.getInternalId(), 4, 2, Note(168, 0x2f)); // F-4

  pattern.setNoteSwapped(track.getInternalId(), 6, 0, Note(155, 0x2f));
  pattern.setNoteSwapped(track.getInternalId(), 6, 1, Note(161, 0x2f));
  pattern.setNoteSwapped(track.getInternalId(), 6, 2, Note(168, 0x2f));


  pattern.setAnnotation(14, "neutral minor tetrad");
  pattern.setNoteSwapped(track.getInternalId(), 14, 0, Note(155, 0x2f));
  pattern.setNoteSwapped(track.getInternalId(), 14, 1, Note(173, 0x2f));
  pattern.setNoteSwapped(track.getInternalId(), 14, 2, Note(163, 0x2f));
  pattern.setNoteSwapped(track.getInternalId(), 14, 3, Note(159, 0x2f));

  pattern.setNoteSwapped(track.getInternalId(), 16, 0, Note(155, 0x2f));  


  pattern.setNoteSwapped(track.getInternalId(), 30, 0, Note(155)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 31, 0, Note(162)); // D♯4
  pattern.setNoteSwapped(track.getInternalId(), 32, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 33, 0, Note(168 + 7)); //

  pattern.setNoteSwapped(track.getInternalId(), 34, 0, Note(162)); // D#4
  pattern.setNoteSwapped(track.getInternalId(), 35, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 36, 0, Note(175)); // G#4
  pattern.setNoteSwapped(track.getInternalId(), 37, 0, Note(193)); // D#5

  pattern.setNoteSwapped(track.getInternalId(), 38, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 39, 0, Note(175)); // G#4
  pattern.setNoteSwapped(track.getInternalId(), 40, 0, Note(181)); // B♭4 
  pattern.setNoteSwapped(track.getInternalId(), 41, 0, Note(199)); // F-5

  pattern.setNoteSwapped(track.getInternalId(), 42, 0, Note(162)); // D#4
  pattern.setNoteSwapped(track.getInternalId(), 43, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 44, 0, Note(187)); // D𝄫5
  pattern.setNoteSwapped(track.getInternalId(), 45, 0, Note(187 - 1)); //
  
  pattern.setAnnotation(55, "3-limit scale");
  pattern.setNoteSwapped(track.getInternalId(), 56, 0, Note(155)); // C-4 1:1
  pattern.setNoteSwapped(track.getInternalId(), 57, 0, Note(160)); // D-4 9:8
  pattern.setNoteSwapped(track.getInternalId(), 58, 0, Note(168)); // F-4 4:3
  pattern.setNoteSwapped(track.getInternalId(), 59, 0, Note(173)); // G-4 3:2
  pattern.setNoteSwapped(track.getInternalId(), 60, 0, Note(181)); // Bb4 1+4:5
  pattern.setNoteSwapped(track.getInternalId(), 61, 0, Note(186)); // C-5 2:1

  pattern.setAnnotation(70, "5-limit scale (Major)");
  pattern.setNoteSwapped(track.getInternalId(), 70, 0, Note(155)); // C-4	10:10
  pattern.setNoteSwapped(track.getInternalId(), 71, 0, Note(160)); // D-4	9:8
  pattern.setNoteSwapped(track.getInternalId(), 72, 0, Note(165)); // E-4	5:4
  pattern.setNoteSwapped(track.getInternalId(), 73, 0, Note(168)); // F-4	4:3
  pattern.setNoteSwapped(track.getInternalId(), 74, 0, Note(173)); // G-4	1+1:2
  pattern.setNoteSwapped(track.getInternalId(), 75, 0, Note(178)); // A-4  1+2:3
  // pattern.setNoteSwapped(track.getInternalId(), 76, 0, Note(183)); // B-4	1+7:8
  pattern.setNoteSwapped(track.getInternalId(), 76, 0, Note(181));
  pattern.setNoteSwapped(track.getInternalId(), 77, 0, Note(186)); // C-5	1:1

  pattern.setAnnotation(80, "5-limit scale (Minor)");
  pattern.setNoteSwapped(track.getInternalId(), 80, 0, Note(155)); // C-4  10:10
  pattern.setNoteSwapped(track.getInternalId(), 81, 0, Note(160)); // D-4  9:8
  pattern.setNoteSwapped(track.getInternalId(), 82, 0, Note(163)); // Eb4  6:5
  pattern.setNoteSwapped(track.getInternalId(), 83, 0, Note(168)); // F-4	4:3
  pattern.setNoteSwapped(track.getInternalId(), 84, 0, Note(173)); // G-4	1+1:2
  pattern.setNoteSwapped(track.getInternalId(), 85, 0, Note(176)); // Ab4	1+3:5 ?
  pattern.setNoteSwapped(track.getInternalId(), 86, 0, Note(181)); // Bb4  1+4:5 ?
  pattern.setNoteSwapped(track.getInternalId(), 87, 0, Note(186)); // C-5  

  pattern.setAnnotation(90, "Sad scale");
  pattern.setNoteSwapped(track.getInternalId(), 90, 0, Note("C-4", 0x40, 0, Tuning::TET31)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 91, 0, Note("E𝄫4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(track.getInternalId(), 92, 0, Note("D#4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(track.getInternalId(), 93, 0, Note("F-4", 0x40, 0, Tuning::TET31));
  pattern.setNoteSwapped(track.getInternalId(), 97, 0, Note("C-5", 0x40, 0, Tuning::TET31));

  #if 0
  pattern.setAnnotation(100, "7-limit scale");
  pattern.setNoteSwapped(track.getInternalId(), 100, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 101, 0, Note(158)); // D♭4  16:15		3
  pattern.setNoteSwapped(track.getInternalId(), 102, 0, Note(161)); // E𝄫4  8:7		3
  pattern.setNoteSwapped(track.getInternalId(), 103, 0, Note(163)); // E♭4  6:5		2
  pattern.setNoteSwapped(track.getInternalId(), 104, 0, Note(165)); // E-4  5:4		2
  pattern.setNoteSwapped(track.getInternalId(), 105, 0, Note(168)); // F-4  4:3		3      		(middle)
  pattern.setNoteSwapped(track.getInternalId(), 106, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 107, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(track.getInternalId(), 108, 0, Note(180)); // A#4  1+3:4		2
  pattern.setNoteSwapped(track.getInternalId(), 109, 0, Note(181)); // Bb4  1+4:5		1
  pattern.setNoteSwapped(track.getInternalId(), 110, 0, Note(182)); // A𝄪4  1+5:6	        1
  pattern.setNoteSwapped(track.getInternalId(), 111, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNoteSwapped(track.getInternalId(), 112, 0, Note(185)); // B#4  1+17:18	1
  pattern.setNoteSwapped(track.getInternalId(), 113, 0, Note(186)); // C-5  2:1		1
#endif
  
  pattern.setAnnotation(100, "7-limit scale (5 tone)");
  pattern.setNoteSwapped(track.getInternalId(), 101, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 102, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(track.getInternalId(), 103, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(track.getInternalId(), 104, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 105, 0, Note(181)); // Bb4  1+4:5		8
  pattern.setNoteSwapped(track.getInternalId(), 106, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(108, "7-limit scale (6 tone)");
  pattern.setNoteSwapped(track.getInternalId(), 109, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 110, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(track.getInternalId(), 111, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(track.getInternalId(), 112, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 113, 0, Note(181)); // Bb4 
  pattern.setNoteSwapped(track.getInternalId(), 114, 0, Note(183)); // B-4 	       
  pattern.setNoteSwapped(track.getInternalId(), 115, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(120, "7-limit scale (7 tone a)");
  pattern.setNoteSwapped(track.getInternalId(), 120, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 121, 0, Note(162)); // D#4  7:6	     	7
  pattern.setNoteSwapped(track.getInternalId(), 122, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(track.getInternalId(), 123, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 124, 0, Note(180)); // A-4 
  pattern.setNoteSwapped(track.getInternalId(), 125, 0, Note(182)); //
  pattern.setNoteSwapped(track.getInternalId(), 126, 0, Note(184)); // Cb4
  pattern.setNoteSwapped(track.getInternalId(), 127, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(130, "7-limit scale (7 tone b)");
  pattern.setNoteSwapped(track.getInternalId(), 130, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 131, 0, Note(160)); // D-4  9:8		5
  pattern.setNoteSwapped(track.getInternalId(), 132, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNoteSwapped(track.getInternalId(), 133, 0, Note(168)); // F-4  4:3		6      	 
  pattern.setNoteSwapped(track.getInternalId(), 134, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 135, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(track.getInternalId(), 136, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNoteSwapped(track.getInternalId(), 137, 0, Note(186)); // C-5  2:1		3
  
  pattern.setAnnotation(140, "7-limit scale (8 tone)");
  pattern.setNoteSwapped(track.getInternalId(), 140, 0, Note(155)); // C-4  1:1		0
  pattern.setNoteSwapped(track.getInternalId(), 141, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNoteSwapped(track.getInternalId(), 142, 0, Note(161)); // D#4  7:6	     	3 
  pattern.setNoteSwapped(track.getInternalId(), 143, 0, Note(165)); // E-4  5:4		4
  pattern.setNoteSwapped(track.getInternalId(), 144, 0, Note(168)); // F-4  4:3		3
  pattern.setNoteSwapped(track.getInternalId(), 145, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 146, 0, Note(178)); // A-4  1+2:3		5
  pattern.setNoteSwapped(track.getInternalId(), 147, 0, Note(181)); // Bb4  1+4:5		3
  pattern.setNoteSwapped(track.getInternalId(), 148, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(150, "7-limit scale (10 tone)");
  pattern.setNoteSwapped(track.getInternalId(), 150, 0, Note(155)); // C-4  1:1		0
  // pattern.setNoteSwapped(track.getInternalId(), 141, 0, Note(156)); // Dbb4 36:35
  pattern.setNoteSwapped(track.getInternalId(), 151, 0, Note(158)); // D♭4  16:15	        3
  pattern.setNoteSwapped(track.getInternalId(), 152, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNoteSwapped(track.getInternalId(), 153, 0, Note(162)); // D#4  7:6	     	2
  // 163 = 6:5
  pattern.setNoteSwapped(track.getInternalId(), 154, 0, Note(165)); // E-4  5:4		3
  pattern.setNoteSwapped(track.getInternalId(), 155, 0, Note(168)); // F-4  4:3		3      	 
  pattern.setNoteSwapped(track.getInternalId(), 156, 0, Note(173)); // G-4	 3:2		5
  pattern.setNoteSwapped(track.getInternalId(), 157, 0, Note(178)); // A-4  5:3		5
  // pattern.setNoteSwapped(track.getInternalId(), 147, 0, Note(180)); // A#4  1+3:4		3
  // pattern.setNoteSwapped(track.getInternalId(), 148, 0, Note(181)); // Bb4  1+4:5 		3     
  // pattern.setNoteSwapped(track.getInternalId(), 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNoteSwapped(track.getInternalId(), 158, 0, Note(181)); // Bb4  9:5		3
  pattern.setNoteSwapped(track.getInternalId(), 159, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNoteSwapped(track.getInternalId(), 150, 0, Note(184)); // C♭4  1+10:11	2
  // pattern.setNoteSwapped(track.getInternalId(), 150, 0, Note(185)); // B#4  1+17:18  
  pattern.setNoteSwapped(track.getInternalId(), 160, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(162, "7-limit scale (8 tone b)");
  pattern.setNoteSwapped(track.getInternalId(), 162, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 163, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  pattern.setNoteSwapped(track.getInternalId(), 164, 0, Note(162)); // D#4  7:6	     	2 
  pattern.setNoteSwapped(track.getInternalId(), 165, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(track.getInternalId(), 166, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 167, 0, Note(180)); // A-4 
  pattern.setNoteSwapped(track.getInternalId(), 168, 0, Note(182)); //
  pattern.setNoteSwapped(track.getInternalId(), 169, 0, Note(184)); // Cb4
  pattern.setNoteSwapped(track.getInternalId(), 170, 0, Note(186)); // C-5  2:1		5

  pattern.setAnnotation(180, "7-limit scale (11 tone)");
  pattern.setNoteSwapped(track.getInternalId(), 180, 0, Note(155)); // C-4  1:1		0
  // pattern.setNoteSwapped(track.getInternalId(), 141, 0, Note(156)); // Dbb4 36:35
  // 157 C#4
  pattern.setNoteSwapped(track.getInternalId(), 181, 0, Note(158)); // D♭4  16:15	        3
  // 159
  pattern.setNoteSwapped(track.getInternalId(), 182, 0, Note(160)); // D-4  9:8		2
  // 161 = 8:7
  // pattern.setNoteSwapped(track.getInternalId(), 153, 0, Note(162)); // D#4  7:6	     	2
  pattern.setNoteSwapped(track.getInternalId(), 183, 0, Note(163)); // 6:5
  // 164
  pattern.setNoteSwapped(track.getInternalId(), 184, 0, Note(165)); // E-4  5:4		3
  // 166
  // 167
  pattern.setNoteSwapped(track.getInternalId(), 185, 0, Note(168)); // F-4  4:3		3
  // 169 11/8, 15/11, 26/19
  // 170 7/5, 45/32, 25/18
  // 171 10/7, 64/45, 36/25
  // 172
  pattern.setNoteSwapped(track.getInternalId(), 186, 0, Note(173)); // G-4	 3:2		5
  // 174
  // 175
  // 176
  // 177
  pattern.setNoteSwapped(track.getInternalId(), 187, 0, Note(178)); // A-4  1+2:3		5
  // 179
  // pattern.setNoteSwapped(track.getInternalId(), 147, 0, Note(180)); // A#4  1+3:4		3
  pattern.setNoteSwapped(track.getInternalId(), 188, 0, Note(181)); // Bb4  9:5		3
  // pattern.setNoteSwapped(track.getInternalId(), 149, 0, Note(182)); // A𝄪4  1+5:6	        2
  pattern.setNoteSwapped(track.getInternalId(), 189, 0, Note(183)); // B-4  1+7:8	        2 
  // pattern.setNoteSwapped(track.getInternalId(), 190, 0, Note(184)); // C♭4  1+10:11	2
  pattern.setNoteSwapped(track.getInternalId(), 190, 0, Note(185)); // B#4  1+17:18  
  pattern.setNoteSwapped(track.getInternalId(), 191, 0, Note(186)); // C-5  2:1		3

  pattern.setAnnotation(200, "neutral minor scale (a)");
  pattern.setNoteSwapped(track.getInternalId(), 200, 0, Note(155)); // C   0
  pattern.setNoteSwapped(track.getInternalId(), 201, 0, Note(159)); // Cx4 4
  pattern.setNoteSwapped(track.getInternalId(), 202, 0, Note(163)); // Eb4 4
  pattern.setNoteSwapped(track.getInternalId(), 203, 0, Note(173)); // G-4 10
  pattern.setNoteSwapped(track.getInternalId(), 204, 0, Note(178)); // A-4
  pattern.setNoteSwapped(track.getInternalId(), 205, 0, Note(181)); // Bb4
  pattern.setNoteSwapped(track.getInternalId(), 206, 0, Note(186)); // C-5 13

  pattern.setAnnotation(210, "neutral minor scale (b)");
  pattern.setNoteSwapped(track.getInternalId(), 210, 0, Note(155)); // C   0
  pattern.setNoteSwapped(track.getInternalId(), 211, 0, Note(159)); // Cx4 4
  pattern.setNoteSwapped(track.getInternalId(), 212, 0, Note(163)); // Eb4 4
  pattern.setNoteSwapped(track.getInternalId(), 213, 0, Note(168)); // F-4 10
  pattern.setNoteSwapped(track.getInternalId(), 214, 0, Note(173)); // G-4 10
  pattern.setNoteSwapped(track.getInternalId(), 215, 0, Note(178)); // A-4
  pattern.setNoteSwapped(track.getInternalId(), 216, 0, Note(181)); // Bb4
  pattern.setNoteSwapped(track.getInternalId(), 217, 0, Note(186)); // C-5 13

#if 0
  pattern.setAnnotation(210, "7-limit scale ()");
  pattern.setNoteSwapped(track.getInternalId(), 211, 0, Note(155)); // C-4  1:1		0 
  pattern.setNoteSwapped(track.getInternalId(), 212, 0, Note(161)); // Ebb4  8:7	     	7
  pattern.setNoteSwapped(track.getInternalId(), 213, 0, Note(168)); // F-4  4:3		6
  pattern.setNoteSwapped(track.getInternalId(), 214, 0, Note(173)); // G-4	 1+1:2		5
  pattern.setNoteSwapped(track.getInternalId(), 215, 0, Note(181)); // Bb4 
  pattern.setNoteSwapped(track.getInternalId(), 216, 0, Note(183)); // B-4 	       
  pattern.setNoteSwapped(track.getInternalId(), 217, 0, Note(186)); // C-5  2:1		5
#endif
  
#if 0
  pattern.setAnnotation(165, "7-limit 7-note scale");
  pattern.setNoteSwapped(track.getInternalId(), 165, 0, Note(155, 0x2f)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 166, 0, Note(158)); // D♭4
  pattern.setNoteSwapped(track.getInternalId(), 167, 0, Note(161)); // E𝄫4 (8:7)
  pattern.setNoteSwapped(track.getInternalId(), 168, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 169, 0, Note(173)); // G-4
  pattern.setNoteSwapped(track.getInternalId(), 170, 0, Note(181)); // Bb-4
  pattern.setNoteSwapped(track.getInternalId(), 171, 0, Note(183)); // B-4
  pattern.setNoteSwapped(track.getInternalId(), 172, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setNoteSwapped(track.getInternalId(), 108, 0, Note(171)); // G♭4	 10:7	3
  pattern.setNoteSwapped(track.getInternalId(), 107, 0, Note(174)); // A𝄫4  32:21	3
  pattern.setNoteSwapped(track.getInternalId(), 108, 0, Note(177)); // A♭4  8:5	3
  pattern.setNoteSwapped(track.getInternalId(), 109, 0, Note(180)); // B𝄫4  1+5:7	3
  pattern.setNoteSwapped(track.getInternalId(), 110, 0, Note(183)); // A𝄪4  64:35	3
  pattern.setNoteSwapped(track.getInternalId(), 111, 0, Note(186)); // C-5  2:1	3
#endif
  
#if 0  
  pattern.setAnnotation(98, "Building a scale");
  pattern.setNoteSwapped(track.getInternalId(), 98, 0, Note(155, 0x30)); // C-4
  // pattern.setNoteSwapped(track.getInternalId(), 98, 1, Note(162, 0x30)); //
  // pattern.setNoteSwapped(track.getInternalId(), 98, 2, Note(168, 0x30)); // F-4

  // pattern.setNoteSwapped(track.getInternalId(), 99, 0, Note(160)); // (160 is possible)
  pattern.setNoteSwapped(track.getInternalId(), 99, 0, Note(162)); // D#4 (7:6)
  pattern.setNoteSwapped(track.getInternalId(), 100, 0, Note(168)); // F-4
  pattern.setNoteSwapped(track.getInternalId(), 101, 0, Note(173)); // G-4
  pattern.setNoteSwapped(track.getInternalId(), 102, 0, Note(181)); // (181 is possible)
  pattern.setNoteSwapped(track.getInternalId(), 103, 0, Note(183)); // 
  pattern.setNoteSwapped(track.getInternalId(), 104, 0, Note(185)); // 
  pattern.setNoteSwapped(track.getInternalId(), 105, 0, Note(186)); // C-5
#endif
  
#if 0
  pattern.setAnnotation(50, "Harmonic seventh chord");
  pattern.setNoteSwapped(track.getInternalId(), 50, 0, Note(155)); // C
  pattern.setNoteSwapped(track.getInternalId(), 50, 1, Note(165)); // E
  pattern.setNoteSwapped(track.getInternalId(), 50, 2, Note(173)); // G
  pattern.setNoteSwapped(track.getInternalId(), 50, 3, Note(180)); // A♯

  pattern.setNoteSwapped(track.getInternalId(), 55, 0, Note(155)); // C
  pattern.setNoteSwapped(track.getInternalId(), 55, 1, Note(164)); // D𝄪4
  pattern.setNoteSwapped(track.getInternalId(), 55, 2, Note(173)); // G

  pattern.setNoteSwapped(track.getInternalId(), 60, 0, Note(155)); // C
  pattern.setNoteSwapped(track.getInternalId(), 60, 1, Note(165)); // E
  pattern.setNoteSwapped(track.getInternalId(), 60, 2, Note(172)); // F𝄪4
  pattern.setNoteSwapped(track.getInternalId(), 60, 3, Note(179)); // B𝄫4
#endif
  
#if 0

  pattern.setNoteSwapped(track.getInternalId(), 144, 0, Note(8*1, 7*1));
  pattern.setNoteSwapped(track.getInternalId(), 144, 1, Note(8*6, 7*5));
  pattern.setNoteSwapped(track.getInternalId(), 144, 2, Note(8*3, 7*2));

  pattern.setNoteSwapped(track.getInternalId(), 146, 0, Note(7*1, 6*1));
  pattern.setNoteSwapped(track.getInternalId(), 146, 1, Note(7*6, 6*5));
  pattern.setNoteSwapped(track.getInternalId(), 146, 2, Note(7*3, 6*2));

  pattern.setNoteSwapped(track.getInternalId(), 148, 0, Note(7*1, 6*1));
  pattern.setNoteSwapped(track.getInternalId(), 148, 1, Note(7*7, 6*6));
  pattern.setNoteSwapped(track.getInternalId(), 148, 2, Note(7*4, 6*3));

  pattern.setNoteSwapped(track.getInternalId(), 152, 0, Note(1, 1));
  pattern.setNoteSwapped(track.getInternalId(), 152, 1, Note(3, 2));
  pattern.setNoteSwapped(track.getInternalId(), 152, 2, Note(21, 12));
#endif
  
#if 0

#if 0
  // pattern.setNoteSwapped(track.getInternalId(), 0, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 1, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(track.getInternalId(), 2, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 3, 0, Note(11, 10)); // ?
  // pattern.setNoteSwapped(track.getInternalId(), 4, 0, Note(2, 1)); // C-5
  pattern.setNoteSwapped(track.getInternalId(), 5, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(track.getInternalId(), 6, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 7, 0, Note(1, 1)); // C-4
  
  pattern.setNoteSwapped(track.getInternalId(), 10, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 11, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 12, 0, Note(3, 2)); // G-4
  pattern.setNoteSwapped(track.getInternalId(), 13, 0, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(track.getInternalId(), 16, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 17, 0, Note(11, 10)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 18, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 19, 0, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(track.getInternalId(), 20, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 20, 1, Note(11, 10)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 20, 2, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 20, 3, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(track.getInternalId(), 20, 4, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(track.getInternalId(), 22, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 23, 0, Note(12, 11)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 24, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 25, 0, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(track.getInternalId(), 26, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 26, 1, Note(12, 11)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 26, 2, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 26, 3, Note(3, 2)); // G-4
  // pattern.setNoteSwapped(track.getInternalId(), 26, 4, Note(2, 1)); // C-5

  pattern.setAnnotation(30, "7-limit triads");
  
  pattern.setNoteSwapped(track.getInternalId(), 30, 0, Note(1, 1)); // C-4
  // pattern.setNoteSwapped(track.getInternalId(), 30, 1, Note(7, 6)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 30, 2, Note(7, 5)); // F
  // pattern.setNoteSwapped(track.getInternalId(), 30, 3, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(track.getInternalId(), 32, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 32, 1, Note(8, 7)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 32, 2, Note(7, 5)); // F
  // pattern.setNoteSwapped(track.getInternalId(), 32, 3, Note(2, 1)); // C-5

  // pattern.setNoteSwapped(track.getInternalId(), 34, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 34, 1, Note(8, 7)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 34, 2, Note(4, 3)); // F
  //pattern.setNoteSwapped(track.getInternalId(), 34, 3, Note(2, 1)); // C-5
  
  // pattern.setNoteSwapped(track.getInternalId(), 36, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 36, 1, Note(7, 6)); // ?
  // pattern.setNoteSwapped(track.getInternalId(), 36, 2, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 36, 3, Note(9, 5)); //
  // pattern.setNoteSwapped(track.getInternalId(), 34, 4, Note(2, 1)); // C-5
    
  pattern.setAnnotation(60, "7-limit supermajor minor7 scale");

  pattern.setNoteSwapped(track.getInternalId(), 60, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 61, 0, Note(8, 7)); // ?
  pattern.setNoteSwapped(track.getInternalId(), 62, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 63, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 64, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(track.getInternalId(), 65, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(track.getInternalId(), 66, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(track.getInternalId(), 67, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(70, "harmonic minor scale");

  pattern.setNoteSwapped(track.getInternalId(), 70, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 71, 0, Note(9, 8)); // D
  pattern.setNoteSwapped(track.getInternalId(), 72, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 73, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 74, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(track.getInternalId(), 75, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(track.getInternalId(), 76, 0, Note(7, 4)); // 
  pattern.setNoteSwapped(track.getInternalId(), 77, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(80, "melodic minor scale");

  pattern.setNoteSwapped(track.getInternalId(), 80, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 81, 0, Note(9, 8)); // D
  pattern.setNoteSwapped(track.getInternalId(), 82, 0, Note(6, 5)); // Eb
  pattern.setNoteSwapped(track.getInternalId(), 83, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 84, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(track.getInternalId(), 85, 0, Note(8, 5)); // Ab
  pattern.setNoteSwapped(track.getInternalId(), 86, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(track.getInternalId(), 87, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(90, "subminor pentatonic scale");

  pattern.setNoteSwapped(track.getInternalId(), 90, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 91, 0, Note(7, 6)); // subminor third
  pattern.setNoteSwapped(track.getInternalId(), 92, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 93, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(track.getInternalId(), 94, 0, Note(9, 5)); // 
  pattern.setNoteSwapped(track.getInternalId(), 95, 0, Note(2, 1)); // C-5

  pattern.setAnnotation(100, "septimal minor pentatonic scale");

  pattern.setNoteSwapped(track.getInternalId(), 100, 0, Note(1, 1)); // C-4
  pattern.setNoteSwapped(track.getInternalId(), 101, 0, Note(6, 5)); // minor thirds
  pattern.setNoteSwapped(track.getInternalId(), 102, 0, Note(4, 3)); // F
  pattern.setNoteSwapped(track.getInternalId(), 103, 0, Note(3, 2)); // G
  pattern.setNoteSwapped(track.getInternalId(), 104, 0, Note(7, 4)); // 
  pattern.setNoteSwapped(track.getInternalId(), 105, 0, Note(2, 1)); // C-5


  pattern.setNoteSwapped(track.getInternalId(), 112, 0, Note(7, 6));
  pattern.setNoteSwapped(track.getInternalId(), 112, 1, Note(4, 3));

  pattern.setNoteSwapped(track.getInternalId(), 114, 0, Note(8, 7));
  pattern.setNoteSwapped(track.getInternalId(), 114, 1, Note(4, 3));

  pattern.setNoteSwapped(track.getInternalId(), 116, 0, Note(7, 6));
  pattern.setNoteSwapped(track.getInternalId(), 116, 1, Note(1, 1));

  pattern.setNoteSwapped(track.getInternalId(), 118, 0, Note(8, 7));
  pattern.setNoteSwapped(track.getInternalId(), 118, 1, Note(1, 1));

  pattern.setNoteSwapped(track.getInternalId(), 120, 0, Note(7, 6));
  pattern.setNoteSwapped(track.getInternalId(), 120, 1, Note(3, 2));

  pattern.setNoteSwapped(track.getInternalId(), 122, 0, Note(8, 7));
  pattern.setNoteSwapped(track.getInternalId(), 122, 1, Note(3, 2));

  pattern.setNoteSwapped(track.getInternalId(), 124, 0, Note(7, 6));
  pattern.setNoteSwapped(track.getInternalId(), 124, 1, Note(5, 4));

  pattern.setNoteSwapped(track.getInternalId(), 126, 0, Note(8, 7));
  pattern.setNoteSwapped(track.getInternalId(), 126, 1, Note(5, 4));

  pattern.setNoteSwapped(track.getInternalId(), 128, 0, Note(7, 6));
  pattern.setNoteSwapped(track.getInternalId(), 128, 1, Note(6, 5));

  pattern.setNoteSwapped(track.getInternalId(), 130, 0, Note(8, 7));
  pattern.setNoteSwapped(track.getInternalId(), 130, 1, Note(6, 5));

#endif
#endif
  
  song->addPattern(pattern);

  current_song = song;
  current_song_filename = "song.xml";
}

void
Controller::createNewSong() {
  auto song = make_shared<Song>();

  song->addTrack(make_unique<InstrumentTrack>(0));
  auto & pattern = song->addPattern(64);
  auto & section = song->addSection();
  section.addPattern(pattern.getInternalId());

  current_song = song;
  current_song_filename = "song.xml";
  // The Player/audio thread holds a Song& into whatever current_song used to
  // point to; it must be told to re-fetch before it dereferences the object
  // we just released ownership of (see Player::play()'s SONG_CHANGED handling).
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
}

bool
Controller::openSong(const string & filename) {
  auto song = make_shared<Song>();
  if (!song->open(filename, instrument_provider)) {
    return false;
  }

  current_song = song;
  current_song_filename = filename;
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SONG_CHANGED));
  return true;
}

bool
Controller::sendCommand(std::string_view cmd) {
  if (cmd == "save-song") {
    current_song->save(current_song_filename);
  } else if (cmd == "add-filter") {

  } else if (cmd == "toggle-mixer-type") {
    // "Bypass HRTF entirely" toggle: AMBISONIC_STEREO <-> AMBISONIC_BINAURAL.
    // A no-op for a MONO config - MixerFactory never attempts binaural
    // decoding there regardless of this setting (see MixerFactory.cpp) -
    // but harmless to still flip, so no type check is needed here either.
    mixer_type_ = (mixer_type_ == MixerType::AMBISONIC_BINAURAL) ? MixerType::AMBISONIC_STEREO : MixerType::AMBISONIC_BINAURAL;
    fmt::print(stderr, "Mixer type set to {}\n", to_string(mixer_type_));
    getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MIXER_CHANGED));
  } else if (command_fallback_) {
    return command_fallback_(cmd);
  } else {
    return false;
  }
  return true;
}

bool
Controller::togglePlaying() {
  auto info = getPlaybackInfo();
  info.setIsPlaying(!info.isPlaying());
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(info.isPlaying() ? PlaybackControlEvent::PLAY : PlaybackControlEvent::STOP));
  setPlaybackInfo(info);
  return info.isPlaying();
}

void
Controller::moveEditPosition(int delta_rows) {
  auto info = getPlaybackInfo();
  auto new_absolute = std::max(0, info.getAbsolutePosition() + delta_rows);
  auto [ pattern_idx, row_idx ] = getSong().normalizePosition(0, new_absolute);
  info.setAbsolutePos(new_absolute);
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, delta_rows));
}

void
Controller::setEditPosition(int absolute_row) {
  auto info = getPlaybackInfo();
  auto new_absolute = std::max(0, absolute_row);
  auto [ pattern_idx, row_idx ] = getSong().normalizePosition(0, new_absolute);
  info.setAbsolutePos(new_absolute);
  info.setPatternIdx(pattern_idx);
  info.setRowIdx(row_idx);
  info.setPositionEditSeq(++local_position_edit_seq_);
  setPlaybackInfo(info);
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_POSITION, absolute_row));
}

void
Controller::receivePlaybackSnapshot(const PlaybackInfo & info) {
  if (info.getPositionEditSeq() < local_position_edit_seq_) {
    // Stale: the audio thread took this snapshot before draining our most
    // recent moveEditPosition()/setEditPosition() control event. Keep
    // every other field from the real snapshot (voice counts, is_playing,
    // meters, ...) but preserve the local, already-correct edit-position
    // fields rather than regressing them - see this method's own doc
    // comment on Controller.h.
    auto merged = info;
    merged.setAbsolutePos(playback_info.getAbsolutePosition());
    merged.setPatternIdx(playback_info.getPatternIndex());
    merged.setRowIdx(playback_info.getRowIndex());
    merged.setPositionEditSeq(playback_info.getPositionEditSeq());
    setPlaybackInfo(merged);
  } else {
    setPlaybackInfo(info);
  }
}

static InstrumentTrack *
asInstrumentTrack(Track * track) {
  if (!track || (track->getType() != TrackType::INSTRUMENT_CONTROL && track->getType() != TrackType::PERCUSSION_CONTROL)) return nullptr;
  return &dynamic_cast<InstrumentTrack&>(*track);
}

bool
Controller::toggleTrackMuted(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setMuted(!instrument_track->isMuted());
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_MUTED, track_id, instrument_track->isMuted() ? 1 : 0));
  return instrument_track->isMuted();
}

bool
Controller::toggleTrackSolo(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return false;
  instrument_track->setSolo(!instrument_track->isSolo());
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SOLO, track_id, instrument_track->isSolo() ? 1 : 0));
  return instrument_track->isSolo();
}

void
Controller::setTrackSendA(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendA(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_A, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendB(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendB(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_B, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackSendMain(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setSendMain(value);
  current_song->incVersion();
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_SEND_MAIN, track_id, static_cast<int>(value * 1000.0f + 0.5f)));
}

void
Controller::setTrackAzimuth(int track_id, float value) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setAzimuth(value);
  current_song->incVersion();
  // Tenths-of-a-degree precision (-1800..1800) - the same "float via a
  // fixed-point int parameter" convention setTrackSendA/B use, just a
  // different scale/unit since this is degrees, not a 0-1 fraction.
  getPlaybackEventQueue().push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::SET_TRACK_AZIMUTH, track_id, static_cast<int>(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f))));
}

void
Controller::addNoteColumn(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() + 1);
  current_song->incVersion();
}

void
Controller::removeNoteColumn(int track_id) {
  auto instrument_track = asInstrumentTrack(current_song->getTrackByInternalId(track_id));
  if (!instrument_track) return;
  instrument_track->setMinNoteColumns(instrument_track->getMinNoteColumns() - 1);
  current_song->incVersion();
}

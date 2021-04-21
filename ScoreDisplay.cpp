#include "ScoreDisplay.h"

#include "UIInput.h"
#include "Synth.h"
#include "Controller.h"

#include <fmt/core.h>

using namespace std;
using namespace fmt;

ScoreDisplay::ScoreDisplay(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);
  
  midi_note_names[127] = "G-9";
  midi_note_names[126] = "F#9";
  midi_note_names[125] = "F-9";
  midi_note_names[124] = "E-9";
  midi_note_names[123] = "D#9";
  midi_note_names[122] = "D-9";
  midi_note_names[121] = "C#9";
  midi_note_names[120] = "C-9";
  midi_note_names[119] = "B-8";
  midi_note_names[118] = "A#8";
  midi_note_names[117] = "A-8";
  midi_note_names[116] = "G#8";
  midi_note_names[115] = "G-8";
  midi_note_names[114] = "F#8";
  midi_note_names[113] = "F-8";
  midi_note_names[112] = "E-8";
  midi_note_names[111] = "D#8";
  midi_note_names[110] = "D-8";
  midi_note_names[109] = "C#8";
  midi_note_names[108] = "C-8";
  midi_note_names[107] = "B-7";
  midi_note_names[106] = "A#7";
  midi_note_names[105] = "A-7";
  midi_note_names[104] = "G#7";
  midi_note_names[103] = "G-7";
  midi_note_names[102] = "F#7";
  midi_note_names[101] = "F-7";
  midi_note_names[100] = "E-7";
  midi_note_names[99] = "D#7";
  midi_note_names[98] = "D-7";
  midi_note_names[97] = "C#7";
  midi_note_names[96] = "C-7";
  midi_note_names[95] = "B-6";
  midi_note_names[94] = "A#6";
  midi_note_names[93] = "A-6";
  midi_note_names[92] = "G#6";
  midi_note_names[91] = "G-6";
  midi_note_names[90] = "F#6";
  midi_note_names[89] = "F-6";
  midi_note_names[88] = "E-6";
  midi_note_names[87] = "D#6";
  midi_note_names[86] = "D-6";
  midi_note_names[85] = "C#6";
  midi_note_names[84] = "C-6";
  midi_note_names[83] = "B-5";
  midi_note_names[82] = "A#5";
  midi_note_names[81] = "A-5";
  midi_note_names[80] = "G#5";
  midi_note_names[79] = "G-5";
  midi_note_names[78] = "F#5";
  midi_note_names[77] = "F-5";
  midi_note_names[76] = "E-5";
  midi_note_names[75] = "D#5";
  midi_note_names[74] = "D-5";
  midi_note_names[73] = "C#5";
  midi_note_names[72] = "C-5";
  midi_note_names[71] = "B-4";
  midi_note_names[70] = "A#4";
  midi_note_names[69] = "A-4";
  midi_note_names[68] = "G#4";
  midi_note_names[67] = "G-4";
  midi_note_names[66] = "F#4";
  midi_note_names[65] = "F-4";
  midi_note_names[64] = "E-4";
  midi_note_names[63] = "D#4";
  midi_note_names[62] = "D-4";
  midi_note_names[61] = "C#4";
  midi_note_names[60] = "C-4";
  midi_note_names[59] = "B-3";
  midi_note_names[58] = "A#3";
  midi_note_names[57] = "A-3";
  midi_note_names[56] = "G#3";
  midi_note_names[55] = "G-3";
  midi_note_names[54] = "F#3";
  midi_note_names[53] = "F-3";
  midi_note_names[52] = "E-3";
  midi_note_names[51] = "D#3";
  midi_note_names[50] = "D-3";
  midi_note_names[49] = "C#3";
  midi_note_names[48] = "C-3";
  midi_note_names[47] = "B-2";
  midi_note_names[46] = "A#2";
  midi_note_names[45] = "A-2";
  midi_note_names[44] = "G#2";
  midi_note_names[43] = "G-2";
  midi_note_names[42] = "F#2";
  midi_note_names[41] = "F-2";
  midi_note_names[40] = "E-2";
  midi_note_names[39] = "D#2";
  midi_note_names[38] = "D-2";
  midi_note_names[37] = "C#2";
  midi_note_names[36] = "C-2";
  midi_note_names[35] = "B-1";
  midi_note_names[34] = "A#1";
  midi_note_names[33] = "A-1";
  midi_note_names[32] = "G#1";
  midi_note_names[31] = "G-1";
  midi_note_names[30] = "F#1";
  midi_note_names[29] = "F-1";
  midi_note_names[28] = "E-1";
  midi_note_names[27] = "D#1";
  midi_note_names[26] = "D-1";
  midi_note_names[25] = "C#1";
  midi_note_names[24] = "C-1";
  midi_note_names[23] = "B-0";
  midi_note_names[22] = "A#0";
  midi_note_names[21] = "A-0";
}

bool
ScoreDisplay::render(bool refresh) {
  if (refresh) getPlane().drawBorder();
  
  bool render_all = refresh;
  auto & synth = getController().getSynth();
  size_t score_section = synth.getTrackPosition();
  size_t score_playing_row = synth.getPatternPosition();
  auto & current_section = getController().getSong().getSection(score_section);
  
  if (score_section != current_score_section) render_all = true;
  
  // bool cursor_row_changed = new_score_cursor_row != current_score_cursor_row;
  bool cursor_col_changed = new_score_cursor_col != current_score_cursor_col;
  
  // size_t old_cursor_row = current_score_cursor_row;
  
  // current_score_cursor_row = new_score_cursor_row;
  current_score_cursor_col = new_score_cursor_col;
  
  bool need_redraw = render_all;
  
  if (render_all) {
    auto [rows, cols] = getDim();
    for (int row = 0; row < rows && row < 32; row++) {
      renderRow(row, row == score_playing_row);
    }
  } else if (current_score_playing_row != score_playing_row) {
    renderRow(current_score_playing_row, false);
    renderRow(score_playing_row, true);
    need_redraw = true;
  } else if (cursor_col_changed || row_edited) {
    renderRow(score_playing_row, true);
    need_redraw = true;
  }
  
  current_score_section = score_section;
  current_score_playing_row = score_playing_row;
  row_edited = false;
  
  return need_redraw;
}

#define suppuabize(w) ((w) + 0x100000)                                                   
                                                                                         
// Special composed key definitions. These values are added to 0x100000.                 
#define NCKEY_INVALID suppuabize(0)                                                      
#define NCKEY_RESIZE  suppuabize(1) // generated internally in response to SIGWINCH      
#define NCKEY_UP      suppuabize(2)                                                      
#define NCKEY_RIGHT   suppuabize(3)                                                      
#define NCKEY_DOWN    suppuabize(4)                                                      
#define NCKEY_LEFT    suppuabize(5)

#define NCKEY_DEL   suppuabize(7)
#define NCKEY_BACKSPACE   suppuabize(8)

static inline int keyToNote(int key) {
  switch (key) {
  case NCKEY_DEL:
  case NCKEY_BACKSPACE:
    return 0;
    
  case 'z': return 48;
  case 's': return 49;
  case 'x': return 50;
    
  case 'q': return 60;
  case '2': return 61;
  case 'w': return 62;
  case '3': return 63;
  case 'e': return 64;
    
  case 'r': return 65;
  case '5': return 66;
  case 't': return 67;
  case '6': return 68;
  case 'y': return 69;
  case '7': return 70;
  case 'u': return 71;

  case 'i': return 72;
  case '9': return 73;
  case 'o': return 74;
  case '0': return 75;
  case 'p': return 76;
  }
  return -1;
}

bool
ScoreDisplay::offerInput(const UIInput & input) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & section = song.getSection(synth.getTrackPosition());
  size_t num_columns = section.getSequences().size();

  if (input.hasCtrl() && (input.getId() == 'a' || input.getId() == 'A')) {
    new_score_cursor_col = 0;
    return true;
  } else if (input.hasCtrl() && (input.getId() == 'e' || input.getId() == 'E')) {
    new_score_cursor_col = num_columns > 1 ? num_columns - 1 : 0; 
  } else if (input.getId() == NCKEY_LEFT) {
    if (new_score_cursor_col > 0) {
      new_score_cursor_col--;
    }
    return true;
  } else if (input.getId() == NCKEY_RIGHT) {
    if (new_score_cursor_col + 1 < num_columns) {
      new_score_cursor_col++;
    }
    return true;
  } else if (input.getId() == NCKEY_UP) {
    if (!synth.isPlaying()) synth.moveBackwards(song);
    return true;
  } else if (input.getId() == NCKEY_DOWN) {
    if (!synth.isPlaying()) synth.moveForward(song);
    return true;
  } else {
    int note = keyToNote(input.getId());
    if (note != -1) {
      auto & section = song.getSection(synth.getTrackPosition());
      auto & sequence = section.getSequence(current_score_cursor_col);
      song.getInstrument(sequence.getInstrumentId()).playNote(note);
      sequence.setNote(synth.getPatternPosition(), note);
      row_edited = true;
      if (input.getId() == NCKEY_BACKSPACE) synth.moveBackwards(song);
      else if (input.getId() != NCKEY_DEL) synth.moveForward(song);
      
      // setStatus(format("Playing {}", note));
      return true;
    }
  }
  
  return false;
}

void
ScoreDisplay::renderRow(size_t row, bool highlight) {
  auto & song = getController().getSong();
  auto & synth = getController().getSynth();
  auto & section = song.getSection(synth.getTrackPosition());
      
  for (int i = -1; i < (int)section.size(); i++) {
    if (highlight && i == current_score_cursor_col) {
      setFgColor(0x00, 0x00, 0x00);
      setBgColor(0xa0, 0xff, 0xa0);
    } else if (highlight) {
      setFgColor(0x80, 0xc0, 0x80);
      setBgColor(0x80, 0xa0, 0x80);
    } else if (row % 4 == 0) {
      setFgColor(0xe0, 0xe0, 0xe0);
      setBgColor(0x20, 0x20, 0x20);
    } else {
      setFgColor(0xa0, 0xa0, 0xa0);
      setBgColor(0x00, 0x00, 0x00);
    }

    if (i == -1) {
      auto s = format("{:02x}|", row);
      putstr(1 + row, 1, s.c_str());

    } else {
      auto & sequence = section.getSequence(i);
      int note = sequence.getNote(row);
      
      if (note != 0) {
	bool has_accent = note & 0x80;
	note &= 0x7f;
	
	string name = getNoteName(note) + " ";
	putstr(1 + row, 1 + 3 + i*4, name.c_str());
      } else {
	putstr(1 + row, 1 + 3 + i*4, "... ");
      }
    }
  }
}

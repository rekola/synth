#include "PatternEditor.h"

#include "InputEvent.h"
#include "SongState.h"
#include "Controller.h"
#include "StyleProvider.h"
#include "Tuner.h"
#include "InstrumentTrack.h"
#include "SampleTrack.h"

#include <string>
#include <fmt/core.h>

using namespace std;
using namespace fmt;

PatternEditor::PatternEditor(UIPlane & parent) : UIElement(parent) {
  // getPlane().setScrolling(true);  
}

static size_t get_depth(const Track & track) {
  size_t max_depth = 0;  
  for (auto & child : track.getChildren()) {
    auto d = get_depth(*child);
    if (d > max_depth) max_depth = d;
  }
  return 1 + max_depth;
}

static void get_root_track_ids(const Track & track, vector<int> & track_ids) {
  for (auto & child : track.getChildren()) {
    if (child->getType() == Track::INSTRUMENT || child->getType() == Track::SAMPLE) {
      track_ids.push_back(child->getId());
    } else {
      get_root_track_ids(*child, track_ids);
    }
  }
}

static void fill_track_widths(const Track & track, std::unordered_map<int, size_t> & widths) {
  for (auto & child : track.getChildren()) {
    if (child->getType() == Track::INSTRUMENT) {
      auto it = widths.find(child->getId());
      
      // at least one note column + one effect column
      if (it != widths.end()) it->second++;
      else widths[child->getId()] = 2;
    } else {
      fill_track_widths(*child, widths);
    }
  }
}

static void get_track_parents(Track & track, std::unordered_map<int, Track *> & parents) {
  for (auto & child : track.getChildren()) {
    parents[child->getId()] = &track;
    get_track_parents(*child, parents);
  }
}

bool
PatternEditor::render(const StyleProvider & styles, bool refresh) {
  bool render_all = refresh;
  auto & info = getController().getPlaybackInfo();
  auto score_pattern = info.getPatternIndex();
  size_t score_playing_row = info.getRowIndex();
  auto & song = getController().getSong();
  auto & current_pattern = song.getPattern(score_pattern);

  auto heading_height = get_depth(song);
  
  auto track_widths = current_pattern.getTrackWidths();
  fill_track_widths(song, track_widths);

  vector<int> track_ids;
  get_root_track_ids(song, track_ids);
			      
  size_t score_total_columns = 0;
  for (auto wd : track_widths) score_total_columns += wd.second;

  auto [rows, cols] = getDim();

  size_t new_scroll_row = current_scroll_row;
  if (score_playing_row < new_scroll_row) {
    new_scroll_row = score_playing_row;
  } else if (score_playing_row >= new_scroll_row + rows - 4) {
    new_scroll_row = score_playing_row - (rows - 4) + 1;
  }

  size_t new_scroll_track = current_scroll_track;
  if (new_score_cursor_track < new_scroll_track) {
    new_scroll_track = new_score_cursor_track;
  } else {
#if 0
    while ( 1 ) {
      size_t pos = 6;
      for (size_t i = new_scroll_track; i < tracks.size() && i <= new_score_cursor_track; i++) {
	auto id = tracks[i]->getId();
	auto it = track_widths.find(id);
	pos += (it != track_widths.end() ? it->second : 0) * 7 - 1;
      }
      if (pos >= (size_t)cols) {
	new_scroll_track++;
      } else {
	break;
      }
    }
#endif
  }

  if (score_pattern != current_score_pattern ||
      song.getVersion() != current_song_version ||
      score_total_columns != current_score_total_columns ||
      new_scroll_row != current_scroll_row ||
      new_scroll_track != current_scroll_track
      ) {
    render_all = true;
  }
  
  bool cursor_changed = new_score_cursor_track != current_score_cursor_track || new_score_cursor_col != current_score_cursor_col || new_score_cursor_subcol != current_score_cursor_subcol;
  
  // size_t old_cursor_row = current_score_cursor_row;
  
  // current_score_cursor_row = new_score_cursor_row;
  current_score_cursor_track = new_score_cursor_track;
  current_score_cursor_col = new_score_cursor_col;
  current_score_cursor_subcol = new_score_cursor_subcol;
  
  bool need_redraw = false;
  if (render_all) {
    current_scroll_row = new_scroll_row;
    current_scroll_track = new_scroll_track;
    
    erase();
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);
    fill();
    getPlane().drawBorder();
    
    renderHeading(styles, track_ids, track_widths);
    for (size_t row = 0; row < current_pattern.getNumRows(); row++) {
      renderRow(styles, heading_height, track_ids, track_widths, row, row == score_playing_row);
    }
    need_redraw = true;
  } else if (current_score_playing_row != score_playing_row) {
    renderHeading(styles, track_ids, track_widths);
    renderRow(styles, heading_height, track_ids, track_widths, current_score_playing_row, false);
    renderRow(styles, heading_height, track_ids, track_widths, score_playing_row, true);
    need_redraw = true;
  } else if (cursor_changed || row_edited) {
    renderRow(styles, heading_height, track_ids, track_widths, score_playing_row, true);
    need_redraw = true;
  }

  Tuning new_tuning = current_pattern.getTuning() != Tuning::INHERIT ? current_pattern.getTuning() : song.getTuning();
  int new_tempo = song.getTempo();
  int new_key = current_pattern.getKey() >= 0 ? current_pattern.getKey() : song.getKey();
  
  if (render_all || edit_step_size != new_edit_step_size || new_tuning != current_tuning || new_tempo != current_tempo || new_key != current_key || cursor_changed) {
    setFgColor(styles.window_border_color);
    setBgColor(styles.window_bg_color);

    std::string tuning = to_string(new_tuning);
    
    string key = new_key >= 0 ? Note::keyToString(new_tuning, new_key) : "?";
    
    putstr(rows - 1, cols - 32, format("{:2d} {} {} {} {}:{}", new_edit_step_size, tuning, key, new_tempo, current_score_cursor_track, current_score_cursor_col));
    
    edit_step_size = new_edit_step_size;
    current_tuning = new_tuning;
    current_tempo = new_tempo;
    current_key = new_key;
    
    need_redraw = true;
  }
  
  current_score_pattern = score_pattern;
  current_score_playing_row = score_playing_row;
  current_score_total_columns = score_total_columns;
  current_song_version = song.getVersion();
  row_edited = false;
  
  return need_redraw;
}

bool
PatternEditor::offerInput(const InputEvent & input) {
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();

  auto score_pattern = info.getPatternIndex();
  auto & current_pattern = song.getPattern(score_pattern);

  auto track_widths = current_pattern.getTrackWidths();
  fill_track_widths(song, track_widths);

  vector<int> track_ids;
  get_root_track_ids(song, track_ids);
  size_t num_tracks = track_ids.size();
  
  auto * current_track = song.getChildById(track_ids[current_score_cursor_track]);

  size_t num_columns = 0;
  if (current_track) {
    auto it0 = track_widths.find(current_track->getId());
    num_columns = it0 != track_widths.end() ? it0->second : 0;
  }
 
  auto & event_queue = getController().getPlaybackEventQueue();

  if (input.hasCtrl()) {
    if (input.getId() == 'r') {
      int track_id;
      auto sample = getController().startRecording();
      if (current_track && current_track->getType() == Track::SAMPLE) {
	SampleTrack & sample_track = dynamic_cast<SampleTrack&>(*current_track);
	sample_track.setSample(sample);
	track_id = sample_track.getId();
      } else {
	new_score_cursor_track = track_ids.size();
	auto & track = song.addChild(make_unique<SampleTrack>(sample));
	track_id = track.getId();
      }
      getController().setRecordingTrackId(track_id);
      song.incVersion();
    } else if (input.getId() == 'e') {
      getController().stopRecording();
    } else if (input.getId() == 'a' || input.getId() == 'A') {
      new_score_cursor_track = new_score_cursor_col = new_score_cursor_subcol = 0;
      return true;
    } else if (input.getId() == 'e' || input.getId() == 'E') {
      new_score_cursor_track = num_tracks > 1 ? num_tracks - 1 : 0;
      new_score_cursor_col = track_widths[new_score_cursor_track] - 1;
      new_score_cursor_subcol = 0;

      auto it = track_widths.find(track_ids[new_score_cursor_track]);
      new_score_cursor_col = it != track_widths.end() ? it->second - 1: 0;
      return true;
    } else if (input.getId() == 't' || input.getId() == 'T') {
      int instrument_id = 0; // pattern.getTracks().back().getInstrumentId();
      auto & track = song.addChild(make_unique<InstrumentTrack>(-1, instrument_id, 0.0f));
      song.incVersion();
    } else if (input.getId() == 'g' || input.getId() == 'G') {
      // create group
      return true;
    } else if (input.getId() == 'd' || input.getId() == 'D') {
      // duplicate track
      return true;
    } else if (input.getId() == '+') {
      edit_step_size++;
    } else if (input.getId() == '-') {
      if (edit_step_size > 0) edit_step_size--;
    } else if (input.hasAlt() && input.getId() == NCKEY_LEFT) {
      // move selected track to left
      return true;
    } else if (input.hasAlt() && input.getId() == NCKEY_RIGHT) {
      // move selected track to right
      return true;
    } else if (input.getId() == NCKEY_LEFT || input.getId() == 'p') {
      Track * track = song.getChildById(track_ids[current_score_cursor_track]);
      if (track && track->getType() == Track::INSTRUMENT) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	if (instrument_track.getInstrumentId() > 0) {
	  instrument_track.setInstrumentId(instrument_track.getInstrumentId() - 1);
	  song.incVersion();
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CLEAR_VOICES, current_score_cursor_track));
	}
      }
      return true;
    } else if (input.getId() == NCKEY_RIGHT || input.getId() == 'i' || input.getId() == 'i' || input.getId() == 'o') {
      Track * track = song.getChildById(track_ids[current_score_cursor_track]);
      if (track && track->getType() == Track::INSTRUMENT) {
	auto & instrument_track = dynamic_cast<InstrumentTrack&>(*track);
	auto & instruments = song.getInstruments();
	if (instrument_track.getInstrumentId() + 1 < instruments.size()) {
	  instrument_track.setInstrumentId(instrument_track.getInstrumentId() + 1);
	  song.incVersion();
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::CLEAR_VOICES, current_score_cursor_track));
	}
      }
      return true;
    } else if (input.getId() == '\\') {
      Track * track = song.getChildById(track_ids[current_score_cursor_track]);
      if (track) track->setSolo(true);
    } else if (input.hasShift()) {
      if (input.getId() == 't' || input.getId() == 'T') {
	// delete track
	return true;
      }
    } else {
      return false;
    }
  } else if (input.getId() == NCKEY_LEFT) {
    if (new_score_cursor_col > 0) {
      new_score_cursor_col--;
      new_score_cursor_subcol = 0;
    } else if (new_score_cursor_track > 0) {
      new_score_cursor_track--;
      new_score_cursor_subcol = 0;

      auto it = track_widths.find(track_ids[new_score_cursor_track]);
      new_score_cursor_col = it != track_widths.end() ? it->second - 1 : 0;
    }
    return true;
  } else if (input.getId() == NCKEY_RIGHT) {
    if (new_score_cursor_col + 1 < num_columns) {
      new_score_cursor_col++;
      new_score_cursor_subcol = 0;
    } else if (new_score_cursor_track + 1 < num_tracks) {
      new_score_cursor_track++;
      new_score_cursor_col = 0;
      new_score_cursor_subcol = 0;
    }
    return true;
  } else if (input.getId() == NCKEY_UP || input.getId() == NCKEY_BUTTON4) {
    if (!info.isPlaying()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, -1));
      // new_score_cursor_subcol = 0;
    }
    return true;
  } else if (input.getId() == NCKEY_DOWN || input.getId() == NCKEY_BUTTON5) {
    if (!info.isPlaying()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, 1));
      // new_score_cursor_subcol = 0;
    }
    return true;
  } else if (input.getId() == NCKEY_PGUP) {
    if (!info.isPlaying()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, -16));
      // new_score_cursor_subcol = 0;
    }
    return true;    
  } else if (input.getId() == NCKEY_PGDOWN) { // scrollwheel down
    if (!info.isPlaying()) {
      event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, 16));
      // new_score_cursor_subcol = 0;
    }
    return true;
  } else if (input.getId() == '\\') {
    Track * track = song.getChildById(track_ids[current_score_cursor_track]);
    if (track) track->setMute(true);
  } else if (input.getId() == '\t') {
    if (new_score_cursor_col + 1 == num_columns) { // effect
      new_score_cursor_subcol = (new_score_cursor_subcol + 1) % 4;
    } else {
      new_score_cursor_subcol = (new_score_cursor_subcol + 1) % 2;
    }
  } else {
    auto & pattern = song.getPattern(info.getPatternIndex());
    int track_id = track_ids[new_score_cursor_track];
    
    if (new_score_cursor_col + 1 == num_columns) {
      if ((input.getId() >= 'a' && input.getId() <= 'z') || (input.getId() >= '0' && input.getId() <= '9') || input.getId() == '-') {	
	auto command = pattern.getCommand(info.getRowIndex(), track_id);
	command.updateData(new_score_cursor_subcol, toupper(input.getId()));
	pattern.setCommand(info.getRowIndex(), track_id, command);
	row_edited = true;
	
	if (new_score_cursor_subcol + 1 < 4) {
	  new_score_cursor_subcol++;
	} else if (new_score_cursor_track + 1 < num_tracks) {
	  new_score_cursor_track++;
	  new_score_cursor_col = 0;
	  new_score_cursor_subcol = 0;
	}
      }
      return true;
    } else {
      Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
      int midi_note = input.toMidiNote(tuning);
      bool is_delete = input.getId() == NCKEY_DEL || input.getId() == NCKEY_BACKSPACE;
      if (is_delete || midi_note >= 0) {
	if (is_delete) {
	  pattern.deleteNote(info.getRowIndex(), track_id, current_score_cursor_col);
	  event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, current_score_cursor_col));
	} else {
	  Note note(midi_note);
	  
	  size_t note_column = 0;
	  if (input.hasShift()) {
	    note_column = pattern.pushNote(info.getRowIndex(), track_id, note);
	  } else {
	    pattern.setNote(info.getRowIndex(), track_id, current_score_cursor_col, note);
	  }
	  	  
	  if (note.isOff()) {
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::STOP_NOTE, track_id, note_column));
	  } else {
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::PLAY_NOTE, track_id, note_column, midi_note));
	  }
	}

	row_edited = true;
	
	if (!info.isPlaying()) {
	  int n = 0;
	  if (input.getId() == NCKEY_BACKSPACE) n = -edit_step_size;
	  else if (input.getId() != NCKEY_DEL) n = edit_step_size;
	  if (n) {
	    event_queue.push(make_unique<PlaybackControlEvent>(PlaybackControlEvent::MOVE_POSITION, n));
	  }
	}
      
	return true;
      }
    }
  }
  
  return false;
}

void
PatternEditor::renderHeading(const StyleProvider & styles, const std::vector<int> & track_ids, const std::unordered_map<int, size_t> & track_widths0) {
  auto & song = getController().getSong();
  auto & info = getController().getPlaybackInfo();
  auto & pattern = song.getPattern(info.getPatternIndex());

  auto [rows, cols] = getDim();

  unordered_map<int, Track *> track_parents;
  get_track_parents(song, track_parents);

  auto heading_height = get_depth(song);

  string padding;
  for (size_t i = 1; i < cols - 1; i++) padding += ' ';

  setBgColor(styles.window_bg_color);
  for (size_t i = 0; i < heading_height; i++) {
    putstr(1 + i, 1, padding);
  }
  
  auto & instruments = song.getInstruments();

  for (size_t level = 0; level < heading_height - 1; level++) {
    vector<Track *> tracks;
    vector<size_t> track_widths;

    for (size_t i = 0; i < track_ids.size(); i++) {
      int track_id = track_ids[i];
      auto track = song.getChildById(track_id);
      for (size_t k = 0; k < level && track; k++) {
	track = track_parents[track->getId()];
	if (track == &song) track = nullptr;
      }
      auto it = track_widths0.find(track_id);
      auto w = (it != track_widths0.end() ? it->second : 0) * 7 - 1;

      if (!tracks.empty() && tracks.back() == track) {
	track_widths.back() += w;
      } else {
	tracks.push_back(track);
	track_widths.push_back(w);
      }
    }
    
    size_t current_pos = 6;
    for (size_t i = 0; i < tracks.size(); i++) {
      if (i < current_scroll_track) continue;
      if (current_pos >= cols) break;

      auto track = tracks[i];
      auto actual_width = track_widths[i];

      if (track) {
	if (level == 0) {
	  setFgColor(0x00, 0x00, 0x00);
	  setBgColor(0xf0, 0x80, 0x10);

	  string name = !track->getName().empty() ? track->getName() : format("Trk {:02d}", track->getId());
	  if (name.size() > actual_width - 1) name.erase(actual_width - 1);
	  else {
	    while (name.size() < actual_width - 1) name += ' ';
	  }
	  name += "│";
	  putstr(1 + heading_height - 2 - level, current_pos, name);
	  
	  setFgColor(0xf0, 0xf0, 0xf0);
	  setBgColor(styles.window_bg_color);
	  
	  string instrument_name;
	  if (track->getType() == Track::SAMPLE) {
	    instrument_name = "Sample";
	  } else if (track->getType() == Track::INSTRUMENT) {
	    auto & instrument_track = dynamic_cast<const InstrumentTrack&>(*track);
	    if (instrument_track.getInstrumentId() >= 0 && instrument_track.getInstrumentId() < instruments.size()) {
	      instrument_name = instruments[instrument_track.getInstrumentId()]->getName();
	    }
	  }
	  if (instrument_name.size() > actual_width - 1) instrument_name.erase(actual_width - 1);
	  putstr(2 + heading_height - 2 - level, current_pos, instrument_name);
	} else {	  
	  string name = track->getElementName();
	  auto & track_info = info.getTrackInfo(track->getId());
	  
	  if (name.size() > actual_width - 4) name.erase(actual_width - 4);
	  else {
	    while (name.size() < actual_width - 4) name += ' ';
	  }
	  name += "│";

	  setBgColor(0x50, 0x50, 0x60);
	  setFgColor(0x10, 0xe0, 0x40);
	  putstr(1 + heading_height - 2 - level, current_pos, track_info.isActive() ? " • " : "   ");
	  
	  setFgColor(0x00, 0x00, 0x00);
	  putstr(1 + heading_height - 2 - level, current_pos + 3, name);
	}
      }
      
      current_pos += actual_width;
    }
  }
}

void
PatternEditor::renderRow(const StyleProvider & styles, size_t heading_height, const std::vector<int> & track_ids, const std::unordered_map<int, size_t> & track_widths, size_t row, bool highlight) {
  auto [rows, cols] = getDim();

  if (row >= current_scroll_row && row < current_scroll_row + rows - 4) {
    auto & song = getController().getSong();
    auto & info = getController().getPlaybackInfo();
    auto & pattern = song.getPattern(info.getPatternIndex());
    
    string padding;
    for (size_t i = 1; i < cols - 1; i++) padding += ' ';
    
    setBgColor(styles.window_bg_color);
    putstr(1 + heading_height + row - current_scroll_row, 1, padding);

    Tuning tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
    
    size_t current_pos = 1;
    for (int i = -1; i < (int)track_ids.size(); i++) {
      if (i >= 0 && (size_t)i < current_scroll_track) continue;
      if (current_pos >= cols) break;
          
      UIColor fg, bg, cell_fg, cell_bg;
      
      if (highlight) {
	fg = UIColor("#80c080");
	bg = UIColor("#80a080");
      } else if (row % 4 == 0) {
	fg = styles.window_accent_fg_color;
	bg = styles.window_accent_bg_color;
      } else {
	fg = styles.window_fg_color;
	bg = styles.window_bg_color;
      }
            
      if (i == -1) {
	setFgColor(fg);
	setBgColor(bg);
	
	putstr(1 + heading_height + row - current_scroll_row, current_pos, format(" {:02x} ", row));
	
	setFgColor(styles.window_border_color);
	setBgColor(styles.window_bg_color);
	
	putstr(1 + heading_height + row - current_scroll_row, current_pos + 4, "│");
	
	current_pos += 5;
      } else {
	auto track_id = track_ids[i];
	auto & notes = pattern.getNotes(row, track_id);
	auto & command = pattern.getCommand(row, track_id);
	auto it = track_widths.find(track_id);
	size_t track_columns = it != track_widths.end() ? it->second : 0;
	auto track = song.getChildById(track_id);
	
	for (size_t k = 0; k < track_columns; k++) {      
	  if (k == track_columns - 1) { // effect column
	    putstr(1 + heading_height + row - current_scroll_row, current_pos, " ");
	    current_pos++;
	    for (size_t l = 0; l < 4; l++) {
	      if (highlight && i == (int)current_score_cursor_track && k == current_score_cursor_col && l == current_score_cursor_subcol) {
		cell_fg = UIColor("#000000");
		cell_bg = UIColor("#a0ffa0");
	      } else {
		cell_fg = fg;
		cell_bg = bg;
	      }
	      setFgColor(cell_fg);
	      setBgColor(cell_bg);

	      string s;
	      s += command.data()[l];
	      putstr(1 + heading_height + row - current_scroll_row, current_pos, s);
	      current_pos++;
	    }
	    
	    setFgColor(styles.window_border_color);
	    setBgColor(bg);
	    putstr(1 + heading_height + row - current_scroll_row, current_pos, "│");
	    current_pos++;
	  } else if (track && track->getType() == Track::SAMPLE) {
	    if (highlight && i == (int)current_score_cursor_track && k == current_score_cursor_col) {
	      cell_fg = UIColor("#000000");
	      cell_bg = UIColor("#a0ffa0");
	    } else {
	      cell_fg = fg;
	      cell_bg = bg;
	    }

	    if (k < notes.size() && notes[k].isDefined()) {
	      auto & note = notes[k];
	      putstr(1 + heading_height + row - current_scroll_row, current_pos, "xxxxxx");
	    } else {
	      putstr(1 + heading_height + row - current_scroll_row, current_pos, "      ");	    
	    }
	  } else {
	    for (size_t l = 0; l < 2; l++) {
	      if (highlight && i == (int)current_score_cursor_track && k == current_score_cursor_col && l == current_score_cursor_subcol) {
		cell_fg = UIColor("#000000");
		cell_bg = UIColor("#a0ffa0");
	      } else {
		cell_fg = fg;
		cell_bg = bg;
	      }
	      setFgColor(cell_fg);
	      setBgColor(cell_bg);
	      
	      string s;
	      if (k < notes.size() && notes[k].isDefined()) {
		auto & note = notes[k];
		if (l == 0) {
		  s = note.toString(tuning);
		} else {
		  s = format(" {:02x}", note.getVelocity());
		}
	      } else {
		if (l == 0) s = "...";
		else s = " ..";
	      }
	      putstr(1 + heading_height + row - current_scroll_row, current_pos, s);
	      
	      setFgColor(styles.window_border_color);
	      setBgColor(bg);
	      
	      putstr(1 + heading_height + row - current_scroll_row, current_pos + 6, " ");
	      
	      current_pos += l == 0 ? 3 : 4;
	    }
	  }
	}
      }
    }

    if (current_pos + 2 < (size_t)cols) {
      auto & annotation = pattern.getAnnotation(row);
      if (!annotation.empty()) {
	setFgColor("#e03030");
	setBgColor("#702020");
	putstr(1 + heading_height + row - current_scroll_row, current_pos + 2, annotation);
      }
    }
  }
}

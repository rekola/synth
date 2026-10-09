#ifndef _EQUALIZEREDITOR_H_
#define _EQUALIZEREDITOR_H_

#include "../../effects/Equalizer.h"
#include "../UIElement.h"

#include <memory>

class Controller;
class InputEvent;

// A modal editor for one equalizer effect, the terminal rendering of
// ui/EqualizerEditorModel.h: the combined response drawn as a spline through
// log-spaced samples (the same Catmull-Rom curve the spectrum uses), a
// numbered marker per band on it, and below it a table of every band's
// settings. Keys and the mouse edit the selected band live; the audio thread
// follows each change.
class EqualizerEditor {
 public:
  enum class Result { NONE, REDRAW, CLOSED };

  bool isOpen() const { return track_id_ >= 0; }

  void open(Controller & controller, int track_id);
  void close(Controller & controller);

  // Follows the song (an undo, say); true if anything changed. Closes when the
  // effect is gone.
  bool refresh(Controller & controller);

  // (Re)draws the box under `parent`, centred on a screen of the given size.
  void show(UIPlane & parent, int screen_rows, int screen_cols);

  Result offerInput(Controller & controller, const InputEvent & input);

 private:
  void edit(Controller & controller, int band, const Equalizer::Band & value);
  void drawPlot();
  void drawTable();
  Result handleMouse(Controller & controller, const InputEvent & input);

  int track_id_ = -1;
  int selected_ = 0;
  int sample_rate_ = 44100;
  bool dragging_ = false;
  // The settings as of the last refresh()/edit, so the curve redraws at once.
  Equalizer eq_;

  std::unique_ptr<UIPlane> plane_;
  // Where the box sits and how big it is, as of the last show().
  int y_ = 0, x_ = 0, rows_ = 0, width_ = 0;
  // The plot's cells within the box: rows 1..plot_rows_, from column plot_x_.
  int plot_rows_ = 0, plot_x_ = 0, plot_cols_ = 0;
};

#endif

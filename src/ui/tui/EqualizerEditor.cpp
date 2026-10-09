#include "EqualizerEditor.h"

#include "../EqualizerEditorModel.h"
#include "../Spline.h"
#include "../StyleProvider.h"
#include "../UIPlane.h"
#include "../../Controller.h"
#include "../../model/Color.h"
#include "../../playback/InputEvent.h"
#include "../../util/Utf8.h"

#include <notcurses/notcurses.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace std;

namespace {

constexpr int kAxisCols = 6;   // the dB labels left of the plot
constexpr int kTableRows = 5;
constexpr int kMaxWidth = 110;
constexpr const char * kHint = "Left/Right band  Up/Down gain  S-Left/Right freq  [ ] Q  t type  Space on/off  r reset  Esc close";

string repeatUtf8(const string & glyph, int count) {
  string out;
  for (int i = 0; i < count; i++) out += glyph;
  return out;
}

void setFg(UIPlane & plane, const Color & c) { plane.setFgColor(c.getRed(), c.getGreen(), c.getBlue()); }
void setBg(UIPlane & plane, const Color & c) { plane.setBgColor(c.getRed(), c.getGreen(), c.getBlue()); }

// Braille dot bits for the pixel at (x, y) within a 2x4 cell.
int brailleBit(int x, int y) {
  static const int bits[2][4] = { { 0x01, 0x02, 0x04, 0x40 }, { 0x08, 0x10, 0x20, 0x80 } };
  return bits[x][y];
}

string brailleGlyph(int bits) {
  unsigned cp = 0x2800u + static_cast<unsigned>(bits);
  string out;
  out += static_cast<char>(0xE0 | (cp >> 12));
  out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out += static_cast<char>(0x80 | (cp & 0x3F));
  return out;
}

} // namespace

void EqualizerEditor::open(Controller & controller, int track_id) {
  if (!controller.findEqualizer(track_id)) return;
  track_id_ = track_id;
  selected_ = 0;
  dragging_ = false;
  sample_rate_ = controller.getChannelConfiguration().getAudioOutSampleRate();
  auto * eq = controller.findEqualizer(track_id);
  selected_ = 2;  // a peak, if nothing is on yet
  for (int i = Equalizer::kBands - 1; i >= 0; i--) {
    eq_.setBand(i, eq->getBand(i));
    if (eq->getBand(i).isActive()) selected_ = i;
  }
}

void EqualizerEditor::close(Controller & controller) {
  if (dragging_) controller.endEqualizerGesture();
  dragging_ = false;
  plane_.reset();
  track_id_ = -1;
}

bool EqualizerEditor::refresh(Controller & controller) {
  if (!isOpen()) return false;
  auto * eq = controller.findEqualizer(track_id_);
  if (!eq) {
    close(controller);
    return true;
  }
  bool changed = false;
  for (int i = 0; i < Equalizer::kBands; i++) {
    if (eq->getBand(i) != eq_.getBand(i)) {
      eq_.setBand(i, eq->getBand(i));
      changed = true;
    }
  }
  return changed;
}

void EqualizerEditor::edit(Controller & controller, int band, const Equalizer::Band & value) {
  eq_.setBand(band, value);
  controller.setEqualizerBand(track_id_, band, eq_.getBand(band));
}

void EqualizerEditor::show(UIPlane & parent, int screen_rows, int screen_cols) {
  if (!isOpen()) return;

  width_ = std::min(kMaxWidth, std::max(40, screen_cols - 2));
  width_ = std::min(width_, screen_cols);
  rows_ = std::min(28, std::max(kTableRows + 10, screen_rows - 2));
  rows_ = std::min(rows_, screen_rows);
  plot_rows_ = std::max(3, rows_ - kTableRows - 3);
  plot_x_ = kAxisCols + 1;
  plot_cols_ = std::max(8, width_ - 1 - plot_x_);

  if (!plane_) plane_ = parent.createChild();
  plane_->resize(rows_, width_);
  x_ = (screen_cols - width_) / 2;
  y_ = (screen_rows - rows_) / 2;
  plane_->move(y_, x_);

  auto & styles = plane_->getStyles();
  setBg(*plane_, styles.window_accent_bg_color);
  setFg(*plane_, styles.window_border_color);
  plane_->setBold(false);
  plane_->erase();
  auto heading = string("┌─ Equalizer ");
  plane_->putstr(0, 0, heading + repeatUtf8("─", std::max(0, width_ - 1 - Utf8::displayWidth(heading))) + "┐");
  auto hint = Utf8::truncateToWidth(string(kHint), std::max(0, width_ - 6));
  auto footing = "└─ " + hint + " ";
  plane_->putstr(rows_ - 1, 0, footing + repeatUtf8("─", std::max(0, width_ - 1 - Utf8::displayWidth(footing))) + "┘");
  string blank(static_cast<size_t>(width_ - 2), ' ');
  for (int r = 1; r < rows_ - 1; r++) {
    setBg(*plane_, styles.window_accent_bg_color);
    plane_->putstr(r, 1, blank);
    setFg(*plane_, styles.window_border_color);
    plane_->putstr(r, 0, "│");
    plane_->putstr(r, width_ - 1, "│");
  }

  drawPlot();
  drawTable();
  plane_->moveToTop();
}

void EqualizerEditor::drawPlot() {
  auto & styles = plane_->getStyles();
  const int pw = plot_cols_, ph = plot_rows_;
  const int pxw = 2 * pw, pyh = 4 * ph;

  // The response, sampled at two log-spaced knots per cell and splined to a
  // value per pixel column, as the spectrum does.
  vector<float> knots(static_cast<size_t>(2 * pw));
  for (size_t i = 0; i < knots.size(); i++) {
    float hz = eqeditor::unitToFreq((static_cast<float>(i) + 0.5f) / static_cast<float>(knots.size()));
    knots[i] = eqeditor::gainToUnit(eq_.responseDb(hz, static_cast<float>(sample_rate_)));
  }
  auto curve = spline::sample(knots, static_cast<size_t>(pxw));
  auto pixelRow = [&](size_t x) { return static_cast<int>(std::lround((1.0f - curve[x]) * static_cast<float>(pyh - 1))); };

  vector<int> bits(static_cast<size_t>(pw * ph), 0);
  for (int x = 0; x < pxw; x++) {
    int y = pixelRow(static_cast<size_t>(x));
    int lo = y, hi = y;
    // Reaches the neighbours' midpoints so a steep slope stays connected.
    if (x > 0) { int m = (y + pixelRow(static_cast<size_t>(x - 1))) / 2; lo = std::min(lo, m); hi = std::max(hi, m); }
    if (x + 1 < pxw) { int m = (y + pixelRow(static_cast<size_t>(x + 1))) / 2; lo = std::min(lo, m); hi = std::max(hi, m); }
    for (int py = lo; py <= hi; py++) {
      bits[static_cast<size_t>((py / 4) * pw + x / 2)] |= brailleBit(x % 2, py % 4);
    }
  }

  auto rowOfUnit = [&](float u) { return std::clamp(static_cast<int>((1.0f - u) * static_cast<float>(ph)), 0, ph - 1); };
  auto colOfFreq = [&](float hz) { return std::clamp(static_cast<int>(eqeditor::freqToUnit(hz) * static_cast<float>(pw)), 0, pw - 1); };

  // Gridlines: 0 dB and +-12 dB across, 100 Hz / 1 kHz / 10 kHz down.
  vector<string> glyph(static_cast<size_t>(pw * ph), " ");
  for (float db : { -12.0f, 0.0f, 12.0f }) {
    int r = rowOfUnit(eqeditor::gainToUnit(db));
    for (int c = 0; c < pw; c++) glyph[static_cast<size_t>(r * pw + c)] = db == 0.0f ? "─" : "┄";
  }
  for (float hz : { 100.0f, 1000.0f, 10000.0f }) {
    int c = colOfFreq(hz);
    for (int r = 0; r < ph; r++) {
      auto & g = glyph[static_cast<size_t>(r * pw + c)];
      g = g == " " ? "┆" : "┼";
    }
  }

  for (int r = 0; r < ph; r++) {
    for (int c = 0; c < pw; c++) {
      auto cell = static_cast<size_t>(r * pw + c);
      if (bits[cell]) {
        setBg(*plane_, styles.window_accent_bg_color);
        setFg(*plane_, styles.spectrumColor(0.75f));
        plane_->putstr(1 + r, plot_x_ + c, brailleGlyph(bits[cell]));
      } else {
        setBg(*plane_, styles.window_accent_bg_color);
        setFg(*plane_, styles.window_border_color);
        plane_->putstr(1 + r, plot_x_ + c, glyph[cell]);
      }
    }
  }

  // The dB axis.
  setBg(*plane_, styles.window_accent_bg_color);
  setFg(*plane_, styles.window_border_color);
  for (float db : { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f }) {
    char text[16];
    std::snprintf(text, sizeof(text), "%+.0f", static_cast<double>(db));
    int r = rowOfUnit(eqeditor::gainToUnit(db));
    plane_->putstr(1 + r, 1, Utf8::padToWidth(string(text) + " ", kAxisCols - 1));
  }

  // The frequency axis, under the plot.
  plane_->putstr(1 + ph, 1, string(static_cast<size_t>(width_ - 2), ' '));
  struct Label { float hz; const char * text; };
  for (auto label : { Label{ 20.0f, "20" }, Label{ 100.0f, "100" }, Label{ 1000.0f, "1k" }, Label{ 10000.0f, "10k" }, Label{ 20000.0f, "20k" } }) {
    int c = colOfFreq(label.hz);
    int w = static_cast<int>(std::string(label.text).size());
    if (c + w > pw) c = pw - w;
    plane_->putstr(1 + ph, plot_x_ + c, label.text);
  }

  // The band markers, the selected one last so it is never hidden.
  auto marker = [&](int i) {
    auto & band = eq_.getBand(i);
    int c = colOfFreq(band.freq), r = rowOfUnit(eqeditor::markerGainUnit(band));
    bool selected = i == selected_;
    if (selected) {
      setFg(*plane_, styles.highlight_fg_color);
      setBg(*plane_, styles.highlight_bg_color);
    } else if (band.isActive()) {
      setFg(*plane_, styles.window_fg_color);
      setBg(*plane_, styles.window_accent_bg_color);
      plane_->setBold(true);
    } else {
      setFg(*plane_, styles.window_border_color);
      setBg(*plane_, styles.window_accent_bg_color);
    }
    plane_->putstr(1 + r, plot_x_ + c, string(1, static_cast<char>('1' + i)));
    plane_->setBold(false);
  };
  for (int i = 0; i < Equalizer::kBands; i++) if (i != selected_) marker(i);
  marker(selected_);
}

void EqualizerEditor::drawTable() {
  auto & styles = plane_->getStyles();
  const int top = 2 + plot_rows_;
  const int cw = (width_ - 2) / Equalizer::kBands;
  for (int i = 0; i < Equalizer::kBands; i++) {
    auto & band = eq_.getBand(i);
    bool selected = i == selected_;
    if (selected) {
      setFg(*plane_, styles.highlight_fg_color);
      setBg(*plane_, styles.highlight_bg_color);
    } else {
      setFg(*plane_, band.isActive() ? styles.window_fg_color : styles.window_border_color);
      setBg(*plane_, styles.window_accent_bg_color);
    }
    string cells[kTableRows] = {
      string(" ") + static_cast<char>('1' + i) + (band.on ? " ● on" : " ○ off"),
      string(" ") + eqeditor::typeName(band.type),
      " " + eqeditor::freqText(band.freq),
      " " + eqeditor::gainText(band),
      " " + eqeditor::qText(band.q),
    };
    for (int r = 0; r < kTableRows; r++) {
      plane_->putstr(top + r, 1 + i * cw, Utf8::padToWidth(Utf8::truncateToWidth(cells[r], cw), cw));
    }
  }
  // Whatever the integer division left at the right edge.
  setBg(*plane_, styles.window_accent_bg_color);
  int used = cw * Equalizer::kBands;
  if (used < width_ - 2) {
    for (int r = 0; r < kTableRows; r++) plane_->putstr(top + r, 1 + used, string(static_cast<size_t>(width_ - 2 - used), ' '));
  }
}

EqualizerEditor::Result
EqualizerEditor::handleMouse(Controller & controller, const InputEvent & input) {
  auto id = input.getId();
  if (id == NCKEY_BUTTON4 || id == NCKEY_BUTTON5) {
    // The wheel narrows or widens the selected band (Shift: gain instead).
    if (input.getKind() == InputEvent::Kind::RELEASE) return Result::NONE;
    float direction = id == NCKEY_BUTTON4 ? 1.0f : -1.0f;
    auto band = eq_.getBand(selected_);
    edit(controller, selected_, input.hasShift() ? eqeditor::withGainDelta(band, direction * 0.5f) : eqeditor::withQScale(band, direction > 0 ? 1.1f : 1.0f / 1.1f));
    return Result::REDRAW;
  }

  int row = input.getY() - y_, col = input.getX() - x_;
  if (input.getKind() == InputEvent::Kind::RELEASE) {
    if (dragging_) controller.endEqualizerGesture();
    dragging_ = false;
    return Result::REDRAW;
  }

  bool inside = row >= 0 && row < rows_ && col >= 0 && col < width_;
  if (!dragging_ && !inside) return Result::CLOSED; // a click outside dismisses

  bool in_plot = row >= 1 && row <= plot_rows_ && col >= plot_x_ && col < plot_x_ + plot_cols_;
  if (dragging_ || in_plot) {
    // Positions are clamped so a drag that leaves the plot keeps going.
    float x_unit = (static_cast<float>(std::clamp(col - plot_x_, 0, plot_cols_ - 1)) + 0.5f) / static_cast<float>(plot_cols_);
    float y_unit = 1.0f - (static_cast<float>(std::clamp(row - 1, 0, plot_rows_ - 1)) + 0.5f) / static_cast<float>(plot_rows_);
    if (!dragging_) {
      selected_ = eqeditor::nearestBand(eq_, x_unit, y_unit, plot_cols_, plot_rows_);
      dragging_ = true;
      controller.beginEqualizerGesture();
    }
    edit(controller, selected_, eqeditor::withPlotPosition(eq_.getBand(selected_), x_unit, y_unit));
    return Result::REDRAW;
  }

  // The table: a click selects the column; its first row switches the band on
  // or off and its second steps the type.
  int table_row = row - (2 + plot_rows_);
  int cw = (width_ - 2) / Equalizer::kBands;
  if (table_row >= 0 && table_row < kTableRows && col >= 1 && cw > 0) {
    int band = std::min((col - 1) / cw, Equalizer::kBands - 1);
    if (band == selected_) {
      auto value = eq_.getBand(band);
      if (table_row == 0) { value.on = !value.on; edit(controller, band, value); }
      else if (table_row == 1) edit(controller, band, eqeditor::withNextType(value, 1));
    }
    selected_ = band;
    return Result::REDRAW;
  }
  return Result::NONE;
}

EqualizerEditor::Result
EqualizerEditor::offerInput(Controller & controller, const InputEvent & input) {
  if (!isOpen()) return Result::CLOSED;
  auto id = input.getId();

  if (id == NCKEY_BUTTON1 || id == NCKEY_BUTTON4 || id == NCKEY_BUTTON5) {
    auto result = handleMouse(controller, input);
    if (result == Result::CLOSED) close(controller);
    return result;
  }
  if (input.getKind() == InputEvent::Kind::RELEASE) return Result::NONE;

  if (id == NCKEY_ESC || id == NCKEY_ENTER || id == 'q' || (input.hasCtrl() && id == 'g')) {
    close(controller);
    return Result::CLOSED;
  }

  auto band = eq_.getBand(selected_);
  bool shift = input.hasShift();
  auto true_id = input.getTrueCaseId();
  if (id == NCKEY_LEFT && shift) edit(controller, selected_, eqeditor::withFreqOctaves(band, -1.0f / 12.0f));
  else if (id == NCKEY_RIGHT && shift) edit(controller, selected_, eqeditor::withFreqOctaves(band, 1.0f / 12.0f));
  else if (id == NCKEY_LEFT) selected_ = (selected_ + Equalizer::kBands - 1) % Equalizer::kBands;
  else if (id == NCKEY_RIGHT) selected_ = (selected_ + 1) % Equalizer::kBands;
  else if (id == NCKEY_UP) edit(controller, selected_, eqeditor::withGainDelta(band, shift ? 3.0f : 0.5f));
  else if (id == NCKEY_DOWN) edit(controller, selected_, eqeditor::withGainDelta(band, shift ? -3.0f : -0.5f));
  else if (id == '[') edit(controller, selected_, eqeditor::withQScale(band, 1.0f / 1.1f));
  else if (id == ']') edit(controller, selected_, eqeditor::withQScale(band, 1.1f));
  else if (id == NCKEY_HOME) selected_ = 0;
  else if (id == NCKEY_END) selected_ = Equalizer::kBands - 1;
  else if (id >= '1' && id <= '8') selected_ = static_cast<int>(id - '1');
  else if (id == 't') edit(controller, selected_, eqeditor::withNextType(band, true_id == 'T' ? -1 : 1));
  else if (id == NCKEY_SPACE) { band.on = !band.on; edit(controller, selected_, band); }
  else if (id == 'r') {
    Equalizer defaults;
    edit(controller, selected_, defaults.getBand(selected_));
  } else return Result::NONE;
  return Result::REDRAW;
}

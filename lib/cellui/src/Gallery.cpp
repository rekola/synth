#include <cellui/Gallery.h>

#include <cellui/ChoiceList.h>
#include <cellui/InfoDialog.h>
#include <cellui/Markdown.h>
#include <cellui/SubcellGlyphs.h>
#include <cellui/Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace cellui {

namespace {

class Page {
 public:
  Page(UIPlane & plane, const Theme & theme) : plane_(plane), theme_(theme) { }

  int row() const { return row_; }
  void skip(int rows = 1) { row_ += rows; }

  void plain() {
    colors(theme_.text_primary, theme_.surface_base);
    style(false, false, false);
  }

  void style(bool bold, bool italic, bool underline) {
    plane_.setBold(bold);
    plane_.setItalic(italic);
    plane_.setUnderline(underline);
  }

  void colors(const Color & fg, const Color & bg) {
    plane_.setFgColor(fg);
    plane_.setBgColor(bg);
  }

  void text(int col, const std::string & s) { plane_.putstr(row_, col, s); }

  void heading(const std::string & title) {
    plain();
    colors(theme_.accent, theme_.surface_base);
    style(true, false, false);
    text(0, title);
    plain();
    skip();
  }

  void fillRow(const Color & bg) {
    plane_.setBgColor(bg);
    text(0, std::string(static_cast<size_t>(plane_.getDim().second), ' '));
  }

 private:
  UIPlane & plane_;
  const Theme & theme_;
  int row_ = 0;
};

std::string hex(const Color & c) {
  char buf[8];
  auto rgb = c.toSrgb8();
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
  return buf;
}

// Black or white, whichever reads better on `bg`.
Color readableOn(const Color & bg) {
  return bg.luminance() > 0.26f ? Color(0, 0, 0) : Color(255, 255, 255);
}

}  // namespace

Gallery::Gallery(UIPlane & plane, const Theme & theme) {
  const StyleProvider & styles = plane.getStyles();
  int width = plane.getDim().second;

  plane.erase();
  {
    Page fill(plane, theme);
    fill.plain();
    for (int r = 0; r < plane.getDim().first; r++) {
      fill.fillRow(theme.surface_base);
      fill.skip();
    }
  }
  Page page(plane, theme);
  page.heading("Cell UI gallery");
  page.skip();

  page.heading("Text styles");
  page.plain();
  page.text(0, "normal");
  page.style(true, false, false);
  page.text(8, "bold");
  page.style(false, true, false);
  page.text(15, "italic");
  page.style(false, false, true);
  page.text(23, "underline");
  page.style(true, true, false);
  page.text(34, "bold italic");
  page.plain();
  page.skip(2);

  page.heading("Surfaces and text");
  const std::pair<const char *, const Color *> surfaces[] = {
    {"surface.base", &theme.surface_base}, {"surface.panel", &theme.surface_panel},
    {"surface.raised", &theme.surface_raised}, {"surface.overlay", &theme.surface_overlay},
  };
  for (auto & [name, color] : surfaces) {
    page.colors(theme.text_primary, *color);
    page.text(0, utf8::padToWidth(std::string(" ") + name, 20));
    page.colors(theme.text_secondary, *color);
    page.text(20, utf8::padToWidth(" secondary text", 20));
    page.colors(theme.text_disabled, *color);
    page.text(40, utf8::padToWidth(" disabled text", 20));
    page.colors(theme.text_primary, theme.surface_base);
    page.text(61, hex(*color));
    page.skip();
  }
  page.plain();
  page.skip();

  page.heading("Accent and state");
  const std::pair<const char *, const Color *> tokens[] = {
    {"accent", &theme.accent}, {"accent.dim", &theme.accent_dim}, {"focus", &theme.focus},
    {"state.playing", &theme.state_playing}, {"state.playing.dim", &theme.state_playing_dim},
    {"state.recording", &theme.state_recording}, {"state.recording.dim", &theme.state_recording_dim},
  };
  for (auto & [name, color] : tokens) {
    page.colors(readableOn(*color), *color);
    page.text(0, utf8::padToWidth(std::string(" ") + name, 24));
    page.colors(theme.text_primary, theme.surface_base);
    page.text(25, hex(*color));
    page.skip();
  }
  page.plain();
  page.skip();

  page.heading("Level meter ramp");
  for (int i = 0; i < 32; i++) {
    Color c = styles.meterColor(static_cast<float>(i) / 31.0f);
    page.colors(c, theme.surface_base);
    page.text(i, "█");
  }
  page.plain();
  page.skip(2);

  page.heading("Sub-cell glyphs");
  std::string quadrants, braille, sextants;
  for (int mask = 0; mask < 16; mask++) quadrants += utf8::encodeCodepoint(mask == 0 ? 0x2591 : kQuadrantCodepoints[mask]);
  for (int mask = 1; mask < 64; mask += 3) sextants += utf8::encodeCodepoint(sextantCodepoint(mask));
  for (int mask = 1; mask < 256; mask += 9) braille += utf8::encodeCodepoint(brailleCodepoint(mask));
  page.text(0, "quadrants  " + quadrants);
  page.skip();
  page.text(0, "sextants   " + sextants);
  page.skip();
  page.text(0, "braille    " + braille);
  page.skip(2);

  page.heading("Width and truncation");
  const std::vector<std::string> samples = {"plain text", "caf\xc3\xa9", "e\xcc\x81 (combining)", "\xe6\xbc\xa2\xe5\xad\x97 (wide)", "\xf0\x9d\x84\xaa sharp"};
  for (auto & sample : samples) {
    page.text(0, utf8::padToWidth(sample, 24));
    page.text(25, std::to_string(utf8::displayWidth(sample)) + " columns, cut to 6: [" + utf8::truncateToWidth(sample, 6) + "]");
    page.skip();
  }
  page.skip();

  page.heading("Markdown");
  auto doc = markdown::parse("# Heading\n\nA paragraph with *italic* and **bold** words that wraps at the width of the box.\n\n## Second\n\nAnd a second paragraph.");
  int box = std::min(36, width - 2);
  for (auto & line : markdown::layout(doc, box)) {
    int col = 0;
    for (auto & run : line) {
      page.style(run.bold, run.italic, false);
      page.text(col, run.text);
      col += utf8::displayWidth(run.text);
    }
    page.style(false, false, false);
    page.skip();
  }
  page.skip();

  page.heading("Choice list");
  ChoiceList list;
  list.reset(8, 5);
  const int visible = 4;
  int first = list.firstVisible(visible);
  for (int i = 0; i < visible; i++) {
    bool selected = first + i == list.selected();
    page.colors(selected ? theme.text_on_accent : theme.text_primary, selected ? theme.accent : theme.surface_panel);
    page.text(0, utf8::padToWidth(" Device " + std::to_string(first + i + 1), 24));
    page.skip();
  }
  page.plain();
  page.text(0, "entry " + std::to_string(list.selected() + 1) + " of " + std::to_string(list.count()));
  page.skip();

  page.skip();
  page.heading("Info dialog");
  dialog_.show(plane, "About", "# Cell UI\n\nA small *toolkit* for **text** interfaces, drawn here from Markdown.", 44, 10);
  dialog_.move(page.row(), 0);
  page.skip(dialog_.rows());

  page.skip();
  page.heading("Choice dialog");
  choice_.open("Devices", {"Built-in Audio", "USB Interface (port 2-1)", "HDMI Output", "Bluetooth Speaker"}, 1);
  // Centred on a 44-column strip that starts at this row.
  choice_.show(plane, 2 * page.row() + 6, 44);
  page.skip(6);

  page.skip();
  page.heading("Meters");
  {
    // A spectrum falling off with frequency, with a few peaks.
    std::vector<float> db(256);
    for (size_t i = 0; i < db.size(); i++) {
      float t = static_cast<float>(i) / static_cast<float>(db.size());
      db[i] = -12.0f - 55.0f * std::sqrt(t) + 8.0f * std::sin(static_cast<float>(i) * 0.35f);
    }
    spectrum_ = std::make_unique<SpectrumMeter>(plane);
    spectrum_->resize(4, 40);
    spectrum_->move(page.row(), 0);
    spectrum_->setSpectrum(db, 86.0f);

    channels_ = std::make_unique<ChannelMeter>(plane);
    channels_->resize(5, 10);
    channels_->move(page.row(), 44);
    channels_->setLevels({0.7f, 0.3f, 0.12f, 0.5f, 0.02f, 0.9f, 1.2f, 0.0f}, "W Y Z X");
  }
  page.skip(5);

  page.skip();
  page.heading("Spin box and cover art");
  spin_ = std::make_unique<SpinBox>(plane, "Octave", 0, 8, [this] { return octave_; },
                                    [this](int v) { octave_ = v; }, theme.surface_panel, theme.text_primary);
  spin_->resize(1, spin_->preferredWidth());
  spin_->move(page.row(), 0);
  spin_->render(plane.getStyles(), true);
  cover_ = std::make_unique<CoverArt>(plane);
  cover_->resize(4, CoverArt::widthForHeight(4));
  cover_->move(page.row(), 30);
  cover_->render(plane.getStyles(), true);
  page.skip(5);

  rows_ = page.row();
}

}  // namespace cellui

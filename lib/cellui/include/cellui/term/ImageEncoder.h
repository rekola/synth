#pragma once

#include <cellui/CellBuffer.h>
#include <cellui/Image.h>
#include <cellui/LinkStatus.h>
#include <cellui/term/Capabilities.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace cellui::term {

// How colours go to the registers of a Sixel image when the picture has more
// colours than the registers.
struct Dither {
  enum class Method {
    NEAREST,          // the nearest entry everywhere: bands in a gradient
    ORDERED,          // a Bayer matrix fixed to the screen, so tiles and redraws line up
    FLOYD_STEINBERG,  // error diffusion: smoothest, but each image starts afresh
  };
  Method method = Method::ORDERED;
  // "nearest", "ordered" or "floyd-steinberg"; false for another name.
  static bool byName(const std::string & name, Method & out) {
    if (name == "nearest") out = Method::NEAREST;
    else if (name == "ordered") out = Method::ORDERED;
    else if (name == "floyd-steinberg") out = Method::FLOYD_STEINBERG;
    else return false;
    return true;
  }
  // The screen pixel of the region's top-left corner (ordered dithering).
  int origin_x = 0, origin_y = 0;
};

// How many colours a picture keeps, which a slow link turns instead of the screen's colour: FULL sends
// the pixels as they are (kitty and iTerm2 RGB, deflated; Sixel up to 128 registers), REDUCED and LOW
// send a palette of at most 64 and 16 colours (a palette PNG for kitty and iTerm2, that many Sixel
// registers). Charts have few colours, so REDUCED is close to lossless for them. Below FULL nothing
// is dithered but the pictures stretched over their cells (a blurred backdrop, a smooth gradient),
// which would band: dithering is noise to a chart, and noise costs bytes a slow link has not got.
enum class PictureQuality { FULL, REDUCED, LOW };
// The palette size at a quality; 0 for FULL (no palette: the pixels as they are).
int pictureColours(PictureQuality quality);
// The quality a link class gets: FULL down to 30 Hz, REDUCED at 15 Hz, LOW below.
PictureQuality pictureQualityFor(LinkClass cls);

// The bytes that show a frame's images, by kitty graphics, iTerm2's inline
// images or Sixel. Only
// what changed is sent: a new picture, a move, or a change in which of its
// cells other planes cover (a popup over a chart). Cells covered by another
// plane are left out - a kitty image is placed as the rectangles of cells
// still showing it, a Sixel image is drawn with those cells transparent -
// an iTerm2 image is sent as one PNG per such rectangle - and an image that
// is gone is deleted (kitty) or overwritten by the text the frame now has
// there (Sixel, iTerm2). A Sixel or iTerm2 image never reaches the screen's
// last row, where drawing one could scroll the screen.
class ImageEncoder {
 public:
  struct Profile {
    PixelProtocol protocol = PixelProtocol::NONE;
    int cell_px_height = 0, cell_px_width = 0;
    // Sent once, before the first Sixel image.
    std::string sixel_setup;
    // The colour registers one Sixel image may use (the terminal's count, at most 128: more only cost
    // bytes and passes).
    int sixel_registers = 128;
    Dither::Method dither = Dither::Method::ORDERED;
    static Profile from(const Capabilities & caps);
  };

  ImageEncoder() = default;
  explicit ImageEncoder(Profile profile) : profile_(std::move(profile)) { }

  // `frame` is the composed frame, its image cells marked; `cleared` says
  // the screen is cleared before these bytes, taking every image with it.
  std::string encode(const CellBuffer & frame, const std::vector<ImagePlacement> & images, bool cleared);
  // The bytes the last encode() made: picture data (kitty uploads, Sixel and
  // iTerm2 images) and placement (positioning, placements and deletions).
  struct Sizes {
    size_t upload = 0, placement = 0;
  };
  const Sizes & lastSizes() const { return sizes_; }
  // What was uploaded, by the kind of a shared picture ("dial") or the size of any other
  // ("232x80"), since the last call: how many pictures and how many bytes of picture data. For
  // measuring what a link carries.
  struct PictureStat {
    size_t uploads = 0, bytes = 0;
  };
  std::map<std::string, PictureStat> takePictureStats() {
    auto out = std::move(picture_stats_);
    picture_stats_.clear();
    return out;
  }
  // A reply from the terminal to a kitty graphics command (the body after
  // "G"): "" for success, else a description of the error. Commands are sent
  // with q=1, so only errors are ever replied.
  std::string handleKittyReply(const std::string & body);
  // Whether uploads are deflated (kitty `o=z`, a PNG's data). A terminal that rejects a compressed
  // upload turns it off for good, and the pictures it refused go up again raw.
  bool compressing() const { return compress_; }
  void setCompressing(bool on) { compress_ = on; }
  // Applies to the pictures sent from now on; the ones shown stay as they are until they change.
  void setPictureQuality(PictureQuality quality) { quality_ = quality; }
  PictureQuality pictureQuality() const { return quality_; }
  // Whether a palette PNG may be sent to kitty. A terminal that refuses one (an error reply to its
  // upload) turns it off for good, and quantised raw RGB, deflated, goes up in its place.
  bool palettePng() const { return palette_png_; }
  // The most the terminal is asked to keep of shared pictures no placement shows, in bytes.
  static constexpr size_t kSharedBudget = 8u << 20;
  void setSharedBudget(size_t bytes) { shared_budget_ = bytes; }
  // Removes every image (kitty graphics; Sixel images go with the text).
  std::string clear();

  // One rectangle of cells, screen coordinates.
  struct Rect {
    int row = 0, col = 0, rows = 0, cols = 0;
    bool operator==(const Rect & o) const { return row == o.row && col == o.col && rows == o.rows && cols == o.cols; }
  };

 private:
  struct Shown {
    uint32_t id = 0;  // kitty image id
    uint64_t version = 0;
    int row = 0, col = 0;
    std::vector<Rect> rects;  // what shows of it
    Image sent;               // (Sixel) the pixels last drawn, to send only the cells that changed
    bool shared = false;      // (kitty) placed from a shared upload: only its placements go with it
    uint32_t place_base = 0;  // (kitty, shared) its placement ids are this plus the rectangle's number
  };
  // (kitty) A shared picture is uploaded once, whatever the number of placements, and kept after its
  // last placement goes, up to a budget of bytes (the terminal's own memory) and the oldest unplaced
  // first. By the picture's version: the tree makes one picture for a key.
  struct SharedUpload {
    uint32_t id = 0;
    size_t bytes = 0;  // what the terminal holds for it, decoded
    uint64_t last_used = 0;
  };

  void uploadKitty(const Image & img, uint32_t id, std::string & out);
  std::string kittySharedPlacement(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  void evictSharedUploads(const std::set<uint32_t> & in_use, std::string & out);
  std::string kitty(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::string sixel(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::string iterm2(const CellBuffer & frame, const ImagePlacement & p, Shown & shown, bool fresh, bool cleared);
  std::vector<Rect> visibleRects(const CellBuffer & frame, const ImagePlacement & p, bool avoid_last_row) const;

  Profile profile_;
  std::map<uint64_t, Shown> shown_;
  std::map<uint64_t, SharedUpload> shared_uploads_;  // by picture version
  uint64_t tick_ = 0;
  size_t shared_budget_ = kSharedBudget;
  uint32_t next_place_ = 0;
  uint32_t next_id_ = 0;
  bool sixel_ready_ = false;
  bool compress_ = true;
  PictureQuality quality_ = PictureQuality::FULL;
  bool palette_png_ = true;
  std::set<uint32_t> png_ids_;  // kitty ids uploaded as a palette PNG and not yet confirmed
  std::set<uint32_t> deflated_ids_;  // kitty ids uploaded compressed and not yet confirmed
  Sizes sizes_;
  std::map<std::string, PictureStat> picture_stats_;
};

// The pixels encoded as Sixel: covered pixels (where `keep` says false, one
// entry per pixel) are left transparent. The registers (at most `registers`)
// are the picture's own colours: exactly, with no dithering, when they fit,
// else their median cut, dithered. Exposed for tests.
std::string encodeSixel(const Image & image, int x, int y, int width, int height, const std::vector<bool> & keep,
                        const Dither & dither = {}, int registers = 256);

// A rectangle of the pixels as a PNG file (8-bit RGBA, deflated). Exposed for tests.
std::vector<uint8_t> encodePng(const Image & image, int x, int y, int width, int height);

// The same as a palette PNG (colour type 3) of at most `colours` colours: the picture's own when they
// fit, else their median cut (dithered where `dither` says: a smooth picture such as a blurred
// backdrop bands without it, a chart only grows noise); 8 bits an index, or 4 for 16 colours or fewer.
// Exposed for tests.
std::vector<uint8_t> encodePalettePng(const Image & image, int x, int y, int width, int height, int colours, bool dither = false);

// The picture's pixels (R, G, B) with their colours cut down to at most `colours`, as the palette PNG
// shows them; for a terminal that takes no palette PNG.
std::vector<uint8_t> quantisedRgb(const Image & image, int colours, bool dither = false);

}  // namespace cellui::term

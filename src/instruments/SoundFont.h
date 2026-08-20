#ifndef _SOUNDFONT_H_
#define _SOUNDFONT_H_

#include "InstrumentSet.h"

#include <string>

class SoundFontFile;

class SoundFont : public InstrumentSet {
 public:  
  explicit SoundFont(std::string filename) : filename_(std::move(filename)) {
    openFile();
  }

  std::unique_ptr<Instrument> createInstrument(size_t preset, const char * name = 0) override;
  std::vector<std::unique_ptr<Instrument> > createAll() override;

  // (bank, program)-addressed sibling of createInstrument(size_t preset, ...)
  // above - preset there is a raw index into the file's own
  // sorted-by-(bank,program) preset array, which only coincides with the GM
  // program number when a font happens to lay bank 0 out contiguously from
  // program 0 with nothing else sorted in between (true of every font this
  // engine has been tested against so far, but not a property any font is
  // required to have). This looks the preset up by its actual (bank,
  // program) instead, so callers who mean "GM program N" don't depend on
  // file layout to get the right patch. Returns nullptr, rather than an
  // empty Instrument, when the font has no preset at that (bank, program) -
  // "no provider here" is a real, expected outcome (see docs/instrument-paths.md's
  // resolver), not an error.
  std::unique_ptr<Instrument> createInstrumentByProgram(int bank, int program, const char * name = 0);
  
protected:
  void openFile();

private:
  std::string filename_;
  std::shared_ptr<SoundFontFile> sf_;
};

#endif

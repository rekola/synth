#ifndef _SOFAFILERESOLVER_H_
#define _SOFAFILERESOLVER_H_

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Mirrors findDefaultSoundFont() in Controller.cpp: project-local override
// first, then well-known directories - including /usr/share/libmysofa,
// where Ubuntu's libmysofa1 package itself ships real SOFA files
// (default.sofa, MIT_KEMAR_normal_pinna.sofa), so binaural decoding works
// out of the box on Ubuntu without needing to source a SOFA file
// separately. Shared by every decoder that needs a SOFA file (currently
// AmbisonicBinauralMixer and AmbisonicMagLSDecoder) so the resolution
// order can't drift apart into two independently-maintained copies.
inline std::string findDefaultSofaFile() {
  namespace fs = std::filesystem;
  std::error_code ec;

  for (auto & entry : fs::directory_iterator("data", ec)) {
    if (entry.path().extension() == ".sofa") return entry.path().string();
  }

  std::vector<fs::path> dirs;
  if (auto home = getenv("HOME")) {
    dirs.push_back(fs::path(home) / ".local/share/sofa");
  }
  dirs.push_back("/usr/share/libmysofa");
  dirs.push_back("/usr/share/sofa");

  const char * preferred[] = {
    "default.sofa",
    "MIT_KEMAR_normal_pinna.sofa",
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
      if (entry.path().extension() != ".sofa") continue;
      auto size = fs::file_size(entry.path(), ec);
      if (!ec && size > best_size) {
        best_size = size;
        best = entry.path();
      }
    }
  }
  return best.string();
}

#endif

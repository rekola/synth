// Times the deflate encoder and measures its output on raw RGB files (name_WxH_k.rgb), for comparing
// with zlib on the same bytes: deflate_bench DIR.  Prints, per picture size, the mean compressed size
// and the speed at each level.
#include <cellui/term/Deflate.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace cellui::term;
namespace fs = std::filesystem;

int main(int argc, char ** argv) {
  if (argc < 2) return 2;
  std::map<std::string, std::vector<std::vector<uint8_t>>> groups;
  for (auto & e : fs::directory_iterator(argv[1])) {
    if (e.path().extension() != ".rgb") continue;
    std::string name = e.path().stem().string();
    name = name.substr(0, name.rfind('_'));
    std::ifstream f(e.path(), std::ios::binary);
    groups[name].emplace_back(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  }
  std::printf("%-26s %8s |", "picture", "raw");
  for (int level = 1; level <= 3; level++) std::printf(" level %d: size   MB/s |", level);
  std::printf("\n");
  for (auto & [name, images] : groups) {
    std::printf("%-26s %8zu |", name.c_str(), images[0].size());
    for (int level = 1; level <= 3; level++) {
      double size = 0, bytes = 0, seconds = 0;
      for (auto & im : images) size += static_cast<double>(zlibCompress(im, level).size());
      size /= static_cast<double>(images.size());
      auto start = std::chrono::steady_clock::now();
      int rounds = 0;
      do {
        for (auto & im : images) {
          auto z = zlibCompress(im, level);
          bytes += static_cast<double>(im.size());
          if (z.empty()) return 1;
        }
        rounds++;
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      } while (seconds < 0.3);
      std::printf(" %14.0f %6.0f |", size, bytes / seconds / 1e6);
    }
    std::printf("\n");
  }
  return 0;
}

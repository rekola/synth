#include "Utf8.h"

#include <unigbrk.h>
#include <uniwidth.h>
#include <unistring/localcharset.h>

#include <cstdint>

namespace Utf8 {

namespace {

const uint8_t * asBytes(const std::string & text) {
  return reinterpret_cast<const uint8_t *>(text.data());
}

}  // namespace

int
displayWidth(const std::string & text) {
  if (text.empty()) return 0;
  int w = u8_strwidth(reinterpret_cast<const uint8_t *>(text.c_str()), locale_charset());
  return w < 0 ? 0 : w;
}

std::string
truncateToWidth(const std::string & text, int max_columns) {
  if (max_columns <= 0) return std::string();

  const uint8_t * start = asBytes(text);
  const uint8_t * end = start + text.size();
  const char * encoding = locale_charset();

  size_t committed_offset = 0;
  int accumulated = 0;
  const uint8_t * cluster_start = start;

  while (cluster_start < end) {
    const uint8_t * cluster_end = u8_grapheme_next(cluster_start, end);
    if (!cluster_end || cluster_end <= cluster_start) cluster_end = cluster_start + 1;  // defensive: never spin on malformed input

    int w = u8_width(cluster_start, static_cast<size_t>(cluster_end - cluster_start), encoding);
    if (w < 0) w = 0;

    if (accumulated + w > max_columns) break;

    accumulated += w;
    committed_offset = static_cast<size_t>(cluster_end - start);
    cluster_start = cluster_end;
  }

  return text.substr(0, committed_offset);
}

std::string
padToWidth(const std::string & text, int width) {
  int current = displayWidth(text);
  if (current >= width) return text;

  std::string result = text;
  result.append(static_cast<size_t>(width - current), ' ');
  return result;
}

}  // namespace Utf8

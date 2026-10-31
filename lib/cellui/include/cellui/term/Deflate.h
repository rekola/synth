#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cellui::term {

// Adler-32 (the check value of a zlib stream) and CRC-32 (a PNG chunk's), continuing from `adler`
// (1 to start) and `crc` (0 to start).
uint32_t adler32(const uint8_t * data, size_t size, uint32_t adler = 1);
uint32_t crc32(const uint8_t * data, size_t size, uint32_t crc = 0);

// The bytes as a zlib stream (RFC 1950) of deflate blocks (RFC 1951), for the kitty protocol's
// `o=z` and for a PNG's IDAT. Any inflate reads it.
//
// `level`: 1 takes the first match found and the single most recent candidate (fast), 2 (the
// default) checks short chains and takes a longer match one byte on (lazy matching), 3 looks
// through longer chains. All three choose, for each block, the smaller of dynamic Huffman codes,
// the fixed ones and stored bytes.
//
// The output depends on the input and the level only: the wide compare used to find match lengths
// changes the speed and never the bytes.
std::vector<uint8_t> zlibCompress(const uint8_t * data, size_t size, int level = 2);
inline std::vector<uint8_t> zlibCompress(const std::vector<uint8_t> & data, int level = 2) {
  return zlibCompress(data.data(), data.size(), level);
}

// For tests: use the portable code only, to check that it makes the same bytes.
void setDeflatePortableOnly(bool portable_only);

}  // namespace cellui::term

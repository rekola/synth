#pragma once

// A small inflate for the tests, so the deflate encoder is checked against a decoder that shares none
// of its code (and no library is needed): RFC 1951 and the zlib wrapper of RFC 1950.

#include <cellui/term/Deflate.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace inflate_test {

class Inflater {
 public:
  Inflater(const uint8_t * in, size_t size) : in_(in), size_(size) { }

  // The bytes of a zlib stream, or nothing if it is not one (or its check value is wrong).
  std::optional<std::vector<uint8_t>> zlib() {
    if (size_ < 6) return std::nullopt;
    if (((in_[0] << 8) | in_[1]) % 31 != 0 || (in_[0] & 15) != 8) return std::nullopt;
    pos_ = 2;
    std::vector<uint8_t> out;
    if (!blocks(out)) return std::nullopt;
    align();
    if (pos_ + 4 > size_) return std::nullopt;
    uint32_t want = (uint32_t{in_[pos_]} << 24) | (uint32_t{in_[pos_ + 1]} << 16) | (uint32_t{in_[pos_ + 2]} << 8) | in_[pos_ + 3];
    if (cellui::term::adler32(out.data(), out.size()) != want) return std::nullopt;
    if (pos_ + 4 != size_) return std::nullopt;  // nothing after it
    return out;
  }

 private:
  struct Huffman {
    uint16_t count[16] = {0};
    uint16_t symbol[320] = {0};
  };

  bool bit(uint32_t & v) {
    if (bitcount_ == 0) {
      if (pos_ >= size_) return false;
      bitbuf_ = in_[pos_++];
      bitcount_ = 8;
    }
    v = bitbuf_ & 1;
    bitbuf_ >>= 1;
    bitcount_--;
    return true;
  }
  bool bits(int n, uint32_t & v) {
    v = 0;
    for (int i = 0; i < n; i++) {
      uint32_t b;
      if (!bit(b)) return false;
      v |= b << i;
    }
    return true;
  }
  void align() { bitcount_ = 0; }

  static bool build(Huffman & h, const uint8_t * lengths, int n) {
    std::fill(std::begin(h.count), std::end(h.count), uint16_t{0});
    for (int i = 0; i < n; i++) h.count[lengths[i]]++;
    int left = 1;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= h.count[len];
      if (left < 0) return false;  // over-subscribed
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    for (int i = 0; i < n; i++) {
      if (lengths[i]) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
    }
    // An incomplete code is only allowed for one code of one bit.
    return left == 0 || (n - h.count[0] == 1 && h.count[1] == 1) || n - h.count[0] == 0;
  }

  int decode(const Huffman & h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      uint32_t b;
      if (!bit(b)) return -1;
      code |= static_cast<int>(b);
      int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }

  bool codes(std::vector<uint8_t> & out, const Huffman & lit, const Huffman & dist) {
    static const uint16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int s = decode(lit);
      if (s < 0) return false;
      if (s < 256) {
        out.push_back(static_cast<uint8_t>(s));
      } else if (s == 256) {
        return true;
      } else {
        s -= 257;
        if (s >= 29) return false;
        uint32_t extra;
        if (!bits(lext[s], extra)) return false;
        size_t len = lbase[s] + extra;
        int ds = decode(dist);
        if (ds < 0 || ds >= 30) return false;
        if (!bits(dext[ds], extra)) return false;
        size_t d = dbase[ds] + extra;
        if (d > out.size()) return false;
        for (size_t i = 0; i < len; i++) out.push_back(out[out.size() - d]);
      }
    }
  }

  bool blocks(std::vector<uint8_t> & out) {
    uint32_t last;
    do {
      uint32_t type;
      if (!bit(last) || !bits(2, type)) return false;
      if (type == 0) {
        align();
        if (pos_ + 4 > size_) return false;
        uint32_t len = in_[pos_] | (uint32_t{in_[pos_ + 1]} << 8), nlen = in_[pos_ + 2] | (uint32_t{in_[pos_ + 3]} << 8);
        pos_ += 4;
        if (len != (~nlen & 0xffff) || pos_ + len > size_) return false;
        out.insert(out.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
      } else if (type == 1) {
        uint8_t lengths[320];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        Huffman lit, dist;
        build(lit, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        build(dist, lengths, 30);
        if (!codes(out, lit, dist)) return false;
      } else if (type == 2) {
        uint32_t nlen, ndist, ncode;
        if (!bits(5, nlen) || !bits(5, ndist) || !bits(4, ncode)) return false;
        nlen += 257;
        ndist += 1;
        ncode += 4;
        if (nlen > 286 || ndist > 30) return false;
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint8_t lengths[320] = {0};
        for (uint32_t i = 0; i < ncode; i++) {
          uint32_t v;
          if (!bits(3, v)) return false;
          lengths[order[i]] = static_cast<uint8_t>(v);
        }
        Huffman lencode;
        if (!build(lencode, lengths, 19)) return false;
        uint8_t all[320] = {0};
        uint32_t index = 0;
        while (index < nlen + ndist) {
          int sym = decode(lencode);
          if (sym < 0) return false;
          if (sym < 16) {
            all[index++] = static_cast<uint8_t>(sym);
          } else {
            uint8_t prev = 0;
            uint32_t rep, extra;
            if (sym == 16) {
              if (index == 0 || !bits(2, extra)) return false;
              prev = all[index - 1];
              rep = 3 + extra;
            } else if (sym == 17) {
              if (!bits(3, extra)) return false;
              rep = 3 + extra;
            } else {
              if (!bits(7, extra)) return false;
              rep = 11 + extra;
            }
            if (index + rep > nlen + ndist) return false;
            while (rep--) all[index++] = prev;
          }
        }
        if (all[256] == 0) return false;  // no end-of-block code
        Huffman lit, dist;
        if (!build(lit, all, static_cast<int>(nlen)) || !build(dist, all + nlen, static_cast<int>(ndist))) return false;
        if (!codes(out, lit, dist)) return false;
      } else {
        return false;
      }
    } while (!last);
    return true;
  }

  const uint8_t * in_;
  size_t size_;
  size_t pos_ = 0;
  uint32_t bitbuf_ = 0;
  int bitcount_ = 0;
};

inline std::optional<std::vector<uint8_t>> zlibDecode(const std::vector<uint8_t> & z) { return Inflater(z.data(), z.size()).zlib(); }

}  // namespace inflate_test

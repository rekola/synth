#include <cellui/term/Deflate.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace cellui::term {

static_assert(std::endian::native == std::endian::little, "the byte loads below assume a little-endian machine");

namespace {

bool g_portable_only = false;

// ---------------------------------------------------------------------------------------------
// Check values

struct CrcTables {
  uint32_t t[8][256];
  CrcTables() {
    for (uint32_t n = 0; n < 256; n++) {
      uint32_t c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      t[0][n] = c;
    }
    for (uint32_t n = 0; n < 256; n++) {
      for (int k = 1; k < 8; k++) t[k][n] = (t[k - 1][n] >> 8) ^ t[0][t[k - 1][n] & 0xff];
    }
  }
};

const CrcTables & crcTables() {
  static const CrcTables tables;
  return tables;
}

uint32_t load32(const uint8_t * p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// ---------------------------------------------------------------------------------------------
// The tables of deflate

constexpr int kWindow = 32768;
constexpr int kMaxMatch = 258;
constexpr int kEndOfBlock = 256;
constexpr int kLitLenSymbols = 286;
constexpr int kDistSymbols = 30;

constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
constexpr uint8_t kClenOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Symbols {
  uint8_t length_symbol[kMaxMatch + 1];
  uint8_t dist_symbol[kWindow + 1];
  Symbols() {
    for (int s = 0; s < 29; s++) {
      for (int len = kLenBase[s]; len < kLenBase[s] + (1 << kLenExtra[s]) && len <= kMaxMatch; len++) length_symbol[len] = static_cast<uint8_t>(s);
    }
    length_symbol[kMaxMatch] = 28;
    for (int s = 0; s < kDistSymbols; s++) {
      for (int d = kDistBase[s]; d < kDistBase[s] + (1 << kDistExtra[s]) && d <= kWindow; d++) dist_symbol[d] = static_cast<uint8_t>(s);
    }
  }
};

const Symbols & symbols() {
  static const Symbols s;
  return s;
}

// ---------------------------------------------------------------------------------------------
// Code lengths: the optimal lengths of at most `limit` bits (package-merge), so the code is complete.

void codeLengths(const uint32_t * freq, int n, int limit, uint8_t * lengths) {
  std::fill(lengths, lengths + n, uint8_t{0});
  std::vector<int> used;
  for (int i = 0; i < n; i++) {
    if (freq[i]) used.push_back(i);
  }
  size_t m = used.size();
  if (m == 0) return;
  if (m == 1) {
    lengths[used[0]] = 1;
    return;
  }
  std::stable_sort(used.begin(), used.end(), [&](int a, int b) { return freq[a] < freq[b]; });
  struct Item {
    uint64_t weight;
    bool package;
  };
  std::vector<Item> leaves(m);
  for (size_t j = 0; j < m; j++) leaves[j] = {freq[used[j]], false};
  // lists[d] is what is available at depth d: the leaves, and the packages of pairs from depth d + 1.
  std::vector<std::vector<Item>> lists(static_cast<size_t>(limit) + 1);
  lists[static_cast<size_t>(limit)] = leaves;
  for (int d = limit - 1; d >= 1; d--) {
    const auto & deeper = lists[static_cast<size_t>(d) + 1];
    std::vector<Item> packages;
    for (size_t j = 0; j + 1 < deeper.size(); j += 2) packages.push_back({deeper[j].weight + deeper[j + 1].weight, true});
    auto & list = lists[static_cast<size_t>(d)];
    list.reserve(m + packages.size());
    size_t a = 0, b = 0;
    while (a < m || b < packages.size()) {
      if (b >= packages.size() || (a < m && leaves[a].weight <= packages[b].weight)) list.push_back(leaves[a++]);
      else list.push_back(packages[b++]);
    }
  }
  std::vector<uint8_t> len(m, 0);
  size_t take = 2 * m - 2;
  for (int d = 1; d <= limit; d++) {
    const auto & list = lists[static_cast<size_t>(d)];
    size_t count = std::min(take, list.size()), leaf = 0, package = 0;
    for (size_t k = 0; k < count; k++) (list[k].package ? package : leaf)++;
    for (size_t j = 0; j < leaf; j++) len[j]++;
    take = 2 * package;
  }
  for (size_t j = 0; j < m; j++) lengths[used[j]] = len[j];
}

uint16_t reverseBits(uint32_t code, int bits) {
  uint32_t out = 0;
  for (int i = 0; i < bits; i++) {
    out = (out << 1) | (code & 1);
    code >>= 1;
  }
  return static_cast<uint16_t>(out);
}

// The canonical codes of the lengths, bit-reversed for a stream that is written low bit first.
void makeCodes(const uint8_t * lengths, int n, uint16_t * codes) {
  uint16_t count[16] = {0}, next[16] = {0};
  for (int i = 0; i < n; i++) count[lengths[i]]++;
  count[0] = 0;
  uint32_t code = 0;
  for (int bits = 1; bits < 16; bits++) {
    code = (code + count[bits - 1]) << 1;
    next[bits] = static_cast<uint16_t>(code);
  }
  for (int i = 0; i < n; i++) {
    if (lengths[i]) codes[i] = reverseBits(next[lengths[i]]++, lengths[i]);
  }
}

// ---------------------------------------------------------------------------------------------
// Output

class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t> & out) : out_(out) { }
  void put(uint32_t bits, int count) {
    acc_ |= static_cast<uint64_t>(bits) << n_;
    n_ += count;
    if (n_ >= 32) {
      uint32_t word = static_cast<uint32_t>(acc_);
      size_t at = out_.size();
      out_.resize(at + 4);
      std::memcpy(out_.data() + at, &word, 4);
      acc_ >>= 32;
      n_ -= 32;
    }
  }
  // To the next byte.
  void align() {
    while (n_ > 0) {
      out_.push_back(static_cast<uint8_t>(acc_));
      acc_ >>= 8;
      n_ = std::max(0, n_ - 8);
    }
    acc_ = 0;
    n_ = 0;
  }
  void bytes(const uint8_t * data, size_t size) { out_.insert(out_.end(), data, data + size); }

 private:
  std::vector<uint8_t> & out_;
  uint64_t acc_ = 0;
  int n_ = 0;
};

struct Token {
  uint16_t value;  // the byte of a literal, or a match's length
  uint16_t dist;   // 0 for a literal
};

struct Lengths {
  uint8_t lit[kLitLenSymbols] = {0};
  uint8_t dist[kDistSymbols] = {0};
};

Lengths fixedLengths() {
  Lengths l;
  for (int i = 0; i < 144; i++) l.lit[i] = 8;
  for (int i = 144; i < 256; i++) l.lit[i] = 9;
  for (int i = 256; i < 280; i++) l.lit[i] = 7;
  for (int i = 280; i < kLitLenSymbols; i++) l.lit[i] = 8;
  for (int i = 0; i < kDistSymbols; i++) l.dist[i] = 5;
  return l;
}

struct Histogram {
  uint32_t lit[kLitLenSymbols] = {0};
  uint32_t dist[kDistSymbols] = {0};
  // Extra bits of lengths and distances, whatever the codes.
  uint64_t extra_bits = 0;
};

uint64_t payloadBits(const Histogram & h, const Lengths & l) {
  uint64_t bits = h.extra_bits;
  for (int i = 0; i < kLitLenSymbols; i++) bits += static_cast<uint64_t>(h.lit[i]) * l.lit[i];
  for (int i = 0; i < kDistSymbols; i++) bits += static_cast<uint64_t>(h.dist[i]) * l.dist[i];
  return bits;
}

// The code lengths of a dynamic block's header run-length coded for the code-length alphabet.
struct HeaderCode {
  std::vector<std::pair<uint8_t, uint8_t>> symbols;  // (symbol, extra bits value)
  uint32_t freq[19] = {0};
  uint8_t clen[19] = {0};
  uint16_t ccodes[19] = {0};
  int hlit = 257, hdist = 1, hclen = 4;
  uint64_t bits = 0;  // the whole header, 3 bits of block type included
};

HeaderCode makeHeader(const Lengths & l) {
  HeaderCode h;
  h.hlit = kLitLenSymbols;
  while (h.hlit > 257 && l.lit[h.hlit - 1] == 0) h.hlit--;
  h.hdist = kDistSymbols;
  while (h.hdist > 1 && l.dist[h.hdist - 1] == 0) h.hdist--;
  std::vector<uint8_t> seq;
  seq.insert(seq.end(), l.lit, l.lit + h.hlit);
  seq.insert(seq.end(), l.dist, l.dist + h.hdist);
  auto emit = [&](uint8_t symbol, uint8_t extra) {
    h.symbols.push_back({symbol, extra});
    h.freq[symbol]++;
  };
  for (size_t i = 0; i < seq.size();) {
    uint8_t v = seq[i];
    size_t run = 1;
    while (i + run < seq.size() && seq[i + run] == v) run++;
    if (v == 0 && run >= 3) {
      size_t left = run;
      while (left >= 3) {
        size_t r = std::min<size_t>(left, 138);
        if (r >= 11) emit(18, static_cast<uint8_t>(r - 11));
        else emit(17, static_cast<uint8_t>(r - 3));
        left -= r;
      }
      for (; left > 0; left--) emit(0, 0);
    } else if (v != 0 && run >= 4) {
      emit(v, 0);
      size_t left = run - 1;
      while (left >= 3) {
        size_t r = std::min<size_t>(left, 6);
        emit(16, static_cast<uint8_t>(r - 3));
        left -= r;
      }
      for (; left > 0; left--) emit(v, 0);
    } else {
      for (size_t k = 0; k < run; k++) emit(v, 0);
    }
    i += run;
  }
  codeLengths(h.freq, 19, 7, h.clen);
  makeCodes(h.clen, 19, h.ccodes);
  h.hclen = 19;
  while (h.hclen > 4 && h.clen[kClenOrder[h.hclen - 1]] == 0) h.hclen--;
  h.bits = 3 + 5 + 5 + 4 + 3 * static_cast<uint64_t>(h.hclen);
  for (auto & [symbol, extra] : h.symbols) {
    h.bits += h.clen[symbol];
    if (symbol == 16) h.bits += 2;
    else if (symbol == 17) h.bits += 3;
    else if (symbol == 18) h.bits += 7;
  }
  return h;
}

// ---------------------------------------------------------------------------------------------
// Matches

// How many of the first `max` bytes of a and b are equal.
size_t matchLength(const uint8_t * a, const uint8_t * b, size_t max) {
  size_t n = 0;
#if defined(__SSE2__)
  if (!g_portable_only) {
    while (n + 16 <= max) {
      __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + n));
      __m128i y = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + n));
      unsigned mask = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(x, y)));
      if (mask != 0xffff) return n + static_cast<size_t>(std::countr_zero(~mask));
      n += 16;
    }
  }
#endif
  while (n + 8 <= max) {
    uint64_t x, y;
    std::memcpy(&x, a + n, 8);
    std::memcpy(&y, b + n, 8);
    if (x != y) return n + static_cast<size_t>(std::countr_zero(x ^ y)) / 8;
    n += 8;
  }
  while (n < max && a[n] == b[n]) n++;
  return n;
}

struct Match {
  int len = 0, dist = 0;
};

struct LevelParameters {
  int chain;  // candidates looked at
  int nice;   // a match this long is taken at once
  bool lazy;  // a longer match one byte on wins
};

LevelParameters parametersFor(int level) {
  switch (level) {
    case 1: return {1, 64, false};
    case 2: return {16, 128, true};
    default: return {128, kMaxMatch, true};
  }
}

// The hash tables, kept between calls so that a picture does not pay for clearing 256 kB: a position is
// stored plus `base`, which grows by the size of each input, and anything at or below the current
// base is from an earlier input and counts as empty.
struct Scratch {
  std::vector<uint32_t> head, prev;
  uint32_t base = 0;
};

Scratch & scratch(size_t size) {
  thread_local Scratch s;
  if (s.head.empty() || s.base > 0x80000000u - size - 8) {
    s.head.assign(1u << 15, 0);
    s.prev.assign(kWindow, 0);
    s.base = 0;
  }
  return s;
}

class Matcher {
 public:
  Matcher(const uint8_t * data, size_t size, LevelParameters p) : d_(data), n_(size), p_(p), s_(scratch(size)), base_(s_.base) {
    s_.base += static_cast<uint32_t>(size) + 8;  // the next input starts above everything stored for this one
  }

  // Positions below `limit` are findable.
  void insertUpTo(size_t limit) {
    for (; next_insert_ < limit; next_insert_++) {
      if (next_insert_ + 4 > n_) continue;
      uint32_t h = hash(next_insert_);
      s_.prev[next_insert_ & (kWindow - 1)] = s_.head[h];
      s_.head[h] = base_ + static_cast<uint32_t>(next_insert_) + 1;
    }
  }
  // After a match of `len` at `pos`: a long match is not inserted through, only at its ends.
  void afterMatch(size_t pos, size_t len) {
    if (len <= 32) return;
    insertUpTo(pos + 3);
    next_insert_ = std::max(next_insert_, pos + len - 3);
  }

  Match find(size_t pos) const {
    Match best;
    if (pos + 4 > n_) return best;
    size_t max_len = std::min<size_t>(kMaxMatch, n_ - pos);
    int chain = p_.chain;
    uint32_t stored = s_.head[hash(pos)];
    size_t best_len = 2;
    while (stored > base_ && chain-- > 0) {
      size_t c = stored - base_ - 1;
      if (c >= pos || pos - c > kWindow) break;
      // A longer match must agree where the best so far ends.
      if (d_[c + best_len] == d_[pos + best_len]) {
        size_t len = matchLength(d_ + c, d_ + pos, max_len);
        if (len > best_len) {
          best_len = len;
          best = {static_cast<int>(len), static_cast<int>(pos - c)};
          if (len >= static_cast<size_t>(p_.nice) || len == max_len) break;
        }
      }
      uint32_t next = s_.prev[c & (kWindow - 1)];
      if (next <= base_ || next - base_ - 1 >= c) break;
      stored = next;
    }
    // A match of three bytes from far away costs as much as the three bytes.
    if (best.len == 3 && best.dist > kTooFar) best = Match{};
    return best;
  }

  const LevelParameters & parameters() const { return p_; }

 private:
  static constexpr int kHashBits = 15;
  static constexpr int kTooFar = 32768;
  // Three bytes: the shortest match.
  uint32_t hash(size_t pos) const { return ((load32(d_ + pos) & 0xffffffu) * 2654435761u) >> (32 - kHashBits); }

  const uint8_t * d_;
  size_t n_;
  LevelParameters p_;
  Scratch & s_;
  uint32_t base_;
  size_t next_insert_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Blocks

class Encoder {
 public:
  Encoder(const uint8_t * data, size_t size, std::vector<uint8_t> & out) : data_(data), size_(size), bw_(out) { }

  void compress(int level) {
    Matcher matcher(data_, size_, parametersFor(level));
    const bool lazy = matcher.parameters().lazy;
    std::vector<Token> tokens;
    tokens.reserve(std::min<size_t>(size_, kBlockTokens) + 1);
    size_t block_start = 0, pos = 0;
    auto flush = [&](bool last) {
      writeBlock(tokens, block_start, pos, last);
      tokens.clear();
      block_start = pos;
    };
    Match pending;
    bool have_pending = false;
    while (pos < size_) {
      matcher.insertUpTo(pos);
      Match m = have_pending ? pending : matcher.find(pos);
      have_pending = false;
      if (m.len < 3) {
        tokens.push_back({data_[pos], 0});
        pos++;
      } else {
        if (lazy && m.len < 32 && pos + 1 < size_) {
          matcher.insertUpTo(pos + 1);
          Match next = matcher.find(pos + 1);
          if (next.len > m.len) {
            tokens.push_back({data_[pos], 0});
            pos++;
            pending = next;
            have_pending = true;
            if (tokens.size() >= kBlockTokens || pos - block_start >= kBlockBytes) flush(false);
            continue;
          }
        }
        tokens.push_back({static_cast<uint16_t>(m.len), static_cast<uint16_t>(m.dist)});
        matcher.afterMatch(pos, static_cast<size_t>(m.len));
        pos += static_cast<size_t>(m.len);
      }
      if ((tokens.size() >= kBlockTokens || pos - block_start >= kBlockBytes) && pos < size_) flush(false);
    }
    flush(true);
    bw_.align();
  }

 private:
  // One less than 65536, so a block of literals alone fits one stored block.
  static constexpr size_t kBlockTokens = 65535;
  static constexpr size_t kBlockBytes = 65535 - kMaxMatch;

  void writeStored(size_t begin, size_t end, bool last) {
    size_t at = begin;
    do {
      size_t n = std::min<size_t>(65535, end - at);
      bool final_block = last && at + n == end;
      bw_.put(final_block ? 1 : 0, 1);
      bw_.put(0, 2);
      bw_.align();
      uint8_t header[4] = {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(~n), static_cast<uint8_t>(~n >> 8)};
      bw_.bytes(header, 4);
      bw_.bytes(data_ + at, n);
      at += n;
    } while (at < end);
  }

  void writeTokens(const std::vector<Token> & tokens, const Lengths & l) {
    uint16_t lit_codes[kLitLenSymbols] = {0}, dist_codes[kDistSymbols] = {0};
    makeCodes(l.lit, kLitLenSymbols, lit_codes);
    makeCodes(l.dist, kDistSymbols, dist_codes);
    const Symbols & s = symbols();
    for (const Token & t : tokens) {
      if (t.dist == 0) {
        bw_.put(lit_codes[t.value], l.lit[t.value]);
        continue;
      }
      int ls = s.length_symbol[t.value];
      bw_.put(lit_codes[257 + ls], l.lit[257 + ls]);
      if (kLenExtra[ls]) bw_.put(static_cast<uint32_t>(t.value - kLenBase[ls]), kLenExtra[ls]);
      int ds = s.dist_symbol[t.dist];
      bw_.put(dist_codes[ds], l.dist[ds]);
      if (kDistExtra[ds]) bw_.put(static_cast<uint32_t>(t.dist - kDistBase[ds]), kDistExtra[ds]);
    }
    bw_.put(lit_codes[kEndOfBlock], l.lit[kEndOfBlock]);
  }

  void writeBlock(const std::vector<Token> & tokens, size_t begin, size_t end, bool last) {
    Histogram h;
    const Symbols & s = symbols();
    for (const Token & t : tokens) {
      if (t.dist == 0) {
        h.lit[t.value]++;
      } else {
        int ls = s.length_symbol[t.value], ds = s.dist_symbol[t.dist];
        h.lit[257 + ls]++;
        h.dist[ds]++;
        h.extra_bits += kLenExtra[ls] + kDistExtra[ds];
      }
    }
    h.lit[kEndOfBlock] = 1;

    Lengths dynamic;
    codeLengths(h.lit, kLitLenSymbols, 15, dynamic.lit);
    codeLengths(h.dist, kDistSymbols, 15, dynamic.dist);
    bool any_dist = false;
    for (int i = 0; i < kDistSymbols; i++) any_dist |= dynamic.dist[i] != 0;
    if (!any_dist) dynamic.dist[0] = 1;
    HeaderCode header = makeHeader(dynamic);
    uint64_t dynamic_bits = header.bits + payloadBits(h, dynamic);
    Lengths fixed = fixedLengths();
    uint64_t fixed_bits = 3 + payloadBits(h, fixed);
    size_t range = end - begin;
    uint64_t stored_bits = 8 * static_cast<uint64_t>(range) + 40 * ((range + 65534) / 65535 + 1) + 7;

    if (range > 0 && stored_bits < dynamic_bits && stored_bits < fixed_bits) {
      writeStored(begin, end, last);
      return;
    }
    if (fixed_bits <= dynamic_bits) {
      bw_.put(last ? 1 : 0, 1);
      bw_.put(1, 2);
      writeTokens(tokens, fixed);
      return;
    }
    bw_.put(last ? 1 : 0, 1);
    bw_.put(2, 2);
    bw_.put(static_cast<uint32_t>(header.hlit - 257), 5);
    bw_.put(static_cast<uint32_t>(header.hdist - 1), 5);
    bw_.put(static_cast<uint32_t>(header.hclen - 4), 4);
    for (int i = 0; i < header.hclen; i++) bw_.put(header.clen[kClenOrder[i]], 3);
    for (auto & [symbol, extra] : header.symbols) {
      bw_.put(header.ccodes[symbol], header.clen[symbol]);
      if (symbol == 16) bw_.put(extra, 2);
      else if (symbol == 17) bw_.put(extra, 3);
      else if (symbol == 18) bw_.put(extra, 7);
    }
    writeTokens(tokens, dynamic);
  }

  const uint8_t * data_;
  size_t size_;
  BitWriter bw_;
};

}  // namespace

#if defined(__SSE2__)
// 16 bytes at a time: the sum of the bytes, and the sum weighted by how many bytes come after, so a and
// b of the whole run follow from a handful of vector sums.
static void adlerBlocksSse2(const uint8_t *& data, size_t & size, uint64_t & a, uint64_t & b) {
  const __m128i zero = _mm_setzero_si128();
  const __m128i w_lo = _mm_setr_epi16(16, 15, 14, 13, 12, 11, 10, 9), w_hi = _mm_setr_epi16(8, 7, 6, 5, 4, 3, 2, 1);
  while (size >= 16) {
    size_t chunks = std::min<size_t>(size / 16, 346);  // sums that fit 32 bits per lane
    __m128i v_a = zero, v_snap = zero, v_w = zero;
    for (size_t i = 0; i < chunks; i++) {
      v_snap = _mm_add_epi32(v_snap, v_a);
      __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 16 * i));
      v_a = _mm_add_epi32(v_a, _mm_sad_epu8(v, zero));
      v_w = _mm_add_epi32(v_w, _mm_madd_epi16(_mm_unpacklo_epi8(v, zero), w_lo));
      v_w = _mm_add_epi32(v_w, _mm_madd_epi16(_mm_unpackhi_epi8(v, zero), w_hi));
    }
    auto hsum = [](__m128i v) {
      alignas(16) uint32_t lanes[4];
      _mm_store_si128(reinterpret_cast<__m128i *>(lanes), v);
      return static_cast<uint64_t>(lanes[0]) + lanes[1] + lanes[2] + lanes[3];
    };
    b += 16 * (a * chunks + hsum(v_snap)) + hsum(v_w);
    a += hsum(v_a);
    a %= 65521;
    b %= 65521;
    data += 16 * chunks;
    size -= 16 * chunks;
  }
}
#endif

uint32_t adler32(const uint8_t * data, size_t size, uint32_t adler) {
  uint32_t a = adler & 0xffff, b = adler >> 16;
#if defined(__SSE2__)
  if (!g_portable_only && size >= 64) {
    uint64_t a64 = a, b64 = b;
    adlerBlocksSse2(data, size, a64, b64);
    a = static_cast<uint32_t>(a64);
    b = static_cast<uint32_t>(b64);
  }
#endif
  while (size > 0) {
    size_t n = std::min<size_t>(size, 5552);  // the most that cannot overflow 32 bits before the modulo
    size -= n;
    for (; n >= 16; n -= 16, data += 16) {
      for (int k = 0; k < 16; k++) {
        a += data[k];
        b += a;
      }
    }
    for (; n > 0; n--, data++) {
      a += *data;
      b += a;
    }
    a %= 65521;
    b %= 65521;
  }
  return (b << 16) | a;
}

uint32_t crc32(const uint8_t * data, size_t size, uint32_t crc) {
  const auto & t = crcTables().t;
  crc = ~crc;
  while (size >= 8) {
    uint32_t a = load32(data) ^ crc, b = load32(data + 4);
    crc = t[7][a & 0xff] ^ t[6][(a >> 8) & 0xff] ^ t[5][(a >> 16) & 0xff] ^ t[4][a >> 24] ^ t[3][b & 0xff] ^ t[2][(b >> 8) & 0xff] ^
          t[1][(b >> 16) & 0xff] ^ t[0][b >> 24];
    data += 8;
    size -= 8;
  }
  while (size-- > 0) crc = t[0][(crc ^ *data++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

std::vector<uint8_t> zlibCompress(const uint8_t * data, size_t size, int level) {
  level = std::clamp(level, 1, 3);
  // Small pictures cost little to search well, and the fast search does worst on them.
  if (size < 4096) level = std::max(level, 2);
  std::vector<uint8_t> out;
  out.reserve(size / 2 + 64);
  out.push_back(0x78);
  out.push_back(level == 1 ? 0x01 : 0x9c);
  Encoder(data, size, out).compress(level);
  uint32_t check = adler32(data, size);
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(check >> shift));
  return out;
}

void setDeflatePortableOnly(bool portable_only) { g_portable_only = portable_only; }

}  // namespace cellui::term

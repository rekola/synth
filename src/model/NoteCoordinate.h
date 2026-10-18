#ifndef _NOTECOORDINATE_H_
#define _NOTECOORDINATE_H_

#include <cstdint>

// Identifies one note event - or, via withInstance(), one sub-voice
// generated from it - for HashField purposes (dsp/HashField.h; see
// InstrumentVoice.h's own start-phase computation, the
// oscillator array's members, effects/TapeDegradation.cpp's per-instance seed,
// SoundFont.cpp's percussion-position jitter). A value type, not a
// stream: two NoteCoordinates built from the same inputs are
// interchangeable, in any order, from any thread - the whole point of
// HashField's design.
//
// Exactly 4 ints (16 bytes) - not 5 - deliberately: on the SysV x86-64
// ABI, an all-integer aggregate this size is passed in registers (two
// eightbytes); anything larger is MEMORY-classified (passed via a hidden
// pointer) regardless of field count. That only matters for places this
// gets copied/returned *by value* (withInstance() below; a future
// TrackEvent member) - playNote() itself still takes it
// by const& either way, matching SphericalPosition.h's own identically-
// sized (4 floats) convention.
//
// track_id/column mirror GridPosition.h's own track/col addressing
// (minus its UI-only subcol/scope fields, which have no equivalent
// here). absolute_row is the note's row on the arrangement's timeline.
// Built from the note's *authored* position only - never from how
// many rows the transport has actually played (which varies with
// pattern-break jumps/loops and would make the same authored cell draw a
// different coordinate depending on playback history - the exact
// non-reproducible-on-loop-replay bug this class exists to avoid).
class NoteCoordinate {
 public:
  // Default: "no coordinate" - a live note before it's assigned its own
  // identity, or a not-yet-migrated call site. Not itself meant to be fed
  // to HashField for anything that must reproduce.
  constexpr NoteCoordinate() noexcept = default;

  constexpr NoteCoordinate(int track_id, int absolute_row, int column) noexcept
    : track_id_(track_id), absolute_row_(absolute_row), column_(column) { }

  // Derives a child coordinate for a sub-voice generated from this note -
  // an oscillator array's member index, a future nested
  // generator's own local index. Combines (doesn't overwrite)
  // instance_id_, so two nesting levels can't collide on the same child
  // identity even though neither knows about the other - a cheap
  // multiply-mix, not cryptographic strength: real avalanche only needs
  // to happen once, when HashField::hash64() finally turns a coordinate
  // into a value: mixing twice would be redundant, not more correct.
  constexpr NoteCoordinate withInstance(int instance_id) const noexcept {
    NoteCoordinate c = *this;
    c.instance_id_ = c.instance_id_ * 2654435761 + instance_id;
    return c;
  }

  constexpr int getTrackId() const noexcept { return track_id_; }
  constexpr int getAbsoluteRow() const noexcept { return absolute_row_; }
  constexpr int getColumn() const noexcept { return column_; }
  constexpr int getInstanceId() const noexcept { return instance_id_; }

  // The value HashField::unit()/range()/bipolar() actually consume.
  // track_id (20 bits)/absolute_row (28 bits)/column (8 bits) occupy
  // disjoint bits (56 of the 64 total - budgeted generously: 1M tracks,
  // 268M total rows across the whole song, 256 simultaneous note columns,
  // none of which this codebase remotely approaches) so two different
  // (track, row, column) triples can never collide when instance_id_ is
  // its default 0 - the common case, and the only one that needs to
  // decode cleanly back to "which authored note was this" for debugging.
  // instance_id_ is XORed in over the low 32 bits rather than packed into
  // its own disjoint slot - it overlaps column/the low half of
  // absolute_row when nonzero, which only costs clean bit-level
  // decodability for synthetic sub-voice coordinates (nothing a human
  // ever needs to decode by hand), not correctness: HashField::hash64()
  // fully avalanches the result afterward regardless, and a same-bits
  // collision between two unrelated (note, instance_id) pairs is exactly
  // as harmless as any other hash collision would be.
  constexpr int64_t toHashCoord() const noexcept {
    int64_t packed = (static_cast<int64_t>(static_cast<uint32_t>(track_id_) & 0xFFFFFu) << 44)
                    | (static_cast<int64_t>(static_cast<uint32_t>(absolute_row_) & 0xFFFFFFFu) << 16)
                    | (static_cast<int64_t>(static_cast<uint32_t>(column_) & 0xFFu) << 8);
    return packed ^ static_cast<int64_t>(static_cast<uint32_t>(instance_id_));
  }

 private:
  int track_id_ = 0, absolute_row_ = 0, column_ = 0, instance_id_ = 0;
};

#endif

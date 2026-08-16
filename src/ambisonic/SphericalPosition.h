#ifndef _SPHERICALPOSITION_H_
#define _SPHERICALPOSITION_H_

// A voice's position in space: azimuth/elevation in degrees (same
// convention as PanLaw.h's azimuthToPan(), 0 = front, positive = right/up),
// distance in meters. The default (all-zero) value is not "at the
// origin" but "no meaningful direction" - see AmbisonicEncoding.h's
// computeFoaGains(), which treats distance <= 0 as diffuse/undirected
// rather than dividing by zero.
//
// extent: the source's own physical half-width, in meters - a position is
// a point plus a size, not just a point. 0 means a point source. Rendered
// angular half-width is always atan(extent / distance), derived at the
// point something actually needs an angle (percussion-key offsets,
// NoteMultiplier's unison scatter, ...) rather than stored as an angle
// itself.
struct SphericalPosition {
  float azimuth = 0, elevation = 0, distance = 0;
  float extent = 0;
};

#endif

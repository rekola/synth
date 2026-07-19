#ifndef _SPHERICALPOSITION_H_
#define _SPHERICALPOSITION_H_

// A voice's position in space: azimuth/elevation in degrees (same
// convention as PanLaw.h's azimuthToPan(), 0 = front, positive = right/up),
// distance in arbitrary units. The default (all-zero) value is not "at the
// origin" but "no meaningful direction" - see AmbisonicEncoding.h's
// computeFoaGains(), which treats distance <= 0 as diffuse/undirected
// rather than dividing by zero.
struct SphericalPosition {
  float azimuth = 0, elevation = 0, distance = 0;
};

#endif

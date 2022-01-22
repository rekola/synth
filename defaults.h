#ifndef _DEFAULTS_H_
#define _DEFAULTS_H_

// The lower this block size is the more accurate the effects are.
// Increasing the value significantly lowers the CPU usage of the voice rendering.
// If LFO affects the low-pass filter it can be hearable even as low as 8.
#define RENDER_EFFECTSAMPLEBLOCK 64

#endif

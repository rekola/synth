# PocketFFT (C++ header-only)

Vendored from https://github.com/mreineck/pocketfft, `cpp` branch,
commit `c90e55b3d529f8efa40ed01a20de22405f45fc65`.

Single header (`pocketfft_hdronly.h`), BSD-3-Clause licensed (see `LICENSE`
in this directory, and the license notice embedded at the top of the
header itself). Used by `dsp/RealFFT.h` as the engine's FFT backend,
replacing FFTW (GPL-2-or-later) - see `plans/magical-wondering-engelbart.md`
for the full migration plan and rationale.

To update: fetch a newer commit of `pocketfft_hdronly.h` from the same
branch, replace the file here, and update the commit hash above.

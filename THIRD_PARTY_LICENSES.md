# Third-party licenses

This project (`synth`) is MIT-licensed (see `LICENSE`). It vendors a small
amount of third-party source code, and dynamically links against several
system libraries at build/run time. This file consolidates the licensing
obligations that come with that, so they reach anyone running the built
binary, not just anyone reading the repository. See `--licenses` on the
command line to print this file's contents at runtime.

## Vendored source (`third_party/`)

### tinyxml2 (`third_party/tinyxml2/`)

zlib license.

```
Original code by Lee Thomason (www.grinninglizard.com)

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must
not claim that you wrote the original software. If you use this
software in a product, an acknowledgment in the product documentation
would be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
```

### PocketFFT (`third_party/pocketfft/`)

BSD-3-Clause. Vendored from https://github.com/mreineck/pocketfft, `cpp`
branch, commit `c90e55b3d529f8efa40ed01a20de22405f45fc65`. This is the FFT
backend behind `dsp/RealFFT.h` (the live spectrum analyzer and MagLS
binaural precomputation), replacing FFTW (GPL-2-or-later) - see
`plans/magical-wondering-engelbart.md` for the migration this was part of.

```
This file is part of pocketfft.

Copyright (C) 2010-2024 Max-Planck-Society
Copyright (C) 2019-2020 Peter Bell

For the odd-sized DCT-IV transforms:
  Copyright (C) 2003, 2007-14 Matteo Frigo
  Copyright (C) 2003, 2007-14 Massachusetts Institute of Technology

For the prev_good_size search:
  Copyright (C) 2024 Tan Ping Liang, Peter Bell

For the safeguards against integer overflow in good_size search:
  Copyright (C) 2024 Cris Luengo

Authors: Martin Reinecke, Peter Bell

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this
  list of conditions and the following disclaimer in the documentation and/or
  other materials provided with the distribution.
* Neither the name of the copyright holder nor the names of its contributors may
  be used to endorse or promote products derived from this software without
  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Adapted source (not under `third_party/`)

### SoundFont loader (`src/instruments/SoundFont.cpp`, `src/state/EnvelopeState.h`)

Written in-house rather than vendored verbatim, but based on TinySoundFont
(Copyright (C) 2017, 2018 Bernhard Schelling) and SFZero (Copyright (C) 2012
Steve Folta, https://github.com/stevefolta/SFZero), both MIT-licensed. Each
of the two files carries the license text below in its own header.

```
LICENSE (MIT)

Copyright (C) 2021, Mikael Rekola
Based on TinySoundFont, Copyright (C) 2017, 2018 Bernhard Schelling
Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

Permission is hereby granted, free of charge, to any person obtaining a copy of this
software and associated documentation files (the "Software"), to deal in the Software
without restriction, including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Dynamically-linked system libraries

Linked, not vendored/redistributed as source - listed here for completeness,
at a summary level rather than full license text.

| Library | License |
|---|---|
| libsndfile | LGPL-2.1+ |
| libfmt | MIT |
| ALSA (libasound) | LGPL-2.1+ |
| notcurses | Apache-2.0 / MIT |
| libmysofa | BSD-3-Clause |
| libunistring | LGPL-3+ / GPL-2+ |

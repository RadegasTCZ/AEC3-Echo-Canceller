# Third-Party Notices

This project incorporates components from the following third-party projects.

---

## WebRTC (AEC3 Echo Canceller)

- **Source:** https://webrtc.googlesource.com/src/
- **License:** BSD 3-Clause
- **Version:** revision `b58dcf8fec3ad5291fb99c7e29650737ae7db953` (2026-10-02)
- **Usage:** Acoustic Echo Cancellation engine (AEC3) and the APM noise
  suppressor (`modules/audio_processing/ns/`) — headers and pre-built static
  library included in `thirdparty/AEC3/`.

```
Copyright (c) 2011, The WebRTC project authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the name of Google nor the names of its contributors may be used
    to endorse or promote products derived from this software without specific
    prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

WebRTC also comes with Google's additional patent grant, reproduced in
[`LICENSES/WebRTC-PATENTS.txt`](LICENSES/WebRTC-PATENTS.txt).

### Code inside WebRTC that is linked into `AEC3.lib`

**Ooura FFT** (`common_audio/third_party/ooura/`), used by AEC3 and the noise suppressor:

```
http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html
Copyright Takuya OOURA, 1996-2001

You may use, copy, modify and distribute this code for any purpose (include
commercial use) and without fee. Please refer to this package when you modify
this code.
```

**spl_sqrt_floor** (`common_audio/third_party/spl_sqrt_floor/`): written by
Wilco Dijkstra, 1996, and placed in the public domain by its author (see the
`LICENSE` file in that WebRTC directory).

---

## Abseil (absl)

- **Source:** https://github.com/abseil/abseil-cpp
- **License:** Apache License 2.0
- **Version:** commit `972e4ab4a076fca3adbba69b64f2fcdbc78d0e52` (2026-03-11, `head` inline namespace)
- **Usage:** C++ utility library statically linked into `AEC3.lib`.
  Headers included in `thirdparty/AEC3/absl/`.

```
Copyright 2017 The Abseil Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

The full Apache License 2.0 text is in [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt).

---

## JUCE 9

- **Source:** https://juce.com / https://github.com/juce-framework/JUCE
- **License:** GNU Affero General Public License v3 (AGPLv3) / Commercial
- **Usage:** Audio plugin framework, used under its open-source AGPLv3 licence.
  JUCE is not bundled; it must be installed separately to build.

```
JUCE is copyright (c) Raw Material Software Limited.

JUCE is free software: you can redistribute it and/or modify it under the
terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version. See <https://www.gnu.org/licenses/agpl-3.0.html>.
```

This project's own code is licensed under the GPLv3 (see `LICENSE`). Section 13
of both the GPLv3 and the AGPLv3 allows a GPLv3 work to be combined with an
AGPLv3 work; in a distributed build of the plugin, the JUCE part remains under
the AGPLv3.

---

## SpeexDSP (resampler)

- **Source:** https://gitlab.xiph.org/xiph/speexdsp
- **License:** BSD 3-Clause
- **Version:** 1.2.1
- **Usage:** Sample-rate conversion (`resample.c`, quality 5) between the host
  rate and the 48 kHz processing rate -- source in `thirdparty/speexdsp/`.

```
Copyright 2002-2008 	Xiph.org Foundation
Copyright 2002-2008 	Jean-Marc Valin
Copyright 2005-2007	Analog Devices Inc.
Copyright 2005-2008	Commonwealth Scientific and Industrial Research
                        Organisation (CSIRO)
Copyright 1993, 2002, 2006 David Rowe
Copyright 2003 		EpicGames
Copyright 1992-1994	Jutta Degener, Carsten Bormann

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

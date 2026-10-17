## SpeexDSP — `thirdparty/speexdsp/`

- Upstream: https://gitlab.xiph.org/xiph/speexdsp
- Version: 1.2.1, commit `7a15878` (commit id as recorded when the copy was taken; the local reference copy has no git metadata, its `configure.ac` reads `AC_INIT([speexdsp],[1.2.1])`)
- License: BSD-3-Clause (`COPYING` and the header of each file; Xiph.Org Foundation, Jean-Marc Valin and others, see `AUTHORS`)
- Local reference copy used for comparison: a local copy of upstream at `../_upstream/speexdsp-main` relative to the repository (not part of it)

### What is kept

Copied from Noise Suppression v2 (`ThirdParty/speexdsp/`, patched copy); only what the resampler (`resample.c`) needs, built with `FLOATING_POINT`:

- Compiled: `resample.c` (the only .c file; it defines `FLOATING_POINT`, `USE_KISS_FFT`, `EXPORT` itself, so no project-wide defines are needed)
- Headers: `arch.h`, `os_support.h`, `resample_sse.h`, `resample_neon.h`, `speex_config_win.h`, `speex/speex_resampler.h`, `speex/speexdsp_types.h`
  (`arch.h` references `fixed_*.h`/`bfin.h` only in fixed-point branches, which are never taken, so those are not kept; `config.h` is only included with `HAVE_CONFIG_H`, never defined here)
- `COPYING`, `AUTHORS`: verbatim from the upstream root.

### Local patches

1. `#include "speex_config_win.h"` added at the top of `resample.c`.
2. `resample.c` additionally contains, right after that include:
   `#define FLOATING_POINT`, `#define USE_KISS_FFT`, `#define EXPORT`, `#ifdef constant` / `#undef constant` / `#endif`.
3. `speex_config_win.h` is local (not upstream; BSD-3-Clause like the rest of this folder): defines `FLOATING_POINT`, `USE_KISS_FFT`, `EXPORT`; undefines `constant`.

All other kept files are identical to upstream apart from line endings.

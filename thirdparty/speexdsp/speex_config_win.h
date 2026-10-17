/* Copyright (c) 2026 RadegasTCZ. Local file of Noise Suppression (not upstream); BSD-3-Clause, see COPYING. */
/* Windows/MSVC build configuration for the vendored SpeexDSP sources (float build, Kiss FFT). */
#ifndef SPEEX_CONFIG_WIN_H
#define SPEEX_CONFIG_WIN_H

#define FLOATING_POINT
#define USE_KISS_FFT
#define EXPORT

#ifdef constant
#undef constant
#endif

#endif // SPEEX_CONFIG_WIN_H
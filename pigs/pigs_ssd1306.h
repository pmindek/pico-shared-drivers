#pragma once

#include "pigs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wires Pigs to ../ssd1306. Call ssd1306_init() yourself first - Pigs
// never touches hardware directly, only through the backend's function
// pointers - then pigs_init(pigs_backend_ssd1306()).
//
// A pigs_color_t reaching this backend gets quantized down to exactly what
// an OLED can actually show: white, black, or (for anything in between,
// by perceived brightness rather than an exact "is it gray" check) a 50%
// checkerboard dither - see pigs_ssd1306.c. A screen written once against
// full RGB565 colors degrades sensibly here with zero backend-specific
// code of its own.
const pigs_backend_t *pigs_backend_ssd1306(void);

#ifdef __cplusplus
}
#endif

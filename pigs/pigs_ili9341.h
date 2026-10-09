#pragma once

#include "pigs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wires Pigs to ../ili9341. Call ili9341_init() yourself first, then
// pigs_init(pigs_backend_ili9341()). Width/height are the driver's
// framebuffer size (ILI9341_WIDTH/HEIGHT - possibly smaller than the
// 240x320 glass, see ili9341.h), so Pigs lays out against what's actually
// drawable.
const pigs_backend_t *pigs_backend_ili9341(void);

#ifdef __cplusplus
}
#endif

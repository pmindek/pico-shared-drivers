#pragma once

#include "pigs.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wires Pigs to ../st7789. Call st7789_init() yourself first - Pigs never
// touches hardware directly, only through the backend's function pointers
// - then pigs_init(pigs_backend_st7789()).
const pigs_backend_t *pigs_backend_st7789(void);

#ifdef __cplusplus
}
#endif

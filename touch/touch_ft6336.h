#pragma once

#include "ft6336.h"
#include "touch.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wires ../touch to ../ft6336. Set the chip up yourself first
// (ft6336_setup(), ft6336_probe()), then
//   touch_backend_t backend = touch_backend_ft6336(&dev);
//   touch_init(&touch, &backend, &orientation, slop);
// `dev` must stay valid as long as `touch` is used. INT, when wired,
// gates the reads (touch.h's data_ready).
touch_backend_t touch_backend_ft6336(ft6336_t *dev);

#ifdef __cplusplus
}
#endif

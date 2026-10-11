#include "touch_ft6336.h"

static bool read_point(void *ctx, int *x, int *y) { return ft6336_read((ft6336_t *)ctx, x, y); }

static bool data_ready(void *ctx) { return ft6336_int_asserted((const ft6336_t *)ctx); }

touch_backend_t touch_backend_ft6336(ft6336_t *dev)
{
    touch_backend_t backend = {read_point, data_ready, dev};
    return backend;
}

#pragma once

#include "hardware/spi.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// SD card over SPI: init and reading 512-byte blocks. Read-only on purpose -
// the first user (birch, 2026-10-13) only loads files a PC put on the card,
// and a reader can't corrupt anyone's card.
//
// Works with SDSC (byte-addressed, up to 2 GB) and SDHC/SDXC (block-
// addressed) cards in SPI mode; MMC isn't handled. No CRCs (SPI mode leaves
// them off, except CMD0/CMD8's fixed ones).
//
// SHARED BUS: the card usually shares SCK/MOSI with a display. This driver
// sets the SPI clock it needs on every call that touches the card (400 kHz
// for sdcard_init()'s first part, then `fast_hz`) and leaves it there - the
// caller puts its display's clock back afterwards, and must make sure no
// display transfer (DMA included) is running while the card is used, and
// that the display's own CS is high. The SPI frame format must be 8 bits,
// mode 0 (spi_init()'s default). The caller owns spi_init() and the
// SCK/MOSI pins; sdcard_init() sets up MISO's and CS's GPIO itself.

typedef struct {
    spi_inst_t *spi;
    uint cs_pin;
    uint32_t fast_hz;
    bool sdhc;  // block-addressed (SDHC/SDXC)
    bool ready; // sdcard_init() succeeded
    const char *error; // why the last call failed (a short static string)
} sdcard_t;

// Wakes the card up and identifies it. False (and `error` says why) if no
// card answers - "no card" looks the same as a dead one. miso_pin gets
// GPIO_FUNC_SPI; cs_pin becomes an output, high.
bool sdcard_init(sdcard_t *card, spi_inst_t *spi, uint cs_pin, uint miso_pin, uint32_t fast_hz);

// Reads `count` blocks of 512 bytes from block `lba` into buf.
bool sdcard_read_blocks(sdcard_t *card, uint32_t lba, uint8_t *buf, uint32_t count);

#ifdef __cplusplus
}
#endif

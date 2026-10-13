#include "sdcard.h"

#include "pico/time.h"

// See sdcard.h. The SPI-mode init sequence from the SD Physical Layer
// Simplified Specification (section 7): 74+ clocks with CS high, CMD0 (go
// idle, enters SPI mode), CMD8 (interface condition - answers on v2 cards),
// ACMD41 until the card leaves idle (HCS set on v2: we take SDHC), CMD58
// (OCR - CCS says block addressing), CMD16 (512-byte blocks) for SDSC.

#define CMD0 0    // GO_IDLE_STATE
#define CMD8 8    // SEND_IF_COND
#define CMD12 12  // STOP_TRANSMISSION
#define CMD16 16  // SET_BLOCKLEN
#define CMD17 17  // READ_SINGLE_BLOCK
#define CMD18 18  // READ_MULTIPLE_BLOCK
#define CMD55 55  // APP_CMD
#define CMD58 58  // READ_OCR
#define ACMD41 41 // SD_SEND_OP_COND

#define R1_IDLE 0x01
#define R1_ILLEGAL 0x04
#define TOKEN_DATA 0xFE

static uint8_t xfer(sdcard_t *card, uint8_t out)
{
    uint8_t in;
    spi_write_read_blocking(card->spi, &out, &in, 1);
    return in;
}

static void card_select(sdcard_t *card)
{
    gpio_put(card->cs_pin, 0);
    xfer(card, 0xFF);
}

static void card_deselect(sdcard_t *card)
{
    gpio_put(card->cs_pin, 1);
    xfer(card, 0xFF); // the card lets go of MISO on the next clock
}

// Waits until the card stops holding MISO low (busy); false on timeout.
static bool wait_ready(sdcard_t *card, uint32_t timeout_ms)
{
    absolute_time_t until = make_timeout_time_ms(timeout_ms);
    do {
        if (xfer(card, 0xFF) == 0xFF) return true;
    } while (!time_reached(until));
    return false;
}

// A command and its R1 answer (0xFF: none).
static uint8_t command(sdcard_t *card, uint8_t cmd, uint32_t arg)
{
    if (cmd != CMD0 && !wait_ready(card, 300)) return 0xFF;
    uint8_t frame[6] = {(uint8_t)(0x40 | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg,
                        (uint8_t)(cmd == CMD0 ? 0x95 : (cmd == CMD8 ? 0x87 : 0x01))}; // the only two CRCs SPI mode checks
    spi_write_blocking(card->spi, frame, sizeof(frame));
    if (cmd == CMD12) xfer(card, 0xFF); // a stuff byte before its answer
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 10 && (r1 & 0x80); i++) r1 = xfer(card, 0xFF);
    return r1;
}

static uint8_t app_command(sdcard_t *card, uint8_t cmd, uint32_t arg)
{
    uint8_t r1 = command(card, CMD55, 0);
    if (r1 > R1_IDLE) return r1;
    return command(card, cmd, arg);
}

static bool fail(sdcard_t *card, const char *why)
{
    card_deselect(card);
    card->error = why;
    return false;
}

bool sdcard_init(sdcard_t *card, spi_inst_t *spi, uint cs_pin, uint miso_pin, uint32_t fast_hz)
{
    card->spi = spi;
    card->cs_pin = cs_pin;
    card->fast_hz = fast_hz;
    card->ready = false;
    card->sdhc = false;
    card->error = 0;
    gpio_init(cs_pin);
    gpio_put(cs_pin, 1);
    gpio_set_dir(cs_pin, GPIO_OUT);
    gpio_set_function(miso_pin, GPIO_FUNC_SPI);
    gpio_pull_up(miso_pin); // an absent card leaves MISO floating - read 0xFF, not noise

    spi_set_baudrate(spi, 400000);
    for (int i = 0; i < 10; i++) xfer(card, 0xFF); // 80 clocks with CS high

    card_select(card);
    uint8_t r1 = 0xFF;
    for (int i = 0; i < 20 && r1 != R1_IDLE; i++) r1 = command(card, CMD0, 0);
    if (r1 != R1_IDLE) return fail(card, "no card");

    bool v2 = false;
    r1 = command(card, CMD8, 0x1AA);
    if (r1 == R1_IDLE) {
        uint8_t r7[4];
        for (int i = 0; i < 4; i++) r7[i] = xfer(card, 0xFF);
        if (r7[3] != 0xAA) return fail(card, "bad CMD8 echo");
        v2 = true;
    } else if (!(r1 & R1_ILLEGAL)) {
        return fail(card, "bad CMD8");
    }

    absolute_time_t until = make_timeout_time_ms(1500);
    do {
        r1 = app_command(card, ACMD41, v2 ? 0x40000000u : 0);
        if (r1 == 0) break;
    } while (!time_reached(until));
    if (r1 != 0) return fail(card, "init timeout");

    if (v2) {
        if (command(card, CMD58, 0) != 0) return fail(card, "bad CMD58");
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) ocr[i] = xfer(card, 0xFF);
        card->sdhc = (ocr[0] & 0x40) != 0;
    }
    if (!card->sdhc && command(card, CMD16, 512) != 0) return fail(card, "bad CMD16");
    card_deselect(card);
    spi_set_baudrate(spi, fast_hz);
    card->ready = true;
    return true;
}

// One data block after a read command: the start token, 512 bytes, the CRC.
static bool read_data(sdcard_t *card, uint8_t *buf)
{
    absolute_time_t until = make_timeout_time_ms(200);
    uint8_t token;
    do {
        token = xfer(card, 0xFF);
    } while (token == 0xFF && !time_reached(until));
    if (token != TOKEN_DATA) return false;
    spi_read_blocking(card->spi, 0xFF, buf, 512);
    xfer(card, 0xFF); // CRC, unchecked
    xfer(card, 0xFF);
    return true;
}

bool sdcard_read_blocks(sdcard_t *card, uint32_t lba, uint8_t *buf, uint32_t count)
{
    if (!card->ready) {
        card->error = "not ready";
        return false;
    }
    if (count == 0) return true;
    spi_set_baudrate(card->spi, card->fast_hz);
    uint32_t addr = card->sdhc ? lba : lba * 512u;
    card_select(card);
    if (count == 1) {
        if (command(card, CMD17, addr) != 0) return fail(card, "bad CMD17");
        if (!read_data(card, buf)) return fail(card, "read timeout");
        card_deselect(card);
        return true;
    }
    if (command(card, CMD18, addr) != 0) return fail(card, "bad CMD18");
    for (uint32_t i = 0; i < count; i++) {
        if (!read_data(card, buf + i * 512u)) {
            command(card, CMD12, 0);
            return fail(card, "read timeout");
        }
    }
    command(card, CMD12, 0);
    wait_ready(card, 300);
    card_deselect(card);
    return true;
}

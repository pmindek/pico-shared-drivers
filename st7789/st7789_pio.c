#include "st7789_pio.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio_instructions.h"
#include "st7789.h"

// See st7789_pio.h. The program (built at run time from the SDK's
// encoders - no pioasm step for the projects using this): SPI mode 0, MSB
// first, two cycles a bit (data out with SCK low, then SCK high). The OSR
// takes a 16-bit pixel at a time (autopull at 16, shifting left: a DMA
// halfword lands in both halves of the FIFO word, the upper copy goes out);
// red's top 4 bits are clocked out, its lowest skipped (shifted, no clock),
// then green's top 4 (2 skipped), blue's top 4 (1 skipped): 12 bits a
// pixel, which is the panel's 12-bit format (3 bytes per 2 pixels).

static PIO s_pio;
static uint s_sm;
static int s_dma = -1;
static uint s_sck, s_mosi;
static bool s_ready = false, s_going = false;
static uint16_t s_program[32];

static void add_bits(int *n, int bits)
{
    for (int i = 0; i < bits; i++) {
        s_program[(*n)++] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(1, 0));
        s_program[(*n)++] = (uint16_t)(pio_encode_nop() | pio_encode_sideset(1, 1));
    }
}

static void skip_bits(int *n, int bits) { s_program[(*n)++] = (uint16_t)(pio_encode_out(pio_null, bits) | pio_encode_sideset(1, 0)); }

bool st7789_pio_init(PIO pio, uint sck_pin, uint mosi_pin, uint32_t bit_hz)
{
    int n = 0;
    add_bits(&n, 4); // red 4..1
    skip_bits(&n, 1);
    add_bits(&n, 4); // green 5..2
    skip_bits(&n, 2);
    add_bits(&n, 4); // blue 4..1
    skip_bits(&n, 1);
    struct pio_program program = {s_program, (uint8_t)n, -1};
    if (!pio_can_add_program(pio, &program)) return false;
    int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) return false;
    uint offset = pio_add_program(pio, &program);

    s_pio = pio;
    s_sm = (uint)sm;
    s_sck = sck_pin;
    s_mosi = mosi_pin;
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + (uint)n - 1);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, sck_pin);
    sm_config_set_out_pins(&c, mosi_pin, 1);
    sm_config_set_out_shift(&c, false, true, 16); // left (MSB first), autopull at 16
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (2.0f * (float)bit_hz));
    pio_sm_init(pio, s_sm, offset, &c);
    // Both pins outputs, low (SCK idles low), whenever they're the PIO's.
    pio_sm_set_pins_with_mask(pio, s_sm, 0, (1u << sck_pin) | (1u << mosi_pin));
    pio_sm_set_pindirs_with_mask(pio, s_sm, (1u << sck_pin) | (1u << mosi_pin), (1u << sck_pin) | (1u << mosi_pin));
    pio_sm_set_enabled(pio, s_sm, true); // stalls on its first pull until a push
    s_dma = dma_claim_unused_channel(true);
    s_ready = true;
    return true;
}

bool st7789_pio_busy(void) { return s_going; }

bool st7789_pio_push_async(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!s_ready) return false;
    st7789_pio_push_wait();
    if (!st7789_raw_write_begin(x, y, w, h)) return false;
    // The pins to the PIO for the push.
    pio_gpio_init(s_pio, s_sck);
    pio_gpio_init(s_pio, s_mosi);
    s_pio->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + s_sm); // cleared, so the wait sees this push's end
    dma_channel_config config = dma_channel_get_default_config((uint)s_dma);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_16);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, pio_get_dreq(s_pio, s_sm, true));
    s_going = true;
    dma_channel_configure((uint)s_dma, &config, &s_pio->txf[s_sm], pixels, (uint)(w * h), true);
    return true;
}

void st7789_pio_push_wait(void)
{
    if (!s_going) return;
    dma_channel_wait_for_finish_blocking((uint)s_dma);
    // The FIFO empty, then the state machine stalled on its next pull: the
    // last pixel's bits are out.
    uint32_t stalled = 1u << (PIO_FDEBUG_TXSTALL_LSB + s_sm);
    s_pio->fdebug = stalled;
    while (!pio_sm_is_tx_fifo_empty(s_pio, s_sm)) tight_loop_contents();
    while (!(s_pio->fdebug & stalled)) tight_loop_contents();
    st7789_raw_write_end();
    gpio_set_function(s_sck, GPIO_FUNC_SPI);
    gpio_set_function(s_mosi, GPIO_FUNC_SPI);
    s_going = false;
}

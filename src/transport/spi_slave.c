#include "spi_slave.h"
#include "ring_buffer.h"
#include "feature_flags.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

// ---------------------------------------------------------------------------
// PIO program — SPI Mode 0 receiver (MSB-first, 3 instructions, wrap 0->2)
// ---------------------------------------------------------------------------
// [0] wait 1 gpio <SCK>   wait SCK HIGH (rising edge)
// [1] in   pins, 1        sample MOSI (in_base = PIN_MOSI)
// [2] wait 0 gpio <SCK>   wait SCK LOW
//
// The <SCK> field (bits [4:0] of WAIT instructions) is patched at runtime
// from PIN_SCK so this is safe even if feature_flags.h changes the pin.
// WAIT 1 GPIO template: 0x2080 | (pin & 0x1F)
// WAIT 0 GPIO template: 0x2000 | (pin & 0x1F)
// ---------------------------------------------------------------------------
static uint16_t spi_slave_program_instructions[3];  // filled by _spi_slave_pio_init

#define SPI_SLAVE_PROG_LEN  3u
#define SPI_SLAVE_WRAP_BOT  0u
#define SPI_SLAVE_WRAP_TOP  2u

// DMA channel reserved for SPI → ring-buffer transfers
#define SPI_DMA_CHAN  0

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void _spi_slave_pio_init(void)
{
    // Patch the WAIT instructions with the actual PIN_SCK at runtime.
    // This makes the program correct even if feature_flags.h changes PIN_SCK.
    //   WAIT 1 GPIO <pin>: 0x2080 | (pin & 0x1F)
    //   IN   pins, 1     : 0x4001  (no pin index in this instruction)
    //   WAIT 0 GPIO <pin>: 0x2000 | (pin & 0x1F)
    spi_slave_program_instructions[0] = (uint16_t)(0x2080u | (PIN_SCK & 0x1Fu));
    spi_slave_program_instructions[1] = 0x4001u;
    spi_slave_program_instructions[2] = (uint16_t)(0x2000u | (PIN_SCK & 0x1Fu));

    // Load program into PIO1
    uint offset = pio_add_program_at_offset(pio1, &(pio_program_t){
        .instructions = spi_slave_program_instructions,
        .length       = SPI_SLAVE_PROG_LEN,
        .origin       = -1,
    }, 0);

    // Configure SM0 on PIO1
    pio_sm_config cfg = pio_get_default_sm_config();

    // in_base = GP0 (MOSI); only 1 pin needed for IN
    sm_config_set_in_pins(&cfg, PIN_MOSI);

    // Auto-push, 8-bit threshold, shift left (MSB first)
    sm_config_set_in_shift(&cfg, /*shift_dir_right=*/false, /*autopush=*/true, /*threshold=*/8);

    // Clock divider = 1.0 (full system clock)
    sm_config_set_clkdiv(&cfg, 1.0f);

    // Wrap
    sm_config_set_wrap(&cfg, offset + SPI_SLAVE_WRAP_BOT, offset + SPI_SLAVE_WRAP_TOP);

    // Claim + configure the state machine
    pio_sm_init(pio1, 0, offset, &cfg);

    // Configure MOSI pin as PIO1 input
    pio_gpio_init(pio1, PIN_MOSI);
    pio_sm_set_consecutive_pindirs(pio1, 0, PIN_MOSI, 1, /*is_out=*/false);

    // Enable SM
    pio_sm_set_enabled(pio1, 0, true);
}

static void _spi_slave_dma_init(void)
{
    rb_init();

    dma_channel_config cfg = dma_channel_get_default_config(SPI_DMA_CHAN);

    // Transfer 1 byte per trigger
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);

    // Source: PIO1 RX FIFO (fixed address, no increment)
    channel_config_set_read_increment(&cfg, false);

    // Destination: ring buffer with hardware ring-wrap
    // ring_size = 12 → 2^12 = 4096-byte ring on write side
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_ring(&cfg, /*write=*/true, /*size_bits=*/12);

    // Trigger on PIO1 RX FIFO not-empty (one byte per dreq)
    channel_config_set_dreq(&cfg, pio_get_dreq(pio1, 0, /*is_tx=*/false));

    // Transfer count: UINT32_MAX = run forever.
    // DREQ from PIO RX FIFO throttles actual byte rate; the ring wrap
    // (set_ring, size_bits=12) handles the circular overwrite behaviour.
    // Using RING_BUFFER_SIZE here would halt DMA after 4096 bytes — a
    // silent data-loss bug for any transfer longer than the ring.
    dma_channel_configure(
        SPI_DMA_CHAN,
        &cfg,
        rb_get_buffer(),                          // write: ring buffer base
        &pio1->rxf[0],                            // read:  PIO1 SM0 RX FIFO
        UINT32_MAX,                               // run forever; dreq controls rate
        /*trigger=*/true
    );
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void spi_slave_init(void)
{
    // GPIO configuration
    // MOSI (GP0): PIO-owned input — handled by pio_gpio_init inside PIO init
    // CS   (GP1): input with pull-up (active-low)
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_IN);
    gpio_pull_up(PIN_CS);

    // SCK  (GP2): input
    gpio_init(PIN_SCK);
    gpio_set_dir(PIN_SCK, GPIO_IN);

    // MISO (GP3): output, idle LOW
    gpio_init(PIN_MISO);
    gpio_set_dir(PIN_MISO, GPIO_OUT);
    gpio_put(PIN_MISO, 0);

    // DC   (GP4): input
    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_IN);

    // BUSY (GP5): output, initially de-asserted (LOW)
    gpio_init(PIN_BUSY);
    gpio_set_dir(PIN_BUSY, GPIO_OUT);
    gpio_put(PIN_BUSY, 0);

    // TE   (GP6): input (tearing-effect from display)
    gpio_init(PIN_TE);
    gpio_set_dir(PIN_TE, GPIO_IN);

    // RESET (GP7): output, hold HIGH (active-low reset)
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_OUT);
    gpio_put(PIN_RESET, 1);

    // PIO and DMA
    _spi_slave_pio_init();
    _spi_slave_dma_init();
}

void spi_slave_send_response(const uint8_t *data, uint32_t len)
{
    // Software bit-bang on MISO, clocked by host SCK
    gpio_set_dir(PIN_MISO, GPIO_OUT);

    for (uint32_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int bit = 7; bit >= 0; bit--) {
            gpio_put(PIN_MISO, (byte >> bit) & 1u);
            // Wait for rising edge
            while (!gpio_get(PIN_SCK)) { tight_loop_contents(); }
            // Wait for falling edge (bit clocked in by host)
            while ( gpio_get(PIN_SCK)) { tight_loop_contents(); }
        }
    }

    gpio_put(PIN_MISO, 0);
    // Tri-state MISO: release the line so it doesn't fight other devices
    // if the bus is ever shared. Also guards against floating-input issues
    // on the host side between query transactions.
    gpio_set_dir(PIN_MISO, GPIO_IN);
}

void spi_set_busy(bool busy)
{
    gpio_put(PIN_BUSY, busy ? 1u : 0u);
}

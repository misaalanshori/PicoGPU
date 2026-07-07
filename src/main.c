// main.c — PicoGPU Coprocessor Firmware Entry Point (RP2350)
// Spec §3, TIP §6.1: Hardware init sequence, Core 1 launch, ring buffer processing loop.
// THIS FILE REPLACES the previous pico_hdmi demo (metaballs/plasma/etc).

#include "feature_flags.h"
#include "error_codes.h"
#include "opcodes.h"

#include "state/coprocessor_state.h"
#include "hal/rp2350/display_rp2350.h"
#include "transport/ring_buffer.h"
#include "transport/spi_slave.h"
#include "protocol/packets.h"
#include "protocol/dispatch.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/busctrl.h"

// =============================================================================
// RESET pin interrupt handler
// Triggered by active-LOW pulse on GP7 from host (spec §3.5)
// =============================================================================
static volatile bool g_reset_requested = false;

static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == PIN_RESET && (events & GPIO_IRQ_EDGE_FALL)) {
        g_reset_requested = true;
    }
}

// =============================================================================
// Core 1 entry — runs pico_hdmi DMA ISR loop forever (never returns)
// Must be launched AFTER display_rp2350_init().
// =============================================================================
static void core1_entry(void) {
    display_rp2350_core1_entry();
}

// =============================================================================
// Hardware initialization (TIP §6.1)
// Critical: order matters — voltage before frequency, DMA priority before DMA config.
// =============================================================================
static void hw_init_all(void) {
    // ── Step 1: Set voltage BEFORE raising frequency (TIP §6.1) ─────────────
#if CPU_SPEED_FAST
    vreg_set_voltage(VREG_VOLTAGE_1_30);  // 384 MHz @ 1.30V (spec §4.1)
#else
    vreg_set_voltage(VREG_VOLTAGE_1_10);  // 240 MHz @ 1.10V (conservative)
#endif
    sleep_ms(5); // voltage settling time

    // ── Step 2: Set system clock ─────────────────────────────────────────────
#if CPU_SPEED_FAST
    set_sys_clock_khz(384000, true);      // 384 MHz
#else
    set_sys_clock_khz(240000, true);      // 240 MHz
#endif

    // ── Step 3: Elevate DMA bus priority (spec §4.2, TIP §4.3 item 5) ───────
    // MUST happen before any DMA channel is configured.
    // Prevents HSTX DMA timing corruption under heavy Core 0 SRAM access.
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                           BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // ── Step 4: Redirect clk_usb from pll_sys (frees pll_usb for HSTX) ─────
    // clk_usb = clk_sys / 8 = 384 / 8 = 48 MHz  (TIP §6.2)
    clock_configure(clk_usb, 0,
        CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        384 * MHZ, 48 * MHZ);

    // ── Step 5: Init pico_hdmi HAL (UNINITIALIZED — no DVI output yet) ──────
    display_rp2350_init();

    // ── Step 6: Init SPI slave PIO + ring buffer ─────────────────────────────
    rb_init();
    spi_slave_init();

    // ── Step 7: Configure control pins ───────────────────────────────────────
    // BUSY (GP5): output, LOW initially
    gpio_init(PIN_BUSY);
    gpio_set_dir(PIN_BUSY, GPIO_OUT);
    gpio_put(PIN_BUSY, 0);

    // TE (GP6): output — managed by display HAL scanline callback
    // (already configured by display_rp2350_init)

    // RESET (GP7): input with pull-up, active LOW — triggers soft reset
    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_IN);
    gpio_pull_up(PIN_RESET);
    gpio_set_irq_enabled_with_callback(PIN_RESET, GPIO_IRQ_EDGE_FALL,
                                       true, gpio_irq_handler);

    // ── Step 8: Init protocol layers ─────────────────────────────────────────
    packets_init();
    dispatch_init();

    // ── Step 9: Init coprocessor state to UNINITIALIZED defaults ────────────
    coprocessor_state_init();
}

// =============================================================================
// Main — GPU coprocessor firmware entry point
// =============================================================================
int main(void) {
    // Initialize all hardware in the correct sequence
    hw_init_all();

    // Launch Core 1 — runs pico_hdmi polling loop + DMA ISR handler
    // Core 1 is launched last, after all HW is configured (spec §5.1)
    multicore_launch_core1(core1_entry);

    // ── Core 0: Ring buffer processing loop (runs forever) ───────────────────
    // Drains validated packets from ring buffer and dispatches them.
    // This is the "GPU work" loop — packet parse + draw + query response.
    while (1) {
        // Check for RESET pulse from host
        if (g_reset_requested) {
            g_reset_requested = false;
            dispatch_command(OP_SOFT_RESET, NULL, 0);
        }

        // Process as many complete packets as possible from the ring buffer
        packets_process();

        // After dispatch, check if a query response was queued
        uint32_t resp_len;
        const uint8_t *resp = dispatch_get_response(&resp_len);
        if (resp_len > 0) {
            // Block until host reads the response (host must wait for CS low)
            // The host asserts CS for MISO read after BUSY goes low + 50 µs.
            // We output via software bit-bang synchronized to SCK.
            spi_slave_send_response(resp, resp_len);
        }

        // Update BUSY pin based on ring buffer fill level
        spi_set_busy(rb_is_busy());
    }

    // Unreachable
    return 0;
}

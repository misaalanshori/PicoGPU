// main.c — PicoGPU Coprocessor Firmware Entry Point (RP2350)
// Spec §3, TIP §6.1.
//
// Clock architecture:
//   pll_sys  → clk_sys  = 432 MHz (CPU_SPEED_FAST=1) or 384 MHz
//   pll_sys  → clk_usb  = 48 MHz  (pll_sys / 9 or /8, rerouted from pll_usb)
//   pll_sys  → clk_adc  = 48 MHz
//   pll_usb  → clk_hstx = 372 MHz (1116 MHz VCO / 3 / 1) for 720p60
//            or           126 MHz (756 MHz VCO / 6 / 1) for 480p60
//
//   QMI (QSPI flash) divider must be set BEFORE raising clk_sys above the
//   XIP clock's safe range. The RP2350 default QMI clkdiv is 1 (pass-through
//   at 150 MHz chip limit). At 432/384 MHz we need divider = ceil(432/150) = 4
//   to keep XIP at ≤108 MHz.

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
#include "hardware/pll.h"
#include "hardware/gpio.h"
#include "hardware/structs/busctrl.h"
#include "hardware/structs/qmi.h"

// =============================================================================
// RESET pin interrupt handler (GP7, active-LOW)
// =============================================================================
static volatile bool g_reset_requested = false;

static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == PIN_RESET && (events & GPIO_IRQ_EDGE_FALL)) {
        g_reset_requested = true;
    }
}

// =============================================================================
// Core 1 entry — runs pico_hdmi DMA/ISR loop, never returns
// =============================================================================
static void core1_entry(void) {
    display_rp2350_core1_entry();
}

// =============================================================================
// hw_init_all — full hardware init in the mandated order (TIP §6.1)
//
// CRITICAL ORDER:
//   1. VREG voltage set BEFORE raising frequency.
//   2. QMI (QSPI flash) clock divider set BEFORE raising pll_sys.
//   3. pll_sys to target frequency.
//   4. clk_usb / clk_adc re-routed to keep 48 MHz.
//   5. pll_usb re-purposed for HSTX (initial boot: 720p VCO = 372 MHz).
//   6. clk_hstx pointed at pll_usb.
//   7. DMA bus priority elevated (must precede DMA channel configuration).
//   8. GPU peripheral init (display HAL → SPI slave → protocol).
// =============================================================================
static void hw_init_all(void) {

    // ── Step 1: Voltage ─────────────────────────────────────────────────────
#if CPU_SPEED_FAST
    // 432 MHz @ 1.40 V (user-verified stable setting)
    vreg_disable_voltage_limit();
    vreg_set_voltage(VREG_VOLTAGE_1_40);
#else
    // 384 MHz @ 1.30 V (conservative)
    vreg_set_voltage(VREG_VOLTAGE_1_30);
#endif
    sleep_ms(20);  // allow VREG to settle

    // ── Step 2: QMI (QSPI flash) clock divider ──────────────────────────────
    // Must be done BEFORE pll_sys is raised above ~200 MHz.
    // Divider 4 → flash sees sys_clk/4 = 432/4 = 108 MHz (below 150 MHz limit).
    // The "read once to force cache fill" is required to safely modify QMI timing.
    qmi_hw->m[0].timing = (qmi_hw->m[0].timing & ~(uint32_t)0xFF) | 4u;

    // Force a flash read to bring the modified XIP config into effect
    volatile uint32_t *flash_ptr = (volatile uint32_t *)0x10000000;
    (void)*flash_ptr;

    // ── Step 3: pll_sys → clk_sys ───────────────────────────────────────────
#if CPU_SPEED_FAST
    set_sys_clock_khz(432000, true);   // 432 MHz
#else
    set_sys_clock_khz(384000, true);   // 384 MHz
#endif

    // ── Step 4: clk_usb and clk_adc from pll_sys (48 MHz each) ─────────────
    // pll_usb will be repurposed for HSTX, so USB/ADC must derive from pll_sys.
    // 432 / 9 = 48 MHz; 384 / 8 = 48 MHz.
#if CPU_SPEED_FAST
    clock_configure(clk_usb, 0,
        CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        432 * MHZ, 48 * MHZ);
    clock_configure(clk_adc, 0,
        CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        432 * MHZ, 48 * MHZ);
#else
    clock_configure(clk_usb, 0,
        CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        384 * MHZ, 48 * MHZ);
    clock_configure(clk_adc, 0,
        CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        384 * MHZ, 48 * MHZ);
#endif

    // ── Step 5: pll_usb → 372 MHz (initial VCO = 1116 MHz / 3 / 1) ─────────
    // This is the correct VCO configuration for 720p60 HSTX clock (TIP §6.2).
    // SYSTEM_CONFIG will re-init pll_usb for the actual profile's timing later;
    // this just gives us a safe, working HSTX clock for the boot state.
    // REFDIV=1, FBDIV=93 (1116/12=93), POSTDIV1=3, POSTDIV2=1 → 372 MHz output.
    pll_init(pll_usb, 1, 1116 * MHZ, 3, 1);

    // ── Step 6: clk_hstx from pll_usb ───────────────────────────────────────
    clock_configure(clk_hstx, 0,
        CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
        372 * MHZ, 372 * MHZ);

    // ── Step 7: DMA bus priority (before ANY DMA channel is configured) ─────
    // Prevents HSTX timing glitches when Core 0 hammers SRAM with pixel writes.
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                           BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // ── Step 8: GPU peripherals ──────────────────────────────────────────────
    // 8a. Display HAL (init TE GPIO; does NOT start DVI output yet)
    display_rp2350_init();

    // 8b. Ring buffer + SPI slave PIO
    rb_init();
    spi_slave_init();

    // 8c. Control pins
    gpio_init(PIN_BUSY);
    gpio_set_dir(PIN_BUSY, GPIO_OUT);
    gpio_put(PIN_BUSY, 0);

    gpio_init(PIN_RESET);
    gpio_set_dir(PIN_RESET, GPIO_IN);
    gpio_pull_up(PIN_RESET);
    gpio_set_irq_enabled_with_callback(PIN_RESET, GPIO_IRQ_EDGE_FALL,
                                       true, gpio_irq_handler);

    // 8d. Protocol layers
    packets_init();
    dispatch_init();

    // 8e. Coprocessor state to UNINITIALIZED defaults
    coprocessor_state_init();
}

// =============================================================================
// main
// =============================================================================
int main(void) {
    hw_init_all();

    // Launch Core 1: starts pico_hdmi Core1 loop (DVI output begins only after
    // SYSTEM_CONFIG calls video_output_start() via display_rp2350_apply_profile).
    multicore_launch_core1(core1_entry);

    // Inform the display HAL that Core 1 is now live (needed for profile-switch
    // logic inside display_rp2350_apply_profile).
    display_rp2350_mark_core1_started();

    // ── Core 0: Main GPU processing loop ────────────────────────────────────
    while (1) {
        // Handle RESET pin assertion from host
        if (g_reset_requested) {
            g_reset_requested = false;
            dispatch_command(OP_SOFT_RESET, NULL, 0);
        }

        // Drain complete validated packets from the ring buffer
        packets_process();

        // Send any queued query response back to host over MISO
        uint32_t resp_len;
        const uint8_t *resp = dispatch_get_response(&resp_len);
        if (resp_len > 0) {
            spi_slave_send_response(resp, resp_len);
        }

        // Update BUSY pin based on ring buffer fill level
        spi_set_busy(rb_is_busy());
    }

    return 0;
}

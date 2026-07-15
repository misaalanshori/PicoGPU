# PicoGPU

A dedicated SPI graphics coprocessor firmware for the **Raspberry Pi RP2350** (Pico 2). Offloads all display rendering from your host MCU — framebuffer management, drawing primitives, sprite blitting, text, tilemaps, and DVI/HDMI output — over a simple packet-based SPI link.

The host sends commands. The GPU draws. Your main MCU never touches a pixel.

---

## What It Is

PicoGPU turns a Raspberry Pi Pico 2 into a standalone display controller. Connect it to a monitor via DVI/HDMI, connect your host MCU via SPI, and drive it using the [host SDK](PicoGPU/host_sdk/).

```
Your MCU  ──── SPI ────►  PicoGPU (RP2350)  ──── DVI/HDMI ────►  Monitor
              ◄──── MISO ─────                ◄──── BUSY/TE ─────
```

**Current implementation status:** Phases 0–3 complete. Pawn VM scripting (Phase 4) is defined and stubbed.

---

## Hardware Requirements

- **Raspberry Pi Pico 2** (RP2350) — primary target
- 16 MB flash variant recommended (large sprites, display lists)
- DVI output requires 4× 220Ω resistors (one per differential pair)
- Any host MCU with an SPI master and a few GPIO pins

---

## Pin Assignment

| GPIO | Signal | Direction | Notes |
|---|---|---|---|
| GP0 | MOSI | Host → GPU | SPI data in |
| GP1 | CS | Host → GPU | Active LOW, managed per-packet |
| GP2 | SCK | Host → GPU | SPI clock |
| GP3 | MISO | GPU → Host | Query responses only |
| GP4 | D/C | Host → GPU | LOW = command byte, HIGH = payload |
| GP5 | BUSY | GPU → Host | HIGH when ring buffer ≥80% full or during init |
| GP6 | TE | GPU → Host | Pulses HIGH during VBLANK |
| GP7 | RESET | Host → GPU | Active LOW; returns GPU to UNINITIALIZED |
| GP12 | DVI CK− | GPU → Monitor | Clock differential pair |
| GP13 | DVI CK+ | GPU → Monitor | |
| GP14 | DVI D0− | GPU → Monitor | Blue lane |
| GP15 | DVI D0+ | GPU → Monitor | |
| GP16 | DVI D1− | GPU → Monitor | Green lane |
| GP17 | DVI D1+ | GPU → Monitor | |
| GP18 | DVI D2− | GPU → Monitor | Red lane |
| GP19 | DVI D2+ | GPU → Monitor | |

**DVI wiring:** 220Ω series resistors on GP12–GP19 are required for correct signal levels. Direct connection without resistors may damage the monitor's HDMI port.

---

## Building and Flashing

### Prerequisites

- [PlatformIO](https://platformio.org/) with the `maxgerhardt/platform-raspberrypi` platform
- The `pico_hdmi` library submodule (included in this repo as a sibling directory)

### Build

```bash
cd PicoGPU
pio run -e rp2350_fast
```

The default `rp2350_fast` environment builds with all Phase 0–3 features enabled and the optional 432 MHz CPU overclock.

### Flash

Hold the BOOTSEL button on the Pico 2, connect USB, release BOOTSEL, then:

```bash
pio run -e rp2350_fast --target upload
```

Or copy `.pio/build/rp2350_fast/firmware.uf2` to the RPI-RP2 mass storage drive.

---

## Feature Flags

All features are enabled or disabled at compile time via `-D` flags in `platformio.ini`. The firmware dynamically reports active features to the host via `GET_CAPABILITIES` (opcode `0xE7`).

| Flag | Default | Description |
|---|---|---|
| `CPU_SPEED_FAST` | `1` | 432 MHz @ 1.40 V (overclock). Set `0` for spec-safe 384 MHz @ 1.30 V |
| `FEATURE_TARGET_RP2350` | `1` | RP2350 build — enables HSTX, hardware FPU paths |
| `FEATURE_RGB565` | `1` | 16bpp profiles available |
| `FEATURE_FPU_PRIMITIVES` | `1` | Bézier curves, gradient fills, Gouraud triangle |
| `FEATURE_DITHERING` | `1` | Bayer ordered dither (2×2 and 4×4) |
| `FEATURE_NAMED_VRAM` | `1` | Named VRAM slot management (64 entries) |
| `FEATURE_DISPLAY_LIST` | `1` | Display list recording and playback |
| `FEATURE_BEGIN_END_FRAME` | `1` | BEGIN/END_FRAME frame lifecycle commands |
| `FEATURE_FRAME_STATS` | `1` | Per-frame render time and ring buffer stats |
| `FEATURE_EVENT_BUFFER` | `1` | Unsolicited async event ring (16 entries) |
| `FEATURE_DEFERRED_DRAW` | `1` | Single-deferred draw queue for single-buffer profiles |
| `FEATURE_CAPTURE_REGION` | `1` | Framebuffer→VRAM region capture |
| `FEATURE_ARBITRARY_ROTATION` | `0` | Non-orthogonal sprite rotation (FPU, planned) |
| `FEATURE_VECTOR_FONTS` | `0` | stb_truetype scalable fonts (planned) |
| `FEATURE_PAWN_VM` | `0` | Pawn scripting VM (Phase 4, planned) |

> Setting `CPU_SPEED_FAST=0` disables the VREG limit override and runs at 384 MHz / 1.30 V, which is within Raspberry Pi's documented RP2350 envelope.

---

## Clock Architecture

The RP2350 has two PLLs. PicoGPU repurposes both:

```
Crystal (12 MHz)
├── pll_sys  → clk_sys   = 432 MHz (CPU_SPEED_FAST=1) or 384 MHz
│            → clk_usb   = 48 MHz  (pll_sys / 9 or / 8)
│            → clk_adc   = 48 MHz
└── pll_usb  → clk_hstx  = 372 MHz  (720p60:  VCO=1116 MHz / 3 / 1)
                         or 126 MHz  (480p60:  VCO=756 MHz  / 6 / 1)
```

`pll_usb` is reclaimed for the HSTX pixel clock. `clk_usb` is rerouted to derive from `pll_sys` instead. The QMI (QSPI flash) clock divider is set to 4 before raising `pll_sys` to keep flash access at ≤108 MHz.

The HSTX clock is re-programmed by `SYSTEM_CONFIG` whenever a new output profile is applied — DVI output briefly drops during this period.

---

## Output Profiles

A profile defines the logical render resolution, the physical DVI output timing, the pixel scale factor, the buffering mode, and the bits-per-pixel class. Send `SYSTEM_CONFIG` (opcode `0x10`) to select a profile.

### 8bpp Profiles

| Profile ID | Const | Render Res | Output | Scale | Buffering | FB Size | VRAM (no VM) |
|---|---|---|---|---|---|---|---|
| `0x01` | `GPU_PROFILE_320x240_DOUBLE` | 320×240 | 640×480@60 | 2× | Double | 75 KB | 314 KB |
| `0x02` | `GPU_PROFILE_640x480_SINGLE` | 640×480 | 640×480@60 | 1× | Single | 300 KB | 164 KB |
| `0x03` | `GPU_PROFILE_320x180_DOUBLE` | 320×180 | 1280×720@60 | 4× | Double | 56 KB | 352 KB |
| `0x04` | `GPU_PROFILE_640x360_SINGLE` | 640×360 | 1280×720@60 | 2× | Single | 225 KB | 239 KB |
| `0x05` | `GPU_PROFILE_640x360_DOUBLE` | 640×360 | 1280×720@60 | 2× | Double | 450 KB | 18 KB |

### 16bpp Profiles (RP2350 + `FEATURE_RGB565`)

| Profile ID | Const | Render Res | Output | Scale | Buffering | FB Size | VRAM (no VM) |
|---|---|---|---|---|---|---|---|
| `0x11` | `GPU_PROFILE_320x240_16BPP` | 320×240 | 640×480@60 | 2× | Double | 150 KB | 164 KB |
| `0x12` | `GPU_PROFILE_320x180_16BPP` | 320×180 | 1280×720@60 | 4× | Double | 225 KB | 239 KB |
| `0x13` | `GPU_PROFILE_640x360_16BPP` | 640×360 | 1280×720@60 | 2× | Single | 450 KB | 18 KB |

**VRAM is the sprite/asset cache** — all remaining SRAM after framebuffer(s) and system overhead (~80 KB). Numbers above assume no VM heap reserved.

---

## SPI Protocol

All host→GPU communication uses a fixed packet format:

```
[0xAA sync][opcode:1B][payload_len:2B LE][payload:N bytes][CRC-16:2B LE]
```

- CRC-16/CCITT (poly `0x1021`, init `0xFFFF`) covers opcode + length + payload — not the sync byte.
- Maximum payload: 4096 bytes (ring buffer size).
- The host must wait for BUSY LOW before each packet.
- Query responses (opcodes `0xE0`–`0xEA`) are returned on MISO after a BUSY-clear + 50 µs guard.

The SDK in `PicoGPU/host_sdk/` handles all of this automatically.

---

## Firmware Architecture

```
main.c
├── Core 0 — Command processing loop
│   ├── spi_slave (PIO) → ring_buffer → packets.c (parse + CRC)
│   ├── dispatch.c (state machine + opcode routing)
│   │   ├── graphics/primitives.c      — 20 drawing primitives
│   │   ├── graphics/primitives_fpu.c  — FPU: Bézier, gradient, Gouraud
│   │   ├── graphics/blit.c            — BLIT_SPRITE, DRAW_VRAM_SPRITE
│   │   ├── graphics/text.c            — Bitmap font rendering
│   │   ├── graphics/effects.c         — Pixel write pipeline (dither, blend, scissor)
│   │   ├── graphics/scissor.c         — Clip rect stack (depth 8)
│   │   ├── graphics/region.c          — Copy, scroll, replace, tilemap
│   │   ├── graphics/nine_patch.c      — 9-slice scalable panels
│   │   ├── assets/vram.c              — Sprite cache + UPLOAD_VRAM
│   │   ├── assets/vram_named.c        — Named slot table (64 entries)
│   │   ├── assets/display_list.c      — Record/playback command lists
│   │   ├── memory/profiles.c          — SYSTEM_CONFIG, arena partition
│   │   └── state/coprocessor_state.c  — Global GPU state
│   └── MISO response dispatch
│
└── Core 1 — DVI output loop (never returns)
    └── pico_hdmi HSTX DMA engine
        ├── DMA ping-pong to HSTX FIFO
        └── VBLANK callback → buffer swap, deferred flush
```

### Pixel Write Pipeline

Every pixel written by a drawing command passes through this gate order in `effects.c`:

```
1. Bounds check    — clip to framebuffer extents
2. Scissor test    — clip to active clip rect
3. Chroma key      — skip if pixel matches chroma key color (transparency)
4. Dithering       — per-channel Bayer bias (if DITHER_BAYER2/4 active)
5. Blend mode      — XOR / OR / AND / OVERWRITE
6. Framebuffer write
```

**Fast path:** filled primitives and `FILL_SCREEN` use `effect_fill_hspan()` — scissor-clipped `memset`. Dithering, blend mode, and chroma key are **not** applied to span fills.

### Memory Layout (typical, 320×240 double-buffer, 8bpp, no VM)

```
SRAM total:         520 KB
System overhead:    ~80 KB  (pico_hdmi DMA, stack, BSS)
──────────────────────────
Arena:              ~440 KB
  Framebuffer A:    75 KB   (320×240×1)
  Framebuffer B:    75 KB   (back buffer)
  VRAM:             ~290 KB (sprite cache, bump-allocated)
```

---

## Coprocessor State Machine

```
Power-on / RESET / SOFT_RESET
       │
       ▼
  UNINITIALIZED  ◄── hardware RESET (GP7) always works
       │ SYSTEM_CONFIG received
       ▼
  INITIALIZING   (BUSY=HIGH, DVI stopped, arena zeroed, PLL retuned)
       │ done
       ▼
    ACTIVE        (DVI running, all commands accepted)
       │
       └── SYSTEM_CONFIG ──► INITIALIZING (re-init, DVI briefly stops)
```

- **All query commands** (`0xE0`–`0xEA`) are accepted in any state.
- **Drawing commands** require ACTIVE state; they return `ERR_NOT_INITIALIZED` otherwise.
- **Hardware RESET** (GP7 active LOW) sets a flag polled by the main loop. Truly stuck Core 0 handlers are not recoverable without power cycling (watchdog not yet implemented).

---

## Dual-Core Design

| Core | Role |
|---|---|
| **Core 0** | Command processing: drains SPI ring buffer, parses packets, routes opcodes, executes draw commands, manages MISO responses |
| **Core 1** | DVI output: runs `pico_hdmi` HSTX DMA loop, handles VBLANK IRQ, executes deferred buffer swaps |

The two cores share the framebuffer pointer atomically. Core 0 writes to `g_fb_back`; Core 1 reads `g_fb_front` for DMA. `SWAP_BUFFERS` sets `g_state.swap_pending`; Core 1's VBLANK callback atomically swaps the pointers.

Core 0 raises the BUSY pin when the ring buffer exceeds 80% capacity. This is the primary backpressure mechanism — the host must respect it.

---

## Project Structure

```
picoGPU/
├── PicoGPU/                        ← Firmware (PlatformIO project)
│   ├── platformio.ini
│   ├── include/
│   │   ├── feature_flags.h         ← All compile-time feature switches
│   │   ├── opcodes.h               ← Opcode + sub-opcode constants
│   │   └── error_codes.h           ← Error code constants
│   ├── src/
│   │   ├── main.c                  ← Entry point, hw_init_all, Core 0 loop
│   │   ├── graphics/               ← Primitive renderers, effects, scissor
│   │   ├── assets/                 ← VRAM, named VRAM, display lists
│   │   ├── fonts/                  ← Built-in 8×8 bitmap font
│   │   ├── memory/                 ← Arena allocator, profile table
│   │   ├── protocol/               ← Packet parser, opcode dispatcher
│   │   ├── state/                  ← GPU state struct
│   │   ├── transport/              ← SPI PIO slave, ring buffer
│   │   └── hal/rp2350/             ← Display HAL (HSTX, profile apply)
│   └── host_sdk/                   ← Host-side driver (plain C)
│       ├── gpu_driver.h / .c       ← Public API + implementation
│       ├── gpu_opcodes.h           ← Opcode/error/format constants (host copy)
│       ├── gpu_profiles.h          ← Profile ID constants (host copy)
│       ├── README.md               ← Quick-start
│       └── DOCS.md                 ← Full integration reference
│
├── pico_hdmi/                      ← DVI/HDMI output library (fork)
├── RP2040-RP2350-GPU-Coprocessor-Specification.md
└── Technical-Implementation-Plan.md
```

---

## Host SDK

See [`PicoGPU/host_sdk/`](PicoGPU/host_sdk/) for the plain-C host driver.

- **[README.md](PicoGPU/host_sdk/README.md)** — quick-start snippet
- **[DOCS.md](PicoGPU/host_sdk/DOCS.md)** — complete integration reference: every API, every wire format, every behavior and caveat

The SDK is MCU-agnostic. Implement six HAL functions (`gpu_hal_spi_write`, `gpu_hal_spi_read`, `gpu_hal_cs_assert`, `gpu_hal_cs_deassert`, `gpu_hal_busy`, `gpu_hal_delay_us`) for your platform and you're done.

---

## Known Hardware Caveats

**D/C pin** — Wired but not yet used for hardware packet resync. The parser uses the `0xAA` sync scan + CRC-16 for corruption recovery. D/C-gated hard resync is planned.

**SPI CS not PIO-gated** — The PIO SPI receiver samples on every SCK edge regardless of CS. Dummy MISO-read clocks can inject bytes. Mitigated by the sync + CRC mechanism; always deassert CS only at byte boundaries.

**Hardware RESET limitation** — RESET (GP7) sets a flag polled by the Core 0 loop. A truly hung command handler (e.g. a pathological flood fill) will block RESET until it returns. Watchdog timer is not yet implemented.

**Overclock** — `CPU_SPEED_FAST=1` runs at 432 MHz / 1.40 V, which requires `vreg_disable_voltage_limit()`. This exceeds the documented RP2350 envelope. Stability is silicon-specific. Use `CPU_SPEED_FAST=0` for the spec-compliant 384 MHz / 1.30 V configuration.

---

## Roadmap

| Phase | Status | Features |
|---|---|---|
| 0 | ✅ Complete | Ring buffer, SPI slave PIO, packet parser, CRC, state machine |
| 1 | ✅ Complete | All 20 drawing primitives, FILL_SCREEN, BLIT_SPRITE, UPLOAD_VRAM, DRAW_VRAM_SPRITE, text, VRAM, profiles |
| 2 | ✅ Complete | Scissor stack, pixel formats, dither, blend, BEGIN/END_FRAME, frame stats, event buffer |
| 3 | ✅ Complete | FPU primitives, named VRAM, display lists, region ops (copy/scroll/tilemap/9-patch/capture) |
| 4 | 🔲 Planned | Pawn VM scripting (LOAD_PROCEDURE, EXEC_PROCEDURE, SCHEDULE_PROCEDURE) |

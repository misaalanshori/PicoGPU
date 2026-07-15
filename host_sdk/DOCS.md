# PicoGPU Host SDK — Complete Integration Reference

**Firmware version:** 1.x (RP2350 primary, RP2040 planned)
**Document version:** 1.0

---

## Table of Contents

1. [Overview](#1-overview)
2. [Hardware Connections](#2-hardware-connections)
3. [SPI Bus Protocol](#3-spi-bus-protocol)
4. [Platform HAL — What You Must Implement](#4-platform-hal--what-you-must-implement)
5. [Coprocessor State Machine](#5-coprocessor-state-machine)
6. [Profiles and Memory Layout](#6-profiles-and-memory-layout)
7. [Initialization Flow](#7-initialization-flow)
8. [Flow Control — BUSY Pin](#8-flow-control--busy-pin)
9. [System Control Commands](#9-system-control-commands)
10. [Drawing State — Global Modifiers](#10-drawing-state--global-modifiers)
11. [Graphics Primitives](#11-graphics-primitives)
12. [Sprite and Blit Operations](#12-sprite-and-blit-operations)
13. [Text Rendering](#13-text-rendering)
14. [Framebuffer and Buffer Management](#14-framebuffer-and-buffer-management)
15. [VRAM — Sprite Cache Management](#15-vram--sprite-cache-management)
16. [Display Lists](#16-display-lists)
17. [Region Operations](#17-region-operations)
18. [Query Commands — Reading GPU State](#18-query-commands--reading-gpu-state)
19. [Event System](#19-event-system)
20. [Pixel Formats and Color Encoding](#20-pixel-formats-and-color-encoding)
21. [Dithering](#21-dithering)
22. [Chroma Key Transparency](#22-chroma-key-transparency)
23. [Scissor Stack — Clip Rectangles](#23-scissor-stack--clip-rectangles)
24. [RLE Encoding for Pixel Data](#24-rle-encoding-for-pixel-data)
25. [Pixel Write Pipeline — Gate Order](#25-pixel-write-pipeline--gate-order)
26. [VM Commands — Phase 4 Stub Reference](#26-vm-commands--phase-4-stub-reference)
27. [Capabilities Discovery](#27-capabilities-discovery)
28. [Error Codes — Complete Reference](#28-error-codes--complete-reference)
29. [Integration Patterns and Recipes](#29-integration-patterns-and-recipes)
30. [Known Limitations and Caveats](#30-known-limitations-and-caveats)
31. [Wire-Format Quick Reference](#31-wire-format-quick-reference)

---

## 1. Overview

PicoGPU is a dedicated graphics coprocessor built on Raspberry Pi RP2350 (or RP2040), connected to your host MCU via a custom SPI protocol. The coprocessor owns the framebuffer, executes drawing commands, manages its own memory, and outputs DVI/HDMI video independently of the host.

**Key facts:**
- The host sends commands; the coprocessor draws. The host never touches pixels directly.
- All drawing commands are **fire-and-forget** — the host does not wait for a draw to complete, only for the ring buffer to have space (BUSY pin).
- Query commands (`GET_*`) require a short wait and a MISO read. All other commands do not read MISO.
- The GPU has three states: UNINITIALIZED, INITIALIZING, ACTIVE. Drawing is only legal in ACTIVE.
- VRAM, framebuffer, and all allocations are reset on every profile switch and every RESET. The GPU never preserves drawing state across `SYSTEM_CONFIG`.

---

## 2. Hardware Connections

### 2.1 Required Pins (16 total)

| Signal | Direction | GPU GPIO | Purpose |
|---|---|---|---|
| MOSI | Host → GPU | GP0 | SPI data in |
| CS | Host → GPU | GP1 | Transaction framing — assert LOW during packet |
| SCK | Host → GPU | GP2 | SPI clock (24–30+ MHz) |
| MISO | GPU → Host | GP3 | Query responses only |
| D/C | Host → GPU | GP4 | LOW = opcode byte; HIGH = payload bytes |
| BUSY | GPU → Host | GP5 | HIGH when ring buffer ≥80% full or during INITIALIZING |
| TE | GPU → Host | GP6 | Pulses HIGH during VBLANK (~0.7–1.4 ms) |
| RESET | Host → GPU | GP7 | Active LOW; returns GPU to UNINITIALIZED immediately |
| DVI CK− | GPU → Monitor | GP12 | Clock differential pair |
| DVI CK+ | GPU → Monitor | GP13 | |
| DVI D0− | GPU → Monitor | GP14 | Blue lane |
| DVI D0+ | GPU → Monitor | GP15 | |
| DVI D1− | GPU → Monitor | GP16 | Green lane |
| DVI D1+ | GPU → Monitor | GP17 | |
| DVI D2− | GPU → Monitor | GP18 | Red lane |
| DVI D2+ | GPU → Monitor | GP19 | |

**Optional 17th pin:**

| Signal | Direction | Purpose |
|---|---|---|
| INT | GPU → Host | Active LOW when event buffer is non-empty. Only present when `FEATURE_INT_PIN` is compiled. |

### 2.2 D/C Pin Protocol

D/C is a hardware packet-framing signal that the GPU reads alongside MOSI:
- Hold D/C **LOW** for the sync byte (`0xAA`) and opcode byte.
- Hold D/C **HIGH** for the payload length bytes, payload bytes, and CRC bytes.

> **Note (Current Limitation):** The current firmware reads D/C as a GPIO input but does not yet use it for hardware parser resync. The parser relies on the `0xAA` sync scan + CRC rejection as its primary resync mechanism. D/C-based hard resync is planned. Host drivers should still assert D/C correctly for future compatibility.

### 2.3 SPI Electrical

- Mode 0 (CPOL=0, CPHA=0) — data sampled on rising SCK edge.
- MSB first.
- CS must be HIGH between packets.
- DVI outputs require 220Ω series resistors on all 8 signal lines.

### 2.4 RESET Pin

- Active LOW; hold for at least 1 µs.
- Immediately returns GPU to UNINITIALIZED regardless of current state.
- Works even if the SPI parser is stuck or the main loop is hung.

---

## 3. SPI Bus Protocol

### 3.1 Packet Structure

Every command sent from host to GPU has the following frame:

```
┌──────────┬─────────┬────────────────┬──────────────┬──────────┐
│ Sync     │ Opcode  │ Payload Length │  Payload     │  CRC-16  │
│ 0xAA (1B)│ (1B)    │ (2B, LE)       │  (N bytes)   │ (2B, LE) │
└──────────┴─────────┴────────────────┴──────────────┴──────────┘
```

- **Sync byte `0xAA`** (binary `10101010`) — alternating bit pattern, rarely collides in payload data.
- **Payload length** — 16-bit little-endian byte count of the payload only (excludes sync, opcode, length, and CRC).
- **CRC-16/CCITT** — polynomial `0x1021`, init `0xFFFF`. Covers opcode + length bytes + payload. Does **not** cover the sync byte.
- Commands with no payload have length `0x0000` and a 2-byte CRC over just `[opcode, 0x00, 0x00]`.

### 3.2 Packet Transmission Rules

1. Poll BUSY LOW before asserting CS.
2. Assert CS LOW.
3. Send sync byte, opcode, length, payload, CRC in a single continuous SPI transaction.
4. Deassert CS HIGH.

The SDK's `gpu_send_command()` handles steps 1–4 automatically.

### 3.3 Packet Size Limit

The GPU's ring buffer is 4096 bytes. The parser rejects any packet whose payload exceeds 4096 bytes (`ERR_PAYLOAD_TOO_LARGE`). Commands with larger payloads (e.g. `UPLOAD_VRAM`, `BLIT_SPRITE`) are automatically split into multiple packets by the SDK. Do not exceed this limit in raw packet construction.

### 3.4 Query Response Protocol

Commands `0xE0`–`0xEA` return data on MISO. The exchange:

1. Send the query packet (steps 1–4 above).
2. Release CS.
3. Poll BUSY LOW.
4. Wait ≥50 µs (guard time for Core 0 to prepare the response).
5. Assert CS, clock dummy bytes on MOSI, read response bytes on MISO.
6. Release CS.

Response sizes are fixed per opcode and known at compile time. There is no length prefix in the response. The SDK's `gpu_query()` handles this sequence.

> **Caution:** The 50 µs guard is a conservative placeholder. Under very heavy ring buffer load, Core 0 scheduling latency may be longer. If you observe stale responses, increase the guard time.

---

## 4. Platform HAL — What You Must Implement

Six platform functions must be provided by your application. The SDK calls these; it contains no MCU-specific code.

```c
// Send 'len' bytes over SPI (MOSI only). CS is already asserted when called.
void gpu_hal_spi_write(const uint8_t *data, uint32_t len);

// Read 'len' bytes from SPI (MISO), clocking dummy 0x00 on MOSI. CS is asserted.
void gpu_hal_spi_read(uint8_t *buf, uint32_t len);

// Assert CS LOW.
void gpu_hal_cs_assert(void);

// Deassert CS HIGH.
void gpu_hal_cs_deassert(void);

// Return true if the BUSY pin is HIGH (GPU busy), false if LOW (GPU ready).
bool gpu_hal_busy(void);

// Block for at least 'us' microseconds.
void gpu_hal_delay_us(uint32_t us);
```

**Example — RP2040 host (Pico SDK):**

```c
#include "hardware/spi.h"
#include "hardware/gpio.h"

#define HOST_CS_PIN   9
#define HOST_BUSY_PIN 10

void gpu_hal_spi_write(const uint8_t *data, uint32_t len) {
    spi_write_blocking(spi0, data, len);
}
void gpu_hal_spi_read(uint8_t *buf, uint32_t len) {
    memset(buf, 0, len);
    spi_read_blocking(spi0, 0x00, buf, len);
}
void gpu_hal_cs_assert(void)   { gpio_put(HOST_CS_PIN, 0); }
void gpu_hal_cs_deassert(void) { gpio_put(HOST_CS_PIN, 1); }
bool gpu_hal_busy(void)        { return gpio_get(HOST_BUSY_PIN); }
void gpu_hal_delay_us(uint32_t us) { sleep_us(us); }
```

---

## 5. Coprocessor State Machine

The GPU operates in exactly one of three states at all times.

### 5.1 States

```
    Power-on / RESET / SOFT_RESET
           │
           ▼
    ┌─────────────────────┐
    │   UNINITIALIZED     │ ◄─── RESET (hardware pin, always)
    │                     │ ◄─── SOFT_RESET (opcode 0x11, if SPI is healthy)
    └──────────┬──────────┘
               │ SYSTEM_CONFIG received
               ▼
    ┌─────────────────────┐
    │    INITIALIZING     │  (BUSY = HIGH throughout)
    │ Arena reset         │  (DVI output stopped)
    │ PLL retuned         │
    │ Profile applied     │
    └──────────┬──────────┘
               │ Done
               ▼
    ┌─────────────────────┐
    │      ACTIVE         │  (DVI running)
    │ Drawing permitted   │◄─┐
    └──────────┬──────────┘  │
               │             │ SYSTEM_CONFIG (re-init, DVI briefly stops)
               └─────────────┘
```

### 5.2 UNINITIALIZED

- No profile active. `GET_PROFILE` returns `0xFF`.
- No framebuffer, no VRAM, no VM heap.
- DVI output is **silent** (no TMDS signal — monitor shows no signal or goes to sleep).
- **All query commands are accepted and respond normally.**
- Drawing commands (`DRAW_PRIMITIVE`, `BLIT_SPRITE`, etc.) return `ERR_NOT_INITIALIZED`.

### 5.3 INITIALIZING

- Entered when `SYSTEM_CONFIG` is received in either UNINITIALIZED or ACTIVE state.
- **BUSY pin is HIGH for the entire duration** — the host must wait before sending anything.
- During this phase: DMA halts, DVI output stops (monitor will lose sync), the arena is fully zeroed and re-partitioned, the PLL is retuned for the new output timing, and the HSTX pixel format registers are written.
- Transitions to ACTIVE automatically when complete.

### 5.4 ACTIVE

- DVI is running. Drawing commands are accepted.
- A new `SYSTEM_CONFIG` triggers a fresh INITIALIZING cycle — the GPU re-enters safe mode, resets all state, and comes back up with the new profile. The monitor loses sync briefly.

### 5.5 What RESET Clears

Both hardware RESET (GP7 active LOW) and `SOFT_RESET` (opcode `0x11`) return the GPU to UNINITIALIZED and clear:

- All arena allocations (framebuffer, VRAM, VM heap)
- Named VRAM slot table (all 64 entries)
- Active profile → `0xFF`
- Chroma key enable state → disabled (key color `0xF81F` retained)
- Dither mode → NONE
- Blend mode → OVERWRITE
- Scissor stack → empty (full screen)
- Pixel format → bpp_class default (8bpp→RGB332, 16bpp→RGB565, 4bpp→RGB121, 24bpp→RGB888)
- Protocol parser state
- Display list recording session (if any)
- Scheduled VM procedures (if any)

**QSPI flash is never touched by RESET.**

---

## 6. Profiles and Memory Layout

### 6.1 What a Profile Is

A profile defines:
- **Logical resolution** — the pixel grid the host draws into (framebuffer dimensions).
- **Physical output timing** — the DVI signal (always 640×480@60 or 1280×720@60).
- **Pixel scale** — integer duplication factor (e.g. 2×2 means each logical pixel becomes a 2×2 block on screen).
- **Buffering mode** — single, double, or single-deferred.
- **bpp class** — bytes per pixel (4bpp, 8bpp, 16bpp, or 24bpp).

The bpp class determines framebuffer byte size. The actual pixel *encoding* (RGB332, MONO8, RGB565, etc.) is selected separately with `SET_PIXEL_FORMAT`.

### 6.2 Profile ID Table

#### 8bpp Profiles

| Profile ID | Constant | Logical Res | Output | Scale | Buffering | FB Size | VRAM (VM off) |
|---|---|---|---|---|---|---|---|
| `0x01` | `GPU_PROFILE_320x240_DOUBLE` | 320×240 | 640×480@60 | 2×2 | Double | 75 KB | 314 KB |
| `0x02` | `GPU_PROFILE_640x480_SINGLE` | 640×480 | 640×480@60 | 1×1 | Single | 300 KB | 164 KB |
| `0x03` | `GPU_PROFILE_320x180_DOUBLE` | 320×180 | 1280×720@60 | 4×4 | Double | 112.5 KB | 351.5 KB |
| `0x04` | `GPU_PROFILE_640x360_SINGLE` | 640×360 | 1280×720@60 | 2×2 | Single | 225 KB | 239 KB |
| `0x05` | `GPU_PROFILE_640x360_DOUBLE` | 640×360 | 1280×720@60 | 2×2 | Double | 450 KB | 18 KB |

#### 16bpp Profiles (RP2350 only, requires `FEATURE_RGB565`)

| Profile ID | Constant | Logical Res | Output | Scale | Buffering | FB Size | VRAM (VM off) |
|---|---|---|---|---|---|---|---|
| `0x11` | `GPU_PROFILE_320x240_16BPP` | 320×240 | 640×480@60 | 2×2 | Double | 150 KB | 164 KB |
| `0x12` | `GPU_PROFILE_320x180_16BPP` | 320×180 | 1280×720@60 | 4×4 | Double | 225 KB | 239 KB |
| `0x13` | `GPU_PROFILE_640x360_16BPP` | 640×360 | 1280×720@60 | 2×2 | Single | 450 KB | 18 KB |

> `0xFF` — returned by `GET_PROFILE` when UNINITIALIZED. Not a valid `SYSTEM_CONFIG` argument.

Sending an unknown profile ID returns `ERR_UNSUPPORTED_PROFILE` and the GPU stays in its current state.

### 6.3 Buffering Modes

**Double buffer (`_DOUBLE` profiles):**
- Two framebuffers — one displayed (front), one drawn into (back).
- `SWAP_BUFFERS` atomically flips them at the next VBLANK. Zero tearing.
- `SWAP_BUFFERS_IMMEDIATE` flips immediately (may tear). Useful when timing via TE pin.
- The back buffer persists between frames — only changed pixels need to be redrawn.

**Single buffer (`_SINGLE` profiles):**
- One framebuffer — the GPU renders and displays from the same memory.
- Tearing is possible. Does not corrupt the DVI signal.
- Use the TE pin hardware interrupt to restrict drawing to the VBLANK window (~0.7–1.4 ms) for tear-free output.
- Half the SRAM cost of double-buffer — significantly more VRAM available.

**Single-deferred buffer:**
- One framebuffer plus an 8 KB deferred command queue.
- Small fixed-size draw commands are enqueued during active scanout and flushed at VBLANK.
- **Streaming commands** (`BLIT_SPRITE`, `UPLOAD_VRAM`, `LOAD_PROCEDURE`) **always bypass the deferred queue** and execute immediately. These can still cause tearing for large pixel blits.
- Management commands (queries, VRAM ops) also bypass and execute immediately.

### 6.4 VRAM — Sprite Cache

The sprite cache (VRAM) is a flat byte array in SRAM, sized by the profile and whether the VM heap is reserved. It persists for the lifetime of a profile session. It is zeroed on every `SYSTEM_CONFIG` or RESET.

| Profile | VRAM available (VM reserved) | VRAM available (VM not reserved) |
|---|---|---|
| `0x01` 320×240 Double 8bpp | 250 KB | 314 KB |
| `0x02` 640×480 Single 8bpp | 100 KB | 164 KB |
| `0x03` 320×180 Double 8bpp | 223.5 KB | 351.5 KB |
| `0x04` 640×360 Single 8bpp | 175 KB | 239 KB |
| `0x05` 640×360 Double 8bpp | 0 (VM not supported) | 18 KB |
| `0x11` 320×240 Double 16bpp | 100 KB | 164 KB |
| `0x12` 320×180 Double 16bpp | 175 KB | 239 KB |
| `0x13` 640×360 Single 16bpp | 0 (VM not supported) | 18 KB |

Sprite capacity examples:

| VRAM | 16×16 @ 8bpp (256 B) | 32×32 @ 8bpp (1 KB) | 64×64 @ 8bpp (4 KB) | 32×32 @ 16bpp (2 KB) |
|---|---|---|---|---|
| 18 KB | 72 | 18 | 4 | 9 |
| 100 KB | 400 | 100 | 25 | 50 |
| 175 KB | 700 | 175 | 43 | 87 |
| 250 KB | 1000 | 250 | 62 | 125 |

---

## 7. Initialization Flow

This is the canonical startup sequence. Follow it exactly.

```c
// Step 1 — Hardware reset (pulse RESET LOW for ≥1 µs)
gpio_put(RESET_PIN, 0);
gpu_hal_delay_us(10);
gpio_put(RESET_PIN, 1);

// Step 2 — Wait for BUSY to go LOW (GPU is in UNINITIALIZED, ready)
gpu_wait_not_busy(1000);   // 1-second timeout

// Step 3 — Optional: query capabilities to auto-detect firmware variant
uint32_t caps = gpu_get_capabilities();
bool has_rgb565 = (caps & GPU_CAP_RGB565) != 0;

// Step 4 — Configure the profile
// Choose single or double-buffer based on your rendering needs.
// Set reserve_vm = GPU_RESERVE_VM_YES if you will use the Pawn VM.
gpu_system_config(GPU_PROFILE_320x240_DOUBLE, GPU_RESERVE_VM_NO);
// BUSY goes HIGH immediately. Wait for INITIALIZING to complete:
gpu_wait_not_busy(5000);   // allow up to 5 seconds for PLL settling

// Step 5 — Optional: set global state
gpu_set_pixel_format(GPU_PIXEL_FORMAT_RGB332);  // default for 8bpp; may omit
gpu_enable_frame_stats(true);

// Step 6 — Upload persistent assets to VRAM
uint32_t sprite_offset = gpu_get_vram_used();   // current bump pointer
gpu_upload_vram(sprite_offset, GPU_BLIT_RAW, my_sprite_data, my_sprite_size);

// Step 7 — Begin rendering
gpu_begin_frame();
gpu_fill_screen(0x0000);   // black
gpu_draw_vram_sprite(10, 10, 32, 32, sprite_offset, GPU_TRANSFORM_ROT_0, 0);
gpu_render_text(10, 50, 0, 0xFFFF, 1, "Hello World");
gpu_end_frame();           // triggers SWAP_BUFFERS in double-buffer mode
```

### 7.1 Re-Initialization (Profile Switch)

At any point in ACTIVE state, send a new `SYSTEM_CONFIG` to switch profiles:

```c
// The GPU re-enters INITIALIZING — BUSY goes HIGH
gpu_system_config(GPU_PROFILE_640x360_SINGLE, GPU_RESERVE_VM_NO);
gpu_wait_not_busy(5000);
// ALL VRAM, framebuffer, and VM state is now zeroed.
// Re-upload all assets. Re-apply all global state settings.
gpu_set_pixel_format(GPU_PIXEL_FORMAT_RGB332);
// ... re-upload assets ...
```

> **Important:** Never assume any GPU state persists across a `SYSTEM_CONFIG`. Every VRAM byte is zeroed, every named slot is cleared, every display list slot is invalidated. Rebuild from scratch.

---

## 8. Flow Control — BUSY Pin

The GPU exposes a BUSY pin to prevent ring buffer overflow.

- **BUSY HIGH:** The ring buffer is ≥80% full (≥3,277 of 4,096 bytes occupied), OR the GPU is in INITIALIZING.
- **BUSY LOW:** Safe to send the next packet.

The SDK's `gpu_send_command()` calls `gpu_wait_not_busy(0)` (infinite wait) before asserting CS. You do not normally need to poll BUSY manually.

### 8.1 When to Poll BUSY Manually

- Before reading a query response (required — the SDK's `gpu_query()` does this).
- After `gpu_system_config()` — `gpu_system_config()` calls `gpu_wait_not_busy(5000)` automatically.
- If you use `gpu_send_command()` directly and need a timeout guarantee:

```c
bool ready = gpu_wait_not_busy(100);  // 100 ms timeout
if (!ready) {
    // GPU didn't clear BUSY within 100 ms — handle error
}
```

### 8.2 BUSY During INITIALIZING

When the host sends `SYSTEM_CONFIG`, BUSY goes HIGH for the full duration of the INITIALIZING sequence (PLL retuning + arena reset + DVI restart). This can take tens of milliseconds. The SDK's `gpu_system_config()` waits up to 5 seconds.

---

## 9. System Control Commands

### 9.1 `gpu_system_config(profile_id, reserve_vm)`

```c
void gpu_system_config(uint8_t profile_id, uint8_t reserve_vm);
```

Initializes the GPU with the specified profile. Triggers INITIALIZING. Blocks until BUSY clears (up to 5 seconds timeout).

- `profile_id`: one of the `GPU_PROFILE_*` constants.
- `reserve_vm`: `GPU_RESERVE_VM_YES` or `GPU_RESERVE_VM_NO`. If `YES` and the profile has a VM heap, it is allocated. If `YES` and the profile has no VM heap (e.g. 0x05), returns `ERR_VM_UNAVAILABLE`.

**Wire payload:** `[profile_id(1B)][reserve_vm(1B)]` — 2 bytes.

**After this call:** all GPU state is reset. All VRAM is zeroed. All previous named slots and display lists are gone.

### 9.2 `gpu_soft_reset()`

```c
void gpu_soft_reset(void);
```

Returns the GPU to UNINITIALIZED. Clears all state (see §5.5). Equivalent to a hardware RESET when the SPI link is healthy. Does **not** block — after calling, poll BUSY or wait a fixed delay before sending `SYSTEM_CONFIG`.

### 9.3 `gpu_begin_frame()` / `gpu_end_frame()`

```c
void gpu_begin_frame(void);
void gpu_end_frame(void);
```

Marks the logical boundaries of one rendered frame.

- `BEGIN_FRAME`: resets the per-frame ring-buffer peak counter and frame timing timer. In double-buffer mode, this is the best time to drain events (`gpu_get_events()`).
- `END_FRAME` in double-buffer: equivalent to `SWAP_BUFFERS` — requests a VBLANK buffer flip.
- `END_FRAME` in single-deferred: triggers the deferred draw queue flush.
- `END_FRAME` in single-buffer: updates frame stats; no rendering action.

Using `BEGIN_FRAME`/`END_FRAME` is required to get meaningful `GET_FRAME_STATS` data.

### 9.4 `gpu_enable_frame_stats(enable)`

```c
void gpu_enable_frame_stats(bool enable);
```

Enables or disables per-frame stat accumulation. Default: **disabled**. When disabled, `GET_FRAME_STATS` returns all zeros. Enable at the start of your session if you need profiling data.

> **Important:** Re-enabling frame stats (calling with `enable=true` when stats were previously disabled) **resets all counters** — `frame_count`, `last_render_ms`, `ring_peak_pct`, and `missed_frames` are zeroed. This is intentional so accumulated stale data doesn't pollute a fresh measurement session.

---

## 10. Drawing State — Global Modifiers

These settings are global and apply to **all subsequent pixel writes** until changed.

### 10.1 Pixel Format — `gpu_set_pixel_format(format)`

```c
void gpu_set_pixel_format(uint8_t format);
```

Switches the active pixel encoding within the current profile's bpp class. Does not resize the framebuffer — only updates the HSTX color expansion registers.

| Constant | Value | bpp class | Description |
|---|---|---|---|
| `GPU_PIXEL_FORMAT_RGB332` | `0x00` | 8bpp | 3R 3G 2B packed in 1 byte |
| `GPU_PIXEL_FORMAT_MONO8` | `0x01` | 8bpp | 8-bit greyscale |
| `GPU_PIXEL_FORMAT_INDEX8` | `0x02` | 8bpp | Palette index (palette lookup Phase 4) |
| `GPU_PIXEL_FORMAT_RGB565` | `0x10` | 16bpp | 5R 6G 5B packed in 2 bytes (LE) |
| `GPU_PIXEL_FORMAT_RGB121` | `0x20` | 4bpp | 1R 2G 1B packed nibble |
| `GPU_PIXEL_FORMAT_MONO4` | `0x21` | 4bpp | 4-bit greyscale nibble |
| `GPU_PIXEL_FORMAT_INDEX4` | `0x22` | 4bpp | 4-bit palette index |
| `GPU_PIXEL_FORMAT_RGB888` | `0x30` | 24bpp | 8R 8G 8B, 3 bytes per pixel |

The format's upper nibble must match the profile's bpp class:
- `0x0X` → 8bpp profile required
- `0x1X` → 16bpp profile required
- `0x2X` → 4bpp profile required
- `0x3X` → 24bpp profile required

Mismatch returns `ERR_INVALID_PARAM`. Takes effect immediately on the next frame.

**Default after SYSTEM_CONFIG:** bpp class default (8bpp→RGB332, 16bpp→RGB565, 4bpp→RGB121, 24bpp→RGB888).

### 10.2 Dither Mode — `gpu_set_dither_mode(mode)`

```c
void gpu_set_dither_mode(uint8_t mode);
```

| Constant | Value | Description |
|---|---|---|
| `0x00` | DITHER_NONE | No dithering (default) |
| `0x01` | DITHER_BAYER2 | 2×2 Bayer ordered dither |
| `0x02` | DITHER_BAYER4 | 4×4 Bayer ordered dither |

Applied transparently to all pixel writes. Dramatically improves perceived color depth at 8bpp and sub-byte formats. Zero extra SRAM. See §21.

### 10.3 Blend Mode — `gpu_set_blend_mode(mode)`

```c
void gpu_set_blend_mode(uint8_t mode);
```

| Constant | Value | Operation |
|---|---|---|
| `0x00` | BLEND_OVERWRITE | Destination = Source (default) |
| `0x01` | BLEND_XOR | Destination = Destination XOR Source |
| `0x02` | BLEND_OR | Destination = Destination OR Source |
| `0x03` | BLEND_AND | Destination = Destination AND Source |

Applied to all pixel writes. Default: OVERWRITE. Chroma key check happens before blend; transparent pixels are not written regardless of blend mode.

---

## 11. Graphics Primitives

All primitives are sent via `DRAW_PRIMITIVE` (opcode `0x30`). The first payload byte selects the sub-opcode. All primitives:
- Pass color explicitly — no global draw color.
- Respect the active scissor stack.
- Respect the active blend mode and dither mode.
- Clip against the current clip rect automatically.

Colors are always passed as `uint16_t` in little-endian. For 8bpp profiles, only the low 8 bits are used (RGB332 encoding). For 16bpp, all 16 bits (RGB565). The firmware does no color-space conversion — pack your color in the format active on the GPU.

### 11.1 Pixel

```c
void gpu_set_pixel(int16_t x, int16_t y, uint16_t color);
```

Writes a single pixel. Off-screen coordinates are silently clipped.

**Payload:** `[0x01][x(2)][y(2)][color(2)]` — 7 bytes total.

### 11.2 Line

```c
void gpu_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
              uint8_t thickness, uint16_t color);
```

Draws a straight line. `thickness=1` gives a 1-pixel hairline. Larger values draw thicker lines (behavior implementation-defined beyond 1; currently uses repeated offset rendering).

**Payload:** `[0x02][x0(2)][y0(2)][x1(2)][y1(2)][thickness(1)][color(2)]` — 12 bytes.

### 11.3 Dashed Line

```c
void gpu_line_dashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                     uint8_t dash_on, uint8_t dash_off, uint16_t color);
```

Line with alternating solid/gap segments. `dash_on` = pixel count of the solid segment; `dash_off` = pixel count of the gap.

**Payload:** `[0x03][x0(2)][y0(2)][x1(2)][y1(2)][dash_on(1)][dash_off(1)][color(2)]` — 13 bytes.

### 11.4 Rectangle (Outline)

```c
void gpu_rect(int16_t x, int16_t y, int16_t w, int16_t h,
              uint8_t border_width, uint16_t color);
```

Outline-only rectangle. `border_width` in pixels. Use `border_width=1` for a hairline border.

**Payload:** `[0x04][x(2)][y(2)][w(2)][h(2)][border_width(1)][color(2)]` — 12 bytes.

### 11.5 Filled Rectangle

```c
void gpu_rect_filled(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
```

Solid filled rectangle. The most common and fastest primitive.

**Payload:** `[0x05][x(2)][y(2)][w(2)][h(2)][color(2)]` — 11 bytes.

### 11.6 Rounded Rectangle (Outline)

```c
void gpu_rect_rounded(int16_t x, int16_t y, int16_t w, int16_t h,
                      uint8_t radius, uint8_t border_width, uint16_t color);
```

`radius` = corner arc radius in pixels.

**Payload:** `[0x06][x(2)][y(2)][w(2)][h(2)][radius(1)][border_width(1)][color(2)]` — 13 bytes.

### 11.7 Rounded Rectangle (Filled)

```c
void gpu_rect_rounded_filled(int16_t x, int16_t y, int16_t w, int16_t h,
                             uint8_t radius, uint16_t color);
```

**Payload:** `[0x07][x(2)][y(2)][w(2)][h(2)][radius(1)][color(2)]` — 12 bytes.

### 11.8 Circle (Outline)

```c
void gpu_circle(int16_t cx, int16_t cy, int16_t r, uint8_t border_width, uint16_t color);
```

`cx,cy` = center. `r` = radius.

**Payload:** `[0x08][cx(2)][cy(2)][r(2)][border_width(1)][color(2)]` — 10 bytes.

### 11.9 Circle (Filled)

```c
void gpu_circle_filled(int16_t cx, int16_t cy, int16_t r, uint16_t color);
```

**Payload:** `[0x09][cx(2)][cy(2)][r(2)][color(2)]` — 9 bytes.

### 11.10 Ellipse (Outline)

```c
void gpu_ellipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                 uint8_t border_width, uint16_t color);
```

`rx` = horizontal radius; `ry` = vertical radius.

**Payload:** `[0x0A][cx(2)][cy(2)][rx(2)][ry(2)][border_width(1)][color(2)]` — 12 bytes.

### 11.11 Ellipse (Filled)

```c
void gpu_ellipse_filled(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t color);
```

**Payload:** `[0x0B][cx(2)][cy(2)][rx(2)][ry(2)][color(2)]` — 11 bytes.

### 11.12 Arc

```c
void gpu_arc(int16_t cx, int16_t cy, int16_t r,
             int16_t start_deg, int16_t end_deg,
             uint8_t border_width, uint16_t color);
```

Partial arc. `start_deg` and `end_deg` in integer degrees (0 = right / 3 o'clock, increasing counter-clockwise).

**Payload:** `[0x0C][cx(2)][cy(2)][r(2)][start(2)][end(2)][bw(1)][color(2)]` — 14 bytes.

### 11.13 Triangle (Outline)

```c
void gpu_triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color);
```

**Payload:** `[0x0D][x0(2)][y0(2)][x1(2)][y1(2)][x2(2)][y2(2)][color(2)]` — 15 bytes.

### 11.14 Triangle (Filled)

```c
void gpu_triangle_filled(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                         int16_t x2, int16_t y2, uint16_t color);
```

Scanline fill. Works for any triangle orientation.

**Payload:** `[0x0E][x0(2)][y0(2)][x1(2)][y1(2)][x2(2)][y2(2)][color(2)]` — 15 bytes.

### 11.15 Polygon (Filled)

```c
void gpu_polygon_filled(uint8_t n, const int16_t *xs, const int16_t *ys, uint16_t color);
```

Scanline fill for an arbitrary polygon. `n` = vertex count (minimum 3, maximum 64). Works for both convex and concave polygons (non-self-intersecting). Vertices are passed as separate arrays for easy construction.

**Payload:** `[0x10][n(1)][color(2)][x0(2)][y0(2)][x1(2)][y1(2)]...` — `4 + n×4` bytes.

### 11.16 Flood Fill

```c
void gpu_flood_fill(int16_t x, int16_t y, uint16_t fill_color);
```

Scanline flood fill from seed point `(x,y)`. Fills all pixels that match the seed pixel's color with `fill_color`. The clip rect limits the fill region. Large fills can be slow and will hold up subsequent commands for their duration.

**Payload:** `[0x14][x(2)][y(2)][fill_color(2)]` — 7 bytes.

### 11.17 FPU Primitives — RP2350 Only

The following require `FEATURE_FPU_PRIMITIVES` in the firmware and the RP2350 hardware FPU. On unsupported builds (RP2040 or `FEATURE_FPU_PRIMITIVES=0`) they return `ERR_FEATURE_UNAVAILABLE`. Always check `GET_CAPABILITIES` bit 2 (`GPU_CAP_FPU_PRIMITIVES`) before use. See §27 for the capability-safe usage pattern.

#### Quadratic Bézier Curve

```c
void gpu_bezier_quad(int16_t x0, int16_t y0,   // start point
                     int16_t cx, int16_t cy,     // control point
                     int16_t x1, int16_t y1,     // end point
                     uint8_t steps,              // subdivision count (8–64 typical)
                     uint16_t color);
```

Higher `steps` = smoother curve, more GPU computation. For a 320×240 display, 16–32 steps gives smooth results for typical UI curves.

**Payload:** `[0x11][x0(2)][y0(2)][cx(2)][cy(2)][x1(2)][y1(2)][steps(1)][color(2)]` — 16 bytes.

#### Cubic Bézier Curve

```c
void gpu_bezier_cubic(int16_t x0, int16_t y0,    // start
                      int16_t cx0, int16_t cy0,   // control 0
                      int16_t cx1, int16_t cy1,   // control 1
                      int16_t x1, int16_t y1,     // end
                      uint8_t steps, uint16_t color);
```

**Payload:** `[0x12][x0(2)][y0(2)][cx0(2)][cy0(2)][cx1(2)][cy1(2)][x1(2)][y1(2)][steps(1)][color(2)]` — 20 bytes.

#### Gradient Rectangle

```c
void gpu_gradient_rect(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint16_t c0, uint16_t c1,
                       uint8_t direction);   // GPU_GRADIENT_HORIZONTAL (0) or GPU_GRADIENT_VERTICAL (1)
```

Linear gradient fill. `c0` = start color, `c1` = end color. Colors are interpolated per-pixel using FPU. In RGB565, each channel is interpolated independently.

**Payload:** `[0x13][x(2)][y(2)][w(2)][h(2)][c0(2)][c1(2)][direction(1)]` — 14 bytes.

#### Gouraud Triangle (3-point gradient)

```c
void gpu_triangle_gradient(int16_t x0, int16_t y0, uint16_t c0,
                           int16_t x1, int16_t y1, uint16_t c1,
                           int16_t x2, int16_t y2, uint16_t c2);
```

Barycentric color interpolation — each vertex has its own color. The interior is smoothly interpolated across all three channels. Requires FPU (RP2350 only).

**Payload:** `[0x0F][x0(2)][y0(2)][c0(2)][x1(2)][y1(2)][c1(2)][x2(2)][y2(2)][c2(2)]` — 19 bytes.

---

## 12. Sprite and Blit Operations

### 12.1 Fill Screen

```c
void gpu_fill_screen(uint16_t color);
```

Fills the **entire framebuffer** with `color`, **ignoring the active scissor rect**. The fastest way to clear the screen. Use this instead of drawing a fullscreen rectangle when you want absolute fill speed.

> **Pipeline note:** `FILL_SCREEN` calls `effect_fill_hspan()` internally, which performs a scissor-clipped `memset`. Unlike `effect_write_pixel()`, it **bypasses dithering and blend mode** — the write is always a raw overwrite regardless of the active dither and blend settings. This makes it faster but means dither/blend are not applied to the fill color.

### 12.2 `BLIT_SPRITE` — Stream Pixel Data Directly

```c
void gpu_blit_sprite(int16_t x, int16_t y, uint8_t w, uint8_t h,
                     uint8_t rle_flag,
                     const uint8_t *pixels, uint32_t pixel_bytes);
```

Streams raw pixel data from the host directly to the framebuffer. The GPU does not store this data — it is rendered once at `(x, y)` and discarded.

- `x, y` — top-left destination in the framebuffer.
- `w, h` — sprite width and height in pixels.
- `rle_flag` — `GPU_BLIT_RAW` (0) or `GPU_BLIT_RLE` (1). See §24 for RLE format.
- `pixels` — pointer to the raw or RLE-compressed pixel data.
- `pixel_bytes` — total byte count of the pixel data.

**Automatic chunking:** The SDK splits large sprites into multiple packets automatically, advancing `y` by the number of rows sent per packet. Each chunk is a full `BLIT_SPRITE` packet with its own CRC. Maximum pixel bytes per packet: `4096 - 7 = 4089` bytes.

**Hard size ceiling from firmware:** The firmware accumulates the entire `BLIT_SPRITE` payload into a 4 KB scratch buffer before dispatching (per the packet parser architecture). The parser rejects any payload exceeding `MAX_PAYLOAD_SIZE` (4096 bytes) with `ERR_PAYLOAD_TOO_LARGE`. With the 7-byte header, the effective pixel data ceiling is **4089 bytes**, which limits a single `BLIT_SPRITE` call to approximately:
- 8bpp: ≈ 63×63 pixels per packet (but the SDK row-chunks so the total sprite can be any size)
- 16bpp: ≈ 45×45 pixels per packet

Because `BLIT_SPRITE` has **no byte_offset field** in its wire format, the SDK works around this by sending multiple packets with different `y` values (row-based chunking). This works for sprites of any height, but any single row of pixel data must still fit in one packet. For sprites wider than ≈4089 pixels at 8bpp (or ≈2044 pixels at 16bpp), use `UPLOAD_VRAM` + `DRAW_VRAM_SPRITE` instead.

**The clip rect applies.** Pixels outside the current clip rect are silently discarded.

**Chroma key applies.** If transparency is enabled, pixels matching the chroma key color are not written.

### 12.3 `UPLOAD_VRAM` — Load Sprite into Persistent Cache

```c
void gpu_upload_vram(uint32_t byte_offset, uint8_t rle_flag,
                     const uint8_t *data, uint32_t byte_count);
```

Writes `byte_count` bytes of data into the GPU's VRAM (sprite cache) starting at `byte_offset`. The data persists in VRAM until the next `SYSTEM_CONFIG` or RESET.

- `byte_offset` — byte position within VRAM (0 = start of sprite cache). Use `gpu_get_vram_used()` to find the current end of allocated data.
- `rle_flag` — `GPU_BLIT_RAW` or `GPU_BLIT_RLE`.
- Data is automatically chunked by the SDK (4087 bytes per chunk maximum).
- Multiple chunks may be sent to fill large sprites — the GPU writes each chunk at `byte_offset + chunk_start`.

```c
// Example: load a 32×32 8bpp sprite
uint32_t offset = gpu_get_vram_used();
gpu_upload_vram(offset, GPU_BLIT_RAW, my_32x32_data, 1024);
// Now 'offset' is valid as the sprite's VRAM base address
```

> **VRAM is not a heap.** The GPU's VRAM is a flat bump-pointer allocator. There is no `free()` for raw offsets. Once data is uploaded, its bytes are "allocated" until the next profile switch. Use named VRAM (§15.2) if you need to track allocations by name.

### 12.4 `DRAW_VRAM_SPRITE` — Blit Cached Sprite

```c
void gpu_draw_vram_sprite(int16_t x, int16_t y, uint16_t w, uint16_t h,
                          uint32_t vram_offset,
                          uint8_t transform_flags,
                          uint16_t palette_color);
```

Blits a sprite from VRAM to the framebuffer at `(x, y)`. The sprite data at `vram_offset` must have already been loaded via `UPLOAD_VRAM`.

- `w, h` — sprite dimensions in pixels.
- `transform_flags` — bitfield of transforms (can be ORed together):

| Constant | Value | Effect |
|---|---|---|
| `GPU_TRANSFORM_ROT_0` | `0x00` | No rotation |
| `GPU_TRANSFORM_ROT_90` | `0x01` | 90° clockwise |
| `GPU_TRANSFORM_ROT_180` | `0x02` | 180° |
| `GPU_TRANSFORM_ROT_270` | `0x03` | 270° clockwise (= 90° CCW) |
| `GPU_TRANSFORM_HFLIP` | `0x04` | Horizontal flip |
| `GPU_TRANSFORM_VFLIP` | `0x08` | Vertical flip |
| `GPU_TRANSFORM_PALETTE` | `0x10` | Replace non-transparent pixels with `palette_color` |

- `palette_color` — solid color used when `GPU_TRANSFORM_PALETTE` is set. All non-chroma-key pixels are replaced with this color — useful for hit-flash effects, silhouettes, or shadows.

**Rotation notes:**
- Orthogonal rotations (0°/90°/180°/270°) work on both RP2040 and RP2350. They use pure index arithmetic — no trigonometry.
- Arbitrary-angle rotation (e.g. 45°) is **RP2350 only**, compiled with `FEATURE_ARBITRARY_ROTATION`. Not supported via this API currently (future extension).

**Clip rect and chroma key apply.**

**Payload:** `[x(2)][y(2)][w(2)][h(2)][vram_offset(4)][transform_flags(1)][palette_color(2)]` — 15 bytes.

### 12.5 `DRAW_9PATCH` — Resolution-Independent Border

```c
void gpu_draw_9patch(uint32_t vram_offset,
                     uint16_t sprite_w, uint16_t sprite_h,
                     uint8_t corner_w, uint8_t corner_h,
                     int16_t dst_x, int16_t dst_y,
                     uint16_t dst_w, uint16_t dst_h);
```

Draws a "9-patch" (also known as 9-slice) bordered panel from a VRAM-resident sprite. The sprite is divided into a 3×3 grid: four fixed-size corners, four stretchable edges, and one stretchable center.

- `sprite_w, sprite_h` — dimensions of the source sprite in VRAM.
- `corner_w, corner_h` — size of each corner region in pixels.
- `dst_x, dst_y, dst_w, dst_h` — target rectangle to fill.

The corners are drawn at their natural size. The edges and center are pixel-repeated (not scaled) to fill the remaining space. This is the standard technique for scalable UI panels, buttons, and windows.

**Usage pattern:**

```c
// Upload a 16×16 border sprite where corners are 4×4
uint32_t panel_offset = gpu_get_vram_used();
gpu_upload_vram(panel_offset, GPU_BLIT_RAW, panel_sprite_16x16, 256);

// Draw a 100×60 panel using that sprite
gpu_draw_9patch(panel_offset, 16, 16, 4, 4, 10, 10, 100, 60);
```

**Payload:** `[vram_offset(4)][sprite_w(2)][sprite_h(2)][corner_w(1)][corner_h(1)][dst_x(2)][dst_y(2)][dst_w(2)][dst_h(2)]` — 18 bytes.

### 12.6 `CAPTURE_REGION_TO_VRAM`

```c
void gpu_capture_region(int16_t src_x, int16_t src_y,
                        uint16_t w, uint16_t h,
                        uint32_t vram_offset);
```

Copies a rectangular region of the **current framebuffer** into VRAM at `vram_offset`. This creates a VRAM sprite from rendered content, enabling retained-mode caching.

**Use cases:**
- Render a complex UI panel once, capture it to VRAM, then blit it cheaply each frame.
- Cache the background scene, restore it after drawing dynamic foreground elements.

The captured data is stored in the profile's native pixel format. VRAM size consumed = `w × h × bytes_per_pixel`.

**Payload:** `[src_x(2)][src_y(2)][w(2)][h(2)][vram_offset(4)]` — 12 bytes.

---

## 13. Text Rendering

### 13.1 `gpu_render_text()`

```c
void gpu_render_text(int16_t x, int16_t y,
                     uint8_t font_id, uint16_t color,
                     uint8_t scale, const char *text);
```

Renders a null-terminated string to the framebuffer from a flash-resident font.

- `x, y` — top-left of the first character.
- `font_id` — font index. `0` = built-in 8×8 bitmap font (always available). Additional fonts depend on firmware build.
- `color` — foreground color. Background is transparent (only set pixels are written).
- `scale` — integer scale factor (`1` = native size; `2` = 2× larger; `0` = treated as `1`).
- `text` — null-terminated string (max 255 characters — SDK truncates beyond this).

**Clip rect applies.** Text that extends outside the clip rect is clipped per-pixel.

**Chroma key applies.** If a font pixel happens to match the chroma key color and transparency is enabled, that pixel is not written.

**Baseline:** Rendered from the top-left of the glyph bounding box, not from the baseline. Query font metadata to get the baseline offset for precise alignment.

**Payload:** `[x(2)][y(2)][font_id(1)][color(2)][scale(1)][text bytes][0x00]` — variable.

### 13.2 `GET_FONT_METADATA` — Text Dimension Queries

```c
// Send via gpu_query directly, or use this pattern:
uint8_t query_payload[1] = { font_id };
uint8_t meta[7];
gpu_query(GPU_OP_GET_FONT_METADATA, query_payload, 1, meta, 7);

uint16_t char_w    = (uint16_t)meta[0] | ((uint16_t)meta[1] << 8);
uint16_t char_h    = (uint16_t)meta[2] | ((uint16_t)meta[3] << 8);
uint16_t baseline  = (uint16_t)meta[4] | ((uint16_t)meta[5] << 8);
bool     scalable  = meta[6] != 0;
```

Call this once during initialization per font. Use the returned dimensions to calculate text layout:

```c
// Measure a string rendered at scale=1:
int text_w = strlen(my_string) * char_w;
int text_h = char_h;

// Center text in a 200×50 box:
int tx = box_x + (200 - text_w) / 2;
int ty = box_y + (50  - text_h) / 2;
gpu_render_text(tx, ty, 0, 0xFFFF, 1, my_string);
```

If `font_id` is invalid, the response is all zeros and `GET_STATUS` returns `ERR_INVALID_PARAM`.

---

## 14. Framebuffer and Buffer Management

### 14.1 `gpu_swap_buffers()`

```c
void gpu_swap_buffers(void);
```

In double-buffer profiles: requests a front/back buffer swap at the next VBLANK. The swap happens atomically in the GPU's VSYNC callback — zero tearing. After swapping, the new back buffer retains its previous content (the old front buffer).

No-op in single-buffer profiles.

### 14.2 `gpu_swap_buffers_immediate()`

```c
void gpu_swap_buffers_immediate(void);
```

Swaps without waiting for VBLANK. Can tear. Useful when:
- You are already within the VBLANK window (detected via TE pin interrupt).
- You want to confirm a draw immediately during debugging.

### 14.3 TE Pin — Tearing Avoidance in Single-Buffer Mode

The TE (Tearing Effect) pin pulses HIGH for the duration of VBLANK on every frame. In single-buffer mode, you can attach a hardware interrupt to TE and draw only within that window:

```c
void te_irq_handler(void) {
    // Called when TE goes HIGH — we are in VBLANK
    gpu_fill_screen(bg_color);
    draw_scene();
    // TE will go LOW when active display begins — stop drawing
}
```

The VBLANK window is approximately:
- **640×480@60:** ~1.4 ms (45 lines × 31.8 µs/line)
- **1280×720@60:** ~0.7 ms (25 lines × 27.8 µs/line)

For single-buffer profiles, budget your frame to fit within this window for tear-free output.

---

## 15. VRAM — Sprite Cache Management

### 15.1 Raw Offset Strategy

The simplest approach: track byte offsets manually.

```c
// At startup, build your offset table:
struct {
    uint32_t player;
    uint32_t enemy;
    uint32_t background;
} sprites;

sprites.player = 0;
gpu_upload_vram(sprites.player, GPU_BLIT_RAW, player_data, PLAYER_BYTES);

sprites.enemy = sprites.player + PLAYER_BYTES;
gpu_upload_vram(sprites.enemy, GPU_BLIT_RAW, enemy_data, ENEMY_BYTES);

sprites.background = sprites.enemy + ENEMY_BYTES;
gpu_upload_vram(sprites.background, GPU_BLIT_RAW, bg_data, BG_BYTES);

// Each frame:
gpu_draw_vram_sprite(px, py, 16, 16, sprites.player, GPU_TRANSFORM_ROT_0, 0);
```

There is no per-sprite free — VRAM offsets are valid until the next profile switch or RESET.

### 15.2 Named VRAM Strategy

A thin naming layer using FNV-1a hashed string names. The GPU maintains a 64-entry hash→offset table.

```c
// Allocate and upload a named sprite:
uint32_t offset = gpu_vram_alloc_named("player_idle", PLAYER_BYTES);
if (offset == GPU_VRAM_OFFSET_INVALID) {
    // ERR_VRAM_FULL or ERR_VRAM_NAME_TABLE_FULL
}
gpu_upload_vram(offset, GPU_BLIT_RAW, player_data, PLAYER_BYTES);

// Later, or after reconnect, look up by name:
uint32_t off = gpu_vram_lookup("player_idle");
if (off != GPU_VRAM_OFFSET_INVALID) {
    gpu_draw_vram_sprite(x, y, 16, 16, off, 0, 0);
}

// Free the slot (does NOT reclaim VRAM bytes — see §28.4):
gpu_vram_free_named("player_idle");
```

**Named slot persistence:** The slot table is cleared on every `SYSTEM_CONFIG` or RESET. Its value is in surviving a host **disconnect/reconnect within the same profile session** — if the GPU never switched profiles, previously named assets can be looked up without re-uploading.

```c
// After host reconnect (GPU still running, no SYSTEM_CONFIG issued):
uint32_t off = gpu_vram_lookup("player_idle");
if (off == GPU_VRAM_OFFSET_INVALID) {
    // Asset was lost (profile switch happened) — re-upload
    off = gpu_vram_alloc_named("player_idle", PLAYER_BYTES);
    gpu_upload_vram(off, GPU_BLIT_RAW, player_data, PLAYER_BYTES);
}
```

**API:**

```c
uint32_t gpu_vram_alloc_named(const char *name, uint32_t byte_count);
// Allocate 'byte_count' bytes, link to 'name'. Returns offset or GPU_VRAM_OFFSET_INVALID.
// Idempotent: calling with the same name and size returns the existing offset.

uint32_t gpu_vram_lookup(const char *name);
// Returns offset for 'name', or GPU_VRAM_OFFSET_INVALID if not found.

void gpu_vram_free_named(const char *name);
// Marks the slot free. Does NOT reclaim VRAM bytes.
```

**Hash collision warning:** The table uses FNV-1a 32-bit hashes. Hash collisions are possible but extremely unlikely for typical asset name sets. The SDK provides `gpu_fnv1a32(name)` if you need the raw hash for any reason.

---

## 16. Display Lists

Display lists record a sequence of draw commands into VRAM and replay them with a single packet. They share the VRAM pool with sprites.

### 16.1 Recording

```c
// Allocate VRAM for the display list (estimate required bytes)
uint32_t dl_offset = gpu_get_vram_used();  // or use named VRAM
uint32_t dl_max    = 4096;  // max bytes for this display list's commands

// Begin recording — subsequent draw commands go to VRAM, not the framebuffer
gpu_begin_display_list(0, dl_offset, dl_max);  // slot 0

gpu_rect_filled(0, 0, 100, 50, 0xF800);
gpu_render_text(5, 15, 0, 0xFFFF, 1, "Score: 0");
gpu_circle_filled(80, 20, 10, 0x07E0);

// End recording — finalizes header and CRC
gpu_end_display_list();
```

### 16.2 Playback

```c
// Replay the recorded commands — identical to executing them one by one,
// but sent as a single packet:
gpu_exec_display_list(0);  // slot 0
```

### 16.3 Rules and Constraints

- Only one recording session at a time. `BEGIN_DISPLAY_LIST` during an active recording returns `ERR_INVALID_PARAM`.
- `EXEC_DISPLAY_LIST` can be called inside a display list (composable), up to a nesting depth of **4**.
- Streaming commands (`BLIT_SPRITE`, `UPLOAD_VRAM`) **cannot be recorded**. They return `ERR_FEATURE_UNAVAILABLE` during a recording session.
- Display lists do not support branching, loops, or parameterisation. Use the Pawn VM for dynamic content.
- If a recorded command would push the cursor past `max_bytes`, it is dropped and `ERR_DISPLAY_LIST_FULL` is set. Recording continues for subsequent commands.
- The display list is verified on playback via a CRC-16 header checksum. A corrupted list returns `ERR_CRC_MISMATCH`.

### 16.4 VRAM Sizing Guidance

Each recorded command occupies its payload size + 3 bytes (opcode + 2-byte length) + 8 bytes for the list header. Estimate total bytes needed before allocating:

```
header = 8 bytes
per_command ≈ payload_size + 3 bytes

Example — 10 filled rects (11B each) + 5 text strings (20B each avg):
  8 + 10*(11+3) + 5*(20+3) = 8 + 140 + 115 = 263 bytes needed
```

Allocate with some headroom and pass that as `max_bytes`.

### 16.5 Slot IDs

64 slots (0–63) are available. Slot IDs index into the GPU's internal `dl_slot_vram_base[64]` and `dl_slot_max_bytes[64]` arrays. These are populated by `BEGIN_DISPLAY_LIST` and persist for the session. A slot's association to a VRAM offset is not affected by uploading new data to other VRAM regions.

---

## 17. Region Operations

### 17.1 Copy Region

```c
void gpu_copy_region(int16_t src_x, int16_t src_y, int16_t w, int16_t h,
                     int16_t dst_x, int16_t dst_y,
                     uint8_t flags);
```

Copies a rectangular region of the framebuffer from `(src_x, src_y)` to `(dst_x, dst_y)`. `flags = GPU_REGION_FLAGS_WRAP` enables wrapping at framebuffer edges. `flags = GPU_REGION_FLAGS_NONE` for a plain copy.

Overlapping regions: behavior is defined (row-order copy). Copy from bottom-right to avoid corruption when moving a region down or right.

### 17.2 Scroll Screen

```c
void gpu_scroll_screen(int16_t dx, int16_t dy,
                       uint8_t wrap_flag, uint16_t fill_color);
```

Shifts the entire framebuffer by `dx` pixels horizontally and `dy` pixels vertically.
- `dx > 0` = right, `dx < 0` = left.
- `dy > 0` = down, `dy < 0` = up.
- `wrap_flag = GPU_SCROLL_WRAP` — pixels that scroll off one edge appear on the other.
- `wrap_flag = GPU_SCROLL_CLAMP` — exposed strip is filled with `fill_color`.

### 17.3 Replace Color

```c
void gpu_replace_color(uint16_t old_color, uint16_t new_color);
```

Walks the entire framebuffer and replaces every pixel that exactly matches `old_color` with `new_color`. This is a full-framebuffer scan and will be slow for large resolutions. It is useful for palette-like effects in 8bpp mode.

### 17.4 Draw Tilemap

```c
void gpu_draw_tilemap(uint32_t tile_vram_offset, uint32_t map_vram_offset,
                      uint8_t tile_w, uint8_t tile_h,
                      uint16_t map_cols, uint16_t map_rows,
                      int16_t scroll_x, int16_t scroll_y);
```

Renders a tile-based scrollable background from VRAM-resident data.

**Prerequisites (must be done before `DRAW_TILEMAP`):**
1. Upload the tile sheet to VRAM at `tile_vram_offset`. The tile sheet is a flat array of `N` tiles, each `tile_w × tile_h` pixels, packed consecutively.
2. Upload the map grid to VRAM at `map_vram_offset`. The grid is `map_cols × map_rows` bytes, one tile index per byte (big maps may require `uint16_t` indices — current implementation uses 1 byte per tile, supporting up to 255 distinct tile types).

**Parameters:**
- `tile_vram_offset` — base of the tile sheet in VRAM.
- `map_vram_offset` — base of the map grid in VRAM.
- `tile_w, tile_h` — single tile size in pixels.
- `map_cols, map_rows` — map dimensions in tiles.
- `scroll_x, scroll_y` — pixel-level scroll offset into the map.

The GPU computes which tiles are visible given the scroll offset and current framebuffer size, and blits only those tiles. The command payload is always 18 bytes regardless of map size, because the map data is VRAM-resident.

---

## 18. Query Commands — Reading GPU State

All query commands follow the MISO response protocol (§3.4). Call them during a session's `BEGIN_FRAME` or at initialization. They can be called in any GPU state.

### 18.1 Status

```c
uint8_t gpu_get_status(void);
```

Returns the error code from the most recently completed command. See §26 for error code definitions.

**Response:** 1 byte.

### 18.2 Profile

```c
uint8_t gpu_get_profile(void);
```

Returns the active profile ID, or `0xFF` if UNINITIALIZED.

**Response:** 1 byte.

### 18.3 VRAM Free / Used

```c
uint32_t gpu_get_vram_free(void);  // bytes not yet allocated
uint32_t gpu_get_vram_used(void);  // bytes allocated (bump pointer position)
```

Returns VRAM allocation state. `free + used = total VRAM for this profile/VM configuration`. Use `gpu_get_vram_used()` to find the next free offset for a raw `UPLOAD_VRAM`.

**Response:** 4 bytes, uint32 LE each.

### 18.4 SRAM Free

```c
uint32_t gpu_get_sram_free(void);
```

Returns SRAM bytes not part of any named arena region. Useful for diagnosing memory pressure.

**Response:** 4 bytes, uint32 LE.

### 18.5 Version

```c
void gpu_get_version(uint8_t *major, uint8_t *minor, uint16_t *patch);
```

Returns firmware version. Check this at initialization to verify compatibility.

**Response:** 4 bytes — `[major(1)][minor(1)][patch_lo(1)][patch_hi(1)]`.

### 18.6 Frame Stats

```c
bool gpu_get_frame_stats(uint16_t *render_ms, uint8_t *ring_peak_pct,
                         uint8_t *missed_frames, uint32_t *frame_count);
```

Returns per-frame performance metrics. Requires `ENABLE_FRAME_STATS` to have been sent first; returns zeros otherwise.

| Field | Type | Description |
|---|---|---|
| `render_ms` | uint16 | Frame render time in milliseconds |
| `ring_peak_pct` | uint8 | Peak ring buffer fill % this frame (0–100). If consistently near 100, the host is sending too fast |
| `missed_frames` | uint8 | Frames where SWAP_BUFFERS didn't complete in the VBLANK window |
| `frame_count` | uint32 | Total frames since `ENABLE_FRAME_STATS` was last enabled |

**Response:** 8 bytes. Returns `false` on timeout.

### 18.7 VM State

```c
// Send manually:
uint8_t r[2];
gpu_query(GPU_OP_GET_VM_STATE, NULL, 0, r, 2);
uint8_t vm_status     = r[0];  // 0=unavailable, 1=available/empty, 2=loaded
uint8_t heap_used_pct = r[1];  // 0–100
```

| `r[0]` | Meaning |
|---|---|
| 0 | No VM heap reserved (RESERVE_VM=0, or profile has no VM slot) |
| 1 | VM heap reserved and empty (no procedure loaded) |
| 2 | Procedure loaded and ready |

---

## 19. Event System

The GPU maintains an unsolicited event ring buffer (capacity: 16 events). Events are generated by the GPU independently of host commands.

### 19.1 Event Types

| Constant | Value | Payload | Meaning |
|---|---|---|---|
| `GPU_EVT_FRAME_COMPLETE` | `0x01` | frame_count (uint32) | Emitted at every VBLANK when frame stats are enabled |
| `GPU_EVT_VM_PROC_DONE` | `0x02` | return value (uint32) | A scheduled or one-shot procedure completed |
| `GPU_EVT_VRAM_NEARLY_FULL` | `0x03` | bytes_free (uint32) | VRAM < 20% free |
| `GPU_EVT_ERROR` | `0x04` | error_code (uint32) | A command returned an error |
| `GPU_EVT_BUFFER_OVERFLOW` | `0xFF` | dropped_count (uint32) | Events were dropped due to ring overflow |

### 19.2 Draining Events

```c
gpu_event_record_t events[16];
uint8_t count = gpu_get_events(events, 16);

for (uint8_t i = 0; i < count; i++) {
    switch (events[i].event_type) {
        case GPU_EVT_FRAME_COMPLETE:
            frames_rendered++;
            break;
        case GPU_EVT_ERROR:
            handle_gpu_error((uint8_t)events[i].payload);
            break;
        case GPU_EVT_VM_PROC_DONE:
            on_vm_done();
            break;
        case GPU_EVT_BUFFER_OVERFLOW:
            log_warning("GPU event buffer overflowed — %u events dropped",
                        (unsigned)events[i].payload);
            break;
    }
}
```

### 19.3 INT Pin (Optional)

When `FEATURE_INT_PIN` is compiled in, the GPU drives the INT pin LOW whenever the event buffer is non-empty. Connect an interrupt-capable GPIO on the host to INT and call `gpu_get_events()` in the interrupt handler instead of polling.

Without INT, call `gpu_get_events()` once per frame (e.g. at `gpu_begin_frame()`).

---

## 20. Pixel Formats and Color Encoding

### 20.1 8bpp — RGB332

One byte per pixel. Bits: `RRRGGGBB`.

```c
// Pack an RGB332 color
uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
}
// Pass as uint16_t (upper byte ignored):
gpu_rect_filled(x, y, w, h, rgb332(255, 0, 0));  // red
```

8 shades of red and green, 4 shades of blue. 256 total colors. Enable dithering (§21) for visually smoother gradients.

### 20.2 16bpp — RGB565

Two bytes per pixel, little-endian. Bits: `RRRRRGGGGGGBBBBB`.

```c
// Pack an RGB565 color
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}
gpu_rect_filled(x, y, w, h, rgb565(255, 0, 0));  // red = 0xF800
```

Common constants:
- Black: `0x0000`
- White: `0xFFFF`
- Red: `0xF800`
- Green: `0x07E0`
- Blue: `0x001F`
- Magenta (chroma key default): `0xF81F`

### 20.3 Other Formats

| Format | Notes |
|---|---|
| `MONO8` | 8-bit greyscale; luminance stored in 1 byte; all 3 HSTX channels receive the same value |
| `INDEX8` | 8-bit palette index; palette lookup not yet implemented — stored as-is |
| `RGB121` | 4bpp nibble: bits `[3:2]` = G (0–3), `[1]` = R (0–1), `[0]` = B (0–1) |
| `MONO4` | 4bpp greyscale nibble; replicated to all channels |
| `INDEX4` | 4bpp palette index |
| `RGB888` | 3 bytes per pixel: `[R][G][B]` |

### 20.4 Color in Firmware

All colors are passed on the wire as `uint16_t` (2 bytes, little-endian). For 8bpp profiles, only the low 8 bits are used. For 4bpp profiles, only the low 4 bits. The firmware does **no color-space conversion** — the host is responsible for encoding colors in the active pixel format.

The chroma key is always stored as 16-bit RGB565 internally; on 8bpp profiles, only the low 8 bits are compared.

---

## 21. Dithering

Bayer ordered dithering is applied **transparently to all pixel writes** when enabled. It improves apparent color depth by spatially distributing quantization error.

```c
gpu_set_dither_mode(0x01);  // 2×2 Bayer (4 threshold levels)
gpu_set_dither_mode(0x02);  // 4×4 Bayer (16 threshold levels — recommended)
gpu_set_dither_mode(0x00);  // Off (default)
```

**Effect:** A gradient that would show obvious banding at 8bpp (256 colors) appears smooth and continuous with 4×4 dithering. Most effective on gradients and large flat-color areas. Has no effect on 24bpp (no quantization).

**Cost:** Zero extra SRAM. Minimal CPU overhead (one lookup per pixel write). No per-frame configuration needed — set it once and forget it.

**Recommendation:** Use `DITHER_BAYER4` by default in all 8bpp sessions.

---

## 22. Chroma Key Transparency

The GPU maintains a global chroma key color and enable flag.

```c
// Set the transparent color (default: 0xF81F = magenta in RGB565)
gpu_set_chroma_key(0xF81F);

// Enable transparency check (default: DISABLED after every RESET)
gpu_enable_transparency(true);

// Disable
gpu_enable_transparency(false);
```

**Critical rule:** `SET_CHROMA_KEY` sets the color but does **NOT** enable transparency. You must separately call `ENABLE_TRANSPARENCY(true)`. This prevents accidental transparency activation when simply updating the key color.

**Behavior when enabled:**
- During any pixel write (primitive, blit, text), if the source pixel exactly matches the chroma key color, the pixel is **not written** — the destination pixel is unchanged.
- Comparison is bitwise — no color-space awareness, no tolerance.
- On 8bpp profiles, only the low 8 bits of the 16-bit key are compared.
- Applies to `BLIT_SPRITE`, `DRAW_VRAM_SPRITE`, all primitives, and text.
- Does **not** apply to `FILL_SCREEN` (which always fills unconditionally).

**Common pattern:**

```c
// Use magenta as the transparent color in your sprite sheets
gpu_set_chroma_key(GPU_CHROMA_KEY_DEFAULT);  // 0xF81F
gpu_enable_transparency(true);

// Blit sprite with magenta pixels left transparent:
gpu_draw_vram_sprite(x, y, 16, 16, sprite_offset, GPU_TRANSFORM_ROT_0, 0);
```

---

## 23. Scissor Stack — Clip Rectangles

The GPU maintains a stack of up to 8 clip rectangles. All pixel writes from all commands respect the current top-of-stack clip rect.

```c
// Push a clip rect (intersected with current top-of-stack):
gpu_push_clip_rect(10, 10, 100, 80);

// Draw — clipped to (10,10)–(110,90)
gpu_rect_filled(0, 0, 200, 200, 0xFFFF);  // only draws within clip area

// Nested clip:
gpu_push_clip_rect(20, 20, 40, 40);  // further restricted — intersection is (20,20)–(60,60)
gpu_rect_filled(0, 0, 200, 200, 0xF800);

gpu_pop_clip_rect();  // back to (10,10)–(110,90)
gpu_pop_clip_rect();  // back to full screen
```

**Intersection guarantee:** `PUSH_CLIP_RECT` always computes the intersection of the new rect with the current stack top. A child clip can never be *larger* than its parent. This means nested clipping is always safe — inner clips cannot leak outside outer containers.

**Empty stack:** Means full-screen (no clipping). Popping an empty stack returns `ERR_CLIP_STACK_UNDERFLOW`.

**Maximum depth:** 8 levels. Pushing a 9th level returns `ERR_CLIP_STACK_OVERFLOW`; the command is ignored.

**Reset behavior:** The scissor stack is cleared (emptied) on RESET and SOFT_RESET.

**Pattern for scrollable containers:**

```c
// Clip to a scrollable list panel:
gpu_push_clip_rect(panel_x, panel_y, panel_w, panel_h);
for (int i = 0; i < item_count; i++) {
    int item_y = panel_y + i * ITEM_H - scroll_offset;
    if (item_y + ITEM_H < panel_y || item_y > panel_y + panel_h) continue;  // coarse cull
    draw_list_item(i, panel_x, item_y);  // GPU clips precisely
}
gpu_pop_clip_rect();
```

---

## 24. RLE Encoding for Pixel Data

`BLIT_SPRITE` and `UPLOAD_VRAM` support optional run-length encoding to reduce SPI transfer size for sprites with large uniform-color regions.

### 24.1 Exact Wire Format (from firmware `blit.c`)

Set `rle_flag = GPU_BLIT_RLE` in the blit or upload call. The encoded stream is a sequence of tokens:

**Token type 1 — Literal sequence:** token byte is `0x00`
```
[0x00] [N:1B] [pixel_0] [pixel_1] ... [pixel_N-1]
```
Emits `N` pixels verbatim. `N` is 1–255.

**Token type 2 — Run:** token byte is any non-zero value
```
[COUNT:1B] [color:bpp_bytes]
```
Emits `COUNT` copies of `color`. `COUNT` is 1–255.

- For **8bpp** profiles: each pixel/color is 1 byte.
- For **16bpp** profiles: each pixel/color is 2 bytes, little-endian.

**Example (8bpp):**
```
// 5 red pixels (0xE0), then 3 literal pixels:
05 E0       → E0 E0 E0 E0 E0
00 03 F0 07 20  → F0, 07, 20
```

**Important:** The decoder used by `BLIT_SPRITE` and `UPLOAD_VRAM` is identical (`rle_state_t` in `blit.c`). However, `UPLOAD_VRAM` passes the decoded pixels to VRAM storage, while `BLIT_SPRITE` passes them directly to `effect_write_pixel()` (the full pixel pipeline applies).

### 24.2 When to Use RLE

RLE is beneficial for:
- UI sprites with large solid-color areas (backgrounds, buttons, borders).
- Tiles in a tile sheet where many tiles are uniform.
- Any sprite with large chroma-key (transparent) regions — encode as a single run.

RLE is not beneficial for:
- Photos or complex gradients with few repeated pixels.
- Sprites smaller than ~64 bytes.
- RLE overhead (2-byte header per token) can make encoded data *larger* than raw if there are few runs.

---

## 25. Pixel Write Pipeline — Gate Order

All pixel writes from primitives, blits, and text go through `effect_write_pixel()` in `effects.c`. The gates are evaluated **in this exact order**; failing any gate causes the pixel to be silently skipped:

```
1. Bounds check      — (x,y) must be within framebuffer dimensions
2. Scissor test      — (x,y) must be inside the current clip rect
3. Chroma key check  — if transparency enabled and pixel == chroma_key_color: skip
4. Dithering         — bayer bias applied per-channel to the source color
5. Blend mode        — source combined with destination (XOR/OR/AND/OVERWRITE)
6. Framebuffer write — final value stored to g_fb_back
```

### 25.1 Fill Operations — Fast Path

`FILL_SCREEN` and all **filled primitive** span-fills use `effect_fill_hspan()` instead of `effect_write_pixel()`. This fast path:
- **Does apply:** bounds check, scissor clipping.
- **Does NOT apply:** chroma key, dithering, blend mode. Span fills are always raw overwrites.

**Implication:** `gpu_fill_screen()` and `gpu_rect_filled()` are unconditional overwrites regardless of the active chroma key, dither mode, or blend mode. Only per-pixel primitives (`SET_PIXEL`, outlines, etc.) and blit operations use the full gate pipeline.

### 25.2 Dithering Implementation Details

Bayer dithering applies a per-channel bias derived from the spatial position `(x mod 2, y mod 2)` for BAYER2 and `(x mod 4, y mod 4)` for BAYER4. The bias is added to each channel independently and clamped to the channel's maximum value. This prevents "color bleeding" between packed-byte channels.

- **RGB332** (8bpp): 3-bit channels get a 0–7 bias; the 2-bit blue channel gets a 0/1 bias at the 50% Bayer threshold.
- **RGB565** (16bpp): 5-bit R/B channels get a 0/1 bias (threshold at 1/16); 6-bit G gets a 0/1/2/3 bias.

---

## 26. VM Commands — Phase 4 Stub Reference

The Pawn VM (`FEATURE_PAWN_VM`) is defined in the firmware architecture but **not yet implemented in Phase 0–3**. All VM opcodes (`0x90`–`0x94` and `0x17`) currently return `ERR_FEATURE_UNAVAILABLE` and perform no action.

Do **not** use these commands in production firmware v1.x. They are documented here for completeness and future integration planning.

### 26.1 SET_VM_MODE (0x17)

```c
// Not yet available — returns ERR_FEATURE_UNAVAILABLE
// Payload: [mode(1B)]
```

| Mode constant | Value | Description |
|---|---|---|
| `VM_MODE_BLOCKING_CORE0` | `0x00` | Execute VM procedure on Core 0, blocking the render loop |
| `VM_MODE_COOPERATIVE_CORE0` | `0x01` | Execute VM in quantized slices; yield back to render loop every 1000 instructions |
| `VM_MODE_PARALLEL_CORE1` | `0x02` | Execute VM on Core 1 in parallel with rendering (RP2350 only) |

### 26.2 LOAD_PROCEDURE (0x90)

```
Payload: [proc_bytecode: N bytes]
Response: none
```

Loads a Pawn VM bytecode image into the VM heap (reserved via `RESERVE_VM=YES` in `SYSTEM_CONFIG`). The bytecode is verified for format correctness but not pre-validated for runtime safety.

### 26.3 EXEC_PROCEDURE (0x91)

```
Payload: [entry_point_offset(4B LE)]
Response: none (completion reported via GPU_EVT_VM_PROC_DONE event)
```

Begins executing the loaded procedure starting at the specified bytecode offset.

### 26.4 VM_RESET (0x92)

```
Payload: none
Response: none
```

Halts any running VM execution and resets the VM heap to empty.

### 26.5 SCHEDULE_PROCEDURE (0x93)

```
Payload: [entry_point(4B)][trigger_type(1B)]
```

Schedules the loaded procedure to run automatically on a trigger event:

| Trigger constant | Value | When |
|---|---|---|
| `TRIGGER_VBLANK` | `0x00` | Every VBLANK interrupt |
| `TRIGGER_BEGIN_FRAME` | `0x01` | When BEGIN_FRAME is received |
| `TRIGGER_END_FRAME` | `0x02` | When END_FRAME is received |

### 26.6 UNSCHEDULE_PROCEDURE (0x94)

```
Payload: [trigger_type(1B)]
```

Removes the scheduled procedure for the given trigger.

### 26.7 GET_VM_STATE (0xE4) — Current Behavior

In Phase 0–3 firmware, `GET_VM_STATE` always returns `[0x00, 0x00]` — VM unavailable. Once Phase 4 is implemented, `r[0]` will reflect the actual VM state (0=unavailable, 1=idle, 2=loaded/running) and `r[1]` the heap utilization percentage.

---

## 27. Capabilities Discovery

Call `GET_CAPABILITIES` once at connection to auto-detect what the attached firmware supports. Do not hardcode assumptions about the firmware build. This is especially important for FPU primitives, RGB565 profiles, and display lists — all of which are compile-time feature flags.

```c
uint32_t caps = gpu_get_capabilities();

if (caps & GPU_CAP_RP2350)          { /* RP2350 — 16bpp and FPU available */ }
if (caps & GPU_CAP_FPU_PRIMITIVES)  { /* Bézier, gradient, Gouraud triangle safe to use */ }
if (caps & GPU_CAP_ARBITRARY_ROTATION) { /* Non-orthogonal sprite rotation available */ }
if (caps & GPU_CAP_RGB565)          { /* 16bpp profiles available */ }
if (caps & GPU_CAP_PAWN_VM)         { /* VM commands available */ }
if (caps & GPU_CAP_DISPLAY_LIST)    { /* BEGIN/END/EXEC_DISPLAY_LIST available */ }
if (caps & GPU_CAP_NAMED_VRAM)      { /* Named VRAM slot management available */ }
if (caps & GPU_CAP_DITHERING)       { /* SET_DITHER_MODE available */ }
if (caps & GPU_CAP_EVENT_BUFFER)    { /* GET_EVENTS available */ }
if (caps & GPU_CAP_FRAME_STATS)     { /* GET_FRAME_STATS available after ENABLE_FRAME_STATS */ }
```

### 27.1 Full Capabilities Bitmask

| Bit | Constant | Feature |
|---|---|---|
| 0 | `GPU_CAP_RP2350` | RP2350 build |
| 1 | `GPU_CAP_RGB565` | 16bpp profiles available |
| 2 | `GPU_CAP_FPU_PRIMITIVES` | Bézier, gradient rect, Gouraud triangle |
| 3 | `GPU_CAP_ARBITRARY_ROTATION` | Non-orthogonal sprite rotation |
| 4 | `GPU_CAP_VECTOR_FONTS` | stb_truetype scalable font support |
| 5 | `GPU_CAP_PAWN_VM` | Pawn VM compiled in |
| 6 | `GPU_CAP_PAWN_FLOAT` | Float native functions in VM |
| 7 | `GPU_CAP_ST7796_COMPAT` | ST7796 emulation compiled in |
| 11 | `GPU_CAP_DITHERING` | Bayer dither modes |
| 12 | `GPU_CAP_DISPLAY_LIST` | Display list recording/playback |
| 13 | `GPU_CAP_NAMED_VRAM` | Named VRAM slot management |
| 14 | `GPU_CAP_BEGIN_END_FRAME` | BEGIN_FRAME / END_FRAME opcodes |
| 15 | `GPU_CAP_FRAME_STATS` | Per-frame stats accumulation |
| 16 | `GPU_CAP_EVENT_BUFFER` | Unsolicited event buffer and GET_EVENTS |
| 18 | `GPU_CAP_VM_PARALLEL_CORE1` | VM_MODE_PARALLEL_CORE1 available |
| 19 | `GPU_CAP_VM_COOPERATIVE` | VM_MODE_COOPERATIVE_CORE0 available |

---

## 28. Error Codes — Complete Reference

The error code from the most recently completed command is readable via `gpu_get_status()`. The firmware stores the error code after *every* command execution, including successful ones (`ERR_OK`). Errors are also posted to the event buffer.

> **Note on spec divergence:** The error code values in this table reflect the **actual firmware and SDK implementation**. The original architectural specification (spec §5.4) had different numeric assignments. The firmware and host SDK are the canonical authority — do not use the raw spec §5.4 table if it differs.

| Code | Constant | Meaning |
|---|---|---|
| `0x00` | `GPU_ERR_OK` | Success |
| `0x01` | `GPU_ERR_UNKNOWN_OPCODE` | Opcode not in the firmware's dispatch table |
| `0x02` | `GPU_ERR_CRC_MISMATCH` | CRC-16 verification failed — packet corrupted |
| `0x03` | `GPU_ERR_INVALID_PARAM` | Parameter out of range: off-screen coord, zero size, incompatible format, etc. |
| `0x04` | `GPU_ERR_VRAM_FULL` | UPLOAD_VRAM or VRAM_ALLOC_NAMED: VRAM exhausted |
| `0x05` | `GPU_ERR_NOT_INITIALIZED` | Draw command received while UNINITIALIZED |
| `0x06` | `GPU_ERR_VM_UNAVAILABLE` | No VM heap reserved, or VM not compiled in |
| `0x07` | `GPU_ERR_OUT_OF_MEMORY` | Arena out of memory; or display list byte_count overflow |
| `0x08` | `GPU_ERR_FEATURE_UNAVAILABLE` | Command valid, but feature not compiled in (e.g. FPU primitive on RP2040) |
| `0x09` | `GPU_ERR_CLIP_STACK_OVERFLOW` | PUSH_CLIP_RECT with stack already at depth 8 |
| `0x0A` | `GPU_ERR_CLIP_STACK_UNDERFLOW` | POP_CLIP_RECT with empty stack |
| `0x0B` | `GPU_ERR_VRAM_NOT_FOUND` | VRAM_LOOKUP or VRAM_FREE_NAMED: name hash not in table |
| `0x0C` | `GPU_ERR_DISPLAY_LIST_ACTIVE` | BEGIN_DISPLAY_LIST while already recording |
| `0x0D` | `GPU_ERR_NO_DISPLAY_LIST` | END_DISPLAY_LIST with no active recording |
| `0x0E` | `GPU_ERR_VM_FAULT` | AMX runtime fault during procedure execution |
| `0x0F` | `GPU_ERR_BUSY` | Command received while GPU was in a state that cannot accept it |
| `0x10` | `GPU_ERR_FRAME_NOT_OPEN` | Command requires an open frame (BEGIN_FRAME not called) |
| `0xFF` | `GPU_ERR_INTERNAL` | Unclassified internal firmware error |

### 28.1 Error-Checking Pattern

Most commands are fire-and-forget. Check errors only when needed:

```c
gpu_rect_filled(x, y, w, h, color);
// Not checking errors here — the host trusts its own coordinate math.

// For VRAM allocation — always check:
uint32_t offset = gpu_vram_alloc_named("bg_tile", 4096);
if (offset == GPU_VRAM_OFFSET_INVALID) {
    uint8_t err = gpu_get_status();
    // err is likely GPU_ERR_VRAM_FULL or GPU_ERR_OUT_OF_MEMORY
}
```

### 28.2 CRC Mismatch Behavior

On CRC failure:
- **Buffered commands** (draw primitives, VRAM management, etc.): Command is rejected entirely. No side effects. `ERR_CRC_MISMATCH` is set.
- **Streamed commands** (`BLIT_SPRITE`, `UPLOAD_VRAM`): The GPU has already written partial data to the destination. This is treated as a visual artifact. Re-issue the command to overwrite with correct data.

---

## 29. Integration Patterns and Recipes

### 29.1 Standard Double-Buffer Game Loop

```c
// Initialization (once):
gpu_system_config(GPU_PROFILE_320x240_DOUBLE, GPU_RESERVE_VM_NO);
gpu_wait_not_busy(5000);
gpu_set_pixel_format(GPU_PIXEL_FORMAT_RGB332);
gpu_set_dither_mode(0x02);  // 4×4 Bayer
gpu_enable_frame_stats(true);

// Upload static assets:
uint32_t player_offset = gpu_get_vram_used();
gpu_upload_vram(player_offset, GPU_BLIT_RAW, player_pixels, PLAYER_SIZE);

// Main loop:
while (1) {
    // Read frame stats / events (optional):
    uint16_t render_ms; uint8_t peak, missed; uint32_t frames;
    gpu_get_frame_stats(&render_ms, &peak, &missed, &frames);

    // Begin frame:
    gpu_begin_frame();

    // Clear back buffer:
    gpu_fill_screen(0x0000);

    // Draw scene:
    gpu_draw_vram_sprite(player_x, player_y, 16, 16,
                         player_offset, GPU_TRANSFORM_ROT_0, 0);
    gpu_render_text(2, 2, 0, 0xFFFF, 1, score_str);

    // Present:
    gpu_end_frame();   // triggers SWAP_BUFFERS at VBLANK
}
```

### 29.2 Single-Buffer with TE Synchronization

```c
gpu_system_config(GPU_PROFILE_640x480_SINGLE, GPU_RESERVE_VM_NO);
gpu_wait_not_busy(5000);

// Attach interrupt to TE pin on the host (active-HIGH edge):
attach_te_interrupt(te_irq_handler);

// In the interrupt handler:
void te_irq_handler(void) {
    // We are in VBLANK (~1.4 ms window)
    gpu_fill_screen(0x0000);
    draw_ui();
    draw_scene();
    // Draw must complete before TE goes LOW (end of VBLANK)
}
```

### 29.3 VRAM Asset Management at Session Start

```c
void load_assets(void) {
    // Query available VRAM:
    uint32_t vram_free = gpu_get_vram_free();
    
    // Load each asset in order, building an offset table:
    typedef struct { uint32_t offset; uint16_t w; uint16_t h; } sprite_t;
    
    sprite_t spr_player = { gpu_get_vram_used(), 16, 16 };
    gpu_upload_vram(spr_player.offset, GPU_BLIT_RAW, player_data, 256);
    
    sprite_t spr_bg_tile = { gpu_get_vram_used(), 32, 32 };
    gpu_upload_vram(spr_bg_tile.offset, GPU_BLIT_RAW, tile_data, 1024);
    
    // Verify we didn't overflow:
    if (gpu_get_status() == GPU_ERR_VRAM_FULL) {
        handle_asset_load_failure();
    }
}
```

### 29.4 Display List for a Static HUD

```c
void build_hud_display_list(uint32_t vram_offset) {
    gpu_begin_display_list(0, vram_offset, 2048);  // slot 0, up to 2 KB

    // Record the static HUD elements:
    gpu_draw_9patch(panel_offset, 16, 16, 4, 4, 0, 0, 320, 30);  // top bar
    gpu_render_text(4, 4, 0, 0xFFFF, 1, "HP:");
    gpu_render_text(4, 14, 0, 0xFFFF, 1, "MP:");

    gpu_end_display_list();
}

// In the main loop — one packet instead of many:
void draw_frame(void) {
    gpu_fill_screen(bg_color);
    draw_game_world();

    // Replay the static HUD (single packet):
    gpu_exec_display_list(0);

    // Only the dynamic parts need per-frame sends:
    gpu_rect_filled(24, 4, hp_bar_w, 8, 0x07E0);   // green HP bar
    gpu_rect_filled(24, 14, mp_bar_w, 8, 0x001F);  // blue MP bar
    gpu_render_text(200, 4, 0, 0xFFFF, 1, score_str);
}
```

### 29.5 Tiled Scrolling Map

```c
// Upload tile sheet (8 tiles of 16×16 at 8bpp = 8 × 256 = 2048 bytes):
uint32_t tiles_offset = gpu_get_vram_used();
gpu_upload_vram(tiles_offset, GPU_BLIT_RAW, tileset, 2048);

// Upload map grid (50×30 tiles, 1 byte each = 1500 bytes):
uint32_t map_offset = gpu_get_vram_used();
gpu_upload_vram(map_offset, GPU_BLIT_RAW, map_data, 50 * 30);

// Render scrolled tilemap each frame:
gpu_draw_tilemap(tiles_offset, map_offset,
                 16, 16,     // tile size
                 50, 30,     // map dimensions in tiles
                 scroll_x, scroll_y);  // pixel scroll offset
```

### 29.6 Capability-Safe FPU Usage

```c
uint32_t caps = gpu_get_capabilities();

void draw_smooth_curve(int x0, int y0, int cx, int cy, int x1, int y1) {
    if (caps & GPU_CAP_FPU_PRIMITIVES) {
        gpu_bezier_quad(x0, y0, cx, cy, x1, y1, 24, 0xFFFF);
    } else {
        // Fallback: straight line approximation
        gpu_line(x0, y0, x1, y1, 1, 0xFFFF);
    }
}
```

### 29.7 Safe Profile Switch

```c
void switch_to_profile(uint8_t new_profile_id) {
    // 1. Forget all VRAM offsets — they will be invalid after the switch
    memset(&sprites, 0, sizeof(sprites));

    // 2. Switch profiles (BUSY goes HIGH):
    gpu_system_config(new_profile_id, GPU_RESERVE_VM_NO);
    gpu_wait_not_busy(5000);

    // 3. Re-apply all global state:
    gpu_set_pixel_format(GPU_PIXEL_FORMAT_RGB332);
    gpu_set_dither_mode(0x02);
    gpu_enable_frame_stats(true);

    // 4. Re-upload all assets:
    load_assets();
}
```

### 29.8 Reconnect Recovery

If the host loses power or resets while the GPU keeps running (no profile switch occurred):

```c
void on_host_reconnect(void) {
    // Check GPU state without disturbing it:
    uint8_t profile = gpu_get_profile();
    if (profile == 0xFF) {
        // GPU is UNINITIALIZED — do a full init:
        gpu_system_config(GPU_PROFILE_320x240_DOUBLE, GPU_RESERVE_VM_NO);
        gpu_wait_not_busy(5000);
        load_assets();
        return;
    }

    // GPU is ACTIVE with a known profile — try to recover named assets:
    uint32_t off = gpu_vram_lookup("player_idle");
    if (off == GPU_VRAM_OFFSET_INVALID) {
        // Asset not in named table — need to re-upload
        off = gpu_vram_alloc_named("player_idle", PLAYER_BYTES);
        gpu_upload_vram(off, GPU_BLIT_RAW, player_data, PLAYER_BYTES);
    }
    sprites.player = off;
    // ... recover remaining assets
}
```

---

## 30. Known Limitations and Caveats

### 30.1 No D/C Hardware Resync (C4)

The D/C pin is wired but not yet used for hardware packet resync. The firmware uses the `0xAA` sync byte scan + CRC-16 rejection as its primary corruption recovery mechanism. This provides reasonable robustness but is not as strong as D/C-based resync. If you experience persistent corruption after a glitch, send a `SOFT_RESET` and reinitialize.

### 30.2 SPI CS Not PIO-Gated (C5)

The PIO SPI receiver does not gate on CS; it runs the clock-edge sampling loop unconditionally. This means:
- Dummy clocks generated by the host during MISO reads may inject spurious bytes into the MOSI stream. The `0xAA` sync scan + CRC catches most of these.
- If CS deasserts mid-byte, the partial byte is not flushed from the ISR shift register. The next transaction may see a framing offset.

**Mitigation:** Always deassert CS only at 8-bit boundaries (which the SDK does automatically). Keep CS HIGH for at least 100 ns between transactions.

### 30.3 BLIT_SPRITE Payload Size Ceiling (4089 bytes hard limit)

`BLIT_SPRITE` has no byte_offset field. The firmware accumulates the full payload into a 4 KB scratch buffer. Packets exceeding `MAX_PAYLOAD_SIZE` (4096 bytes) are rejected with `ERR_PAYLOAD_TOO_LARGE` in the parser — this is a hard limit. The effective pixel ceiling per packet (4089 bytes after the 7-byte header) constrains single-packet sprites to approximately **63×63 px at 8bpp** or **45×45 px at 16bpp**.

The SDK row-chunks to work around this for tall sprites, but this cannot help when a single row exceeds 4089 bytes. For sprites wider than these limits, always use `UPLOAD_VRAM` + `DRAW_VRAM_SPRITE`.

### 30.4 VRAM Named Slot: No Memory Reclaim on Free

`gpu_vram_free_named()` marks the 64-entry slot as available, but the underlying VRAM bytes are **not reclaimed**. The bump pointer (`g_vram_used`) is never decremented. Repeated alloc/free cycles will exhaust VRAM monotonically until the next profile switch.

**Implication:** Named VRAM is designed for session-level asset management (allocate at startup, use throughout, clear on profile switch). It is not suitable for dynamic per-frame allocation cycles.

### 30.5 Named VRAM and Raw UPLOAD_VRAM Cannot Safely Mix

Both `gpu_vram_alloc_named()` and `gpu_upload_vram()` share the same bump pointer (`g_vram_used`). Mixing both allocation strategies risks offset overlap:
- If you call `gpu_upload_vram()` at a raw offset below `g_vram_used`, it overwrites already-named data.
- If you call `gpu_upload_vram()` at a raw offset above `g_vram_used`, the bump pointer is not advanced, so the next named alloc may collide.

**Rule:** Use either raw offsets or named slots for a given VRAM region, not both interchangeably.

### 30.6 VRAM Is Zeroed on Every Profile Switch and RESET

There is no partial preservation. All VRAM data, all named slots, all display list registrations — gone. Design your startup code to rebuild from scratch after any `SYSTEM_CONFIG` or RESET.

### 30.7 Flood Fill Performance

`gpu_flood_fill()` is a recursive scanline flood fill. For large regions, it processes the entire fill area before dequeuing the next SPI command. During a large flood fill, the ring buffer may fill if the host continues sending commands; BUSY will assert.

**Mitigation:** Send flood fills during low-traffic moments (e.g. at the start of a frame before other draws). For large static fills, use `gpu_fill_screen()` or `gpu_rect_filled()` instead.

### 30.8 BLIT_SPRITE During Recording Ignored

Streaming commands (`BLIT_SPRITE`, `UPLOAD_VRAM`) cannot be recorded into display lists. They return `ERR_FEATURE_UNAVAILABLE` if called between `BEGIN_DISPLAY_LIST` and `END_DISPLAY_LIST`. The recording session continues; only that command is skipped.

### 30.9 Display List Nesting Depth = 4

`EXEC_DISPLAY_LIST` inside a display list is supported, but only to depth 4. Deeper nesting returns `ERR_INVALID_PARAM`. For typical HUD composition (top-level exec → panel layer → icon layer → text layer), depth 4 is sufficient.

### 30.10 50 µs Query Guard Is a Conservative Placeholder

The 50 µs delay between sending a query and reading the MISO response is a placeholder. Under maximum ring buffer load with complex commands, Core 0 scheduling latency may exceed this. If you observe stale zero-responses from queries, increase the guard time in `gpu_query()` or call `gpu_wait_not_busy()` with a timeout before the response read.

### 30.11 VREG Overclock (CPU_SPEED_FAST)

The firmware supports an optional 432 MHz / 1.40 V overclock via `CPU_SPEED_FAST=1` in the platformio.ini. This is outside Raspberry Pi's documented operating envelope and requires `vreg_disable_voltage_limit()`. The standard spec-compliant configuration is 384 MHz / 1.30 V. If you see instability, disable the overclock.

### 30.12 Hardware RESET Only Stops Main Loop

The hardware RESET handler (`GPIO_IRQ_EDGE_FALL` on GP7) sets a flag that is polled by the main loop. If Core 0 is stuck in an infinite loop inside a command handler (e.g. a pathological flood fill), the RESET flag will not be processed until the handler returns. The watchdog timer is not yet implemented. For stuck scenarios, power-cycle the GPU.

### 30.13 Filled Primitives Bypass Dither and Blend

As detailed in §25.1, `FILL_SCREEN` and all span-filled primitives (`RECT_FILLED`, `CIRCLE_FILLED`, `ELLIPSE_FILLED`, `TRIANGLE_FILLED`, `POLYGON_FILLED`, `RECT_ROUNDED_FILLED`) use `effect_fill_hspan()` internally. This path **does not apply dithering or blend mode**. If you need dithering on a filled region, use `effect_write_pixel()`-based commands (outlines, or `GRADIENT_RECT`) instead.

### 30.14 Deferred Queue Fixed at 8 KB

The deferred draw queue (single-deferred profiles) is a flat 8 KB SRAM ring allocated from the arena. The deferred command format is `[opcode(1B)][len_lo(1B)][len_hi(1B)][payload(len B)]`. If the queue is full when a draw command arrives, `ERR_OVERFLOW` is returned and the command is dropped. For deferred profiles, budget your per-frame commands to stay within 8 KB total payload.

### 30.15 SWAP_BUFFERS Is Deferred to VBLANK

`SWAP_BUFFERS` sets `g_state.swap_pending = true` and returns immediately. The actual pointer swap happens inside the VBLANK scanline callback (`line == 0`). There is a 1-frame window where `swap_pending` is true but the swap has not yet occurred. `SWAP_BUFFERS_IMMEDIATE` calls `display_rp2350_swap_buffers()` synchronously and can tear.

---

## 31. Wire-Format Quick Reference

All multi-byte integers are little-endian. All coordinates are signed 16-bit. Opcode `0x30` (DRAW_PRIMITIVE) wraps all primitives — its first payload byte is the sub-opcode.

### System Commands

| Opcode | Name | Payload bytes | Format |
|---|---|---|---|
| `0x10` | SYSTEM_CONFIG | 2 | `[profile_id(1)][reserve_vm(1)]` |
| `0x11` | SOFT_RESET | 0 | *(empty)* |
| `0x12` | BEGIN_FRAME | 0 | *(empty)* |
| `0x13` | END_FRAME | 0 | *(empty)* |
| `0x14` | SET_PIXEL_FORMAT | 1 | `[format_enum(1)]` |
| `0x15` | SET_DITHER_MODE | 1 | `[mode(1)]` |
| `0x16` | ENABLE_FRAME_STATS | 1 | `[enable(1)]` |
| `0x17` | SET_VM_MODE | 1 | `[mode(1)]` |
| `0x20` | PUSH_CLIP_RECT | 8 | `[x(2)][y(2)][w(2)][h(2)]` |
| `0x21` | POP_CLIP_RECT | 0 | *(empty)* |
| `0x22` | SET_BLEND_MODE | 1 | `[mode(1)]` |

### Drawing Commands (opcode `0x30` with sub-opcode byte 0)

| Sub-op | Primitive | Payload bytes | Format |
|---|---|---|---|
| `0x01` | SET_PIXEL | 7 | `[0x01][x(2)][y(2)][color(2)]` |
| `0x02` | LINE | 12 | `[0x02][x0(2)][y0(2)][x1(2)][y1(2)][thick(1)][color(2)]` |
| `0x03` | LINE_DASHED | 13 | `[0x03][x0(2)][y0(2)][x1(2)][y1(2)][on(1)][off(1)][color(2)]` |
| `0x04` | RECT | 12 | `[0x04][x(2)][y(2)][w(2)][h(2)][bw(1)][color(2)]` |
| `0x05` | RECT_FILLED | 11 | `[0x05][x(2)][y(2)][w(2)][h(2)][color(2)]` |
| `0x06` | RECT_ROUNDED | 13 | `[0x06][x(2)][y(2)][w(2)][h(2)][r(1)][bw(1)][color(2)]` |
| `0x07` | RECT_ROUNDED_FILLED | 12 | `[0x07][x(2)][y(2)][w(2)][h(2)][r(1)][color(2)]` |
| `0x08` | CIRCLE | 10 | `[0x08][cx(2)][cy(2)][r(2)][bw(1)][color(2)]` |
| `0x09` | CIRCLE_FILLED | 9 | `[0x09][cx(2)][cy(2)][r(2)][color(2)]` |
| `0x0A` | ELLIPSE | 12 | `[0x0A][cx(2)][cy(2)][rx(2)][ry(2)][bw(1)][color(2)]` |
| `0x0B` | ELLIPSE_FILLED | 11 | `[0x0B][cx(2)][cy(2)][rx(2)][ry(2)][color(2)]` |
| `0x0C` | ARC | 14 | `[0x0C][cx(2)][cy(2)][r(2)][start(2)][end(2)][bw(1)][color(2)]` |
| `0x0D` | TRIANGLE | 15 | `[0x0D][x0(2)][y0(2)][x1(2)][y1(2)][x2(2)][y2(2)][color(2)]` |
| `0x0E` | TRIANGLE_FILLED | 15 | `[0x0E][x0(2)][y0(2)][x1(2)][y1(2)][x2(2)][y2(2)][color(2)]` |
| `0x0F` | TRIANGLE_GRADIENT | 19 | `[0x0F][x0(2)][y0(2)][c0(2)][x1(2)][y1(2)][c1(2)][x2(2)][y2(2)][c2(2)]` |
| `0x10` | POLYGON_FILLED | 4+n×4 | `[0x10][n(1)][color(2)][x0(2)][y0(2)]...[xN(2)][yN(2)]` |
| `0x11` | BEZIER_QUAD | 16 | `[0x11][x0(2)][y0(2)][cx(2)][cy(2)][x1(2)][y1(2)][steps(1)][color(2)]` |
| `0x12` | BEZIER_CUBIC | 20 | `[0x12][x0(2)][y0(2)][cx0(2)][cy0(2)][cx1(2)][cy1(2)][x1(2)][y1(2)][steps(1)][color(2)]` |
| `0x13` | GRADIENT_RECT | 14 | `[0x13][x(2)][y(2)][w(2)][h(2)][c0(2)][c1(2)][dir(1)]` |
| `0x14` | FLOOD_FILL | 7 | `[0x14][x(2)][y(2)][fill_color(2)]` |

### Blit, Text, Buffer

| Opcode | Name | Payload bytes | Format |
|---|---|---|---|
| `0x31` | FILL_SCREEN | 2 | `[color(2)]` |
| `0x50` | BLIT_SPRITE | 7+N | `[x(2)][y(2)][w(1)][h(1)][rle(1)][pixels(N)]` |
| `0x51` | DRAW_VRAM_SPRITE | 15 | `[x(2)][y(2)][w(2)][h(2)][offset(4)][flags(1)][palette(2)]` |
| `0x52` | DRAW_9PATCH | 18 | `[offset(4)][sw(2)][sh(2)][cw(1)][ch(1)][dx(2)][dy(2)][dw(2)][dh(2)]` |
| `0x53` | CAPTURE_REGION | 12 | `[sx(2)][sy(2)][w(2)][h(2)][vram_offset(4)]` |
| `0x60` | RENDER_TEXT | 8+str+1 | `[x(2)][y(2)][font_id(1)][color(2)][scale(1)][text...][0x00]` |
| `0x70` | SWAP_BUFFERS | 0 | *(empty)* |
| `0x71` | SWAP_BUFFERS_IMM | 0 | *(empty)* |

### VRAM and Asset Management

| Opcode | Name | Payload bytes | Format |
|---|---|---|---|
| `0x80` | UPLOAD_VRAM | 9+N | `[offset(4)][count(4)][rle(1)][data(N)]` |
| `0x81` | VRAM_ALLOC_NAMED | 8 | `[hash(4)][byte_count(4)]` → response: `[offset(4)]` |
| `0x82` | VRAM_LOOKUP | 4 | `[hash(4)]` → response: `[offset(4)]` |
| `0x83` | VRAM_FREE_NAMED | 4 | `[hash(4)]` |
| `0x84` | BEGIN_DISPLAY_LIST | 9 | `[slot_id(1)][vram_offset(4)][max_bytes(4)]` (all LE) |
| `0x85` | END_DISPLAY_LIST | 0 | *(empty)* |
| `0x86` | EXEC_DISPLAY_LIST | 1 | `[slot_id(1)]` |

### Region Operations

| Opcode | Name | Payload bytes | Format |
|---|---|---|---|
| `0x32` | COPY_REGION | 13 | `[sx(2)][sy(2)][w(2)][h(2)][dx(2)][dy(2)][flags(1)]` |
| `0x33` | REPLACE_COLOR | 4 | `[old(2)][new(2)]` |
| `0x34` | DRAW_TILEMAP | 18 | `[tile_offset(4)][map_offset(4)][tw(1)][th(1)][cols(2)][rows(2)][sx(2)][sy(2)]` |
| `0x35` | SCROLL_SCREEN | 7 | `[dx(2)][dy(2)][wrap(1)][fill(2)]` |

### Transparency

| Opcode | Name | Payload bytes | Format |
|---|---|---|---|
| `0xA0` | SET_CHROMA_KEY | 2 | `[color(2)]` |
| `0xA1` | ENABLE_TRANSPARENCY | 1 | `[enable(1)]` |

### Query Responses (MISO)

| Opcode | Name | Response bytes | Format |
|---|---|---|---|
| `0xE0` | GET_STATUS | 1 | `[err_code(1)]` |
| `0xE1` | GET_PROFILE | 1 | `[profile_id(1)]` (0xFF = UNINITIALIZED) |
| `0xE2` | GET_VRAM_FREE | 4 | `[bytes_free(uint32 LE)]` |
| `0xE3` | GET_VRAM_USED | 4 | `[bytes_used(uint32 LE)]` |
| `0xE4` | GET_VM_STATE | 2 | `[state(1)][heap_pct(1)]` |
| `0xE5` | GET_SRAM_FREE | 4 | `[bytes_free(uint32 LE)]` |
| `0xE6` | GET_VERSION | 4 | `[major(1)][minor(1)][patch_lo(1)][patch_hi(1)]` |
| `0xE7` | GET_CAPABILITIES | 4 | `[caps(uint32 LE)]` |
| `0xE8` | GET_FRAME_STATS | 8 | `[render_ms(2)][peak_pct(1)][missed(1)][frame_count(4)]` |
| `0xE9` | GET_EVENTS | 1+N×8 | `[count(1)][event_type(1)][reserved(1)][ts_ms(2)][payload(4)]×N` |
| `0xEA` | GET_FONT_METADATA | 7 | `[char_w(2)][char_h(2)][baseline(2)][scalable(1)]`; query payload: `[font_id(1)]` |

---

*Document generated from PicoGPU firmware v1.x and host_sdk source.*
*For bug reports or corrections, see the project repository.*

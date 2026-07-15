# PicoGPU Host SDK

Plain C host driver for the **PicoGPU** SPI graphics coprocessor (RP2350 / RP2040).

Drop `gpu_driver.c`, `gpu_driver.h`, `gpu_opcodes.h`, and `gpu_profiles.h` into your project. Implement the five `gpu_hal_*` platform functions. Then call `gpu_system_config()` and start drawing.

## Files

| File | Purpose |
|---|---|
| `gpu_driver.h` | Public API — all drawing, query, and asset management functions |
| `gpu_driver.c` | Implementation — CRC-16 engine, packet framing, chunked streaming |
| `gpu_opcodes.h` | Opcode constants, error codes, pixel formats, capability bits |
| `gpu_profiles.h` | Profile ID constants and RESERVE_VM flags |

## Quick Start

```c
// 1. Implement the 6 HAL functions for your MCU (SPI, CS, BUSY, delay).
// 2. On boot — wait for BUSY low, then:
gpu_system_config(GPU_PROFILE_320x240_DOUBLE, GPU_RESERVE_VM_NO);
// 3. Draw:
gpu_fill_screen(0x0000);
gpu_rect_filled(10, 10, 100, 50, 0xF800);
gpu_render_text(10, 70, 0, 0xFFFF, 1, "Hello GPU");
gpu_swap_buffers();
```

**Full documentation:** see [`DOCS.md`](DOCS.md) — every API, every wire format, every behavior and caveat.

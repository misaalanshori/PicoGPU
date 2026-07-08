// gpu_profiles.h — Host SDK: Profile ID constants and buffering mode definitions
// Mirrors firmware's memory/profiles.h for host MCU use.
#pragma once
#include <stdint.h>

// =============================================================================
// Profile IDs (spec §3.4)
// =============================================================================

// 8bpp profiles
#define GPU_PROFILE_320x240_DOUBLE  0x01u  // 320×240 px, 2× scale, double-buffer, 640×480@60
#define GPU_PROFILE_640x480_SINGLE  0x02u  // 640×480 px, 1× scale, single-buffer, 640×480@60
#define GPU_PROFILE_320x180_DOUBLE  0x03u  // 320×180 px, 4× scale, double-buffer, 1280×720@60
#define GPU_PROFILE_640x360_SINGLE  0x04u  // 640×360 px, 2× scale, single-buffer, 1280×720@60
#define GPU_PROFILE_640x360_DOUBLE  0x05u  // 640×360 px, 2× scale, double-buffer, 1280×720@60

// 16bpp profiles (RP2350 only, requires FEATURE_RGB565)
#define GPU_PROFILE_320x240_16BPP   0x11u  // 320×240 px, 2× scale, double-buffer, RGB565
#define GPU_PROFILE_320x180_16BPP   0x12u  // 320×180 px, 4× scale, double-buffer, RGB565
#define GPU_PROFILE_640x360_16BPP   0x13u  // 640×360 px, 2× scale, single-buffer, RGB565

// =============================================================================
// Buffering mode constants (for reference — not sent in SYSTEM_CONFIG)
// =============================================================================
#define GPU_BUFFERING_SINGLE         1u
#define GPU_BUFFERING_DOUBLE         2u
#define GPU_BUFFERING_DEFERRED       3u

// =============================================================================
// RESERVE_VM flag for SYSTEM_CONFIG payload byte 1
// =============================================================================
#define GPU_RESERVE_VM_NO    0x00u  // Do not allocate VM heap
#define GPU_RESERVE_VM_YES   0x01u  // Allocate VM heap (profile must support it)

// =============================================================================
// Default chroma key color (RGB565 magenta, spec §6.5)
// =============================================================================
#define GPU_CHROMA_KEY_DEFAULT 0xF81Fu

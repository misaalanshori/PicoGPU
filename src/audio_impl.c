#include "shared_state.h"
#include "audio.h"
#include "pico_hdmi/video_output.h"
#include "pico_hdmi/hstx_data_island_queue.h"
#include "pico_hdmi/hstx_packet.h"
#include <math.h>

// Audio global allocations
int16_t sine_table[SINE_TABLE_SIZE];
uint32_t audio_phase = 0;
uint32_t phase_increment = 0;
int audio_frame_counter = 0;
int melody_index = 0;
int note_frames_remaining = 0;
volatile sfx_type_t current_sfx = SFX_NONE;
volatile int sfx_frame = 0;
volatile audio_mode_t current_audio_mode = AUDIO_TETRIS;

void init_sine_table(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = (float)i * 2.0f * 3.14159265f / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(sinf(angle) * TONE_AMPLITUDE);
    }
}

void advance_melody(void) {
    if (--note_frames_remaining <= 0) {
        melody_index = (melody_index + 1) % KOROBEINIKI_LENGTH;
        note_frames_remaining = korobeiniki[melody_index].duration;
        uint16_t freq = korobeiniki[melody_index].freq;
        if (freq > 0) {
            phase_increment = (uint32_t)(((uint64_t)freq << 32) / AUDIO_SAMPLE_RATE);
        } else {
            phase_increment = 0; // Rest
        }
    }
}

static inline int16_t get_sine_sample(void) {
    uint32_t sfx_phase_inc = 0;
    
    // SFX always takes highest priority (plays over any ambient noise/music)
    if (current_sfx != SFX_NONE) {
        uint32_t sfx_freq = 0;
        if (current_sfx == SFX_FLAP) {
            sfx_freq = 200 + sfx_frame * 80;
            sfx_frame++;
            if (sfx_frame > 5) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_POINT) {
            sfx_freq = (sfx_frame < 5) ? 880 : 1046;
            sfx_frame++;
            if (sfx_frame > 10) current_sfx = SFX_NONE;
        } else if (current_sfx == SFX_CRASH) {
            sfx_freq = 150 - sfx_frame * 7;
            if (sfx_freq < 40) sfx_freq = 40;
            sfx_frame++;
            if (sfx_frame > 15) current_sfx = SFX_NONE;
        }
        
        sfx_phase_inc = (uint32_t)(((uint64_t)sfx_freq << 32) / AUDIO_SAMPLE_RATE);
        static uint32_t sfx_phase = 0;
        int16_t s = sine_table[(sfx_phase >> 24) & 0xFF];
        sfx_phase += sfx_phase_inc;
        return s;
    }

    // Process active ambient mode
    if (current_audio_mode == AUDIO_TETRIS) {
        if (phase_increment == 0) return 0;
        int16_t s = sine_table[(audio_phase >> 24) & 0xFF];
        audio_phase += phase_increment;
        return s;
    } else if (current_audio_mode == AUDIO_NOISE) {
        // White Noise
        int16_t raw = (int16_t)(fast_rand() >> 16);
        int16_t noise = (int16_t)((int32_t)raw * 4000 / 32768);
        return noise;
    } else if (current_audio_mode == AUDIO_RAIN) {
        // Rain Sound: Smooth brown wash (LPF at ~76Hz) + decaying raindrop patters
        static float lp_history = 0.0f;
        static float patter_val = 0.0f;
        
        int16_t raw1 = (int16_t)(fast_rand() >> 16);
        float raw_noise = (float)raw1 * (4000.0f / 32768.0f);
        lp_history = 0.99f * lp_history + 0.15f * raw_noise;
        
        // Decay existing patter pop
        patter_val *= 0.92f;
        
        // Trigger a new raindrop patter envelope
        if ((fast_rand() % 1000) < 3) {
            int16_t raw2 = (int16_t)(fast_rand() >> 16);
            patter_val = (float)raw2 * (4000.0f / 32768.0f);
        }
        
        float final_val = lp_history + patter_val;
        if (final_val > 32767.0f) final_val = 32767.0f;
        if (final_val < -32768.0f) final_val = -32768.0f;
        
        return (int16_t)final_val;
    }

    // AUDIO_SILENT
    return 0;
}

void generate_audio(void) {
    int iterations = 0;
    while (hstx_di_queue_get_level() < 200 && iterations < 50) {
        iterations++;
        audio_sample_t samples[4];
        for (int i = 0; i < 4; i++) {
            int16_t s = get_sine_sample();
            samples[i].left = s;
            samples[i].right = s;
        }

        hstx_packet_t packet;
        audio_frame_counter = hstx_packet_set_audio_samples(&packet, samples, 4, audio_frame_counter);

        hstx_data_island_t island;
        hstx_encode_data_island(&island, &packet, false, DI_HSYNC_ACTIVE);
        if (!hstx_di_queue_push(&island)) {
            break;
        }
    }
}

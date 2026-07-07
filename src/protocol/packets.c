#include "packets.h"
#include "../transport/ring_buffer.h"
#include "../state/coprocessor_state.h"
#include "error_codes.h"
#include "dispatch.h"    // dispatch_command()

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------
// CRC-16/CCITT  (poly 0x1021, init 0xFFFF, no reflection, no final XOR)
// ---------------------------------------------------------------------------
static uint16_t g_crc_table[256];

static void _build_crc_table(void)
{
    for (uint32_t i = 0; i < 256u; i++) {
        uint16_t crc = (uint16_t)(i << 8u);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1u);
            }
        }
        g_crc_table[i] = crc;
    }
}

uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    uint8_t idx = (uint8_t)((crc >> 8u) ^ byte);
    return (uint16_t)((crc << 8u) ^ g_crc_table[idx]);
}

uint16_t crc16_compute(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Packet constants
// ---------------------------------------------------------------------------
#define SYNC_BYTE          0xAAu
#define MAX_PAYLOAD_SIZE   65535u

// ---------------------------------------------------------------------------
// Parser state machine
// ---------------------------------------------------------------------------
typedef enum {
    PKT_WAIT_SYNC,     // searching for 0xAA
    PKT_READ_OPCODE,   // next byte = opcode
    PKT_READ_LEN_LO,   // payload length low byte
    PKT_READ_LEN_HI,   // payload length high byte
    PKT_ACCUMULATE,    // collecting payload bytes
    PKT_CRC_LO,        // CRC low byte
    PKT_CRC_HI,        // CRC high byte (then validate + dispatch)
} pkt_state_t;

// 64 KB scratch buffer for payload accumulation
static uint8_t g_pkt_payload_buf[65536u];

static struct {
    pkt_state_t state;
    uint8_t     opcode;
    uint16_t    payload_len;
    uint16_t    payload_idx;
    uint16_t    running_crc;   // CRC computed over opcode+len+payload
    uint8_t     rx_crc_lo;     // received CRC low byte
} g_parser;

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void packets_init(void)
{
    _build_crc_table();

    g_parser.state       = PKT_WAIT_SYNC;
    g_parser.opcode      = 0;
    g_parser.payload_len = 0;
    g_parser.payload_idx = 0;
    g_parser.running_crc = 0xFFFFu;
    g_parser.rx_crc_lo   = 0;
}

// ---------------------------------------------------------------------------
// Reset parser to WAIT_SYNC
// ---------------------------------------------------------------------------
static void _reset_parser(void)
{
    g_parser.state       = PKT_WAIT_SYNC;
    g_parser.running_crc = 0xFFFFu;
    g_parser.payload_idx = 0;
}

// ---------------------------------------------------------------------------
// Main processing loop — called from Core 0 main loop
// ---------------------------------------------------------------------------
uint32_t packets_process(void)
{
    uint32_t dispatched = 0u;
    uint8_t  byte;

    while (rb_read_byte(&byte)) {

        switch (g_parser.state) {

        // ----------------------------------------------------------------
        case PKT_WAIT_SYNC:
            if (byte == SYNC_BYTE) {
                g_parser.running_crc = 0xFFFFu;
                g_parser.state = PKT_READ_OPCODE;
            }
            // else: keep scanning
            break;

        // ----------------------------------------------------------------
        case PKT_READ_OPCODE:
            g_parser.opcode      = byte;
            g_parser.running_crc = crc16_update(g_parser.running_crc, byte);
            g_parser.state       = PKT_READ_LEN_LO;
            break;

        // ----------------------------------------------------------------
        case PKT_READ_LEN_LO:
            g_parser.payload_len = (uint16_t)byte;
            g_parser.running_crc = crc16_update(g_parser.running_crc, byte);
            g_parser.state       = PKT_READ_LEN_HI;
            break;

        // ----------------------------------------------------------------
        case PKT_READ_LEN_HI:
            g_parser.payload_len |= (uint16_t)(byte << 8u);
            g_parser.running_crc  = crc16_update(g_parser.running_crc, byte);
            g_parser.payload_idx  = 0;

            if (g_parser.payload_len == 0u) {
                // No payload — go straight to CRC
                g_parser.state = PKT_CRC_LO;
            } else {
                g_parser.state = PKT_ACCUMULATE;
            }
            break;

        // ----------------------------------------------------------------
        case PKT_ACCUMULATE:
            g_pkt_payload_buf[g_parser.payload_idx] = byte;
            g_parser.running_crc = crc16_update(g_parser.running_crc, byte);
            g_parser.payload_idx++;

            if (g_parser.payload_idx >= g_parser.payload_len) {
                g_parser.state = PKT_CRC_LO;
            }
            break;

        // ----------------------------------------------------------------
        case PKT_CRC_LO:
            g_parser.rx_crc_lo = byte;
            g_parser.state     = PKT_CRC_HI;
            break;

        // ----------------------------------------------------------------
        case PKT_CRC_HI: {
            uint16_t rx_crc = (uint16_t)((byte << 8u) | g_parser.rx_crc_lo);

            if (rx_crc == g_parser.running_crc) {
                // Valid packet — dispatch
                dispatch_command(g_parser.opcode,
                                 g_pkt_payload_buf,
                                 g_parser.payload_len);
                dispatched++;
            } else {
                coprocessor_set_error(ERR_CRC_MISMATCH);
            }

            _reset_parser();
            break;
        }

        default:
            _reset_parser();
            break;
        }
    }

    return dispatched;
}

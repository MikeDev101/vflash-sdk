#include "vflash/input.h"

#define UART0_DATA_REG  (*(volatile uint32_t *)0x90020000)
#define UART0_LSR_REG   (*(volatile uint32_t *)0x90020014)

#define UART_LSR_DR     (1u << 0)   /* Data Ready in RX FIFO */
#define UART_LSR_THRE   (1u << 5)   /* Transmitter Holding Register Empty */

#define RAW_AXIS_CENTER 2048
#define RAW_AXIS_SPAN   1792
#define DEADZONE_INT16  16000

/* State instance */
static vflash_input_state_t s_state;
static uint32_t s_prev_buttons = 0;
static uint32_t s_async_buttons = 0;
static uint32_t s_consecutive_misses = 0;

/* 9-byte poll request packet: header 0xFF, 0x01, 0x02, followed by 6 packed bytes */
static const uint8_t s_poll_request[9] = {
    0xFF, 0x01, 0x02, 0x40, 0x40, 0x40, 0x43, 0x40, 0x40
};

static inline void uart_send_byte(uint8_t byte)
{
    uint32_t timeout = 50000;
    while ((UART0_LSR_REG & UART_LSR_THRE) == 0 && --timeout) {
        __asm__ volatile("" ::: "memory");
    }
    UART0_DATA_REG = (uint32_t)byte;
}

static inline int uart_recv_byte(void)
{
    if ((UART0_LSR_REG & UART_LSR_DR) == 0) {
        return -1;
    }
    return (int)(UART0_DATA_REG & 0xFF);
}

/*
 * Decode pairs of payload bytes from triples of 6-bit shifted serial bytes.
 * Returns decoded byte count.
 */
static int decode_packet(const uint8_t *src, int src_len, uint8_t *dst, int max_dst)
{
    int dst_idx = 0;
    int src_idx = 0;

    while (src_idx + 2 < src_len && dst_idx + 1 < max_dst) {
        uint8_t c0 = src[src_idx] & 0x3F;
        uint8_t c1 = src[src_idx + 1] & 0x3F;
        uint8_t c2 = src[src_idx + 2] & 0x3F;

        dst[dst_idx] = (uint8_t)((c0 & 0x0F) | ((c1 & 0x0F) << 4));
        dst[dst_idx + 1] = (uint8_t)(((c1 >> 4) & 0x03) | ((c2 & 0x3F) << 2));

        dst_idx += 2;
        src_idx += 3;
    }
    return dst_idx;
}

int vflash_input_init(void)
{
    /* Drain any stale bytes from UART0 RX FIFO */
    for (int i = 0; i < 256; i++) {
        if (uart_recv_byte() < 0) {
            break;
        }
    }

    s_state.buttons = 0;
    s_state.pressed = 0;
    s_state.released = 0;
    s_state.axis_x = 0;
    s_state.axis_y = 0;
    s_state.raw_x = RAW_AXIS_CENTER;
    s_state.raw_y = RAW_AXIS_CENTER;
    s_state.connected = false;

    s_prev_buttons = 0;
    s_async_buttons = 0;
    s_consecutive_misses = 0;

    return 0;
}

void vflash_input_shutdown(void)
{
    s_state.buttons = 0;
    s_state.pressed = 0;
    s_state.released = 0;
    s_state.axis_x = 0;
    s_state.axis_y = 0;
    s_state.connected = false;
    s_prev_buttons = 0;
    s_async_buttons = 0;
}

const vflash_input_state_t *vflash_input_get_state(void)
{
    return &s_state;
}

void vflash_input_poll(void)
{
    /* Send the 9-byte poll request */
    for (int i = 0; i < 9; i++) {
        uart_send_byte(s_poll_request[i]);
    }

    /* Buffer for assembling incoming frames from UART */
    uint8_t rx_buf[32];
    int rx_count = 0;
    bool got_poll_response = false;

    /* Read available bytes from UART RX FIFO */
    uint32_t timeout = 50000;
    while (--timeout) {
        int b = uart_recv_byte();
        if (b < 0) {
            if (got_poll_response && rx_count == 0) {
                break;
            }
            continue;
        }

        /* Frame synchronization: frames begin with 0xFF 0x01 */
        if (rx_count == 0) {
            if ((uint8_t)b == 0xFF) {
                rx_buf[rx_count++] = 0xFF;
            }
            continue;
        }
        if (rx_count == 1) {
            if ((uint8_t)b == 0x01) {
                rx_buf[rx_count++] = 0x01;
            } else if ((uint8_t)b == 0xFF) {
                rx_buf[0] = 0xFF;
            } else {
                rx_count = 0;
            }
            continue;
        }

        rx_buf[rx_count++] = (uint8_t)b;

        /* Once we have length at index 2, check if frame is complete */
        if (rx_count >= 3) {
            uint8_t plen = rx_buf[2];
            int expected_frame_len = 3 * ((plen + 1) / 2) + 6;

            if (rx_count >= expected_frame_len) {
                /* Decode frame */
                uint8_t decoded[16];
                int dec_len = decode_packet(&rx_buf[3], expected_frame_len - 3, decoded, sizeof(decoded));

                if (dec_len >= plen + 2) {
                    /* Verify 16-bit checksum */
                    uint16_t chk_received = (uint16_t)(decoded[plen] | (decoded[plen + 1] << 8));
                    uint16_t chk_computed = (uint16_t)(plen + 1);
                    for (int p = 0; p < plen; p++) {
                        chk_computed = (uint16_t)(chk_computed + decoded[p]);
                    }

                    if (chk_received == chk_computed) {
                        if (plen == 4) {
                            /* 4-byte polled response packet */
                            uint16_t raw_y = (uint16_t)(((decoded[1] << 8) & 0x0F00) | decoded[0]);
                            uint16_t raw_x = (uint16_t)(((decoded[2] << 4) & 0x0FF0) | ((decoded[1] >> 4) & 0x000F));
                            uint8_t btn_ok = (uint8_t)(decoded[3] & 1);

                            s_state.raw_x = raw_x;
                            s_state.raw_y = raw_y;

                            /* Normalize axes to signed 16-bit without hardware division:
                             * (dx * 32767) / 1792 ~= (dx * 18) + ((dx * 73) >> 8) */
                            int32_t dx = (int32_t)raw_x - RAW_AXIS_CENTER;
                            int32_t val_x = (dx * 18) + ((dx * 73) >> 8);
                            if (val_x > 32767) val_x = 32767;
                            if (val_x < -32768) val_x = -32768;
                            s_state.axis_x = (int16_t)val_x;

                            int32_t dy = RAW_AXIS_CENTER - (int32_t)raw_y;
                            int32_t val_y = (dy * 18) + ((dy * 73) >> 8);
                            if (val_y > 32767) val_y = 32767;
                            if (val_y < -32768) val_y = -32768;
                            s_state.axis_y = (int16_t)val_y;

                            if (btn_ok) {
                                s_state.buttons |= VFLASH_BTN_OK;
                            } else {
                                s_state.buttons &= ~VFLASH_BTN_OK;
                            }

                            got_poll_response = true;
                            s_state.connected = true;
                            s_consecutive_misses = 0;
                        } else if (plen == 6) {
                            /* 6-byte periodic frame broadcast packet:
                             * decoded[0..1]: raw Y axis (0x7F=Up, 0x81=Down, 0=neutral)
                             * decoded[1..2]: raw X axis (0x7F=Right, 0x81=Left, 0=neutral)
                             * decoded[2..4]: 16-bit button bitmask */
                            uint8_t raw_y_byte = (uint8_t)((decoded[0] >> 4) | ((decoded[1] & 0x0F) << 4));
                            uint8_t raw_x_byte = (uint8_t)((decoded[1] >> 5) | ((decoded[2] & 0x1F) << 3));
                            uint32_t raw_btns = (uint32_t)((decoded[2] >> 6) | (decoded[3] << 2) | ((decoded[4] & 0x3F) << 10));

                            int16_t axis_x = 0;
                            if (raw_x_byte == 0x7F) axis_x = 32767;
                            else if (raw_x_byte == 0x81) axis_x = -32768;
                            else if (raw_x_byte > 0 && raw_x_byte < 0x80) axis_x = (int16_t)(raw_x_byte * 258);
                            else if (raw_x_byte > 0x80) axis_x = (int16_t)((int8_t)raw_x_byte * 258);

                            int16_t axis_y = 0;
                            if (raw_y_byte == 0x7F) axis_y = -32768; /* Up is negative in screen/SDL coords */
                            else if (raw_y_byte == 0x81) axis_y = 32767;  /* Down is positive */
                            else if (raw_y_byte > 0 && raw_y_byte < 0x80) axis_y = (int16_t)(-(raw_y_byte * 258));
                            else if (raw_y_byte > 0x80) axis_y = (int16_t)(-((int8_t)raw_y_byte * 258));

                            s_state.axis_x = axis_x;
                            s_state.axis_y = axis_y;
                            s_state.raw_x = (uint16_t)(RAW_AXIS_CENTER + (((int32_t)axis_x * 1792) >> 15));
                            s_state.raw_y = (uint16_t)(RAW_AXIS_CENTER - (((int32_t)axis_y * 1792) >> 15));

                            uint32_t btns = 0;
                            if (raw_btns & (1u << 2))  btns |= VFLASH_BTN_OK;
                            if (raw_btns & (1u << 12)) btns |= VFLASH_BTN_RED;
                            if (raw_btns & (1u << 11)) btns |= VFLASH_BTN_YELLOW;
                            if (raw_btns & (1u << 14)) btns |= VFLASH_BTN_GREEN;
                            if (raw_btns & (1u << 13)) btns |= VFLASH_BTN_BLUE;

                            s_state.buttons = (s_state.buttons & ~(VFLASH_BTN_OK | VFLASH_BTN_RED | VFLASH_BTN_YELLOW | VFLASH_BTN_GREEN | VFLASH_BTN_BLUE)) | btns;
                            got_poll_response = true;
                            s_state.connected = true;
                            s_consecutive_misses = 0;
                        } else if (plen == 2) {
                            /* 2-byte asynchronous action button packet */
                            uint8_t btn_id = decoded[0];
                            if (btn_id == 1) s_async_buttons |= VFLASH_BTN_RED;
                            else if (btn_id == 2) s_async_buttons |= VFLASH_BTN_YELLOW;
                            else if (btn_id == 3) s_async_buttons |= VFLASH_BTN_GREEN;
                            else if (btn_id == 4) s_async_buttons |= VFLASH_BTN_BLUE;
                        }
                    }
                }
                /* Reset frame buffer for next packet in FIFO */
                rx_count = 0;
            }
        }
    }

    if (!got_poll_response) {
        s_consecutive_misses++;
        if (s_consecutive_misses > 10) {
            s_state.connected = false;
        }
    }

    /* Compute digital directional buttons from analog axes with deadzone */
    uint32_t dir_buttons = 0;
    if (s_state.axis_x < -DEADZONE_INT16) dir_buttons |= VFLASH_BTN_LEFT;
    if (s_state.axis_x > DEADZONE_INT16)  dir_buttons |= VFLASH_BTN_RIGHT;
    if (s_state.axis_y < -DEADZONE_INT16) dir_buttons |= VFLASH_BTN_UP;
    if (s_state.axis_y > DEADZONE_INT16)  dir_buttons |= VFLASH_BTN_DOWN;

    /* Combine OK button, directional buttons, and any async button triggers */
    uint32_t cur_buttons = (s_state.buttons & VFLASH_BTN_OK) | dir_buttons | s_async_buttons;
    s_async_buttons = 0; /* Consume one-shot triggers */

    s_state.buttons = cur_buttons;
    s_state.pressed = cur_buttons & ~s_prev_buttons;
    s_state.released = ~cur_buttons & s_prev_buttons;
    s_prev_buttons = cur_buttons;
}

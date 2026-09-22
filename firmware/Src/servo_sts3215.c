#include "servo_sts3215.h"
#include <string.h>

/* === STS 프로토콜 내부 상수 === */
#define HEADER1                 0xFF
#define HEADER2                 0xFF
#define BROADCAST_ID            0xFE
#define INST_PING               0x01
#define INST_READ               0x02
#define INST_WRITE              0x03
#define INST_SYNC_WRITE         0x83
#define READ_BYTES              8
#define READ_RESP_LEN_BYTE      (READ_BYTES + 2)
#define ACK_LEN_BYTE            2
#define TIMEOUT_MS              30

static uint8_t calc_checksum(const uint8_t *buf, uint8_t len) {
    uint16_t sum = 0;
    for (uint8_t i = 2; i < len - 1; i++) sum += buf[i];
    return (uint8_t)(~sum & 0xFF);
}

bool sts_ping(UART_HandleTypeDef *huart, uint8_t id) {
    uint8_t pkt[6];
    pkt[0] = HEADER1; pkt[1] = HEADER2; pkt[2] = id;
    pkt[3] = 2; pkt[4] = INST_PING;
    pkt[5] = calc_checksum(pkt, 6);
    if (HAL_UART_Transmit(huart, pkt, 6, 50) != HAL_OK) return false;

    uint8_t raw[7] = {0};
    HAL_UART_Receive(huart, raw, 7, TIMEOUT_MS);
    uint16_t received = 7 - huart->RxXferCount;
    if (received < 6) return false;

    for (int off = 0; off + 5 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == 2) {
            uint8_t resp[6];
            memcpy(resp, &raw[off], 6);
            if (resp[5] == calc_checksum(resp, 6) && resp[4] == 0) return true;
        }
    }
    return false;
}

sts_read_result_t sts_read_state(UART_HandleTypeDef *huart, uint8_t id) {
    sts_read_result_t r = {0};
    uint8_t req[8];
    req[0] = HEADER1; req[1] = HEADER2; req[2] = id;
    req[3] = 4; req[4] = INST_READ;
    req[5] = STS_REG_PRESENT_POSITION; req[6] = READ_BYTES;
    req[7] = calc_checksum(req, 8);

    if (HAL_UART_Transmit(huart, req, 8, 50) != HAL_OK) return r;

    uint8_t raw[15] = {0};
    HAL_UART_Receive(huart, raw, 15, TIMEOUT_MS);
    uint16_t received = 15 - huart->RxXferCount;
    if (received < 14) return r;

    for (int off = 0; off + 13 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == READ_RESP_LEN_BYTE) {
            uint8_t resp[14];
            memcpy(resp, &raw[off], 14);
            if (resp[13] == calc_checksum(resp, 14)) {
                r.position = resp[5]  | ((uint16_t)resp[6]  << 8);
                r.load     = resp[9]  | ((uint16_t)resp[10] << 8);
                r.ok       = (resp[4] == 0);
                return r;
            }
        }
    }
    return r;
}

sts_full_state_t sts_read_full_state(UART_HandleTypeDef *huart, uint8_t id) {
    sts_full_state_t r = {0};
    uint8_t req[8];
    req[0] = HEADER1; req[1] = HEADER2; req[2] = id;
    req[3] = 4; req[4] = INST_READ;
    req[5] = STS_REG_PRESENT_POSITION; req[6] = READ_BYTES;
    req[7] = calc_checksum(req, 8);

    if (HAL_UART_Transmit(huart, req, 8, 50) != HAL_OK) return r;

    uint8_t raw[15] = {0};
    HAL_UART_Receive(huart, raw, 15, TIMEOUT_MS);
    uint16_t received = 15 - huart->RxXferCount;
    if (received < 14) return r;

    for (int off = 0; off + 13 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == READ_RESP_LEN_BYTE) {
            uint8_t resp[14];
            memcpy(resp, &raw[off], 14);
            if (resp[13] == calc_checksum(resp, 14)) {
                r.position      = resp[5]  | ((uint16_t)resp[6]  << 8);
                r.speed         = sts_speed_to_signed(
                    resp[7] | ((uint16_t)resp[8] << 8));
                r.load          = sts_load_to_signed(resp[9] | ((uint16_t)resp[10] << 8));
                r.voltage_dV    = resp[11];
                r.temperature_C = resp[12];
                r.ok            = (resp[4] == 0);
                return r;
            }
        }
    }
    return r;
}

sts_write_result_t sts_write_byte(UART_HandleTypeDef *huart, uint8_t id, uint8_t addr, uint8_t val) {
    sts_write_result_t r = {0};
    uint8_t req[8];
    req[0] = HEADER1; req[1] = HEADER2; req[2] = id;
    req[3] = 4; req[4] = INST_WRITE;
    req[5] = addr; req[6] = val;
    req[7] = calc_checksum(req, 8);

    if (HAL_UART_Transmit(huart, req, 8, 50) != HAL_OK) return r;

    uint8_t raw[7] = {0};
    HAL_UART_Receive(huart, raw, 7, TIMEOUT_MS);
    uint16_t received = 7 - huart->RxXferCount;
    if (received < 6) return r;

    for (int off = 0; off + 5 < received; off++) {
        if (raw[off] == HEADER1 && raw[off+1] == HEADER2
            && raw[off+2] == id && raw[off+3] == ACK_LEN_BYTE) {
            uint8_t resp[6];
            memcpy(resp, &raw[off], 6);
            if (resp[5] == calc_checksum(resp, 6)) {
                r.ok = (resp[4] == 0);
                return r;
            }
        }
    }
    return r;
}

bool sts_sync_write_goal(UART_HandleTypeDef *huart,
                         const uint8_t *ids, const uint16_t *goals, uint8_t count) {
    if (count == 0 || count > 12) return false;

    uint8_t pkt[8 + 12 * 3];
    uint8_t len_field = (1 + 2) * count + 4;

    pkt[0] = HEADER1; pkt[1] = HEADER2; pkt[2] = BROADCAST_ID;
    pkt[3] = len_field; pkt[4] = INST_SYNC_WRITE;
    pkt[5] = STS_REG_GOAL_POSITION; pkt[6] = 2;

    for (uint8_t i = 0; i < count; i++) {
        pkt[7 + i*3 + 0] = ids[i];
        pkt[7 + i*3 + 1] = goals[i] & 0xFF;
        pkt[7 + i*3 + 2] = (goals[i] >> 8) & 0xFF;
    }

    uint8_t total_len = 8 + count * 3;
    pkt[total_len - 1] = calc_checksum(pkt, total_len);

    return (HAL_UART_Transmit(huart, pkt, total_len, 50) == HAL_OK);
}

int16_t sts_load_to_signed(uint16_t raw) {
    int16_t mag = (int16_t)(raw & 0x3FF);
    return (raw & 0x400) ? -mag : mag;
}

int16_t sts_speed_to_signed(uint16_t raw) {
    int16_t mag = (int16_t)(raw & 0x7FFF);
    return (raw & 0x8000) ? -mag : mag;
}

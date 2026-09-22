#include "spi_protocol.h"
#include "robot_state.h"
#include "stm32f4xx_hal.h"
#include <string.h>

uint16_t crc16_ccitt_false(const uint8_t *data, uint32_t length) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline float read_f32_le(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static inline void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline float read_f32_le_u32(const uint8_t *p) {
    /* read_u32_le inline for float */
    uint32_t v = (uint32_t)p[0]
               | ((uint32_t)p[1] << 8)
               | ((uint32_t)p[2] << 16)
               | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static inline uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline void write_f32_le(uint8_t *p, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    write_u32_le(p, v);
}

/*
 * MOSI layout (117 byte payload):
 *   0: magic(2)  2: seq(2)  4: timestamp_us(4)
 *   8: mode(1)   9: flags(1)
 *  10: target_rad[12](48)
 *  58: max_delta_rad[12](48)
 * 106: gait_phase(4)
 * 110: gait_cycle_count(4)
 * 114: motion_state(1)
 * 115: crc16(2)
 */
decode_result_t spi_decode_command(const uint8_t *rx) {
    uint16_t magic = read_u16_le(rx + 0);
    if (magic != SPI_MOSI_MAGIC)
        return DECODE_BAD_MAGIC;

    uint16_t calc_crc = crc16_ccitt_false(rx, MOSI_PAYLOAD_SIZE - 2);
    uint16_t recv_crc = read_u16_le(rx + MOSI_PAYLOAD_SIZE - 2);
    if (calc_crc != recv_crc) {
        g_robot_state.crc_error_count++;
        g_robot_state.fault_code = FAULT_CRC_ERROR;
        return DECODE_BAD_CRC;
    }

    g_robot_state.cmd_seq = read_u16_le(rx + 2);

    /* Wire mode -> internal control_mode_t.
     * STM 의 wire 는 DISABLE/OPERATE 두 가지뿐. ROS 의 semantic mode (STAND/RL/CROUCH 등)
     * 는 bridge 에서 OPERATE 로 매핑됨. E_STOP 은 SPI_FLAG_E_STOP 비트로 전달 → control_loop 처리.
     * Unknown wire 값은 안전 fallback (DISABLE).
     */
    uint8_t wire_mode = rx[8];
    switch (wire_mode) {
        case SPI_MODE_OPERATE:
            g_robot_state.mode = MODE_POSITION;
            break;
        case SPI_MODE_DISABLE:
        default:
            g_robot_state.mode = MODE_IDLE;
            break;
    }
    g_robot_state.flags = rx[9];

    for (int i = 0; i < NUM_JOINTS; i++)
        g_robot_state.target_rad[i] = read_f32_le(rx + 10 + i * 4);

    for (int i = 0; i < NUM_JOINTS; i++)
        g_robot_state.max_delta_rad[i] = read_f32_le(rx + 58 + i * 4);

    g_robot_state.gait_phase       = read_f32_le_u32(rx + 106);
    g_robot_state.gait_cycle_count = read_u32_le(rx + 110);
    g_robot_state.motion_state     = rx[114];

    g_robot_state.last_cmd_time_ms = HAL_GetTick();
    g_robot_state.status |= STATUS_BIT_CMD_FRESH;

    return DECODE_OK;
}

/*
 * MISO layout (261 byte):
 *   0: magic(2)         2: seq_echo(2)       4: timestamp_us(4)
 *   8: status(1)        9: fault_code(1)    10: motion_state(1)
 *  11: gait_phase(4)   15: gait_cycle_count(4)
 *  19: imu_yaw_rad(4)
 *  23: position_rad[12](48)
 *  71: velocity_rad_s[12](48)
 * 119: load_or_current[12](48)
 * 167: temperature[12](48)
 * 215: gyro_rad_s[3](12)
 * 227: accel_m_s2[3](12)
 * 239: quat_wxyz[4](16)
 * 255: bus_voltage(4)
 * 259: crc16(2)
 */
void spi_encode_feedback(uint8_t *tx) {
    memset(tx, 0, SPI_FRAME_SIZE);

    write_u16_le(tx + 0, SPI_MISO_MAGIC);
    write_u16_le(tx + 2, g_robot_state.cmd_seq);
    write_u32_le(tx + 4, HAL_GetTick() * 1000);
    tx[8] = g_robot_state.status;
    tx[9] = (uint8_t)g_robot_state.fault_code;

    tx[10] = g_robot_state.motion_state;

    /* gait echo */
    write_f32_le(tx + 11, g_robot_state.gait_phase);
    write_u32_le(tx + 15, g_robot_state.gait_cycle_count);

    /* imu_yaw_rad (Euler yaw deg → rad) */
    write_f32_le(tx + 19, g_robot_state.imu.yaw * 3.14159265f / 180.0f);

    for (int i = 0; i < NUM_JOINTS; i++)
        write_f32_le(tx + 23 + i * 4, g_robot_state.position_rad[i]);

    for (int i = 0; i < NUM_JOINTS; i++)
        write_f32_le(tx + 71 + i * 4, g_robot_state.velocity_rad_s[i]);

    for (int i = 0; i < NUM_JOINTS; i++)
        write_f32_le(tx + 119 + i * 4, g_robot_state.load[i]);

    for (int i = 0; i < NUM_JOINTS; i++)
        write_f32_le(tx + 167 + i * 4, g_robot_state.temperature[i]);

    for (int i = 0; i < 3; i++)
        write_f32_le(tx + 215 + i * 4, g_robot_state.imu.gyro[i]);

    for (int i = 0; i < 3; i++)
        write_f32_le(tx + 227 + i * 4, g_robot_state.imu.accel[i]);

    for (int i = 0; i < 4; i++)
        write_f32_le(tx + 239 + i * 4, g_robot_state.imu.quat[i]);

    write_f32_le(tx + 255, g_robot_state.bus_voltage);

    uint16_t crc = crc16_ccitt_false(tx, 259);
    write_u16_le(tx + 259, crc);
}

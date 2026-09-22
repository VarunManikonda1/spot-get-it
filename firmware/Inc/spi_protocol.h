#ifndef SPI_PROTOCOL_H
#define SPI_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "robot_state.h"

/*
 * Wire protocol 정의 — Jetson actuator_bridge 와 binary-compatible.
 * Command (Jetson → STM): 117 byte
 * Feedback (STM → Jetson): 261 byte
 * 50 Hz, UART 921600 8N1 (이전: SPI Mode 0 5MHz 261B padded full-duplex)
 * Endianness: little-endian (양쪽 ARM).
 *
 * "SPI" 라는 이름은 역사적 — 실제 transport 는 UART 이며 frame 자체는
 * transport-agnostic. 매크로/타입명은 Jetson 코드와의 호환을 위해 유지.
 */

#define SPI_NUM_JOINTS          12

#define SPI_MOSI_MAGIC          0xA55A
#define SPI_MISO_MAGIC          0x5AA5

/* MOSI (command) frame: 117 byte, padding 없음 */
#define MOSI_PAYLOAD_SIZE       117

/* MISO (feedback) frame: 261 byte, padding 없음 */
#define MISO_PAYLOAD_SIZE       261

/* SPI_FRAME_SIZE — 레거시 alias (MISO 크기와 동일).
 * control_loop.c 등에서 TX buffer 크기로 아직 사용 중. Step C 후 제거 예정. */
#define SPI_FRAME_SIZE          MISO_PAYLOAD_SIZE

/* Jetson -> STM wire mode
 * STM 의 실제 행동은 두 가지뿐 — torque off / target 따라가기.
 * 의미적 모드 (STAND/RL/CROUCH 등 robot_interfaces/JointTarget.msg) 는
 * Jetson 측 bridge 가 OPERATE 로 매핑하고 자세 차이는 target_rad 로 결정.
 * E_STOP 은 별도 wire mode 가 아니라 SPI_FLAG_E_STOP 비트로 전달.
 */
#define SPI_MODE_DISABLE        0   /* torque off — safe state */
#define SPI_MODE_OPERATE        1   /* torque on + apply target_rad */

/* STM -> Jetson motion_state (feedback)
 * Jetson -> STM command 의 motion_state 를 그대로 echo.
 * 값 정의는 robot_interfaces/msg/StmMotion.msg 와 Jetson bridge 쪽을 따른다.
 */
#define MOTION_STATE_STOP       0
#define MOTION_STATE_UNKNOWN    8

/* flags 비트 — 현재 펌웨어/Jetson 양쪽 모두 사용 안 함 (RL/stand 모두 flags=0 송신).
 * STM 의 torque on/off 는 wire mode dispatch 가 자동 결정:
 *   wire OPERATE → torque on, wire DISABLE → torque off.
 * E-STOP 은 STM 내부 트리거 (UART ESC / safety_check) 로만 발동.
 * 비트 정의는 향후 reserved hook 으로 남겨둠.
 */
#define SPI_FLAG_TORQUE_EN      (1 << 0)   /* reserved (unused) */
#define SPI_FLAG_E_STOP         (1 << 3)   /* reserved (unused) */

/* MOSI: Jetson -> STM (117 byte, no padding) */
typedef struct __attribute__((packed)) {
    uint16_t magic;                             /* 0xA55A */
    uint16_t seq;
    uint32_t timestamp_us;
    uint8_t  mode;
    uint8_t  flags;
    float    target_rad[SPI_NUM_JOINTS];        /* 48 */
    float    max_delta_rad[SPI_NUM_JOINTS];     /* 48 */
    float    gait_phase;                        /* 4  */
    uint32_t gait_cycle_count;                  /* 4  */
    uint8_t  motion_state;                      /* 1  */
    uint16_t crc16;
} spi_mosi_frame_t;

/* MISO: STM -> Jetson (261 byte, 패딩 없음) */
typedef struct __attribute__((packed)) {
    uint16_t magic;                             /* 0x5AA5 */
    uint16_t seq_echo;
    uint32_t timestamp_us;
    uint8_t  status;
    uint8_t  fault_code;
    uint8_t  motion_state;                      /* 1  */
    float    gait_phase;                        /* 4  — echo from MOSI */
    uint32_t gait_cycle_count;                  /* 4  — echo from MOSI */
    float    imu_yaw_rad;                       /* 4  */
    float    position_rad[SPI_NUM_JOINTS];      /* 48 */
    float    velocity_rad_s[SPI_NUM_JOINTS];    /* 48 */
    float    load_or_current[SPI_NUM_JOINTS];   /* 48 */
    float    temperature[SPI_NUM_JOINTS];       /* 48 */
    float    gyro_rad_s[3];                     /* 12 */
    float    accel_m_s2[3];                     /* 12 */
    float    quat_wxyz[4];                      /* 16 */
    float    bus_voltage;                       /* 4  */
    uint16_t crc16;                             /* 2  */
} spi_miso_frame_t;

_Static_assert(sizeof(spi_mosi_frame_t) == MOSI_PAYLOAD_SIZE, "MOSI frame size mismatch");
_Static_assert(sizeof(spi_miso_frame_t) == MISO_PAYLOAD_SIZE, "MISO frame size mismatch");

/* === Decode 결과 === */
typedef enum {
    DECODE_OK = 0,
    DECODE_BAD_MAGIC,
    DECODE_BAD_CRC,
} decode_result_t;

/* CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF) */
uint16_t crc16_ccitt_false(const uint8_t *data, uint32_t length);

/* MOSI 수신 buffer -> g_robot_state 갱신 */
decode_result_t spi_decode_command(const uint8_t *rx_buffer);

/* g_robot_state -> MISO 송신 buffer 인코딩 */
void spi_encode_feedback(uint8_t *tx_buffer);

#endif /* SPI_PROTOCOL_H */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/*
 * 사용자가 만지는 모든 매크로 한 곳.
 * 코드 로직 변경 없이 여기 값만 바꿔서 동작 조정.
 */

/* ===== 모드 선택 ===== */
#define IN_HAND_MODE        1       /* 1 = 손에 들고 (safety 비활성), 0 = 바닥 */

/*
 * 데모 모드 — compile-time 선택. 향후 state machine 진입 시 runtime 변수
 * (예: robot_mode_t current_mode)로 승격하고 main.c #if를 switch로 변환.
 */
#define MODE_STAND_ONLY     0       /* 일정 높이 정자세만 유지, trot 안 함 */
#define MODE_TROT           1       /* trot 보행 실행 */
#define MODE_TELEMETRY_TEST 2       /* telemetry 50Hz read 검증 */
#define MODE_JOINT_TEST     3       /* joint_control rad→raw 검증 */
#define MODE_CAL_MEASURE    4       /* 캘리브레이션 (토크 OFF + raw 출력) */
#define MODE_RL_CONTROL     6       /* Jetson RL 50Hz control loop */
#define DEMO_MODE           MODE_RL_CONTROL

/* ===== Body / IK 파라미터 ===== */
#define BODY_HEIGHT_MM      170.0f      /* default 정자세 몸체 높이 (mm) */
#define BODY_HEIGHT_MIN_MM   60.0f      /* knee 거의 다 접힘 한계 */
#define BODY_HEIGHT_MAX_MM  210.0f      /* leg 거의 다 펴짐 한계 */

#define DEFAULT_FOOT_X      -10.0f                  /* hip frame x — COM 균형 위해 살짝 뒤 */
#define DEFAULT_FOOT_Z      (-BODY_HEIGHT_MM)       /* foot_z = -height */

#define L1_THIGH_MM         105.0f
#define L2_SHIN_MM          130.0f

/* ===== Trot 파라미터 ===== */
#define GAIT_PERIOD_MS      1500        /* 한 cycle 시간 */
#define DUTY_FACTOR         0.55f       /* stance 비율 (0.5=pure trot, 0.55+=4-leg overlap) */
#define N_CYCLES            5
#define LOOP_PERIOD_MS      20          /* 50Hz */
#define MONITOR_PERIOD_MS   100         /* 10Hz 모니터 출력 (ENABLE_MONITOR=1일 때만) */

#define ENABLE_MONITOR      0           /* 1 = 모니터 활성 (run_trot 안 #if로 게이팅) */

#define STRIDE_X            80.0f       /* 발끝 전후 sweep 폭 (mm) */
#define LIFT_Z              10.0f       /* swing peak 높이 (대칭 arch + 4/3 보정으로 실제 peak = LIFT_Z) */

/* Safety 임계 — IN_HAND_MODE에 따라 자동 결정 */
#if IN_HAND_MODE
  #define MAX_PITCH_DEG     180.0f
  #define MAX_ROLL_DEG      180.0f
#else
  #define MAX_PITCH_DEG     35.0f
  #define MAX_ROLL_DEG      40.0f
#endif

/* ===== 모션 (transition / startup) ===== */
#define ZERO_POS                2048
#define HIP_OFFSET              0
#define SPEED_DEG_PER_SEC       30
#define STEP_MS                 25
#define MIN_TRANSITION_MS       100

#define COUNTDOWN_SEC          3
#define INITIAL_SETTLE_MS      2000
#define POLL_PERIOD_MS         10
#define ESC_KEY                0x1B

/* ===== Per-joint calibration (A4에서 실측 값으로 교체) ===== */
#define NUM_JOINTS_CFG         12
extern const uint16_t JOINT_ZERO_POS[NUM_JOINTS_CFG];
extern const int8_t   JOINT_SIGN[NUM_JOINTS_CFG];
extern const float    JOINT_MIN_RAD[NUM_JOINTS_CFG];
extern const float    JOINT_MAX_RAD[NUM_JOINTS_CFG];

#endif /* CONFIG_H */

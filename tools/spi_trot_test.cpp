/*
 * spi_trot_test.cpp — Jetson 단독 SPI trot 보행 테스트 (ROS2 불필요)
 * v3: DATA_READY GPIO 동기화 옵션 추가 — STM32 의 ready 신호 기반 100% sync
 *
 * 빌드 (Jetson):
 *   g++ -O2 -o spi_trot_test spi_trot_test.cpp -lm -lgpiod
 *
 * 실행:
 *   sudo ./spi_trot_test                            # 기존 relative timing
 *   sudo ./spi_trot_test --data-ready 0,105         # DATA_READY GPIO 폴링 동기화
 *
 * DATA_READY 핀 찾기 (Jetson 에서 한 번만):
 *   sudo apt install gpiod libgpiod-dev
 *   gpioinfo | grep -i "PR\|GPIO01"   # Pin 29 의 chip,line 확인
 *   예: "gpiochip0 line 144 PR.04" → --data-ready 0,144
 *
 * Ctrl+C → IDLE (torque OFF) 전송 후 종료
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sched.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

/* ─── 프로토콜 상수 ─── */
static constexpr uint16_t COMMAND_MAGIC  = 0xA55A;
static constexpr uint16_t FEEDBACK_MAGIC = 0x5AA5;
static constexpr int      NUM_JOINTS     = 12;
static constexpr int      SPI_FRAME_SIZE = 261;
static constexpr int      MOSI_PAYLOAD   = 116;

/* Wire mode (STM 펌웨어 spi_protocol.h 와 동기 — DISABLE/OPERATE 두 가지)
 * Bridge 가 ROS msg semantic mode (STAND/RL/CROUCH/...) 를 OPERATE 로 매핑함.
 * 이 test 도구는 직접 wire 값으로 보내므로 OPERATE 만 사용.
 */
static constexpr uint8_t MODE_DISABLE = 0;
static constexpr uint8_t MODE_OPERATE = 1;
static constexpr uint8_t FLAG_TORQUE_EN = 0x01;

/* ─── 로봇 파라미터 (firmware/Inc/config.h) ─── */
static constexpr float L1_MM = 105.0f;
static constexpr float L2_MM = 130.0f;
static constexpr float BODY_HEIGHT_MM = 170.0f;
static constexpr float DEFAULT_FOOT_X = -10.0f;
static constexpr float DEFAULT_FOOT_Z = -BODY_HEIGHT_MM;

/* Trot phase offset: FL, FR, RL, RR */
static const float TROT_PHASE_OFFSET[4] = {0.5f, 0.0f, 0.0f, 0.5f};

/* ─── Fault 코드 이름 (디버그용) ─── */
static const char* fault_name(uint8_t f) {
    switch (f) {
        case 0: return "OK";
        case 1: return "IMU_FAIL";
        case 2: return "SERVO_TMO";
        case 3: return "CRC_ERR";
        case 4: return "STALE";
        case 5: return "TEMP_HIGH";
        case 6: return "VOLT_LOW";
        case 7: return "SAFETY";
        case 8: return "NAN_TGT";
        default: return "???";
    }
}

/* ─── 글로벌 ─── */
static volatile sig_atomic_t g_stop = 0;
static int spi_fd = -1;

/* ─── DATA_READY GPIO (선택) ───
 * STM32 의 DATA_READY 핀이 HIGH 일 때만 SPI frame 을 보내서
 * STM32 의 body 처리 중 (DMA 안 armed) 시점에 frame 이 사라지는 문제 제거.
 * --data-ready CHIP,LINE 옵션으로 활성화. 비활성 시 기존 relative timing 그대로.
 */
static gpiod_chip* g_dr_chip   = nullptr;
static gpiod_line* g_dr_line   = nullptr;
static bool        g_dr_enable = false;
/* 5ms timeout: 50Hz 주기 (20ms) 의 1/4. STM32 의 cycle 이 wedge 됐다면
 * 더 기다려도 회수 가능성 낮음 → 빠르게 fallback send 가 낫다. */
static constexpr long DR_TIMEOUT_US = 5000;

static void signal_handler(int) { g_stop = 1; }

/* DATA_READY HIGH 까지 폴링. timeout 초과 시 false (그래도 호출자는 send 강행). */
static bool wait_data_ready_high()
{
    if (!g_dr_enable) return true;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    while (!g_stop) {
        int v = gpiod_line_get_value(g_dr_line);
        if (v == 1) return true;
        if (v < 0) {
            /* gpiod read error — 동기화 포기, send 강행 */
            return false;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long us = (now.tv_sec - t0.tv_sec) * 1000000L
                + (now.tv_nsec - t0.tv_nsec) / 1000L;
        if (us > DR_TIMEOUT_US) return false;

        /* 10us 단위 폴링 — busy spin 안 하면서도 latency 충분히 낮음 */
        struct timespec sl = {0, 10000};
        nanosleep(&sl, nullptr);
    }
    return false;
}

/* ═══════════════════════════════════
   시간 유틸
   ═══════════════════════════════════ */

static double now_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void sleep_until(double target)
{
    double remain = target - now_sec();
    if (remain > 0) {
        struct timespec ts;
        ts.tv_sec  = (time_t)remain;
        ts.tv_nsec = (long)((remain - ts.tv_sec) * 1e9);
        nanosleep(&ts, nullptr);
    }
}

static uint32_t timestamp_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL) & 0xFFFFFFFFULL);
}

/* ═══════════════════════════════════
   CRC-16 / CCITT-FALSE
   ═══════════════════════════════════ */

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

/* ═══════════════════════════════════
   SPI 초기화 / 전송
   ═══════════════════════════════════ */

static uint32_t g_spi_speed_hz = 2000000;   /* 기본 2MHz, --speed 로 변경 가능 */

static int spi_open(int bus, int dev, uint32_t speed_hz)
{
    char path[64];
    snprintf(path, sizeof(path), "/dev/spidev%d.%d", bus, dev);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return -1; }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz);

    g_spi_speed_hz = speed_hz;
    printf("SPI: %s @ %u Hz, mode %u\n", path, speed_hz, mode);
    return fd;
}

static void spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_ioc_transfer tr{};
    tr.tx_buf = (__u64)(uintptr_t)tx;
    tr.rx_buf = (__u64)(uintptr_t)rx;
    tr.len    = (uint32_t)len;
    tr.speed_hz = g_spi_speed_hz;
    tr.bits_per_word = 8;
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) perror("SPI_IOC_MESSAGE");
}

/* ═══════════════════════════════════
   패킷 빌드 / 파싱
   ═══════════════════════════════════ */

struct FeedbackPacket {
    uint16_t seq_echo;
    uint8_t  status;
    uint8_t  fault;
    uint8_t  motion_state;
    float    pos[12];
    float    gyro[3];
    float    quat[4];
    float    bus_voltage;
    float    gait_phase;
    uint32_t gait_cycle;
    bool     valid;
};

static void write_le16(uint8_t *p, uint16_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; }
static void write_le32(uint8_t *p, uint32_t v) {
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}
static void write_f32(uint8_t *p, float f) { uint32_t v; memcpy(&v,&f,4); write_le32(p,v); }
static uint16_t read_le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static float read_f32(const uint8_t *p) {
    uint32_t v = p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    float f; memcpy(&f,&v,4); return f;
}

static void build_mosi(uint8_t *buf, uint16_t seq, uint8_t mode, uint8_t flags,
                       const float target[12], const float delta[12],
                       float gait_phase, uint32_t gait_cycle)
{
    memset(buf, 0, SPI_FRAME_SIZE);
    int off = 0;
    write_le16(buf+off, COMMAND_MAGIC); off += 2;
    write_le16(buf+off, seq);           off += 2;
    write_le32(buf+off, timestamp_us());off += 4;
    buf[off++] = mode;
    buf[off++] = flags;
    for (int i = 0; i < 12; i++) { write_f32(buf+off, target[i]); off += 4; }
    for (int i = 0; i < 12; i++) { write_f32(buf+off, delta[i]);  off += 4; }
    write_f32(buf+off, gait_phase);     off += 4;
    write_le32(buf+off, gait_cycle);    off += 4;
    /* off == 114 */
    uint16_t crc = crc16_ccitt(buf, 114);
    write_le16(buf+114, crc);
}

static FeedbackPacket parse_miso(const uint8_t *buf)
{
    FeedbackPacket fb{};
    fb.valid = false;

    if (read_le16(buf) != FEEDBACK_MAGIC) return fb;
    uint16_t calc = crc16_ccitt(buf, 259);
    if (calc != read_le16(buf+259)) return fb;

    fb.valid = true;
    int off = 2;
    fb.seq_echo = read_le16(buf+off); off += 2;
    off += 4; /* timestamp */
    fb.status       = buf[off++];
    fb.fault        = buf[off++];
    fb.motion_state = buf[off++];
    fb.gait_phase = read_f32(buf+off); off += 4;
    fb.gait_cycle = read_le16(buf+off) | ((uint32_t)read_le16(buf+off+2)<<16); off += 4;
    off += 4; /* imu_yaw */
    for (int i = 0; i < 12; i++) { fb.pos[i]  = read_f32(buf+off); off += 4; }
    off += 48; /* velocity */
    off += 48; /* load */
    off += 48; /* temperature */
    for (int i = 0; i < 3; i++)  { fb.gyro[i] = read_f32(buf+off); off += 4; }
    off += 12; /* accel */
    for (int i = 0; i < 4; i++)  { fb.quat[i] = read_f32(buf+off); off += 4; }
    fb.bus_voltage = read_f32(buf+off);
    return fb;
}

/* ═══════════════════════════════════
   IK — firmware/Src/leg_ik.c 포팅
   ═══════════════════════════════════ */

static void ik_2link(float foot_x, float foot_z,
                     float &theta_t, float &theta_k)
{
    float r2 = foot_x * foot_x + foot_z * foot_z;
    float cos_k = (r2 - L1_MM*L1_MM - L2_MM*L2_MM) / (2.0f * L1_MM * L2_MM);
    if (cos_k >  1.0f) cos_k =  1.0f;
    if (cos_k < -1.0f) cos_k = -1.0f;
    float sin_k = sqrtf(1.0f - cos_k * cos_k);
    theta_k = atan2f(sin_k, cos_k);

    float A = L1_MM + L2_MM * cos_k;
    float B = L2_MM * sin_k;
    float det = A*A + B*B;
    float fz_neg = -foot_z;
    float sin_t = (A * foot_x - B * fz_neg) / det;
    float cos_t = (B * foot_x + A * fz_neg) / det;
    theta_t = atan2f(sin_t, cos_t);
}

/* ═══════════════════════════════════
   Gait — firmware/Src/gait.c 포팅
   ═══════════════════════════════════ */

static float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

static void compute_trot_offset(float leg_phase, float stride_x, float lift_z,
                                float duty, float &fx, float &fz)
{
    if (leg_phase < duty) {
        float s = leg_phase / duty;
        fx = stride_x * 0.5f - s * stride_x;
        fz = 0.0f;
        return;
    }
    float s = (leg_phase - duty) / (1.0f - duty);
    s = smoothstep(s);
    float oms = 1.0f - s;
    float B0 = oms*oms*oms, B1 = 3*oms*oms*s, B2 = 3*oms*s*s, B3 = s*s*s;
    float z_ctrl = (4.0f / 3.0f) * lift_z;
    fx = B0 * (-stride_x*0.5f) + B3 * (stride_x*0.5f);
    fz = B1 * z_ctrl + B2 * z_ctrl;
}

static void compute_targets(float phase, float stride_x, float lift_z,
                            float duty, float target[12])
{
    for (int leg = 0; leg < 4; leg++) {
        float lp = phase + TROT_PHASE_OFFSET[leg];
        if (lp >= 1.0f) lp -= 1.0f;

        float fx_off, fz_off;
        compute_trot_offset(lp, stride_x, lift_z, duty, fx_off, fz_off);

        float foot_x = DEFAULT_FOOT_X + fx_off;
        float foot_z = DEFAULT_FOOT_Z + fz_off;

        float theta_t, theta_k;
        ik_2link(foot_x, foot_z, theta_t, theta_k);

        target[leg*3 + 0] = 0.0f;     /* shoulder */
        target[leg*3 + 1] = theta_t;  /* thigh */
        target[leg*3 + 2] = theta_k;  /* knee */
    }
}

/* ═══════════════════════════════════
   메인
   ═══════════════════════════════════ */

int main(int argc, char **argv)
{
    /* ─── RT 친화 설정 (sudo 로 실행 시 자동 적용) ───
     * 매번 chrt 안 쳐도 sudo 로 실행만 하면 SCHED_FIFO + memory lock 됨.
     * 권한 없으면 (sudo X) 경고 후 일반 priority 로 계속 진행.
     */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "[warn] mlockall 실패 (sudo 권한 필요): %s\n", strerror(errno));
    }
    {
        struct sched_param sp = {};
        sp.sched_priority = 50;
        if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
            fprintf(stderr, "[warn] SCHED_FIFO 설정 실패 (sudo 권한 필요): %s\n", strerror(errno));
        } else {
            printf("[RT] SCHED_FIFO priority 50 + mlockall 적용\n");
        }
    }

    /* 기본값 */
    int   n_cycles  = 3;
    float stride_x  = 70.0f;
    float lift_z    = 13.0f;
    float period_s  = 1.5f;
    float duty      = 0.55f;
    int   spi_bus   = 0;
    int   spi_dev   = 0;
    bool  idle_test = false;     /* --idle-test: 모터 안 움직이고 SPI loss 측정 */
    int   dr_chip_id = -1;       /* --data-ready CHIP,LINE → gpiochipN */
    int   dr_line_id = -1;       /* --data-ready CHIP,LINE → line offset */

    /* 인자 파싱 */
    static struct option long_opts[] = {
        {"cycles",     required_argument, 0, 'c'},
        {"stride",     required_argument, 0, 's'},
        {"lift",       required_argument, 0, 'l'},
        {"period",     required_argument, 0, 'p'},
        {"duty",       required_argument, 0, 'd'},
        {"bus",        required_argument, 0, 'b'},
        {"dev",        required_argument, 0, 'D'},
        {"idle-test",  no_argument,       0, 'I'},
        {"data-ready", required_argument, 0, 'R'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "c:s:l:p:d:b:D:IR:h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c': n_cycles = atoi(optarg); break;
            case 's': stride_x = strtof(optarg, nullptr); break;
            case 'l': lift_z   = strtof(optarg, nullptr); break;
            case 'p': period_s = strtof(optarg, nullptr); break;
            case 'd': duty     = strtof(optarg, nullptr); break;
            case 'b': spi_bus  = atoi(optarg); break;
            case 'D': spi_dev  = atoi(optarg); break;
            case 'I': idle_test = true; break;
            case 'R':
                if (sscanf(optarg, "%d,%d", &dr_chip_id, &dr_line_id) != 2) {
                    fprintf(stderr, "[err] --data-ready 형식: CHIP,LINE (예: 0,144)\n");
                    return 1;
                }
                break;
            case 'h':
                printf("Usage: sudo %s [--cycles N] [--stride MM] [--lift MM] "
                       "[--period S] [--duty F] [--bus N] [--dev N] [--idle-test] "
                       "[--data-ready CHIP,LINE]\n"
                       "  --idle-test  : 모터 안 움직이고 IDLE 명령만 보내서 SPI loss 측정\n"
                       "  --data-ready : STM32 ready 신호 폴링 동기화\n"
                       "                 CHIP/LINE 은 `gpioinfo | grep PR.04` 등으로 확인\n",
                       argv[0]);
                return 0;
        }
    }

    /* 시그널 핸들러 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* SPI 열기 */
    spi_fd = spi_open(spi_bus, spi_dev, spi_speed_hz);
    if (spi_fd < 0) return 1;

    /* DATA_READY GPIO 열기 (옵션) */
    if (dr_chip_id >= 0 && dr_line_id >= 0) {
        char chip_path[32];
        snprintf(chip_path, sizeof(chip_path), "/dev/gpiochip%d", dr_chip_id);
        g_dr_chip = gpiod_chip_open(chip_path);
        if (!g_dr_chip) {
            fprintf(stderr, "[err] gpiod_chip_open(%s) 실패: %s\n",
                    chip_path, strerror(errno));
            close(spi_fd);
            return 1;
        }
        g_dr_line = gpiod_chip_get_line(g_dr_chip, dr_line_id);
        if (!g_dr_line) {
            fprintf(stderr, "[err] gpiod_chip_get_line(%d) 실패\n", dr_line_id);
            gpiod_chip_close(g_dr_chip);
            close(spi_fd);
            return 1;
        }
        /* input 으로 요청 — STM32 가 driver, Jetson 은 reader */
        if (gpiod_line_request_input(g_dr_line, "spi_trot_test") < 0) {
            fprintf(stderr, "[err] gpiod_line_request_input 실패: %s\n",
                    strerror(errno));
            gpiod_chip_close(g_dr_chip);
            close(spi_fd);
            return 1;
        }
        g_dr_enable = true;
        printf("[GPIO] DATA_READY enabled: gpiochip%d line %d (timeout %ldus)\n",
               dr_chip_id, dr_line_id, DR_TIMEOUT_US);
    } else {
        printf("[GPIO] DATA_READY disabled — relative timing only\n");
    }

    /* standing pose 계산 */
    float stand_target[12];
    compute_targets(0.0f, 0.0f, 0.0f, duty, stand_target);

    printf("\n=== SPI Trot Walking Test (C++ v2 — diag) ===\n");
    printf("  Cycles: %d  Period: %.1fs  Duty: %.2f\n", n_cycles, period_s, duty);
    printf("  Stride: %.0fmm  Lift: %.0fmm\n", stride_x, lift_z);
    printf("  Standing (thigh, knee): (%.3f, %.3f) rad\n",
           stand_target[1], stand_target[2]);
    printf("  All 12 standing targets: [");
    for (int i = 0; i < 12; i++) printf("%.3f%s", stand_target[i], i<11?", ":"");
    printf("]\n\n");

    /*
     * delta_smooth: standup/settle용 (smoothstep 보간 사용 → slew limit 사실상 무력화)
     * delta_walk:   trot용 (안전상 slew limit 유지)
     */
    float delta_smooth[12], delta_walk[12];
    for (int i = 0; i < 12; i++) {
        delta_smooth[i] = 10.0f;   /* effectively no limit */
        delta_walk[i]   = 0.08f;
    }

    uint8_t tx[SPI_FRAME_SIZE], rx[SPI_FRAME_SIZE];
    float   target[12] = {};
    uint16_t seq = 0;
    const double TICK = 0.02;  /* 50Hz */
    int dr_timeouts = 0;       /* DATA_READY HIGH 못 보고 fallback send 한 횟수 */

    auto send = [&](uint8_t mode, uint8_t flags, const float *tgt,
                    const float *dlt, float gph = 0, uint32_t gcyc = 0) -> FeedbackPacket {
        /* STM32 ready 신호 폴링 — g_dr_enable=false 면 즉시 true 반환 */
        if (!wait_data_ready_high()) dr_timeouts++;
        build_mosi(tx, seq, mode, flags, tgt, dlt, gph, gcyc);
        spi_transfer(spi_fd, tx, rx, SPI_FRAME_SIZE);
        seq = (seq + 1) & 0xFFFF;
        return parse_miso(rx);
    };

    auto send_idle = [&]() {
        float z[12] = {};
        return send(MODE_DISABLE, 0, z, z);
    };

    /*
     * 진단 데이터 버퍼 (제어 루프 안에서 printf 절대 금지)
     * Phase 2 끝나고 settle 끝나고 한 번에 출력
     */
    struct DiagSnap {
        int      tick, cycle;
        float    phase, fl_lp, fl_fx, fl_fz;
        float    tgt[3], fb_pos[3];
        uint8_t  fault, mst;
    };
    struct FaultEvent {
        int      tick;
        float    phase;
        uint8_t  from, to;
    };
    /*
     * Pause 진단 — 어떤 iteration 이 비정상적으로 길었는지 추적.
     *   send_ms : SPI ioctl 시간 (정상 1~2ms)
     *   sleep_ms: sleep_until 시간 (정상 ~18ms)
     *   iter_ms : 전체 iteration 시간 (정상 ~20ms)
     */
    struct PauseEvent {
        int    tick;
        double iter_ms;
        double send_ms;
        double sleep_ms;
    };
    static DiagSnap   snaps[64];     int snap_count = 0;
    static FaultEvent fevents[32];   int fevent_count = 0;
    static PauseEvent pauses[64];    int pause_count = 0;
    double max_iter_ms = 0;
    double max_send_ms = 0;
    double max_sleep_ms = 0;
    int    iter_over_30 = 0;     /* iter > 30ms 횟수 */
    int    send_over_5  = 0;     /* send > 5ms 횟수 */
    int    sleep_over_30 = 0;    /* sleep > 30ms 횟수 */

    int total_ticks = 0;
    int valid_ticks = 0;
    int safety_count = 0;
    int idle_count = 0;

    /* ── SPI 진단 (Phase 진입 전, 한 번만) ── */
    {
        printf("[Diag] SPI check... ");
        float zt[12] = {}, zd[12] = {};
        build_mosi(tx, 0xFFFF, MODE_DISABLE, 0, zt, zd, 0, 0);
        spi_transfer(spi_fd, tx, rx, SPI_FRAME_SIZE);
        printf("TX[0:4]=%02X %02X %02X %02X  RX[0:4]=%02X %02X %02X %02X\n",
               tx[0], tx[1], tx[2], tx[3],
               rx[0], rx[1], rx[2], rx[3]);
        FeedbackPacket diag = parse_miso(rx);
        if (diag.valid)
            printf("[Diag] MISO OK — seq=%u fault=%s(%u) Vbus=%.1fV\n",
                   diag.seq_echo, fault_name(diag.fault), diag.fault,
                   diag.bus_voltage);
        else
            printf("[Diag] MISO invalid (magic=0x%02X%02X) — STM32 연결 확인!\n",
                   rx[1], rx[0]);
    }
    printf("\n");

    /* ── Phase 0: 카운트다운 + 초기 자세 read ──
     *   IDLE 모드로 (torque OFF 유지) feedback 받아 현재 다리 자세 캡처.
     *   smooth standup의 from-pose로 사용.
     */
    printf(">>> 3초 후 시작 — 로봇을 잡을 준비! <<<\n");
    float initial_pose[12] = {0};
    bool got_initial = false;
    for (int i = 3; i > 0 && !g_stop; i--) {
        printf("  %d...\n", i);
        double t_end = now_sec() + 1.0;
        while (now_sec() < t_end && !g_stop) {
            FeedbackPacket fb = send_idle();
            if (fb.valid) {
                memcpy(initial_pose, fb.pos, sizeof(initial_pose));
                got_initial = true;
            }
            sleep_until(now_sec() + TICK);
        }
    }
    printf("\n");
    if (got_initial) {
        printf("[Init pose] FL=(%+.2f,%+.2f,%+.2f) FR=(%+.2f,%+.2f,%+.2f) "
               "RL=(%+.2f,%+.2f,%+.2f) RR=(%+.2f,%+.2f,%+.2f)\n\n",
               initial_pose[0], initial_pose[1], initial_pose[2],
               initial_pose[3], initial_pose[4], initial_pose[5],
               initial_pose[6], initial_pose[7], initial_pose[8],
               initial_pose[9], initial_pose[10], initial_pose[11]);
    } else {
        memcpy(initial_pose, stand_target, sizeof(initial_pose));
        printf("[Init pose] feedback 못 받음 — standing pose로 가정\n\n");
    }

    /* ── Phase 1: Smooth standup (smoothstep 보간) ──
     *   STM의 robot_transition과 동일한 방식.
     *   adaptive duration: max delta * 1초/rad, [1, 3]초 클램프
     *   loop 안에서 printf 없음 — 50Hz 정확히 유지
     *
     *   --idle-test 모드면 Phase 1 / 1.5 / 3 / 4 모두 skip (모터 안 움직임)
     */
    if (!g_stop && !idle_test) {
        float max_delta = 0.0f;
        for (int i = 0; i < 12; i++) {
            float d = fabsf(stand_target[i] - initial_pose[i]);
            if (d > max_delta) max_delta = d;
        }
        double dur = (double)max_delta;       /* 1 rad/s */
        if (dur < 1.0) dur = 1.0;
        if (dur > 3.0) dur = 3.0;
        int n_steps = (int)(dur / TICK);

        printf("[Phase 1] Smooth standup (%.1fs, max delta %.2f rad)\n", dur, max_delta);

        for (int t = 0; t <= n_steps && !g_stop; t++) {
            float r = (float)t / (float)n_steps;
            float s = r * r * (3.0f - 2.0f * r);   /* smoothstep */
            float interp[12];
            for (int i = 0; i < 12; i++) {
                interp[i] = initial_pose[i]
                          + (stand_target[i] - initial_pose[i]) * s;
            }
            send(MODE_OPERATE, FLAG_TORQUE_EN, interp, delta_smooth);
            sleep_until(now_sec() + TICK);
        }
    }

    /* ── Phase 1.5: Dwell at standing (1.5초) ──
     *   "자세 잡고 → 걷기 시작" 의 자연스러운 pause.
     *   STM 자체 trot 의 stand_at_height 후 잠시 대기와 동일한 효과.
     */
    if (!g_stop && !idle_test) {
        const double DWELL_BEFORE_TROT = 1.5;
        printf("[Phase 1.5] Hold standing (%.1fs)\n", DWELL_BEFORE_TROT);
        double t_end = now_sec() + DWELL_BEFORE_TROT;
        while (now_sec() < t_end && !g_stop) {
            send(MODE_OPERATE, FLAG_TORQUE_EN, stand_target, delta_smooth);
            sleep_until(now_sec() + TICK);
        }
    }

    /* ── Phase 2: Trot walking ──
     *   loop 안 printf 절대 없음. 진단 데이터는 메모리 버퍼에만 저장.
     *   Phase 1 → 2 사이도 printf 없으므로 STM32 stale 안 일어남.
     *
     *   --idle-test 모드면 trot 안 하고 IDLE 명령 N*period 초 동안 송신.
     */
    if (!g_stop) {
        double total_walk = n_cycles * period_s;
        if (idle_test) {
            printf("[Phase 2] IDLE-only SPI test — %.1fs (모터 안 움직임)\n", total_walk);
        } else {
            printf("[Phase 2] Trot walking — %d cycles (%.1fs)\n", n_cycles, total_walk);
        }

        double t_walk_start = now_sec();
        int tick_count = 0;
        uint8_t last_fault = 255;

        while (!g_stop) {
            double t_iter_start = now_sec();    /* === pause 진단 시작 === */
            double elapsed = t_iter_start - t_walk_start;
            if (elapsed >= total_walk) break;

            float phase = fmodf((float)elapsed, period_s) / period_s;
            int cycle = (int)(elapsed / period_s);

            compute_targets(phase, stride_x, lift_z, duty, target);

            /* --idle-test: 모터 안 움직이고 IDLE 명령만 보냄 (순수 SPI 검증) */
            float zero_target[12] = {0};
            float zero_delta[12]  = {0};
            FeedbackPacket fb;
            double t_before_send = now_sec();
            if (idle_test) {
                fb = send(MODE_DISABLE, 0, zero_target, zero_delta, 0.0f, 0);
            } else {
                fb = send(MODE_OPERATE, FLAG_TORQUE_EN,
                          target, delta_walk, phase, (uint32_t)cycle);
            }
            double send_ms = (now_sec() - t_before_send) * 1000.0;
            if (send_ms > max_send_ms) max_send_ms = send_ms;
            if (send_ms > 5.0) send_over_5++;

            if (fb.valid) {
                valid_ticks++;
                if (fb.fault == 7) safety_count++;
                if (fb.motion_state == 0) idle_count++;

                if (fb.fault != last_fault && fevent_count < 32) {
                    fevents[fevent_count++] = {tick_count, phase, last_fault, fb.fault};
                    last_fault = fb.fault;
                }

                if (tick_count % 10 == 0 && snap_count < 64) {
                    float fl_lp = phase + TROT_PHASE_OFFSET[0];
                    if (fl_lp >= 1.0f) fl_lp -= 1.0f;
                    float fl_fx, fl_fz;
                    compute_trot_offset(fl_lp, stride_x, lift_z, duty, fl_fx, fl_fz);

                    DiagSnap& s = snaps[snap_count++];
                    s.tick = tick_count;
                    s.cycle = cycle + 1;
                    s.phase = phase;
                    s.fl_lp = fl_lp;
                    s.fl_fx = DEFAULT_FOOT_X + fl_fx;
                    s.fl_fz = DEFAULT_FOOT_Z + fl_fz;
                    s.tgt[0] = target[0]; s.tgt[1] = target[1]; s.tgt[2] = target[2];
                    s.fb_pos[0] = fb.pos[0]; s.fb_pos[1] = fb.pos[1]; s.fb_pos[2] = fb.pos[2];
                    s.fault = fb.fault;
                    s.mst   = fb.motion_state;
                }
            }

            tick_count++;
            double t_before_sleep = now_sec();
            /*
             * RELATIVE timing (Phase 5 와 동일):
             * 절대 시간 기준 catch-up 안 함. Jetson 송신 주기가 STM32 의 자연 주기
             * (~47Hz, body 13~17ms + delay 4~7ms) 와 자연스럽게 sync.
             *
             * 이전 (absolute timing) 은 Jetson 을 정확 50Hz 로 강제해서 STM32 47Hz
             * 와 3Hz beat → 매 1/3 초마다 Jetson frame 이 STM32 의 body 처리 중
             * (DMA 안 armed) 시점에 도착해서 frame 사라짐.
             */
            sleep_until(t_before_sleep + TICK);
            double sleep_ms = (now_sec() - t_before_sleep) * 1000.0;
            if (sleep_ms > max_sleep_ms) max_sleep_ms = sleep_ms;
            if (sleep_ms > 30.0) sleep_over_30++;

            /* === Pause 진단: 비정상적으로 긴 iteration 기록 === */
            double iter_ms = (now_sec() - t_iter_start) * 1000.0;
            if (iter_ms > max_iter_ms) max_iter_ms = iter_ms;
            if (iter_ms > 30.0) {
                iter_over_30++;
                if (pause_count < 64) {
                    PauseEvent& p = pauses[pause_count++];
                    p.tick = tick_count - 1;
                    p.iter_ms = iter_ms;
                    p.send_ms = send_ms;
                    p.sleep_ms = sleep_ms;
                }
            }
        }
        total_ticks = tick_count;
    }

    /* save last trot target → settle interp의 from-pose */
    float last_target[12];
    memcpy(last_target, target, sizeof(last_target));

    /* ── Phase 3: Smooth settle back to standing ──
     *   trot 마지막 자세에서 standing으로 부드럽게 보간.
     *   loop 안 printf 없음.
     */
    if (!g_stop && !idle_test) {
        float max_delta = 0.0f;
        for (int i = 0; i < 12; i++) {
            float d = fabsf(stand_target[i] - last_target[i]);
            if (d > max_delta) max_delta = d;
        }
        double dur = (double)max_delta;
        if (dur < 1.0) dur = 1.0;
        if (dur > 3.0) dur = 3.0;
        int n_steps = (int)(dur / TICK);

        printf("[Phase 3] Smooth settle to standing (%.1fs)\n", dur);

        for (int t = 0; t <= n_steps && !g_stop; t++) {
            float r = (float)t / (float)n_steps;
            float s = r * r * (3.0f - 2.0f * r);
            float interp[12];
            for (int i = 0; i < 12; i++) {
                interp[i] = last_target[i]
                          + (stand_target[i] - last_target[i]) * s;
            }
            send(MODE_OPERATE, FLAG_TORQUE_EN, interp, delta_smooth);
            sleep_until(now_sec() + TICK);
        }
    }

    /* ── Phase 4: Smooth return to initial pose ──
     *   Standing → 처음 시작했던 자세 (Phase 0에서 read한 initial_pose)
     *   "올라간 만큼 다시 천천히 내려간다" — STM 자체 trot 마지막 흐름과 동일.
     */
    if (!g_stop && got_initial && !idle_test) {
        float max_delta = 0.0f;
        for (int i = 0; i < 12; i++) {
            float d = fabsf(initial_pose[i] - stand_target[i]);
            if (d > max_delta) max_delta = d;
        }
        double dur = (double)max_delta;
        if (dur < 1.0) dur = 1.0;
        if (dur > 3.0) dur = 3.0;
        int n_steps = (int)(dur / TICK);

        printf("[Phase 4] Smooth return to initial pose (%.1fs)\n", dur);

        for (int t = 0; t <= n_steps && !g_stop; t++) {
            float r = (float)t / (float)n_steps;
            float s = r * r * (3.0f - 2.0f * r);
            float interp[12];
            for (int i = 0; i < 12; i++) {
                interp[i] = stand_target[i]
                          + (initial_pose[i] - stand_target[i]) * s;
            }
            send(MODE_OPERATE, FLAG_TORQUE_EN, interp, delta_smooth);
            sleep_until(now_sec() + TICK);
        }
    }

    /* ── 진단 출력 (제어 루프 모두 끝난 후) ── */
    printf("\n══ Phase 2 진단 결과 ══\n");
    printf("  총 tick: %d (valid: %d, %.0f%% loss)\n",
           total_ticks, valid_ticks,
           total_ticks > 0 ? 100.0*(total_ticks-valid_ticks)/total_ticks : 0.0);
    printf("  SAFETY(7) tick: %d / %d (%.0f%%)\n",
           safety_count, valid_ticks,
           valid_ticks > 0 ? 100.0*safety_count/valid_ticks : 0.0);
    printf("  IDLE mst tick: %d / %d (%.0f%%)\n",
           idle_count, valid_ticks,
           valid_ticks > 0 ? 100.0*idle_count/valid_ticks : 0.0);

    /* === Pause 진단 결과 === */
    printf("\n  ── Pause 진단 ──\n");
    printf("  Loop iteration 시간:\n");
    printf("    max iter:  %.1fms (정상 ~20ms)\n", max_iter_ms);
    printf("    max send:  %.1fms (정상 1~2ms)  →  큼이면 SPI ioctl block\n", max_send_ms);
    printf("    max sleep: %.1fms (정상 ~18ms)  →  큼이면 scheduler 지연\n", max_sleep_ms);
    printf("  비정상 카운트:\n");
    printf("    iter  > 30ms: %d 회\n", iter_over_30);
    printf("    send  >  5ms: %d 회\n", send_over_5);
    printf("    sleep > 30ms: %d 회\n", sleep_over_30);
    if (g_dr_enable) {
        printf("    DATA_READY timeout: %d 회 / %d ticks (%.1f%%)\n",
               dr_timeouts, total_ticks,
               total_ticks > 0 ? 100.0 * dr_timeouts / total_ticks : 0.0);
    }
    if (pause_count > 0) {
        printf("  상세 (iter > 30ms 인 iteration, 최대 %d개):\n",
               (int)(sizeof(pauses)/sizeof(pauses[0])));
        for (int i = 0; i < pause_count; i++) {
            PauseEvent& p = pauses[i];
            const char* cause = "?";
            if (p.send_ms > 10.0) cause = "SPI ioctl block";
            else if (p.sleep_ms > 30.0) cause = "scheduler 지연";
            else cause = "기타 (body 처리 느림)";
            printf("    tick=%d iter=%.1fms send=%.1fms sleep=%.1fms → %s\n",
                   p.tick, p.iter_ms, p.send_ms, p.sleep_ms, cause);
        }
    }
    printf("\n");

    if (fevent_count > 0) {
        printf("  fault transitions:\n");
        for (int i = 0; i < fevent_count; i++) {
            printf("    [tick=%d ph=%.2f] %s(%u) -> %s(%u)\n",
                   fevents[i].tick, fevents[i].phase,
                   fault_name(fevents[i].from), fevents[i].from,
                   fault_name(fevents[i].to), fevents[i].to);
        }
    }
    if (snap_count > 0) {
        printf("  snapshots (every 10 tick):\n");
        for (int i = 0; i < snap_count; i++) {
            DiagSnap& s = snaps[i];
            printf("    [c%d ph=%.2f] FL(lp=%.2f foot=%+.0f,%+.0f) "
                   "tgt=(%+.3f,%+.3f,%+.3f) fb=(%+.3f,%+.3f,%+.3f) f=%s mst=%u\n",
                   s.cycle, s.phase, s.fl_lp, s.fl_fx, s.fl_fz,
                   s.tgt[0], s.tgt[1], s.tgt[2],
                   s.fb_pos[0], s.fb_pos[1], s.fb_pos[2],
                   fault_name(s.fault), s.mst);
        }
    }
    printf("\n");

    /* ── Phase 5: HOLD at initial pose until Ctrl+C ──
     *   처음 자세 유지 (이미 낮게 내려와 있어서 토크 풀어도 위험 없음).
     *   자동으로 토크 OFF 안 함 — 사용자가 결정.
     *   진단 출력 동안 STM32가 잠시 stale → HOLD가 돼도, 어차피 같은 자세 유지라 무관.
     */
    if (!g_stop) {
        printf("[Phase 5] Holding initial pose. Press Ctrl+C to release torque.\n");
        while (!g_stop) {
            send(MODE_OPERATE, FLAG_TORQUE_EN, initial_pose, delta_smooth);
            sleep_until(now_sec() + TICK);
        }
    }

    /* ── Ctrl+C 받음 → IDLE (안전한 토크 OFF) ── */
    printf("\n토크 OFF (IDLE) 전송 중...\n");
    for (int i = 0; i < 25; i++) {
        send_idle();
        sleep_until(now_sec() + TICK);
    }

    close(spi_fd);

    /* DATA_READY GPIO 정리 — line release 안 하면 다음 실행에서 EBUSY */
    if (g_dr_line) gpiod_line_release(g_dr_line);
    if (g_dr_chip) gpiod_chip_close(g_dr_chip);

    printf("\n=== %s ===\n", g_stop ? "Ctrl+C: 종료" : "Trot test complete!");
    return 0;
}

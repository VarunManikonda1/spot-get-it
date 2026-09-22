/**
 * bridge_main.c — BridgeDaemon 메인
 *
 * 스레드 구성 (로봇 N대 기준):
 *   main/watchdog      ★★     (1개)
 *   jetson_rx          ★★★★   (1개, 코어2 고정)
 *   reassembly_shm     ★★★    (N개, OS 스케줄)
 *   jetson_tx          ★★★★★  (1개, OS 스케줄)
 *   protocol_timer     ★★★★★  (1개, OS 스케줄)
 *   pc_link            ★★★    (1개, OS 스케줄)
 *
 * 실행 인자:
 *   ./bridge_app [num_robots]   기본값: 1, 최대: MAX_ROBOTS
 */

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#include "shm_def.h"
#include "frag_index_queue.h"
#include "rx_packet_pool.h"
#include "proto.h"
#include "bridge_ctx.h"
#include "bridge_api.h"
#include "shm_cmd_queue.h"

/* ─── 외부 스레드 함수 선언 ─────────────────────────────────── */
extern void *jetson_rx_thread(void *arg);
extern void *jetson_tx_thread(void *arg);
extern void *reassembly_shm_thread(void *arg);
extern void *protocol_timer_thread(void *arg);
extern void *pc_link_thread(void *arg);

/* ─── 전역 컨텍스트 ─────────────────────────────────────────── */
typedef struct {
    int              num_robots;
    SharedData      *shm[MAX_ROBOTS];
    FragIndexQueue   fq[MAX_ROBOTS];
    RxPacketPool     rx_pool;
    atomic_bool      stop;

    JetsonAddrTable  addr_table;
    PcCommandPeer    pc_peer;
    BridgeApi        api;
    JetsonRxCtx      rx_ctx;
    JetsonTxCtx      tx_ctx;
    ProtoTimerCtx    timer_ctx;
    PcLinkCtx        pc_ctx;
    ReasmCtx         reasm_ctx[MAX_ROBOTS];

    pthread_t        rx_tid;
    pthread_t        tx_tid;
    pthread_t        timer_tid;
    pthread_t        pc_tid;
    pthread_t        reasm_tid[MAX_ROBOTS];
} BridgeCtx;

static BridgeCtx g_ctx;

/* ─── SHM 초기화 (로봇 1대분) ──────────────────────────────── */
static SharedData *shm_init_one(int robot_id) {
    char name[32];
    snprintf(name, sizeof(name), SHM_NAME_FMT, robot_id);
    // SHM_NAME_FMT( /robot_bridge_%d" shm_def.h에 정의됨)에 %d 부분에 robot_id를 넣어서 shm 파일 이름("name") 생성
    

    shm_unlink(name);
    // name이라는 공유메모리를 제거 
    // 만들기전에, 혹시 이전에 공유메모리를 제거하지 못 했을 경우를 대비

    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    // name이라는 공유메모리를 읽기/쓰기 모드로 생성하고, fd라는 파일 디스크립터에 저장

    if (fd < 0) { perror("shm_open"); return NULL; }
    // fd가 0보다 작은 경우, shm_open이 실패한 것이므로 에러 메시지를 출력하고 NULL 반환
    
    if (ftruncate(fd, sizeof(SharedData)) < 0) {
        perror("ftruncate"); close(fd); shm_unlink(name); return NULL;
    }
    //ftruncate : 공유 메모리 크기를 SharedData(shm_def에 정의됨) 구조체 크기( ~600kb)로 확장 하는 함수 
    // 리턴값이 0인 경우 실패임으로 종료

    SharedData *shm = mmap(NULL, sizeof(SharedData),PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // fd의 크기만큼 공유 메모리를  프로세스 공간에 직접 매핑해주는 함수.
    // sizeof(SharedData) 이 공간만큼 매핑
    // PROT_READ | PROT_WRITE : 읽기/쓰기 모두 허용
    // MAP_SHARED : 여러 프로세스가 이 메모리를 공유할 수 있도록 설정
    

    close(fd);
    // fd는 더 이상 필요 없으므로 닫음. 
    //공유 메모리는 이미 프로세스 주소 공간에 매핑되어 있고 shm으로 접근할 수 있기 때문에 fd는 필요 없음.
    //fd를 닫아도 매핑된 메모리는 유지됨
    // fd는 파일 디스크립터로서, 공유 메모리를 가리키는 역할을 했지만, 이제는 매핑된 메모리를 직접 사용할 것이므로 fd는 필요 없음
    // 공유메모리를 정리 해야 할 경우 shm_unlink(name)을 통해 삭제할 수 있음



    if (shm == MAP_FAILED) {
        perror("mmap"); shm_unlink(name); return NULL;
    }

    memset(shm, 0, sizeof(SharedData));
    //최초 생성시 메모리 청소

    atomic_store(&shm->img_ready_idx,   -1);
    //SharedData의 ATOMIC_INT     img_ready_idx;  부분을   -1로 초기화 합니다(사용하지 않음을 나타내기 위해), 
    //atomic_store는 원자적 연산으로, 여러 스레드가 동시에 접근하더라도 안전하게 값을 저장할 수 있도록 보장합니다.
    atomic_store(&shm->lidar_ready_idx, -1);
    //SharedData의 lidar_ready_idx 부분을  -1로 초기화 합니다(사용하지 않음을 나타내기 위해)
    /*
    왜 atomic_store를 사용할까요?
    atomic_store는 cpu가 다른 메모리 버스에서 이 변수에 대한 쓰기 연산이 완전히 끝날 때까지 다른 스레드가 이 변수의 값을 읽지 못하도록 보장합니다.
    
    슬롯에서 트리플 버퍼링을 사용할 때, img_ready_idx와 lidar_ready_idx는 최신 데이터가 저장된 슬롯의 인덱스를 나타냅니다.
    이 변수들은 여러 스레드에서 동시에 읽고 쓰일 수 있기 때문에, atomic 연산을 사용하여 일관된 상태를 유지하는 것이 중요합니다.
    예를 들어, 한 스레드가 img_ready_idx를 업데이트하는 동안, 다른 스레드가 이 값을 읽으려고 할 때, atomic_store를 사용하면,
    읽는 스레드는 항상 일관된 값을 보게 됩니다. 

    */

    if (sem_init(&shm->img_sem, 1, 0) < 0) {
        perror("sem_init img");
        munmap(shm, sizeof(SharedData)); shm_unlink(name); return NULL;
    }

    if (sem_init(&shm->lidar_sem, 1, 0) < 0) {
        perror("sem_init lidar");
        sem_destroy(&shm->img_sem);
        munmap(shm, sizeof(SharedData)); shm_unlink(name); return NULL;
    }

    pthread_rwlockattr_t rw_attr;
    pthread_rwlockattr_init(&rw_attr);
    pthread_rwlockattr_setpshared(&rw_attr, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&shm->odom_lock, &rw_attr);
    pthread_rwlock_init(&shm->meta_lock, &rw_attr);
    pthread_rwlock_init(&shm->state_lock, &rw_attr);
    pthread_rwlock_init(&shm->path_lock, &rw_attr);
    pthread_rwlockattr_destroy(&rw_attr);

    pthread_mutexattr_t cmd_mu_attr;
    pthread_mutexattr_init(&cmd_mu_attr);
    pthread_mutexattr_setpshared(&cmd_mu_attr, PTHREAD_PROCESS_SHARED);
    shm_cmd_queue_init(&shm->cmd_queue, &cmd_mu_attr);
    pthread_mutexattr_destroy(&cmd_mu_attr);

    bridge_api_init_shm(shm, robot_id);

    fprintf(stderr, "[main] SHM 초기화: %s (%zu KB)\n",
            name, sizeof(SharedData) / 1024);
    return shm;
}

/* ─── SHM 정리 (로봇 1대분) ─────────────────────────────────── */
static void shm_cleanup_one(SharedData *shm, int robot_id) {
    if (!shm) return;
    sem_destroy(&shm->img_sem);
    sem_destroy(&shm->lidar_sem);
    pthread_rwlock_destroy(&shm->odom_lock);
    pthread_rwlock_destroy(&shm->meta_lock);
    pthread_rwlock_destroy(&shm->state_lock);
    pthread_rwlock_destroy(&shm->path_lock);
    shm_cmd_queue_destroy(&shm->cmd_queue);
    munmap(shm, sizeof(SharedData));
    char name[32];
    snprintf(name, sizeof(name), SHM_NAME_FMT, robot_id);
    shm_unlink(name);
}

/* ─── 전체 자원 정리 ────────────────────────────────────────── */
static void cleanup_all(int num_robots) {
    for (int i = 0; i < num_robots; i++) {
        frag_index_queue_destroy(&g_ctx.fq[i]);
        shm_cleanup_one(g_ctx.shm[i], i);
    }
    rx_packet_pool_destroy(&g_ctx.rx_pool);
    pthread_mutex_destroy(&g_ctx.addr_table.mu);
    pthread_mutex_destroy(&g_ctx.pc_peer.mu);
    bridge_api_destroy(&g_ctx.api);
}

/* ─── 스레드 생성 헬퍼 ──────────────────────────────────────── */
static pthread_t create_thread(void *(*fn)(void *), void *arg,
                                int priority, int core) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if (core >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
    }
    if (priority > 0) {
        struct sched_param param = { .sched_priority = priority };
        pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
        pthread_attr_setschedparam(&attr, &param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    pthread_t tid;
    int rc = pthread_create(&tid, &attr, fn, arg);
    if (rc != 0) {
        fprintf(stderr,
                "[main] pthread_create realtime/affinity failed: %s; retry normal\n",
                strerror(rc));
        pthread_attr_destroy(&attr);
        pthread_attr_init(&attr);
        rc = pthread_create(&tid, &attr, fn, arg);
    }
    if (rc != 0) {
        fprintf(stderr, "[main] pthread_create failed: %s\n", strerror(rc));
        tid = 0;
    }
    pthread_attr_destroy(&attr);
    return tid;
}

/* ─── 시그널 핸들러 ─────────────────────────────────────────── */
static void sig_handler(int sig) {
    (void)sig;
    atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
}

static void request_stop_all(void) {
    atomic_store_explicit(&g_ctx.stop, true, memory_order_release);
    for (int i = 0; i < g_ctx.num_robots; i++)
        frag_index_queue_stop(&g_ctx.fq[i]);
}

/* ─── watchdog ──────────────────────────────────────────────── */
static void watchdog_loop(void) {
    int prev_health_pkt_count[MAX_ROBOTS] = {0};
    int no_health_pkt_sec[MAX_ROBOTS]     = {0};

    while (!atomic_load_explicit(&g_ctx.stop, memory_order_acquire)) {
        sleep(1);
        if (atomic_load_explicit(&g_ctx.stop, memory_order_acquire)) break;
        for (int i = 0; i < g_ctx.num_robots; i++) {
            SharedData *shm = g_ctx.shm[i];

            uint32_t img_drop, lidar_drop;
            pthread_rwlock_rdlock(&shm->meta_lock);
            img_drop   = shm->meta.img_drop_count;
            lidar_drop = shm->meta.lidar_drop_count;
            pthread_rwlock_unlock(&shm->meta_lock);

            uint8_t connected = atomic_load(&shm->meta.jetson_connected);

            /* ODOM 또는 PATH_PROGRESS 수신 여부로 연결 판단 */
            int cur_health_pkt = atomic_load(&shm->meta.pkt_count);
            if (cur_health_pkt == prev_health_pkt_count[i]) {
                if (++no_health_pkt_sec[i] >= 2)
                    atomic_store(&shm->meta.jetson_connected, 0);
            } else {
                no_health_pkt_sec[i] = 0;
            }
            prev_health_pkt_count[i] = cur_health_pkt;
            if (!atomic_load(&shm->meta.jetson_connected) && connected) {
                pthread_rwlock_wrlock(&shm->state_lock);
                shm->state.seq++;
                shm->state.connected = 0;
                pthread_rwlock_unlock(&shm->state_lock);
                bridge_api_publish_event(&g_ctx.api, (uint8_t)i,
                    EVENT_SEVERITY_WARN, EVENT_TYPE_DISCONNECTED, 0,
                    "robot disconnected");
            }

            fprintf(stderr,
                "[watchdog] robot=%d connected=%d img_drop=%u lidar_drop=%u\n",
                i, (int)connected, img_drop, lidar_drop);
        }
    }
}

/* ─── main ───────────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    umask(0);
    int num_robots = (argc >= 2) ? atoi(argv[1]) : 1;
    if (num_robots < 1 || num_robots > MAX_ROBOTS) {
        fprintf(stderr, "[main] 유효하지 않은 로봇 수: %d (1~%d)\n",
                num_robots, MAX_ROBOTS);
        return 1;
    }
    g_ctx.num_robots = num_robots;
    atomic_init(&g_ctx.stop, false);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* addr_table 초기화 */
    memset(&g_ctx.addr_table, 0, sizeof(g_ctx.addr_table));
    pthread_mutex_init(&g_ctx.addr_table.mu, NULL);
    memset(&g_ctx.pc_peer, 0, sizeof(g_ctx.pc_peer));
    g_ctx.pc_peer.fd = -1;
    pthread_mutex_init(&g_ctx.pc_peer.mu, NULL);
    rx_packet_pool_init(&g_ctx.rx_pool);

    /* SHM + FragQueue 초기화 */
    for (int i = 0; i < num_robots; i++) {
        g_ctx.shm[i] = shm_init_one(i);
        if (!g_ctx.shm[i]) {
            for (int j = 0; j < i; j++) {
                frag_index_queue_destroy(&g_ctx.fq[j]);
                shm_cleanup_one(g_ctx.shm[j], j);
            }
            rx_packet_pool_destroy(&g_ctx.rx_pool);
            pthread_mutex_destroy(&g_ctx.addr_table.mu);
            pthread_mutex_destroy(&g_ctx.pc_peer.mu);
            return 1;
        }
        frag_index_queue_init(&g_ctx.fq[i]);
    }

    bridge_api_init(&g_ctx.api, &g_ctx.addr_table, g_ctx.shm, num_robots);

    /* 컨텍스트 구성 */
    g_ctx.rx_ctx.num_robots = num_robots;
    g_ctx.rx_ctx.stop       = &g_ctx.stop;
    g_ctx.rx_ctx.addr_table = &g_ctx.addr_table;
    g_ctx.rx_ctx.pc_peer    = &g_ctx.pc_peer;
    g_ctx.rx_ctx.api        = &g_ctx.api;
    g_ctx.rx_ctx.rx_pool    = &g_ctx.rx_pool;
    for (int i = 0; i < num_robots; i++) {
        g_ctx.rx_ctx.shm_arr[i] = g_ctx.shm[i];
        g_ctx.rx_ctx.fq_arr[i]  = &g_ctx.fq[i];
    }

    g_ctx.tx_ctx.addr_table = &g_ctx.addr_table;
    g_ctx.tx_ctx.pc_peer    = &g_ctx.pc_peer;
    g_ctx.tx_ctx.num_robots = num_robots;
    g_ctx.tx_ctx.stop       = &g_ctx.stop;
    g_ctx.tx_ctx.api        = &g_ctx.api;
    for (int i = 0; i < num_robots; i++)
        g_ctx.tx_ctx.shm_arr[i] = g_ctx.shm[i];

    g_ctx.timer_ctx.addr_table = &g_ctx.addr_table;
    g_ctx.timer_ctx.num_robots = num_robots;
    g_ctx.timer_ctx.stop       = &g_ctx.stop;
    g_ctx.timer_ctx.api        = &g_ctx.api;

    g_ctx.pc_ctx.addr_table = &g_ctx.addr_table;
    g_ctx.pc_ctx.pc_peer    = &g_ctx.pc_peer;
    g_ctx.pc_ctx.num_robots = num_robots;
    g_ctx.pc_ctx.stop       = &g_ctx.stop;
    g_ctx.pc_ctx.api        = &g_ctx.api;
    for (int i = 0; i < num_robots; i++)
        g_ctx.pc_ctx.shm_arr[i] = g_ctx.shm[i];

    for (int i = 0; i < num_robots; i++) {
        g_ctx.reasm_ctx[i].shm = g_ctx.shm[i];
        g_ctx.reasm_ctx[i].fq  = &g_ctx.fq[i];
        g_ctx.reasm_ctx[i].rx_pool = &g_ctx.rx_pool;
        g_ctx.reasm_ctx[i].api = &g_ctx.api;
        g_ctx.reasm_ctx[i].robot_id = (uint8_t)i;
    }

    /* 스레드 생성 */
    g_ctx.rx_tid = create_thread(jetson_rx_thread, &g_ctx.rx_ctx, 60, 2);
    if (!g_ctx.rx_tid) { cleanup_all(num_robots); return 1; }

    g_ctx.tx_tid = create_thread(jetson_tx_thread, &g_ctx.tx_ctx, 80, -1);
    if (!g_ctx.tx_tid) {
        request_stop_all();
        pthread_join(g_ctx.rx_tid, NULL);
        cleanup_all(num_robots); return 1;
    }

    g_ctx.timer_tid = create_thread(protocol_timer_thread,
                                    &g_ctx.timer_ctx, 80, -1);
    if (!g_ctx.timer_tid) {
        request_stop_all();
        pthread_join(g_ctx.rx_tid, NULL);
        pthread_join(g_ctx.tx_tid, NULL);
        cleanup_all(num_robots); return 1;
    }

    g_ctx.pc_tid = create_thread(pc_link_thread, &g_ctx.pc_ctx, 40, -1);
    if (!g_ctx.pc_tid) {
        request_stop_all();
        pthread_join(g_ctx.rx_tid, NULL);
        pthread_join(g_ctx.tx_tid, NULL);
        pthread_join(g_ctx.timer_tid, NULL);
        cleanup_all(num_robots); return 1;
    }

    for (int i = 0; i < num_robots; i++) {
        g_ctx.reasm_tid[i] = create_thread(reassembly_shm_thread,
                                           &g_ctx.reasm_ctx[i], 0, -1);
        if (!g_ctx.reasm_tid[i]) {
            request_stop_all();
            pthread_join(g_ctx.rx_tid, NULL);
            pthread_join(g_ctx.tx_tid, NULL);
            pthread_join(g_ctx.timer_tid, NULL);
            pthread_join(g_ctx.pc_tid, NULL);
            for (int j = 0; j < i; j++) pthread_join(g_ctx.reasm_tid[j], NULL);
            cleanup_all(num_robots); return 1;
        }
    }

    fprintf(stderr,
        "[main] BridgeDaemon 시작 (PID=%d, robots=%d)\n"
        "[main] 수신 포트: %d | 커맨드 포트: %d | PC 포트: %d\n"
        "[main] Qt 명령 IPC: SHM cmd_queue (/robot_bridge_N)\n",
        getpid(), num_robots,
        BRIDGE_PORT, JETSON_CMD_PORT, PC_LINK_PORT);

    watchdog_loop();
    request_stop_all();

    pthread_join(g_ctx.rx_tid,    NULL);
    pthread_join(g_ctx.tx_tid,    NULL);
    pthread_join(g_ctx.timer_tid, NULL);
    pthread_join(g_ctx.pc_tid,    NULL);
    for (int i = 0; i < num_robots; i++)
        pthread_join(g_ctx.reasm_tid[i], NULL);

    cleanup_all(num_robots);
    fprintf(stderr, "[main] BridgeDaemon 완전 종료\n");
    return 0;
}

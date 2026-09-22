/**
 * reassembly_shm.c - index-based fragment reassembly to SHM.
 *
 * Image/LiDAR fragments stay in RxPacketPool slots until a frame is complete.
 * Reassembly stores only frag_idx -> slot_id mappings. Image commit copies
 * pool payloads directly into the final SHM image slot. LiDAR keeps the
 * existing SHM SoA layout, so the first indexed version uses a staging buffer
 * during commit and then writes x/y/z/intensity arrays.
 */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proto.h"
#include "shm_def.h"
#include "frag_index_queue.h"
#include "rx_packet_pool.h"
#include "bridge_ctx.h"
#include "bridge_api.h"
#include "utils.h"

#define REASM_SLOTS       8
#define REASM_MAX_FRAGS   512
#define REASM_BUF_MAX     (512 * 1024)
#define REASM_TIMEOUT_US  300000ULL

typedef struct {
    int      in_use;
    uint8_t  type;
    uint32_t frame_id;
    uint16_t frags_recv;
    uint16_t frags_total;
    uint64_t timestamp_us;
    uint64_t first_recv_us;
    uint32_t total_size;
    int      slot_ids[REASM_MAX_FRAGS];
    uint8_t  frag_received[REASM_MAX_FRAGS];
} IndexReasmSlot;

static void init_reasm_slot(IndexReasmSlot *rs) {
    memset(rs, 0, sizeof(*rs));
    for (int i = 0; i < REASM_MAX_FRAGS; i++) {
        rs->slot_ids[i] = RX_SLOT_INVALID;
    }
}

static void release_reasm_slots(RxPacketPool *pool, IndexReasmSlot *rs) {
    for (int i = 0; i < REASM_MAX_FRAGS; i++) {
        if (rs->slot_ids[i] != RX_SLOT_INVALID) {
            rx_packet_pool_release(pool, rs->slot_ids[i]);
            rs->slot_ids[i] = RX_SLOT_INVALID;
        }
    }
}

static void clear_reasm_slot(RxPacketPool *pool, IndexReasmSlot *rs) {
    release_reasm_slots(pool, rs);
    init_reasm_slot(rs);
}

static int first_missing_frag(const IndexReasmSlot *rs) {
    for (int i = 0; i < rs->frags_total && i < REASM_MAX_FRAGS; i++) {
        if (!rs->frag_received[i]) return i;
    }
    return -1;
}

static void expire_stale(ReasmCtx *ctx, IndexReasmSlot *slots) {
    uint64_t now = now_us();
    for (int i = 0; i < REASM_SLOTS; i++) {
        IndexReasmSlot *rs = &slots[i];
        uint64_t age_us = now - rs->first_recv_us;
        if (rs->in_use && age_us > REASM_TIMEOUT_US) {
            fprintf(stderr,
                    "[reasm] timeout: type=%d frame_id=%u frags=%u/%u "
                    "missing_first=%d age_us=%llu\n",
                    rs->type, rs->frame_id, rs->frags_recv, rs->frags_total,
                    first_missing_frag(rs), (unsigned long long)age_us);
            bridge_api_note_drop(ctx->api, ctx->robot_id, rs->type,
                                 "reassembly timeout");
            clear_reasm_slot(ctx->rx_pool, rs);
        }
    }
}

static IndexReasmSlot *find_slot(IndexReasmSlot *slots, uint8_t type,
                                 uint32_t frame_id) {
    for (int i = 0; i < REASM_SLOTS; i++) {
        if (slots[i].in_use && slots[i].type == type &&
            slots[i].frame_id == frame_id) {
            return &slots[i];
        }
    }
    return NULL;
}

static IndexReasmSlot *alloc_slot(IndexReasmSlot *slots) {
    for (int i = 0; i < REASM_SLOTS; i++) {
        if (!slots[i].in_use) return &slots[i];
    }
    return NULL;
}

static int commit_image_indexed(ReasmCtx *ctx, IndexReasmSlot *rs) {
    SharedData *shm = ctx->shm;
    RxPacketPool *pool = ctx->rx_pool;

    int ready = atomic_load_explicit(&shm->img_ready_idx, memory_order_acquire);
    int dst_idx = (ready < 0) ? 0 : (ready + 1) % IMG_SLOTS;
    ImgSlot *dst = &shm->img_slots[dst_idx];
    uint32_t total_size = 0;

    for (int i = 0; i < rs->frags_total; i++) {
        int slot_id = rs->slot_ids[i];
        if (slot_id == RX_SLOT_INVALID) return 0;

        RxPacketSlot *src = &pool->slots[slot_id];
        if (src->len < (int)sizeof(PktHeader)) return 0;

        const PktHeader *hdr = (const PktHeader *)src->buf;
        const uint8_t *payload = src->buf + sizeof(PktHeader);
        if (hdr->payload_offset >= IMG_SLOT_SIZE ||
            hdr->payload_len > IMG_SLOT_SIZE - hdr->payload_offset) {
            fprintf(stderr,
                    "[reasm] image commit OOB: offset=%u len=%u\n",
                    hdr->payload_offset, hdr->payload_len);
            return 0;
        }

        memcpy(dst->data + hdr->payload_offset, payload, hdr->payload_len);
        uint32_t end = hdr->payload_offset + hdr->payload_len;
        if (end > total_size) total_size = end;
    }

    dst->size = total_size;
    dst->frame_id = rs->frame_id;
    dst->timestamp_us = rs->timestamp_us;

    atomic_store_explicit(&shm->img_ready_idx, dst_idx, memory_order_release);
    sem_post(&shm->img_sem);
    bridge_api_note_frame_ready(ctx->api, ctx->robot_id, PKT_TYPE_IMAGE,
                                rs->frame_id, rs->timestamp_us);
    return 1;
}

static int build_lidar_staging(ReasmCtx *ctx, IndexReasmSlot *rs,
                               uint8_t *stage, uint32_t *total_size_out) {
    RxPacketPool *pool = ctx->rx_pool;
    uint32_t total_size = 0;

    for (int i = 0; i < rs->frags_total; i++) {
        int slot_id = rs->slot_ids[i];
        if (slot_id == RX_SLOT_INVALID) return 0;

        RxPacketSlot *src = &pool->slots[slot_id];
        if (src->len < (int)sizeof(PktHeader)) return 0;

        const PktHeader *hdr = (const PktHeader *)src->buf;
        const uint8_t *payload = src->buf + sizeof(PktHeader);
        if (hdr->payload_offset >= REASM_BUF_MAX ||
            hdr->payload_len > REASM_BUF_MAX - hdr->payload_offset) {
            fprintf(stderr,
                    "[reasm] lidar staging OOB: offset=%u len=%u\n",
                    hdr->payload_offset, hdr->payload_len);
            return 0;
        }

        memcpy(stage + hdr->payload_offset, payload, hdr->payload_len);
        uint32_t end = hdr->payload_offset + hdr->payload_len;
        if (end > total_size) total_size = end;
    }

    *total_size_out = total_size;
    return 1;
}

static int commit_lidar_indexed(ReasmCtx *ctx, IndexReasmSlot *rs,
                                uint8_t *stage) {
    SharedData *shm = ctx->shm;
    uint32_t total_size = 0;
    if (!build_lidar_staging(ctx, rs, stage, &total_size)) return 0;
    if (total_size < sizeof(uint32_t)) return 0;

    uint32_t count;
    memcpy(&count, stage, sizeof(count));
    if (count > LIDAR_MAX_PTS) {
        fprintf(stderr, "[reasm] lidar count clipped: %u -> %d\n",
                count, LIDAR_MAX_PTS);
        count = LIDAR_MAX_PTS;
    }

    uint32_t needed = (uint32_t)sizeof(uint32_t) +
                      count * 4U * (uint32_t)sizeof(float);
    if (total_size < needed) {
        fprintf(stderr,
                "[reasm] lidar short frame: total_size=%u needed=%u count=%u\n",
                total_size, needed, count);
        return 0;
    }

    int ready = atomic_load_explicit(&shm->lidar_ready_idx, memory_order_acquire);
    int dst_idx = (ready < 0) ? 0 : (ready + 1) % LIDAR_SLOTS;
    LidarSlot *dst = &shm->lidar_slots[dst_idx];

    dst->count = count;
    dst->frame_id = rs->frame_id;
    dst->timestamp_us = rs->timestamp_us;

    const float *pts = (const float *)(stage + sizeof(uint32_t));
    for (uint32_t i = 0; i < count; i++) {
        dst->x[i] = pts[i * 4];
        dst->y[i] = pts[i * 4 + 1];
        dst->z[i] = pts[i * 4 + 2];
        dst->intensity[i] = pts[i * 4 + 3];
    }

    atomic_store_explicit(&shm->lidar_ready_idx, dst_idx, memory_order_release);
    sem_post(&shm->lidar_sem);
    bridge_api_note_frame_ready(ctx->api, ctx->robot_id, PKT_TYPE_LIDAR,
                                rs->frame_id, rs->timestamp_us);
    return 1;
}

static int validate_fragment_header(const PktHeader *hdr, int plen) {
    if (hdr->frag_total == 0 || hdr->frag_total > REASM_MAX_FRAGS) return 0;
    if (hdr->frag_idx >= hdr->frag_total) return 0;
    if (hdr->payload_len != (uint16_t)plen) return 0;
    if (hdr->payload_offset >= REASM_BUF_MAX) return 0;
    if ((uint32_t)plen > REASM_BUF_MAX - hdr->payload_offset) return 0;
    return 1;
}

void *reassembly_shm_thread(void *arg) {
    ReasmCtx *ctx = (ReasmCtx *)arg;
    FragIndexQueue *fq = ctx->fq;

    IndexReasmSlot *slots = calloc(REASM_SLOTS, sizeof(IndexReasmSlot));
    if (!slots) {
        fprintf(stderr, "[reasm] slot allocation failed\n");
        return NULL;
    }
    for (int i = 0; i < REASM_SLOTS; i++) init_reasm_slot(&slots[i]);

    uint8_t *lidar_stage = malloc(REASM_BUF_MAX);
    if (!lidar_stage) {
        fprintf(stderr, "[reasm] lidar staging allocation failed\n");
        free(slots);
        return NULL;
    }

    fprintf(stderr, "[reasm] indexed start robot=%u\n", ctx->robot_id);

    while (1) {
        int slot_id = frag_index_queue_pop(fq);
        if (slot_id == RX_SLOT_INVALID) break;

        RxPacketSlot *pkt_slot = &ctx->rx_pool->slots[slot_id];
        if (pkt_slot->len < (int)sizeof(PktHeader)) {
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        const PktHeader *hdr = (const PktHeader *)pkt_slot->buf;
        int plen = pkt_slot->len - (int)sizeof(PktHeader);

        expire_stale(ctx, slots);

        if (!validate_fragment_header(hdr, plen)) {
            fprintf(stderr,
                    "[reasm] invalid fragment: type=%u frame=%u idx=%u total=%u\n",
                    hdr->type, hdr->frame_id, hdr->frag_idx, hdr->frag_total);
            bridge_api_note_drop(ctx->api, ctx->robot_id, hdr->type,
                                 "invalid fragment header");
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        IndexReasmSlot *rs = find_slot(slots, hdr->type, hdr->frame_id);
        if (!rs) {
            rs = alloc_slot(slots);
            if (!rs) {
                fprintf(stderr, "[reasm] slots exhausted frame_id=%u\n",
                        hdr->frame_id);
                bridge_api_note_drop(ctx->api, ctx->robot_id, hdr->type,
                                     "reassembly slots exhausted");
                rx_packet_pool_release(ctx->rx_pool, slot_id);
                continue;
            }

            init_reasm_slot(rs);
            rs->in_use = 1;
            rs->type = hdr->type;
            rs->frame_id = hdr->frame_id;
            rs->frags_total = hdr->frag_total;
            rs->timestamp_us = hdr->timestamp_us;
            rs->first_recv_us = now_us();
        }

        if (hdr->frag_total != rs->frags_total) {
            fprintf(stderr,
                    "[reasm] metadata mismatch frame_id=%u total=%u/%u\n",
                    hdr->frame_id, hdr->frag_total, rs->frags_total);
            bridge_api_note_drop(ctx->api, ctx->robot_id, hdr->type,
                                 "fragment metadata mismatch");
            clear_reasm_slot(ctx->rx_pool, rs);
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        if (rs->frag_received[hdr->frag_idx]) {
            rx_packet_pool_release(ctx->rx_pool, slot_id);
            continue;
        }

        rs->slot_ids[hdr->frag_idx] = slot_id;
        rs->frag_received[hdr->frag_idx] = 1;
        rs->frags_recv++;

        uint32_t end = hdr->payload_offset + hdr->payload_len;
        if (end > rs->total_size) rs->total_size = end;

        if (rs->frags_recv == rs->frags_total) {
            int ok = 0;
            if (rs->type == PKT_TYPE_IMAGE) {
                ok = commit_image_indexed(ctx, rs);
            } else if (rs->type == PKT_TYPE_LIDAR) {
                ok = commit_lidar_indexed(ctx, rs, lidar_stage);
            }

            if (!ok) {
                bridge_api_note_drop(ctx->api, ctx->robot_id, rs->type,
                                     "indexed commit failed");
            }
            clear_reasm_slot(ctx->rx_pool, rs);
        }
    }

    for (int i = 0; i < REASM_SLOTS; i++) {
        if (slots[i].in_use) clear_reasm_slot(ctx->rx_pool, &slots[i]);
    }

    free(lidar_stage);
    free(slots);
    fprintf(stderr, "[reasm] indexed stop robot=%u\n", ctx->robot_id);
    return NULL;
}

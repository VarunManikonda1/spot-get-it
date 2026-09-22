#include "robotshmconnection.h"

#include <QByteArray>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "shm_def.h"

namespace {

constexpr qint64 kImageStaleMs = 2000;

SharedData *asShm(void *data)
{
    return static_cast<SharedData *>(data);
}

QString fixedUtf8(const char *data, size_t maxLen)
{
    const void *end = std::memchr(data, '\0', maxLen);
    const size_t len = end
        ? static_cast<size_t>(static_cast<const char *>(end) - data)
        : maxLen;
    return QString::fromUtf8(data, static_cast<qsizetype>(len)).trimmed();
}

uint8_t effectivePriority(const ShmCmdEntry &entry)
{
    if (entry.priority != 0) {
        return entry.priority;
    }
    return (entry.cmd.cmd_type == CMD_TYPE_ESTOP)
        ? CMD_PRIORITY_CRITICAL : CMD_PRIORITY_NORMAL;
}

void dropQueueEntry(ShmCmdQueue *queue, int dropOffset, int head, int count)
{
    for (int offset = dropOffset; offset > 0; --offset) {
        const int dst = (head + offset) % SHM_CMD_QUEUE_SIZE;
        const int src = (head + offset - 1) % SHM_CMD_QUEUE_SIZE;
        queue->entries[dst] = queue->entries[src];
    }
    queue->head.store((head + 1) % SHM_CMD_QUEUE_SIZE, std::memory_order_release);
    queue->count.store(count - 1, std::memory_order_release);
}

bool pushCommandToShm(SharedData *shm, const ShmCmdEntry &entry)
{
    ShmCmdQueue *queue = &shm->cmd_queue;
    if (pthread_mutex_lock(&queue->mu) != 0) {
        return false;
    }

    int head = queue->head.load(std::memory_order_relaxed);
    int tail = queue->tail.load(std::memory_order_relaxed);
    int count = queue->count.load(std::memory_order_acquire);

    if (count >= SHM_CMD_QUEUE_SIZE) {
        int dropOffset = 0;
        uint8_t dropPriority = effectivePriority(queue->entries[head]);
        for (int offset = 1; offset < count; ++offset) {
            const int idx = (head + offset) % SHM_CMD_QUEUE_SIZE;
            const uint8_t priority = effectivePriority(queue->entries[idx]);
            if (priority < dropPriority) {
                dropPriority = priority;
                dropOffset = offset;
            }
        }

        if (effectivePriority(entry) < dropPriority) {
            pthread_mutex_unlock(&queue->mu);
            return false;
        }

        dropQueueEntry(queue, dropOffset, head, count);
        queue->drop_count.fetch_add(1, std::memory_order_relaxed);
        head = queue->head.load(std::memory_order_relaxed);
        tail = queue->tail.load(std::memory_order_relaxed);
        count = SHM_CMD_QUEUE_SIZE - 1;
    }

    queue->entries[tail] = entry;
    tail = (tail + 1) % SHM_CMD_QUEUE_SIZE;
    queue->tail.store(tail, std::memory_order_release);
    queue->count.store(count + 1, std::memory_order_release);
    queue->write_seq.fetch_add(1, std::memory_order_release);

    pthread_mutex_unlock(&queue->mu);
    return true;
}

} // namespace

RobotShmConnection::RobotShmConnection(int robotId)
    : m_robotId(robotId)
{
}

RobotShmConnection::~RobotShmConnection()
{
    close();
}

QString RobotShmConnection::shmName() const
{
    return QString("/robot_bridge_%1").arg(m_robotId);
}

bool RobotShmConnection::open()
{
    if (m_data) {
        return true;
    }

    const QByteArray name = shmName().toUtf8();
    m_fd = shm_open(name.constData(), O_RDWR, 0666);
    if (m_fd < 0) {
        return false;
    }

    struct stat st {};
    if (fstat(m_fd, &st) != 0) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    if (st.st_size < static_cast<off_t>(sizeof(SharedData))) {
        errno = EINVAL;
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    SharedData *shm = static_cast<SharedData *>(mmap(nullptr, sizeof(SharedData),
                                                     PROT_READ | PROT_WRITE,
                                                     MAP_SHARED, m_fd, 0));
    if (shm == MAP_FAILED) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    if (shm->shm_magic != SHM_MAGIC ||
        shm->shm_version != SHM_VERSION ||
        shm->shared_data_size != static_cast<uint32_t>(sizeof(SharedData))) {
        errno = EINVAL;
        munmap(shm, sizeof(SharedData));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    m_data = shm;
    m_lastEventSeq = shm->event_log.write_seq.load(std::memory_order_acquire);
    return true;
}

void RobotShmConnection::close()
{
    if (m_data) {
        munmap(m_data, sizeof(SharedData));
        m_data = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_haveLastImageFrame = false;
    m_lastImageFrame = 0;
    m_lastImageFrameTimer.invalidate();
    m_haveLastLidarFrame = false;
    m_lastLidarFrame = 0;
}

bool RobotShmConnection::waitForImage(int timeoutMs)
{
    if (!open()) {
        return false;
    }

    SharedData *shm = asShm(m_data);
    timespec deadline {};
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        return false;
    }

    const int safeTimeoutMs = std::max(0, timeoutMs);
    deadline.tv_sec += safeTimeoutMs / 1000;
    deadline.tv_nsec += static_cast<long>(safeTimeoutMs % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    while (sem_timedwait(&shm->img_sem, &deadline) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

RobotSnapshot RobotShmConnection::poll(const std::function<void(const UiEvent &)> &eventSink)
{
    RobotSnapshot snapshot;
    snapshot.id = m_robotId;
    if (!open()) {
        return snapshot;
    }

    snapshot.shmOpen = true;
    readState(snapshot);
    readGlobalPath(snapshot);
    readImage(snapshot);
    readLidar(snapshot);
    readEvents(eventSink);
    return snapshot;
}

void RobotShmConnection::readState(RobotSnapshot &snapshot)
{
    SharedData *shm = asShm(m_data);
    if (!shm) {
        return;
    }

    bool metaConnectedRead = false;
    bool metaConnected = false;
    if (pthread_rwlock_rdlock(&shm->meta_lock) == 0) {
        metaConnectedRead = true;
        metaConnected = shm->meta.jetson_connected.load(std::memory_order_acquire) != 0;
        snapshot.connected = shm->meta.jetson_connected.load(std::memory_order_acquire) != 0;
        snapshot.imgDropCount = shm->meta.img_drop_count;
        snapshot.lidarDropCount = shm->meta.lidar_drop_count;
        pthread_rwlock_unlock(&shm->meta_lock);
    }

    RobotState state;
    if (pthread_rwlock_rdlock(&shm->state_lock) == 0) {
        state = shm->state;
        pthread_rwlock_unlock(&shm->state_lock);
        snapshot.connected = metaConnectedRead ? metaConnected : state.connected != 0;
        snapshot.mode = state.mode;
        snapshot.faultLevel = state.fault_level;
        snapshot.x = state.x;
        snapshot.y = state.y;
        snapshot.theta = state.theta;
        snapshot.vx = state.vx;
        snapshot.vy = state.vy;
        snapshot.omega = state.omega;
        snapshot.battery = state.battery_percent;
        snapshot.linkRttMs = state.link_rtt_ms;
        snapshot.imageFps = state.image_fps;
        snapshot.lidarFps = state.lidar_fps;
        snapshot.dropRate = state.drop_rate;
        snapshot.odomSeq = state.odom_seq;
        snapshot.missionId = state.mission_id;
        snapshot.waypointIdx = state.waypoint_idx;
        snapshot.totalWaypoints = state.total_waypoints;
        snapshot.missionProgress = state.mission_progress;
        snapshot.pathOk = state.path_ok != 0;
        snapshot.poseOk = state.pose_ok != 0;
        snapshot.goalReached = state.goal_reached != 0;
        snapshot.nearestPathIdx = state.nearest_path_idx;
        snapshot.distanceToNearestM = state.distance_to_nearest_m;
        snapshot.nearestX = state.nearest_x;
        snapshot.nearestY = state.nearest_y;
        snapshot.targetX = state.target_x;
        snapshot.targetY = state.target_y;
        snapshot.targetHeading = state.target_heading;
        snapshot.headingError = state.heading_error;
        snapshot.distanceToTargetM = state.distance_to_target_m;
        snapshot.distanceToGoalM = state.distance_to_goal_m;
        snapshot.faultCode = state.fault_code;
        snapshot.faultText = fixedUtf8(state.fault_text, sizeof(state.fault_text));
        snapshot.lastRxUs = state.last_rx_us;
    } else if (pthread_rwlock_rdlock(&shm->odom_lock) == 0) {
        snapshot.x = shm->odom_x;
        snapshot.y = shm->odom_y;
        snapshot.theta = shm->odom_theta;
        snapshot.vx = shm->odom_vx;
        snapshot.vy = shm->odom_vy;
        snapshot.omega = shm->odom_omega;
        snapshot.odomSeq = shm->odom_seq;
        snapshot.odomTimestampUs = shm->odom_timestamp_us;
        pthread_rwlock_unlock(&shm->odom_lock);
    }

    if (pthread_rwlock_rdlock(&shm->odom_lock) == 0) {
        snapshot.odomTimestampUs = shm->odom_timestamp_us;
        pthread_rwlock_unlock(&shm->odom_lock);
    }

    snapshot.rxPackets = shm->metrics.rx_packets.load(std::memory_order_acquire);
    snapshot.txCommands = shm->metrics.tx_commands.load(std::memory_order_acquire);
    snapshot.ackPackets = shm->metrics.ack_packets.load(std::memory_order_acquire);
}

void RobotShmConnection::readGlobalPath(RobotSnapshot &snapshot)
{
    SharedData *shm = asShm(m_data);
    if (!shm) {
        return;
    }

    GlobalPathState path;
    if (pthread_rwlock_rdlock(&shm->path_lock) != 0) {
        return;
    }
    path = shm->global_path;
    pthread_rwlock_unlock(&shm->path_lock);

    const uint8_t count = std::min<uint8_t>(path.count, GLOBAL_PATH_MAX_WAYPOINTS);
    snapshot.globalPathSeq = path.seq;
    snapshot.globalPathUpdatedUs = path.updated_us;
    snapshot.globalPath.reserve(count);
    for (uint8_t i = 0; i < count; ++i) {
        const PathWaypoint &wp = path.waypoints[i];
        snapshot.globalPath.append(GlobalPathPoint{wp.x, wp.y, wp.z, wp.yaw});
    }
}

void RobotShmConnection::readImage(RobotSnapshot &snapshot)
{
    SharedData *shm = asShm(m_data);
    if (!snapshot.connected) {
        return;
    }

    const int idx = shm->img_ready_idx.load(std::memory_order_acquire);
    if (idx < 0 || idx >= IMG_SLOTS) {
        m_haveLastImageFrame = false;
        m_lastImageFrameTimer.invalidate();
        return;
    }

    const ImgSlot &slot = shm->img_slots[idx];
    snapshot.imageFrameId = slot.frame_id;
    if (slot.size == 0 || slot.size > IMG_SLOT_SIZE) {
        return;
    }

    const bool newFrame = !m_haveLastImageFrame || slot.frame_id != m_lastImageFrame;
    if (newFrame) {
        m_lastImageFrame = slot.frame_id;
        m_haveLastImageFrame = true;
        m_lastImageFrameTimer.restart();
    } else if (!m_lastImageFrameTimer.isValid() || m_lastImageFrameTimer.elapsed() > kImageStaleMs) {
        return;
    }

    QImage image = QImage::fromData(slot.data, static_cast<int>(slot.size), "JPEG");
    if (!image.isNull()) {
        snapshot.image = image;
    }
}

void RobotShmConnection::readLidar(RobotSnapshot &snapshot)
{
    SharedData *shm = asShm(m_data);
    const int idx = shm->lidar_ready_idx.load(std::memory_order_acquire);
    if (idx < 0 || idx >= LIDAR_SLOTS) {
        return;
    }

    const LidarSlot &slot = shm->lidar_slots[idx];
    snapshot.lidarFrameId = slot.frame_id;
    if (m_haveLastLidarFrame && slot.frame_id == m_lastLidarFrame) {
        return;
    }

    const uint32_t count = std::min(slot.count, static_cast<uint32_t>(LIDAR_MAX_PTS));
    const uint32_t step = std::max(1u, count / 500u);
    snapshot.lidarPoints.reserve(static_cast<int>(count / step + 1));
    for (uint32_t i = 0; i < count; i += step) {
        snapshot.lidarPoints.append(LidarPoint{slot.x[i], slot.y[i], slot.z[i], slot.intensity[i]});
    }
    m_lastLidarFrame = slot.frame_id;
    m_haveLastLidarFrame = true;
}

void RobotShmConnection::readEvents(const std::function<void(const UiEvent &)> &eventSink)
{
    SharedData *shm = asShm(m_data);
    const int writeSeq = shm->event_log.write_seq.load(std::memory_order_acquire);
    int from = m_lastEventSeq;
    if (writeSeq - from > EVENT_LOG_SIZE) {
        from = writeSeq - EVENT_LOG_SIZE;
    }
    for (int seq = from; seq < writeSeq; ++seq) {
        const RobotEvent &src = shm->event_log.events[seq % EVENT_LOG_SIZE];
        UiEvent event;
        event.robotId = m_robotId;
        event.severity = src.severity;
        event.type = src.event_type;
        event.timestampUs = src.timestamp_us;
        event.message = fixedUtf8(src.message, sizeof(src.message));
        event.code = static_cast<int>(src.code);
        if (event.message.isEmpty()) {
            event.message = QString("event type %1 code %2").arg(event.type).arg(src.code);
        }
        eventSink(event);
    }
    m_lastEventSeq = writeSeq;
}

bool RobotShmConnection::sendCommand(uint8_t commandType, float vx, float vy, float omega,
                                     uint32_t seq, QString *errorMessage, uint8_t flags)
{
    if (m_robotId < 0 || m_robotId >= kMaxRobots) {
        if (errorMessage) {
            *errorMessage = QString("invalid robot id %1").arg(m_robotId);
        }
        return false;
    }

    if (!open()) {
        if (errorMessage) {
            *errorMessage = QString("shm_open(%1): %2")
                .arg(shmName(), QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    SharedData *shm = asShm(m_data);
    ShmCmdEntry entry {};
    entry.cmd.robot_id = static_cast<uint8_t>(m_robotId);
    entry.cmd.cmd_type = commandType;
    entry.cmd.vx = vx;
    entry.cmd.vy = vy;
    entry.cmd.omega = omega;
    entry.cmd.seq = seq;
    entry.priority = (commandType == CMD_TYPE_ESTOP)
        ? CMD_PRIORITY_CRITICAL : CMD_PRIORITY_NORMAL;
    entry.flags = flags;
    if (entry.priority >= CMD_PRIORITY_HIGH) {
        entry.flags |= CMD_FLAG_REQUIRES_ACK;
    }

    if (!pushCommandToShm(shm, entry)) {
        if (errorMessage) {
            *errorMessage = QString("command queue push failed for %1").arg(shmName());
        }
        return false;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

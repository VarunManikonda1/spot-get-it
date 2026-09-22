#include "shmmonitor.h"

#include "robotshmconnection.h"
#include "robotworkerthread.h"

#include <algorithm>

ShmMonitor::ShmMonitor(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<UiEvent>("UiEvent");
    qRegisterMetaType<LidarPoint>("LidarPoint");
    qRegisterMetaType<RobotSnapshot>("RobotSnapshot");
    qRegisterMetaType<QVector<RobotSnapshot>>("QVector<RobotSnapshot>");
    connect(&m_uiTimer, &QTimer::timeout, this, &ShmMonitor::flushSnapshots);
}

ShmMonitor::~ShmMonitor()
{
    stopWorkers();
}

void ShmMonitor::start(int maxRobots)
{
    stopWorkers();

    m_maxRobots = std::clamp(maxRobots, 1, kMaxRobots);
    m_snapshots.resize(m_maxRobots);
    m_workers.reserve(m_maxRobots);
    for (int i = 0; i < m_maxRobots; ++i) {
        m_snapshots[i].id = i;
        auto worker = std::make_unique<RobotWorkerThread>(i);
        connect(worker.get(), &RobotWorkerThread::snapshotReady,
                this, &ShmMonitor::handleSnapshot);
        connect(worker.get(), &RobotWorkerThread::eventReceived,
                this, &ShmMonitor::eventReceived);
        worker->start();
        m_workers.push_back(std::move(worker));
    }
    emit snapshotsUpdated(m_snapshots);
    m_uiTimer.start(33);
}

void ShmMonitor::stopWorkers()
{
    m_uiTimer.stop();
    for (const std::unique_ptr<RobotWorkerThread> &worker : m_workers) {
        worker->requestInterruption();
    }
    for (const std::unique_ptr<RobotWorkerThread> &worker : m_workers) {
        worker->wait(1000);
    }
    m_workers.clear();
    m_dirty = false;
}

void ShmMonitor::handleSnapshot(const RobotSnapshot &snapshot)
{
    if (snapshot.id < 0 || snapshot.id >= m_snapshots.size()) {
        return;
    }
    m_snapshots[snapshot.id] = snapshot;
    m_dirty = true;
}

void ShmMonitor::flushSnapshots()
{
    if (!m_dirty) {
        return;
    }
    m_dirty = false;
    emit snapshotsUpdated(m_snapshots);
}

bool ShmMonitor::sendCommand(int robotId, uint8_t commandType,
                             float vx, float vy, float omega, QString *errorMessage,
                             uint8_t flags)
{
    if (robotId < 0 || robotId >= m_maxRobots) {
        if (errorMessage) {
            *errorMessage = QString("invalid robot id %1").arg(robotId);
        }
        return false;
    }

    RobotShmConnection connection(robotId);
    return connection.sendCommand(commandType, vx, vy, omega, m_commandSeq++, errorMessage, flags);
}

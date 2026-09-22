#ifndef SHMMONITOR_H
#define SHMMONITOR_H

#include "shmtypes.h"

#include <QObject>
#include <QTimer>
#include <QVector>

#include <memory>
#include <vector>
#include <cstdint>

class RobotWorkerThread;

class ShmMonitor : public QObject
{
    Q_OBJECT

public:
    explicit ShmMonitor(QObject *parent = nullptr);
    ~ShmMonitor() override;

    void start(int maxRobots = kMaxRobots);
    QVector<RobotSnapshot> snapshots() const { return m_snapshots; }
    bool sendCommand(int robotId, uint8_t commandType,
                     float vx, float vy, float omega, QString *errorMessage,
                     uint8_t flags = 0);

signals:
    void snapshotsUpdated(const QVector<RobotSnapshot> &snapshots);
    void eventReceived(const UiEvent &event);

private slots:
    void handleSnapshot(const RobotSnapshot &snapshot);
    void flushSnapshots();

private:
    void stopWorkers();

    QTimer m_uiTimer;
    int m_maxRobots = kMaxRobots;
    uint32_t m_commandSeq = 1;
    bool m_dirty = false;
    std::vector<std::unique_ptr<RobotWorkerThread>> m_workers;
    QVector<RobotSnapshot> m_snapshots;
};

#endif

#ifndef ROBOTSHMCONNECTION_H
#define ROBOTSHMCONNECTION_H

#include "shmtypes.h"

#include <QElapsedTimer>
#include <QString>

#include <cstdint>
#include <functional>

class RobotShmConnection
{
public:
    explicit RobotShmConnection(int robotId);
    ~RobotShmConnection();

    RobotShmConnection(const RobotShmConnection &) = delete;
    RobotShmConnection &operator=(const RobotShmConnection &) = delete;

    int robotId() const { return m_robotId; }
    QString shmName() const;
    bool isOpen() const { return m_data != nullptr; }

    bool waitForImage(int timeoutMs);
    RobotSnapshot poll(const std::function<void(const UiEvent &)> &eventSink);
    bool sendCommand(uint8_t commandType, float vx, float vy, float omega,
                     uint32_t seq, QString *errorMessage, uint8_t flags = 0);
    void close();

private:
    bool open();
    void readState(RobotSnapshot &snapshot);
    void readGlobalPath(RobotSnapshot &snapshot);
    void readImage(RobotSnapshot &snapshot);
    void readLidar(RobotSnapshot &snapshot);
    void readEvents(const std::function<void(const UiEvent &)> &eventSink);

    int m_robotId = -1;
    int m_fd = -1;
    void *m_data = nullptr;
    uint32_t m_lastImageFrame = 0;
    QElapsedTimer m_lastImageFrameTimer;
    bool m_haveLastImageFrame = false;
    uint32_t m_lastLidarFrame = 0;
    bool m_haveLastLidarFrame = false;
    int m_lastEventSeq = 0;
};

#endif

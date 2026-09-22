#ifndef SHMTYPES_H
#define SHMTYPES_H

#include <QImage>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>

constexpr int kMaxRobots = 10;
constexpr int kEventTypeCmdAck = 4;
constexpr int kEventTypeVictimDetected = 8;

struct UiEvent {
    int robotId = -1;
    int severity = 0;
    int type = 0;
    quint64 timestampUs = 0;
    QString message;
    int code = -1;
};

struct LidarPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float intensity = 0.0f;
};

struct GlobalPathPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
};

struct RobotSnapshot {
    int id = -1;
    bool shmOpen = false;
    bool connected = false;
    int mode = 0;
    int faultLevel = 0;
    float x = 0.0f;
    float y = 0.0f;
    float theta = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float omega = 0.0f;
    float battery = 0.0f;
    float linkRttMs = 0.0f;
    float imageFps = 0.0f;
    float lidarFps = 0.0f;
    float dropRate = 0.0f;
    uint32_t odomSeq = 0;
    quint64 odomTimestampUs = 0;
    uint32_t missionId = 0;
    uint32_t waypointIdx = 0;
    uint32_t totalWaypoints = 0;
    float missionProgress = 0.0f;
    bool pathOk = false;
    bool poseOk = false;
    bool goalReached = false;
    bool commandMoving = false;
    uint32_t nearestPathIdx = 0;
    float distanceToNearestM = 0.0f;
    float nearestX = 0.0f;
    float nearestY = 0.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetHeading = 0.0f;
    float headingError = 0.0f;
    float distanceToTargetM = 0.0f;
    float distanceToGoalM = 0.0f;
    uint32_t faultCode = 0;
    QString faultText;
    uint32_t imgDropCount = 0;
    uint32_t lidarDropCount = 0;
    int rxPackets = 0;
    int txCommands = 0;
    int ackPackets = 0;
    quint64 lastRxUs = 0;
    QImage image;
    uint32_t imageFrameId = 0;
    uint32_t lidarFrameId = 0;
    QVector<LidarPoint> lidarPoints;
    uint32_t globalPathSeq = 0;
    quint64 globalPathUpdatedUs = 0;
    QVector<GlobalPathPoint> globalPath;
};

Q_DECLARE_METATYPE(UiEvent)
Q_DECLARE_METATYPE(LidarPoint)
Q_DECLARE_METATYPE(GlobalPathPoint)
Q_DECLARE_METATYPE(RobotSnapshot)

#endif

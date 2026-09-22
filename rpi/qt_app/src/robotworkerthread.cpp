#include "robotworkerthread.h"

RobotWorkerThread::RobotWorkerThread(int robotId, QObject *parent)
    : QThread(parent)
    , m_robotId(robotId)
{
}

RobotWorkerThread::~RobotWorkerThread()
{
    requestInterruption();
    wait(1000);
}

void RobotWorkerThread::run()
{
    RobotShmConnection connection(m_robotId);

    while (!isInterruptionRequested()) {
        connection.waitForImage(100);
        RobotSnapshot snapshot = connection.poll([this](const UiEvent &event) {
            emit eventReceived(event);
        });
        emit snapshotReady(snapshot);
        if (!snapshot.shmOpen) {
            msleep(100);
        }
    }
}

#ifndef ROBOTWORKERTHREAD_H
#define ROBOTWORKERTHREAD_H

#include "robotshmconnection.h"
#include "shmtypes.h"

#include <QThread>

class RobotWorkerThread : public QThread
{
    Q_OBJECT

public:
    explicit RobotWorkerThread(int robotId, QObject *parent = nullptr);
    ~RobotWorkerThread() override;

    int robotId() const { return m_robotId; }

signals:
    void snapshotReady(const RobotSnapshot &snapshot);
    void eventReceived(const UiEvent &event);

protected:
    void run() override;

private:
    int m_robotId = -1;
};

#endif

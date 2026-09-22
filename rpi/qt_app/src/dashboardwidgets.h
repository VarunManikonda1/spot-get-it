#ifndef DASHBOARDWIDGETS_H
#define DASHBOARDWIDGETS_H

#include "mapdata.h"
#include "shmmonitor.h"

#include <QFrame>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPoint>
#include <QPointF>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

class LidarMap2DView;
class PointCloud3DView;
class QPaintEvent;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QHideEvent;
class QPropertyAnimation;
class QStackedWidget;

class AnimatedProgressBar : public QProgressBar
{
    Q_OBJECT

public:
    explicit AnimatedProgressBar(QWidget *parent = nullptr);
    void animateToValue(int value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    QColor chunkColor() const;
    QColor trackColor() const;
    qreal cornerRadius() const;
    void ensureMotionTimer();

    QPropertyAnimation *m_valueAnimation = nullptr;
    int m_motionTimerId = 0;
    qreal m_phase = 0.0;
};

void setProgressBarValueAnimated(QProgressBar *bar, int value);

class VideoTile : public QFrame
{
    Q_OBJECT

public:
    enum ResizeEdge {
        NoEdge = 0x0,
        LeftEdge = 0x1,
        TopEdge = 0x2,
        RightEdge = 0x4,
        BottomEdge = 0x8
    };

    explicit VideoTile(int robotId, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void setRobotId(int robotId);
    void setSnapshot(const RobotSnapshot &snapshot);

signals:
    void clicked(int robotId);
    void moveRequested(int robotId, const QPoint &globalPos);
    void resizeRequested(int robotId, int edgeMask, const QPoint &globalPos);
    void shrinkRequested(int robotId, int edgeMask);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int resizeEdgeAt(const QPoint &pos) const;

    int m_robotId = 0;
    QLabel *m_title = nullptr;
    QLabel *m_image = nullptr;
    QLabel *m_badge = nullptr;
    QElapsedTimer m_noFrameTimer;
    QPoint m_pressPos;
    QPoint m_lastDragPreviewGlobalPos;
    int m_pressEdgeMask = NoEdge;
    bool m_dragging = false;
    bool m_hasLastDragPreview = false;
};

class StatusRow : public QFrame
{
    Q_OBJECT

public:
    explicit StatusRow(int robotId, QWidget *parent = nullptr);
    void setSnapshot(const RobotSnapshot &snapshot);

private:
    int m_robotId = 0;
    QLabel *m_name = nullptr;
    QLabel *m_batteryText = nullptr;
    QProgressBar *m_batteryBar = nullptr;
    QLabel *m_mission = nullptr;
    QWidget *m_signal = nullptr;
    bool m_everConnected = false;
};

class RobotStatusCard : public QFrame
{
    Q_OBJECT

public:
    explicit RobotStatusCard(int robotId, QWidget *parent = nullptr);
    void setSnapshot(const RobotSnapshot &snapshot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    QLabel *makeValueLabel(const QString &objectName = QStringLiteral("robotCardValue"));
    QFrame *makeInfoCell(const QString &title, QLabel *value, const QString &detail = QString());
    QLabel *makeSensorChip(const QString &text);
    QLabel *makeStatusChip(const QString &text, const QString &objectName);
    QString missionText(int mode) const;
    QString statusText(const RobotSnapshot &snapshot) const;
    void restyleState(const QString &stateName);
    void randomizeFakeTelemetry();
    void updateFakeTelemetryLabels(const QString &textColor);
    bool partStreamInactiveForCard(const RobotSnapshot &snapshot, int partIndex) const;
    bool displayedPartsAreNormalForCard(const RobotSnapshot &snapshot) const;
    int displayedOfflinePartCountForCard(const RobotSnapshot &snapshot) const;

    int m_robotId = 0;
    QLabel *m_name = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_target = nullptr;
    QLabel *m_battery = nullptr;
    QProgressBar *m_batteryBar = nullptr;
    QLabel *m_positionX = nullptr;
    QLabel *m_positionY = nullptr;
    QLabel *m_rtt = nullptr;
    QLabel *m_packetLoss = nullptr;
    QLabel *m_linkQuality = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_temp = nullptr;
    QLabel *m_camera = nullptr;
    QWidget *m_robotView = nullptr;
    QWidget *m_connectionWarningIcon = nullptr;
    QVector<QFrame *> m_sensorCards;
    QVector<QLabel *> m_sensorChips;
    RobotSnapshot m_snapshot;
    float m_fakeSpeedMps = 0.0f;
    float m_fakeTempC = 0.0f;
    bool m_fakeTelemetryValid = false;
    bool m_lidarEverSeen = false;
    bool m_everConnected = false;
    QString m_telemetryTextColor = QStringLiteral("#dce7f3");
    float m_lastPoseX = 0.0f;
    float m_lastPoseY = 0.0f;
    float m_lastPoseTheta = 0.0f;
    bool m_haveLastPose = false;
    QElapsedTimer m_lastPoseChangeTimer;
    bool m_connectionWarningActive = false;
    qint64 m_connectionWarningStartMs = -1;
    int m_connectionWarningTimerId = 0;
};

class MapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MapWidget(QWidget *parent = nullptr);
    bool loadMapConfig(const QString &path);
    void setSnapshots(const QVector<RobotSnapshot> &snapshots);
    void setSelectedRobot(int robotId);
    void showMoveCommandIndicators(const QVector<int> &robotIds);
    void setViewMode3D(bool enabled);
    void setGlobalPathsVisible(bool visible);
    void setGlobalPathRobotIds(const QVector<int> &robotIds);
    void addGlobalPathRobotIds(const QVector<int> &robotIds);
    void showVictimDetection(int robotId);
    void fitToAvailableSize(float targetScale = 0.0f);

signals:
    void routeGenerationRequested(int robotId, const QPointF &end);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateRouteButtonState();
    void applyGlobalPathRobotIds();

    QVector<RobotSnapshot> m_snapshots;
    int m_selectedRobot = 0;
    bool m_view3d = false;
    QStackedWidget *m_stack = nullptr;
    LidarMap2DView *m_map2d = nullptr;
    PointCloud3DView *m_map3d = nullptr;
    QPushButton *m_resetViewButton = nullptr;
    QPushButton *m_routeButton = nullptr;
    QVector<int> m_globalPathRobotIds;
    bool m_routeGenerationPending = false;
    bool m_mapLoaded = false;
};

QFrame *makePanel(const QString &title, QWidget *body);
QLabel *makeMetric(const QString &title, const QString &value, const QString &color);

#endif

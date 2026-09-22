#include "dashboardwidgets.h"

#include <QApplication>
#include <QGridLayout>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineF>
#include <QLinearGradient>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QRandomGenerator>
#include <QSet>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVector3D>
#include <QVector4D>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QSizePolicy>
#include <QStyleOption>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <tuple>
#include <vector>

AnimatedProgressBar::AnimatedProgressBar(QWidget *parent)
    : QProgressBar(parent)
    , m_valueAnimation(new QPropertyAnimation(this, "value", this))
{
    setTextVisible(false);
    m_valueAnimation->setDuration(420);
    m_valueAnimation->setEasingCurve(QEasingCurve::OutCubic);
    ensureMotionTimer();
}

void AnimatedProgressBar::animateToValue(int targetValue)
{
    const int clamped = qBound(minimum(), targetValue, maximum());
    if (clamped == value()) {
        update();
        return;
    }

    m_valueAnimation->stop();
    m_valueAnimation->setStartValue(value());
    m_valueAnimation->setEndValue(clamped);
    const int delta = qAbs(clamped - value());
    m_valueAnimation->setDuration(qBound(260, 180 + delta * 8, 720));
    m_valueAnimation->start();
}

void AnimatedProgressBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = cornerRadius();
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor());
    p.drawRoundedRect(track, radius, radius);

    const int span = maximum() - minimum();
    if (span <= 0 || value() <= minimum()) {
        return;
    }

    const qreal ratio = qBound<qreal>(0.0,
                                      static_cast<qreal>(value() - minimum()) / static_cast<qreal>(span),
                                      1.0);
    QRectF chunk = track;
    chunk.setWidth(qMax<qreal>(radius * 1.6, track.width() * ratio));

    QPainterPath clip;
    clip.addRoundedRect(chunk, radius, radius);
    p.save();
    p.setClipPath(clip);

    const QColor base = chunkColor();
    QLinearGradient fill(chunk.topLeft(), chunk.bottomRight());
    fill.setColorAt(0.0, base.lighter(118));
    fill.setColorAt(0.55, base);
    fill.setColorAt(1.0, base.darker(112));
    p.setBrush(fill);
    p.drawRoundedRect(chunk, radius, radius);

    QColor shine = QColor(255, 255, 255, objectName() == QStringLiteral("headerProgressBar") ? 112 : 88);
    const qreal bandWidth = qMax<qreal>(22.0, track.width() * 0.22);
    const qreal travel = track.width() + bandWidth * 2.0;
    const qreal x = track.left() - bandWidth + std::fmod(m_phase, 1.0) * travel;
    QLinearGradient band(QPointF(x - bandWidth, track.top()), QPointF(x + bandWidth, track.top()));
    band.setColorAt(0.0, QColor(255, 255, 255, 0));
    band.setColorAt(0.38, QColor(255, 255, 255, 0));
    band.setColorAt(0.52, shine);
    band.setColorAt(0.66, QColor(255, 255, 255, 0));
    band.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(band);
    p.drawRect(chunk);

    QColor topLine = base.lighter(145);
    topLine.setAlpha(objectName() == QStringLiteral("headerProgressBar") ? 70 : 54);
    p.setPen(QPen(topLine, 1.0));
    p.drawLine(chunk.topLeft() + QPointF(radius * 0.65, 1.0),
               chunk.topRight() + QPointF(-radius * 0.65, 1.0));
    p.restore();
}

void AnimatedProgressBar::showEvent(QShowEvent *event)
{
    QProgressBar::showEvent(event);
    ensureMotionTimer();
}

void AnimatedProgressBar::hideEvent(QHideEvent *event)
{
    if (m_motionTimerId != 0) {
        killTimer(m_motionTimerId);
        m_motionTimerId = 0;
    }
    QProgressBar::hideEvent(event);
}

void AnimatedProgressBar::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_motionTimerId) {
        m_phase = std::fmod(m_phase + 0.026, 1.0);
        update();
        return;
    }
    QProgressBar::timerEvent(event);
}

QColor AnimatedProgressBar::chunkColor() const
{
    const QString state = property("state").toString();
    if (objectName() == QStringLiteral("headerProgressBar")) {
        return QColor("#18d878");
    }
    if (state == QStringLiteral("warning")) {
        return QColor("#ffd166");
    }
    if (state == QStringLiteral("danger")) {
        return QColor("#ff736b");
    }
    if (state == QStringLiteral("offline")) {
        return objectName() == QStringLiteral("robotBatteryBar") ? QColor("#555f68") : QColor("#55616b");
    }
    return objectName() == QStringLiteral("robotBatteryBar") ? QColor("#72b9ff") : QColor("#16d968");
}

QColor AnimatedProgressBar::trackColor() const
{
    if (objectName() == QStringLiteral("headerProgressBar")) {
        return QColor("#26333a");
    }
    if (objectName() == QStringLiteral("robotBatteryBar")) {
        return QColor("#142131");
    }
    return QColor("#22333d");
}

qreal AnimatedProgressBar::cornerRadius() const
{
    if (objectName() == QStringLiteral("headerProgressBar")) {
        return 7.0;
    }
    if (objectName() == QStringLiteral("robotBatteryBar")) {
        return 3.0;
    }
    return 5.0;
}

void AnimatedProgressBar::ensureMotionTimer()
{
    if (m_motionTimerId == 0 && isVisible()) {
        m_motionTimerId = startTimer(45);
    }
}

void setProgressBarValueAnimated(QProgressBar *bar, int value)
{
    if (auto *animated = qobject_cast<AnimatedProgressBar *>(bar)) {
        animated->animateToValue(value);
        return;
    }
    if (bar) {
        bar->setValue(value);
    }
}

static QString robotName(int id)
{
    return QString("SPOT-%1").arg(id + 1, 2, 10, QLatin1Char('0'));
}

static int displayBatteryPercent(int robotId, float snapshotBattery)
{
    if (robotId == 0) {
        return 90;
    }
    if (robotId == 1) {
        return 70;
    }
    if (robotId == 2) {
        return 89;
    }
    if (robotId == 3) {
        return 91;
    }
    if (robotId == 4) {
        return 77;
    }
    return qBound(0, qRound(snapshotBattery), 100);
}

static QString robotMapLabel(int index)
{
    return QString("S%1").arg(index + 1, 2, 10, QLatin1Char('0'));
}

static bool isMapRobotId(int id)
{
    return id >= 0 && id < kMaxRobots;
}

static int markerStartRobotId(int robotId)
{
    if (robotId == 4) {
        return 0;
    }
    return robotId;
}

static float startThetaForRobot(const MapConfig &config, int robotId)
{
    if (robotId == 4) {
        return 0.0f;
    }

    const QVector<MapPoint> starts = config.starts();
    if (starts.isEmpty()) {
        return 0.0f;
    }
    const int startId = markerStartRobotId(robotId);
    if (startId >= 0 && startId < starts.size()) {
        return starts[startId].theta;
    }
    return starts.first().theta;
}

static bool isForcedFaultRobot(int id)
{
    Q_UNUSED(id);
    return false;
}

static quint64 monotonicNowUs()
{
    timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return static_cast<quint64>(ts.tv_sec) * 1000000ULL
        + static_cast<quint64>(ts.tv_nsec / 1000ULL);
}

static bool robotRecentlyActive(const RobotSnapshot &snapshot)
{
    constexpr quint64 kRobotActiveWindowUs = 5000000ULL;
    if (!snapshot.shmOpen || !snapshot.connected || snapshot.lastRxUs == 0) {
        return false;
    }
    const quint64 nowUs = monotonicNowUs();
    return nowUs >= snapshot.lastRxUs && nowUs - snapshot.lastRxUs <= kRobotActiveWindowUs;
}

static bool robotPoseAvailable(const RobotSnapshot &snapshot)
{
    return snapshot.shmOpen && (snapshot.odomTimestampUs != 0 || snapshot.odomSeq != 0);
}

static bool robotEverConnected(const RobotSnapshot &snapshot)
{
    return snapshot.lastRxUs != 0 || snapshot.rxPackets > 0 || snapshot.odomSeq != 0;
}

static bool robotConnectionLost(const RobotSnapshot &snapshot)
{
    return snapshot.shmOpen && !snapshot.connected && robotEverConnected(snapshot);
}

static bool globalPathRenderable(const RobotSnapshot &snapshot)
{
    return snapshot.shmOpen && snapshot.connected
        && snapshot.faultLevel < 3 && snapshot.globalPath.size() >= 2;
}

static bool hasHardcodedNormalParts(int id)
{
    return id == 0 || id == 1 || id == 2 || id == 3;
}

static bool hasHardcodedNormalPart(int id, int partIndex)
{
    if (id == 4 && partIndex == 2) {
        return true;
    }
    return hasHardcodedNormalParts(id) && partIndex >= 0 && partIndex <= 2;
}

static bool partStreamInactive(const RobotSnapshot &snapshot, int partIndex)
{
    return (partIndex == 0 && snapshot.imageFps <= 0.0f)
        || (partIndex == 1 && snapshot.lidarFps <= 0.0f);
}

static bool displayedPartsAreNormal(const RobotSnapshot &snapshot, int robotId)
{
    if (!snapshot.shmOpen || !snapshot.connected) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!hasHardcodedNormalPart(robotId, i) && partStreamInactive(snapshot, i)) {
            return false;
        }
    }
    return true;
}

static int displayedOfflinePartCount(const RobotSnapshot &snapshot, int robotId)
{
    if (!snapshot.shmOpen || !snapshot.connected) {
        return 3;
    }

    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!hasHardcodedNormalPart(robotId, i) && partStreamInactive(snapshot, i)) {
            ++count;
        }
    }
    return count;
}

static constexpr float kElevatedMapHeight = 0.20f;
static constexpr float kDefaultObstacleHeightMeters = 2.0f;
static constexpr float kBoundaryWallHeightMeters = 0.70f;
static constexpr float kMapMetersPerPixel = 0.05f;
static constexpr float kDefaultMapScale = 1.2f;
static constexpr float kRobotMarkerRadiusMeters = 0.23f;
static constexpr float kSelectedRobotMarkerRadiusMeters = 0.32f;
static constexpr float kRobotMarker2DRadiusMeters = 0.35f;
static constexpr float kSelectedRobotMarker2DRadiusMeters = 0.47f;
static constexpr float kPathDeviationWarnMeters = 2.00f;
static constexpr float kHeadingErrorWarnRad = 1.75f;
static constexpr float kLidarRenderZOffsetMeters = 0.18f;
static constexpr float kLidarMountForwardMeters = kSelectedRobotMarkerRadiusMeters;
static constexpr float kHeightColorRampMeters = 0.5f;
static constexpr float kMapHeightColorRampMeters = 1.40f;
static constexpr float kMapColorZMin = 0.0f;
static constexpr float kMapColorZMax = kMapColorZMin + kMapHeightColorRampMeters;
static constexpr qint64 kFakeSpeedHoldMs = 1000;

static bool isBoundaryWallObstacle(const MapRect &obs)
{
    const QString id = obs.id.toLower();
    if (id.startsWith(QStringLiteral("wall"))) {
        return true;
    }
    const float width = obs.xMax - obs.xMin;
    const float depth = obs.yMax - obs.yMin;
    return qMin(width, depth) <= 0.05f && qMax(width, depth) >= 1.0f;
}

static float variedObstacleHeightMeters(const MapRect &obs)
{
    if (obs.z > 0.0f) {
        return obs.z;
    }
    if (isBoundaryWallObstacle(obs)) {
        return kBoundaryWallHeightMeters;
    }
    if (obs.clearance <= 0.0f) {
        return 0.0f;
    }

    const int seed = qAbs(qHash(obs.id)
                         ^ qRound(obs.xMin * 37.0f)
                         ^ qRound(obs.yMin * 53.0f)
                         ^ qRound(obs.xMax * 71.0f)
                         ^ qRound(obs.yMax * 89.0f));
    const float variants[] = {-0.25f, 0.0f, 0.18f, 0.35f};
    return kDefaultObstacleHeightMeters + variants[seed % 4];
}
static constexpr qint64 kLidarFrameStaleMs = 1000;
static constexpr qint64 kRobotMarkerPulseMs = 1300;
static constexpr qint64 kRobotMarkerPulseEchoDelayMs = 220;
static constexpr qint64 kRobotMarkerHighlightTailMs = 180;
static constexpr qint64 kRobotWarningBlinkMs = 4200;
static constexpr qint64 kRobotWarningBlinkIntervalMs = 700;
static constexpr qint64 kRobotMoveCommandVisibleMs = 1000;
static constexpr qint64 kRobotMoveCommandFadeMs = 420;
static constexpr float kLidarVoxelMeters = 0.20f;
static constexpr float kLidar2DVoxelMeters = 0.10f;

struct LidarRobotPoint {
    float forward = 0.0f;
    float left = 0.0f;
    float up = 0.0f;
    float intensity = 0.0f;
};

static LidarRobotPoint lidarToRobotFrame(const LidarPoint &p)
{
    return {
        p.x,
        p.y,
        p.z + kLidarRenderZOffsetMeters,
        p.intensity
    };
}

static QColor robotPathColor(int id)
{
    static const QColor colors[] = {
        QColor(255, 105, 105),
        QColor(70, 190, 255),
        QColor(255, 195, 70),
        QColor(235, 105, 255),
        QColor(90, 240, 125),
    };
    return colors[qBound(0, id, 1024) % (sizeof(colors) / sizeof(colors[0]))];
}

static void drawWarningTriangle(QPainter &p, const QPointF &top, qreal size, qreal alphaScale = 1.0)
{
    const qreal h = size * 0.88;
    const QPointF left(top.x() - size * 0.5, top.y() + h);
    const QPointF right(top.x() + size * 0.5, top.y() + h);
    QPainterPath triangle;
    triangle.moveTo(top);
    triangle.lineTo(left);
    triangle.lineTo(right);
    triangle.closeSubpath();

    const int contrastAlpha = qRound(176.0 * alphaScale);
    const int haloAlpha = qRound(60.0 * alphaScale);
    const int glowAlpha = qRound(130.0 * alphaScale);
    const int fillAlpha = qRound(188.0 * alphaScale);
    const int edgeAlpha = qRound(252.0 * alphaScale);

    QColor contrast("#1b0710");
    contrast.setAlpha(contrastAlpha);
    p.setPen(QPen(contrast, qMax<qreal>(3.8, size * 0.13), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(triangle);

    QColor halo("#e71f3d");
    halo.setAlpha(haloAlpha);
    p.setPen(QPen(halo, qMax<qreal>(7.0, size * 0.23), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(triangle);

    QColor glow("#e63a42");
    glow.setAlpha(glowAlpha);
    p.setPen(QPen(glow, qMax<qreal>(3.8, size * 0.13), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(triangle);

    QLinearGradient fill(top, QPointF(top.x(), top.y() + h));
    fill.setColorAt(0.0, QColor(236, 92, 100, qRound(fillAlpha * 1.03)));
    fill.setColorAt(0.50, QColor(205, 34, 46, fillAlpha));
    fill.setColorAt(1.0, QColor(120, 12, 28, qRound(fillAlpha * 0.98)));
    p.setPen(QPen(QColor(238, 160, 168, edgeAlpha), qMax<qreal>(1.6, size * 0.065),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(triangle);

    p.setPen(QPen(QColor(218, 38, 50, qRound(245.0 * alphaScale)),
                  qMax<qreal>(1.1, size * 0.038),
                  Qt::SolidLine,
                  Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(triangle);

    p.setPen(QPen(QColor(236, 188, 194, qRound(120.0 * alphaScale)),
                  qMax<qreal>(0.8, size * 0.032),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(top + QPointF(0.0, h * 0.12), left + QPointF(size * 0.18, -h * 0.10));

    p.setPen(QPen(QColor(66, 10, 12, qRound(220.0 * alphaScale)),
                  qMax<qreal>(3.8, size * 0.180),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(top.x(), top.y() + h * 0.22),
               QPointF(top.x(), top.y() + h * 0.66));
    p.setPen(QPen(QColor(66, 10, 12, qRound(220.0 * alphaScale)),
                  qMax<qreal>(5.0, size * 0.245),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawPoint(QPointF(top.x(), top.y() + h * 0.81));

    p.setPen(QPen(QColor(255, 255, 255, qRound(252.0 * alphaScale)),
                  qMax<qreal>(2.7, size * 0.128),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(top.x(), top.y() + h * 0.22),
               QPointF(top.x(), top.y() + h * 0.66));
    const qreal baseSide = qMax<qreal>(3.4, size * 0.18);
    QRectF base(top.x() - baseSide * 0.5,
                top.y() + h * 0.76,
                baseSide,
                baseSide);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, qRound(252.0 * alphaScale)));
    p.drawRoundedRect(base, qMax<qreal>(0.8, baseSide * 0.14), qMax<qreal>(0.8, baseSide * 0.14));
}

static void drawRobotStatusWarningTriangle(QPainter &p, const QPointF &top, qreal size, qreal alphaScale = 1.0)
{
    const qreal h = size * 0.88;
    const QPointF left(top.x() - size * 0.5, top.y() + h);
    const QPointF right(top.x() + size * 0.5, top.y() + h);
    QPainterPath triangle;
    triangle.moveTo(top);
    triangle.lineTo(left);
    triangle.lineTo(right);
    triangle.closeSubpath();

    p.setPen(QPen(QColor(66, 10, 12, qRound(188.0 * alphaScale)),
                  qMax<qreal>(3.3, size * 0.118),
                  Qt::SolidLine,
                  Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(triangle);

    p.setPen(QPen(QColor(230, 31, 61, qRound(148.0 * alphaScale)),
                  qMax<qreal>(4.4, size * 0.162),
                  Qt::SolidLine,
                  Qt::RoundCap,
                  Qt::RoundJoin));
    p.drawPath(triangle);

    QLinearGradient fill(top, QPointF(top.x(), top.y() + h));
    fill.setColorAt(0.0, QColor(246, 108, 116, qRound(206.0 * alphaScale)));
    fill.setColorAt(0.50, QColor(218, 38, 52, qRound(204.0 * alphaScale)));
    fill.setColorAt(1.0, QColor(130, 12, 30, qRound(196.0 * alphaScale)));
    p.setPen(QPen(QColor(250, 178, 184, qRound(252.0 * alphaScale)),
                  qMax<qreal>(1.45, size * 0.058),
                  Qt::SolidLine,
                  Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(triangle);

    p.setPen(QPen(QColor(66, 10, 12, qRound(220.0 * alphaScale)),
                  qMax<qreal>(3.3, size * 0.152),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(top.x(), top.y() + h * 0.21),
               QPointF(top.x(), top.y() + h * 0.66));

    p.setPen(QPen(QColor(66, 10, 12, qRound(220.0 * alphaScale)),
                  qMax<qreal>(4.6, size * 0.218),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawPoint(QPointF(top.x(), top.y() + h * 0.80));

    p.setPen(QPen(QColor(255, 255, 255, qRound(252.0 * alphaScale)),
                  qMax<qreal>(2.35, size * 0.106),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawLine(QPointF(top.x(), top.y() + h * 0.21),
               QPointF(top.x(), top.y() + h * 0.66));

    p.setPen(QPen(QColor(255, 255, 255, qRound(252.0 * alphaScale)),
                  qMax<qreal>(3.0, size * 0.142),
                  Qt::SolidLine,
                  Qt::RoundCap));
    p.drawPoint(QPointF(top.x(), top.y() + h * 0.80));
}

class WarningTriangleOverlay : public QWidget
{
public:
    WarningTriangleOverlay(QLabel *anchorLabel, QWidget *host)
        : QWidget(host), m_anchorLabel(anchorLabel), m_host(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        hide();
        if (m_host) {
            m_host->installEventFilter(this);
        }
        if (m_anchorLabel) {
            m_anchorLabel->installEventFilter(this);
        }
        reposition();
    }

    void setActive(bool active)
    {
        if (m_active == active) {
            if (active) {
                reposition();
                raise();
            }
            return;
        }
        m_active = active;
        if (m_active) {
            m_startMs = QDateTime::currentMSecsSinceEpoch();
            if (m_timerId == 0) {
                m_timerId = startTimer(80);
            }
            reposition();
            raise();
            show();
        } else {
            if (m_timerId != 0) {
                killTimer(m_timerId);
                m_timerId = 0;
            }
            m_startMs = -1;
            hide();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_active || m_startMs < 0) {
            return;
        }
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startMs;
        if (elapsed < 0 || elapsed >= kRobotWarningBlinkIntervalMs * 6) {
            return;
        }
        if ((elapsed / kRobotWarningBlinkIntervalMs) % 2 != 0) {
            return;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawRobotStatusWarningTriangle(p, QPointF(width() * 0.5, m_topPadding), m_triangleSize, 0.92);
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_timerId) {
            if (m_active && m_startMs >= 0
                && QDateTime::currentMSecsSinceEpoch() - m_startMs >= kRobotWarningBlinkIntervalMs * 6) {
                if (m_timerId != 0) {
                    killTimer(m_timerId);
                    m_timerId = 0;
                }
                hide();
                update();
                return;
            }
            reposition();
            update();
            return;
        }
        QWidget::timerEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((watched == m_host || watched == m_anchorLabel)
            && (event->type() == QEvent::Resize
                || event->type() == QEvent::Move
                || event->type() == QEvent::Show)) {
            reposition();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void reposition()
    {
        if (!m_host || !m_anchorLabel) {
            return;
        }
        const QPoint labelTopLeft = m_anchorLabel->mapTo(m_host, QPoint(0, 0));
        const qreal panelW = qMax(1, m_host->width());
        const qreal panelH = qMax(1, m_host->height());
        m_triangleSize = qBound<qreal>(40.0, qMin(panelW * 0.20, panelH * 0.18), 58.0);
        m_topPadding = qMax<qreal>(5.0, m_triangleSize * 0.10);
        const qreal triHeight = m_triangleSize * 0.88;
        const int overlayW = qCeil(m_triangleSize * 1.34);
        const int overlayH = qCeil(triHeight + m_topPadding + m_triangleSize * 0.22);
        if (size() != QSize(overlayW, overlayH)) {
            setFixedSize(overlayW, overlayH);
        }

        const qreal textWidth = qMin<qreal>(
            m_anchorLabel->width(),
            m_anchorLabel->fontMetrics().horizontalAdvance(m_anchorLabel->text()));
        const qreal desiredCenterX = labelTopLeft.x() + textWidth + m_triangleSize * 0.78;
        const qreal desiredLeft = desiredCenterX - overlayW * 0.5;
        const qreal desiredTop = labelTopLeft.y() + m_anchorLabel->height() * 0.5
            - triHeight * 0.5 - m_topPadding;
        const qreal margin = qMax<qreal>(6.0, m_triangleSize * 0.10);
        const int x = qRound(qBound<qreal>(margin, desiredLeft, panelW - overlayW - margin));
        const int y = qRound(qBound<qreal>(margin, desiredTop, panelH - overlayH - margin));
        move(x, y);
    }

    QLabel *m_anchorLabel = nullptr;
    QWidget *m_host = nullptr;
    bool m_active = false;
    qint64 m_startMs = -1;
    int m_timerId = 0;
    qreal m_triangleSize = 56.0;
    qreal m_topPadding = 6.0;
};

static float missionProgressPercent(const RobotSnapshot &snapshot)
{
    return snapshot.missionProgress > 1.0f
        ? snapshot.missionProgress
        : snapshot.missionProgress * 100.0f;
}

static bool missionComplete(const RobotSnapshot &snapshot)
{
    return snapshot.goalReached || qRound(missionProgressPercent(snapshot)) >= 100;
}

static bool robotMissionStarted(const RobotSnapshot &snapshot)
{
    if (!robotPoseAvailable(snapshot)) {
        return false;
    }
    const float linearSpeed = std::hypot(snapshot.vx, snapshot.vy);
    return snapshot.commandMoving
        || linearSpeed > 0.02f
        || std::fabs(snapshot.omega) > 0.02f;
}

static bool hasPathProgressData(const RobotSnapshot &snapshot)
{
    return snapshot.pathOk || snapshot.poseOk || snapshot.goalReached
        || snapshot.waypointIdx > 0 || snapshot.missionProgress > 0.0f
        || snapshot.distanceToGoalM > 0.0f;
}

static bool pathTrackingAlert(const RobotSnapshot &snapshot)
{
    if (!snapshot.shmOpen || !snapshot.connected || !hasPathProgressData(snapshot)) {
        return false;
    }
    return snapshot.distanceToNearestM > kPathDeviationWarnMeters
        || std::fabs(snapshot.headingError) > kHeadingErrorWarnRad;
}

static QString robotOperationText(const RobotSnapshot &snapshot)
{
    if (!snapshot.shmOpen) {
        return QStringLiteral("미연결");
    }
    if (robotConnectionLost(snapshot)) {
        return QStringLiteral("연결 끊김");
    }
    if (!snapshot.connected) {
        return QStringLiteral("연결 안됨");
    }
    if (missionComplete(snapshot)) {
        return QStringLiteral("도착");
    }
    if (robotMissionStarted(snapshot)) {
        if (pathTrackingAlert(snapshot)) {
            return QStringLiteral("경로 점검");
        }
        return QStringLiteral("탐색 중");
    }
    return QStringLiteral("대기");
}

static QString targetPositionText(const RobotSnapshot &snapshot)
{
    if (!hasPathProgressData(snapshot)
        || !std::isfinite(snapshot.targetX)
        || !std::isfinite(snapshot.targetY)) {
        return QString("AREA_%1").arg(snapshot.id + 1);
    }
    return QString("%1, %2")
        .arg(snapshot.targetX, 0, 'f', 1)
        .arg(snapshot.targetY, 0, 'f', 1);
}

static QString progressText(const RobotSnapshot &snapshot)
{
    const int progress = qBound(0, qRound(missionProgressPercent(snapshot)), 100);
    if (hasPathProgressData(snapshot)
        && std::isfinite(snapshot.distanceToGoalM)
        && snapshot.distanceToGoalM > 0.0f) {
        return QString("%1% / %2 m")
            .arg(progress)
            .arg(snapshot.distanceToGoalM, 0, 'f', 1);
    }
    return QString("%1%").arg(progress);
}

static QPixmap makeRobotInfoIcon(const QString &type, int side = 24)
{
    QPixmap pix(side, side);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor glow(72, 176, 255, 70);
    const QColor stroke(138, 210, 255);
    const QColor fill(25, 73, 108, 115);
    const qreal s = side / 24.0;

    QPen glowPen(glow, 4.4 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen linePen(stroke, 1.8 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen dashPen(stroke, 1.6 * s, Qt::DashLine, Qt::FlatCap, Qt::RoundJoin);
    dashPen.setDashPattern({2.2, 2.2});

    if (type == QStringLiteral("mission")) {
        QPointF start(5.0 * s, 18.0 * s);
        QPointF mid(11.0 * s, 12.5 * s);
        QPointF end(16.6 * s, 8.0 * s);

        QPainterPath route;
        route.moveTo(start);
        route.cubicTo(QPointF(8.0 * s, 17.0 * s),
                      QPointF(7.8 * s, 12.0 * s),
                      mid);
        route.cubicTo(QPointF(13.2 * s, 10.0 * s),
                      QPointF(14.0 * s, 8.4 * s),
                      end);

        p.setPen(glowPen);
        p.drawPath(route);
        p.setPen(dashPen);
        p.drawPath(route);

        p.setPen(linePen);
        p.setBrush(fill);
        p.drawEllipse(start, 2.8 * s, 2.8 * s);
        p.drawEllipse(mid, 2.0 * s, 2.0 * s);
        p.setBrush(stroke);
        p.drawEllipse(start, 1.0 * s, 1.0 * s);

        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(16.8 * s, 5.8 * s), QPointF(16.8 * s, 13.2 * s));
        QPainterPath flag;
        flag.moveTo(17.2 * s, 6.1 * s);
        flag.lineTo(20.0 * s, 7.1 * s);
        flag.lineTo(17.2 * s, 8.1 * s);
        flag.closeSubpath();
        p.fillPath(flag, fill);
        p.drawPath(flag);
    } else if (type == QStringLiteral("parts")) {
        const QPointF center(12.0 * s, 12.0 * s);
        const qreal outer = 6.2 * s;
        const qreal inner = 3.3 * s;

        QPainterPath gear;
        for (int i = 0; i < 16; ++i) {
            const qreal angle = (-90.0 + i * 22.5) * M_PI / 180.0;
            const qreal r = (i % 2 == 0) ? outer : 5.0 * s;
            const QPointF pt(center.x() + std::cos(angle) * r,
                             center.y() + std::sin(angle) * r);
            if (i == 0) {
                gear.moveTo(pt);
            } else {
                gear.lineTo(pt);
            }
        }
        gear.closeSubpath();

        p.setPen(glowPen);
        p.drawPath(gear);
        p.setPen(linePen);
        p.setBrush(fill);
        p.drawPath(gear);
        p.setBrush(QColor(5, 13, 20));
        p.drawEllipse(center, inner, inner);
        p.setBrush(stroke);
        p.drawEllipse(center, 1.0 * s, 1.0 * s);
    } else {
        QRectF head(5.0 * s, 7.0 * s, 14.0 * s, 11.0 * s);

        p.setPen(glowPen);
        p.drawRoundedRect(head, 3.5 * s, 3.5 * s);
        p.setPen(linePen);
        p.setBrush(fill);
        p.drawRoundedRect(head, 3.5 * s, 3.5 * s);

        p.setBrush(stroke);
        p.drawEllipse(QPointF(9.0 * s, 12.0 * s), 1.1 * s, 1.1 * s);
        p.drawEllipse(QPointF(15.0 * s, 12.0 * s), 1.1 * s, 1.1 * s);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(12.0 * s, 7.0 * s), QPointF(12.0 * s, 4.5 * s));
        p.drawEllipse(QPointF(12.0 * s, 3.8 * s), 1.1 * s, 1.1 * s);
        p.drawLine(QPointF(8.0 * s, 18.0 * s), QPointF(8.0 * s, 20.0 * s));
        p.drawLine(QPointF(16.0 * s, 18.0 * s), QPointF(16.0 * s, 20.0 * s));
        p.drawLine(QPointF(9.0 * s, 15.3 * s), QPointF(15.0 * s, 15.3 * s));
    }

    return pix;
}

static float pathPulseProgress()
{
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return (ms % 2600) / 2600.0f;
}

static void drawPathGradientHighlight(QPainter &p,
                                      const QVector<QPointF> &pts,
                                      const QColor &pathColor,
                                      bool selected,
                                      float progress)
{
    if (pts.size() < 2) {
        return;
    }

    QVector<qreal> lengths;
    lengths.reserve(pts.size() - 1);
    qreal totalLength = 0.0;
    for (int i = 0; i < pts.size() - 1; ++i) {
        if (std::isnan(pts[i].x()) || std::isnan(pts[i + 1].x())) {
            lengths.append(0.0);
            continue;
        }
        const QPointF d = pts[i + 1] - pts[i];
        const qreal len = std::hypot(d.x(), d.y());
        lengths.append(len);
        totalLength += len;
    }
    if (totalLength <= 0.0) {
        return;
    }

    const qreal highlightLength = qBound<qreal>(34.0, totalLength * 0.28, 170.0);
    const qreal head = progress * (totalLength + highlightLength);
    const qreal startDistance = head - highlightLength;
    const qreal endDistance = head;
    const qreal coreWidth = selected ? 5.8 : 4.4;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setCompositionMode(QPainter::CompositionMode_Plus);

    const struct {
        qreal widthExtra;
        qreal alphaScale;
        int lighten;
    } glowLayers[] = {
        {14.0, 0.12, 125},
        {8.0,  0.22, 140},
        {3.0,  0.42, 160},
        {0.0,  0.68, 185},
    };

    for (const auto &layer : glowLayers) {
        qreal pathCursor = 0.0;
        for (int i = 0; i < pts.size() - 1; ++i) {
            const qreal segLength = lengths[i];
            if (std::isnan(pts[i].x()) || std::isnan(pts[i + 1].x())) {
                continue;
            }
            if (segLength <= 0.0) {
                continue;
            }

            const qreal segStart = pathCursor;
            const qreal segEnd = pathCursor + segLength;
            const qreal overlapStart = qMax(segStart, startDistance);
            const qreal overlapEnd = qMin(segEnd, endDistance);
            if (overlapEnd > overlapStart) {
                const int steps = qMax(8, qCeil((overlapEnd - overlapStart) / 2.5));
                for (int step = 0; step < steps; ++step) {
                    const qreal aDist = overlapStart + (overlapEnd - overlapStart) * step / steps;
                    const qreal bDist = overlapStart + (overlapEnd - overlapStart) * (step + 1) / steps;
                    const qreal a = (aDist - segStart) / segLength;
                    const qreal b = (bDist - segStart) / segLength;
                    const QPointF aPt = pts[i] + (pts[i + 1] - pts[i]) * a;
                    const QPointF bPt = pts[i] + (pts[i + 1] - pts[i]) * b;

                    const qreal mid = ((aDist + bDist) * 0.5 - startDistance) / highlightLength;
                    const qreal fade = qBound<qreal>(0.0, std::sin(mid * M_PI), 1.0);
                    const qreal intensity = fade * fade;
                    QColor hi = pathColor.lighter(qRound(layer.lighten + 28 * intensity));
                    const qreal maxAlpha = selected ? 150.0 : 105.0;
                    hi.setAlpha(qRound(maxAlpha * layer.alphaScale * intensity));
                    p.setPen(QPen(hi, coreWidth + layer.widthExtra, Qt::SolidLine,
                                  Qt::FlatCap, Qt::RoundJoin));
                    p.drawLine(aPt, bPt);
                }
            }

            pathCursor = segEnd;
        }
    }

    p.restore();
}

static bool rangesOverlap(float aMin, float aMax, float bMin, float bMax)
{
    return qMin(aMax, bMax) > qMax(aMin, bMin);
}

static QVector<MapRect> connectedAreaRects(const MapConfig &config)
{
    QVector<MapRect> rects = config.areas();
    const QVector<MapRect> areas = config.areas();

    for (int i = 0; i < areas.size(); ++i) {
        for (int j = i + 1; j < areas.size(); ++j) {
            const MapRect &a = areas[i];
            const MapRect &b = areas[j];

            if (rangesOverlap(a.xMin, a.xMax, b.xMin, b.xMax)) {
                const float gapMin = qMin(a.yMax, b.yMax);
                const float gapMax = qMax(a.yMin, b.yMin);
                if (gapMax > gapMin) {
                    MapRect bridge;
                    bridge.id = "connector";
                    bridge.xMin = qMax(a.xMin, b.xMin);
                    bridge.xMax = qMin(a.xMax, b.xMax);
                    bridge.yMin = gapMin;
                    bridge.yMax = gapMax;
                    rects.append(bridge);
                }
            }

            if (rangesOverlap(a.yMin, a.yMax, b.yMin, b.yMax)) {
                const float gapMin = qMin(a.xMax, b.xMax);
                const float gapMax = qMax(a.xMin, b.xMin);
                if (gapMax > gapMin) {
                    MapRect bridge;
                    bridge.id = "connector";
                    bridge.xMin = gapMin;
                    bridge.xMax = gapMax;
                    bridge.yMin = qMax(a.yMin, b.yMin);
                    bridge.yMax = qMin(a.yMax, b.yMax);
                    rects.append(bridge);
                }
            }
        }
    }

    return rects;
}

QFrame *makePanel(const QString &title, QWidget *body)
{
    QFrame *panel = new QFrame;
    panel->setObjectName("panel");
    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);

    QLabel *label = new QLabel(title);
    label->setObjectName("panelTitle");
    layout->addWidget(label);
    layout->addWidget(body, 1);
    return panel;
}

QLabel *makeMetric(const QString &title, const QString &value, const QString &color)
{
    QLabel *label = new QLabel(QString("<span style='color:#8a96a3'>%1</span><br><b style='color:%2'>%3</b>")
                               .arg(title, color, value));
    label->setObjectName("metric");
    label->setTextFormat(Qt::RichText);
    return label;
}

class VideoStandbyLabel : public QLabel
{
public:
    explicit VideoStandbyLabel(const QString &text, QWidget *parent = nullptr)
        : QLabel(text, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (text().isEmpty()) {
            QLabel::paintEvent(event);
            return;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect();

        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor("#08151d"));
        bg.setColorAt(0.55, QColor("#040b10"));
        bg.setColorAt(1.0, QColor("#020507"));
        p.fillRect(r, bg);

        p.setPen(QPen(QColor(64, 103, 126, 20), 1.0));
        const int grid = qMax(42, qMin(width(), height()) / 3);
        for (int x = 0; x <= width(); x += grid) {
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (int y = 0; y <= height(); y += grid) {
            p.drawLine(QPointF(0, y), QPointF(width(), y));
        }

        const qreal corner = qMin(width(), height()) * 0.12;
        const QRectF frame = r.adjusted(18, 18, -18, -18);
        p.setPen(QPen(QColor(46, 96, 125, 145), 1.4));
        p.drawLine(frame.topLeft(), frame.topLeft() + QPointF(corner, 0));
        p.drawLine(frame.topLeft(), frame.topLeft() + QPointF(0, corner));
        p.drawLine(frame.topRight(), frame.topRight() - QPointF(corner, 0));
        p.drawLine(frame.topRight(), frame.topRight() + QPointF(0, corner));
        p.drawLine(frame.bottomLeft(), frame.bottomLeft() + QPointF(corner, 0));
        p.drawLine(frame.bottomLeft(), frame.bottomLeft() - QPointF(0, corner));
        p.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(corner, 0));
        p.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(0, corner));

        const QPointF c = r.center();
        const qreal iconW = qMin<qreal>(92.0, qMax<qreal>(54.0, width() * 0.18));
        const QRectF body(c.x() - iconW * 0.42, c.y() - iconW * 0.34, iconW * 0.84, iconW * 0.52);
        p.setPen(QPen(QColor(118, 174, 204, 88), 5.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(body, 6, 6);
        p.setPen(QPen(QColor("#7fb7d4"), 2.0));
        p.setBrush(QColor(28, 52, 72, 74));
        p.drawRoundedRect(body, 6, 6);
        p.drawEllipse(body.center(), body.height() * 0.20, body.height() * 0.20);
        QPolygonF lens;
        lens << QPointF(body.right(), body.center().y() - body.height() * 0.22)
             << QPointF(body.right() + iconW * 0.24, body.top() + body.height() * 0.10)
             << QPointF(body.right() + iconW * 0.24, body.bottom() - body.height() * 0.10)
             << QPointF(body.right(), body.center().y() + body.height() * 0.22);
        p.drawPolygon(lens);

        const QStringList lines = text().split(QLatin1Char('\n'));
        QRectF textRect(c.x() - 150, c.y() + iconW * 0.20, 300, 56);
        p.setFont(QFont("Noto Sans", 12, QFont::Black));
        p.setPen(QColor("#dce7f3"));
        p.drawText(textRect.adjusted(0, 0, 0, -25),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   lines.value(0));
        p.setFont(QFont("Noto Sans", 9, QFont::Bold));
        p.setPen(QColor("#7f91a1"));
        p.drawText(textRect.adjusted(0, 25, 0, 0),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   lines.value(1));

    }
};

VideoTile::VideoTile(int robotId, QWidget *parent)
    : QFrame(parent), m_robotId(robotId)
{
    setObjectName("videoTile");
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_image = new VideoStandbyLabel(QStringLiteral("영상 대기\n스트림 연결 대기"));
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setObjectName("videoImage");
    m_image->setMinimumSize(110, 72);
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_image->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_title = new QLabel(robotName(robotId));
    m_title->setObjectName("videoTitle");
    m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title->setParent(m_image);
    m_title->move(12, 8);

    m_badge = new QLabel;
    m_badge->setObjectName("videoBadge");
    m_badge->setText(QStringLiteral("STANDBY"));
    m_badge->setProperty("state", "wait");
    m_badge->setParent(m_image);
    m_badge->move(12, 38);
    m_badge->adjustSize();

    layout->addWidget(m_image, 1);
}

QSize VideoTile::sizeHint() const
{
    return QSize(320, 190);
}

QSize VideoTile::minimumSizeHint() const
{
    return QSize(110, 72);
}

void VideoTile::setRobotId(int robotId)
{
    m_robotId = robotId;
    m_title->setText(robotName(m_robotId));
    m_title->adjustSize();
}

void VideoTile::setSnapshot(const RobotSnapshot &snapshot)
{
    constexpr qint64 kInitialVideoWaitMs = 4000;

    m_title->setText(robotName(m_robotId));
    const bool hasFrame = !snapshot.image.isNull();
    if (hasFrame || !m_noFrameTimer.isValid()) {
        m_noFrameTimer.restart();
    }

    const bool waiting = !hasFrame && m_noFrameTimer.elapsed() < kInitialVideoWaitMs;
    const bool noSignal = !hasFrame && !waiting;
    m_badge->setText(hasFrame ? "LIVE" : (waiting ? "WAIT" : "NO SIGNAL"));
    m_badge->setProperty("state", noSignal ? "nosignal" : (waiting ? "wait" : QString()));
    m_badge->style()->unpolish(m_badge);
    m_badge->style()->polish(m_badge);
    m_badge->adjustSize();

    if (hasFrame) {
        m_image->setPixmap(QPixmap::fromImage(snapshot.image).scaled(m_image->size(),
                                                                      Qt::KeepAspectRatioByExpanding,
                                                                      Qt::SmoothTransformation));
        m_image->setText(QString());
    } else {
        m_image->setPixmap(QPixmap());
        m_image->setText(waiting ? QStringLiteral("영상 대기\n스트림 연결 대기")
                                 : QStringLiteral("NO SIGNAL\n스트림 미수신"));
    }
}

int VideoTile::resizeEdgeAt(const QPoint &pos) const
{
    constexpr int edgeMargin = 14;
    int edgeMask = NoEdge;
    if (pos.x() <= edgeMargin) {
        edgeMask |= LeftEdge;
    }
    if (pos.x() >= width() - edgeMargin) {
        edgeMask |= RightEdge;
    }
    if (pos.y() <= edgeMargin) {
        edgeMask |= TopEdge;
    }
    if (pos.y() >= height() - edgeMargin) {
        edgeMask |= BottomEdge;
    }
    return edgeMask;
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int edgeMask = resizeEdgeAt(event->pos());
        if (edgeMask != NoEdge) {
            emit shrinkRequested(m_robotId, edgeMask);
            event->accept();
            return;
        }
    }
    QFrame::mouseDoubleClickEvent(event);
}

void VideoTile::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        if ((event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragging = true;
            setCursor(m_pressEdgeMask == NoEdge ? Qt::ClosedHandCursor : Qt::SizeAllCursor);
        }
        if (m_dragging) {
            const QPoint globalPos = event->globalPosition().toPoint();
            if (!m_hasLastDragPreview
                || (globalPos - m_lastDragPreviewGlobalPos).manhattanLength() >= 1) {
                if (m_pressEdgeMask != NoEdge) {
                    emit resizeRequested(m_robotId, m_pressEdgeMask, globalPos);
                } else {
                    emit moveRequested(m_robotId, globalPos);
                }
                m_lastDragPreviewGlobalPos = globalPos;
                m_hasLastDragPreview = true;
            }
        }
    } else {
        const int edgeMask = resizeEdgeAt(event->pos());
        if ((edgeMask & (LeftEdge | RightEdge)) && (edgeMask & (TopEdge | BottomEdge))) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edgeMask & (LeftEdge | RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edgeMask & (TopEdge | BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            setCursor(Qt::PointingHandCursor);
        }
    }
    QFrame::mouseMoveEvent(event);
}

void VideoTile::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_pressEdgeMask = resizeEdgeAt(event->pos());
        m_dragging = false;
        m_hasLastDragPreview = false;
    }
    QFrame::mousePressEvent(event);
}

void VideoTile::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_dragging && m_pressEdgeMask != NoEdge) {
            emit resizeRequested(m_robotId, m_pressEdgeMask, event->globalPosition().toPoint());
        } else if (m_dragging) {
            emit moveRequested(m_robotId, event->globalPosition().toPoint());
        } else if (rect().contains(event->pos())) {
            emit clicked(m_robotId);
        }
    }
    m_dragging = false;
    m_pressEdgeMask = NoEdge;
    m_hasLastDragPreview = false;
    setCursor(Qt::PointingHandCursor);
    QFrame::mouseReleaseEvent(event);
}

class SignalBarsWidget : public QWidget
{
public:
    explicit SignalBarsWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(34, 22);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int level = qBound(0, property("level").toInt(), 4);
        const bool danger = property("danger").toBool();
        const QColor active = danger ? QColor("#ff453a") : QColor("#16d968");
        const QColor inactive("#263744");
        const int barWidth = 4;
        const int gap = 3;
        const int baseY = height() - 3;
        for (int i = 0; i < 4; ++i) {
            const int h = 5 + i * 4;
            QRectF bar(4 + i * (barWidth + gap), baseY - h, barWidth, h);
            p.setPen(Qt::NoPen);
            p.setBrush(i < level ? active : inactive);
            p.drawRoundedRect(bar, 1.4, 1.4);
        }
    }
};

static QString statusRowMissionText(const RobotSnapshot &snapshot, bool forcedFault)
{
    Q_UNUSED(forcedFault);
    if (!snapshot.shmOpen) {
        return QStringLiteral("미연결");
    }
    if (robotConnectionLost(snapshot)) {
        return QStringLiteral("연결 끊김");
    }
    if (!snapshot.connected) {
        return QStringLiteral("연결 안됨");
    }
    return robotOperationText(snapshot);
}

StatusRow::StatusRow(int robotId, QWidget *parent)
    : QFrame(parent), m_robotId(robotId)
{
    setObjectName("statusRow");
    setMinimumHeight(30);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(10, 3, 10, 3);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(0);
    layout->setColumnMinimumWidth(0, 82);
    layout->setColumnMinimumWidth(1, 206);
    layout->setColumnMinimumWidth(2, 106);
    layout->setColumnMinimumWidth(3, 48);
    layout->setColumnStretch(0, 5);
    layout->setColumnStretch(1, 9);
    layout->setColumnStretch(2, 5);
    layout->setColumnStretch(3, 4);

    m_name = new QLabel(robotName(robotId));
    m_name->setObjectName("statusRobotName");
    m_name->setProperty("state", "offline");
    m_name->setMinimumWidth(82);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QWidget *batteryCell = new QWidget;
    batteryCell->setObjectName("statusBatteryCell");
    batteryCell->setFixedWidth(206);
    batteryCell->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *batteryLayout = new QHBoxLayout(batteryCell);
    batteryLayout->setContentsMargins(0, 0, 0, 0);
    batteryLayout->setSpacing(8);
    m_batteryText = new QLabel("--");
    m_batteryText->setObjectName("statusBatteryText");
    m_batteryText->setProperty("state", "offline");
    m_batteryText->setFixedWidth(28);
    m_batteryText->setAlignment(Qt::AlignCenter);
    m_batteryBar = new AnimatedProgressBar;
    m_batteryBar->setObjectName("statusBatteryBar");
    m_batteryBar->setProperty("state", "offline");
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setTextVisible(false);
    m_batteryBar->setFixedWidth(170);
    m_batteryBar->setMaximumHeight(10);
    batteryLayout->addWidget(m_batteryText);
    batteryLayout->addWidget(m_batteryBar, 1);

    m_mission = new QLabel(QStringLiteral("미투입"));
    m_mission->setObjectName("statusMissionChip");
    m_mission->setProperty("state", "offline");
    m_mission->setAlignment(Qt::AlignCenter);
    m_mission->setFixedSize(86, 22);
    m_mission->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_signal = new SignalBarsWidget;
    m_signal->setProperty("level", 0);

    layout->addWidget(m_name, 0, 0, Qt::AlignCenter);
    layout->addWidget(batteryCell, 0, 1, Qt::AlignCenter);
    layout->addWidget(m_mission, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_signal, 0, 3, Qt::AlignRight | Qt::AlignVCenter);
}

void StatusRow::setSnapshot(const RobotSnapshot &snapshot)
{
    const bool forcedFault = isForcedFaultRobot(m_robotId);
    const int offlinePartCount = displayedOfflinePartCount(snapshot, m_robotId);
    const bool offline = !snapshot.shmOpen || !snapshot.connected || offlinePartCount >= 3;
    if (snapshot.shmOpen && (snapshot.connected || robotEverConnected(snapshot))) {
        m_everConnected = true;
    }
    const bool connectionLost = snapshot.shmOpen && !snapshot.connected && m_everConnected;
    const int battery = displayBatteryPercent(m_robotId, snapshot.battery);
    const bool started = !offline && robotMissionStarted(snapshot);
    const bool complete = !offline && missionComplete(snapshot);
    const bool pathAlert = started && !complete && pathTrackingAlert(snapshot);
    const bool danger = !offline
        && (offlinePartCount >= 2 || forcedFault || snapshot.faultLevel >= 3 || battery < 20);
    const bool warning = !offline && !danger && (offlinePartCount == 1 || pathAlert);
    const bool moving = started && !complete && !danger && !warning;
    const QString stateName = connectionLost ? QStringLiteral("danger")
                                      : (offline ? QStringLiteral("offline")
                                      : (danger ? QStringLiteral("danger")
                                                : (warning ? QStringLiteral("warning")
                                                             : (moving ? QStringLiteral("moving") : QStringLiteral("normal")))));
    int signalLevel = 0;
    if (!offline) {
        signalLevel = 4;
        if (snapshot.linkRttMs > 150.0f || snapshot.dropRate > 5.0f) {
            signalLevel = 1;
        } else if (snapshot.linkRttMs > 100.0f || snapshot.dropRate > 2.0f) {
            signalLevel = 2;
        } else if (snapshot.linkRttMs > 60.0f || snapshot.dropRate > 0.5f) {
            signalLevel = 3;
        }
    }

    m_name->setText(robotName(m_robotId));
    m_name->setProperty("state", stateName);
    m_name->style()->unpolish(m_name);
    m_name->style()->polish(m_name);

    m_batteryText->setText(offline ? QStringLiteral("--") : QString("%1%").arg(battery));
    m_batteryText->setProperty("state", stateName);
    m_batteryText->style()->unpolish(m_batteryText);
    m_batteryText->style()->polish(m_batteryText);

    m_batteryBar->setProperty("state", stateName);
    m_batteryBar->style()->unpolish(m_batteryBar);
    m_batteryBar->style()->polish(m_batteryBar);
    setProgressBarValueAnimated(m_batteryBar, offline ? 0 : battery);

    const QString missionStateText = connectionLost ? QStringLiteral("연결 끊김")
        : (offlinePartCount >= 3 ? QStringLiteral("연결 안됨")
        : (danger ? QStringLiteral("위험")
                  : (offlinePartCount == 1 ? QStringLiteral("경고")
                                            : statusRowMissionText(snapshot, forcedFault))));
    m_mission->setText(missionStateText);
    m_mission->setProperty("state", stateName);
    m_mission->style()->unpolish(m_mission);
    m_mission->style()->polish(m_mission);

    m_signal->setProperty("level", signalLevel);
    m_signal->setProperty("danger", danger);
    m_signal->update();
}

class SpotMicro3DView : public QWidget
{
public:
    explicit SpotMicro3DView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(180, 130);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::OpenHandCursor);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    void setSnapshot(const RobotSnapshot &snapshot, const QColor &accent)
    {
        m_snapshot = snapshot;
        m_accent = accent;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(0, 7, 7));

        const QColor gridColor(126, 227, 95, 34);
        p.setPen(QPen(gridColor, 1));
        for (int y = 0; y < height(); y += 7) {
            p.drawLine(0, y, width(), y);
        }
        p.setPen(QPen(gridColor, 1));
        for (int x = 0; x < width(); x += 26) {
            p.drawLine(x, 0, x, height());
        }

        QVector<DrawItem> items;
        const QColor yellow("#caa43a");
        const QColor yellowLight("#d8b94c");
        const QColor black("#111313");
        const QColor fault = QColor("#ff453a");
        const uint32_t code = m_snapshot.faultCode;
        const bool batteryFault = m_snapshot.battery < 20.0f || (code & 0x01u);
        const bool commFault = m_snapshot.linkRttMs > 85.0f || (code & 0x02u);
        const bool frontLegFault = (code & 0x04u);
        const bool rearLegFault = (code & 0x08u);
        const bool bodyFault = (m_snapshot.faultLevel >= 3 && code == 0 && !batteryFault) || (code & 0x10u);

        addBox(items, {0.00f, 0.48f, 0.00f}, {1.78f, 0.34f, 0.58f}, bodyFault ? fault : yellow);
        addBox(items, {0.82f, 0.48f, 0.00f}, {0.44f, 0.38f, 0.62f}, commFault ? fault : yellowLight);
        addBox(items, {-0.88f, 0.48f, 0.00f}, {0.32f, 0.38f, 0.62f}, yellow.darker(108));
        addBox(items, {-0.10f, 0.73f, 0.00f}, {1.30f, 0.08f, 0.46f}, batteryFault ? fault : yellowLight);
        addBox(items, {-0.04f, 0.31f, -0.32f}, {0.95f, 0.12f, 0.05f}, black);
        addBox(items, {1.06f, 0.47f, 0.00f}, {0.035f, 0.22f, 0.34f}, commFault ? fault.darker(120) : QColor("#050606"));

        const std::array<LegAnchor, 4> legs = {{
            {-0.58f, -0.34f, true},
            {-0.58f,  0.34f, true},
            { 0.58f, -0.34f, false},
            { 0.58f,  0.34f, false}
        }};
        for (const LegAnchor &leg : legs) {
            const bool isFault = leg.rear ? rearLegFault : frontLegFault;
            const QColor limb = isFault ? fault : black;
            const QColor shoulder = isFault ? fault : yellow;
            const float side = leg.z < 0.0f ? -1.0f : 1.0f;
            const float stride = leg.rear ? -0.14f : 0.12f;
            const Vec3 hip{leg.x, 0.30f, leg.z};
            const Vec3 knee{leg.x + stride, -0.10f, leg.z + side * 0.10f};
            const Vec3 ankle{leg.x + stride * 0.25f, -0.55f, leg.z + side * 0.18f};
            const Vec3 toe{ankle.x + 0.18f, -0.58f, ankle.z + side * 0.03f};
            addBox(items, {leg.x, 0.29f, leg.z}, {0.26f, 0.22f, 0.20f}, shoulder);
            addLine(items, hip, knee, limb, 8.0f);
            addLine(items, knee, ankle, limb, 8.0f);
            addLine(items, ankle, toe, limb, 6.0f);
            addBox(items, toe, {0.18f, 0.07f, 0.10f}, limb);
        }

        std::sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b) {
            return a.depth < b.depth;
        });

        for (const DrawItem &item : items) {
            if (item.kind == DrawItem::Line) {
                p.setPen(QPen(QColor(item.color.red(), item.color.green(), item.color.blue(), 80),
                              item.width + 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(item.a, item.b);
                p.setPen(QPen(item.color, item.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(item.a, item.b);
            } else {
                p.setPen(QPen(item.color.lighter(130), 1));
                p.setBrush(QColor(item.color.red(), item.color.green(), item.color.blue(), 185));
                p.drawPolygon(item.poly);
            }
        }

        p.setPen(QPen(QColor(126, 227, 95, 115), 1));
        p.drawText(QRect(0, height() - 18, width(), 16), Qt::AlignCenter, "drag to rotate");
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_lastMouse = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint delta = event->pos() - m_lastMouse;
        m_lastMouse = event->pos();
        m_yaw += delta.x() * 0.012f;
        m_pitch = qBound(-0.70f, m_pitch + delta.y() * 0.010f, 0.75f);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        setCursor(Qt::OpenHandCursor);
    }

private:
    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct LegAnchor {
        float x;
        float z;
        bool rear;
    };

    struct DrawItem {
        enum Kind { Face, Line } kind = Face;
        QPolygonF poly;
        QPointF a;
        QPointF b;
        float depth = 0.0f;
        qreal width = 1.0;
        QColor color;
    };

    Vec3 rotate(Vec3 v) const
    {
        const float cy = std::cos(m_yaw);
        const float sy = std::sin(m_yaw);
        const float cp = std::cos(m_pitch);
        const float sp = std::sin(m_pitch);
        const float x1 = v.x * cy + v.z * sy;
        const float z1 = -v.x * sy + v.z * cy;
        const float y1 = v.y * cp - z1 * sp;
        const float z2 = v.y * sp + z1 * cp;
        return {x1, y1, z2};
    }

    QPointF project(Vec3 v) const
    {
        const Vec3 r = rotate(v);
        const float scale = qMin(width(), height()) * 1.42f;
        const float depth = 3.9f + r.z;
        const float f = scale / qMax(1.2f, depth);
        return QPointF(width() * 0.50f + r.x * f, height() * 0.56f - r.y * f);
    }

    float depthOf(Vec3 v) const
    {
        return rotate(v).z;
    }

    void addLine(QVector<DrawItem> &items, Vec3 a, Vec3 b, QColor color, qreal width)
    {
        DrawItem item;
        item.kind = DrawItem::Line;
        item.a = project(a);
        item.b = project(b);
        item.depth = (depthOf(a) + depthOf(b)) * 0.5f;
        item.color = color;
        item.width = width;
        items.append(item);
    }

    void addBox(QVector<DrawItem> &items, Vec3 center, Vec3 size, QColor color)
    {
        const float hx = size.x * 0.5f;
        const float hy = size.y * 0.5f;
        const float hz = size.z * 0.5f;
        const std::array<Vec3, 8> v = {{
            {center.x - hx, center.y - hy, center.z - hz},
            {center.x + hx, center.y - hy, center.z - hz},
            {center.x + hx, center.y + hy, center.z - hz},
            {center.x - hx, center.y + hy, center.z - hz},
            {center.x - hx, center.y - hy, center.z + hz},
            {center.x + hx, center.y - hy, center.z + hz},
            {center.x + hx, center.y + hy, center.z + hz},
            {center.x - hx, center.y + hy, center.z + hz}
        }};
        static const int idx[6][4] = {
            {0, 1, 2, 3},
            {4, 7, 6, 5},
            {0, 4, 5, 1},
            {3, 2, 6, 7},
            {1, 5, 6, 2},
            {0, 3, 7, 4}
        };
        for (int f = 0; f < 6; ++f) {
            DrawItem item;
            item.kind = DrawItem::Face;
            item.color = (f == 3) ? color.lighter(122) : ((f == 2 || f == 5) ? color.darker(120) : color);
            float depth = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const Vec3 pt = v[idx[f][i]];
                item.poly << project(pt);
                depth += depthOf(pt);
            }
            item.depth = depth / 4.0f;
            items.append(item);
        }
    }

    QPoint m_lastMouse;
    QColor m_accent = QColor("#7ee35f");
    RobotSnapshot m_snapshot;
    float m_yaw = -0.62f;
    float m_pitch = 0.28f;
};

class SpotRobotImageView : public QLabel
{
public:
    explicit SpotRobotImageView(QWidget *parent = nullptr)
        : QLabel(parent), m_source(":/assets/spot_robot.png")
    {
        setObjectName("robotImageView");
        setAlignment(Qt::AlignCenter);
        setMinimumSize(220, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setPixmapForSize();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setPixmapForSize();
    }

private:
    void setPixmapForSize()
    {
        if (m_source.isNull() || width() <= 0 || height() <= 0) {
            return;
        }
        setPixmap(m_source.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QPixmap m_source;
};

class SpotStl3DView : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    explicit SpotStl3DView(QWidget *parent = nullptr)
        : QOpenGLWidget(parent)
    {
        setObjectName("robotStlView");
        setMinimumSize(220, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::OpenHandCursor);
        QSurfaceFormat fmt;
        fmt.setSamples(4);
        setFormat(fmt);
        loadRobotMeshes();
        m_spinTimerId = startTimer(50);
    }

    ~SpotStl3DView() override
    {
        if (context()) {
            makeCurrent();
            m_vbo.destroy();
            m_lineVBO.destroy();
            doneCurrent();
        }
    }

    void setSnapshot(const RobotSnapshot &snapshot, const QString &stateName)
    {
        Q_UNUSED(snapshot);
        const QVector3D tint = stateName == QStringLiteral("danger")
            ? QVector3D(0.95f, 0.18f, 0.15f)
            : (stateName == QStringLiteral("warning")
                   ? QVector3D(0.95f, 0.70f, 0.16f)
            : (stateName == QStringLiteral("offline")
                   ? QVector3D(0.42f, 0.48f, 0.54f)
                   : QVector3D(0.12f, 0.78f, 0.32f)));
        if (m_statusTint != tint) {
            m_statusTint = tint;
            m_outlineCacheValid = false;
            update();
        }
    }

    void setConnectionWarningActive(bool active)
    {
        if (active && !m_connectionWarningActive) {
            m_connectionWarningStartMs = QDateTime::currentMSecsSinceEpoch();
        } else if (!active) {
            m_connectionWarningStartMs = -1;
        }
        if (m_connectionWarningActive != active) {
            m_connectionWarningActive = active;
            update();
        }
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_TRUE);

        static const char *vertSrc = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec3 aColor;
uniform mat4 uMvp;
uniform float uWireMode;
varying vec3 vNormal;
varying vec3 vColor;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vNormal = aNormal;
    vColor = aColor;
}
)";

        static const char *fragSrc = R"(
varying vec3 vNormal;
varying vec3 vColor;
uniform vec3 uTint;
uniform float uWireMode;
void main() {
    vec3 n = normalize(vNormal);
    vec3 lightA = normalize(vec3(0.45, -0.55, 0.75));
    vec3 lightB = normalize(vec3(-0.65, 0.25, 0.35));
    float d = max(dot(n, lightA), 0.0) * 0.74 + max(dot(n, lightB), 0.0) * 0.26;
    vec3 neon = min(vColor * 1.12 + uTint * 0.10, vec3(1.0, 1.0, 1.0));
    vec3 fill = vColor * (0.24 + d * 0.22) + uTint * 0.05;
    if (uWireMode > 1.5) {
        gl_FragColor = vec4(neon, 0.08);
    } else if (uWireMode > 0.5) {
        gl_FragColor = vec4(neon, 0.50);
    } else {
        gl_FragColor = vec4(fill, 0.42);
    }
}
)";

        m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc);
        m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc);
        m_prog.link();
        m_locMvp = m_prog.uniformLocation("uMvp");
        m_locTint = m_prog.uniformLocation("uTint");
        m_locWireMode = m_prog.uniformLocation("uWireMode");

        m_vbo.create();
        m_lineVBO.create();
        m_glReady = true;
        uploadMesh();
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (m_partOutlines.isEmpty()) {
            return;
        }

        QMatrix4x4 proj;
        QMatrix4x4 view;
        QMatrix4x4 model;
        const float aspect = height() > 0 ? width() / static_cast<float>(height()) : 1.0f;
        proj.perspective(38.0f, aspect, 0.05f, 50.0f);
        view.lookAt(QVector3D(0.0f, -2.80f, 0.36f + m_zoom),
                    QVector3D(0.0f, 0.0f, 0.02f),
                    QVector3D(0.0f, 0.0f, 1.0f));
        model.rotate(m_pitchDeg, 1.0f, 0.0f, 0.0f);
        model.rotate(m_yawDeg, 0.0f, 0.0f, 1.0f);
        const QMatrix4x4 mvp = proj * view * model;

        m_lastMvp = mvp;
        m_haveLastMvp = true;
    }

    void paintEvent(QPaintEvent *event) override
    {
        QOpenGLWidget::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawPartOutlines(&p);
        drawConnectionWarning(&p);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_lastMouse = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint delta = event->pos() - m_lastMouse;
        m_lastMouse = event->pos();
        m_yawDeg += delta.x() * 0.45f;
        m_pitchDeg = qBound(-68.0f, m_pitchDeg + delta.y() * 0.35f, 68.0f);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        setCursor(Qt::OpenHandCursor);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        m_zoom = qBound(-0.35f, m_zoom - event->angleDelta().y() * 0.0008f, 0.95f);
        update();
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_spinTimerId && !underMouse()) {
            m_yawDeg += 0.18f;
            update();
            return;
        }
        QOpenGLWidget::timerEvent(event);
    }

private:
    struct Vertex {
        float x;
        float y;
        float z;
        float nx;
        float ny;
        float nz;
        float r;
        float g;
        float b;
    };

    struct MeshInstance {
        QString file;
        QVector3D color;
        QVector3D translateMm;
    };

    struct PartOutline {
        QVector<QVector3D> points;
    };

    static float readFloat(const char *ptr)
    {
        float value = 0.0f;
        std::memcpy(&value, ptr, sizeof(float));
        return value;
    }

    QString findStlRoot() const
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString cwd = QDir::currentPath();
        const QStringList candidates = {
            QDir(appDir).absoluteFilePath("../../stl_data/Parts"),
            QDir(appDir).absoluteFilePath("../../../stl_data/Parts"),
            QDir(cwd).absoluteFilePath("stl_data/Parts"),
            QDir(cwd).absoluteFilePath("../stl_data/Parts"),
            QStringLiteral("/home/pi/robot_project/stl_data/Parts")
        };
        for (const QString &path : candidates) {
            QFileInfo info(path);
            if (info.exists() && info.isDir()) {
                return info.canonicalFilePath();
            }
        }
        return QString();
    }

    bool appendBinaryStl(const QString &path, const QVector3D &color, const QVector3D &translateMm)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QByteArray data = file.readAll();
        if (data.size() < 84) {
            return false;
        }
        quint32 triCount = 0;
        std::memcpy(&triCount, data.constData() + 80, sizeof(quint32));
        if (data.size() < 84 + static_cast<int>(triCount) * 50) {
            return false;
        }

        auto pushEdge = [this](const Vertex &a, const Vertex &b) {
            m_lineVertices.append(a);
            m_lineVertices.append(b);
        };

        PartOutline outline;
        const quint32 surfaceStride = qMax<quint32>(1, (triCount + 7999u) / 8000u);
        const quint32 lineStride = qMax<quint32>(1, (triCount + 179u) / 180u);
        const quint32 outlineStride = qMax<quint32>(1, (triCount + 179u) / 180u);

        for (quint32 i = 0; i < triCount; ++i) {
            const char *tri = data.constData() + 84 + static_cast<int>(i) * 50;
            QVector3D normal(readFloat(tri), readFloat(tri + 4), readFloat(tri + 8));
            if (normal.lengthSquared() < 0.000001f) {
                const QVector3D a(readFloat(tri + 12), readFloat(tri + 16), readFloat(tri + 20));
                const QVector3D b(readFloat(tri + 24), readFloat(tri + 28), readFloat(tri + 32));
                const QVector3D c(readFloat(tri + 36), readFloat(tri + 40), readFloat(tri + 44));
                normal = QVector3D::normal(a, b, c);
            } else {
                normal.normalize();
            }
            std::array<Vertex, 3> triVertices;
            for (int v = 0; v < 3; ++v) {
                const char *vp = tri + 12 + v * 12;
                const QVector3D p(readFloat(vp) + translateMm.x(),
                                  readFloat(vp + 4) + translateMm.y(),
                                  readFloat(vp + 8) + translateMm.z());
                m_minBounds.setX(qMin(m_minBounds.x(), p.x()));
                m_minBounds.setY(qMin(m_minBounds.y(), p.y()));
                m_minBounds.setZ(qMin(m_minBounds.z(), p.z()));
                m_maxBounds.setX(qMax(m_maxBounds.x(), p.x()));
                m_maxBounds.setY(qMax(m_maxBounds.y(), p.y()));
                m_maxBounds.setZ(qMax(m_maxBounds.z(), p.z()));
                triVertices[v] = {p.x(), p.y(), p.z(),
                                  normal.x(), normal.y(), normal.z(),
                                  color.x(), color.y(), color.z()};
                if (i % surfaceStride == 0) {
                    m_vertices.append(triVertices[v]);
                }
                if (i % outlineStride == 0) {
                    outline.points.append(p);
                }
            }
            if (i % lineStride == 0) {
                pushEdge(triVertices[0], triVertices[1]);
                pushEdge(triVertices[1], triVertices[2]);
                pushEdge(triVertices[2], triVertices[0]);
            }
        }
        if (!outline.points.isEmpty()) {
            m_partOutlines.append(outline);
        }
        return triCount > 0;
    }

    void loadRobotMeshes()
    {
        const QString root = findStlRoot();
        if (root.isEmpty()) {
            m_loadMessage = "STL folder not found";
            return;
        }

        const QVector3D bodyColor(0.38f, 0.45f, 0.52f);
        const QVector3D rearOffset(-205.0f, 0.0f, 0.0f);
        const QVector<MeshInstance> instances = {
            {"mainbody.stl", bodyColor, {}},
            {"frontpart.stl", bodyColor, {}},
            {"backpart.stl", bodyColor, {}},
            {"jetsonPlate.stl", bodyColor, {}},
            {"lshoulder.stl", bodyColor, {}},
            {"rshoulder.stl", bodyColor, {}},
            {"larm.stl", bodyColor, {}},
            {"rarm.stl", bodyColor, {}},
            {"larm_cover.stl", bodyColor, {}},
            {"rarm_cover.stl", bodyColor, {}},
            {"lfoot.stl", bodyColor, {}},
            {"rfoot.stl", bodyColor, {}},
            {"lshoulder.stl", bodyColor, rearOffset},
            {"rshoulder.stl", bodyColor, rearOffset},
            {"larm.stl", bodyColor, rearOffset},
            {"rarm.stl", bodyColor, rearOffset},
            {"larm_cover.stl", bodyColor, rearOffset},
            {"rfoot.stl", bodyColor, rearOffset},
            {"lfoot.stl", bodyColor, rearOffset}
        };

        int loaded = 0;
        for (const MeshInstance &instance : instances) {
            if (appendBinaryStl(QDir(root).absoluteFilePath(instance.file),
                                instance.color,
                                instance.translateMm)) {
                ++loaded;
            }
        }

        if (m_vertices.isEmpty()) {
            m_loadMessage = "STL load failed";
            return;
        }

        const QVector3D center = (m_minBounds + m_maxBounds) * 0.5f;
        const QVector3D span = m_maxBounds - m_minBounds;
        const float maxSpan = qMax(span.x(), qMax(span.y(), span.z()));
        const float scale = maxSpan > 0.001f ? 1.34f / maxSpan : 1.0f;
        auto normalizeVertices = [center, scale](QVector<Vertex> *vertices) {
            for (Vertex &v : *vertices) {
                const QVector3D p(v.x, v.y, v.z);
                const QVector3D n(v.nx, v.ny, v.nz);
                const QVector3D normalized((p.x() - center.x()) * scale,
                                           (p.y() - center.y()) * scale,
                                           (p.z() - center.z()) * scale);
                v.x = normalized.x();
                v.y = normalized.y();
                v.z = normalized.z();
                v.nx = n.x();
                v.ny = n.y();
                v.nz = n.z();
            }
        };
        normalizeVertices(&m_vertices);
        normalizeVertices(&m_lineVertices);
        for (PartOutline &outline : m_partOutlines) {
            for (QVector3D &p : outline.points) {
                p = QVector3D((p.x() - center.x()) * scale,
                              (p.y() - center.y()) * scale,
                              (p.z() - center.z()) * scale);
            }
        }
        m_vertexCount = m_vertices.size();
        m_lineVertexCount = m_lineVertices.size();
        m_loadMessage = QString("%1 STL parts").arg(loaded);
    }

    void uploadMesh()
    {
        if (!m_glReady || m_vertices.isEmpty()) {
            return;
        }
        m_vbo.bind();
        m_vbo.allocate(m_vertices.constData(), m_vertices.size() * static_cast<int>(sizeof(Vertex)));
        m_vbo.release();
        m_lineVBO.bind();
        m_lineVBO.allocate(m_lineVertices.constData(), m_lineVertices.size() * static_cast<int>(sizeof(Vertex)));
        m_lineVBO.release();
    }

    static qreal cross2d(const QPointF &o, const QPointF &a, const QPointF &b)
    {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    }

    QVector<QPointF> convexHull(QVector<QPointF> points) const
    {
        if (points.size() < 3) {
            return points;
        }
        std::sort(points.begin(), points.end(), [](const QPointF &a, const QPointF &b) {
            if (!qFuzzyCompare(a.x(), b.x())) {
                return a.x() < b.x();
            }
            return a.y() < b.y();
        });

        QVector<QPointF> hull;
        hull.reserve(points.size() * 2);
        for (const QPointF &pt : points) {
            while (hull.size() >= 2 && cross2d(hull[hull.size() - 2], hull[hull.size() - 1], pt) <= 0.0) {
                hull.removeLast();
            }
            hull.append(pt);
        }
        const int lowerSize = hull.size();
        for (int i = points.size() - 2; i >= 0; --i) {
            const QPointF &pt = points[i];
            while (hull.size() > lowerSize && cross2d(hull[hull.size() - 2], hull[hull.size() - 1], pt) <= 0.0) {
                hull.removeLast();
            }
            hull.append(pt);
        }
        if (!hull.isEmpty()) {
            hull.removeLast();
        }
        return hull;
    }

    bool projectPoint(const QVector3D &world, QPointF *screen) const
    {
        const QVector4D clip = m_lastMvp * QVector4D(world, 1.0f);
        if (clip.w() <= 0.0f) {
            return false;
        }
        const float x = clip.x() / clip.w();
        const float y = clip.y() / clip.w();
        if (x < -1.25f || x > 1.25f || y < -1.25f || y > 1.25f) {
            return false;
        }
        *screen = QPointF((x * 0.5f + 0.5f) * width(),
                          (0.5f - y * 0.5f) * height());
        return true;
    }

    void drawPartOutlines(QPainter *painter) const
    {
        if (!m_haveLastMvp) {
            return;
        }
        rebuildOutlineCacheIfNeeded();
        const QColor base = QColor::fromRgbF(m_statusTint.x(), m_statusTint.y(), m_statusTint.z());
        const QColor fill(base.red(), base.green(), base.blue(), 92);
        const QColor glow(base.lighter(145).red(), base.lighter(145).green(), base.lighter(145).blue(), 86);
        const QColor line(base.lighter(160).red(), base.lighter(160).green(), base.lighter(160).blue(), 235);
        for (const QPolygonF &poly : m_cachedOutlinePolygons) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawPolygon(poly);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(glow, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPolygon(poly);
            painter->setPen(QPen(line, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPolygon(poly);
        }
    }

    void drawConnectionWarning(QPainter *painter) const
    {
        if (!m_connectionWarningActive || m_connectionWarningStartMs < 0) {
            return;
        }
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_connectionWarningStartMs;
        if (elapsed < 0 || elapsed >= kRobotWarningBlinkMs) {
            return;
        }
        if ((elapsed / kRobotWarningBlinkIntervalMs) % 2 != 0) {
            return;
        }

        const qreal size = qBound<qreal>(24.0, qMin(width(), height()) * 0.18, 38.0);
        const QPointF top(width() - size * 1.24, height() * 0.34 - size * 0.42);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        drawWarningTriangle(*painter, top, size, 0.92);
        painter->restore();
    }

    void rebuildOutlineCacheIfNeeded() const
    {
        const int yawBucket = qRound(m_yawDeg / 0.25f);
        const int pitchBucket = qRound(m_pitchDeg / 0.25f);
        if (m_outlineCacheValid
            && m_cachedWidth == width()
            && m_cachedHeight == height()
            && m_cachedYawBucket == yawBucket
            && m_cachedPitchBucket == pitchBucket) {
            return;
        }

        QVector<QPolygonF> polygons;
        polygons.reserve(m_partOutlines.size());
        for (const PartOutline &part : m_partOutlines) {
            QVector<QPointF> screenPts;
            screenPts.reserve(part.points.size());
            for (const QVector3D &pt : part.points) {
                QPointF screen;
                if (projectPoint(pt, &screen)) {
                    screenPts.append(screen);
                }
            }
            if (screenPts.size() < 5) {
                continue;
            }
            const QVector<QPointF> hull = convexHull(screenPts);
            if (hull.size() < 3) {
                continue;
            }
            QPolygonF poly;
            for (const QPointF &pt : hull) {
                poly << pt;
            }
            polygons.append(poly);
        }

        m_cachedOutlinePolygons = polygons;
        m_cachedWidth = width();
        m_cachedHeight = height();
        m_cachedYawBucket = yawBucket;
        m_cachedPitchBucket = pitchBucket;
        m_outlineCacheValid = true;
    }

    QVector<Vertex> m_vertices;
    QVector<Vertex> m_lineVertices;
    QVector<PartOutline> m_partOutlines;
    mutable QVector<QPolygonF> m_cachedOutlinePolygons;
    QVector3D m_minBounds {std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max(),
                           std::numeric_limits<float>::max()};
    QVector3D m_maxBounds {-std::numeric_limits<float>::max(),
                           -std::numeric_limits<float>::max(),
                           -std::numeric_limits<float>::max()};
    QOpenGLShaderProgram m_prog;
    QOpenGLBuffer m_vbo { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_lineVBO { QOpenGLBuffer::VertexBuffer };
    QPoint m_lastMouse;
    QVector3D m_statusTint {0.02f, 0.58f, 0.12f};
    QString m_loadMessage;
    int m_vertexCount = 0;
    int m_lineVertexCount = 0;
    int m_locMvp = -1;
    int m_locTint = -1;
    int m_locWireMode = -1;
    int m_spinTimerId = 0;
    QMatrix4x4 m_lastMvp;
    mutable int m_cachedWidth = -1;
    mutable int m_cachedHeight = -1;
    mutable int m_cachedYawBucket = 0;
    mutable int m_cachedPitchBucket = 0;
    float m_yawDeg = -90.0f;
    float m_pitchDeg = 0.0f;
    float m_zoom = 0.0f;
    bool m_glReady = false;
    bool m_haveLastMvp = false;
    mutable bool m_outlineCacheValid = false;
    bool m_connectionWarningActive = false;
    qint64 m_connectionWarningStartMs = -1;
};

RobotStatusCard::RobotStatusCard(int robotId, QWidget *parent)
    : QFrame(parent), m_robotId(robotId)
{
    setObjectName("robotStatusCard");
    setMinimumSize(620, 350);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(0);

    QHBoxLayout *main = new QHBoxLayout;
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(14);

    QFrame *visual = new QFrame;
    visual->setObjectName("robotVisualPanel");
    visual->setMinimumWidth(220);
    visual->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *visualLayout = new QVBoxLayout(visual);
    visualLayout->setContentsMargins(16, 14, 16, 14);
    visualLayout->setSpacing(8);

    QLabel *idLabel = new QLabel("ID");
    idLabel->setObjectName("robotIdLabel");
    m_name = new QLabel(robotName(robotId));
    m_name->setObjectName("robotCardName");
    QFrame *accent = new QFrame;
    accent->setObjectName("robotAccentLine");
    accent->setFixedSize(118, 2);

    visualLayout->addWidget(idLabel);
    visualLayout->addWidget(m_name);
    visualLayout->addWidget(accent);

    m_connectionWarningIcon = new WarningTriangleOverlay(m_name, visual);

    m_robotView = new SpotStl3DView;
    visualLayout->addWidget(m_robotView, 1);

    m_statusBadge = new QLabel;
    m_statusBadge->setObjectName("robotStatusBadge");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumSize(102, 38);
    visualLayout->addWidget(m_statusBadge, 0, Qt::AlignLeft);
    main->addWidget(visual, 5);

    QVBoxLayout *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(10);

    QFrame *batteryPanel = new QFrame;
    batteryPanel->setObjectName("robotBatteryPanel");
    QHBoxLayout *batteryLayout = new QHBoxLayout(batteryPanel);
    batteryLayout->setContentsMargins(14, 10, 10, 10);
    batteryLayout->setSpacing(10);
    QWidget *batteryIcon = new QWidget;
    batteryIcon->setObjectName("robotSectionIcon");
    batteryIcon->setFixedSize(30, 16);
    QHBoxLayout *batteryIconLayout = new QHBoxLayout(batteryIcon);
    batteryIconLayout->setContentsMargins(0, 0, 0, 0);
    batteryIconLayout->setSpacing(2);
    QFrame *batteryBody = new QFrame(batteryIcon);
    batteryBody->setObjectName("robotBatteryIconBody");
    QFrame *batteryNub = new QFrame(batteryIcon);
    batteryNub->setObjectName("robotBatteryIconNub");
    batteryNub->setFixedSize(4, 8);
    batteryIconLayout->addWidget(batteryBody, 1);
    batteryIconLayout->addWidget(batteryNub, 0, Qt::AlignVCenter);
    QLabel *batteryTitle = new QLabel("배터리");
    batteryTitle->setObjectName("robotBatteryTitle");
    m_batteryBar = new AnimatedProgressBar;
    m_batteryBar->setObjectName("robotBatteryBar");
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setTextVisible(false);
    m_batteryBar->setMinimumWidth(150);
    m_battery = makeValueLabel("robotBatteryValue");
    m_battery->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_battery->setFixedWidth(42);
    batteryLayout->addWidget(batteryIcon);
    batteryLayout->addWidget(batteryTitle);
    batteryLayout->addWidget(m_batteryBar, 1);
    batteryLayout->addWidget(m_battery);
    right->addWidget(batteryPanel);

    m_target = makeValueLabel();
    m_positionX = makeValueLabel("robotCardSubValue");
    m_positionY = makeValueLabel("robotCardSubValue");
    m_rtt = makeValueLabel("robotCardSubValue");
    m_packetLoss = makeValueLabel("robotCardSubValue");
    m_rtt->setParent(this);
    m_packetLoss->setParent(this);
    m_rtt->hide();
    m_packetLoss->hide();
    m_linkQuality = makeValueLabel("robotCardSubValue");
    m_temp = makeValueLabel("robotMiniValue");
    m_speed = makeValueLabel("robotMiniValue");
    m_camera = makeValueLabel("robotMiniValue");

    auto addRow = [](QVBoxLayout *layout, const QString &label, QLabel *value) {
        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        QLabel *dot = new QLabel("●");
        dot->setObjectName("robotRowDot");
        QLabel *name = new QLabel(label);
        name->setObjectName("robotRowLabel");
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(dot);
        row->addWidget(name);
        row->addStretch(1);
        row->addWidget(value);
        layout->addLayout(row);
    };

    QHBoxLayout *sections = new QHBoxLayout;
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(10);

    QFrame *missionCell = makeInfoCell("임무 상태", m_target, QStringLiteral("mission"));
    if (QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(missionCell->layout())) {
        layout->removeWidget(m_target);
        addRow(layout, "목표 위치", m_target);
        addRow(layout, "현재 위치", m_positionX);
        addRow(layout, "진행률", m_positionY);
    }

    QFrame *robotCell = makeInfoCell("로봇 상태", m_temp, QStringLiteral("robot"));
    if (QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(robotCell->layout())) {
        layout->removeWidget(m_temp);
        addRow(layout, "온도", m_temp);
        addRow(layout, "속도", m_speed);
        addRow(layout, "네트워크 상태", m_linkQuality);
    }
    sections->addWidget(missionCell, 1);
    sections->addWidget(robotCell, 1);
    right->addLayout(sections, 1);

    QFrame *partsCell = new QFrame;
    partsCell->setObjectName("robotPartsPanel");
    partsCell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    partsCell->setMinimumHeight(78);
    partsCell->setMaximumHeight(84);

    m_camera->setParent(this);
    m_camera->hide();

    QHBoxLayout *partsRoot = new QHBoxLayout(partsCell);
    partsRoot->setContentsMargins(12, 9, 12, 9);
    partsRoot->setSpacing(10);

    QFrame *partsTitleBlock = new QFrame;
    partsTitleBlock->setObjectName("robotPartsTitleBlock");
    partsTitleBlock->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    partsTitleBlock->setMinimumWidth(92);
    QHBoxLayout *partsTitleLayout = new QHBoxLayout(partsTitleBlock);
    partsTitleLayout->setContentsMargins(0, 0, 8, 0);
    partsTitleLayout->setSpacing(8);

    QLabel *partsIcon = new QLabel;
    partsIcon->setObjectName("robotPartsIcon");
    partsIcon->setFixedSize(34, 34);
    partsIcon->setAlignment(Qt::AlignCenter);
    partsIcon->setPixmap(makeRobotInfoIcon(QStringLiteral("parts"), 34));
    QLabel *partsTitle = new QLabel(QStringLiteral("부품\n상태"));
    partsTitle->setObjectName("robotPartsTitle");
    partsTitle->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    partsTitleLayout->addWidget(partsIcon);
    partsTitleLayout->addWidget(partsTitle);
    partsRoot->addWidget(partsTitleBlock);

    QFrame *partsDivider = new QFrame;
    partsDivider->setObjectName("robotPartsDivider");
    partsDivider->setFixedWidth(1);
    partsRoot->addWidget(partsDivider);

    const QStringList partLabels = {
        QStringLiteral("카메라"),
        QStringLiteral("라이다"),
        QStringLiteral("모터")
    };
    QFont partLabelFont(QStringLiteral("Noto Sans"), 13, QFont::Black);
    partLabelFont.setBold(true);
    QFont chipFont(QStringLiteral("Noto Sans"), 13, QFont::Black);
    chipFont.setBold(true);
    chipFont.setStretch(88);
    QFontMetrics labelMetrics(partLabelFont);
    QFontMetrics chipMetrics(chipFont);
    const int chipWidth = 66;
    const int chipHeight = qMax(26, chipMetrics.height() + 8);
    const int labelHeight = qMax(18, labelMetrics.height() + 4);
    for (const QString &labelText : partLabels) {
        QFrame *part = new QFrame;
        part->setObjectName("robotPartCard");
        part->setProperty("state", "offline");
        part->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        part->setMinimumWidth(96);
        QGridLayout *partLayout = new QGridLayout(part);
        partLayout->setContentsMargins(6, 5, 6, 5);
        partLayout->setHorizontalSpacing(0);
        partLayout->setVerticalSpacing(2);
        partLayout->setRowMinimumHeight(1, labelHeight);
        partLayout->setRowMinimumHeight(2, chipHeight);
        partLayout->setRowStretch(1, 0);
        partLayout->setRowStretch(2, 0);
        partLayout->setColumnStretch(0, 1);

        QLabel *partLabel = new QLabel(labelText);
        partLabel->setObjectName("robotPartCardLabel");
        partLabel->setFont(partLabelFont);
        partLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        partLabel->setFixedHeight(labelHeight);
        partLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QLabel *button = makeStatusChip("online", "robotStatusChip");
        button->setFont(chipFont);
        button->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        button->setMinimumSize(chipWidth, chipHeight);
        button->setFixedHeight(chipHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_sensorCards.append(part);
        m_sensorChips.append(button);

        partLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), 0, 0, 1, 1);
        partLayout->addWidget(partLabel, 1, 0, Qt::AlignCenter);
        partLayout->addWidget(button, 2, 0, Qt::AlignCenter);
        partLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), 3, 0, 1, 1);
        partLayout->setRowStretch(0, 1);
        partLayout->setRowStretch(3, 1);
        partsRoot->addWidget(part, 1);
    }
    right->addWidget(partsCell);

    main->addLayout(right, 8);
    root->addLayout(main, 1);

    QTimer *fakeTelemetryTimer = new QTimer(this);
    connect(fakeTelemetryTimer, &QTimer::timeout, this, [this]() {
        if (!m_snapshot.shmOpen || !m_snapshot.connected) {
            m_fakeTelemetryValid = false;
            m_fakeSpeedMps = 0.0f;
            updateFakeTelemetryLabels(m_telemetryTextColor);
            return;
        }
        randomizeFakeTelemetry();
        if (!m_lastPoseChangeTimer.isValid() || m_lastPoseChangeTimer.elapsed() > kFakeSpeedHoldMs) {
            m_fakeSpeedMps = 0.0f;
        }
        updateFakeTelemetryLabels(m_telemetryTextColor);
    });
    fakeTelemetryTimer->start(500);

    RobotSnapshot initial;
    initial.id = robotId;
    initial.shmOpen = true;
    initial.connected = true;
    initial.mode = robotId == 1 ? 1 : 0;
    initial.battery = robotId == 4 ? 77.0f
        : (robotId == 3 ? 91.0f
        : (robotId == 2 ? 89.0f
        : (robotId == 1 ? 70.0f : 86.0f)));
    initial.x = robotId == 0 ? 12.45f : (robotId == 1 ? 25.87f : (robotId == 2 ? -8.32f : 3.14f));
    initial.y = robotId == 0 ? -3.21f : (robotId == 1 ? 8.12f : (robotId == 2 ? 15.67f : -12.78f));
    initial.vx = robotId == 0 ? 1.2f : (robotId == 1 ? 1.8f : (robotId == 2 ? 1.1f : 0.2f));
    initial.linkRttMs = robotId == 3 ? 21.0f : (robotId == 2 ? 20.0f : (robotId == 1 ? 22.0f : 18.0f));
    initial.dropRate = 0.0f;
    initial.imageFps = 24.0f;
    initial.lidarFps = 12.0f;
    initial.missionProgress = robotId == 1 ? 43.0f : (robotId == 2 ? 88.0f : 0.0f);
    initial.faultLevel = 0;
    setSnapshot(initial);
}

void RobotStatusCard::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);

    if (!m_connectionWarningActive || m_connectionWarningStartMs < 0 || !m_name) {
        return;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_connectionWarningStartMs;
    if (elapsed < 0) {
        return;
    }
    if ((elapsed / kRobotWarningBlinkIntervalMs) % 2 != 0) {
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QWidget *visualPanel = m_name->parentWidget();
    const QPoint panelTopLeft = visualPanel ? visualPanel->mapTo(this, QPoint(0, 0)) : QPoint(0, 0);
    const QSize panelSize = visualPanel ? visualPanel->size() : size();
    const QRectF panelRect(panelTopLeft, panelSize);
    const qreal size = qBound<qreal>(
        44.0,
        qMin(panelRect.width() * 0.24, panelRect.height() * 0.22),
        70.0);
    const QPoint nameTopLeft = m_name->mapTo(this, QPoint(0, 0));
    const qreal h = size * 0.88;
    const qreal margin = qMax<qreal>(8.0, size * 0.14);
    const qreal desiredX = nameTopLeft.x() + m_name->width() + size * 0.92;
    const qreal desiredY = nameTopLeft.y() + m_name->height() * 0.5 - h * 0.5;
    const QPointF top(qBound(panelRect.left() + margin + size * 0.5,
                             desiredX,
                             panelRect.right() - margin - size * 0.5),
                      qBound(panelRect.top() + margin,
                             desiredY,
                             panelRect.bottom() - margin - h));
    drawWarningTriangle(p, top, size, 0.90);
}

void RobotStatusCard::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_connectionWarningTimerId) {
        if (!m_connectionWarningActive || m_connectionWarningStartMs < 0) {
            if (m_connectionWarningTimerId != 0) {
                killTimer(m_connectionWarningTimerId);
                m_connectionWarningTimerId = 0;
            }
            update();
            return;
        }
        update();
        return;
    }
    QFrame::timerEvent(event);
}

QLabel *RobotStatusCard::makeValueLabel(const QString &objectName)
{
    QLabel *label = new QLabel;
    label->setObjectName(objectName);
    label->setWordWrap(true);
    return label;
}

QFrame *RobotStatusCard::makeInfoCell(const QString &title, QLabel *value, const QString &detail)
{
    QFrame *cell = new QFrame;
    cell->setObjectName("robotInfoCell");
    QVBoxLayout *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    QHBoxLayout *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(7);
    if (!detail.isEmpty()) {
        QLabel *icon = new QLabel;
        icon->setObjectName("robotInfoIcon");
        icon->setFixedSize(24, 24);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(makeRobotInfoIcon(detail));
        titleRow->addWidget(icon, 0, Qt::AlignVCenter);
    }
    QLabel *titleLabel = new QLabel(title);
    titleLabel->setObjectName("robotInfoTitle");
    titleRow->addWidget(titleLabel, 0, Qt::AlignVCenter);
    titleRow->addStretch(1);
    layout->addLayout(titleRow);
    layout->addWidget(value, 1);
    return cell;
}

QLabel *RobotStatusCard::makeSensorChip(const QString &text)
{
    return makeStatusChip(text, QStringLiteral("sensorChip"));
}

QLabel *RobotStatusCard::makeStatusChip(const QString &text, const QString &objectName)
{
    QLabel *chip = new QLabel(text);
    chip->setObjectName(objectName);
    chip->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    chip->setMinimumHeight(26);
    chip->setMinimumWidth(72);
    chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return chip;
}

QString RobotStatusCard::missionText(int mode) const
{
    switch (mode) {
    case 1:
        return "탐색 중";
    case 2:
        return "탐색 중";
    case 3:
        return "탐색 중";
    default:
        return "대기";
    }
}

QString RobotStatusCard::statusText(const RobotSnapshot &snapshot) const
{
    const int offlinePartCount = displayedOfflinePartCountForCard(snapshot);
    if (!snapshot.shmOpen || offlinePartCount >= 3) {
        return "미연결";
    }
    if (snapshot.faultLevel >= 3 || snapshot.battery < 15.0f) {
        return "저배터리";
    }
    if (robotConnectionLost(snapshot)) {
        return "연결 끊김";
    }
    if (!snapshot.connected) {
        return "연결 안됨";
    }
    if (missionComplete(snapshot)) {
        return "도착";
    }
    if (offlinePartCount >= 2) {
        return "위험";
    }
    if (offlinePartCount == 1) {
        return "경고";
    }
    if (pathTrackingAlert(snapshot)) {
        return "경로 점검";
    }
    return robotOperationText(snapshot);
}

void RobotStatusCard::restyleState(const QString &stateName)
{
    setProperty("state", stateName);
    style()->unpolish(this);
    style()->polish(this);
    if (m_statusBadge) {
        m_statusBadge->setProperty("state", stateName);
        m_statusBadge->style()->unpolish(m_statusBadge);
        m_statusBadge->style()->polish(m_statusBadge);
    }
    if (m_batteryBar) {
        m_batteryBar->setProperty("state", stateName);
        m_batteryBar->style()->unpolish(m_batteryBar);
        m_batteryBar->style()->polish(m_batteryBar);
    }
}

void RobotStatusCard::randomizeFakeTelemetry()
{
    QRandomGenerator *rng = QRandomGenerator::global();
    if (!m_fakeTelemetryValid) {
        m_fakeTempC = 41.6f + rng->bounded(5) / 10.0f;
    } else {
        const float delta = (rng->bounded(5) - 2) / 10.0f;
        m_fakeTempC = qBound(39.0f, m_fakeTempC + delta, 42.0f);
    }
    m_fakeTelemetryValid = true;
}

void RobotStatusCard::updateFakeTelemetryLabels(const QString &textColor)
{
    const bool offline = !m_snapshot.shmOpen || !m_snapshot.connected;
    if (!offline && !m_fakeTelemetryValid) {
        randomizeFakeTelemetry();
    }

    m_speed->setText(offline ? "--" : QString("%1 m/s").arg(m_fakeSpeedMps, 0, 'f', 2));
    m_speed->setStyleSheet(QString("color:%1").arg(textColor));
    m_temp->setText(offline ? "--" : QString("%1 °C").arg(m_fakeTempC, 0, 'f', 1));
    m_temp->setStyleSheet(QString("color:%1").arg(textColor));
}

bool RobotStatusCard::partStreamInactiveForCard(const RobotSnapshot &snapshot, int partIndex) const
{
    return partStreamInactive(snapshot, partIndex);
}

bool RobotStatusCard::displayedPartsAreNormalForCard(const RobotSnapshot &snapshot) const
{
    return displayedOfflinePartCountForCard(snapshot) == 0;
}

int RobotStatusCard::displayedOfflinePartCountForCard(const RobotSnapshot &snapshot) const
{
    if (!snapshot.shmOpen || !snapshot.connected) {
        return 3;
    }

    int count = 0;
    for (int i = 0; i < 3; ++i) {
        if (!hasHardcodedNormalPart(m_robotId, i) && partStreamInactiveForCard(snapshot, i)) {
            ++count;
        }
    }
    return count;
}

void RobotStatusCard::setSnapshot(const RobotSnapshot &snapshot)
{
    const bool poseChanged = m_haveLastPose
        && (std::hypot(snapshot.x - m_lastPoseX, snapshot.y - m_lastPoseY) > 0.001f
            || std::fabs(snapshot.theta - m_lastPoseTheta) > 0.001f);
    if (!m_haveLastPose) {
        m_haveLastPose = true;
        m_lastPoseChangeTimer.start();
    } else if (poseChanged) {
        m_lastPoseChangeTimer.restart();
    }
    m_lastPoseX = snapshot.x;
    m_lastPoseY = snapshot.y;
    m_lastPoseTheta = snapshot.theta;

    m_snapshot = snapshot;
    const bool forcedFault = isForcedFaultRobot(m_robotId);
    const int offlinePartCount = displayedOfflinePartCountForCard(snapshot);
    const bool offline = !snapshot.shmOpen || !snapshot.connected || offlinePartCount >= 3;
    if (snapshot.shmOpen && (snapshot.connected || robotEverConnected(snapshot))) {
        m_everConnected = true;
    }
    const bool connectionLost = snapshot.shmOpen && !snapshot.connected && m_everConnected;
    const bool complete = !offline && missionComplete(snapshot);
    const bool started = !offline && robotMissionStarted(snapshot);
    const bool pathAlert = started && !complete && pathTrackingAlert(snapshot);
    const int battery = displayBatteryPercent(m_robotId, snapshot.battery);
    const bool danger = !offline
        && (offlinePartCount >= 2 || forcedFault || snapshot.faultLevel >= 3 || battery < 20);
    const bool warning = !offline && !danger && (offlinePartCount == 1 || pathAlert);
    const bool moving = started && !complete && !danger && !warning;
    const QString stateName = connectionLost ? "danger"
                                      : (offline ? "offline"
                                      : (danger ? "danger"
                                                : (warning ? "warning"
                                                             : (moving ? "moving" : "normal"))));
    const QString textColor = connectionLost ? "#ff5b57" : (offline ? "#747f89" : "#dce7f3");
    const QString mutedColor = connectionLost ? "#ff9a9a" : (offline ? "#5f6972" : "#aeb8c8");
    m_telemetryTextColor = textColor;
    const float packetLoss = qMax(snapshot.dropRate, forcedFault ? 2.1f : 0.0f);
    if (offline) {
        m_fakeSpeedMps = 0.0f;
    } else if (poseChanged) {
        m_fakeSpeedMps = 0.05f + QRandomGenerator::global()->bounded(51) / 1000.0f;
    } else if (!m_lastPoseChangeTimer.isValid() || m_lastPoseChangeTimer.elapsed() > kFakeSpeedHoldMs) {
        m_fakeSpeedMps = 0.0f;
    }
    restyleState(stateName);

    m_name->setText(robotName(m_robotId));
    m_name->setStyleSheet(QString("color:%1").arg(textColor));
    m_statusBadge->setText(connectionLost ? "연결 끊김"
                                   : (offline ? "연결 안됨"
                                   : (complete ? "도착"
                                               : (danger ? "위험"
                                                         : (warning ? (offlinePartCount == 1 ? "경고" : "경로 점검")
                                                                      : (moving ? "탐색 중" : "정상"))))));
    if (m_batteryBar) {
        setProgressBarValueAnimated(m_batteryBar, offline ? 0 : battery);
    }
    m_battery->setText(offline ? "--" : QString("%1%").arg(battery));
    m_battery->setStyleSheet(QString("color:%1").arg(textColor));
    m_target->setText(offline ? "--" : targetPositionText(snapshot));
    m_target->setStyleSheet(QString("color:%1").arg(textColor));
    m_positionX->setText(offline ? "--" : QString("%1, %2").arg(snapshot.x, 0, 'f', 1).arg(snapshot.y, 0, 'f', 1));
    m_positionY->setText(offline ? "--%" : progressText(snapshot));
    m_positionX->setStyleSheet(QString("color:%1").arg(textColor));
    m_positionY->setStyleSheet(QString("color:%1").arg(textColor));
    m_rtt->setText(offline ? "RTT             --" : QString("RTT             %1 ms").arg(qRound(snapshot.linkRttMs)));
    m_rtt->setStyleSheet(QString("color:%1").arg(textColor));
    m_packetLoss->setText(offline ? "패킷 손실       --" : QString("패킷 손실       %1%").arg(packetLoss, 0, 'f', 1));
    m_packetLoss->setStyleSheet(QString("color:%1").arg(textColor));
    m_linkQuality->setText(connectionLost ? "연결 끊김"
                                   : (offline ? "오프라인"
                                   : QString("%1 / %2 ms")
                                         .arg(danger || warning || packetLoss > 1.0f ? "점검" : "정상")
                                         .arg(qRound(snapshot.linkRttMs))));
    m_linkQuality->setStyleSheet(QString("color:%1").arg(mutedColor));
    updateFakeTelemetryLabels(textColor);
    m_camera->setText(offline ? "연결 안됨" : (danger ? "점검 필요" : "정상"));
    m_camera->setStyleSheet(QString("color:%1").arg(textColor));
    if (auto *stlView = dynamic_cast<SpotStl3DView *>(m_robotView)) {
        RobotSnapshot renderSnapshot = snapshot;
        renderSnapshot.faultLevel = 0;
        renderSnapshot.battery = qMax(renderSnapshot.battery, 35.0f);
        stlView->setSnapshot(renderSnapshot, stateName);
        stlView->setConnectionWarningActive(false);
    }
    const bool cardWarningActive = connectionLost || danger || warning;
    if (auto *warningIcon = dynamic_cast<WarningTriangleOverlay *>(m_connectionWarningIcon)) {
        warningIcon->setActive(cardWarningActive);
    }
    update();
    for (int i = 0; i < m_sensorChips.size(); ++i) {
        const bool faultedSensor = !offline && snapshot.faultLevel >= 3 && (i == 0 || i == 1);
        const bool hardcodedNormal = !faultedSensor
            && !offline
            && hasHardcodedNormalPart(m_robotId, i);
        const bool streamInactive = partStreamInactiveForCard(snapshot, i);
        const bool partOffline = !faultedSensor && !hardcodedNormal && (offline || streamInactive);
        const bool partError = faultedSensor || (!hardcodedNormal && !partOffline && danger && i == 2);
        const QString partState = partOffline ? QStringLiteral("offline")
                                              : (partError ? QStringLiteral("danger") : QStringLiteral("normal"));
        if (i < m_sensorCards.size()) {
            m_sensorCards[i]->setProperty("state", partState);
            m_sensorCards[i]->style()->unpolish(m_sensorCards[i]);
            m_sensorCards[i]->style()->polish(m_sensorCards[i]);
        }
        m_sensorChips[i]->setObjectName(partOffline ? "robotStatusChipOffline"
                                                    : (partError ? "robotStatusChipDanger" : "robotStatusChip"));
        m_sensorChips[i]->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        if (hardcodedNormal) {
            m_sensorChips[i]->setText("정상");
        } else if (offline) {
            m_sensorChips[i]->setText("연결 안됨");
        } else if (streamInactive) {
            m_sensorChips[i]->setText("연결 안됨");
        } else if (partError) {
            m_sensorChips[i]->setText("비정상");
        } else {
            m_sensorChips[i]->setText("정상");
        }
        m_sensorChips[i]->style()->unpolish(m_sensorChips[i]);
        m_sensorChips[i]->style()->polish(m_sensorChips[i]);
    }
    update();
}

class LidarMap2DView : public QWidget
{
public:
    explicit LidarMap2DView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(520, 360);
        setAttribute(Qt::WA_OpaquePaintEvent);
        m_pulseTimerId = startTimer(33);
    }

    void setMapConfig(const MapConfig &config)
    {
        m_mapConfig = config;
        resetViewToMap();
        update();
    }

    void fitToCurrentSize(float targetScale = 0.0f)
    {
        resetViewToMap(true);
        if (targetScale > 0.0f) {
            m_scale = qBound(0.1f, targetScale, 50.0f);
        }
        update();
    }

    void setSelectedRobot(int robotId)
    {
        if (m_selectedRobot == robotId) {
            return;
        }
        m_selectedRobot = robotId;
        m_currentScan.clear();
        m_haveCurrentScan = false;
        m_lastFrameByRobot.clear();
        resetViewToMap();
        update();
    }

    void showMoveCommandIndicators(const QVector<int> &robotIds)
    {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (int robotId : robotIds) {
            if (!isMapRobotId(robotId)) {
                continue;
            }
            auto it = std::find_if(m_snapshots.cbegin(), m_snapshots.cend(), [robotId](const RobotSnapshot &snapshot) {
                return snapshot.id == robotId;
            });
            if (it == m_snapshots.cend() || !it->shmOpen || !it->connected || it->faultLevel >= 3) {
                continue;
            }
            m_moveCommandStartMs.insert(robotId, nowMs);
        }
        update();
    }

    void showVictimDetection(int robotId)
    {
        if (!isMapRobotId(robotId)) {
            return;
        }

        auto poseIt = std::find_if(m_robotPoses.cbegin(), m_robotPoses.cend(),
                                   [robotId](const RobotPose2D &pose) {
                                       return pose.id == robotId;
                                   });
        if (poseIt == m_robotPoses.cend()) {
            return;
        }

        const QPointF marker = victimMarkerPoint(*poseIt);
        if (m_mapConfig.isValid() &&
            !isRobotInsideMap(static_cast<float>(marker.x()), static_cast<float>(marker.y()))) {
            return;
        }

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        constexpr qreal kVictimMarkerMergeMeters = 0.75;
        auto markerIt = std::find_if(m_victimMarkers.begin(), m_victimMarkers.end(),
                                     [robotId, marker, kVictimMarkerMergeMeters](const VictimMarker2D &victim) {
                                         return victim.robotId == robotId
                                             && QLineF(marker, victim.position).length() <= kVictimMarkerMergeMeters;
                                     });
        if (markerIt == m_victimMarkers.end()) {
            m_victimMarkers.append(VictimMarker2D{robotId, marker, nowMs});
        } else {
            markerIt->robotId = robotId;
            markerIt->pulseStartMs = nowMs;
        }
        update();
    }

    void setRouteSelectionChangedCallback(std::function<void()> callback)
    {
        m_routeSelectionChanged = std::move(callback);
    }

    void setRouteSelectionEnabled(bool enabled)
    {
        if (m_routeSelectionEnabled == enabled) {
            return;
        }
        m_routeSelectionEnabled = enabled;
        setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
        if (!enabled) {
            clearRouteSelection();
        }
        notifyRouteSelectionChanged();
        update();
    }

    void clearRouteSelection()
    {
        const bool changed = m_hasRouteEnd;
        m_hasRouteEnd = false;
        m_routeEnd = QPointF();
        if (changed) {
            notifyRouteSelectionChanged();
            update();
        }
    }

    bool isRouteSelectionEnabled() const
    {
        return m_routeSelectionEnabled;
    }

    bool routeSelectionComplete() const
    {
        return m_hasRouteEnd;
    }

    QPointF routeEnd() const
    {
        return m_routeEnd;
    }

    void setGlobalPathsVisible(bool visible)
    {
        if (m_globalPathsVisible == visible) {
            return;
        }
        m_globalPathsVisible = visible;
        update();
    }

    void setGlobalPathRobotIds(const QSet<int> &robotIds)
    {
        m_globalPathRobotIds = robotIds;
        update();
    }

    void setSnapshots(const QVector<RobotSnapshot> &snapshots)
    {
        m_snapshots = snapshots;
        m_robotPoses.clear();
        QSet<int> presentRobotIds;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        for (const RobotSnapshot &robot : snapshots) {
            if (!isMapRobotId(robot.id)) {
                continue;
            }
            const bool poseAvailable = robotPoseAvailable(robot);
            if (poseAvailable && !m_odomAnchors.contains(robot.id)) {
                m_odomAnchors.insert(robot.id, QPointF(robot.x, robot.y));
            }

            const MapPoint start = startPointForRobot(robot.id);
            const QPointF displayPos = poseAvailable
                ? displayRobotPoint(robot.id, robot.x, robot.y)
                : QPointF(start.x, start.y);
            RobotPose2D pose;
            pose.id = robot.id;
            pose.x = static_cast<float>(displayPos.x());
            pose.y = static_cast<float>(displayPos.y());
            pose.theta = poseAvailable ? robot.theta : startThetaForRobot(m_mapConfig, robot.id);
            pose.selected = robot.id == m_selectedRobot;
            pose.faulted = !robot.shmOpen || !robot.connected || robot.faultLevel >= 3;
            pose.connectionLost = robotConnectionLost(robot);
            pose.neverConnected = !robotConnectionLost(robot) && (!robot.shmOpen || !robot.connected);
            if (!m_mapConfig.isValid() || isRobotInsideMap(pose.x, pose.y)) {
                m_robotPoses.append(pose);
                presentRobotIds.insert(robot.id);
                const bool firstSeen = !m_seenRobotPoseIds.contains(robot.id);
                if (firstSeen) {
                    m_seenRobotPoseIds.insert(robot.id);
                    m_addedPulseStartMs.insert(robot.id, nowMs);
                }
                const bool warningFault = pose.faulted;
                const bool wasFaulted = m_lastFaultState.value(robot.id, false);
                if (!firstSeen && pose.faulted && !wasFaulted) {
                    m_faultPulseStartMs.insert(robot.id, nowMs);
                    m_warningBlinkStartMs.insert(robot.id, nowMs);
                } else if (firstSeen && warningFault) {
                    m_warningBlinkStartMs.insert(robot.id, nowMs);
                }
                m_lastFaultState.insert(robot.id, pose.faulted);
            }
        }
        for (auto it = m_seenRobotPoseIds.begin(); it != m_seenRobotPoseIds.end();) {
            if (!presentRobotIds.contains(*it)) {
                m_lastFaultState.remove(*it);
                m_warningBlinkStartMs.remove(*it);
                it = m_seenRobotPoseIds.erase(it);
            } else {
                ++it;
            }
        }

        auto selectedIt = std::find_if(snapshots.cbegin(), snapshots.cend(), [this](const RobotSnapshot &snapshot) {
            return snapshot.id == m_selectedRobot;
        });
        if (selectedIt != snapshots.cend()) {
            const RobotSnapshot &selected = *selectedIt;
            const bool poseAvailable = robotPoseAvailable(selected);
            const MapPoint start = startPointForRobot(selected.id);
            const QPointF displayPos = poseAvailable
                ? displayRobotPoint(selected.id, selected.x, selected.y)
                : QPointF(start.x, start.y);
            m_robotX = static_cast<float>(displayPos.x());
            m_robotY = static_cast<float>(displayPos.y());
            m_robotTheta = poseAvailable ? selected.theta : startThetaForRobot(m_mapConfig, selected.id);
        } else if (isMapRobotId(m_selectedRobot)) {
            const MapPoint start = startPointForRobot(m_selectedRobot);
            m_robotX = start.x;
            m_robotY = start.y;
            m_robotTheta = startThetaForRobot(m_mapConfig, m_selectedRobot);
        }

        auto activeIt = std::find_if(snapshots.cbegin(), snapshots.cend(), [this](const RobotSnapshot &snapshot) {
            return snapshot.id == m_selectedRobot && robotRecentlyActive(snapshot);
        });
        if (activeIt == snapshots.cend()) {
            if (!m_currentScan.isEmpty()) {
                m_currentScan.clear();
                m_haveCurrentScan = false;
            }
            update();
            return;
        }

        const RobotSnapshot &snapshot = *activeIt;

        if (!snapshot.shmOpen || !snapshot.connected) {
            if (!m_currentScan.isEmpty()) {
                m_currentScan.clear();
                m_haveCurrentScan = false;
                m_lastFrameByRobot.remove(snapshot.id);
            }
            update();
            return;
        }

        if (snapshot.shmOpen && !snapshot.lidarPoints.isEmpty()
            && (!m_lastFrameByRobot.contains(snapshot.id)
                || m_lastFrameByRobot.value(snapshot.id) != snapshot.lidarFrameId)) {
            const float cosT = std::cos(snapshot.theta);
            const float sinT = std::sin(snapshot.theta);
            const QPointF robotBase = displayRobotPoint(snapshot.id, snapshot.x, snapshot.y);
            const QPointF lidarBase(robotBase.x() + kLidarMountForwardMeters * cosT,
                                    robotBase.y() + kLidarMountForwardMeters * sinT);
            m_lidarForwardAxis = QStringLiteral("x");
            QVector<LidarRobotPoint> localPts;
            localPts.reserve(snapshot.lidarPoints.size());
            m_lidarForwardMin = std::numeric_limits<float>::max();
            m_lidarForwardMax = -std::numeric_limits<float>::max();
            for (const LidarPoint &p : snapshot.lidarPoints) {
                const LidarRobotPoint lp = lidarToRobotFrame(p);
                if (lp.forward <= 0.02f) {
                    continue;
                }
                localPts.append(lp);
                m_lidarForwardMin = std::min(m_lidarForwardMin, lp.forward);
                m_lidarForwardMax = std::max(m_lidarForwardMax, lp.forward);
            }
            if (localPts.isEmpty()) {
                m_lidarForwardMin = 0.0f;
                m_lidarForwardMax = 0.0f;
            }

            QVector<Pt2D> framePts;
            framePts.reserve(localPts.size());
            for (const LidarRobotPoint &lp : localPts) {
                const float forward = lp.forward;
                const float left = lp.left;
                Pt2D pt;
                pt.x = static_cast<float>(lidarBase.x()) + forward * cosT - left * sinT;
                pt.y = static_cast<float>(lidarBase.y()) + forward * sinT + left * cosT;
                pt.z = lp.up;
                pt.intensity = lp.intensity;
                framePts.append(pt);
            }
            mergeNearbyLidarScanPoints(&framePts);
            m_currentScan = std::move(framePts);
            m_haveCurrentScan = true;
            m_lidarFrameTimer.restart();
            m_lastFrameByRobot[snapshot.id] = snapshot.lidarFrameId;
        } else if (m_haveCurrentScan
                   && m_lidarFrameTimer.isValid()
                   && m_lidarFrameTimer.elapsed() > kLidarFrameStaleMs) {
            m_currentScan.clear();
            m_haveCurrentScan = false;
            m_lastFrameByRobot.remove(snapshot.id);
        }

        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(rect(), QColor(10, 11, 15));

        if (!m_mapConfig.isValid()) {
            drawMapPlaceholder(p);
            return;
        }

        drawMap(p);
        drawGlobalPaths(p);
        drawLidarScan(p);

        drawRobotPoses(p);
        drawVictimMarkers(p);
        drawRouteSelection(p);

        p.setPen(Qt::white);
        p.setFont(QFont("monospace", 10));
        p.drawText(10, 20, QString("scale: %1x").arg(m_scale, 0, 'f', 1));
        if (isMapRobotId(m_selectedRobot)) {
            p.drawText(10, 36, QString("robot: (%1, %2)")
                       .arg(m_robotX, 0, 'f', 2)
                       .arg(m_robotY, 0, 'f', 2));
            p.drawText(10, 52, QString("theta: %1 deg")
                       .arg(qRadiansToDegrees(m_robotTheta), 0, 'f', 1));
        }
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_pulseTimerId) {
            update();
            return;
        }
        QWidget::timerEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        m_scale *= (event->angleDelta().y() > 0) ? 1.15f : 0.87f;
        m_scale = qBound(0.1f, m_scale, 50.0f);
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_lastMouse = event->pos();
        m_pressMouse = event->pos();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event->buttons() & Qt::LeftButton) {
            m_panOffset += event->pos() - m_lastMouse;
            m_lastMouse = event->pos();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_routeSelectionEnabled && event->button() == Qt::LeftButton
            && (event->pos() - m_pressMouse).manhattanLength() <= 4) {
            handleRouteSelectionClick(event->pos());
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    struct Pt2D {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float intensity = 0.0f;
    };

    struct RobotPose2D {
        int id = -1;
        float x = 0.0f;
        float y = 0.0f;
        float theta = 0.0f;
        bool selected = false;
        bool faulted = false;
        bool connectionLost = false;
        bool neverConnected = false;
    };

    struct VictimMarker2D {
        int robotId = -1;
        QPointF position;
        qint64 pulseStartMs = 0;
    };

    void drawMapPlaceholder(QPainter &p)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect().adjusted(26, 24, -26, -24);
        p.setPen(QPen(QColor(49, 76, 96, 95), 1.0));
        const int grid = qMax(28, qMin(width(), height()) / 12);
        for (int x = static_cast<int>(bounds.left()); x <= bounds.right(); x += grid) {
            p.drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
        }
        for (int y = static_cast<int>(bounds.top()); y <= bounds.bottom(); y += grid) {
            p.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
        }

        QRectF mapFrame = bounds.adjusted(bounds.width() * 0.14,
                                          bounds.height() * 0.18,
                                          -bounds.width() * 0.14,
                                          -bounds.height() * 0.18);
        p.setPen(QPen(QColor("#2e5f7b"), 1.6, Qt::DashLine));
        p.setBrush(QColor(28, 52, 72, 62));
        p.drawRoundedRect(mapFrame, 6, 6);

        const QPointF center = bounds.center();
        const QRectF textBox(center.x() - 205, center.y() - 66, 410, 132);

        const QPointF iconCenter(center.x(), textBox.top() + 37.0);
        const qreal s = 0.86;
        QPainterPath mapShape;
        mapShape.moveTo(iconCenter.x() - 34 * s, iconCenter.y() - 18 * s);
        mapShape.lineTo(iconCenter.x() - 12 * s, iconCenter.y() - 25 * s);
        mapShape.lineTo(iconCenter.x() + 11 * s, iconCenter.y() - 18 * s);
        mapShape.lineTo(iconCenter.x() + 34 * s, iconCenter.y() - 25 * s);
        mapShape.lineTo(iconCenter.x() + 34 * s, iconCenter.y() + 18 * s);
        mapShape.lineTo(iconCenter.x() + 11 * s, iconCenter.y() + 25 * s);
        mapShape.lineTo(iconCenter.x() - 12 * s, iconCenter.y() + 18 * s);
        mapShape.lineTo(iconCenter.x() - 34 * s, iconCenter.y() + 25 * s);
        mapShape.closeSubpath();
        p.setBrush(QColor(8, 24, 39, 170));
        p.setPen(QPen(QColor("#ffd21a"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(mapShape);
        p.setPen(QPen(QColor(255, 210, 26, 150), 1.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(iconCenter.x() - 12 * s, iconCenter.y() - 25 * s),
                   QPointF(iconCenter.x() - 12 * s, iconCenter.y() + 18 * s));
        p.drawLine(QPointF(iconCenter.x() + 11 * s, iconCenter.y() - 18 * s),
                   QPointF(iconCenter.x() + 11 * s, iconCenter.y() + 25 * s));

        const qreal pinX = iconCenter.x() + 22 * s;
        QPainterPath pin;
        pin.addEllipse(QPointF(pinX, iconCenter.y() - 5 * s), 6 * s, 6 * s);
        pin.moveTo(pinX, iconCenter.y() + 12 * s);
        pin.lineTo(pinX - 6 * s, iconCenter.y() + 2 * s);
        pin.lineTo(pinX + 6 * s, iconCenter.y() + 2 * s);
        pin.closeSubpath();
        p.setPen(QPen(QColor("#ffd21a"), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(pin);
        p.drawEllipse(QPointF(pinX, iconCenter.y() - 5 * s), 1.8 * s, 1.8 * s);

        p.setPen(QColor("#dce7f3"));
        p.setFont(QFont("Noto Sans", 14, QFont::Black));
        p.drawText(textBox.adjusted(0, 66, 0, -42),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("탐색 지도 대기"));
        p.setPen(QColor("#8fa1b1"));
        p.setFont(QFont("Noto Sans", 10, QFont::Bold));
        p.drawText(textBox.adjusted(18, 96, -18, -10),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("드론 탭에서 맵 생성 후 관제 지도에 적용됩니다."));

        p.restore();
    }

    void mergeNearbyLidarScanPoints(QVector<Pt2D> *points) const
    {
        if (!points || points->size() < 2) {
            return;
        }

        struct Accum {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float intensity = 0.0f;
            int count = 0;
        };

        QHash<QString, Accum> voxels;
        voxels.reserve(points->size());
        for (const Pt2D &pt : *points) {
            const int ix = static_cast<int>(std::floor(pt.x / kLidar2DVoxelMeters));
            const int iy = static_cast<int>(std::floor(pt.y / kLidar2DVoxelMeters));
            const QString key = QStringLiteral("%1,%2").arg(ix).arg(iy);
            Accum &acc = voxels[key];
            acc.x += pt.x;
            acc.y += pt.y;
            acc.z += pt.z;
            acc.intensity += pt.intensity;
            ++acc.count;
        }

        QVector<Pt2D> merged;
        merged.reserve(voxels.size());
        for (const Accum &acc : voxels) {
            if (acc.count <= 0) {
                continue;
            }
            const float inv = 1.0f / static_cast<float>(acc.count);
            merged.append(Pt2D{acc.x * inv, acc.y * inv, acc.z * inv, acc.intensity * inv});
        }
        *points = std::move(merged);
    }

    QPointF worldToWidget(float wx, float wy) const
    {
        const float px = (wx - m_originX) / m_mPerPixel;
        const float py = (wy - m_originY) / m_mPerPixel;
        return QPointF(px * m_scale + width() / 2.0f + m_panOffset.x(),
                       -py * m_scale + height() / 2.0f + m_panOffset.y());
    }

    QPointF widgetToWorld(const QPointF &pos) const
    {
        const float px = static_cast<float>((pos.x() - width() / 2.0f - m_panOffset.x()) / m_scale);
        const float py = static_cast<float>(-(pos.y() - height() / 2.0f - m_panOffset.y()) / m_scale);
        return QPointF(m_originX + px * m_mPerPixel,
                       m_originY + py * m_mPerPixel);
    }

    QRectF worldRectToWidget(const MapRect &rect) const
    {
        return QRectF(worldToWidget(rect.xMin, rect.yMax),
                      worldToWidget(rect.xMax, rect.yMin)).normalized();
    }

    bool isRobotInsideMap(float x, float y) const
    {
        for (const MapRect &area : connectedAreaRects(m_mapConfig)) {
            if (x >= area.xMin && x <= area.xMax && y >= area.yMin && y <= area.yMax) {
                return true;
            }
        }
        return false;
    }

    bool isSelectableRoutePoint(const QPointF &world) const
    {
        if (!m_mapConfig.isValid() || !isRobotInsideMap(static_cast<float>(world.x()),
                                                        static_cast<float>(world.y()))) {
            return false;
        }
        for (const MapRect &obs : m_mapConfig.obstacles()) {
            if (world.x() >= obs.xMin && world.x() <= obs.xMax
                && world.y() >= obs.yMin && world.y() <= obs.yMax) {
                return false;
            }
        }
        return true;
    }

    int routePointHitAt(const QPoint &pos) const
    {
        constexpr qreal hitRadius = 14.0;
        if (m_hasRouteEnd
            && QLineF(pos, worldToWidget(static_cast<float>(m_routeEnd.x()),
                                         static_cast<float>(m_routeEnd.y()))).length() <= hitRadius) {
            return 0;
        }
        return -1;
    }

    void handleRouteSelectionClick(const QPoint &pos)
    {
        const int hit = routePointHitAt(pos);
        if (hit == 0) {
            m_hasRouteEnd = false;
            m_routeEnd = QPointF();
            notifyRouteSelectionChanged();
            update();
            return;
        }

        const QPointF world = widgetToWorld(pos);
        if (!isSelectableRoutePoint(world)) {
            return;
        }
        m_routeEnd = world;
        m_hasRouteEnd = true;
        notifyRouteSelectionChanged();
        update();
    }

    void notifyRouteSelectionChanged()
    {
        if (m_routeSelectionChanged) {
            m_routeSelectionChanged();
        }
    }

    MapPoint startPointForRobot(int robotId) const
    {
        const QVector<MapPoint> starts = m_mapConfig.starts();
        if (starts.isEmpty()) {
            return {};
        }
        const int startId = markerStartRobotId(robotId);
        if (startId >= 0 && startId < starts.size()) {
            return starts[startId];
        }
        MapPoint fallback = starts.first();
        if (robotId == 4) {
            return fallback;
        }
        const int overflow = qMax(0, startId - starts.size());
        fallback.x += static_cast<float>((overflow % 3) - 1) * 0.45f;
        fallback.y += static_cast<float>((overflow / 3) + 1) * 0.45f;
        return fallback;
    }

    QPointF displayRobotPoint(int robotId, float rawX, float rawY) const
    {
        if (!m_mapConfig.isValid() || isRobotInsideMap(rawX, rawY)) {
            return QPointF(rawX, rawY);
        }
        const MapPoint start = startPointForRobot(robotId);
        const QPointF anchor = m_odomAnchors.value(robotId, QPointF(rawX, rawY));
        return QPointF(start.x + (rawX - static_cast<float>(anchor.x())),
                       start.y + (rawY - static_cast<float>(anchor.y())));
    }

    QPointF victimMarkerPoint(const RobotPose2D &pose) const
    {
        return QPointF(pose.x, pose.y);
    }

    void drawVictimMarkers(QPainter &p)
    {
        if (m_victimMarkers.isEmpty()) {
            return;
        }

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const QColor yellow("#ffd21a");

        for (const VictimMarker2D &marker : m_victimMarkers) {
            const QPointF mp = worldToWidget(static_cast<float>(marker.position.x()),
                                             static_cast<float>(marker.position.y()));
            const qreal markerRadius = kRobotMarker2DRadiusMeters / m_mPerPixel * m_scale;

            auto drawPulseRing = [&p, mp, markerRadius, &yellow](qint64 startMs, qint64 now) {
                if (now < startMs) {
                    return;
                }
                const qreal progress = qBound<qreal>(
                    0.0,
                    static_cast<qreal>(now - startMs) / static_cast<qreal>(kRobotMarkerPulseMs),
                    1.0);
                if (progress >= 1.0) {
                    return;
                }
                const qreal fade = progress < 0.78
                    ? 1.0
                    : qBound<qreal>(0.0, (1.0 - progress) / 0.22, 1.0);
                const qreal pulseRadius = markerRadius * (2.0 + (1.0 - progress) * 12.5);
                QColor pulseColor(yellow);
                pulseColor.setAlpha(qRound(215.0 * fade));
                QColor pulseGlow(yellow);
                pulseGlow.setAlpha(qRound(48.0 * fade));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(pulseGlow,
                              8.0 + (1.0 - progress) * 3.0,
                              Qt::SolidLine,
                              Qt::RoundCap));
                p.drawEllipse(mp, pulseRadius, pulseRadius);
                p.setPen(QPen(pulseColor,
                              1.6 + (1.0 - progress) * 1.2,
                              Qt::SolidLine,
                              Qt::RoundCap));
                p.drawEllipse(mp, pulseRadius, pulseRadius);
            };
            drawPulseRing(marker.pulseStartMs, nowMs);
            drawPulseRing(marker.pulseStartMs + kRobotMarkerPulseEchoDelayMs, nowMs);

            QColor glow(yellow);
            glow.setAlpha(22);
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(mp, markerRadius * 1.78, markerRadius * 1.78);

            QColor innerGlow(yellow);
            innerGlow.setAlpha(18);
            p.setBrush(innerGlow);
            p.drawEllipse(mp, markerRadius * 0.90, markerRadius * 0.90);

            p.setPen(QPen(QColor("#fff8b8"), 2.1, Qt::SolidLine, Qt::RoundCap));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(mp, markerRadius * 1.38, markerRadius * 1.38);

            auto drawPersonIcon = [&p, mp, markerRadius](const QColor &color, qreal width, bool headFill) {
                const qreal r = markerRadius * 1.28;
                p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(headFill ? color : Qt::NoBrush);

                const QPointF head(mp.x(), mp.y() - r * 0.49);
                p.drawEllipse(head, r * 0.13, r * 0.13);

                const qreal shoulderY = mp.y() - r * 0.20;
                const qreal hipY = mp.y() + r * 0.18;
                const qreal armTopY = mp.y() - r * 0.11;
                const qreal armBottomY = mp.y() + r * 0.20;
                const qreal legTopY = mp.y() + r * 0.23;
                const qreal footY = mp.y() + r * 0.54;

                QPainterPath body;
                body.moveTo(mp.x() - r * 0.20, shoulderY);
                body.lineTo(mp.x() + r * 0.20, shoulderY);
                body.moveTo(mp.x() - r * 0.20, shoulderY);
                body.lineTo(mp.x() - r * 0.20, hipY);
                body.moveTo(mp.x() + r * 0.20, shoulderY);
                body.lineTo(mp.x() + r * 0.20, hipY);
                body.moveTo(mp.x() - r * 0.34, armTopY);
                body.lineTo(mp.x() - r * 0.34, armBottomY);
                body.moveTo(mp.x() + r * 0.34, armTopY);
                body.lineTo(mp.x() + r * 0.34, armBottomY);
                body.moveTo(mp.x() - r * 0.09, legTopY);
                body.lineTo(mp.x() - r * 0.09, footY);
                body.moveTo(mp.x() + r * 0.09, legTopY);
                body.lineTo(mp.x() + r * 0.09, footY);
                p.drawPath(body);
            };

            drawPersonIcon(QColor(255, 210, 26, 54), qMax<qreal>(2.0, markerRadius * 0.058), false);
            drawPersonIcon(QColor("#fff8b8"), qMax<qreal>(1.25, markerRadius * 0.038), true);

            p.setPen(QColor("#ffd21a"));
            p.setFont(QFont("monospace", 9, QFont::Bold));
            p.drawText(mp + QPointF(markerRadius + 6.0, -markerRadius - 2.0),
                       QStringLiteral("DETECTED"));
        }

        p.restore();
    }

    void drawRobotPoses(QPainter &p)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        auto drawOne = [&p, this, nowMs](const RobotPose2D &pose) {
            const QPointF rp = worldToWidget(pose.x, pose.y);
            const float robotSizeMeters = pose.selected ? kSelectedRobotMarker2DRadiusMeters
                                                        : kRobotMarker2DRadiusMeters;
            const float robotSize = robotSizeMeters / m_mPerPixel * m_scale;
            const QColor base = robotPathColor(pose.id);
            const QColor danger("#ff2d2d");
            const QColor disconnectedGray("#8a96a3");
            const QColor markerBase = pose.faulted
                ? (pose.neverConnected ? disconnectedGray : danger)
                : base;
            auto highlightForPulse = [nowMs](qint64 startMs) {
                qreal highlight = 0.0;
                const qint64 elapsedMs = nowMs - startMs;
                const qint64 highlightMs = kRobotMarkerPulseMs + kRobotMarkerHighlightTailMs;
                if (elapsedMs >= 0 && elapsedMs < highlightMs) {
                    const qreal t = static_cast<qreal>(elapsedMs) /
                        static_cast<qreal>(highlightMs);
                    highlight = 1.0 - t;
                }
                return highlight;
            };
            qreal markerHighlight = 0.0;
            if (m_addedPulseStartMs.contains(pose.id)) {
                markerHighlight = qMax(markerHighlight, highlightForPulse(m_addedPulseStartMs.value(pose.id)));
            }
            if (m_faultPulseStartMs.contains(pose.id)) {
                markerHighlight = qMax(markerHighlight, highlightForPulse(m_faultPulseStartMs.value(pose.id)));
            }
            const QColor rawFill(markerBase.red(), markerBase.green(), markerBase.blue(),
                                 pose.faulted ? 138 : (pose.selected ? 120 : 54));
            const QColor rawOutline(markerBase.lighter(pose.selected ? 130 : 116).red(),
                                    markerBase.lighter(pose.selected ? 130 : 116).green(),
                                    markerBase.lighter(pose.selected ? 130 : 116).blue(),
                                    pose.faulted ? 255 : (pose.selected ? 245 : 205));
            QColor fill = rawFill;
            QColor outline = rawOutline;
            if (markerHighlight > 0.0) {
                fill = QColor(qRound(rawFill.red() + (255 - rawFill.red()) * markerHighlight),
                              qRound(rawFill.green() + (255 - rawFill.green()) * markerHighlight),
                              qRound(rawFill.blue() + (255 - rawFill.blue()) * markerHighlight),
                              qRound(rawFill.alpha() + (210 - rawFill.alpha()) * markerHighlight));
                outline = QColor(255, 255, 255, qRound(rawOutline.alpha() + (255 - rawOutline.alpha()) * markerHighlight));
            }
            const qreal penWidth = pose.selected ? 2.2 : 1.4;

            auto drawPulseRing = [&p, rp, robotSize](qint64 startMs, qint64 now, const QColor &color) {
                if (now < startMs) {
                    return;
                }
                const qreal progress = qBound<qreal>(
                    0.0,
                    static_cast<qreal>(now - startMs) / static_cast<qreal>(kRobotMarkerPulseMs),
                    1.0);
                if (progress >= 1.0) {
                    return;
                }
                const qreal fade = progress < 0.78 ? 1.0 : qBound<qreal>(0.0, (1.0 - progress) / 0.22, 1.0);
                const qreal radius = robotSize * (2.0 + (1.0 - progress) * 12.5);
                QColor ringColor(color);
                ringColor.setAlpha(qRound(215.0 * fade));
                QColor glowColor(color);
                glowColor.setAlpha(qRound(48.0 * fade));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(glowColor,
                              8.0 + (1.0 - progress) * 3.0,
                              Qt::SolidLine,
                              Qt::RoundCap));
                p.drawEllipse(rp, radius, radius);
                p.setPen(QPen(ringColor,
                              1.6 + (1.0 - progress) * 1.2,
                              Qt::SolidLine,
                              Qt::RoundCap));
                p.drawEllipse(rp, radius, radius);
            };
            auto drawPulse = [&drawPulseRing](qint64 startMs, qint64 now, const QColor &color) {
                drawPulseRing(startMs, now, color);
                drawPulseRing(startMs + kRobotMarkerPulseEchoDelayMs, now, color);
            };

            if (m_addedPulseStartMs.contains(pose.id)) {
                drawPulse(m_addedPulseStartMs.value(pose.id), nowMs, QColor("#26d97c"));
            }
            if (m_faultPulseStartMs.contains(pose.id)) {
                drawPulse(m_faultPulseStartMs.value(pose.id), nowMs, QColor("#ff2d2d"));
            }

            if (pose.selected && !pose.faulted) {
                const qreal beat = std::fmod(static_cast<qreal>(nowMs), 1800.0) / 1800.0;
                const qreal fade = beat < 0.72
                    ? 1.0
                    : qBound<qreal>(0.0, (1.0 - beat) / 0.28, 1.0);
                const qreal radius = robotSize * (1.35 + beat * 1.35);
                QColor beatGlow(markerBase);
                beatGlow.setAlpha(qRound(28.0 * fade));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(beatGlow, 4.0, Qt::SolidLine, Qt::RoundCap));
                p.drawEllipse(rp, radius, radius);

                QColor beatLine(markerBase.lighter(125));
                beatLine.setAlpha(qRound(66.0 * fade));
                p.setPen(QPen(beatLine, 1.15, Qt::SolidLine, Qt::RoundCap));
                p.drawEllipse(rp, radius, radius);
            }

            QColor markerGlow(markerBase);
            markerGlow.setAlpha(pose.faulted ? 34 : (pose.selected ? 46 : 30));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, pose.selected ? 58 : 38));
            p.drawEllipse(rp + QPointF(1.6, 2.0), robotSize * 1.14, robotSize * 1.14);
            p.setBrush(markerGlow);
            p.drawEllipse(rp, robotSize * (pose.selected ? 1.72 : 1.48),
                          robotSize * (pose.selected ? 1.72 : 1.48));

            p.setPen(QPen(outline, penWidth));
            p.setBrush(fill);
            p.drawEllipse(rp, robotSize, robotSize);
            QColor markerShine(markerBase.lighter(170));
            markerShine.setAlpha(pose.selected ? 82 : 46);
            p.setPen(Qt::NoPen);
            p.setBrush(markerShine);
            p.drawEllipse(rp + QPointF(-robotSize * 0.28, -robotSize * 0.30),
                          robotSize * 0.28, robotSize * 0.28);

            const qint64 moveStart = m_moveCommandStartMs.value(pose.id, -1);
            const qint64 moveElapsed = moveStart >= 0 ? nowMs - moveStart : kRobotMoveCommandVisibleMs;
            const bool showMoveCommand = moveElapsed >= 0 && moveElapsed < kRobotMoveCommandVisibleMs;
            auto drawMoveCommandMarker = [&p, rp, robotSize, moveElapsed]() {
                const qint64 fadeStartMs = qMax<qint64>(0, kRobotMoveCommandVisibleMs - kRobotMoveCommandFadeMs);
                const qint64 fadeElapsedMs = qMax<qint64>(0, moveElapsed - fadeStartMs);
                const qreal fade = qBound<qreal>(
                    0.0,
                    1.0 - static_cast<qreal>(fadeElapsedMs) / static_cast<qreal>(kRobotMoveCommandFadeMs),
                    1.0);
                const qreal chevronSize = qMax<qreal>(12.0, robotSize * 0.72);
                const qreal y = rp.y() - robotSize - chevronSize * 0.68 - 7.0;
                const qreal x = rp.x() - chevronSize * 0.42;
                QColor glow("#12ff7a");
                glow.setAlpha(qRound(92.0 * fade));
                QColor stroke("#26ff8a");
                stroke.setAlpha(qRound(245.0 * fade));
                p.setPen(QPen(glow, qMax<qreal>(5.5, chevronSize * 0.20), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (int i = 0; i < 2; ++i) {
                    const qreal cx = x + i * chevronSize * 0.54;
                    p.drawLine(QPointF(cx - chevronSize * 0.22, y - chevronSize * 0.34),
                               QPointF(cx + chevronSize * 0.22, y));
                    p.drawLine(QPointF(cx + chevronSize * 0.22, y),
                               QPointF(cx - chevronSize * 0.22, y + chevronSize * 0.34));
                }
                p.setPen(QPen(stroke, qMax<qreal>(2.8, chevronSize * 0.105), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                for (int i = 0; i < 2; ++i) {
                    const qreal cx = x + i * chevronSize * 0.54;
                    p.drawLine(QPointF(cx - chevronSize * 0.22, y - chevronSize * 0.34),
                               QPointF(cx + chevronSize * 0.22, y));
                    p.drawLine(QPointF(cx + chevronSize * 0.22, y),
                               QPointF(cx - chevronSize * 0.22, y + chevronSize * 0.34));
                }
            };

            if (pose.faulted) {
                p.setPen(QPen(QColor("#ffffff"), qMax<qreal>(2.0, penWidth + 0.8),
                              Qt::SolidLine, Qt::RoundCap));
                const qreal xSize = robotSize * 0.72;
                p.drawLine(QPointF(rp.x() - xSize, rp.y() - xSize),
                           QPointF(rp.x() + xSize, rp.y() + xSize));
                p.drawLine(QPointF(rp.x() + xSize, rp.y() - xSize),
                           QPointF(rp.x() - xSize, rp.y() + xSize));

                const qint64 warningStart = m_warningBlinkStartMs.value(pose.id, -1);
                const qint64 warningElapsed = warningStart >= 0 ? nowMs - warningStart : kRobotWarningBlinkMs;
                const bool showWarning = warningElapsed >= 0
                    && warningElapsed < kRobotWarningBlinkMs
                    && !showMoveCommand
                    && ((warningElapsed / kRobotWarningBlinkIntervalMs) % 2 == 0);
                if (showWarning) {
                    const qreal triSize = qMax<qreal>(17.0, robotSize * 1.15);
                    const qreal triHeight = triSize * 0.88;
                    const QPointF triTop(rp.x(), rp.y() - robotSize - triHeight - 7.0);
                    drawWarningTriangle(p, triTop, triSize, 0.96);
                }
            }
            if (showMoveCommand) {
                drawMoveCommandMarker();
            }

            const QColor thetaColor(markerBase.lighter(150).red(),
                                    markerBase.lighter(150).green(),
                                    markerBase.lighter(150).blue(),
                                    pose.selected ? 245 : 215);
            p.setPen(QPen(thetaColor, penWidth));
            const float dx = std::cos(pose.theta) * robotSize * 1.45f;
            const float dy = -std::sin(pose.theta) * robotSize * 1.45f;
            p.drawLine(rp, QPointF(rp.x() + dx, rp.y() + dy));

            const QString label = robotMapLabel(pose.id);
            if (!label.isEmpty()) {
                p.setPen(pose.faulted ? QColor("#ff4d4d")
                                       : QColor(base.lighter(145).red(),
                                                base.lighter(145).green(),
                                                base.lighter(145).blue(),
                                                pose.selected ? 245 : 205));
                const float viewScale = qBound(0.72f, qMin(width() / 920.0f, height() / 560.0f), 1.0f);
                const int labelSize = qRound((pose.selected ? 11.0f : 10.0f) * viewScale);
                p.setFont(QFont("monospace", qMax(8, labelSize), QFont::Bold));
                const QPointF labelOffset(robotSize + 4.0f * viewScale,
                                          -robotSize - 3.0f * viewScale);
                p.drawText(rp + labelOffset, label);
            }
        };

        for (const RobotPose2D &pose : m_robotPoses) {
            if (!pose.selected) {
                drawOne(pose);
            }
        }
        for (const RobotPose2D &pose : m_robotPoses) {
            if (pose.selected) {
                drawOne(pose);
            }
        }

        p.restore();
    }

    void drawGlobalPaths(QPainter &p)
    {
        if (!m_globalPathsVisible) {
            return;
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        const float pulse = pathPulseProgress();
        QPointF sharedEnd;
        const bool hasSharedEnd = s02GlobalPathEndPoint(&sharedEnd);
        for (const RobotSnapshot &snapshot : m_snapshots) {
            if (!isMapRobotId(snapshot.id) || !globalPathRenderable(snapshot)) {
                continue;
            }
            if (!m_globalPathRobotIds.contains(snapshot.id)) {
                continue;
            }

            QColor base = robotPathColor(snapshot.id);
            const bool showAll = m_selectedRobot < 0;
            const bool selected = showAll || snapshot.id == m_selectedRobot;
            if (!selected) {
                continue;
            }

            QVector<QPointF> pts;
            pts.reserve(snapshot.globalPath.size() + 1);
            for (const GlobalPathPoint &wp : snapshot.globalPath) {
                pts.append(worldToWidget(wp.x, wp.y));
            }
            if (hasSharedEnd && !pts.isEmpty()) {
                pts.last() = worldToWidget(static_cast<float>(sharedEnd.x()),
                                           static_cast<float>(sharedEnd.y()));
            } else if (m_mapConfig.hasEndPoint()) {
                const MapPoint end = m_mapConfig.endPoint();
                const QPointF goal = worldToWidget(end.x, end.y);
                if (pts.isEmpty() || QLineF(pts.last(), goal).length() > 1.0) {
                    pts.append(goal);
                }
            }
            QColor underGlow = base.lighter(116);
            underGlow.setAlpha(showAll ? 24 : 38);
            p.setPen(QPen(underGlow, showAll ? 5.0 : 6.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 0; i < pts.size() - 1; ++i) {
                p.drawLine(pts[i], pts[i + 1]);
            }

            base.setAlpha(showAll ? 158 : 215);
            p.setPen(QPen(base, showAll ? 2.2 : 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 0; i < pts.size() - 1; ++i) {
                p.drawLine(pts[i], pts[i + 1]);
            }
            QColor nodeColor = base.lighter(126);
            nodeColor.setAlpha(showAll ? 28 : 40);
            p.setPen(Qt::NoPen);
            p.setBrush(nodeColor);
            if (pts.size() > 4) {
                const int mid = pts.size() / 2;
                p.drawEllipse(pts[mid], selected ? 1.0 : 0.8, selected ? 1.0 : 0.8);
            }

            drawPathGradientHighlight(p, pts, robotPathColor(snapshot.id), !showAll, pulse);

        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    bool s02GlobalPathEndPoint(QPointF *end) const
    {
        if (!end) {
            return false;
        }
        auto it = std::find_if(m_snapshots.cbegin(), m_snapshots.cend(), [](const RobotSnapshot &snapshot) {
            return snapshot.id == 1 && globalPathRenderable(snapshot);
        });
        if (it == m_snapshots.cend() || it->globalPath.isEmpty()) {
            return false;
        }
        const GlobalPathPoint &last = it->globalPath.last();
        *end = QPointF(last.x, last.y);
        return true;
    }

    void drawRouteSelection(QPainter &p)
    {
        if (!m_routeSelectionEnabled && !m_hasRouteEnd) {
            return;
        }

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        const auto drawMarker = [&p, this](const QPointF &world, const QString &label,
                                           const QColor &color) {
            const QPointF screen = worldToWidget(static_cast<float>(world.x()),
                                                 static_cast<float>(world.y()));
            QColor glow(color);
            glow.setAlpha(55);
            p.setPen(QPen(glow, 9.0, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(screen);

            p.setPen(QPen(color.lighter(120), 2.0));
            p.setBrush(QColor(color.red(), color.green(), color.blue(), 190));
            p.drawEllipse(screen, 6.0, 6.0);

            p.setPen(QColor("#ffffff"));
            p.setFont(QFont("monospace", 9, QFont::Bold));
            p.drawText(screen + QPointF(10.0, -8.0), label);
        };

        if (m_hasRouteEnd) {
            const QPointF end = worldToWidget(static_cast<float>(m_routeEnd.x()),
                                              static_cast<float>(m_routeEnd.y()));
            for (const RobotPose2D &pose : m_robotPoses) {
                if (pose.faulted) {
                    continue;
                }
                if (m_selectedRobot >= 0 && pose.id != m_selectedRobot) {
                    continue;
                }
                const QPointF start = worldToWidget(pose.x, pose.y);
                const QPointF delta = end - start;
                const qreal length = std::hypot(delta.x(), delta.y());
                if (length > 1.0) {
                    const QPointF dir(delta.x() / length, delta.y() / length);
                    p.setPen(QPen(QColor(255, 210, 26, 62), 1.1, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(start, end);

                    const qreal dashLength = 9.0;
                    const qreal period = 22.0;
                    const qreal offset = std::fmod(QDateTime::currentMSecsSinceEpoch() / 28.0, period);
                    p.setPen(QPen(QColor(255, 210, 26, 230), 2.6, Qt::SolidLine, Qt::RoundCap));
                    for (qreal cursor = offset - period; cursor < length; cursor += period) {
                        const qreal segmentStart = qMax<qreal>(0.0, cursor);
                        const qreal segmentEnd = qMin<qreal>(length, cursor + dashLength);
                        if (segmentEnd <= segmentStart) {
                            continue;
                        }
                        p.drawLine(start + dir * segmentStart, start + dir * segmentEnd);
                    }
                }
            }
        }
        if (m_hasRouteEnd) {
            drawMarker(m_routeEnd, QStringLiteral("GOAL"), QColor("#ffd21a"));
        }

        p.restore();
    }

    void drawLidarScan(QPainter &p)
    {
        if (m_currentScan.isEmpty()) {
            return;
        }

        QVector<QPointF> screenPts;
        QVector<const Pt2D *> visiblePts;
        screenPts.reserve(m_currentScan.size());
        visiblePts.reserve(m_currentScan.size());
        for (const Pt2D &pt : m_currentScan) {
            const QPointF wp = worldToWidget(pt.x, pt.y);
            if (wp.x() < -8 || wp.x() > width() + 8 || wp.y() < -8 || wp.y() > height() + 8) {
                continue;
            }
            screenPts.append(wp);
            visiblePts.append(&pt);
        }
        if (screenPts.isEmpty()) {
            return;
        }

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setCompositionMode(QPainter::CompositionMode_Screen);

        for (int i = 0; i < screenPts.size(); ++i) {
            const float energy = lidarPointEnergy(*visiblePts[i]);
            QColor glow(255, 38, 22, qRound(24 + 46 * energy));
            p.setPen(QPen(glow, 5.0 + 2.5 * energy, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(screenPts[i]);
        }

        for (int i = 0; i < screenPts.size(); ++i) {
            const Pt2D &pt = *visiblePts[i];
            const float energy = lidarPointEnergy(pt);
            p.setPen(QPen(lidarPointColor(pt), 2.2 + 1.5 * energy, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(screenPts[i]);
        }

        for (int i = 0; i < screenPts.size(); i += 3) {
            const float energy = lidarPointEnergy(*visiblePts[i]);
            QColor core(255, 236, 210, qRound(120 + 100 * energy));
            p.setPen(QPen(core, 0.9 + 0.8 * energy, Qt::SolidLine, Qt::RoundCap));
            p.drawPoint(screenPts[i]);
        }

        p.restore();
    }

    QString startLabel(const MapPoint &start, int index) const
    {
        Q_UNUSED(start);
        return robotMapLabel(index);
    }

    void drawMap(QPainter &p)
    {
        if (!m_mapConfig.isValid()) {
            return;
        }

        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath mapPath;
        for (const MapRect &area : connectedAreaRects(m_mapConfig)) {
            QPainterPath rectPath;
            rectPath.addRect(worldRectToWidget(area));
            mapPath = mapPath.isEmpty() ? rectPath : mapPath.united(rectPath);
        }
        QPainterPath elevatedPath;
        for (const MapRect &obs : m_mapConfig.obstacles()) {
            if (obs.clearance > 0.0f) {
                QPainterPath rectPath;
                rectPath.addRect(worldRectToWidget(obs));
                elevatedPath = elevatedPath.isEmpty() ? rectPath : elevatedPath.united(rectPath);
            }
        }
        const QPainterPath visibleFloorPath = elevatedPath.isEmpty() ? mapPath : mapPath.subtracted(elevatedPath);

        const QRectF mapBounds = mapPath.boundingRect();
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 52));
        p.drawPath(mapPath.translated(2.6, 3.2));
        p.restore();

        QLinearGradient floorGradient(mapBounds.topLeft(), mapBounds.bottomRight());
        floorGradient.setColorAt(0.0, QColor(34, 66, 92, 142));
        floorGradient.setColorAt(0.58, QColor(28, 52, 72, 124));
        floorGradient.setColorAt(1.0, QColor(18, 37, 54, 138));
        p.setPen(QPen(QColor(72, 128, 172, 210), 2));
        p.setBrush(floorGradient);
        p.drawPath(visibleFloorPath);

        p.save();
        p.setClipPath(visibleFloorPath);
        const qreal gridStep = qBound<qreal>(22.0, qMin(mapBounds.width(), mapBounds.height()) / 9.0, 48.0);
        p.setPen(QPen(QColor(104, 168, 210, 24), 1.0));
        for (qreal x = mapBounds.left(); x <= mapBounds.right(); x += gridStep) {
            p.drawLine(QPointF(x, mapBounds.top()), QPointF(x, mapBounds.bottom()));
        }
        for (qreal y = mapBounds.top(); y <= mapBounds.bottom(); y += gridStep) {
            p.drawLine(QPointF(mapBounds.left(), y), QPointF(mapBounds.right(), y));
        }
        p.restore();

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(118, 179, 218, 92), 1.0));
        p.drawPath(mapPath);

        for (const MapRect &obs : m_mapConfig.obstacles()) {
            const QRectF r = worldRectToWidget(obs);
            const float renderHeight = variedObstacleHeightMeters(obs);
            const bool elevated = renderHeight > 0.0f;
            QPen obsPen(QColor(235, 126, 72), 2);
            p.setPen(obsPen);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, elevated ? 74 : 44));
            p.drawRect(r.translated(2.0, 2.0));
            p.setPen(obsPen);
            if (elevated) {
                const int alpha = isBoundaryWallObstacle(obs) ? 170 : 220;
                QLinearGradient obstacleGradient(r.topLeft(), r.bottomRight());
                obstacleGradient.setColorAt(0.0, QColor(34, 36, 42, alpha));
                obstacleGradient.setColorAt(1.0, QColor(13, 14, 18, qMin(alpha + 18, 245)));
                p.setBrush(obstacleGradient);
                p.drawRect(r);
                p.setPen(QPen(QColor(255, 190, 144, isBoundaryWallObstacle(obs) ? 54 : 82), 1.0));
                p.drawLine(r.topLeft() + QPointF(1.0, 1.0), r.topRight() + QPointF(-1.0, 1.0));
                if (!isBoundaryWallObstacle(obs) && r.width() >= 34.0 && r.height() >= 22.0) {
                    p.setPen(QColor("#f4d4b8"));
                    QFont font = p.font();
                    font.setPointSize(8);
                    font.setBold(true);
                    p.setFont(font);
                    p.drawText(r.adjusted(4, 3, -4, -3),
                               Qt::AlignLeft | Qt::AlignTop,
                               QString("%1m").arg(renderHeight, 0, 'f', 1));
                    p.setPen(obsPen);
                }
                continue;
            }
            QLinearGradient lowGradient(r.topLeft(), r.bottomRight());
            lowGradient.setColorAt(0.0, QColor(180, 78, 44, 168));
            lowGradient.setColorAt(1.0, QColor(116, 39, 27, 156));
            p.setBrush(lowGradient);
            p.drawRect(r);
            p.setPen(QPen(QColor(255, 177, 126, 76), 1.0));
            p.drawLine(r.topLeft() + QPointF(1.0, 1.0), r.topRight() + QPointF(-1.0, 1.0));
        }

        p.setRenderHint(QPainter::Antialiasing, false);
    }

    void resetViewToMap(bool fitToSize = false)
    {
        if (!m_mapConfig.isValid()) {
            return;
        }

        m_originX = m_mapConfig.centerX();
        m_originY = m_mapConfig.centerY();
        if (fitToSize) {
            const float fitPixelsX = qMax(width() - 70, 100);
            const float fitPixelsY = qMax(height() - 70, 100);
            const float mapPixelsX = qMax(m_mapConfig.widthMeters() / m_mPerPixel, 1.0f);
            const float mapPixelsY = qMax(m_mapConfig.heightMeters() / m_mPerPixel, 1.0f);
            m_scale = qBound(0.1f, qMin(fitPixelsX / mapPixelsX, fitPixelsY / mapPixelsY), 50.0f);
        } else {
            m_scale = kDefaultMapScale;
        }
        m_panOffset = QPoint(0, 0);
    }

    float lidarPointEnergy(const Pt2D &pt) const
    {
        float zEnergy = (pt.z - m_zFloor) / qMax(m_zCeil - m_zFloor, 0.001f);
        zEnergy = qBound(0.0f, zEnergy, 1.0f);
        float intensity = qBound(0.0f, pt.intensity, 1.0f);
        if (pt.intensity > 1.0f) {
            intensity = qBound(0.0f, pt.intensity / 255.0f, 1.0f);
        }
        return qBound(0.0f, 0.35f + 0.45f * intensity + 0.20f * zEnergy, 1.0f);
    }

    QColor lidarPointColor(const Pt2D &pt) const
    {
        float t = (pt.z - m_zFloor) / qMax(m_zCeil - m_zFloor, 0.001f);
        t = qBound(0.0f, t, 1.0f);
        const float energy = lidarPointEnergy(pt);
        const int r = 235 + qRound(20 * energy);
        const int g = 34 + qRound(96 * t + 55 * energy);
        const int b = 28 + qRound(28 * (1.0f - t));
        const int a = 170 + qRound(75 * energy);
        return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
    }

    int m_selectedRobot = 0;
    QHash<int, uint32_t> m_lastFrameByRobot;
    QVector<Pt2D> m_currentScan;
    bool m_haveCurrentScan = false;
    QElapsedTimer m_lidarFrameTimer;
    QVector<RobotSnapshot> m_snapshots;
    float m_robotX = 0.0f;
    float m_robotY = 0.0f;
    float m_robotTheta = 0.0f;
    QHash<int, QPointF> m_odomAnchors;
    QVector<RobotPose2D> m_robotPoses;
    QSet<int> m_seenRobotPoseIds;
    QHash<int, bool> m_lastFaultState;
    QHash<int, qint64> m_addedPulseStartMs;
    QHash<int, qint64> m_faultPulseStartMs;
    QHash<int, qint64> m_warningBlinkStartMs;
    QHash<int, qint64> m_moveCommandStartMs;
    QVector<VictimMarker2D> m_victimMarkers;
    MapConfig m_mapConfig;
    float m_mPerPixel = kMapMetersPerPixel;
    float m_originX = 0.0f;
    float m_originY = 0.0f;
    float m_scale = kDefaultMapScale;
    QPoint m_panOffset;
    QPoint m_lastMouse;
    QPoint m_pressMouse;
    float m_zFloor = 0.1f;
    float m_zCeil = 2.5f;
    float m_lidarForwardMin = 0.0f;
    float m_lidarForwardMax = 0.0f;
    QString m_lidarForwardAxis = QStringLiteral("x");
    int m_pulseTimerId = 0;
    bool m_routeSelectionEnabled = false;
    bool m_hasRouteEnd = false;
    bool m_globalPathsVisible = false;
    QSet<int> m_globalPathRobotIds;
    QPointF m_routeEnd;
    std::function<void()> m_routeSelectionChanged;
};

class PointCloud3DView : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
    explicit PointCloud3DView(QWidget *parent = nullptr)
        : QOpenGLWidget(parent)
    {
        setMinimumSize(520, 360);
        QSurfaceFormat fmt;
        fmt.setSamples(4);
        setFormat(fmt);
        m_pulseTimerId = startTimer(33);
    }

    ~PointCloud3DView() override
    {
        makeCurrent();
        m_pointVBO.destroy();
        m_lineVBO.destroy();
        m_mapLineVBO.destroy();
        m_mapPointVBO.destroy();
        m_mapFillVBO.destroy();
        m_robotLineVBO.destroy();
        m_currentRobotFillVBO.destroy();
        m_currentRobotLineVBO.destroy();
        doneCurrent();
    }

    void setMapConfig(const MapConfig &config)
    {
        m_mapConfig = config;
        buildMapGeometry();
        uploadMapToGpu();
        uploadMapFillToGpu();
        uploadRobotMarkersToGpu();
        update();
    }

    void setSelectedRobot(int robotId)
    {
        if (m_selectedRobot == robotId) {
            return;
        }
        m_selectedRobot = robotId;
        m_renderPoints.clear();
        m_linePoints.clear();
        m_pointCount = 0;
        m_linePointCount = 0;
        m_hasLastFrame = false;
        m_lidarFrameTimer.invalidate();
        m_haveOdomAnchor = false;
        m_haveSelectedDisplayPose = false;
        m_currentRobotFillPoints.clear();
        m_currentRobotFillPointCount = 0;
        m_currentRobotLinePoints.clear();
        m_currentRobotLinePointCount = 0;
    }

    void setGlobalPathsVisible(bool visible)
    {
        if (m_globalPathsVisible == visible) {
            return;
        }
        m_globalPathsVisible = visible;
        update();
    }

    void setGlobalPathRobotIds(const QSet<int> &robotIds)
    {
        m_globalPathRobotIds = robotIds;
        update();
    }

    void setSnapshots(const QVector<RobotSnapshot> &snapshots)
    {
        m_snapshots = snapshots;
        updateRobotMarkers(snapshots);

        if (!isMapRobotId(m_selectedRobot)) {
            m_currentRobotFillPoints.clear();
            m_currentRobotLinePoints.clear();
            uploadCurrentRobotMarkerToGpu();
            update();
            return;
        }

        auto it = std::find_if(snapshots.cbegin(), snapshots.cend(), [this](const RobotSnapshot &snapshot) {
            return snapshot.id == m_selectedRobot;
        });
        if (it == snapshots.cend()) {
            setSelectedDisplayPoseToStart();
            buildCurrentRobotMarker();
            uploadCurrentRobotMarkerToGpu();
            clearLidarCloud();
            update();
            return;
        }

        const RobotSnapshot &snapshot = *it;
        m_robotX = snapshot.x;
        m_robotY = snapshot.y;
        const bool poseAvailable = robotPoseAvailable(snapshot);
        const bool liveScan = robotRecentlyActive(snapshot);
        m_robotTheta = poseAvailable ? snapshot.theta : startThetaForRobot(m_mapConfig, snapshot.id);
        if (poseAvailable && !m_haveOdomAnchor) {
            m_odomAnchorX = snapshot.x;
            m_odomAnchorY = snapshot.y;
            m_haveOdomAnchor = true;
        }
        updateSelectedDisplayPose(snapshot, poseAvailable);
        buildCurrentRobotMarker();
        uploadCurrentRobotMarkerToGpu();

        if (!liveScan) {
            clearLidarCloud();
            update();
            return;
        }
        if (snapshot.lidarPoints.isEmpty()) {
            if (m_lidarFrameTimer.isValid() && m_lidarFrameTimer.elapsed() > kLidarFrameStaleMs) {
                clearLidarCloud();
            }
            update();
            return;
        }
        if (m_hasLastFrame && m_lastFrame == snapshot.lidarFrameId) {
            if (m_lidarFrameTimer.isValid() && m_lidarFrameTimer.elapsed() > kLidarFrameStaleMs) {
                clearLidarCloud();
            }
            update();
            return;
        }
        m_lastFrame = snapshot.lidarFrameId;
        m_hasLastFrame = true;
        m_lidarFrameTimer.restart();

        const float resetPitch = qDegreesToRadians(-8.0f);
        const float resetYaw = snapshot.theta;
        const QVector3D lidarOrigin(displayRobotX() + kLidarMountForwardMeters * std::cos(resetYaw),
                                    displayRobotY() + kLidarMountForwardMeters * std::sin(resetYaw),
                                    0.0f);
        const QVector3D lidarForward(std::cos(resetYaw) * std::cos(resetPitch),
                                      std::sin(resetYaw) * std::cos(resetPitch),
                                      std::sin(resetPitch));
        const QVector3D worldUp(0.0f, 0.0f, 1.0f);
        const QVector3D lidarRight = QVector3D::crossProduct(lidarForward, worldUp).normalized();
        const QVector3D lidarLeft = -lidarRight;
        const QVector3D lidarUp = QVector3D::crossProduct(lidarRight, lidarForward).normalized();
        m_renderPoints.clear();
        m_renderPoints.reserve(snapshot.lidarPoints.size());
        float zMin = std::numeric_limits<float>::max();
        float zMax = -std::numeric_limits<float>::max();
        for (const LidarPoint &p : snapshot.lidarPoints) {
            const LidarRobotPoint lp = lidarToRobotFrame(p);
            if (lp.forward <= 0.02f) {
                continue;
            }
            const QVector3D world = lidarOrigin
                + lidarForward * lp.forward
                + lidarLeft * lp.left
                + lidarUp * lp.up;
            GpuPoint gp;
            gp.x = world.x();
            gp.y = world.y();
            gp.z = world.z();
            gp.zval = world.z();
            m_renderPoints.append(gp);
            zMin = std::min(zMin, gp.z);
            zMax = std::max(zMax, gp.z);
        }
        mergeNearbyLidarPoints();
        zMin = std::numeric_limits<float>::max();
        zMax = -std::numeric_limits<float>::max();
        for (const GpuPoint &gp : m_renderPoints) {
            zMin = std::min(zMin, gp.z);
            zMax = std::max(zMax, gp.z);
        }

        if (m_renderPoints.isEmpty()) {
            m_zMin = 0.0f;
            m_zMax = kHeightColorRampMeters;
        } else {
            m_zMin = zMin;
            m_zMax = zMin + qMax(kHeightColorRampMeters, zMax - zMin);
        }
        buildWireframe();
        uploadToGpu();
        update();
    }

    void resetFirstPersonView()
    {
        m_firstPersonEyeZ = 0.20f;
        m_firstPersonPitch = -8.0f;
        m_firstPersonYawOffset = 0.0f;
        update();
    }

protected:
    void initializeGL() override
    {
        initializeOpenGLFunctions();
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);

        static const char *vertSrc = R"(
attribute vec4 aPos;
uniform mat4 uMvp;
uniform float uZMin;
uniform float uZMax;
uniform int uIsLine;
varying vec3 vColor;

vec3 zColor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c;
    if (t < 0.25) c = mix(vec3(0.18,0.58,0.88), vec3(0.16,0.92,0.96), t*4.0);
    else if (t < 0.50) c = mix(vec3(0.16,0.92,0.96), vec3(0.28,0.92,0.42), (t-0.25)*4.0);
    else if (t < 0.75) c = mix(vec3(0.28,0.92,0.42), vec3(1.00,0.86,0.20), (t-0.50)*4.0);
	    else c = mix(vec3(1.00,0.86,0.20), vec3(1.00,0.24,0.18), (t-0.75)*4.0);
	    return c;
	}

	vec3 robotColor(float idValue) {
	    float idx = mod(floor(idValue + 0.5), 5.0);
	    if (idx < 0.5) return vec3(0.353, 0.941, 0.490);
	    if (idx < 1.5) return vec3(0.275, 0.745, 1.000);
	    if (idx < 2.5) return vec3(1.000, 0.765, 0.275);
	    if (idx < 3.5) return vec3(0.922, 0.412, 1.000);
	    return vec3(1.000, 0.412, 0.412);
	}
	
	void main() {
	    gl_Position = uMvp * vec4(aPos.xyz, 1.0);
	    gl_PointSize = 4.0;
	    if (uIsLine == 3) {
	        vColor = robotColor(aPos.w);
	    } else {
	        float t = (aPos.w - uZMin) / max(uZMax - uZMin, 0.001);
	        vColor = zColor(t);
	    }
	}
	)";

        static const char *fragSrc = R"(
varying vec3 vColor;
uniform int uIsLine;
void main() {
    float alpha = (uIsLine == 1) ? 0.45
                : ((uIsLine == 7) ? 0.88 : 1.0);
	    vec3 color = (uIsLine == 2) ? vec3(0.55, 0.72, 0.90)
	               : ((uIsLine == 3) ? vColor
	               : ((uIsLine == 4) ? vec3(0.055, 0.050, 0.058)
	               : ((uIsLine == 5) ? vec3(0.82, 0.32, 0.18) : vColor)));
    gl_FragColor = vec4(color, alpha);
}
)";

        m_prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc);
        m_prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc);
        m_prog.link();
        m_locMvp = m_prog.uniformLocation("uMvp");
        m_locZMin = m_prog.uniformLocation("uZMin");
        m_locZMax = m_prog.uniformLocation("uZMax");
        m_locIsLine = m_prog.uniformLocation("uIsLine");

        m_pointVBO.create();
        m_lineVBO.create();
        m_mapLineVBO.create();
        m_mapPointVBO.create();
        m_mapFillVBO.create();
        m_robotLineVBO.create();
        m_currentRobotFillVBO.create();
        m_currentRobotLineVBO.create();
        m_glReady = true;
        uploadToGpu();
        uploadMapToGpu();
        uploadMapFillToGpu();
        uploadRobotMarkersToGpu();
        uploadCurrentRobotMarkerToGpu();
    }

    void resizeGL(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }

    void paintGL() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        QMatrix4x4 proj;
        QMatrix4x4 view;
        QMatrix4x4 model;
        const float aspect = height() > 0 ? width() / static_cast<float>(height()) : 1.0f;
        if (m_selectedRobot < 0 && m_mapConfig.isValid()) {
            const float targetPixelsPerMeter = kDefaultMapScale / kMapMetersPerPixel;
            const float viewportHalfW = qMax(width() / (targetPixelsPerMeter * 2.0f), 1.0f);
            const float viewportHalfH = qMax(height() / (targetPixelsPerMeter * 2.0f), 1.0f);
            const float mapHalfW = qMax(m_mapConfig.widthMeters() * 0.5f, 1.0f);
            const float mapHalfH = qMax(m_mapConfig.heightMeters() * 0.5f, 1.0f);
            const float fitZoomOut = qMax(1.0f, qMax(mapHalfW / viewportHalfW, mapHalfH / viewportHalfH));
            const float halfW = viewportHalfW * fitZoomOut;
            const float halfH = viewportHalfH * fitZoomOut;

            proj.ortho(-halfW, halfW, -halfH, halfH, 0.1f, 500.0f);
            const QVector3D center(m_mapConfig.centerX(), m_mapConfig.centerY(), 0.0f);
            const float eyeZ = qMax(qMax(m_mapConfig.widthMeters(), m_mapConfig.heightMeters()) * 2.0f, 20.0f);
            view.lookAt(center + QVector3D(0.0f, 0.0f, eyeZ), center, QVector3D(0.0f, 1.0f, 0.0f));
        } else {
            proj.perspective(45.0f, aspect, 0.1f, 500.0f);

            const float rx = displayRobotX();
            const float ry = displayRobotY();
            const float eyeZ = qBound(0.20f, m_firstPersonEyeZ, 2.0f);
            const float pitchR = qDegreesToRadians(qBound(-30.0f, m_firstPersonPitch, 20.0f));
            const float yaw = m_robotTheta + m_firstPersonYawOffset;
            const QVector3D eye(rx, ry, eyeZ);
            const QVector3D forward(std::cos(yaw) * std::cos(pitchR),
                                    std::sin(yaw) * std::cos(pitchR),
                                    std::sin(pitchR));
            view.lookAt(eye, eye + forward, {0, 0, 1});
        }
        const QMatrix4x4 mvp = proj * view * model;

        m_prog.bind();
        m_prog.setUniformValue(m_locMvp, mvp);

        const int posLoc = m_prog.attributeLocation("aPos");
        m_prog.enableAttributeArray(posLoc);

        m_prog.setUniformValue(m_locZMin, kMapColorZMin);
        m_prog.setUniformValue(m_locZMax, kMapColorZMax);

        if (m_mapFillPointCount + m_stageFillPointCount > 0) {
            m_mapFillVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 4);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_TRIANGLES, 0, m_mapFillPointCount + m_stageFillPointCount);
            m_mapFillVBO.release();
        }

        if (m_mapLinePointCount > 0) {
            m_mapLineVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 7);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_LINES, 0, m_mapLinePointCount);
            m_mapLineVBO.release();
        }

        if (m_mapPointCount > 0) {
            m_mapPointVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 7);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_POINTS, 0, m_mapPointCount);
            m_mapPointVBO.release();
        }

        m_prog.setUniformValue(m_locZMin, m_zMin);
        m_prog.setUniformValue(m_locZMax, m_zMax);

        if (m_robotLinePointCount > 0) {
            m_robotLineVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 3);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_LINES, 0, m_robotLinePointCount);
            m_robotLineVBO.release();
        }

        if (m_currentRobotFillPointCount > 0) {
            m_currentRobotFillVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 3);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_TRIANGLES, 0, m_currentRobotFillPointCount);
            m_currentRobotFillVBO.release();
        }

        if (m_currentRobotLinePointCount > 0) {
            m_currentRobotLineVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 3);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_LINES, 0, m_currentRobotLinePointCount);
            m_currentRobotLineVBO.release();
        }

        if (m_linePointCount > 0) {
            m_lineVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 1);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_LINES, 0, m_linePointCount);
            m_lineVBO.release();
        }

        if (m_pointCount > 0) {
            m_pointVBO.bind();
            m_prog.setUniformValue(m_locIsLine, 0);
            m_prog.setAttributeBuffer(posLoc, GL_FLOAT, 0, 4, sizeof(GpuPoint));
            glDrawArrays(GL_POINTS, 0, m_pointCount);
            m_pointVBO.release();
        }

        m_prog.disableAttributeArray(posLoc);
        m_prog.release();

        drawGlobalPaths(mvp);
        drawRobotLabels(mvp);
        drawStatusOverlay();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_lastMouse = event->pos();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint delta = event->pos() - m_lastMouse;
        m_firstPersonYawOffset += delta.x() * 0.01f;
        m_firstPersonPitch = qBound(-30.0f, m_firstPersonPitch - delta.y() * 0.12f, 20.0f);
        m_lastMouse = event->pos();
        update();
    }

    void wheelEvent(QWheelEvent *event) override
    {
        m_firstPersonEyeZ += event->angleDelta().y() * 0.0015f;
        m_firstPersonEyeZ = qBound(0.20f, m_firstPersonEyeZ, 2.0f);
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *) override
    {
        resetFirstPersonView();
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_pulseTimerId) {
            update();
            return;
        }
        QOpenGLWidget::timerEvent(event);
    }

private:
    struct GpuPoint {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float zval = 0.0f;
    };

    void clearLidarCloud()
    {
        if (m_renderPoints.isEmpty() && m_linePoints.isEmpty() && m_pointCount == 0 && m_linePointCount == 0) {
            return;
        }
        m_renderPoints.clear();
        m_linePoints.clear();
        m_pointCount = 0;
        m_linePointCount = 0;
        m_hasLastFrame = false;
        uploadToGpu();
    }

    void mergeNearbyLidarPoints()
    {
        if (m_renderPoints.size() < 2) {
            return;
        }

        struct VoxelAccum {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            int count = 0;
        };

        QHash<QString, VoxelAccum> voxels;
        voxels.reserve(m_renderPoints.size());
        for (const GpuPoint &point : m_renderPoints) {
            const int ix = static_cast<int>(std::floor(point.x / kLidarVoxelMeters));
            const int iy = static_cast<int>(std::floor(point.y / kLidarVoxelMeters));
            const int iz = static_cast<int>(std::floor(point.z / kLidarVoxelMeters));
            const QString key = QStringLiteral("%1,%2,%3").arg(ix).arg(iy).arg(iz);
            VoxelAccum &acc = voxels[key];
            acc.x += point.x;
            acc.y += point.y;
            acc.z += point.z;
            ++acc.count;
        }

        QVector<GpuPoint> merged;
        merged.reserve(voxels.size());
        for (const VoxelAccum &acc : voxels) {
            if (acc.count <= 0) {
                continue;
            }
            const float inv = 1.0f / static_cast<float>(acc.count);
            const float z = acc.z * inv;
            merged.append(GpuPoint{acc.x * inv, acc.y * inv, z, z});
        }
        m_renderPoints = std::move(merged);
    }

    void buildWireframe()
    {
        m_linePoints.clear();
        const int n = m_renderPoints.size();
        if (n < 2) {
            return;
        }
        constexpr int K = 4;
        const float thresh2 = m_connThresh * m_connThresh;
        QVector<float> bestD2(K);
        QVector<int> bestJ(K);

        for (int i = 0; i < n; ++i) {
            std::fill(bestD2.begin(), bestD2.end(), std::numeric_limits<float>::max());
            std::fill(bestJ.begin(), bestJ.end(), -1);

            for (int j = 0; j < n; ++j) {
                if (i == j) {
                    continue;
                }
                const float dx = m_renderPoints[i].x - m_renderPoints[j].x;
                const float dy = m_renderPoints[i].y - m_renderPoints[j].y;
                const float dz = m_renderPoints[i].z - m_renderPoints[j].z;
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > thresh2) {
                    continue;
                }

                int worst = 0;
                for (int k = 1; k < K; ++k) {
                    if (bestD2[k] > bestD2[worst]) {
                        worst = k;
                    }
                }
                if (d2 < bestD2[worst]) {
                    bestD2[worst] = d2;
                    bestJ[worst] = j;
                }
            }

            for (int j : bestJ) {
                if (j > i) {
                    m_linePoints.append(m_renderPoints[i]);
                    m_linePoints.append(m_renderPoints[j]);
                }
            }
        }
    }

    void buildMapGeometry()
    {
        m_mapLinePoints.clear();
        m_mapPointPoints.clear();
        m_mapLineKeys.clear();
        m_mapFillPoints.clear();
        m_stageFillPoints.clear();
        m_robotLinePoints.clear();
        if (!m_mapConfig.isValid()) {
            return;
        }

        buildMapFillGeometry();
        const QVector<MapRect> areas = connectedAreaRects(m_mapConfig);
        for (const MapRect &area : areas) {
            addFloorSurfaceWire(area, 0.0f, true);
        }
        addAreaOutlineLines(areas, 0.0f);

        for (const MapRect &obs : m_mapConfig.obstacles()) {
            const float z = obstacleRenderHeight(obs);
            const bool elevated = z > 0.0f;
            if (elevated) {
                addFloorSurfaceWire(obs, z, false);
                addRectWallSurfaceWire(obs, z);
            }
            addMapRectLines(obs, z, elevated);
        }

        m_viewCenter = QVector3D(m_mapConfig.centerX(), m_mapConfig.centerY(), 0.0f);
        m_distance = qMax(qMax(m_mapConfig.widthMeters(), m_mapConfig.heightMeters()) * 1.8f, 8.0f);
    }

    void appendQuad(QVector<GpuPoint> *dst, const MapRect &rect, float z)
    {
        const GpuPoint a {rect.xMin, rect.yMin, z, z};
        const GpuPoint b {rect.xMax, rect.yMin, z, z};
        const GpuPoint c {rect.xMax, rect.yMax, z, z};
        const GpuPoint d {rect.xMin, rect.yMax, z, z};
        dst->append(a);
        dst->append(b);
        dst->append(c);
        dst->append(a);
        dst->append(c);
        dst->append(d);
    }

    void buildMapFillGeometry()
    {
        const QVector<MapRect> areas = connectedAreaRects(m_mapConfig);
        const QVector<MapRect> occluders = elevatedOccluders();
        if (areas.isEmpty()) {
            return;
        }

        QVector<float> xs;
        QVector<float> ys;
        for (const MapRect &rect : areas) {
            xs.append(rect.xMin);
            xs.append(rect.xMax);
            ys.append(rect.yMin);
            ys.append(rect.yMax);
        }
        for (const MapRect &rect : occluders) {
            xs.append(rect.xMin);
            xs.append(rect.xMax);
            ys.append(rect.yMin);
            ys.append(rect.yMax);
        }

        auto normalizeAxis = [](QVector<float> *values) {
            std::sort(values->begin(), values->end());
            QVector<float> unique;
            for (float value : *values) {
                if (unique.isEmpty() || std::abs(unique.last() - value) > 0.0001f) {
                    unique.append(value);
                }
            }
            *values = unique;
        };
        normalizeAxis(&xs);
        normalizeAxis(&ys);

        auto containsPoint = [](const MapRect &rect, float x, float y) {
            return x > rect.xMin && x < rect.xMax && y > rect.yMin && y < rect.yMax;
        };

        for (int ix = 0; ix < xs.size() - 1; ++ix) {
            for (int iy = 0; iy < ys.size() - 1; ++iy) {
                const float cx = (xs[ix] + xs[ix + 1]) * 0.5f;
                const float cy = (ys[iy] + ys[iy + 1]) * 0.5f;
                bool inArea = false;
                for (const MapRect &area : areas) {
                    if (containsPoint(area, cx, cy)) {
                        inArea = true;
                        break;
                    }
                }
                if (!inArea) {
                    continue;
                }

                bool hiddenByUpper = false;
                for (const MapRect &occ : occluders) {
                    if (containsPoint(occ, cx, cy)) {
                        hiddenByUpper = true;
                        break;
                    }
                }
                if (hiddenByUpper) {
                    continue;
                }

                MapRect cell;
                cell.xMin = xs[ix];
                cell.xMax = xs[ix + 1];
                cell.yMin = ys[iy];
                cell.yMax = ys[iy + 1];
                appendQuad(&m_mapFillPoints, cell, 0.0f);
            }
        }

        for (const MapRect &occ : occluders) {
            const float z = obstacleRenderHeight(occ);
            appendQuad(&m_stageFillPoints, occ, z);
            appendWallQuad(&m_stageFillPoints, occ.xMin, occ.yMin, occ.xMax, occ.yMin, z);
            appendWallQuad(&m_stageFillPoints, occ.xMax, occ.yMin, occ.xMax, occ.yMax, z);
            appendWallQuad(&m_stageFillPoints, occ.xMax, occ.yMax, occ.xMin, occ.yMax, z);
            appendWallQuad(&m_stageFillPoints, occ.xMin, occ.yMax, occ.xMin, occ.yMin, z);
        }
    }

    void appendWallQuad(QVector<GpuPoint> *dst, float x1, float y1, float x2, float y2, float zTop)
    {
        const GpuPoint a {x1, y1, 0.0f, 0.0f};
        const GpuPoint b {x2, y2, 0.0f, 0.0f};
        const GpuPoint c {x2, y2, zTop, zTop};
        const GpuPoint d {x1, y1, zTop, zTop};
        dst->append(a);
        dst->append(b);
        dst->append(c);
        dst->append(a);
        dst->append(c);
        dst->append(d);
    }

    void addRobotMarkerLines(int robotId, float x, float y, float theta)
    {
        constexpr int segments = 28;
        constexpr float radius = 0.18f;
        constexpr float z = 0.08f;
        constexpr float headingLen = 0.27f;
        constexpr float arrowWidth = 0.10f;
        const float baseAngle = std::atan2(arrowWidth, headingLen);
        const float colorId = static_cast<float>(qMax(0, robotId));

        const float hx = std::cos(theta);
        const float hy = std::sin(theta);
        const QPointF tip(x + hx * headingLen, y + hy * headingLen);
        const QPointF left(x + std::cos(theta + baseAngle) * radius,
                           y + std::sin(theta + baseAngle) * radius);
        const QPointF right(x + std::cos(theta - baseAngle) * radius,
                            y + std::sin(theta - baseAngle) * radius);

        const float startAngle = theta + baseAngle;
        const float endAngle = theta + 2.0f * float(M_PI) - baseAngle;
        QPointF prev = left;
        for (int i = 1; i <= segments; ++i) {
            const float t = i / float(segments);
            const float a = startAngle + (endAngle - startAngle) * t;
            const QPointF cur(x + std::cos(a) * radius, y + std::sin(a) * radius);
            m_robotLinePoints.append({static_cast<float>(prev.x()), static_cast<float>(prev.y()), z, colorId});
            m_robotLinePoints.append({static_cast<float>(cur.x()), static_cast<float>(cur.y()), z, colorId});
            prev = cur;
        }

        m_robotLinePoints.append({static_cast<float>(left.x()), static_cast<float>(left.y()), z, colorId});
        m_robotLinePoints.append({static_cast<float>(tip.x()), static_cast<float>(tip.y()), z, colorId});
        m_robotLinePoints.append({static_cast<float>(tip.x()), static_cast<float>(tip.y()), z, colorId});
        m_robotLinePoints.append({static_cast<float>(right.x()), static_cast<float>(right.y()), z, colorId});
    }

    void updateRobotMarkers(const QVector<RobotSnapshot> &snapshots)
    {
        m_robotLinePoints.clear();
        for (const RobotSnapshot &robot : snapshots) {
            if (!isMapRobotId(robot.id) || robot.id == m_selectedRobot) {
                continue;
            }
            const bool poseAvailable = robotPoseAvailable(robot);
            if (poseAvailable && !m_robotOdomAnchors.contains(robot.id)) {
                m_robotOdomAnchors.insert(robot.id, QPointF(robot.x, robot.y));
            }
            const MapPoint start = startPointForRobot(robot.id);
            const QPointF pos = poseAvailable
                ? displayRobotPoint(robot.id, robot.x, robot.y)
                : QPointF(start.x, start.y);
            if (m_mapConfig.isValid() && !isRobotInsideMap(static_cast<float>(pos.x()), static_cast<float>(pos.y()))) {
                continue;
            }
            addRobotMarkerLines(robot.id,
                                static_cast<float>(pos.x()),
                                static_cast<float>(pos.y()),
                                poseAvailable ? robot.theta : startThetaForRobot(m_mapConfig, robot.id));
        }
        uploadRobotMarkersToGpu();
    }

    bool isRobotInsideMap(float x, float y) const
    {
        for (const MapRect &area : connectedAreaRects(m_mapConfig)) {
            if (x >= area.xMin && x <= area.xMax && y >= area.yMin && y <= area.yMax) {
                return true;
            }
        }
        return false;
    }

    bool isRobotInsideMap() const
    {
        return isRobotInsideMap(m_robotX, m_robotY);
    }

    MapPoint selectedStartPoint() const
    {
        return startPointForRobot(m_selectedRobot);
    }

    MapPoint startPointForRobot(int robotId) const
    {
        const QVector<MapPoint> starts = m_mapConfig.starts();
        if (starts.isEmpty()) {
            return {};
        }
        const int startId = markerStartRobotId(robotId);
        if (startId >= 0 && startId < starts.size()) {
            return starts[startId];
        }
        MapPoint fallback = starts.first();
        if (robotId == 4) {
            return fallback;
        }
        const int overflow = qMax(0, startId - starts.size());
        fallback.x += static_cast<float>((overflow % 3) - 1) * 0.45f;
        fallback.y += static_cast<float>((overflow / 3) + 1) * 0.45f;
        return fallback;
    }

    QPointF displayRobotPoint(int robotId, float rawX, float rawY) const
    {
        if (!m_mapConfig.isValid() || isRobotInsideMap(rawX, rawY)) {
            return QPointF(rawX, rawY);
        }
        const MapPoint start = startPointForRobot(robotId);
        const QPointF anchor = m_robotOdomAnchors.value(robotId, QPointF(rawX, rawY));
        return QPointF(start.x + (rawX - static_cast<float>(anchor.x())),
                       start.y + (rawY - static_cast<float>(anchor.y())));
    }

    void setSelectedDisplayPoseToStart()
    {
        const MapPoint start = selectedStartPoint();
        m_selectedDisplayX = start.x;
        m_selectedDisplayY = start.y;
        m_haveSelectedDisplayPose = m_mapConfig.isValid();
        m_robotTheta = startThetaForRobot(m_mapConfig, m_selectedRobot);
    }

    void updateSelectedDisplayPose(const RobotSnapshot &snapshot, bool livePose)
    {
        if (!m_mapConfig.isValid()) {
            m_selectedDisplayX = snapshot.x;
            m_selectedDisplayY = snapshot.y;
            m_haveSelectedDisplayPose = true;
            return;
        }

        if (!livePose) {
            setSelectedDisplayPoseToStart();
            return;
        }

        if (isRobotInsideMap(snapshot.x, snapshot.y)) {
            m_selectedDisplayX = snapshot.x;
            m_selectedDisplayY = snapshot.y;
            m_haveSelectedDisplayPose = true;
            return;
        }

        const MapPoint start = selectedStartPoint();
        m_selectedDisplayX = start.x + (snapshot.x - m_odomAnchorX);
        m_selectedDisplayY = start.y + (snapshot.y - m_odomAnchorY);
        m_haveSelectedDisplayPose = true;
    }

    float displayRobotX() const
    {
        if (m_haveSelectedDisplayPose) {
            return m_selectedDisplayX;
        }
        if (!m_mapConfig.isValid() || isRobotInsideMap()) {
            return m_robotX;
        }
        const MapPoint start = selectedStartPoint();
        return start.x + (m_robotX - m_odomAnchorX);
    }

    float displayRobotY() const
    {
        if (m_haveSelectedDisplayPose) {
            return m_selectedDisplayY;
        }
        if (!m_mapConfig.isValid() || isRobotInsideMap()) {
            return m_robotY;
        }
        const MapPoint start = selectedStartPoint();
        return start.y + (m_robotY - m_odomAnchorY);
    }

    void buildCurrentRobotMarker()
    {
        m_currentRobotLinePoints.clear();
        m_currentRobotFillPoints.clear();
        if (m_mapConfig.isValid() && !isRobotInsideMap(displayRobotX(), displayRobotY())) {
            return;
        }

        const float x = displayRobotX();
        const float y = displayRobotY();

        constexpr int segments = 36;
        constexpr float radius = 0.14f;
        constexpr float z = 0.025f;
        constexpr float arrowZ = 0.065f;
        const float colorId = static_cast<float>(qMax(0, m_selectedRobot));
        for (int i = 0; i < segments; ++i) {
            const float a0 = (2.0f * float(M_PI) * i) / segments;
            const float a1 = (2.0f * float(M_PI) * (i + 1)) / segments;
            m_currentRobotLinePoints.append({x + std::cos(a0) * radius, y + std::sin(a0) * radius, z, colorId});
            m_currentRobotLinePoints.append({x + std::cos(a1) * radius, y + std::sin(a1) * radius, z, colorId});
        }

        const float hx = std::cos(m_robotTheta);
        const float hy = std::sin(m_robotTheta);

        constexpr int outlineSegments = 28;
        constexpr float outlineRadius = 0.18f;
        constexpr float outlineZ = 0.040f;
        constexpr float outlineHeadingLen = 0.30f;
        constexpr float outlineArrowWidth = 0.105f;
        const float baseAngle = std::atan2(outlineArrowWidth, outlineHeadingLen);

        constexpr float fillScale = 0.53f;
        constexpr float fillForwardOffset = 0.055f;
        constexpr float headingLen = outlineHeadingLen * fillScale;
        constexpr float fillRadius = outlineRadius * fillScale;
        const float fillBaseCenterLen = fillForwardOffset + std::cos(baseAngle) * fillRadius;
        const float connectorLen = fillBaseCenterLen + 0.010f;
        const float fillX = x + hx * fillForwardOffset;
        const float fillY = y + hy * fillForwardOffset;
        const GpuPoint tip {fillX + hx * headingLen, fillY + hy * headingLen, arrowZ, colorId};
        const GpuPoint left {fillX + std::cos(m_robotTheta + baseAngle) * fillRadius,
                             fillY + std::sin(m_robotTheta + baseAngle) * fillRadius,
                             arrowZ,
                             colorId};
        const GpuPoint right {fillX + std::cos(m_robotTheta - baseAngle) * fillRadius,
                              fillY + std::sin(m_robotTheta - baseAngle) * fillRadius,
                              arrowZ,
                              colorId};
        m_currentRobotFillPoints.append(tip);
        m_currentRobotFillPoints.append(left);
        m_currentRobotFillPoints.append(right);
        m_currentRobotLinePoints.append({x + hx * radius, y + hy * radius, z, colorId});
        m_currentRobotLinePoints.append({x + hx * connectorLen,
                                         y + hy * connectorLen,
                                         arrowZ,
                                         colorId});

        const QPointF outlineTip(x + hx * outlineHeadingLen, y + hy * outlineHeadingLen);
        const QPointF outlineLeft(x + std::cos(m_robotTheta + baseAngle) * outlineRadius,
                                  y + std::sin(m_robotTheta + baseAngle) * outlineRadius);
        const QPointF outlineRight(x + std::cos(m_robotTheta - baseAngle) * outlineRadius,
                                   y + std::sin(m_robotTheta - baseAngle) * outlineRadius);
        const float startAngle = m_robotTheta + baseAngle;
        const float endAngle = m_robotTheta + 2.0f * float(M_PI) - baseAngle;
        QPointF prev = outlineLeft;
        for (int i = 1; i <= outlineSegments; ++i) {
            const float t = i / float(outlineSegments);
            const float a = startAngle + (endAngle - startAngle) * t;
            const QPointF cur(x + std::cos(a) * outlineRadius, y + std::sin(a) * outlineRadius);
            m_currentRobotLinePoints.append({static_cast<float>(prev.x()), static_cast<float>(prev.y()), outlineZ, colorId});
            m_currentRobotLinePoints.append({static_cast<float>(cur.x()), static_cast<float>(cur.y()), outlineZ, colorId});
            prev = cur;
        }
        m_currentRobotLinePoints.append({static_cast<float>(outlineLeft.x()), static_cast<float>(outlineLeft.y()), outlineZ, colorId});
        m_currentRobotLinePoints.append({static_cast<float>(outlineTip.x()), static_cast<float>(outlineTip.y()), outlineZ, colorId});
        m_currentRobotLinePoints.append({static_cast<float>(outlineTip.x()), static_cast<float>(outlineTip.y()), outlineZ, colorId});
        m_currentRobotLinePoints.append({static_cast<float>(outlineRight.x()), static_cast<float>(outlineRight.y()), outlineZ, colorId});
    }

    float obstacleRenderHeight(const MapRect &obs) const
    {
        return variedObstacleHeightMeters(obs);
    }

    QVector<MapRect> elevatedOccluders() const
    {
        QVector<MapRect> occluders;
        for (const MapRect &obs : m_mapConfig.obstacles()) {
            if (obstacleRenderHeight(obs) > 0.0f) {
                occluders.append(obs);
            }
        }
        return occluders;
    }

    void appendMapLine(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        constexpr float eps = 0.0005f;
        if (std::abs(x1 - x2) < eps && std::abs(y1 - y2) < eps && std::abs(z1 - z2) < eps) {
            return;
        }

        auto snap = [](float value) {
            return std::round(value * 1000.0f) / 1000.0f;
        };
        x1 = snap(x1);
        y1 = snap(y1);
        z1 = snap(z1);
        x2 = snap(x2);
        y2 = snap(y2);
        z2 = snap(z2);

        const bool swapEnds = std::tie(x2, y2, z2) < std::tie(x1, y1, z1);
        const QString key = swapEnds
            ? QString("%1,%2,%3:%4,%5,%6").arg(x2).arg(y2).arg(z2).arg(x1).arg(y1).arg(z1)
            : QString("%1,%2,%3:%4,%5,%6").arg(x1).arg(y1).arg(z1).arg(x2).arg(y2).arg(z2);
        if (m_mapLineKeys.contains(key)) {
            return;
        }
        m_mapLineKeys.insert(key);

        const GpuPoint a {x1, y1, z1, z1};
        const GpuPoint b {x2, y2, z2, z2};
        m_mapLinePoints.append(a);
        m_mapLinePoints.append(b);
        m_mapPointPoints.append(a);
        m_mapPointPoints.append(b);
    }

    void addFloorSurfaceWire(const MapRect &rect, float z, bool hideUnderElevated)
    {
        constexpr float step = 0.75f;
        constexpr float eps = 0.001f;
        auto line = [&](float x1, float y1, float x2, float y2) {
            if (hideUnderElevated && z <= eps) {
                addVisibleFloorLine(x1, y1, x2, y2);
            } else {
                appendMapLine(x1, y1, z, x2, y2, z);
            }
        };

        for (float y = rect.yMin; y <= rect.yMax + eps; y += step) {
            line(rect.xMin, qMin(y, rect.yMax), rect.xMax, qMin(y, rect.yMax));
        }
        if (std::fmod(rect.yMax - rect.yMin, step) > eps) {
            line(rect.xMin, rect.yMax, rect.xMax, rect.yMax);
        }
        for (float x = rect.xMin; x <= rect.xMax + eps; x += step) {
            line(qMin(x, rect.xMax), rect.yMin, qMin(x, rect.xMax), rect.yMax);
        }
        if (std::fmod(rect.xMax - rect.xMin, step) > eps) {
            line(rect.xMax, rect.yMin, rect.xMax, rect.yMax);
        }
    }

    void addWallSurfaceWire(float x1, float y1, float x2, float y2, float zTop)
    {
        constexpr float step = 2.40f;
        constexpr float zStep = 0.50f;
        constexpr float eps = 0.001f;

        if (std::abs(y1 - y2) <= eps) {
            const float y = y1;
            const float minX = qMin(x1, x2);
            const float maxX = qMax(x1, x2);
            for (float x = minX; x <= maxX + eps; x += step) {
                appendMapLine(qMin(x, maxX), y, 0.0f, qMin(x, maxX), y, zTop);
            }
            if (std::fmod(maxX - minX, step) > eps) {
                appendMapLine(maxX, y, 0.0f, maxX, y, zTop);
            }
        } else if (std::abs(x1 - x2) <= eps) {
            const float x = x1;
            const float minY = qMin(y1, y2);
            const float maxY = qMax(y1, y2);
            for (float y = minY; y <= maxY + eps; y += step) {
                appendMapLine(x, qMin(y, maxY), 0.0f, x, qMin(y, maxY), zTop);
            }
            if (std::fmod(maxY - minY, step) > eps) {
                appendMapLine(x, maxY, 0.0f, x, maxY, zTop);
            }
        }

        appendMapLine(x1, y1, 0.0f, x2, y2, 0.0f);
        for (float z = zStep; z < zTop; z += zStep) {
            appendMapLine(x1, y1, z, x2, y2, z);
        }
        appendMapLine(x1, y1, zTop, x2, y2, zTop);
    }

    void addRectWallSurfaceWire(const MapRect &rect, float zTop)
    {
        addWallSurfaceWire(rect.xMin, rect.yMin, rect.xMax, rect.yMin, zTop);
        addWallSurfaceWire(rect.xMax, rect.yMin, rect.xMax, rect.yMax, zTop);
        addWallSurfaceWire(rect.xMin, rect.yMax, rect.xMax, rect.yMax, zTop);
        addWallSurfaceWire(rect.xMin, rect.yMin, rect.xMin, rect.yMax, zTop);
    }

    void addVisibleFloorLine(float x1, float y1, float x2, float y2)
    {
        constexpr float eps = 0.0001f;
        QVector<QPair<float, float>> hidden;

        for (const MapRect &occ : elevatedOccluders()) {
            if (std::abs(x1 - x2) <= eps) {
                const float x = x1;
                if (x > occ.xMin + eps && x < occ.xMax - eps) {
                    const float a = qMax(qMin(y1, y2), occ.yMin);
                    const float b = qMin(qMax(y1, y2), occ.yMax);
                    if (b > a + eps) {
                        hidden.append({a, b});
                    }
                }
            } else if (std::abs(y1 - y2) <= eps) {
                const float y = y1;
                if (y > occ.yMin + eps && y < occ.yMax - eps) {
                    const float a = qMax(qMin(x1, x2), occ.xMin);
                    const float b = qMin(qMax(x1, x2), occ.xMax);
                    if (b > a + eps) {
                        hidden.append({a, b});
                    }
                }
            }
        }

        if (hidden.isEmpty()) {
            appendMapLine(x1, y1, 0.0f, x2, y2, 0.0f);
            return;
        }

        std::sort(hidden.begin(), hidden.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        QVector<QPair<float, float>> merged;
        for (const auto &range : hidden) {
            if (merged.isEmpty() || range.first > merged.last().second + eps) {
                merged.append(range);
            } else {
                merged.last().second = qMax(merged.last().second, range.second);
            }
        }

        const bool vertical = std::abs(x1 - x2) <= eps;
        const float start = vertical ? y1 : x1;
        const float end = vertical ? y2 : x2;
        const bool reversed = start > end;
        const float lo = qMin(start, end);
        const float hi = qMax(start, end);
        float cursor = lo;

        auto emitSegment = [&](float a, float b) {
            if (b <= a + eps) {
                return;
            }
            if (vertical) {
                appendMapLine(x1, reversed ? b : a, 0.0f, x2, reversed ? a : b, 0.0f);
            } else {
                appendMapLine(reversed ? b : a, y1, 0.0f, reversed ? a : b, y2, 0.0f);
            }
        };

        for (const auto &range : merged) {
            const float a = qBound(lo, range.first, hi);
            const float b = qBound(lo, range.second, hi);
            emitSegment(cursor, a);
            cursor = qMax(cursor, b);
        }
        emitSegment(cursor, hi);
    }

    bool projectToScreen(const QMatrix4x4 &mvp, const QVector3D &world, QPointF *screen) const
    {
        const QVector4D clip = mvp * QVector4D(world, 1.0f);
        if (clip.w() <= 0.0f) {
            return false;
        }

        const float ndcX = clip.x() / clip.w();
        const float ndcY = clip.y() / clip.w();
        if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) {
            return false;
        }

        *screen = QPointF((ndcX * 0.5f + 0.5f) * width(),
                          (0.5f - ndcY * 0.5f) * height());
        return true;
    }

    bool projectPathPointToScreen(const QMatrix4x4 &mvp, const QVector3D &world, QPointF *screen) const
    {
        const QVector4D clip = mvp * QVector4D(world, 1.0f);
        if (clip.w() <= 0.0f) {
            return false;
        }

        const float ndcX = clip.x() / clip.w();
        const float ndcY = clip.y() / clip.w();
        *screen = QPointF((ndcX * 0.5f + 0.5f) * width(),
                          (0.5f - ndcY * 0.5f) * height());
        return true;
    }

    void drawGlobalPaths(const QMatrix4x4 &mvp)
    {
        if (!m_globalPathsVisible) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const float pulse = pathPulseProgress();
        QPointF sharedEnd;
        const bool hasSharedEnd = s02GlobalPathEndPoint(&sharedEnd);

        for (const RobotSnapshot &snapshot : m_snapshots) {
            if (!isMapRobotId(snapshot.id) || !globalPathRenderable(snapshot)) {
                continue;
            }
            if (!m_globalPathRobotIds.contains(snapshot.id)) {
                continue;
            }
            const bool showAll = m_selectedRobot < 0;
            const bool selected = showAll || snapshot.id == m_selectedRobot;
            if (!selected) {
                continue;
            }

            QVector<QPointF> pts;
            pts.reserve(snapshot.globalPath.size() + 1);
            for (const GlobalPathPoint &wp : snapshot.globalPath) {
                QPointF screen;
                const float z = qMax(wp.z + 0.035f, 0.075f);
                if (projectPathPointToScreen(mvp, QVector3D(wp.x, wp.y, z), &screen)) {
                    pts.append(screen);
                } else {
                    pts.append(QPointF(std::numeric_limits<qreal>::quiet_NaN(),
                                        std::numeric_limits<qreal>::quiet_NaN()));
                }
            }
            if (hasSharedEnd && !pts.isEmpty()) {
                QPointF screen;
                if (projectPathPointToScreen(mvp, QVector3D(sharedEnd.x(), sharedEnd.y(), 0.075f), &screen)) {
                    pts.last() = screen;
                } else {
                    pts.last() = QPointF(std::numeric_limits<qreal>::quiet_NaN(),
                                         std::numeric_limits<qreal>::quiet_NaN());
                }
            } else if (m_mapConfig.hasEndPoint()) {
                const MapPoint end = m_mapConfig.endPoint();
                QPointF screen;
                if (projectPathPointToScreen(mvp, QVector3D(end.x, end.y, 0.075f), &screen)) {
                    if (pts.isEmpty() || std::isnan(pts.last().x())
                        || QLineF(pts.last(), screen).length() > 1.0) {
                        pts.append(screen);
                    }
                } else {
                    pts.append(QPointF(std::numeric_limits<qreal>::quiet_NaN(),
                                        std::numeric_limits<qreal>::quiet_NaN()));
                }
            }

            QColor color = robotPathColor(snapshot.id);
            color.setAlpha(showAll ? 145 : 220);
            painter.setPen(QPen(color, showAll ? 2.0 : 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 0; i < pts.size() - 1; ++i) {
                if (std::isnan(pts[i].x()) || std::isnan(pts[i + 1].x())) {
                    continue;
                }
                painter.drawLine(pts[i], pts[i + 1]);
            }

            drawPathGradientHighlight(painter, pts, robotPathColor(snapshot.id), !showAll, pulse);
        }
    }

    bool s02GlobalPathEndPoint(QPointF *end) const
    {
        if (!end) {
            return false;
        }
        auto it = std::find_if(m_snapshots.cbegin(), m_snapshots.cend(), [](const RobotSnapshot &snapshot) {
            return snapshot.id == 1 && globalPathRenderable(snapshot);
        });
        if (it == m_snapshots.cend() || it->globalPath.isEmpty()) {
            return false;
        }
        const GlobalPathPoint &last = it->globalPath.last();
        *end = QPointF(last.x, last.y);
        return true;
    }

    void drawRobotLabels(const QMatrix4x4 &mvp)
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool overviewMode = m_selectedRobot < 0;

        for (const RobotSnapshot &robot : m_snapshots) {
            if (!isMapRobotId(robot.id)) {
                continue;
            }
            const bool faulted = !robot.shmOpen || !robot.connected || robot.faultLevel >= 3;
            painter.setPen(faulted ? QColor("#ff4d4d") : QColor(190, 245, 205));
            painter.setFont(QFont("monospace", faulted ? 17 : 15, QFont::Bold));
            const bool poseAvailable = robotPoseAvailable(robot);
            const MapPoint start = startPointForRobot(robot.id);
            const QPointF pos = poseAvailable
                ? displayRobotPoint(robot.id, robot.x, robot.y)
                : QPointF(start.x, start.y);
            if (m_mapConfig.isValid() && !isRobotInsideMap(static_cast<float>(pos.x()), static_cast<float>(pos.y()))) {
                continue;
            }

            const float labelZ = robot.id == m_selectedRobot ? 0.14f : 0.18f;
            const QVector4D clip = mvp * QVector4D(static_cast<float>(pos.x()),
                                                   static_cast<float>(pos.y()),
                                                   labelZ,
                                                   1.0f);
            if (clip.w() <= 0.0f) {
                continue;
            }

            const float ndcX = clip.x() / clip.w();
            const float ndcY = clip.y() / clip.w();
            if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) {
                continue;
            }

            const QPointF screen((ndcX * 0.5f + 0.5f) * width(),
                                 (0.5f - ndcY * 0.5f) * height());
            const QString label = faulted && !overviewMode
                ? QString("%1 X").arg(robotMapLabel(robot.id))
                : robotMapLabel(robot.id);
            const QFontMetrics fm(painter.font());
            const QRect textRect = fm.boundingRect(label);
            const qreal labelLiftPx = robot.id == m_selectedRobot ? 22.0 : 20.0;
            const QRectF centeredRect(screen.x() - textRect.width() * 0.5,
                                      screen.y() - textRect.height() * 0.5 - labelLiftPx,
                                      textRect.width(),
                                      textRect.height());
            painter.drawText(centeredRect, Qt::AlignCenter, label);

            if (faulted) {
                const qreal penWidth = overviewMode ? 1.8 : 2.6;
                const qreal mark = overviewMode
                    ? qMax<qreal>(4.5, textRect.height() * 0.28)
                    : qMax<qreal>(12.0, textRect.height() * 0.62);
                painter.setPen(QPen(QColor("#ffffff"), penWidth, Qt::SolidLine, Qt::RoundCap));
                const QPointF c = overviewMode
                    ? screen
                    : QPointF(screen.x(), centeredRect.center().y() + 20.0);
                painter.drawLine(QPointF(c.x() - mark, c.y() - mark),
                                 QPointF(c.x() + mark, c.y() + mark));
                painter.drawLine(QPointF(c.x() + mark, c.y() - mark),
                                 QPointF(c.x() - mark, c.y() + mark));
            }
        }
    }

    void drawStatusOverlay()
    {
        if (!isMapRobotId(m_selectedRobot)) {
            return;
        }

        QPainter painter(this);
        painter.setPen(Qt::white);
        painter.setFont(QFont("monospace", 10));
        painter.drawText(10, 20, QString("robot: (%1, %2)")
                         .arg(displayRobotX(), 0, 'f', 2)
                         .arg(displayRobotY(), 0, 'f', 2));
        painter.drawText(10, 36, QString("theta: %1 deg")
                         .arg(qRadiansToDegrees(m_robotTheta), 0, 'f', 1));
    }

    void addAreaOutlineLines(const QVector<MapRect> &rects, float z)
    {
        if (rects.isEmpty()) {
            return;
        }

        QVector<float> xs;
        QVector<float> ys;
        xs.reserve(rects.size() * 2);
        ys.reserve(rects.size() * 2);
        for (const MapRect &rect : rects) {
            xs.append(rect.xMin);
            xs.append(rect.xMax);
            ys.append(rect.yMin);
            ys.append(rect.yMax);
        }

        auto normalizeAxis = [](QVector<float> *values) {
            std::sort(values->begin(), values->end());
            QVector<float> unique;
            for (float value : *values) {
                if (unique.isEmpty() || std::abs(unique.last() - value) > 0.0001f) {
                    unique.append(value);
                }
            }
            *values = unique;
        };
        normalizeAxis(&xs);
        normalizeAxis(&ys);
        if (xs.size() < 2 || ys.size() < 2) {
            return;
        }

        QVector<QVector<bool>> filled(xs.size() - 1, QVector<bool>(ys.size() - 1, false));
        for (int ix = 0; ix < xs.size() - 1; ++ix) {
            for (int iy = 0; iy < ys.size() - 1; ++iy) {
                const float cx = (xs[ix] + xs[ix + 1]) * 0.5f;
                const float cy = (ys[iy] + ys[iy + 1]) * 0.5f;
                for (const MapRect &rect : rects) {
                    if (cx > rect.xMin && cx < rect.xMax && cy > rect.yMin && cy < rect.yMax) {
                        filled[ix][iy] = true;
                        break;
                    }
                }
            }
        }

        auto isFilled = [&filled](int ix, int iy) {
            return ix >= 0 && iy >= 0 && ix < filled.size() && iy < filled[ix].size() && filled[ix][iy];
        };
        auto addLine = [this, z](float x1, float y1, float x2, float y2) {
            if (std::abs(z) <= 0.0001f) {
                addVisibleFloorLine(x1, y1, x2, y2);
            } else {
                appendMapLine(x1, y1, z, x2, y2, z);
            }
        };

        for (int ix = 0; ix < xs.size() - 1; ++ix) {
            for (int iy = 0; iy < ys.size() - 1; ++iy) {
                if (!filled[ix][iy]) {
                    continue;
                }
                if (!isFilled(ix - 1, iy)) {
                    addLine(xs[ix], ys[iy], xs[ix], ys[iy + 1]);
                }
                if (!isFilled(ix + 1, iy)) {
                    addLine(xs[ix + 1], ys[iy], xs[ix + 1], ys[iy + 1]);
                }
                if (!isFilled(ix, iy - 1)) {
                    addLine(xs[ix], ys[iy], xs[ix + 1], ys[iy]);
                }
                if (!isFilled(ix, iy + 1)) {
                    addLine(xs[ix], ys[iy + 1], xs[ix + 1], ys[iy + 1]);
                }
            }
        }
    }

    void addMapRectLines(const MapRect &rect, float z, bool verticals)
    {
        const GpuPoint a {rect.xMin, rect.yMin, z, z};
        const GpuPoint b {rect.xMax, rect.yMin, z, z};
        const GpuPoint c {rect.xMax, rect.yMax, z, z};
        const GpuPoint d {rect.xMin, rect.yMax, z, z};

        appendMapLine(a.x, a.y, a.z, b.x, b.y, b.z);
        appendMapLine(b.x, b.y, b.z, c.x, c.y, c.z);
        appendMapLine(c.x, c.y, c.z, d.x, d.y, d.z);
        appendMapLine(d.x, d.y, d.z, a.x, a.y, a.z);

        if (!verticals) {
            return;
        }

        const GpuPoint a0 {rect.xMin, rect.yMin, 0.0f, 0.0f};
        const GpuPoint b0 {rect.xMax, rect.yMin, 0.0f, 0.0f};
        const GpuPoint c0 {rect.xMax, rect.yMax, 0.0f, 0.0f};
        const GpuPoint d0 {rect.xMin, rect.yMax, 0.0f, 0.0f};
        appendMapLine(a0.x, a0.y, a0.z, a.x, a.y, a.z);
        appendMapLine(b0.x, b0.y, b0.z, b.x, b.y, b.z);
        appendMapLine(c0.x, c0.y, c0.z, c.x, c.y, c.z);
        appendMapLine(d0.x, d0.y, d0.z, d.x, d.y, d.z);
    }

    void uploadToGpu()
    {
        if (!m_glReady) {
            return;
        }
        makeCurrent();

        m_pointVBO.bind();
        m_pointVBO.allocate(m_renderPoints.constData(), m_renderPoints.size() * sizeof(GpuPoint));
        m_pointVBO.release();
        m_pointCount = m_renderPoints.size();

        m_linePointCount = 0;
        if (!m_linePoints.isEmpty()) {
            m_lineVBO.bind();
            m_lineVBO.allocate(m_linePoints.constData(), m_linePoints.size() * sizeof(GpuPoint));
            m_lineVBO.release();
            m_linePointCount = m_linePoints.size();
        }

        doneCurrent();
    }

    void uploadMapToGpu()
    {
        if (!m_glReady) {
            return;
        }

        makeCurrent();
        m_mapLinePointCount = 0;
        if (!m_mapLinePoints.isEmpty()) {
            m_mapLineVBO.bind();
            m_mapLineVBO.allocate(m_mapLinePoints.constData(), m_mapLinePoints.size() * sizeof(GpuPoint));
            m_mapLineVBO.release();
            m_mapLinePointCount = m_mapLinePoints.size();
        }
        m_mapPointCount = 0;
        if (!m_mapPointPoints.isEmpty()) {
            m_mapPointVBO.bind();
            m_mapPointVBO.allocate(m_mapPointPoints.constData(), m_mapPointPoints.size() * sizeof(GpuPoint));
            m_mapPointVBO.release();
            m_mapPointCount = m_mapPointPoints.size();
        }
        doneCurrent();
    }

    void uploadMapFillToGpu()
    {
        if (!m_glReady) {
            return;
        }

        makeCurrent();
        QVector<GpuPoint> allFillPoints = m_mapFillPoints;
        allFillPoints += m_stageFillPoints;
        m_mapFillPointCount = m_mapFillPoints.size();
        m_stageFillPointCount = m_stageFillPoints.size();
        if (!allFillPoints.isEmpty()) {
            m_mapFillVBO.bind();
            m_mapFillVBO.allocate(allFillPoints.constData(), allFillPoints.size() * sizeof(GpuPoint));
            m_mapFillVBO.release();
        }
        doneCurrent();
    }

    void uploadRobotMarkersToGpu()
    {
        if (!m_glReady) {
            return;
        }

        makeCurrent();
        m_robotLinePointCount = 0;
        if (!m_robotLinePoints.isEmpty()) {
            m_robotLineVBO.bind();
            m_robotLineVBO.allocate(m_robotLinePoints.constData(), m_robotLinePoints.size() * sizeof(GpuPoint));
            m_robotLineVBO.release();
            m_robotLinePointCount = m_robotLinePoints.size();
        }
        doneCurrent();
    }

    void uploadCurrentRobotMarkerToGpu()
    {
        if (!m_glReady) {
            return;
        }

        makeCurrent();
        m_currentRobotFillPointCount = 0;
        if (!m_currentRobotFillPoints.isEmpty()) {
            m_currentRobotFillVBO.bind();
            m_currentRobotFillVBO.allocate(m_currentRobotFillPoints.constData(),
                                           m_currentRobotFillPoints.size() * sizeof(GpuPoint));
            m_currentRobotFillVBO.release();
            m_currentRobotFillPointCount = m_currentRobotFillPoints.size();
        }

        m_currentRobotLinePointCount = 0;
        if (!m_currentRobotLinePoints.isEmpty()) {
            m_currentRobotLineVBO.bind();
            m_currentRobotLineVBO.allocate(m_currentRobotLinePoints.constData(),
                                           m_currentRobotLinePoints.size() * sizeof(GpuPoint));
            m_currentRobotLineVBO.release();
            m_currentRobotLinePointCount = m_currentRobotLinePoints.size();
        }
        doneCurrent();
    }

    int m_selectedRobot = 0;
    uint32_t m_lastFrame = 0;
    bool m_hasLastFrame = false;
    QElapsedTimer m_lidarFrameTimer;
    QVector<RobotSnapshot> m_snapshots;
    QOpenGLShaderProgram m_prog;
    QOpenGLBuffer m_pointVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_lineVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_mapLineVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_mapPointVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_mapFillVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_robotLineVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_currentRobotFillVBO { QOpenGLBuffer::VertexBuffer };
    QOpenGLBuffer m_currentRobotLineVBO { QOpenGLBuffer::VertexBuffer };
    QVector<GpuPoint> m_renderPoints;
    QVector<GpuPoint> m_linePoints;
    QVector<GpuPoint> m_mapLinePoints;
    QVector<GpuPoint> m_mapPointPoints;
    QSet<QString> m_mapLineKeys;
    QVector<GpuPoint> m_mapFillPoints;
    QVector<GpuPoint> m_stageFillPoints;
    QVector<GpuPoint> m_robotLinePoints;
    QVector<GpuPoint> m_currentRobotFillPoints;
    QVector<GpuPoint> m_currentRobotLinePoints;
    int m_pointCount = 0;
    int m_linePointCount = 0;
    int m_mapLinePointCount = 0;
    int m_mapPointCount = 0;
    int m_mapFillPointCount = 0;
    int m_stageFillPointCount = 0;
    int m_robotLinePointCount = 0;
    int m_currentRobotFillPointCount = 0;
    int m_currentRobotLinePointCount = 0;
    float m_yaw = 30.0f;
    float m_pitch = 45.0f;
    float m_distance = 20.0f;
    QVector3D m_viewCenter {0.0f, 0.0f, 0.0f};
    MapConfig m_mapConfig;
    float m_robotX = 0.0f;
    float m_robotY = 0.0f;
    float m_robotTheta = 0.0f;
    float m_selectedDisplayX = 0.0f;
    float m_selectedDisplayY = 0.0f;
    bool m_haveSelectedDisplayPose = false;
    float m_firstPersonEyeZ = 0.20f;
    float m_firstPersonPitch = -8.0f;
    float m_firstPersonYawOffset = 0.0f;
    float m_odomAnchorX = 0.0f;
    float m_odomAnchorY = 0.0f;
    bool m_haveOdomAnchor = false;
    QHash<int, QPointF> m_robotOdomAnchors;
    QPoint m_lastMouse;
    float m_zMin = -0.5f;
    float m_zMax = 2.5f;
    float m_connThresh = 0.75f;
    bool m_glReady = false;
    bool m_globalPathsVisible = false;
    QSet<int> m_globalPathRobotIds;
    int m_locMvp = -1;
    int m_locZMin = -1;
    int m_locZMax = -1;
    int m_locIsLine = -1;
    int m_pulseTimerId = 0;
};

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(520, 360);
    QGridLayout *layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack = new QStackedWidget(this);
    m_map2d = new LidarMap2DView;
    m_map3d = new PointCloud3DView;
    m_map2d->setRouteSelectionChangedCallback([this]() {
        updateRouteButtonState();
    });
    m_stack->addWidget(m_map2d);
    m_stack->addWidget(m_map3d);
    layout->addWidget(m_stack, 0, 0);

    m_routeButton = new QPushButton(QStringLiteral("경로 선택"), this);
    m_routeButton->setCursor(Qt::PointingHandCursor);
    m_routeButton->setMinimumSize(92, 22);
    m_routeButton->setObjectName("routeButton");
    m_routeButton->setStyleSheet(
        "#routeButton {"
        " background:#071017;"
        " color:#dce7f3;"
        " border:1px solid #2b4050;"
        " border-radius:4px;"
        " padding:1px 10px;"
        " font-size:13px;"
        " font-weight:800;"
        "}"
        "#routeButton:hover { border-color:#ffd21a; color:#ffd21a; }"
        "#routeButton:pressed { background:#1b1805; color:#ffd21a; }"
        "#routeButton[ready=\"true\"] {"
        " background:#062115;"
        " color:#26d97c;"
        " border-color:#26d97c;"
        "}"
        "#routeButton[ready=\"true\"]:hover {"
        " color:#5dffa8;"
        " border-color:#5dffa8;"
        "}");
    layout->addWidget(m_routeButton, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    m_routeButton->hide();
    connect(m_routeButton, &QPushButton::clicked, this, [this]() {
        if (m_routeGenerationPending) {
            return;
        }
        if (!m_map2d->isRouteSelectionEnabled()) {
            m_map2d->setRouteSelectionEnabled(true);
            updateRouteButtonState();
            return;
        }
        if (m_map2d->routeSelectionComplete()) {
            const int selectedRobot = m_selectedRobot;
            const QPointF routeEnd = m_map2d->routeEnd();
            m_routeGenerationPending = true;
            updateRouteButtonState();
            QTimer::singleShot(500, this, [this, selectedRobot, routeEnd]() {
                emit routeGenerationRequested(selectedRobot, routeEnd);
                m_map2d->setRouteSelectionEnabled(false);
                m_routeGenerationPending = false;
                updateRouteButtonState();
            });
            return;
        }
        m_map2d->setRouteSelectionEnabled(false);
        updateRouteButtonState();
    });

    m_resetViewButton = new QPushButton("RESET VIEW", this);
    m_resetViewButton->setCursor(Qt::PointingHandCursor);
    m_resetViewButton->setFixedSize(m_routeButton->minimumSize());
    m_resetViewButton->setObjectName("resetViewButton");
    m_resetViewButton->setStyleSheet(
        "#resetViewButton {"
        " background:#071017;"
        " color:#dce7f3;"
        " border:1px solid #2b4050;"
        " border-radius:4px;"
        " padding:2px 9px;"
        " font-size:10px;"
        " font-weight:700;"
        "}"
        "#resetViewButton:hover { border-color:#ffd21a; color:#ffd21a; }"
        "#resetViewButton:pressed { background:#1b1805; color:#ffd21a; }");
    m_resetViewButton->hide();
    layout->addWidget(m_resetViewButton, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    connect(m_resetViewButton, &QPushButton::clicked, this, [this]() {
        m_map3d->resetFirstPersonView();
    });
}

bool MapWidget::loadMapConfig(const QString &path)
{
    MapConfig config;
    QString error;
    if (!config.loadFromYaml(path, &error)) {
        qWarning() << "MapWidget:" << error;
        m_mapLoaded = false;
        updateRouteButtonState();
        return false;
    }

    m_mapLoaded = true;
    m_map2d->setMapConfig(config);
    m_map3d->setMapConfig(config);
    updateRouteButtonState();
    return true;
}

void MapWidget::setSnapshots(const QVector<RobotSnapshot> &snapshots)
{
    m_snapshots = snapshots;
    m_map2d->setSnapshots(snapshots);
    m_map3d->setSnapshots(snapshots);
}

void MapWidget::setSelectedRobot(int robotId)
{
    m_selectedRobot = robotId;
    m_map2d->setSelectedRobot(robotId);
    m_map3d->setSelectedRobot(robotId);
    if (!m_snapshots.isEmpty()) {
        m_map2d->setSnapshots(m_snapshots);
        m_map3d->setSnapshots(m_snapshots);
    }
}

void MapWidget::showMoveCommandIndicators(const QVector<int> &robotIds)
{
    m_map2d->showMoveCommandIndicators(robotIds);
}

void MapWidget::showVictimDetection(int robotId)
{
    m_map2d->showVictimDetection(robotId);
}

void MapWidget::setViewMode3D(bool enabled)
{
    m_view3d = enabled;
    m_stack->setCurrentWidget(enabled ? static_cast<QWidget *>(m_map3d) : static_cast<QWidget *>(m_map2d));
    m_resetViewButton->setVisible(enabled);
    if (enabled && m_map2d->isRouteSelectionEnabled()) {
        m_map2d->setRouteSelectionEnabled(false);
    }
    updateRouteButtonState();
}

void MapWidget::setGlobalPathsVisible(bool visible)
{
    m_map2d->setGlobalPathsVisible(visible);
    m_map3d->setGlobalPathsVisible(visible);
}

void MapWidget::setGlobalPathRobotIds(const QVector<int> &robotIds)
{
    m_globalPathRobotIds = robotIds;
    applyGlobalPathRobotIds();
}

void MapWidget::addGlobalPathRobotIds(const QVector<int> &robotIds)
{
    for (int id : robotIds) {
        if (!m_globalPathRobotIds.contains(id)) {
            m_globalPathRobotIds.append(id);
        }
    }
    applyGlobalPathRobotIds();
}

void MapWidget::applyGlobalPathRobotIds()
{
    QSet<int> ids;
    ids.reserve(m_globalPathRobotIds.size());
    for (int id : m_globalPathRobotIds) {
        ids.insert(id);
    }
    m_map2d->setGlobalPathRobotIds(ids);
    m_map3d->setGlobalPathRobotIds(ids);
}

void MapWidget::fitToAvailableSize(float targetScale)
{
    m_map2d->fitToCurrentSize(targetScale);
}

void MapWidget::paintEvent(QPaintEvent *)
{
}

void MapWidget::updateRouteButtonState()
{
    if (!m_routeButton) {
        return;
    }

    const bool selectable = !m_view3d && m_mapLoaded;
    m_routeButton->setVisible(selectable);
    if (!selectable) {
        return;
    }
    if (m_routeGenerationPending) {
        m_routeButton->setText(QStringLiteral("경로 생성중.."));
        m_routeButton->setEnabled(false);
        m_routeButton->setProperty("ready", true);
        m_routeButton->style()->unpolish(m_routeButton);
        m_routeButton->style()->polish(m_routeButton);
        return;
    }

    const bool enabled = m_map2d->isRouteSelectionEnabled();
    const bool ready = m_map2d->routeSelectionComplete();
    m_routeButton->setEnabled(true);
    m_routeButton->setText(!enabled ? QStringLiteral("경로 선택")
                                    : (ready ? QStringLiteral("경로 생성")
                                             : QStringLiteral("선택 취소")));
    m_routeButton->setProperty("ready", ready);
    m_routeButton->style()->unpolish(m_routeButton);
    m_routeButton->style()->polish(m_routeButton);
}

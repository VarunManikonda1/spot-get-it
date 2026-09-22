#include "mainwindow.h"

#include <QAbstractItemView>
#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QDateTime>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputMethod>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QGradient>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

extern "C" {
#include "proto.h"
}

static QString robotName(int id)
{
    return QString("SPOT-%1").arg(id + 1, 2, 10, QLatin1Char('0'));
}

static constexpr int kAllRobotsSelection = -1;
static constexpr int kDisplaySwapA = 0;
static constexpr int kDisplaySwapB = 4;
static constexpr float kDashboardMapStartupScale = 1.2f;
static constexpr float kManualMaxForwardMps = 0.10f;
static constexpr float kManualMaxReverseMps = 0.10f;
static constexpr float kManualMaxYawRadps = 0.40f;
static constexpr float kManualCommandDeadband = 0.06f;
static constexpr int kManualActionStand = 1;
static constexpr int kManualActionSit = 2;
static constexpr int kManualActionGreet = 1;

static int swappedRobotId(int robotId)
{
    if (robotId == kDisplaySwapA) {
        return kDisplaySwapB;
    }
    if (robotId == kDisplaySwapB) {
        return kDisplaySwapA;
    }
    return robotId;
}

static int displayToPhysicalRobotId(int displayRobotId)
{
    return swappedRobotId(displayRobotId);
}

static int physicalToDisplayRobotId(int physicalRobotId)
{
    return swappedRobotId(physicalRobotId);
}

static bool isHardcodedDisplayRobot(int displayRobotId)
{
    Q_UNUSED(displayRobotId);
    return false;
}

static bool showsPathOnDeploy(int displayRobotId)
{
    return displayRobotId == 4;
}

static int featuredVideoRobotForSelection(int selectedRobot, int robotCount)
{
    if (robotCount <= 0) {
        return -1;
    }
    const int clamped = qBound(1, robotCount, kMaxRobots);
    if (selectedRobot == kAllRobotsSelection) {
        return clamped == 5 ? 4 : 0;
    }
    return (selectedRobot >= 0 && selectedRobot < clamped) ? selectedRobot : 0;
}

static RobotSnapshot hardcodedDisplaySnapshot(int displayId)
{
    RobotSnapshot snapshot;
    snapshot.id = displayId;
    return snapshot;
}

static QVector<RobotSnapshot> remapSnapshotsForDisplay(const QVector<RobotSnapshot> &snapshots)
{
    int displaySize = snapshots.size();
    for (const RobotSnapshot &snapshot : snapshots) {
        displaySize = qMax(displaySize, snapshot.id + 1);
    }
    displaySize = qMax(displaySize, qMax(kDisplaySwapA, kDisplaySwapB) + 1);
    QVector<RobotSnapshot> display(displaySize);
    for (int i = 0; i < display.size(); ++i) {
        display[i] = hardcodedDisplaySnapshot(i);
    }

    for (const RobotSnapshot &snapshot : snapshots) {
        const int displayId = physicalToDisplayRobotId(snapshot.id);

        if (displayId >= 0 && displayId < display.size()) {
            RobotSnapshot remapped = snapshot;
            remapped.id = displayId;
            display[displayId] = remapped;
        }
    }
    return display;
}

static QVector<RobotSnapshot> snapshotsForDeployedRobots(const QVector<RobotSnapshot> &snapshots, int robotCount)
{
    QVector<RobotSnapshot> deployed;
    const int count = qBound(0, robotCount, snapshots.size());
    deployed.reserve(count);
    for (int i = 0; i < count; ++i) {
        deployed.append(snapshots[i]);
    }
    return deployed;
}

static bool routeEligibleSnapshot(const RobotSnapshot &snapshot)
{
    return !isHardcodedDisplayRobot(snapshot.id)
        && snapshot.shmOpen && snapshot.connected && snapshot.faultLevel < 3;
}

static QVector<int> deployedRobotIds(const QVector<RobotSnapshot> &snapshots, int robotCount)
{
    QVector<int> ids;
    const QVector<RobotSnapshot> deployedSnapshots = snapshotsForDeployedRobots(snapshots, robotCount);
    ids.reserve(deployedSnapshots.size());
    for (const RobotSnapshot &snapshot : deployedSnapshots) {
        if (snapshot.id >= 0 && snapshot.id < kMaxRobots
            && !isHardcodedDisplayRobot(snapshot.id)) {
            ids.append(snapshot.id);
        }
    }
    return ids;
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

static constexpr int kDefaultRobotCount = 0;
static constexpr int kRobotCardGridColumns = 2;
static constexpr int kVideoLayoutBaseRows = 4;
static constexpr int kVideoLayoutBaseColumns = 4;
static constexpr int kVideoLayoutScale = 4;
static constexpr int kVideoLayoutRows = kVideoLayoutBaseRows * kVideoLayoutScale;
static constexpr int kVideoLayoutColumns = kVideoLayoutBaseColumns * kVideoLayoutScale;

struct GridSpec {
    int columns = 1;
    int rows = 1;
};

struct VideoLayoutSlot {
    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
};

struct VideoLayoutSpec {
    int rows = 1;
    int columns = 1;
    QVector<int> rowStretch;
    QVector<int> columnStretch;
    QVector<VideoLayoutSlot> placements;
    bool hasFeaturedTile = false;
};

static GridSpec statusGridSpec(int count)
{
    const int clamped = qBound(0, count, kMaxRobots);
    if (clamped <= 0) {
        return {1, 1};
    }
    return {1, clamped};
}

static VideoLayoutSpec videoLayoutSpec(int count)
{
    const int clamped = qBound(0, count, kMaxRobots);
    switch (clamped) {
    case 0:
        return {0, 0, {}, {}, {}, false};
    case 1:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1}, {{0, 0, 4, 4}}, false};
    case 2:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 4, 2}, {0, 2, 4, 2}}, false};
    case 3:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 4, 2}, {0, 2, 2, 2}, {2, 2, 2, 2}}, true};
    case 4:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 2, 2}, {0, 2, 2, 2}, {2, 0, 2, 2}, {2, 2, 2, 2}}, false};
    case 5:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 4, 2}, {0, 2, 2, 1}, {0, 3, 2, 1},
                 {2, 2, 2, 1}, {2, 3, 2, 1}}, true};
    case 6:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 2, 2}, {0, 2, 1, 2}, {2, 0, 2, 1},
                 {2, 1, 1, 1}, {1, 2, 2, 2}, {3, 1, 1, 3}}, true};
    case 7:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 2, 2}, {0, 2, 1, 1}, {0, 3, 1, 1},
                 {1, 2, 1, 1}, {1, 3, 1, 1}, {2, 0, 2, 2},
                 {2, 2, 2, 2}}, true};
    case 8:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 2, 2}, {0, 2, 1, 1}, {0, 3, 1, 1},
                 {1, 2, 1, 1}, {1, 3, 1, 1}, {2, 0, 1, 1},
                 {2, 1, 1, 1}, {2, 2, 2, 2}}, true};
    case 9:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 1, 1}, {0, 1, 1, 1}, {0, 2, 1, 1},
                 {1, 0, 1, 1}, {1, 1, 1, 1}, {1, 2, 1, 1},
                 {2, 0, 1, 1}, {2, 1, 1, 1}, {2, 2, 2, 2}}, false};
    default:
        return {4, 4, {1, 1, 1, 1}, {1, 1, 1, 1},
                {{0, 0, 1, 1}, {0, 1, 1, 1}, {0, 2, 1, 1},
                 {0, 3, 1, 1}, {1, 0, 1, 1}, {1, 1, 1, 1},
                 {1, 2, 1, 1}, {1, 3, 1, 1}, {2, 0, 2, 2},
                 {2, 2, 2, 2}}, false};
    }
}

static bool videoSlotInBounds(const VideoLayoutSlot &slot)
{
    return slot.row >= 0 && slot.column >= 0
        && slot.rowSpan >= 1 && slot.columnSpan >= 1
        && slot.row + slot.rowSpan <= kVideoLayoutRows
        && slot.column + slot.columnSpan <= kVideoLayoutColumns;
}

static int videoSlotArea(const VideoLayoutSlot &slot)
{
    return slot.rowSpan * slot.columnSpan;
}

static VideoLayoutSlot scaledVideoSlot(const VideoLayoutSlot &slot)
{
    return {slot.row * kVideoLayoutScale,
            slot.column * kVideoLayoutScale,
            slot.rowSpan * kVideoLayoutScale,
            slot.columnSpan * kVideoLayoutScale};
}

static void splitVideoRect(const VideoLayoutSlot &rect, int count,
                           QVector<VideoLayoutSlot> *placements)
{
    if (count <= 0 || !videoSlotInBounds(rect) || videoSlotArea(rect) < count) {
        return;
    }
    if (count == 1) {
        placements->append(rect);
        return;
    }

    const bool splitColumns = rect.columnSpan >= rect.rowSpan;
    const int firstCount = count / 2;
    const int secondCount = count - firstCount;

    if (splitColumns && rect.columnSpan >= 2) {
        int firstSpan = qRound(static_cast<double>(rect.columnSpan) * firstCount / count);
        firstSpan = qBound(1, firstSpan, rect.columnSpan - 1);
        while (firstSpan * rect.rowSpan < firstCount && firstSpan < rect.columnSpan - 1) {
            ++firstSpan;
        }
        while ((rect.columnSpan - firstSpan) * rect.rowSpan < secondCount && firstSpan > 1) {
            --firstSpan;
        }
        splitVideoRect({rect.row, rect.column, rect.rowSpan, firstSpan}, firstCount, placements);
        splitVideoRect({rect.row, rect.column + firstSpan, rect.rowSpan,
                        rect.columnSpan - firstSpan}, secondCount, placements);
        return;
    }

    if (rect.rowSpan >= 2) {
        int firstSpan = qRound(static_cast<double>(rect.rowSpan) * firstCount / count);
        firstSpan = qBound(1, firstSpan, rect.rowSpan - 1);
        while (firstSpan * rect.columnSpan < firstCount && firstSpan < rect.rowSpan - 1) {
            ++firstSpan;
        }
        while ((rect.rowSpan - firstSpan) * rect.columnSpan < secondCount && firstSpan > 1) {
            --firstSpan;
        }
        splitVideoRect({rect.row, rect.column, firstSpan, rect.columnSpan}, firstCount, placements);
        splitVideoRect({rect.row + firstSpan, rect.column, rect.rowSpan - firstSpan,
                        rect.columnSpan}, secondCount, placements);
    }
}

static bool videoSlotCellsAvailable(const VideoLayoutSlot &slot,
                                    const bool occupied[kVideoLayoutRows][kVideoLayoutColumns],
                                    const bool covered[kVideoLayoutRows][kVideoLayoutColumns])
{
    if (!videoSlotInBounds(slot)) {
        return false;
    }

    for (int row = slot.row; row < slot.row + slot.rowSpan; ++row) {
        for (int col = slot.column; col < slot.column + slot.columnSpan; ++col) {
            if (occupied[row][col] || covered[row][col]) {
                return false;
            }
        }
    }
    return true;
}

static int remainingFreeVideoCells(const bool occupied[kVideoLayoutRows][kVideoLayoutColumns],
                                   const bool covered[kVideoLayoutRows][kVideoLayoutColumns])
{
    int count = 0;
    for (int row = 0; row < kVideoLayoutRows; ++row) {
        for (int col = 0; col < kVideoLayoutColumns; ++col) {
            if (!occupied[row][col] && !covered[row][col]) {
                ++count;
            }
        }
    }
    return count;
}

static bool partitionVideoCells(const bool occupied[kVideoLayoutRows][kVideoLayoutColumns],
                                bool covered[kVideoLayoutRows][kVideoLayoutColumns],
                                int slotsLeft,
                                QVector<VideoLayoutSlot> *placements)
{
    int firstRow = -1;
    int firstCol = -1;
    for (int row = 0; row < kVideoLayoutRows && firstRow < 0; ++row) {
        for (int col = 0; col < kVideoLayoutColumns; ++col) {
            if (!occupied[row][col] && !covered[row][col]) {
                firstRow = row;
                firstCol = col;
                break;
            }
        }
    }

    if (firstRow < 0) {
        return slotsLeft == 0;
    }
    if (slotsLeft <= 0) {
        return false;
    }

    const int freeCells = remainingFreeVideoCells(occupied, covered);
    if (freeCells < slotsLeft) {
        return false;
    }

    QVector<VideoLayoutSlot> candidates;
    for (int rowSpan = 1; firstRow + rowSpan <= kVideoLayoutRows; ++rowSpan) {
        for (int columnSpan = 1; firstCol + columnSpan <= kVideoLayoutColumns; ++columnSpan) {
            VideoLayoutSlot slot{firstRow, firstCol, rowSpan, columnSpan};
            if (!videoSlotCellsAvailable(slot, occupied, covered)) {
                continue;
            }
            if (freeCells - videoSlotArea(slot) < slotsLeft - 1) {
                continue;
            }
            candidates.append(slot);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const VideoLayoutSlot &a,
                                                       const VideoLayoutSlot &b) {
        const int areaA = videoSlotArea(a);
        const int areaB = videoSlotArea(b);
        if (areaA != areaB) {
            return areaA > areaB;
        }
        const int balanceA = qAbs(a.rowSpan - a.columnSpan);
        const int balanceB = qAbs(b.rowSpan - b.columnSpan);
        if (balanceA != balanceB) {
            return balanceA < balanceB;
        }
        return a.columnSpan > b.columnSpan;
    });

    for (const VideoLayoutSlot &slot : candidates) {
        for (int row = slot.row; row < slot.row + slot.rowSpan; ++row) {
            for (int col = slot.column; col < slot.column + slot.columnSpan; ++col) {
                covered[row][col] = true;
            }
        }
        placements->append(slot);
        if (partitionVideoCells(occupied, covered, slotsLeft - 1, placements)) {
            return true;
        }
        placements->removeLast();
        for (int row = slot.row; row < slot.row + slot.rowSpan; ++row) {
            for (int col = slot.column; col < slot.column + slot.columnSpan; ++col) {
                covered[row][col] = false;
            }
        }
    }

    return false;
}

static bool arrangeRemainingVideoSlots(const VideoLayoutSlot &fixedSlot,
                                       int remainingTileCount,
                                       QVector<VideoLayoutSlot> *placements)
{
    if (!videoSlotInBounds(fixedSlot)) {
        return false;
    }

    const int freeCells = kVideoLayoutRows * kVideoLayoutColumns - videoSlotArea(fixedSlot);
    if (freeCells < remainingTileCount) {
        return false;
    }

    placements->clear();

    struct Region {
        VideoLayoutSlot rect;
        int count = 0;
    };

    QVector<Region> regions;
    if (fixedSlot.row > 0) {
        regions.append({{0, 0, fixedSlot.row, kVideoLayoutColumns}, 0});
    }
    const int fixedBottom = fixedSlot.row + fixedSlot.rowSpan;
    if (fixedBottom < kVideoLayoutRows) {
        regions.append({{fixedBottom, 0, kVideoLayoutRows - fixedBottom, kVideoLayoutColumns}, 0});
    }
    if (fixedSlot.column > 0) {
        regions.append({{fixedSlot.row, 0, fixedSlot.rowSpan, fixedSlot.column}, 0});
    }
    const int fixedRight = fixedSlot.column + fixedSlot.columnSpan;
    if (fixedRight < kVideoLayoutColumns) {
        regions.append({{fixedSlot.row, fixedRight, fixedSlot.rowSpan,
                         kVideoLayoutColumns - fixedRight}, 0});
    }

    if (remainingTileCount == 0) {
        return true;
    }
    if (regions.isEmpty()) {
        return false;
    }

    for (int i = 0; i < remainingTileCount; ++i) {
        int bestIndex = -1;
        double bestScore = -1.0;
        for (int regionIndex = 0; regionIndex < regions.size(); ++regionIndex) {
            const int area = videoSlotArea(regions[regionIndex].rect);
            if (regions[regionIndex].count >= area) {
                continue;
            }
            const double score = static_cast<double>(area) / (regions[regionIndex].count + 1);
            if (score > bestScore) {
                bestScore = score;
                bestIndex = regionIndex;
            }
        }
        if (bestIndex < 0) {
            return false;
        }
        ++regions[bestIndex].count;
    }

    for (const Region &region : regions) {
        splitVideoRect(region.rect, region.count, placements);
    }

    return placements->size() == remainingTileCount;
}

static int demoRobotCountFromEnvironment()
{
    const QByteArray value = qgetenv("DISASTER_QT_DEMO_ROBOTS");
    if (value.isEmpty()) {
        return 0;
    }

    bool ok = false;
    const int count = value.toInt(&ok);
    return ok ? qBound(1, count, kMaxRobots) : 0;
}

static int missionProgressPercent(const RobotSnapshot &snapshot)
{
    const float percent = snapshot.missionProgress > 1.0f
        ? snapshot.missionProgress
        : snapshot.missionProgress * 100.0f;
    return qBound(0, qRound(percent), 100);
}

static bool missionComplete(const RobotSnapshot &snapshot)
{
    return snapshot.goalReached || missionProgressPercent(snapshot) >= 100;
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

static QString robotMissionStateText(const RobotSnapshot &snapshot)
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
        return QStringLiteral("탐색 중");
    }
    return QStringLiteral("대기");
}

static bool hasPathProgressData(const RobotSnapshot &snapshot)
{
    return snapshot.pathOk || snapshot.poseOk || snapshot.goalReached
        || snapshot.waypointIdx > 0 || snapshot.missionProgress > 0.0f
        || snapshot.distanceToGoalM > 0.0f;
}

static QString pathDistanceSummary(const RobotSnapshot &snapshot)
{
    if (!hasPathProgressData(snapshot)
        || !std::isfinite(snapshot.distanceToGoalM)
        || snapshot.distanceToGoalM <= 0.0f) {
        return QStringLiteral("--");
    }
    return QString("%1 m").arg(snapshot.distanceToGoalM, 0, 'f', 1);
}

static QString waypointSummary(const RobotSnapshot &snapshot)
{
    if (snapshot.totalWaypoints > 0) {
        return QString("%1/%2").arg(snapshot.waypointIdx).arg(snapshot.totalWaypoints);
    }
    return QString::number(snapshot.waypointIdx);
}

class NavWatermark : public QWidget
{
public:
    explicit NavWatermark(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(190);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF frame = rect().adjusted(2.0, 2.0, -2.0, -2.0);
        p.setPen(QPen(QColor(255, 216, 64), 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(8, 19, 31));
        p.drawRoundedRect(frame, 16.0, 16.0);

        p.setClipPath([frame]() {
            QPainterPath path;
            path.addRoundedRect(frame.adjusted(3.0, 3.0, -3.0, -3.0), 12.0, 12.0);
            return path;
        }());

        auto toPoint = [frame](qreal x, qreal y) {
            return QPointF(frame.left() + frame.width() * x,
                           frame.top() + frame.height() * y);
        };
        auto drawStroke = [&p, &toPoint](std::initializer_list<QPointF> points, qreal width = 3.0) {
            if (points.size() < 2) {
                return;
            }
            QPainterPath path;
            auto it = points.begin();
            path.moveTo(toPoint(it->x(), it->y()));
            QPointF prev = *it++;
            for (; it != points.end(); ++it) {
                const QPointF current = *it;
                const QPointF mid((prev.x() + current.x()) * 0.5, (prev.y() + current.y()) * 0.5);
                path.quadTo(toPoint(prev.x(), prev.y()), toPoint(mid.x(), mid.y()));
                prev = current;
            }
            path.lineTo(toPoint(prev.x(), prev.y()));
            p.setPen(QPen(Qt::white, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        };

        drawStroke({{0.58, 0.06}, {0.36, 0.09}, {0.31, 0.13}, {0.43, 0.16}, {0.50, 0.18},
                    {0.35, 0.22}, {0.24, 0.23}}, 2.8);
        drawStroke({{0.57, 0.18}, {0.52, 0.25}, {0.47, 0.33}, {0.35, 0.41}}, 3.0);
        drawStroke({{0.50, 0.19}, {0.69, 0.20}, {0.70, 0.25}, {0.57, 0.26}}, 3.0);
        drawStroke({{0.43, 0.46}, {0.34, 0.49}, {0.31, 0.54}, {0.34, 0.58}, {0.48, 0.57},
                    {0.55, 0.51}, {0.50, 0.46}}, 3.0);
        drawStroke({{0.23, 0.66}, {0.52, 0.68}, {0.72, 0.69}}, 3.0);
        drawStroke({{0.54, 0.69}, {0.43, 0.74}, {0.35, 0.81}}, 3.0);

        drawStroke({{0.24, 0.91}, {0.45, 0.91}, {0.60, 0.92}, {0.47, 0.98}, {0.30, 1.05},
                    {0.21, 1.00}, {0.18, 0.96}, {0.22, 0.92}, {0.31, 0.88}}, 3.1);
        drawStroke({{0.66, 0.89}, {0.62, 0.99}, {0.83, 0.98}}, 3.1);
        drawStroke({{0.65, 0.89}, {0.82, 0.88}, {0.96, 0.88}}, 3.1);
        drawStroke({{0.64, 0.94}, {0.79, 0.94}}, 3.1);
        drawStroke({{0.78, 0.88}, {1.08, 0.88}}, 3.1);
        drawStroke({{0.94, 0.88}, {0.92, 1.07}}, 3.1);
    }
};

class NavBrandWidget : public QWidget
{
public:
    explicit NavBrandWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setObjectName(QStringLiteral("navBrand"));
        setStyleSheet(QStringLiteral("background:transparent; border:0;"));
        setFixedSize(98, 104);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        loadLogo();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (!m_logo.isNull()) {
            const QRectF target(0.0, 0.0, 98.0, 102.0);
            QImage softened(target.size().toSize(), QImage::Format_ARGB32_Premultiplied);
            softened.fill(Qt::transparent);
            QPainter imgPainter(&softened);
            imgPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            imgPainter.drawImage(QRectF(QPointF(0.0, 0.0), QSizeF(softened.size())),
                                 m_logo,
                                 QRectF(QPointF(0.0, 0.0), QSizeF(m_logo.size())));

            QLinearGradient leftFade(0.0, 0.0, 7.0, 0.0);
            leftFade.setColorAt(0.0, QColor(0, 0, 0, 0));
            leftFade.setColorAt(1.0, QColor(0, 0, 0, 255));
            QLinearGradient rightFade(softened.width(), 0.0, softened.width() - 3.0, 0.0);
            rightFade.setColorAt(0.0, QColor(0, 0, 0, 0));
            rightFade.setColorAt(1.0, QColor(0, 0, 0, 255));
            QLinearGradient topFade(0.0, 0.0, 0.0, 6.0);
            topFade.setColorAt(0.0, QColor(0, 0, 0, 0));
            topFade.setColorAt(1.0, QColor(0, 0, 0, 255));
            QLinearGradient bottomFade(0.0, softened.height(), 0.0, softened.height() - 15.0);
            bottomFade.setColorAt(0.0, QColor(0, 0, 0, 0));
            bottomFade.setColorAt(1.0, QColor(0, 0, 0, 255));
            imgPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            imgPainter.fillRect(QRectF(0.0, 0.0, 7.0, softened.height()), leftFade);
            imgPainter.fillRect(QRectF(softened.width() - 3.0, 0.0, 3.0, softened.height()), rightFade);
            imgPainter.fillRect(QRectF(0.0, 0.0, softened.width(), 6.0), topFade);
            imgPainter.fillRect(QRectF(0.0, softened.height() - 15.0, softened.width(), 15.0), bottomFade);
            imgPainter.end();

            p.drawImage(target.topLeft(), softened);
        }
    }

private:
    void loadLogo()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString cwd = QDir::currentPath();
        const QString fileName = QStringLiteral("nav_logo.png");
        const QStringList candidates = {
            QDir(appDir).absoluteFilePath("../image_data/" + fileName),
            QDir(appDir).absoluteFilePath("../../image_data/" + fileName),
            QDir(appDir).absoluteFilePath("../../../image_data/" + fileName),
            QDir(cwd).absoluteFilePath("image_data/" + fileName),
            QDir(cwd).absoluteFilePath("../image_data/" + fileName),
            QDir(cwd).absoluteFilePath("../../image_data/" + fileName),
            QStringLiteral("/home/pi/robot_project/image_data/") + fileName
        };
        for (const QString &path : candidates) {
            QFileInfo info(path);
            if (info.exists() && info.isFile()) {
                m_logo.load(info.canonicalFilePath());
                return;
            }
        }
    }

    QImage m_logo;
};

class SideNavButton : public QPushButton
{
public:
    explicit SideNavButton(const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setMinimumSize(88, 96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const bool active = objectName() == QStringLiteral("navButtonActive");
        const bool hover = underMouse();
        const QColor accent = active ? QColor(226, 184, 57) : QColor(145, 158, 171);
        const QColor textColor = active ? QColor(242, 201, 76) : QColor(164, 176, 188);
        QRectF card = rect().adjusted(2.0, 4.0, -2.0, -4.0);

        if (active) {
            QLinearGradient fill(card.topLeft(), card.bottomLeft());
            fill.setColorAt(0.0, QColor(43, 34, 8, 232));
            fill.setColorAt(0.55, QColor(31, 27, 13, 228));
            fill.setColorAt(1.0, QColor(17, 21, 18, 226));
            p.setPen(QPen(QColor(130, 99, 18, 110), 2.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(card.adjusted(2.0, 2.0, -2.0, -2.0), 8.0, 8.0);
            p.setPen(QPen(QColor(224, 171, 33), 0.9));
            p.setBrush(fill);
            p.drawRoundedRect(card, 7.0, 7.0);
        } else {
            if (hover) {
                p.setPen(QPen(QColor(55, 78, 98), 1.0));
                p.setBrush(QColor(9, 22, 33, 150));
                p.drawRoundedRect(card.adjusted(4.0, 4.0, -4.0, -4.0), 7.0, 7.0);
            }
        }

        const QPointF iconCenter(width() * 0.5, card.top() + 30.0);
        drawIcon(&p, iconCenter, accent);

        QFont font(QStringLiteral("Noto Sans"), active ? 12 : 12, QFont::Black);
        p.setFont(font);
        p.setPen(textColor);
        p.drawText(QRectF(card.left() + 4.0, card.top() + 60.0,
                          card.width() - 8.0, card.height() - 64.0),
                   Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   text().replace('\n', ' '));
    }

private:
    void drawIcon(QPainter *p, const QPointF &c, const QColor &color)
    {
        const QString label = text().remove('\n');
        QPen glow(QColor(color.red(), color.green(), color.blue(), 44), 3.0,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        QPen pen(color, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        p->save();
        p->translate(c);
        p->scale(0.86, 0.86);
        p->translate(-c);
        p->setBrush(Qt::NoBrush);
        if (label == QStringLiteral("관제")) {
            p->setPen(glow);
            p->drawLine(QPointF(c.x(), c.y() - 17.0), QPointF(c.x(), c.y() - 6.0));
            p->drawLine(QPointF(c.x() - 14.0, c.y() + 5.0), QPointF(c.x() - 5.0, c.y() - 3.0));
            p->drawLine(QPointF(c.x() + 14.0, c.y() + 5.0), QPointF(c.x() + 5.0, c.y() - 3.0));
            p->drawLine(QPointF(c.x() - 14.0, c.y() + 5.0), QPointF(c.x() - 14.0, c.y() + 17.0));
            p->drawLine(QPointF(c.x() + 14.0, c.y() + 5.0), QPointF(c.x() + 14.0, c.y() + 17.0));
            p->setPen(pen);
            p->drawLine(QPointF(c.x(), c.y() - 17.0), QPointF(c.x(), c.y() - 6.0));
            p->drawLine(QPointF(c.x() - 14.0, c.y() + 5.0), QPointF(c.x() - 5.0, c.y() - 3.0));
            p->drawLine(QPointF(c.x() + 14.0, c.y() + 5.0), QPointF(c.x() + 5.0, c.y() - 3.0));
            p->drawLine(QPointF(c.x() - 14.0, c.y() + 5.0), QPointF(c.x() - 14.0, c.y() + 17.0));
            p->drawLine(QPointF(c.x() + 14.0, c.y() + 5.0), QPointF(c.x() + 14.0, c.y() + 17.0));
            p->drawRoundedRect(QRectF(c.x() - 7.0, c.y() - 7.0, 14.0, 14.0), 3.0, 3.0);
            p->drawRoundedRect(QRectF(c.x() - 21.0, c.y() + 13.0, 14.0, 14.0), 3.0, 3.0);
            p->drawRoundedRect(QRectF(c.x() + 7.0, c.y() + 13.0, 14.0, 14.0), 3.0, 3.0);
            p->setBrush(color);
            p->drawEllipse(QPointF(c.x(), c.y()), 2.0, 2.0);
            p->drawEllipse(QPointF(c.x() - 14.0, c.y() + 20.0), 2.0, 2.0);
            p->drawEllipse(QPointF(c.x() + 14.0, c.y() + 20.0), 2.0, 2.0);
        } else if (label == QStringLiteral("로봇상태")) {
            const QRectF panel(c.x() - 22.0, c.y() - 17.0, 44.0, 33.0);
            p->setPen(glow);
            p->drawRoundedRect(panel, 3.5, 3.5);
            p->setPen(pen);
            p->drawRoundedRect(panel, 3.5, 3.5);
            p->drawLine(QPointF(c.x() - 14.0, c.y() + 21.0), QPointF(c.x() + 14.0, c.y() + 21.0));
            p->drawLine(QPointF(c.x(), c.y() + 16.0), QPointF(c.x(), c.y() + 21.0));

            QPainterPath pulse;
            pulse.moveTo(c.x() - 15.0, c.y() - 2.0);
            pulse.lineTo(c.x() - 8.0, c.y() - 2.0);
            pulse.lineTo(c.x() - 4.0, c.y() - 9.0);
            pulse.lineTo(c.x() + 1.0, c.y() + 6.0);
            pulse.lineTo(c.x() + 6.0, c.y() - 4.0);
            pulse.lineTo(c.x() + 15.0, c.y() - 4.0);
            p->drawPath(pulse);

            p->drawLine(QPointF(c.x() - 15.0, c.y() + 8.0), QPointF(c.x() + 15.0, c.y() + 8.0));
            p->setBrush(color);
            p->drawRect(QRectF(c.x() - 15.0, c.y() + 10.0, 4.0, 3.2));
            p->drawRect(QRectF(c.x() - 5.0, c.y() + 10.0, 4.0, 3.2));
            p->drawRect(QRectF(c.x() + 5.0, c.y() + 10.0, 4.0, 3.2));
            p->drawEllipse(QPointF(c.x() + 15.0, c.y() + 11.5), 1.7, 1.7);
        } else if (label == QStringLiteral("드론")) {
            const QPointF rotorOffsets[] = {
                QPointF(-16.0, -12.0), QPointF(16.0, -12.0),
                QPointF(-16.0, 12.0), QPointF(16.0, 12.0)
            };
            p->setPen(glow);
            p->drawLine(QPointF(c.x() - 12.0, c.y() - 8.0), QPointF(c.x() + 12.0, c.y() + 8.0));
            p->drawLine(QPointF(c.x() + 12.0, c.y() - 8.0), QPointF(c.x() - 12.0, c.y() + 8.0));
            for (const QPointF &offset : rotorOffsets) {
                p->drawEllipse(c + offset, 6.0, 6.0);
            }
            p->setPen(pen);
            p->drawLine(QPointF(c.x() - 12.0, c.y() - 8.0), QPointF(c.x() + 12.0, c.y() + 8.0));
            p->drawLine(QPointF(c.x() + 12.0, c.y() - 8.0), QPointF(c.x() - 12.0, c.y() + 8.0));
            for (const QPointF &offset : rotorOffsets) {
                p->drawEllipse(c + offset, 6.0, 6.0);
            }
            p->setBrush(color);
            p->drawRoundedRect(QRectF(c.x() - 7.5, c.y() - 5.5, 15.0, 11.0), 3.0, 3.0);
            p->drawEllipse(c, 2.0, 2.0);
        } else if (label == QStringLiteral("로그")) {
            QPainterPath doc;
            doc.moveTo(c.x() - 14.0, c.y() - 20.0);
            doc.lineTo(c.x() + 6.0, c.y() - 20.0);
            doc.lineTo(c.x() + 18.0, c.y() - 8.0);
            doc.lineTo(c.x() + 18.0, c.y() + 20.0);
            doc.lineTo(c.x() - 14.0, c.y() + 20.0);
            doc.closeSubpath();
            p->setPen(glow);
            p->drawPath(doc);
            p->setPen(pen);
            p->drawPath(doc);
            p->drawLine(QPointF(c.x() + 6.0, c.y() - 20.0), QPointF(c.x() + 6.0, c.y() - 8.0));
            p->drawLine(QPointF(c.x() + 6.0, c.y() - 8.0), QPointF(c.x() + 18.0, c.y() - 8.0));
            p->drawLine(QPointF(c.x() - 7.0, c.y() - 1.0), QPointF(c.x() + 7.0, c.y() - 1.0));
            p->drawLine(QPointF(c.x() - 7.0, c.y() + 8.0), QPointF(c.x() + 10.0, c.y() + 8.0));
        } else {
            QPainterPath gear;
            for (int i = 0; i < 16; ++i) {
                const qreal a = (-90.0 + i * 22.5) * M_PI / 180.0;
                const qreal r = (i % 2 == 0) ? 20.0 : 15.0;
                const QPointF pt(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
                i == 0 ? gear.moveTo(pt) : gear.lineTo(pt);
            }
            gear.closeSubpath();
            p->setPen(glow);
            p->drawPath(gear);
            p->setPen(pen);
            p->drawPath(gear);
            p->drawEllipse(c, 7.5, 7.5);
        }
        p->restore();
    }
};

class CommandButton : public QPushButton
{
public:
    explicit CommandButton(const QString &text, QWidget *parent = nullptr)
        : QPushButton(text, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        QStyleOptionButton option;
        initStyleOption(&option);
        const QString label = option.text;
        option.text.clear();
        style()->drawControl(QStyle::CE_PushButton, &option, &p, this);

        if (property("striped").toBool()) {
            drawStripes(&p);
        }

        const QString kind = property("commandKind").toString();
        if (kind.isEmpty()) {
            drawLabel(&p, label, rect().adjusted(8, 0, -8, 0), Qt::AlignCenter);
            return;
        }
        if (kind == QStringLiteral("auto")) {
            drawLabel(&p, label, rect().adjusted(8, 0, -8, 0), Qt::AlignCenter | Qt::TextWordWrap);
            return;
        }

        const QRectF iconRect(width() * 0.5 - 24.0, height() * 0.20, 48.0, 48.0);
        drawCommandIcon(&p, iconRect, kind);
        drawLabel(&p, label, rect().adjusted(8, qRound(height() * 0.54), -8, 8),
                  Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap);
    }

private:
    void drawStripes(QPainter *p)
    {
        p->save();
        QPainterPath clip;
        clip.addRoundedRect(rect().adjusted(2, 2, -2, -2), 7, 7);
        p->setClipPath(clip);

        const QColor stripeColor = objectName() == QStringLiteral("redCommand")
            ? QColor(255, 120, 120, isEnabled() ? 38 : 18)
            : QColor(255, 241, 166, isEnabled() ? 32 : 14);
        p->setPen(QPen(stripeColor, 1.2, Qt::SolidLine, Qt::FlatCap));
        const int spacing = 16;
        for (int x = -height(); x < width() + height(); x += spacing) {
            p->drawLine(QPointF(x, height()), QPointF(x + height(), 0));
        }
        p->restore();
    }

    void drawLabel(QPainter *p, const QString &text, const QRect &textRect, int flags)
    {
        QFont f = font();
        f.setBold(true);
        p->setFont(f);
        QColor color = isEnabled() ? palette().color(QPalette::ButtonText)
                                   : QColor("#596978");
        p->setPen(color);
        p->drawText(textRect, flags, text);
    }

    void drawCommandIcon(QPainter *p, const QRectF &r, const QString &kind)
    {
        const bool red = kind == QStringLiteral("estop");
        const QColor stroke = isEnabled()
            ? (red ? QColor("#ff6b6b") : QColor("#ffd21a"))
            : QColor("#596978");
        QColor glow(stroke);
        glow.setAlpha(isEnabled() ? 58 : 24);
        const QPointF c = r.center();
        const qreal s = qMin(r.width(), r.height()) / 48.0;

        p->save();
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(glow, 6.0 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawIconPath(p, c, s, kind);
        p->setPen(QPen(stroke, 2.2 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawIconPath(p, c, s, kind);
        p->restore();
    }

    void drawIconPath(QPainter *p, const QPointF &c, qreal s, const QString &kind)
    {
        if (kind == QStringLiteral("move")) {
            p->drawEllipse(c, 17 * s, 17 * s);
            p->drawEllipse(c, 7 * s, 7 * s);
            p->drawLine(QPointF(c.x() - 22 * s, c.y()), QPointF(c.x() - 11 * s, c.y()));
            p->drawLine(QPointF(c.x() + 11 * s, c.y()), QPointF(c.x() + 22 * s, c.y()));
            p->drawLine(QPointF(c.x(), c.y() - 22 * s), QPointF(c.x(), c.y() - 11 * s));
            p->drawLine(QPointF(c.x(), c.y() + 11 * s), QPointF(c.x(), c.y() + 22 * s));
            p->drawPoint(c);
        } else if (kind == QStringLiteral("manual")) {
            const QRectF pad(c.x() - 18 * s, c.y() - 10 * s, 36 * s, 22 * s);
            p->drawRoundedRect(pad, 8 * s, 8 * s);
            p->drawLine(QPointF(c.x() - 7 * s, c.y() - 16 * s), QPointF(c.x() - 7 * s, c.y() - 10 * s));
            p->drawLine(QPointF(c.x() + 7 * s, c.y() - 16 * s), QPointF(c.x() + 7 * s, c.y() - 10 * s));
            p->drawLine(QPointF(c.x() - 11 * s, c.y() + 1 * s), QPointF(c.x() - 3 * s, c.y() + 1 * s));
            p->drawLine(QPointF(c.x() - 7 * s, c.y() - 3 * s), QPointF(c.x() - 7 * s, c.y() + 5 * s));
            p->drawEllipse(QPointF(c.x() + 8 * s, c.y()), 2.8 * s, 2.8 * s);
            p->drawEllipse(QPointF(c.x() + 15 * s, c.y() - 4 * s), 2.2 * s, 2.2 * s);
        } else if (kind == QStringLiteral("auto")) {
            p->drawArc(QRectF(c.x() - 17 * s, c.y() - 17 * s, 34 * s, 34 * s), 30 * 16, 290 * 16);
            p->drawLine(QPointF(c.x() + 13 * s, c.y() - 10 * s), QPointF(c.x() + 18 * s, c.y() - 17 * s));
            p->drawLine(QPointF(c.x() + 13 * s, c.y() - 10 * s), QPointF(c.x() + 5 * s, c.y() - 12 * s));
            p->drawEllipse(c, 5 * s, 5 * s);
        } else if (kind == QStringLiteral("deploy")) {
            const QRectF body(c.x() - 12 * s, c.y() - 7 * s, 24 * s, 16 * s);
            p->drawRoundedRect(body, 5 * s, 5 * s);
            p->drawEllipse(QPointF(c.x() - 5 * s, c.y()), 1.8 * s, 1.8 * s);
            p->drawEllipse(QPointF(c.x() + 5 * s, c.y()), 1.8 * s, 1.8 * s);
            p->drawLine(QPointF(c.x(), c.y() - 7 * s), QPointF(c.x(), c.y() - 15 * s));
            p->drawEllipse(QPointF(c.x(), c.y() - 17 * s), 2.2 * s, 2.2 * s);
            p->drawLine(QPointF(c.x() - 8 * s, c.y() + 9 * s), QPointF(c.x() - 13 * s, c.y() + 16 * s));
            p->drawLine(QPointF(c.x() + 8 * s, c.y() + 9 * s), QPointF(c.x() + 13 * s, c.y() + 16 * s));
            p->drawLine(QPointF(c.x() + 17 * s, c.y()), QPointF(c.x() + 24 * s, c.y()));
            p->drawLine(QPointF(c.x() + 19 * s, c.y() - 5 * s), QPointF(c.x() + 24 * s, c.y()));
            p->drawLine(QPointF(c.x() + 19 * s, c.y() + 5 * s), QPointF(c.x() + 24 * s, c.y()));
        } else if (kind == QStringLiteral("estop")) {
            QPolygonF oct;
            for (int i = 0; i < 8; ++i) {
                const qreal a = (-22.5 + i * 45.0) * M_PI / 180.0;
                oct << QPointF(c.x() + std::cos(a) * 18 * s,
                               c.y() + std::sin(a) * 18 * s);
            }
            p->drawPolygon(oct);
            p->drawLine(QPointF(c.x(), c.y() - 9 * s), QPointF(c.x(), c.y() + 4 * s));
            p->drawPoint(QPointF(c.x(), c.y() + 10 * s));
        }
    }
};

class DashboardEmptyState : public QWidget
{
public:
    enum class Kind {
        Video,
        RobotStatus
    };

    explicit DashboardEmptyState(Kind kind, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_kind(kind)
    {
        setObjectName(kind == Kind::Video ? QStringLiteral("videoEmptyState")
                                          : QStringLiteral("robotEmptyState"));
        if (kind == Kind::RobotStatus) {
            setAttribute(Qt::WA_TranslucentBackground, true);
            setAutoFillBackground(false);
        }
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumHeight(kind == Kind::Video ? 250 : 170);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF outer = rect().adjusted(10, 10, -10, -10);
        if (!outer.isValid()) {
            return;
        }

        if (m_kind == Kind::Video) {
            drawBackdrop(&p, outer);
            drawVideoState(&p, outer);
        } else {
            drawRobotState(&p, outer);
        }
    }

private:
    void drawBackdrop(QPainter *p, const QRectF &r)
    {
        p->save();

        QPainterPath clip;
        clip.addRoundedRect(r, 8, 8);
        p->setClipPath(clip);

        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor(7, 18, 30, 225));
        bg.setColorAt(0.55, QColor(9, 29, 48, 205));
        bg.setColorAt(1.0, QColor(4, 12, 20, 230));
        p->fillPath(clip, bg);

        const QColor gridMajor(63, 132, 190, m_kind == Kind::Video ? 18 : 38);
        const QColor gridMinor(63, 132, 190, m_kind == Kind::Video ? 8 : 20);
        const qreal step = m_kind == Kind::Video ? 56.0 : 28.0;
        for (qreal x = r.left(); x <= r.right(); x += step) {
            const bool major = qRound((x - r.left()) / step) % 3 == 0;
            p->setPen(QPen(major ? gridMajor : gridMinor, major ? 1.0 : 0.7));
            p->drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        }
        for (qreal y = r.top(); y <= r.bottom(); y += step) {
            const bool major = qRound((y - r.top()) / step) % 3 == 0;
            p->setPen(QPen(major ? gridMajor : gridMinor, major ? 1.0 : 0.7));
            p->drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }

        QColor scan(255, 210, 26, m_kind == Kind::Video ? 7 : 14);
        p->setPen(QPen(scan, 1.0));
        for (qreal y = r.top() + 24.0; y < r.bottom(); y += (m_kind == Kind::Video ? 72.0 : 36.0)) {
            p->drawLine(QPointF(r.left() + 12.0, y), QPointF(r.right() - 12.0, y));
        }

        p->setClipping(false);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor(0, 216, 255, 78), 1.0));
        p->drawRoundedRect(r, 8, 8);

        const qreal corner = qMin<qreal>(34.0, qMin(r.width(), r.height()) * 0.13);
        p->setPen(QPen(QColor(156, 210, 255, 130), 1.2));
        p->drawLine(r.topLeft() + QPointF(10, 0), r.topLeft() + QPointF(corner, 0));
        p->drawLine(r.topLeft() + QPointF(0, 10), r.topLeft() + QPointF(0, corner));
        p->drawLine(r.topRight() + QPointF(-10, 0), r.topRight() + QPointF(-corner, 0));
        p->drawLine(r.topRight() + QPointF(0, 10), r.topRight() + QPointF(0, corner));
        p->drawLine(r.bottomLeft() + QPointF(10, 0), r.bottomLeft() + QPointF(corner, 0));
        p->drawLine(r.bottomLeft() + QPointF(0, -10), r.bottomLeft() + QPointF(0, -corner));
        p->drawLine(r.bottomRight() + QPointF(-10, 0), r.bottomRight() + QPointF(-corner, 0));
        p->drawLine(r.bottomRight() + QPointF(0, -10), r.bottomRight() + QPointF(0, -corner));

        p->restore();
    }

    void drawVideoState(QPainter *p, const QRectF &r)
    {
        p->save();

        const QPointF c = r.center() + QPointF(0, -38);
        const qreal iconSize = qMin<qreal>(54.0, qMin(r.width(), r.height()) * 0.18);
        const QRectF glowRect(c.x() - iconSize * 0.64, c.y() - iconSize * 0.48,
                              iconSize * 1.28, iconSize * 0.96);
        QRadialGradient glow(c, iconSize * 0.72);
        glow.setColorAt(0.0, QColor(255, 210, 26, 44));
        glow.setColorAt(0.68, QColor(0, 202, 255, 18));
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        p->setPen(Qt::NoPen);
        p->setBrush(glow);
        p->drawRoundedRect(glowRect, 18, 18);

        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor("#ffd21a"), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal s = iconSize / 64.0;
        const QRectF body(c.x() - 18 * s, c.y() - 11 * s, 30 * s, 22 * s);
        p->drawRoundedRect(body, 3.5 * s, 3.5 * s);
        p->drawLine(QPointF(c.x() - 10 * s, c.y() - 15 * s),
                    QPointF(c.x() + 3 * s, c.y() - 15 * s));
        p->drawLine(QPointF(c.x() - 4 * s, c.y() + 15 * s),
                    QPointF(c.x() + 14 * s, c.y() + 15 * s));
        p->drawEllipse(QPointF(c.x() - 4 * s, c.y()), 5.5 * s, 5.5 * s);
        QPainterPath lens;
        lens.moveTo(c.x() + 12 * s, c.y() - 5 * s);
        lens.lineTo(c.x() + 26 * s, c.y() - 12 * s);
        lens.lineTo(c.x() + 26 * s, c.y() + 12 * s);
        lens.lineTo(c.x() + 12 * s, c.y() + 5 * s);
        p->drawPath(lens);

        drawCenteredText(p, QRectF(r.left() + 24, c.y() + iconSize * 0.62,
                                   r.width() - 48, 34),
                         QStringLiteral("영상 스트리밍 대기"),
                         QColor("#f3f7fc"), 14, QFont::Black);
        drawCenteredText(p, QRectF(r.left() + 28, c.y() + iconSize * 0.62 + 35,
                                   r.width() - 56, 30),
                         QStringLiteral("로봇 투입 후 카메라 피드가 표시됩니다."),
                         QColor(173, 194, 214), 10, QFont::DemiBold);

        p->restore();
    }

    void drawRobotState(QPainter *p, const QRectF &r)
    {
        p->save();

        const qreal iconSide = qMin<qreal>(44.0, qMin(r.width(), r.height()) * 0.27);
        const QPointF c(r.center().x(), r.center().y() - 36.0);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(QColor("#ffd21a"), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal s = iconSide / 64.0;
        const QRectF body(c.x() - 17 * s, c.y() - 7 * s, 34 * s, 23 * s);
        p->drawRoundedRect(body, 6 * s, 6 * s);
        p->drawEllipse(QPointF(c.x() - 7 * s, c.y() + 3 * s), 2.0 * s, 2.0 * s);
        p->drawEllipse(QPointF(c.x() + 7 * s, c.y() + 3 * s), 2.0 * s, 2.0 * s);
        p->drawLine(QPointF(c.x(), c.y() - 7 * s), QPointF(c.x(), c.y() - 16 * s));
        p->drawEllipse(QPointF(c.x(), c.y() - 18 * s), 2.1 * s, 2.1 * s);
        p->drawLine(QPointF(c.x() - 11 * s, c.y() + 16 * s),
                    QPointF(c.x() - 17 * s, c.y() + 24 * s));
        p->drawLine(QPointF(c.x() + 11 * s, c.y() + 16 * s),
                    QPointF(c.x() + 17 * s, c.y() + 24 * s));
        p->drawArc(QRectF(c.x() - 16 * s, c.y() - 33 * s, 32 * s, 22 * s), 35 * 16, 110 * 16);
        p->drawArc(QRectF(c.x() - 24 * s, c.y() - 41 * s, 48 * s, 34 * s), 35 * 16, 110 * 16);

        drawCenteredText(p, QRectF(r.left() + 24, c.y() + iconSide * 0.68,
                                   r.width() - 48, 32),
                         QStringLiteral("로봇 상태 대기"),
                         QColor("#f3f7fc"), 14, QFont::Black);
        drawCenteredText(p, QRectF(r.left() + 28, c.y() + iconSide * 0.68 + 34,
                                   r.width() - 56, 30),
                         QStringLiteral("로봇 투입 후 상태 정보와 위치가 표시됩니다."),
                         QColor(173, 194, 214), 10, QFont::DemiBold);

        p->restore();
    }

    void drawCenteredText(QPainter *p, const QRectF &rect, const QString &text,
                          const QColor &color, int pointSize, QFont::Weight weight)
    {
        QFont f(QStringLiteral("Noto Sans"));
        f.setPointSize(pointSize);
        f.setWeight(weight);
        p->setFont(f);
        p->setPen(color);
        p->drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }

    void drawLeftText(QPainter *p, const QRectF &rect, const QString &text,
                      const QColor &color, int pointSize, QFont::Weight weight)
    {
        QFont f(QStringLiteral("Noto Sans"));
        f.setPointSize(pointSize);
        f.setWeight(weight);
        p->setFont(f);
        p->setPen(color);
        p->drawText(rect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
    }

    Kind m_kind;
};

class ButtonGlowFilter : public QObject
{
public:
    explicit ButtonGlowFilter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        QAbstractButton *button = qobject_cast<QAbstractButton *>(watched);
        if (!button) {
            return QObject::eventFilter(watched, event);
        }

        switch (event->type()) {
        case QEvent::Enter:
            animateGlow(button, true);
            break;
        case QEvent::Leave:
        case QEvent::Hide:
        case QEvent::EnabledChange:
            animateGlow(button, false);
            break;
        case QEvent::Destroy:
            m_effects.remove(button);
            m_animations.remove(button);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QGraphicsDropShadowEffect *glowEffectFor(QAbstractButton *button)
    {
        if (button->graphicsEffect()) {
            if (button->graphicsEffect()->property("codexHoverGlow").toBool()) {
                return qobject_cast<QGraphicsDropShadowEffect *>(button->graphicsEffect());
            }
            return nullptr;
        }

        auto *effect = new QGraphicsDropShadowEffect(button);
        effect->setProperty("codexHoverGlow", true);
        effect->setColor(QColor(255, 210, 26, 185));
        effect->setOffset(0, 0);
        effect->setBlurRadius(0.0);
        effect->setEnabled(false);
        button->setGraphicsEffect(effect);
        m_effects.insert(button, effect);
        return effect;
    }

    static qreal targetBlurRadius(const QAbstractButton *button)
    {
        return qBound<qreal>(12.0, qMin(button->width(), button->height()) * 0.36, 24.0);
    }

    void animateGlow(QAbstractButton *button, bool visible)
    {
        if (visible && !button->isEnabled()) {
            return;
        }

        QGraphicsDropShadowEffect *effect = glowEffectFor(button);
        if (!effect) {
            return;
        }

        if (QPointer<QPropertyAnimation> existing = m_animations.value(button)) {
            existing->stop();
            existing->deleteLater();
        }

        if (visible) {
            effect->setEnabled(true);
        }

        auto *animation = new QPropertyAnimation(effect, "blurRadius", this);
        animation->setDuration(visible ? 170 : 230);
        animation->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic);
        animation->setStartValue(effect->blurRadius());
        animation->setEndValue(visible ? targetBlurRadius(button) : 0.0);
        m_animations.insert(button, animation);

        QPointer<QAbstractButton> guardedButton(button);
        connect(animation, &QPropertyAnimation::finished, this, [this, guardedButton, effect, animation, visible]() {
            if (!visible && guardedButton && !guardedButton->underMouse()) {
                effect->setEnabled(false);
            }
            if (guardedButton && m_animations.value(guardedButton) == animation) {
                m_animations.remove(guardedButton);
            }
            animation->deleteLater();
        });
        animation->start();
    }

    QHash<QObject *, QPointer<QGraphicsDropShadowEffect>> m_effects;
    QHash<QObject *, QPointer<QPropertyAnimation>> m_animations;
};

class BorderlessComboBox : public QComboBox
{
public:
    explicit BorderlessComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
    }

    void showPopup() override
    {
        ensurePopup();
        rebuildPopupItems();

        const int popupWidth = qMax(width(), 1);
        const int visibleRows = qMin(count(), qMax(1, maxVisibleItems()));
        m_popupList->setVerticalScrollBarPolicy(count() > visibleRows
                                                    ? Qt::ScrollBarAsNeeded
                                                    : Qt::ScrollBarAlwaysOff);
        m_popup->setFixedSize(popupWidth, visibleRows * kPopupRowHeight);

        const QPoint comboTopLeft = mapToGlobal(QPoint(0, 0));
        QPoint popupPos(comboTopLeft.x(), comboTopLeft.y() + height() / 2 - m_popup->height() / 2);
        constexpr int topMargin = 12;
        constexpr int bottomMargin = 44;
        if (QScreen *screen = QGuiApplication::screenAt(popupPos)) {
            const QRect available = screen->availableGeometry();
            const int minTop = available.top() + topMargin;
            const int maxTop = available.bottom() - bottomMargin - m_popup->height();
            popupPos.setY(qBound(minTop, popupPos.y(), qMax(minTop, maxTop)));
        }
        m_popup->move(popupPos);
        m_popup->show();
        m_popup->raise();
        m_popupList->setFocus(Qt::PopupFocusReason);
    }

    void hidePopup() override
    {
        if (m_popup) {
            m_popup->hide();
        }
        QComboBox::hidePopup();
    }

private:
    static constexpr int kPopupRowHeight = 46;

    void ensurePopup()
    {
        if (m_popup) {
            return;
        }

        m_popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        m_popup->setObjectName("comboPopup");
        m_popup->setFrameShape(QFrame::NoFrame);
        m_popup->setFrameStyle(QFrame::NoFrame);
        m_popup->setLineWidth(0);
        m_popup->setMidLineWidth(0);
        m_popup->setAttribute(Qt::WA_StyledBackground, true);
        m_popup->setAutoFillBackground(true);
        m_popup->setStyleSheet(
            "QFrame#comboPopup { background:#071017; border:0; margin:0; padding:0; }"
            "QListWidget { background:#071017; border:0; margin:0; padding:0; outline:0; }"
            "QListWidget::viewport { background:#071017; border:0; margin:0; padding:0; }"
            "QListWidget::item { background:#071017; color:#dce7f3; min-height:34px; padding:6px 10px; margin:0; border:0; }"
            "QListWidget::item:selected { background:#1b1805; color:#ffd21a; }"
            "QListWidget::item:focus { outline:0; }");

        QVBoxLayout *popupLayout = new QVBoxLayout(m_popup);
        popupLayout->setContentsMargins(0, 0, 0, 0);
        popupLayout->setSpacing(0);

        m_popupList = new QListWidget(m_popup);
        m_popupList->setFrameShape(QFrame::NoFrame);
        m_popupList->setFrameStyle(QFrame::NoFrame);
        m_popupList->setLineWidth(0);
        m_popupList->setMidLineWidth(0);
        m_popupList->setContentsMargins(0, 0, 0, 0);
        m_popupList->setSpacing(0);
        m_popupList->setUniformItemSizes(true);
        m_popupList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_popupList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_popupList->setAttribute(Qt::WA_StyledBackground, true);
        m_popupList->viewport()->setAttribute(Qt::WA_StyledBackground, true);
        popupLayout->addWidget(m_popupList);

        connect(m_popupList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (!item) {
                return;
            }
            const int comboIndex = item->data(Qt::UserRole).toInt();
            if (comboIndex >= 0 && comboIndex < count()) {
                setCurrentIndex(comboIndex);
            }
            hidePopup();
        });
    }

    void rebuildPopupItems()
    {
        m_popupList->clear();
        for (int i = 0; i < count(); ++i) {
            auto *item = new QListWidgetItem(itemText(i));
            item->setData(Qt::UserRole, i);
            item->setSizeHint(QSize(qMax(width(), 1), kPopupRowHeight));
            m_popupList->addItem(item);
            if (i == currentIndex()) {
                m_popupList->setCurrentItem(item);
            }
        }
    }

    QFrame *m_popup = nullptr;
    QListWidget *m_popupList = nullptr;
};

class ManualJoystickWidget : public QWidget
{
public:
    explicit ManualJoystickWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(230, 230);
        setCursor(Qt::PointingHandCursor);
        m_sendTimer.setInterval(100);
        connect(&m_sendTimer, &QTimer::timeout, this, [this]() {
            emitVelocity(false);
        });
    }

    void setVelocityCallback(std::function<void(float, float, float, bool)> callback)
    {
        m_velocityCallback = std::move(callback);
    }

    void resetStick()
    {
        m_knob = QPointF(0.0, 0.0);
        m_dragging = false;
        m_sendTimer.stop();
        update();
        emitVelocity(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect().adjusted(8.0, 8.0, -8.0, -8.0);
        const QPointF c = bounds.center();
        const qreal radius = qMin(bounds.width(), bounds.height()) * 0.39;

        QRadialGradient bg(c, radius * 1.28);
        bg.setColorAt(0.0, QColor(24, 31, 37));
        bg.setColorAt(0.72, QColor(10, 15, 21));
        bg.setColorAt(1.0, QColor(5, 9, 13, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawEllipse(c, radius * 1.34, radius * 1.34);

        p.setPen(QPen(QColor(255, 210, 26, 220), 2.1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, radius, radius);

        QRadialGradient well(c, radius * 0.72);
        well.setColorAt(0.0, QColor(52, 58, 64));
        well.setColorAt(0.52, QColor(18, 21, 25));
        well.setColorAt(1.0, QColor(3, 6, 9));
        p.setPen(QPen(QColor(68, 78, 86), 1.4));
        p.setBrush(well);
        p.drawEllipse(c, radius * 0.70, radius * 0.70);

        const QPointF knobCenter = c + QPointF(m_knob.x() * radius * 0.46,
                                               m_knob.y() * radius * 0.46);
        QRadialGradient knob(knobCenter - QPointF(radius * 0.15, radius * 0.18),
                             radius * 0.40);
        knob.setColorAt(0.0, QColor(115, 119, 123));
        knob.setColorAt(0.48, QColor(60, 63, 67));
        knob.setColorAt(1.0, QColor(18, 20, 23));
        p.setPen(QPen(QColor(16, 18, 21), 2.0));
        p.setBrush(knob);
        p.drawEllipse(knobCenter, radius * 0.34, radius * 0.34);

        p.setPen(QPen(QColor(255, 210, 26, 225), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const qreal arrow = radius * 0.12;
        p.drawPolyline(QPolygonF{QPointF(c.x(), c.y() - radius - arrow * 2.6),
                                 QPointF(c.x() - arrow, c.y() - radius - arrow * 1.7),
                                 QPointF(c.x(), c.y() - radius - arrow * 2.6),
                                 QPointF(c.x() + arrow, c.y() - radius - arrow * 1.7)});
        p.drawPolyline(QPolygonF{QPointF(c.x() - radius - arrow * 2.6, c.y()),
                                 QPointF(c.x() - radius - arrow * 1.7, c.y() - arrow),
                                 QPointF(c.x() - radius - arrow * 2.6, c.y()),
                                 QPointF(c.x() - radius - arrow * 1.7, c.y() + arrow)});
        p.drawPolyline(QPolygonF{QPointF(c.x() + radius + arrow * 2.6, c.y()),
                                 QPointF(c.x() + radius + arrow * 1.7, c.y() - arrow),
                                 QPointF(c.x() + radius + arrow * 2.6, c.y()),
                                 QPointF(c.x() + radius + arrow * 1.7, c.y() + arrow)});
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragging = true;
        updateStick(event->position());
        emitVelocity(true);
        m_sendTimer.start();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging) {
            return;
        }
        updateStick(event->position());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        resetStick();
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (m_dragging && !(QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            resetStick();
        }
    }

private:
    void updateStick(const QPointF &pos)
    {
        const QRectF bounds = rect().adjusted(8.0, 8.0, -8.0, -8.0);
        const QPointF c = bounds.center();
        const qreal radius = qMin(bounds.width(), bounds.height()) * 0.39;
        QPointF delta((pos.x() - c.x()) / qMax(radius, 1.0),
                      (pos.y() - c.y()) / qMax(radius, 1.0));
        const qreal len = std::hypot(delta.x(), delta.y());
        if (len > 1.0) {
            delta /= len;
        }
        m_knob = delta;
        update();
    }

    void emitVelocity(bool force)
    {
        if (!m_velocityCallback) {
            return;
        }
        const float x = static_cast<float>(m_knob.x());
        const float y = static_cast<float>(m_knob.y());
        const float forward = -y;
        const float vx = forward >= 0.0f
            ? forward * kManualMaxForwardMps
            : forward * kManualMaxReverseMps;
        const float omega = -x * kManualMaxYawRadps;
        m_velocityCallback(vx, 0.0f, omega, force);
    }

    QPointF m_knob {0.0, 0.0};
    bool m_dragging = false;
    QTimer m_sendTimer;
    std::function<void(float, float, float, bool)> m_velocityCallback;
};

static QString findMapYaml()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    const QStringList candidates = {
        QDir(appDir).absoluteFilePath("../map_data/map_final.yaml"),
        QDir(appDir).absoluteFilePath("../../map_data/map_final.yaml"),
        QDir(appDir).absoluteFilePath("../../../map_data/map_final.yaml"),
        QDir(cwd).absoluteFilePath("map_data/map_final.yaml"),
        QDir(cwd).absoluteFilePath("../map_data/map_final.yaml"),
        QStringLiteral("/home/pi/robot_project/map_data/map_final.yaml"),
        QDir(appDir).absoluteFilePath("../map_data/new_map.yaml"),
        QDir(appDir).absoluteFilePath("../../map_data/new_map.yaml"),
        QDir(appDir).absoluteFilePath("../../../map_data/new_map.yaml"),
        QDir(cwd).absoluteFilePath("map_data/new_map.yaml"),
        QDir(cwd).absoluteFilePath("../map_data/new_map.yaml"),
        QStringLiteral("/home/pi/robot_project/map_data/new_map.yaml"),
        QDir(appDir).absoluteFilePath("../map_data/map.yaml"),
        QDir(appDir).absoluteFilePath("../../map_data/map.yaml"),
        QDir(appDir).absoluteFilePath("../../../map_data/map.yaml"),
        QDir(cwd).absoluteFilePath("map_data/map.yaml"),
        QDir(cwd).absoluteFilePath("../map_data/map.yaml"),
        QStringLiteral("/home/pi/robot_project/map_data/map.yaml")
    };

    for (const QString &path : candidates) {
        QFileInfo info(path);
        if (info.exists() && info.isFile()) {
            return info.canonicalFilePath();
        }
    }
    return QString();
}

static QString findDroneCaptureImagePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    const QStringList fileNames = {
        QStringLiteral("폐허진.png"),
        QStringLiteral("폐허.png"),
        QStringLiteral("map_final_scene.png"),
        QStringLiteral("drone_capture.png")
    };

    for (const QString &fileName : fileNames) {
        const QStringList candidates = {
            QDir(appDir).absoluteFilePath("../image_data/" + fileName),
            QDir(appDir).absoluteFilePath("../../image_data/" + fileName),
            QDir(appDir).absoluteFilePath("../../../image_data/" + fileName),
            QDir(cwd).absoluteFilePath("image_data/" + fileName),
            QDir(cwd).absoluteFilePath("../image_data/" + fileName),
            QDir(cwd).absoluteFilePath("../../image_data/" + fileName),
            QStringLiteral("/home/pi/robot_project/image_data/") + fileName
        };

        for (const QString &path : candidates) {
            QFileInfo info(path);
            if (info.exists() && info.isFile()) {
                return info.canonicalFilePath();
            }
        }
    }
    return QString();
}

static QString findDroneCameraLoopPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    const QString fileName = QStringLiteral("폐허드론_loop.gif");
    const QStringList candidates = {
        QDir(appDir).absoluteFilePath("../image_data/" + fileName),
        QDir(appDir).absoluteFilePath("../../image_data/" + fileName),
        QDir(appDir).absoluteFilePath("../../../image_data/" + fileName),
        QDir(cwd).absoluteFilePath("image_data/" + fileName),
        QDir(cwd).absoluteFilePath("../image_data/" + fileName),
        QDir(cwd).absoluteFilePath("../../image_data/" + fileName),
        QStringLiteral("/home/pi/robot_project/image_data/") + fileName
    };

    for (const QString &path : candidates) {
        QFileInfo info(path);
        if (info.exists() && info.isFile()) {
            return info.canonicalFilePath();
        }
    }
    return QString();
}

static QString findSpotStartFramePath(int displayRobotId)
{
    const int physicalRobotId = displayToPhysicalRobotId(displayRobotId);
    if (physicalRobotId < 0) {
        return QString();
    }

    const QString fileName = QString("spot_%1_start.png")
                                 .arg(physicalRobotId + 1, 2, 10, QLatin1Char('0'));
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    const QStringList candidates = {
        QDir(appDir).absoluteFilePath("../image_data/" + fileName),
        QDir(appDir).absoluteFilePath("../../image_data/" + fileName),
        QDir(appDir).absoluteFilePath("../../../image_data/" + fileName),
        QDir(cwd).absoluteFilePath("image_data/" + fileName),
        QDir(cwd).absoluteFilePath("../image_data/" + fileName),
        QDir(cwd).absoluteFilePath("../../image_data/" + fileName),
        QStringLiteral("/home/pi/robot_project/image_data/") + fileName
    };

    for (const QString &path : candidates) {
        QFileInfo info(path);
        if (info.exists() && info.isFile()) {
            return info.canonicalFilePath();
        }
    }
    return QString();
}

static QImage spotStartFrameForDisplayRobot(int displayRobotId)
{
    if (displayRobotId < 0 || displayRobotId >= kMaxRobots) {
        return QImage();
    }

    static QVector<QImage> cache(kMaxRobots);
    static QVector<bool> attempted(kMaxRobots, false);
    if (!attempted[displayRobotId]) {
        attempted[displayRobotId] = true;
        const QString path = findSpotStartFramePath(displayRobotId);
        if (!path.isEmpty()) {
            cache[displayRobotId].load(path);
        }
    }
    return cache[displayRobotId];
}

static QString logSeverityText(const QString &code)
{
    if (code == "CRITICAL") {
        return "위험";
    }
    if (code == "WARN") {
        return "주의";
    }
    return "정보";
}

static QString commandTypeText(uint8_t commandType)
{
    switch (commandType) {
    case CMD_TYPE_ESTOP:
        return "긴급 정지";
    case CMD_TYPE_STOP:
        return "정지";
    case CMD_TYPE_MOVE:
        return "이동";
    case CMD_TYPE_START_MISSION:
        return "탐색 시작";
    case CMD_TYPE_PAUSE_MISSION:
        return "일시 정지";
    case CMD_TYPE_CANCEL_MISSION:
        return "임무 취소";
    case CMD_TYPE_RETURN_HOME:
        return "복귀";
    case CMD_TYPE_SET_WAYPOINT:
        return "경로 시작점";
    case CMD_TYPE_SET_ROUTE:
        return "경로 생성";
    case CMD_TYPE_MANUAL_MOVE:
        return "수동 조작";
    case CMD_TYPE_SET_AUTO:
        return "자동 제어";
    case CMD_TYPE_BODY_ACTION:
        return "자세 동작";
    default:
        return QString("알 수 없는 명령(%1)").arg(commandType);
    }
}

static bool isBridgeCommandSentEvent(const UiEvent &event)
{
    return event.message.startsWith(QStringLiteral("cmd sent:"), Qt::CaseInsensitive);
}

static bool isReassemblyTimeoutEvent(const UiEvent &event)
{
    return event.message.contains(QStringLiteral("reassembly timeout"), Qt::CaseInsensitive)
        || event.message.contains(QStringLiteral("reasm timeout"), Qt::CaseInsensitive);
}

static bool isCommandAckEvent(const UiEvent &event)
{
    return event.type == kEventTypeCmdAck
        || event.message.compare(QStringLiteral("cmd ack"), Qt::CaseInsensitive) == 0;
}

class LogTrendWidget : public QWidget
{
public:
    explicit LogTrendWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(80, 38);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QVector<float> values = {0.25f, 0.32f, 0.28f, 0.52f, 0.44f, 0.82f, 0.55f, 0.90f};
        QPolygonF line;
        const QRectF r = rect().adjusted(4, 5, -4, -5);
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = r.left() + r.width() * i / qMax(values.size() - 1, 1);
            const qreal y = r.bottom() - r.height() * values[i];
            line << QPointF(x, y);
        }
        p.setPen(QPen(QColor("#287dff"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(line);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 125, 255, 70));
        for (const QPointF &pt : line) {
            p.drawEllipse(pt, 2.6, 2.6);
        }
    }
};

class SeverityDonutWidget : public QWidget
{
public:
    explicit SeverityDonutWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(72, 72);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(8, 8, -8, -8);
        const int infoCount = property("infoCount").toInt();
        const int warnCount = property("warnCount").toInt();
        const int criticalCount = property("criticalCount").toInt();
        const int total = infoCount + warnCount + criticalCount;
        if (total <= 0) {
            p.setPen(QPen(QColor("#26384b"), 12, Qt::SolidLine, Qt::FlatCap));
            p.drawArc(r, 0, 360 * 16);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#071321"));
            p.drawEllipse(r.adjusted(14, 14, -14, -14));
            return;
        }
        const QVector<QPair<QColor, int>> slices = {
            {QColor("#68df78"), infoCount},
            {QColor("#f6bd32"), warnCount},
            {QColor("#ff5b57"), criticalCount},
        };
        int start = 90 * 16;
        for (const auto &slice : slices) {
            if (slice.second <= 0) {
                continue;
            }
            const int span = -qRound(360.0 * slice.second / static_cast<double>(total) * 16.0);
            p.setPen(QPen(slice.first, 12, Qt::SolidLine, Qt::FlatCap));
            p.drawArc(r, start, span);
            start += span;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#071321"));
        p.drawEllipse(r.adjusted(14, 14, -14, -14));
    }
};

class DroneCameraWidget : public QWidget
{
public:
    explicit DroneCameraWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(420, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        loadLoopMovie();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(4, 4, -4, -4);
        QLinearGradient sky(r.topLeft(), r.bottomRight());
        sky.setColorAt(0.0, QColor("#28323a"));
        sky.setColorAt(0.55, QColor("#151b20"));
        sky.setColorAt(1.0, QColor("#0a0f14"));
        p.setPen(QPen(QColor("#1e3445"), 1.0));
        p.setBrush(sky);
        p.drawRoundedRect(r, 7, 7);

        p.setClipPath([r]() {
            QPainterPath path;
            path.addRoundedRect(r, 7, 7);
            return path;
        }());

        if (m_movie && m_movie->isValid() && !m_movie->currentImage().isNull()) {
            drawMovieFrame(p, r);
            drawCameraHud(p, r);
            return;
        }

        p.setPen(QPen(QColor(255, 255, 255, 24), 1));
        for (int x = -120; x < r.width() + 160; x += 54) {
            p.drawLine(QPointF(r.left() + x, r.top()),
                       QPointF(r.left() + x + 165, r.bottom()));
        }
        auto drawBuilding = [&p](const QRectF &b, const QColor &color) {
            p.setPen(QPen(QColor(255, 255, 255, 24), 1));
            p.setBrush(color);
            p.drawPolygon(QPolygonF{b.topLeft(), b.topRight() + QPointF(16, 12),
                                    b.bottomRight(), b.bottomLeft() - QPointF(16, 12)});
            p.setBrush(QColor(color.red() + 10, color.green() + 10, color.blue() + 10, 190));
            p.drawPolygon(QPolygonF{b.topLeft(), b.topRight() + QPointF(16, 12),
                                    b.topRight() + QPointF(16, 32), b.topLeft() + QPointF(0, 20)});
        };

        drawBuilding(QRectF(r.left() + r.width() * 0.09, r.top() + r.height() * 0.08,
                            r.width() * 0.22, r.height() * 0.24), QColor(42, 50, 56, 210));
        drawBuilding(QRectF(r.left() + r.width() * 0.61, r.top() + r.height() * 0.05,
                            r.width() * 0.30, r.height() * 0.30), QColor(72, 76, 78, 220));
        drawBuilding(QRectF(r.left() + r.width() * 0.72, r.top() + r.height() * 0.50,
                            r.width() * 0.24, r.height() * 0.34), QColor(50, 57, 61, 220));

        QPainterPath road;
        road.moveTo(r.left() + r.width() * 0.22, r.bottom());
        road.cubicTo(r.left() + r.width() * 0.30, r.top() + r.height() * 0.62,
                     r.left() + r.width() * 0.45, r.top() + r.height() * 0.52,
                     r.left() + r.width() * 0.55, r.top());
        road.lineTo(r.left() + r.width() * 0.72, r.top());
        road.cubicTo(r.left() + r.width() * 0.62, r.top() + r.height() * 0.46,
                     r.left() + r.width() * 0.52, r.top() + r.height() * 0.68,
                     r.left() + r.width() * 0.46, r.bottom());
        road.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(85, 87, 83, 155));
        p.drawPath(road);

        p.setPen(QPen(QColor(200, 205, 210, 180), 2.0));
        const QPointF cc = r.center();
        p.drawLine(QPointF(cc.x() - 16, cc.y()), QPointF(cc.x() + 16, cc.y()));
        p.drawLine(QPointF(cc.x(), cc.y() - 16), QPointF(cc.x(), cc.y() + 16));

        drawCameraHud(p, r);
    }

private:
    void loadLoopMovie()
    {
        const QString path = findDroneCameraLoopPath();
        if (path.isEmpty()) {
            qWarning() << "DroneCameraWidget: drone loop asset not found";
            return;
        }

        m_movie = new QMovie(path, QByteArray(), this);
        m_movie->setCacheMode(QMovie::CacheNone);
        if (!m_movie->isValid()) {
            qWarning() << "DroneCameraWidget: failed to open drone loop asset" << path;
            return;
        }

        connect(m_movie, &QMovie::frameChanged, this, [this]() {
            update();
        });
        connect(m_movie, &QMovie::finished, this, [this]() {
            if (m_movie) {
                m_movie->start();
            }
        });
        m_movie->start();
    }

    void drawMovieFrame(QPainter &p, const QRectF &r)
    {
        const QImage frame = m_movie->currentImage();
        if (frame.isNull()) {
            return;
        }

        const qreal sourceAspect = static_cast<qreal>(frame.width()) / qMax(1, frame.height());
        const qreal targetAspect = r.width() / qMax<qreal>(1.0, r.height());
        QRectF source(QPointF(0.0, 0.0), QSizeF(frame.size()));
        if (sourceAspect > targetAspect) {
            const qreal cropWidth = frame.height() * targetAspect;
            source.setLeft((frame.width() - cropWidth) * 0.5);
            source.setWidth(cropWidth);
        } else if (sourceAspect < targetAspect) {
            const qreal cropHeight = frame.width() / targetAspect;
            source.setTop((frame.height() - cropHeight) * 0.5);
            source.setHeight(cropHeight);
        }

        p.drawImage(r, frame, source);

        QLinearGradient shade(r.topLeft(), r.bottomLeft());
        shade.setColorAt(0.0, QColor(0, 0, 0, 34));
        shade.setColorAt(0.42, QColor(0, 0, 0, 0));
        shade.setColorAt(1.0, QColor(0, 0, 0, 30));
        p.setPen(Qt::NoPen);
        p.setBrush(shade);
        p.drawRect(r);
    }

    void drawCameraHud(QPainter &p, const QRectF &r)
    {
        const qreal corner = 32.0;
        p.setPen(QPen(QColor("#ffffff"), 2.2, Qt::SolidLine, Qt::SquareCap));
        p.drawLine(r.topLeft() + QPointF(18, 18), r.topLeft() + QPointF(18 + corner, 18));
        p.drawLine(r.topLeft() + QPointF(18, 18), r.topLeft() + QPointF(18, 18 + corner));
        p.drawLine(r.topRight() + QPointF(-18, 18), r.topRight() + QPointF(-18 - corner, 18));
        p.drawLine(r.topRight() + QPointF(-18, 18), r.topRight() + QPointF(-18, 18 + corner));
        p.drawLine(r.bottomLeft() + QPointF(18, -18), r.bottomLeft() + QPointF(18 + corner, -18));
        p.drawLine(r.bottomLeft() + QPointF(18, -18), r.bottomLeft() + QPointF(18, -18 - corner));
        p.drawLine(r.bottomRight() + QPointF(-18, -18), r.bottomRight() + QPointF(-18 - corner, -18));
        p.drawLine(r.bottomRight() + QPointF(-18, -18), r.bottomRight() + QPointF(-18, -18 - corner));

        QRectF badge(r.left() + 20, r.top() + 18, 118, 62);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(2, 7, 12, 195));
        p.drawRoundedRect(badge, 6, 6);
        p.setPen(QColor("#ffffff"));
        QFont idFont = p.font();
        idFont.setPointSize(13);
        idFont.setBold(true);
        p.setFont(idFont);
        p.drawText(badge.adjusted(12, 8, -8, -30), Qt::AlignLeft | Qt::AlignVCenter, "DRONE-01");
        p.setPen(QColor("#24f36a"));
        p.drawEllipse(QPointF(badge.left() + 18, badge.bottom() - 17), 4, 4);
        p.drawText(badge.adjusted(28, 31, -8, -8), Qt::AlignLeft | Qt::AlignVCenter, "LIVE");
    }

    QMovie *m_movie = nullptr;
};

class DroneMapWidget : public QWidget
{
public:
    enum Tool {
        OriginTool = 0,
        XAxisTool,
        ObstacleTool
    };

    explicit DroneMapWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(360, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
    }

    void setTool(Tool tool)
    {
        m_tool = tool;
        m_autoYamlPreview = false;
        m_previewMode = false;
        update();
        notifyChanged();
    }

    Tool tool() const
    {
        return m_tool;
    }

    void setChangedCallback(std::function<void()> callback)
    {
        m_changed = std::move(callback);
    }

    int obstacleCount() const
    {
        if (m_autoYamlPreview) {
            return m_autoObstacles.size();
        }
        return m_obstacles.size();
    }

    bool axesReady() const
    {
        return m_hasOrigin && m_hasXAxis;
    }

    bool previewMode() const
    {
        return m_previewMode;
    }

    bool hasCapturedImage() const
    {
        return m_hasCapturedImage;
    }

    void loadCapturedImage()
    {
        m_hasCapturedImage = true;
        ensureCapturedImageLoaded();
        m_hasOrigin = false;
        m_hasXAxis = false;
        m_obstacles.clear();
        clearAutoYamlPreview();
        m_dragging = false;
        m_previewMode = false;
        m_tool = OriginTool;
        update();
        notifyChanged();
    }

    QString statusText() const
    {
        if (!m_hasCapturedImage) {
            return QStringLiteral("이미지 캡쳐 대기");
        }
        if (!m_hasOrigin) {
            return QStringLiteral("원점 지정 대기");
        }
        if (!m_hasXAxis) {
            return QStringLiteral("+X 방향 지정 대기");
        }
        if (m_previewMode) {
            return QStringLiteral("디지털 맵 미리보기");
        }
        return m_tool == ObstacleTool ? QStringLiteral("장애물 그리기")
                                      : QStringLiteral("좌표축 보정");
    }

    QString detailText() const
    {
        if (!m_hasCapturedImage) {
            return QStringLiteral("드론 카메라에서 이미지 캡쳐 후 맵 빌더를 시작합니다.");
        }
        if (m_autoYamlPreview && m_autoMapBounds.isValid()) {
            return QString("YAML 맵 자동 생성  |  장애물 %1개  |  영역 %2 x %3 m")
                .arg(m_autoObstacles.size())
                .arg(m_autoMapBounds.width(), 0, 'f', 1)
                .arg(m_autoMapBounds.height(), 0, 'f', 1);
        }
        const QRectF field = fieldBoundsMeters();
        return QString("고도 12.0 m  |  FOV 84°  |  축척 %1 m/px  |  장애물 %2개  |  영역 %3 x %4 m")
            .arg(metersPerPixel(), 0, 'f', 4)
            .arg(m_obstacles.size())
            .arg(field.width(), 0, 'f', 1)
            .arg(field.height(), 0, 'f', 1);
    }

    void undo()
    {
        if (!m_hasCapturedImage) {
            return;
        }
        if (m_dragging) {
            m_dragging = false;
        } else if (!m_obstacles.isEmpty()) {
            m_obstacles.removeLast();
        } else if (m_hasXAxis) {
            m_hasXAxis = false;
            m_tool = XAxisTool;
        } else if (m_hasOrigin) {
            m_hasOrigin = false;
            m_tool = OriginTool;
        }
        m_previewMode = false;
        update();
        notifyChanged();
    }

    void clearAll()
    {
        if (!m_hasCapturedImage) {
            return;
        }
        m_hasOrigin = false;
        m_hasXAxis = false;
        m_obstacles.clear();
        clearAutoYamlPreview();
        m_dragging = false;
        m_previewMode = false;
        m_tool = OriginTool;
        update();
        notifyChanged();
    }

    void generatePreview()
    {
        if (!m_hasCapturedImage || !axesReady()) {
            return;
        }
        m_autoYamlPreview = false;
        m_previewMode = true;
        update();
        notifyChanged();
    }

    bool generateAutoPreviewFromYaml(const QString &path)
    {
        MapConfig config;
        QString error;
        if (path.isEmpty() || !config.loadFromYaml(path, &error)) {
            qWarning() << "DroneMapWidget:" << (error.isEmpty()
                                                ? QStringLiteral("map yaml path is empty")
                                                : error);
            return false;
        }

        QVector<QRectF> areas;
        areas.reserve(config.areas().size());
        for (const MapRect &area : config.areas()) {
            areas.append(QRectF(QPointF(area.xMin, area.yMin),
                                QPointF(area.xMax, area.yMax)).normalized());
        }

        QVector<QRectF> obstacles;
        for (const MapRect &obstacle : config.obstacles()) {
            obstacles.append(QRectF(QPointF(obstacle.xMin, obstacle.yMin),
                                    QPointF(obstacle.xMax, obstacle.yMax)).normalized());
        }

        m_hasCapturedImage = true;
        ensureCapturedImageLoaded();
        m_hasOrigin = true;
        m_hasXAxis = true;
        m_dragging = false;
        m_previewMode = true;
        m_autoYamlPreview = true;
        m_hasCursor = false;
        m_tool = ObstacleTool;
        m_originNorm = QPointF(0.18, 0.78);
        m_xAxisNorm = QPointF(0.44, 0.78);
        m_obstacles.clear();
        m_autoAreas = areas;
        m_autoObstacles = obstacles;
        m_autoStarts.clear();
        m_autoStarts.reserve(config.starts().size());
        for (const MapPoint &start : config.starts()) {
            m_autoStarts.append(QPointF(start.x, start.y));
        }
        m_hasAutoEnd = config.hasEndPoint();
        const MapPoint end = config.endPoint();
        m_autoEnd = QPointF(end.x, end.y);
        m_autoMapBounds = QRectF(QPointF(config.minX(), config.minY()),
                                 QPointF(config.maxX(), config.maxY())).normalized();
        update();
        notifyChanged();
        return true;
    }

    void generateAutoPreview()
    {
        m_hasCapturedImage = true;
        ensureCapturedImageLoaded();
        m_hasOrigin = true;
        m_hasXAxis = true;
        m_dragging = false;
        m_previewMode = true;
        clearAutoYamlPreview();
        m_hasCursor = false;
        m_tool = ObstacleTool;
        m_originNorm = QPointF(0.18, 0.78);
        m_xAxisNorm = QPointF(0.44, 0.78);
        m_obstacles = {
            QRectF(2.0, 1.3, 1.9, 1.1),
            QRectF(6.4, -1.9, 2.4, 1.5),
            QRectF(10.3, 2.1, 1.7, 2.2)
        };
        update();
        notifyChanged();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton) {
            if (!m_obstacles.isEmpty()) {
                m_obstacles.removeLast();
                m_autoYamlPreview = false;
                m_previewMode = false;
                update();
                notifyChanged();
            }
            return;
        }

        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (!m_hasCapturedImage) {
            return;
        }
        const QPointF norm = widgetToNorm(event->pos());
        if (!isNormInside(norm)) {
            return;
        }

        if (m_tool == OriginTool) {
            clearAutoYamlPreview();
            m_originNorm = norm;
            m_hasOrigin = true;
            m_hasXAxis = false;
            m_obstacles.clear();
            m_tool = XAxisTool;
            m_previewMode = false;
            update();
            notifyChanged();
            return;
        }

        if (m_tool == XAxisTool && m_hasOrigin) {
            if (QLineF(norm, m_originNorm).length() < 0.04) {
                return;
            }
            clearAutoYamlPreview();
            m_xAxisNorm = norm;
            m_hasXAxis = true;
            m_tool = ObstacleTool;
            m_previewMode = false;
            update();
            notifyChanged();
            return;
        }

        if (m_tool == ObstacleTool && axesReady()) {
            m_autoYamlPreview = false;
            m_dragStartMeters = normToMeters(norm);
            m_dragCurrentMeters = m_dragStartMeters;
            m_dragging = true;
            m_previewMode = false;
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPointF norm = widgetToNorm(event->pos());
        if (m_hasCapturedImage && isNormInside(norm)) {
            m_cursorNorm = norm;
            m_hasCursor = true;
            if (m_dragging) {
                m_dragCurrentMeters = normToMeters(norm);
            }
        } else {
            m_hasCursor = false;
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_hasCapturedImage || !m_dragging || !axesReady()) {
            return;
        }
        const QPointF norm = widgetToNorm(event->pos());
        if (isNormInside(norm)) {
            m_dragCurrentMeters = normToMeters(norm);
            QRectF rect = normalizedMeterRect(m_dragStartMeters, m_dragCurrentMeters);
            if (rect.width() >= 0.08 && rect.height() >= 0.08) {
                m_obstacles.append(rect);
            }
        }
        m_dragging = false;
        update();
        notifyChanged();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(4, 4, -4, -4);
        p.setPen(Qt::NoPen);
        p.setBrush(m_previewMode ? QColor("#0b151d") : QColor("#07131a"));
        p.drawRoundedRect(r, 6, 6);

        p.setClipPath([r]() {
            QPainterPath path;
            path.addRoundedRect(r, 6, 6);
            return path;
        }());

        if (!m_hasCapturedImage) {
            drawCapturePlaceholder(p, r);
            drawHud(p, r);
            return;
        }

        drawDroneImage(p);
        if (m_autoYamlPreview) {
            drawAutoYamlMap(p);
            drawAutoYamlOriginFrame(p);
            drawHud(p, r);
            return;
        }
        if (m_hasOrigin) {
            drawCoordinateFrame(p);
        }
        drawObstacles(p);
        drawDragPreview(p);
        drawHud(p, r);
    }

private:
    static constexpr qreal kAltitudeM = 12.0;
    static constexpr qreal kFovDeg = 84.0;
    static constexpr qreal kImageWidthPx = 1280.0;
    static constexpr qreal kImageHeightPx = 720.0;

    QRectF imageRect() const
    {
        QRectF area = rect().adjusted(14, 14, -14, -14);
        const qreal target = capturedImageAspectRatio();
        qreal w = area.width();
        qreal h = w / target;
        if (h > area.height()) {
            h = area.height();
            w = h * target;
        }
        return QRectF(area.center().x() - w / 2.0,
                      area.center().y() - h / 2.0,
                      w,
                      h);
    }

    QPointF widgetToNorm(const QPointF &point) const
    {
        const QRectF img = imageRect();
        return QPointF((point.x() - img.left()) / img.width(),
                       (point.y() - img.top()) / img.height());
    }

    QPointF normToWidget(const QPointF &norm) const
    {
        const QRectF img = imageRect();
        return QPointF(img.left() + norm.x() * img.width(),
                       img.top() + norm.y() * img.height());
    }

    bool isNormInside(const QPointF &norm) const
    {
        return norm.x() >= 0.0 && norm.x() <= 1.0
            && norm.y() >= 0.0 && norm.y() <= 1.0;
    }

    qreal metersPerPixel() const
    {
        return (2.0 * kAltitudeM * std::tan(qDegreesToRadians(kFovDeg) / 2.0)) / kImageWidthPx;
    }

    QPointF xHat() const
    {
        const QPointF a(m_originNorm.x() * kImageWidthPx, m_originNorm.y() * kImageHeightPx);
        const QPointF b(m_xAxisNorm.x() * kImageWidthPx, m_xAxisNorm.y() * kImageHeightPx);
        const qreal dx = b.x() - a.x();
        const qreal dy = b.y() - a.y();
        const qreal len = std::hypot(dx, dy);
        if (len < 1e-6) {
            return QPointF(1.0, 0.0);
        }
        return QPointF(dx / len, dy / len);
    }

    QPointF yHat() const
    {
        const QPointF x = xHat();
        return QPointF(x.y(), -x.x());
    }

    QPointF normToMeters(const QPointF &norm) const
    {
        if (!m_hasOrigin || !m_hasXAxis) {
            return QPointF();
        }
        const qreal scale = metersPerPixel();
        const QPointF x = xHat();
        const QPointF y = yHat();
        const qreal du = norm.x() * kImageWidthPx - m_originNorm.x() * kImageWidthPx;
        const qreal dv = norm.y() * kImageHeightPx - m_originNorm.y() * kImageHeightPx;
        return QPointF((du * x.x() + dv * x.y()) * scale,
                       (du * y.x() + dv * y.y()) * scale);
    }

    QPointF metersToNorm(const QPointF &meters) const
    {
        const qreal scale = metersPerPixel();
        const QPointF x = xHat();
        const QPointF y = yHat();
        const qreal xPx = meters.x() / scale;
        const qreal yPx = meters.y() / scale;
        const qreal du = xPx * x.x() + yPx * y.x();
        const qreal dv = xPx * x.y() + yPx * y.y();
        return QPointF((m_originNorm.x() * kImageWidthPx + du) / kImageWidthPx,
                       (m_originNorm.y() * kImageHeightPx + dv) / kImageHeightPx);
    }

    QRectF normalizedMeterRect(const QPointF &a, const QPointF &b) const
    {
        return QRectF(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                      QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
    }

    QPolygonF obstaclePolygon(const QRectF &obstacle) const
    {
        return QPolygonF{
            normToWidget(metersToNorm(obstacle.topLeft())),
            normToWidget(metersToNorm(obstacle.topRight())),
            normToWidget(metersToNorm(obstacle.bottomRight())),
            normToWidget(metersToNorm(obstacle.bottomLeft()))
        };
    }

    QRectF fieldBoundsMeters() const
    {
        if (!axesReady()) {
            const qreal w = kImageWidthPx * metersPerPixel();
            const qreal h = kImageHeightPx * metersPerPixel();
            return QRectF(0, 0, w, h);
        }
        QVector<QPointF> corners = {
            normToMeters(QPointF(0, 0)),
            normToMeters(QPointF(1, 0)),
            normToMeters(QPointF(1, 1)),
            normToMeters(QPointF(0, 1))
        };
        qreal minX = corners.first().x();
        qreal maxX = minX;
        qreal minY = corners.first().y();
        qreal maxY = minY;
        for (const QPointF &corner : corners) {
            minX = qMin(minX, corner.x());
            maxX = qMax(maxX, corner.x());
            minY = qMin(minY, corner.y());
            maxY = qMax(maxY, corner.y());
        }
        return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }

    QRectF autoMapDrawRect() const
    {
        const QRectF img = imageRect();
        if (!m_autoMapBounds.isValid()
            || m_autoMapBounds.width() <= 0.0
            || m_autoMapBounds.height() <= 0.0) {
            return img;
        }

        const qreal scale = qMin(img.width() / m_autoMapBounds.width(),
                                 img.height() / m_autoMapBounds.height());
        const qreal w = m_autoMapBounds.width() * scale;
        const qreal h = m_autoMapBounds.height() * scale;
        return QRectF(img.center().x() - w * 0.5,
                      img.center().y() - h * 0.5,
                      w,
                      h);
    }

    QPointF autoWorldToWidget(const QPointF &world) const
    {
        const QRectF draw = autoMapDrawRect();
        if (!m_autoMapBounds.isValid()
            || m_autoMapBounds.width() <= 0.0
            || m_autoMapBounds.height() <= 0.0) {
            return draw.center();
        }

        const qreal x = draw.left()
            + (world.x() - m_autoMapBounds.left()) / m_autoMapBounds.width() * draw.width();
        const qreal y = draw.bottom()
            - (world.y() - m_autoMapBounds.top()) / m_autoMapBounds.height() * draw.height();
        return QPointF(x, y);
    }

    QRectF autoWorldRectToWidget(const QRectF &rect) const
    {
        return QRectF(autoWorldToWidget(rect.topLeft()),
                      autoWorldToWidget(rect.bottomRight())).normalized();
    }

    void drawAutoYamlMap(QPainter &p)
    {
        if (!m_autoMapBounds.isValid()) {
            return;
        }

        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF draw = autoMapDrawRect();
        p.setPen(QPen(QColor("#35e878"), 1.4, Qt::DashLine));
        p.setBrush(QColor(53, 232, 120, 10));
        p.drawRoundedRect(draw, 4, 4);

        QPainterPath floorPath;
        if (m_autoAreas.isEmpty()) {
            floorPath.addRect(draw);
        } else {
            for (const QRectF &area : m_autoAreas) {
                QPainterPath areaPath;
                areaPath.addRect(autoWorldRectToWidget(area));
                floorPath = floorPath.isEmpty() ? areaPath : floorPath.united(areaPath);
            }
        }
        p.setPen(QPen(QColor(96, 171, 228, 210), 2.0));
        p.setBrush(QColor(28, 52, 72, 40));
        p.drawPath(floorPath);

        int index = 1;
        for (const QRectF &obstacle : m_autoObstacles) {
            const QRectF r = autoWorldRectToWidget(obstacle);
            const bool wall = r.width() < 3.0 || r.height() < 3.0;
            p.setPen(QPen(wall ? QColor("#d6e1ef") : QColor("#ff8a4a"), wall ? 1.4 : 2.0));
            p.setBrush(wall ? QColor(136, 168, 189, 64) : QColor(18, 19, 24, 96));
            p.drawRect(r);
            if (!wall) {
                QFont font = p.font();
                font.setPointSize(9);
                font.setBold(true);
                p.setFont(font);
                p.setPen(QColor("#ffffff"));
                p.drawText(r.topLeft() + QPointF(8, 18),
                           QString("obs_%1").arg(index++, 2, 10, QLatin1Char('0')));
            }
        }

        p.restore();
    }

    void drawAutoYamlOriginFrame(QPainter &p)
    {
        if (!m_autoMapBounds.isValid()) {
            return;
        }

        const QRectF draw = autoMapDrawRect();
        QPointF origin = autoWorldToWidget(QPointF(0.0, 0.0));
        origin.setX(qBound(draw.left(), origin.x(), draw.right()));
        origin.setY(qBound(draw.top(), origin.y(), draw.bottom()));

        const qreal axisLen = qMin(draw.width(), draw.height()) * 0.34;
        const QPointF xEnd(qMin(draw.right(), origin.x() + axisLen), origin.y());
        const QPointF yEnd(origin.x(), qMax(draw.top(), origin.y() - axisLen));

        QFont font = p.font();
        font.setPointSize(10);
        font.setBold(true);
        p.setFont(font);

        p.setBrush(QColor("#35e878"));
        p.setPen(QPen(QColor("#35e878"), 2.4));
        p.drawEllipse(origin, 5.5, 5.5);
        p.drawText(origin + QPointF(9, -8), QStringLiteral("0,0"));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#ff5b57"), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, xEnd);
        p.drawLine(xEnd, xEnd - QPointF(12, -5));
        p.drawLine(xEnd, xEnd - QPointF(12, 5));
        p.drawText(xEnd + QPointF(8, -8), "+X");

        p.setPen(QPen(QColor("#68a7ff"), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, yEnd);
        p.drawLine(yEnd, yEnd + QPointF(-5, 12));
        p.drawLine(yEnd, yEnd + QPointF(5, 12));
        p.drawText(yEnd + QPointF(8, -8), "+Y");
    }

    void drawDroneImage(QPainter &p)
    {
        const QRectF img = imageRect();
        p.setPen(Qt::NoPen);
        p.setBrush(m_previewMode ? QColor("#0a1218") : QColor("#081017"));
        p.drawRoundedRect(img, 4, 4);

        const QRectF target = m_autoYamlPreview ? autoMapDrawRect() : img;
        if (!m_capturedImage.isNull()) {
            p.drawImage(target, m_capturedImage, QRectF(QPointF(0, 0), QSizeF(m_capturedImage.size())));
        } else {
            QLinearGradient fallback(target.topLeft(), target.bottomRight());
            fallback.setColorAt(0.0, QColor("#202a30"));
            fallback.setColorAt(0.55, QColor("#111820"));
            fallback.setColorAt(1.0, QColor("#060a0d"));
            p.setBrush(fallback);
            p.drawRect(target);
        }

        p.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(target);

        if (m_previewMode) {
            p.setPen(QPen(QColor("#35e878"), 2.0, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(target.adjusted(1, 1, -1, -1), 4, 4);
        }
    }

    void drawCapturePlaceholder(QPainter &p, const QRectF &r)
    {
        QFont font = p.font();
        font.setPointSize(14);
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor("#607384"));
        p.drawText(r.adjusted(18, 18, -18, -18), Qt::AlignCenter, QStringLiteral("CAPTURE WAITING"));
    }

    void drawCoordinateFrame(QPainter &p)
    {
        const QPointF origin = normToWidget(m_originNorm);
        p.setPen(QPen(QColor("#35e878"), 2.4));
        p.setBrush(QColor("#35e878"));
        p.drawEllipse(origin, 5.5, 5.5);

        QFont font = p.font();
        font.setPointSize(10);
        font.setBold(true);
        p.setFont(font);
        p.drawText(origin + QPointF(9, -8), "O");

        if (!m_hasXAxis) {
            return;
        }

        const QPointF xEnd = normToWidget(m_xAxisNorm);
        const QPointF x = xHat();
        const QPointF y = yHat();
        const qreal len = qMax(92.0, imageRect().width() * 0.28);
        const QPointF yEnd = origin + QPointF(y.x() * len, y.y() * len);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#ff5b57"), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, xEnd);
        p.drawLine(xEnd, xEnd - QPointF(x.x() * 12 - x.y() * 5, x.y() * 12 + x.x() * 5));
        p.drawLine(xEnd, xEnd - QPointF(x.x() * 12 + x.y() * 5, x.y() * 12 - x.x() * 5));
        p.drawText(xEnd + QPointF(8, -8), "+X");

        p.setPen(QPen(QColor("#68a7ff"), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(origin, yEnd);
        p.drawLine(yEnd, yEnd - QPointF(y.x() * 12 - y.y() * 5, y.y() * 12 + y.x() * 5));
        p.drawLine(yEnd, yEnd - QPointF(y.x() * 12 + y.y() * 5, y.y() * 12 - y.x() * 5));
        p.drawText(yEnd + QPointF(8, -8), "+Y");
    }

    void drawObstacles(QPainter &p)
    {
        if (!axesReady()) {
            return;
        }
        int index = 1;
        for (const QRectF &obstacle : m_obstacles) {
            const QPolygonF poly = obstaclePolygon(obstacle);
            p.setPen(QPen(m_previewMode ? QColor("#b88cff") : QColor("#48b6ff"), 2.0));
            p.setBrush(m_previewMode ? QColor(184, 140, 255, 110) : QColor(40, 150, 255, 82));
            p.drawPolygon(poly);

            QFont font = p.font();
            font.setPointSize(9);
            font.setBold(true);
            p.setFont(font);
            p.setPen(QColor("#ffffff"));
            p.drawText(poly.boundingRect().topLeft() + QPointF(8, 18),
                       QString("obs_%1").arg(index++, 2, 10, QLatin1Char('0')));
        }
    }

    void drawDragPreview(QPainter &p)
    {
        if (!m_dragging || !axesReady()) {
            return;
        }
        const QRectF rect = normalizedMeterRect(m_dragStartMeters, m_dragCurrentMeters);
        const QPolygonF poly = obstaclePolygon(rect);
        p.setPen(QPen(QColor("#ffd21a"), 2.0, Qt::DashLine));
        p.setBrush(QColor(255, 210, 26, 52));
        p.drawPolygon(poly);
    }

    void drawHud(QPainter &p, const QRectF &r)
    {
        QFont font = p.font();
        font.setPointSize(11);
        font.setBold(true);
        p.setFont(font);

        const QString title = QStringLiteral("IMAGE MAP BUILDER");
        const QString status = statusText();
        const int textWidth = qMax(p.fontMetrics().horizontalAdvance(title),
                                   p.fontMetrics().horizontalAdvance(status));
        QRectF badge(r.left() + 14,
                     r.top() + 12,
                     qMin<qreal>(r.width() - 28, qMax(210, textWidth + 32)),
                     58);
        p.setPen(QPen(QColor(43, 73, 94, 185), 1.0));
        p.setBrush(QColor(3, 8, 13, 178));
        p.drawRoundedRect(badge, 6, 6);

        p.setPen(QColor("#ffffff"));
        p.drawText(badge.adjusted(12, 7, -10, -30), Qt::AlignLeft | Qt::AlignVCenter,
                   title);
        p.setPen(axesReady() ? QColor("#35e878") : QColor("#ffd21a"));
        p.drawText(badge.adjusted(12, 29, -10, -7), Qt::AlignLeft | Qt::AlignVCenter,
                   status);

        if (m_hasCursor && axesReady()) {
            const QPointF m = normToMeters(m_cursorNorm);
            QRectF coord(r.right() - 182, r.bottom() - 44, 166, 28);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150));
            p.drawRoundedRect(coord, 5, 5);
            p.setPen(QColor("#dce7f3"));
            p.drawText(coord, Qt::AlignCenter,
                       QString("x %1  y %2").arg(m.x(), 0, 'f', 1).arg(m.y(), 0, 'f', 1));
        }
    }

    void clearAutoYamlPreview()
    {
        m_autoYamlPreview = false;
        m_autoAreas.clear();
        m_autoObstacles.clear();
        m_autoStarts.clear();
        m_autoMapBounds = QRectF();
        m_autoEnd = QPointF();
        m_hasAutoEnd = false;
    }

    void notifyChanged()
    {
        if (m_changed) {
            m_changed();
        }
    }

    void ensureCapturedImageLoaded()
    {
        if (m_captureImageLoadAttempted) {
            return;
        }
        m_captureImageLoadAttempted = true;
        const QString path = findDroneCaptureImagePath();
        if (!path.isEmpty() && !m_capturedImage.load(path)) {
            qWarning() << "DroneMapWidget: failed to load capture image" << path;
        }
    }

    qreal capturedImageAspectRatio() const
    {
        if (!m_capturedImage.isNull() && m_capturedImage.height() > 0) {
            return static_cast<qreal>(m_capturedImage.width()) / static_cast<qreal>(m_capturedImage.height());
        }
        return 16.0 / 9.0;
    }

    Tool m_tool = OriginTool;
    bool m_hasOrigin = false;
    bool m_hasXAxis = false;
    bool m_hasCapturedImage = false;
    bool m_dragging = false;
    bool m_previewMode = false;
    bool m_hasCursor = false;
    QPointF m_originNorm;
    QPointF m_xAxisNorm;
    QPointF m_cursorNorm;
    QPointF m_dragStartMeters;
    QPointF m_dragCurrentMeters;
    QVector<QRectF> m_obstacles;
    QImage m_capturedImage;
    bool m_captureImageLoadAttempted = false;
    bool m_autoYamlPreview = false;
    bool m_hasAutoEnd = false;
    QRectF m_autoMapBounds;
    QPointF m_autoEnd;
    QVector<QRectF> m_autoAreas;
    QVector<QRectF> m_autoObstacles;
    QVector<QPointF> m_autoStarts;
    std::function<void()> m_changed;
};

class DroneStatusIcon : public QWidget
{
public:
    explicit DroneStatusIcon(const QString &kind, QWidget *parent = nullptr)
        : QWidget(parent), m_kind(kind)
    {
        setObjectName("droneStatusIcon");
        setFixedSize(58, 58);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(7, 7, -7, -7);
        const QColor blue("#4fb0ff");
        const QColor green("#35e878");
        const QColor color = (m_kind == QStringLiteral("battery")
                              || m_kind == QStringLiteral("signal")) ? green : blue;
        p.setPen(QPen(QColor(color.red(), color.green(), color.blue(), 46), 5.0,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawIcon(p, r, color, true);
        p.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawIcon(p, r, color, false);
    }

private:
    void drawIcon(QPainter &p, const QRectF &r, const QColor &color, bool glow)
    {
        Q_UNUSED(color);
        if (m_kind == QStringLiteral("battery")) {
            const QRectF body(r.left() + 8, r.top() + 3, r.width() - 16, r.height() - 6);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(body, 4, 4);
            p.drawLine(QPointF(body.center().x() - 5, body.top() - 4),
                       QPointF(body.center().x() + 5, body.top() - 4));
            p.drawLine(QPointF(body.center().x() - 5, body.top() - 4),
                       QPointF(body.center().x() - 5, body.top() + 1));
            p.drawLine(QPointF(body.center().x() + 5, body.top() - 4),
                       QPointF(body.center().x() + 5, body.top() + 1));
            if (!glow) {
                p.setBrush(QColor("#35e878"));
                p.setPen(Qt::NoPen);
                const qreal gap = 3.5;
                const qreal h = (body.height() - gap * 5) / 4.0;
                for (int i = 0; i < 4; ++i) {
                    const qreal y = body.bottom() - gap - (i + 1) * h - i * gap;
                    p.drawRoundedRect(QRectF(body.left() + 5, y, body.width() - 10, h), 2.0, 2.0);
                }
            }
        } else if (m_kind == QStringLiteral("altitude")) {
            const qreal cx = r.center().x();
            const qreal top = r.top() + 6.0;
            const qreal gap = 11.0;
            const qreal half = 13.0;
            for (int i = 0; i < 3; ++i) {
                const qreal y = top + i * gap;
                p.drawLine(QPointF(cx - half, y + 10.0), QPointF(cx, y));
                p.drawLine(QPointF(cx, y), QPointF(cx + half, y + 10.0));
            }
        } else if (m_kind == QStringLiteral("speed")) {
            const QPointF c = r.center() + QPointF(0, 7);
            const qreal rad = r.width() * 0.46;
            p.drawArc(QRectF(c.x() - rad, c.y() - rad, rad * 2, rad * 2), 28 * 16, 124 * 16);
            p.drawLine(c, c + QPointF(rad * 0.48, -rad * 0.50));
            p.drawEllipse(c, 2.4, 2.4);
        } else {
            const qreal base = r.bottom() - 4;
            const QVector<qreal> heights = {10, 18, 27, 36};
            for (int i = 0; i < heights.size(); ++i) {
                const qreal x = r.left() + 8 + i * 10;
                const QRectF bar(x, base - heights[i], 5.0, heights[i]);
                p.setPen(Qt::NoPen);
                p.setBrush(glow
                               ? QColor(53, 232, 120, 48)
                               : QColor("#35e878"));
                p.drawRoundedRect(bar, 2.3, 2.3);
            }
        }
    }

    QString m_kind;
};

class HeaderMetricIcon : public QWidget
{
public:
    explicit HeaderMetricIcon(const QString &kind, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_kind(kind)
    {
        setFixedSize(48, 48);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QPointF c = rect().center();
        QRadialGradient halo(c, 24);
        halo.setColorAt(0.0, QColor(255, 210, 26, 42));
        halo.setColorAt(0.62, QColor(0, 216, 255, 24));
        halo.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QRectF(c.x() - 23, c.y() - 23, 46, 46));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#ffd21a"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        drawIcon(p, c, 0.82);
    }

private:
    void drawIcon(QPainter &p, const QPointF &c, qreal s)
    {
        if (m_kind == QStringLiteral("progress")) {
            const QColor ringColor("#ffd21a");

            p.save();
            p.setPen(QPen(QColor(255, 210, 26, 178), 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (qreal radius : {7.0 * s, 14.5 * s}) {
                p.drawEllipse(c, radius, radius);
            }
            p.setPen(QPen(QColor(255, 210, 26, 210), 2.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawEllipse(c, 22.0 * s, 22.0 * s);

            p.setPen(QPen(ringColor, 2.0, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c, QPointF(c.x() + 19 * s, c.y() - 10 * s));

            p.setPen(Qt::NoPen);
            p.setBrush(ringColor);
            p.drawEllipse(c, 3.8 * s, 3.8 * s);
            p.drawEllipse(QPointF(c.x() + 19 * s, c.y() - 10 * s), 2.5 * s, 2.5 * s);
            p.restore();
        } else if (m_kind == QStringLiteral("time")) {
            p.drawEllipse(c, 17 * s, 17 * s);
            p.drawLine(c, QPointF(c.x(), c.y() - 10 * s));
            p.drawLine(c, QPointF(c.x() + 9 * s, c.y() + 5 * s));
        } else if (m_kind == QStringLiteral("robot")) {
            const QPointF rc(c.x(), c.y() + 5 * s);
            const QRectF body(rc.x() - 13 * s, rc.y() - 6 * s, 26 * s, 18 * s);
            p.drawRoundedRect(body, 5 * s, 5 * s);
            p.drawEllipse(QPointF(rc.x() - 5 * s, rc.y() + 2 * s), 1.7 * s, 1.7 * s);
            p.drawEllipse(QPointF(rc.x() + 5 * s, rc.y() + 2 * s), 1.7 * s, 1.7 * s);
            p.drawLine(QPointF(rc.x(), rc.y() - 6 * s), QPointF(rc.x(), rc.y() - 13 * s));
            p.drawArc(QRectF(rc.x() - 12 * s, rc.y() - 22 * s, 24 * s, 16 * s), 35 * 16, 110 * 16);
            p.drawArc(QRectF(rc.x() - 17 * s, rc.y() - 27 * s, 34 * s, 24 * s), 35 * 16, 110 * 16);
        } else {
            const QRectF bell(c.x() - 11 * s, c.y() - 9 * s, 22 * s, 22 * s);
            p.drawArc(bell, 20 * 16, 140 * 16);
            p.drawLine(QPointF(c.x() - 11 * s, c.y() + 2 * s), QPointF(c.x() - 11 * s, c.y() + 10 * s));
            p.drawLine(QPointF(c.x() + 11 * s, c.y() + 2 * s), QPointF(c.x() + 11 * s, c.y() + 10 * s));
            p.drawLine(QPointF(c.x() - 14 * s, c.y() + 10 * s), QPointF(c.x() + 14 * s, c.y() + 10 * s));
            p.drawArc(QRectF(c.x() - 4 * s, c.y() + 10 * s, 8 * s, 7 * s), 180 * 16, 180 * 16);
            p.drawLine(QPointF(c.x(), c.y() - 14 * s), QPointF(c.x(), c.y() - 18 * s));
        }
    }

    QString m_kind;
};

class EmptyEventListWidget : public QListWidget
{
public:
    explicit EmptyEventListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QListWidget::paintEvent(event);
        if (count() > 0) {
            return;
        }

        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const QRectF area = viewport()->rect().adjusted(18, 18, -18, -18);
        const QPointF c(area.center().x(), area.center().y() - 42.0);
        const qreal s = 0.72;

        QRadialGradient halo(c, 28);
        halo.setColorAt(0.0, QColor(255, 210, 26, 34));
        halo.setColorAt(0.72, QColor(0, 216, 255, 18));
        halo.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(QRectF(c.x() - 28, c.y() - 28, 56, 56));

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#ffd21a"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath doc;
        doc.moveTo(c.x() - 15 * s, c.y() - 21 * s);
        doc.lineTo(c.x() + 7 * s, c.y() - 21 * s);
        doc.lineTo(c.x() + 17 * s, c.y() - 11 * s);
        doc.lineTo(c.x() + 17 * s, c.y() + 22 * s);
        doc.lineTo(c.x() - 15 * s, c.y() + 22 * s);
        doc.closeSubpath();
        p.drawPath(doc);
        p.drawLine(QPointF(c.x() + 7 * s, c.y() - 21 * s),
                   QPointF(c.x() + 7 * s, c.y() - 11 * s));
        p.drawLine(QPointF(c.x() + 7 * s, c.y() - 11 * s),
                   QPointF(c.x() + 17 * s, c.y() - 11 * s));
        p.drawLine(QPointF(c.x() - 7 * s, c.y() - 2 * s),
                   QPointF(c.x() + 8 * s, c.y() - 2 * s));
        p.drawLine(QPointF(c.x() - 7 * s, c.y() + 8 * s),
                   QPointF(c.x() + 10 * s, c.y() + 8 * s));

        p.setPen(QColor("#f3f7fc"));
        QFont title(QStringLiteral("Noto Sans"), 14, QFont::Black);
        p.setFont(title);
        p.drawText(QRectF(area.left(), c.y() + 34, area.width(), 28),
                   Qt::AlignCenter, QStringLiteral("이벤트가 없습니다"));
        p.setPen(QColor("#aeb8c8"));
        QFont sub(QStringLiteral("Noto Sans"), 10, QFont::DemiBold);
        p.setFont(sub);
        p.drawText(QRectF(area.left(), c.y() + 64, area.width(), 24),
                   Qt::AlignCenter, QStringLiteral("발생한 이벤트가 여기에 표시됩니다."));
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const int demoRobotCount = demoRobotCountFromEnvironment();
    if (demoRobotCount > 0) {
        m_demoMode = true;
        m_robotCount = demoRobotCount;
    } else {
        m_robotCount = kDefaultRobotCount;
    }

    buildUi();
    applyStyle();
    qApp->installEventFilter(new ButtonGlowFilter(this));

    if (m_demoMode) {
        startDemoMode(m_robotCount);
    } else {
        connect(&m_monitor, &ShmMonitor::snapshotsUpdated, this, &MainWindow::updateSnapshots);
        connect(&m_monitor, &ShmMonitor::eventReceived, this, [this](UiEvent event) {
            if (event.robotId >= 0) {
                event.robotId = physicalToDisplayRobotId(event.robotId);
            }
            appendEvent(event);
        });
        m_monitor.start(kMaxRobots);
    }

    QTimer *clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this] {
        m_clock->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
        updateMissionSummary();
    });
    clockTimer->start(1000);
    m_clock->setText(QDateTime::currentDateTime().toString("hh:mm:ss"));
}

MainWindow::~MainWindow()
{
}

void MainWindow::buildUi()
{
    resize(1680, 940);
    setWindowTitle("SPOT Get IT - 통합 관제 대시보드");

    QWidget *root = new QWidget(this);
    setCentralWidget(root);
    QHBoxLayout *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 2, 12, 2);
    rootLayout->setSpacing(12);

    QFrame *nav = new QFrame;
    nav->setObjectName("nav");
    nav->setFixedWidth(126);
    QVBoxLayout *navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(14, 10, 14, 16);
    navLayout->setSpacing(12);
    auto addNavSeparator = [navLayout]() {
        QFrame *line = new QFrame;
        line->setObjectName("navSeparator");
        line->setFixedSize(56, 1);
        line->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        navLayout->addWidget(line, 0, Qt::AlignHCenter);
    };
    QPushButton *navExplore = makeNavButton("관제", true);
    QPushButton *navRobots = makeNavButton("로봇\n상태");
    QPushButton *navDrone = makeNavButton("드론");
    QPushButton *navLogs = makeNavButton("로그");
    QPushButton *navSettings = makeNavButton("설정");
    m_navButtons = {navExplore, navRobots, navDrone, navLogs, navSettings};
    navLayout->addWidget(new NavBrandWidget(nav), 0, Qt::AlignHCenter);
    navLayout->addSpacing(8);
    navLayout->addWidget(navExplore);
    addNavSeparator();
    navLayout->addWidget(navRobots);
    addNavSeparator();
    navLayout->addWidget(navDrone);
    addNavSeparator();
    navLayout->addWidget(navLogs);
    addNavSeparator();
    navLayout->addWidget(navSettings);
    m_manualActionPanel = new QWidget(nav);
    m_manualActionPanel->setObjectName("manualActionPanel");
    QHBoxLayout *manualActionLayout = new QHBoxLayout(m_manualActionPanel);
    manualActionLayout->setContentsMargins(0, 0, 0, 0);
    manualActionLayout->setSpacing(12);
    manualActionLayout->setAlignment(Qt::AlignVCenter);
    auto makeManualActionButton = [this, manualActionLayout](const QString &text,
                                                             int actionCode,
                                                             const QString &label) {
        QPushButton *button = new QPushButton(text, m_manualActionPanel);
        button->setObjectName("manualActionButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(96);
        button->setMaximumHeight(108);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QPushButton::clicked, this, [this, actionCode, label]() {
            sendManualAction(actionCode, label);
        });
        manualActionLayout->addWidget(button);
    };
    makeManualActionButton(QStringLiteral("서기"), kManualActionStand, QStringLiteral("서기"));
    makeManualActionButton(QStringLiteral("앉기"), kManualActionSit, QStringLiteral("앉기"));
    makeManualActionButton(QStringLiteral("인사"), kManualActionGreet, QStringLiteral("인사"));
    m_manualActionPanel->hide();
    navLayout->addStretch(1);
    navLayout->addWidget(new NavWatermark(nav), 0, Qt::AlignHCenter);
    rootLayout->addWidget(nav);

    QVBoxLayout *main = new QVBoxLayout;
    main->setSpacing(12);
    rootLayout->addLayout(main, 1);

    m_victimAlertButton = new QPushButton(root);
    m_victimAlertButton->setObjectName("victimAlert");
    m_victimAlertButton->setCursor(Qt::PointingHandCursor);
    m_victimAlertButton->setText(QString());
    auto *victimAlertGlow = new QGraphicsDropShadowEffect(m_victimAlertButton);
    victimAlertGlow->setColor(QColor(255, 55, 48, 112));
    victimAlertGlow->setBlurRadius(34.0);
    victimAlertGlow->setOffset(0, 0);
    m_victimAlertButton->setGraphicsEffect(victimAlertGlow);
    QVBoxLayout *victimAlertLayout = new QVBoxLayout(m_victimAlertButton);
    victimAlertLayout->setContentsMargins(32, 6, 32, 22);
    victimAlertLayout->setSpacing(12);
    m_victimAlertTitle = new QLabel("생존자 탐지", m_victimAlertButton);
    m_victimAlertTitle->setObjectName("victimAlertTitle");
    m_victimAlertTitle->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_victimAlertTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    QWidget *victimAlertFooter = new QWidget(m_victimAlertButton);
    victimAlertFooter->setObjectName("victimAlertFooter");
    victimAlertFooter->setFixedWidth(430);
    victimAlertFooter->setMinimumHeight(64);
    victimAlertFooter->setMaximumHeight(64);
    victimAlertFooter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    victimAlertFooter->setAttribute(Qt::WA_TransparentForMouseEvents);
    QHBoxLayout *victimAlertFooterLayout = new QHBoxLayout(victimAlertFooter);
    victimAlertFooterLayout->setContentsMargins(0, 9, 0, 9);
    victimAlertFooterLayout->setSpacing(34);
    m_victimAlertRobot = new QLabel("SPOT-01", victimAlertFooter);
    m_victimAlertRobot->setObjectName("victimAlertSubtitle");
    m_victimAlertRobot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_victimAlertAction = new QLabel("카메라 보기", victimAlertFooter);
    m_victimAlertAction->setObjectName("victimAlertSubtitle");
    m_victimAlertAction->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_victimAlertAction->setAttribute(Qt::WA_TransparentForMouseEvents);
    victimAlertFooterLayout->addStretch(1);
    victimAlertFooterLayout->addWidget(m_victimAlertRobot);
    victimAlertFooterLayout->addWidget(m_victimAlertAction);
    victimAlertFooterLayout->addStretch(1);
    victimAlertLayout->addStretch(1);
    victimAlertLayout->addWidget(m_victimAlertTitle, 1);
    victimAlertLayout->addWidget(victimAlertFooter, 0, Qt::AlignHCenter);
    victimAlertLayout->addSpacing(18);
    m_victimAlertButton->hide();
    connect(m_victimAlertButton, &QPushButton::clicked, this, [this]() {
        const int robotId = m_victimAlertRobotId;
        m_victimAlertButton->hide();
        if (robotId >= 0) {
            showCameraFullscreen(robotId);
        }
    });

    QFrame *top = new QFrame;
    top->setObjectName("topBar");
    QHBoxLayout *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(18, 10, 18, 10);
    QLabel *title = new QLabel("통합 관제 대시보드");
    title->setObjectName("title");
    m_crumb = new QLabel("  >  관제");
    m_crumb->setObjectName("accentText");
    m_system = new QLabel("● 시스템 대기");
    m_system->setObjectName("systemText");
    m_clock = new QLabel;
    m_clock->setObjectName("clockText");
    topLayout->addWidget(title);
    topLayout->addWidget(m_crumb);
    topLayout->addStretch();
    topLayout->addWidget(m_system);
    topLayout->addSpacing(18);
    topLayout->addWidget(m_clock);
    main->addWidget(top);

    m_contentStack = new QStackedWidget;
    main->addWidget(m_contentStack, 1);

    m_manualJoystickPanel = new QFrame(root);
    m_manualJoystickPanel->setObjectName("manualJoystickPanel");
    m_manualJoystickPanel->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *manualJoystickLayout = new QVBoxLayout(m_manualJoystickPanel);
    manualJoystickLayout->setContentsMargins(6, 8, 6, 14);
    manualJoystickLayout->setSpacing(0);
    m_manualJoystick = new ManualJoystickWidget(m_manualJoystickPanel);
    m_manualJoystick->setVelocityCallback([this](float vx, float vy, float omega, bool force) {
        sendManualVelocity(vx, vy, omega, force);
    });
    manualJoystickLayout->addWidget(m_manualJoystick);
    m_manualJoystickPanel->hide();

    QWidget *dashboardPage = new QWidget;
    QVBoxLayout *dashboard = new QVBoxLayout(dashboardPage);
    dashboard->setContentsMargins(0, 0, 0, 0);
    dashboard->setSpacing(12);

    QHBoxLayout *metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    metrics->addWidget(makeHeaderMetricCard("탐색 진행률", &m_metricProgress, "#18d878",
                                            &m_metricProgressBar, "progress"), 3);
    metrics->addWidget(makeHeaderMetricCard("임무 수행 시간", &m_metricMissionTime, "#ffffff",
                                            nullptr, "time"), 2);
    metrics->addWidget(makeHeaderMetricCard("연결 로봇 갯수", &m_metricConnected, "#ffffff",
                                            nullptr, "robot"), 2);
    metrics->addWidget(makeHeaderMetricCard("주요 이벤트", &m_metricEvents, "#ffffff",
                                            nullptr, "event"), 2);
    dashboard->addLayout(metrics);

    QHBoxLayout *upper = new QHBoxLayout;
    upper->setSpacing(12);
    QWidget *videoBody = new QWidget;
    m_videoGrid = new QGridLayout(videoBody);
    m_videoGrid->setContentsMargins(0, 0, 0, 0);
    m_videoGrid->setSpacing(8);
    m_videoGrid->setColumnStretch(0, 1);
    m_videoGrid->setColumnStretch(1, 1);
    m_videoScroll = new QScrollArea;
    m_videoScroll->setObjectName("panelScrollArea");
    m_videoScroll->setWidgetResizable(true);
    m_videoScroll->setFrameShape(QFrame::NoFrame);
    m_videoScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_videoScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_videoScroll->setWidget(videoBody);
    syncVideoTiles(m_robotCount);
    upper->addWidget(makePanel("실시간 영상 스트리밍", m_videoScroll), 5);

    QWidget *mapBody = new QWidget;
    m_mapLayout = new QVBoxLayout(mapBody);
    m_mapLayout->setContentsMargins(0, 0, 0, 0);
    m_mapLayout->setSpacing(0);
    m_btn2d = new QPushButton("2D");
    m_btn2d->setObjectName("smallActive");
    m_btn3d = new QPushButton("3D");
    m_btn3d->setObjectName("smallButton");
    QPushButton *mapMaximize = new QPushButton("최대화");
    mapMaximize->setObjectName("smallButton");
    for (QPushButton *button : {m_btn2d, m_btn3d, mapMaximize}) {
        button->setFixedHeight(30);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    m_map = new MapWidget;
    connect(m_map, &MapWidget::routeGenerationRequested,
            this, &MainWindow::handleRouteGenerationRequested);
    m_mapLayout->addWidget(m_map, 1);

    QFrame *mapPanel = new QFrame;
    mapPanel->setObjectName("panel");
    QVBoxLayout *mapPanelLayout = new QVBoxLayout(mapPanel);
    mapPanelLayout->setContentsMargins(14, 12, 14, 14);
    mapPanelLayout->setSpacing(10);
    QHBoxLayout *mapHeader = new QHBoxLayout;
    mapHeader->setContentsMargins(0, 0, 0, 0);
    mapHeader->setSpacing(8);
    QLabel *mapTitle = new QLabel("탐색 지도");
    mapTitle->setObjectName("panelTitle");
    mapHeader->addWidget(mapTitle, 0, Qt::AlignVCenter);
    mapHeader->addStretch(1);
    mapHeader->addWidget(m_btn2d, 0, Qt::AlignVCenter);
    mapHeader->addWidget(m_btn3d, 0, Qt::AlignVCenter);
    mapHeader->addWidget(mapMaximize, 0, Qt::AlignVCenter);
    mapPanelLayout->addLayout(mapHeader);
    mapPanelLayout->addWidget(mapBody, 1);
    upper->addWidget(mapPanel, 5);
    dashboard->addLayout(upper, 5);

    QHBoxLayout *lower = new QHBoxLayout;
    lower->setSpacing(12);

    QWidget *robotBody = new QWidget;
    m_robotStatusGrid = new QGridLayout(robotBody);
    m_robotStatusGrid->setContentsMargins(0, 0, 0, 0);
    m_robotStatusGrid->setSpacing(6);
    m_robotStatusScroll = new QScrollArea;
    m_robotStatusScroll->setObjectName("panelScrollArea");
    m_robotStatusScroll->setWidgetResizable(true);
    m_robotStatusScroll->setFrameShape(QFrame::NoFrame);
    m_robotStatusScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_robotStatusScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_robotStatusScroll->setWidget(robotBody);
    syncStatusRows(m_robotCount);
    lower->addWidget(makePanel("로봇 상태", m_robotStatusScroll), 4);

    m_eventList = new EmptyEventListWidget;
    m_eventList->setObjectName("eventList");
    lower->addWidget(makePanel("이벤트 목록", m_eventList), 3);

    QWidget *controlBody = new QWidget;
    QVBoxLayout *control = new QVBoxLayout(controlBody);
    control->setContentsMargins(0, 0, 0, 0);
    control->setSpacing(10);
    m_robotSelectRow = new QWidget(controlBody);
    QHBoxLayout *robots = new QHBoxLayout(m_robotSelectRow);
    robots->setContentsMargins(0, 0, 0, 0);
    robots->setSpacing(8);
    QLabel *robotSelectLabel = new QLabel("제어 로봇");
    robotSelectLabel->setObjectName("infoLine");
    m_robotSelector = new BorderlessComboBox;
    m_robotSelector->setMinimumHeight(44);
    m_robotSelector->setMaxVisibleItems(qMin(m_robotCount + 1, kMaxRobots + 1));
    QListView *selectorView = new QListView(m_robotSelector);
    selectorView->setFrameShape(QFrame::NoFrame);
    selectorView->setFrameStyle(QFrame::NoFrame);
    selectorView->setLineWidth(0);
    selectorView->setMidLineWidth(0);
    selectorView->setContentsMargins(0, 0, 0, 0);
    selectorView->setSpacing(0);
    selectorView->setUniformItemSizes(true);
    selectorView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    selectorView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    selectorView->viewport()->setContentsMargins(0, 0, 0, 0);
    selectorView->setAttribute(Qt::WA_StyledBackground, true);
    selectorView->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    m_robotSelector->setView(selectorView);
    if (m_robotSelector->view()) {
        m_robotSelector->view()->setAutoFillBackground(true);
        m_robotSelector->view()->viewport()->setAutoFillBackground(true);
        m_robotSelector->view()->setStyleSheet(
            "QListView, QAbstractItemView { background:#071017; border:0; padding:0; margin:0; outline:0; }"
            "QListView::viewport, QAbstractScrollArea::viewport { background:#071017; border:0; margin:0; padding:0; }"
            "QListView::item, QAbstractItemView::item { background:#071017; color:#dce7f3; min-height:34px; padding:6px 10px; margin:0; border:0; }"
            "QListView::item:selected, QAbstractItemView::item:selected { background:#1b1805; color:#ffd21a; }"
            "QListView::item:disabled, QAbstractItemView::item:disabled { color:#64717d; background:#071017; }");
        QPalette popupPalette = m_robotSelector->view()->palette();
        popupPalette.setColor(QPalette::Base, QColor("#071017"));
        popupPalette.setColor(QPalette::Window, QColor("#071017"));
        popupPalette.setColor(QPalette::AlternateBase, QColor("#071017"));
        popupPalette.setColor(QPalette::Button, QColor("#071017"));
        popupPalette.setColor(QPalette::Light, QColor("#071017"));
        popupPalette.setColor(QPalette::Midlight, QColor("#071017"));
        popupPalette.setColor(QPalette::Mid, QColor("#071017"));
        popupPalette.setColor(QPalette::Dark, QColor("#071017"));
        popupPalette.setColor(QPalette::Shadow, QColor("#071017"));
        m_robotSelector->view()->setPalette(popupPalette);
        m_robotSelector->view()->viewport()->setPalette(popupPalette);
    }
    connect(m_robotSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updatingRobotSelector || index < 0) {
            return;
        }
        const int robotId = m_robotSelector->itemData(index).toInt();
        selectRobot(robotId);
    });
    robots->addWidget(robotSelectLabel);
    robots->addWidget(m_robotSelector, 1);

    m_autoCommandBody = new QWidget(controlBody);
    m_commandButtonLayout = new QHBoxLayout(m_autoCommandBody);
    m_commandButtonLayout->setContentsMargins(0, 0, 0, 0);
    m_commandButtonLayout->setSpacing(8);
    m_moveButton = makeCommandButton(QStringLiteral("이동"), "yellowCommand");
    m_moveButton->setProperty("commandKind", "move");
    m_manualToggleButton = makeCommandButton(QStringLiteral("수동 제어"), "controlCommand");
    m_manualToggleButton->setProperty("commandKind", "manual");
    m_manualToggleButton->setCheckable(true);
    m_manualModeToggleButton = makeCommandButton(QStringLiteral("자동 제어"), "controlCommand");
    m_manualModeToggleButton->setProperty("commandKind", "auto");
    m_manualModeToggleButton->setObjectName("manualModeToggleButton");
    m_manualModeToggleButton->setCheckable(true);
    m_manualModeToggleButton->setChecked(true);
    m_addRobotButton = makeCommandButton(QStringLiteral("로봇 투입"), "controlCommand");
    m_addRobotButton->setProperty("commandKind", "deploy");
    m_addRobotButton->setEnabled(false);
    m_estopButton = makeCommandButton(QStringLiteral("긴급 정지"), "redCommand");
    m_estopButton->setProperty("striped", true);
    m_estopButton->setProperty("commandKind", "estop");
    connect(m_moveButton, &QPushButton::clicked, this, &MainWindow::sendMove);
    connect(m_manualToggleButton, &QPushButton::clicked, this, &MainWindow::toggleManualControl);
    connect(m_manualModeToggleButton, &QPushButton::clicked, this, &MainWindow::toggleManualControl);
    connect(m_addRobotButton, &QPushButton::clicked, this, [this]() {
        const int available = kMaxRobots - m_robotCount;
        if (available <= 0) {
            QMessageBox::information(this,
                                     "로봇 투입",
                                     QString("최대 %1대까지 투입할 수 있습니다.").arg(kMaxRobots));
            return;
        }

        QDialog countDialog(this);
        countDialog.setWindowTitle("로봇 투입");
        countDialog.setAttribute(Qt::WA_InputMethodEnabled, false);
        countDialog.setFocusPolicy(Qt::NoFocus);

        QVBoxLayout *countLayout = new QVBoxLayout(&countDialog);
        countLayout->setContentsMargins(24, 20, 24, 18);
        countLayout->setSpacing(14);

        QLabel *countLabel = new QLabel("투입할 로봇 수를 선택하세요", &countDialog);
        countLabel->setWordWrap(true);
        countLabel->setFocusPolicy(Qt::NoFocus);
        countLabel->setAttribute(Qt::WA_InputMethodEnabled, false);
        countLabel->setStyleSheet("QLabel { font-size: 16px; font-weight: 700; }");
        countLayout->addWidget(countLabel);

        int selectedAddCount = 1;
        QHBoxLayout *stepperLayout = new QHBoxLayout;
        stepperLayout->setContentsMargins(0, 4, 0, 4);
        stepperLayout->setSpacing(12);

        QPushButton *minusButton = new QPushButton("-", &countDialog);
        QPushButton *plusButton = new QPushButton("+", &countDialog);
        for (QPushButton *button : {minusButton, plusButton}) {
            button->setFocusPolicy(Qt::NoFocus);
            button->setAttribute(Qt::WA_InputMethodEnabled, false);
            button->setMinimumSize(84, 84);
            button->setCursor(Qt::PointingHandCursor);
            button->setStyleSheet("QPushButton { font-size: 34px; font-weight: 800; border-radius: 8px; padding: 0; }");
        }

        QLabel *countValue = new QLabel(&countDialog);
        countValue->setAlignment(Qt::AlignCenter);
        countValue->setMinimumSize(180, 84);
        countValue->setFocusPolicy(Qt::NoFocus);
        countValue->setAttribute(Qt::WA_InputMethodEnabled, false);
        countValue->setStyleSheet("QLabel { font-size: 42px; font-weight: 800; border: 1px solid rgba(255,255,255,70); border-radius: 8px; padding: 8px 18px; }");

        auto updateCountValue = [&]() {
            countValue->setText(QString("%1대").arg(selectedAddCount));
            minusButton->setEnabled(selectedAddCount > 1);
            plusButton->setEnabled(selectedAddCount < available);
        };
        connect(minusButton, &QPushButton::clicked, &countDialog, [&]() {
            selectedAddCount = qMax(1, selectedAddCount - 1);
            updateCountValue();
        });
        connect(plusButton, &QPushButton::clicked, &countDialog, [&]() {
            selectedAddCount = qMin(available, selectedAddCount + 1);
            updateCountValue();
        });
        updateCountValue();

        stepperLayout->addWidget(minusButton);
        stepperLayout->addWidget(countValue, 1);
        stepperLayout->addWidget(plusButton);
        countLayout->addLayout(stepperLayout);

        QDialogButtonBox *countButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                                              Qt::Horizontal,
                                                              &countDialog);
        auto makeDeployDialogIcon = [](bool deploy) {
            constexpr int side = 28;
            QPixmap pix(side, side);
            pix.fill(Qt::transparent);

            QPainter p(&pix);
            p.setRenderHint(QPainter::Antialiasing, true);

            const QColor stroke = deploy ? QColor(138, 210, 255) : QColor(255, 118, 118);
            const QColor glow = deploy ? QColor(72, 176, 255, 65) : QColor(255, 84, 84, 60);
            const QColor fill = deploy ? QColor(25, 73, 108, 120) : QColor(88, 37, 45, 118);
            QPen glowPen(glow, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            QPen linePen(stroke, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

            QRectF body(6.0, 10.0, 12.0, 8.0);
            p.setPen(glowPen);
            p.drawRoundedRect(body, 3.0, 3.0);
            p.setPen(linePen);
            p.setBrush(fill);
            p.drawRoundedRect(body, 3.0, 3.0);
            p.setBrush(stroke);
            p.drawEllipse(QPointF(10.0, 14.0), 1.1, 1.1);
            p.drawEllipse(QPointF(14.5, 14.0), 1.1, 1.1);
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(7.5, 18.0), QPointF(5.5, 21.5));
            p.drawLine(QPointF(16.5, 18.0), QPointF(18.5, 21.5));

            if (deploy) {
                p.drawLine(QPointF(19.0, 14.0), QPointF(24.0, 14.0));
                p.drawLine(QPointF(21.5, 10.8), QPointF(24.2, 14.0));
                p.drawLine(QPointF(21.5, 17.2), QPointF(24.2, 14.0));
            } else {
                p.drawLine(QPointF(19.5, 9.0), QPointF(24.0, 18.8));
                p.drawLine(QPointF(24.0, 9.0), QPointF(19.5, 18.8));
            }

            return QIcon(pix);
        };
        QPushButton *deployButton = countButtons->button(QDialogButtonBox::Ok);
        QPushButton *cancelButton = countButtons->button(QDialogButtonBox::Cancel);
        deployButton->setText("투입");
        deployButton->setIcon(makeDeployDialogIcon(true));
        deployButton->setIconSize(QSize(28, 28));
        deployButton->setFocusPolicy(Qt::NoFocus);
        deployButton->setAttribute(Qt::WA_InputMethodEnabled, false);
        cancelButton->setText("취소");
        cancelButton->setIcon(makeDeployDialogIcon(false));
        cancelButton->setIconSize(QSize(28, 28));
        cancelButton->setFocusPolicy(Qt::NoFocus);
        cancelButton->setAttribute(Qt::WA_InputMethodEnabled, false);
        connect(countButtons, &QDialogButtonBox::accepted, &countDialog, &QDialog::accept);
        connect(countButtons, &QDialogButtonBox::rejected, &countDialog, &QDialog::reject);
        countLayout->addWidget(countButtons);

        QTimer::singleShot(0, &countDialog, [&countDialog]() {
            countDialog.setFocus();
            if (QGuiApplication::inputMethod()) {
                QGuiApplication::inputMethod()->hide();
            }
        });

        if (countDialog.exec() != QDialog::Accepted) {
            return;
        }

        const int addCount = selectedAddCount;
        if (addCount <= 0) {
            return;
        }

        const int previousCount = m_robotCount;
        setRobotCount(m_robotCount + addCount);
        if (previousCount == 0 && addCount == 4) {
            selectRobot(kAllRobotsSelection);
        } else {
            selectRobot(m_robotCount - 1);
        }
        if (m_map && m_robotCount > previousCount) {
            refreshGlobalPathDisplay();
        }
        appendEvent(UiEvent{-1,
                            1,
                            0,
                            static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL,
                            QString("로봇 투입: %1대 -> %2대").arg(previousCount).arg(m_robotCount)});
    });
    connect(m_estopButton, &QPushButton::clicked, this, &MainWindow::sendEstop);
    for (QPushButton *button : {m_moveButton, m_manualToggleButton, m_addRobotButton, m_estopButton}) {
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    m_manualModeToggleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_manualModeToggleButton->setMinimumHeight(116);
    m_manualModeToggleButton->setMaximumHeight(124);

    m_manualControlBody = new QWidget(controlBody);
    m_manualControlBody->setObjectName("manualControlBody");
    QGridLayout *manualModeLayout = new QGridLayout(m_manualControlBody);
    manualModeLayout->setSizeConstraint(QLayout::SetNoConstraint);
    manualModeLayout->setContentsMargins(0, 0, 0, 0);
    manualModeLayout->setHorizontalSpacing(12);
    manualModeLayout->setVerticalSpacing(8);
    manualModeLayout->setColumnStretch(0, 5);
    manualModeLayout->setColumnStretch(1, 4);
    manualModeLayout->setRowStretch(0, 0);
    manualModeLayout->setRowStretch(1, 1);
    m_manualJoystickPanel->setParent(m_manualControlBody);
    m_manualJoystickPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_manualJoystickPanel->setMinimumSize(230, 230);
    QFrame *manualActionGroupPanel = new QFrame(m_manualControlBody);
    manualActionGroupPanel->setObjectName("manualActionGroupPanel");
    manualActionGroupPanel->setAttribute(Qt::WA_StyledBackground, true);
    manualActionGroupPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout *manualActionGroupLayout = new QVBoxLayout(manualActionGroupPanel);
    manualActionGroupLayout->setContentsMargins(14, 14, 14, 14);
    manualActionGroupLayout->setSpacing(12);
    m_manualActionPanel->setParent(manualActionGroupPanel);
    m_manualActionPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    manualActionGroupLayout->addWidget(m_manualModeToggleButton);
    manualActionGroupLayout->addWidget(m_manualActionPanel, 1);
    manualModeLayout->addWidget(manualActionGroupPanel, 0, 0, 2, 1);
    manualModeLayout->addWidget(m_manualJoystickPanel, 0, 1, 2, 1);

    m_autoControlPage = new QWidget(controlBody);
    QVBoxLayout *autoControlLayout = new QVBoxLayout(m_autoControlPage);
    autoControlLayout->setSizeConstraint(QLayout::SetNoConstraint);
    autoControlLayout->setContentsMargins(0, 0, 0, 0);
    autoControlLayout->setSpacing(10);
    m_robotSelectRow->setParent(m_autoControlPage);
    m_autoCommandBody->setParent(m_autoControlPage);
    autoControlLayout->addWidget(m_robotSelectRow);
    autoControlLayout->addWidget(m_autoCommandBody, 1);

    m_controlModeStack = new QStackedWidget(controlBody);
    m_controlModeStack->setObjectName("controlModeStack");
    m_controlModeStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_controlModeStack->addWidget(m_autoControlPage);
    m_controlModeStack->addWidget(m_manualControlBody);
    control->addWidget(m_controlModeStack, 1);
    updateCommandButtonOrder();

    QFrame *controlPanel = new QFrame;
    controlPanel->setObjectName("panel");
    QVBoxLayout *controlPanelLayout = new QVBoxLayout(controlPanel);
    controlPanelLayout->setContentsMargins(14, 12, 14, 14);
    controlPanelLayout->setSpacing(8);
    QHBoxLayout *controlHeader = new QHBoxLayout;
    controlHeader->setContentsMargins(0, 0, 0, 0);
    controlHeader->setSpacing(10);
    QLabel *controlTitle = new QLabel("운용 제어");
    controlTitle->setObjectName("panelTitle");
    controlHeader->addWidget(controlTitle);
    controlHeader->addSpacing(18);
    m_controlRobotCaption = new QLabel("현재 제어 로봇");
    m_controlRobotCaption->setObjectName("controlRobotCaption");
    controlHeader->addWidget(m_controlRobotCaption);
    m_controlRobotBadge = new QLabel(robotName(qMax(0, m_selectedRobot)));
    m_controlRobotBadge->setObjectName("controlRobotBadge");
    m_controlRobotBadge->setAlignment(Qt::AlignCenter);
    controlHeader->addWidget(m_controlRobotBadge);
    m_controlRobotCaption->hide();
    m_controlRobotBadge->hide();
    controlHeader->addStretch(1);
    controlPanelLayout->addLayout(controlHeader);
    controlPanelLayout->addWidget(controlBody, 1);
    lower->addWidget(controlPanel, 5);

    dashboard->addLayout(lower, 3);
    m_contentStack->addWidget(dashboardPage);

    QWidget *robotPage = new QWidget;
    robotPage->setObjectName("robotOverviewPage");
    QVBoxLayout *robotPageLayout = new QVBoxLayout(robotPage);
    robotPageLayout->setContentsMargins(0, 0, 0, 0);
    robotPageLayout->setSpacing(10);

    QLabel *robotTitle = new QLabel("SPOT 로봇 상태");
    robotTitle->setObjectName("robotPageTitle");
    robotPageLayout->addWidget(robotTitle);

    QWidget *robotGridBody = new QWidget;
    m_robotCardGrid = new QGridLayout(robotGridBody);
    m_robotCardGrid->setContentsMargins(0, 0, 0, 0);
    m_robotCardGrid->setSpacing(10);
    m_robotCardGrid->setColumnStretch(0, 1);
    m_robotCardGrid->setColumnStretch(1, 1);
    m_robotCardScroll = new QScrollArea;
    m_robotCardScroll->setObjectName("panelScrollArea");
    m_robotCardScroll->setWidgetResizable(true);
    m_robotCardScroll->setFrameShape(QFrame::NoFrame);
    m_robotCardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_robotCardScroll->setWidget(robotGridBody);
    syncRobotStatusCards(m_robotCount);
    robotPageLayout->addWidget(m_robotCardScroll, 1);

    QFrame *robotFooter = new QFrame;
    robotFooter->setObjectName("robotFooter");
    QHBoxLayout *footerLayout = new QHBoxLayout(robotFooter);
    footerLayout->setContentsMargins(18, 8, 18, 8);
    footerLayout->setSpacing(22);
    auto addLegendItem = [footerLayout](const QString &text, const QString &color) {
        QWidget *item = new QWidget;
        item->setObjectName("robotLegendItem");
        QHBoxLayout *itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(6);
        QFrame *dot = new QFrame;
        dot->setObjectName("robotLegendDot");
        dot->setFixedSize(9, 9);
        dot->setStyleSheet(QString("#robotLegendDot { background:%1; border:0; border-radius:4px; }").arg(color));
        QLabel *label = new QLabel(text);
        label->setObjectName("robotLegendLabel");
        itemLayout->addWidget(dot, 0, Qt::AlignVCenter);
        itemLayout->addWidget(label, 0, Qt::AlignVCenter);
        footerLayout->addWidget(item, 0, Qt::AlignVCenter);
    };
    addLegendItem("정상", "#20e878");
    addLegendItem("이동/작업", "#72b9ff");
    addLegendItem("주의", "#ffd447");
    addLegendItem("경고", "#ff5b57");
    footerLayout->addSpacing(14);
    m_robotTotalLabel = new QLabel(QString("총 로봇: %1").arg(m_robotCount));
    footerLayout->addWidget(m_robotTotalLabel);
    footerLayout->addStretch();
    footerLayout->addWidget(new QLabel("마지막 업데이트: 실시간"));
    footerLayout->addWidget(new QLabel("자동 갱신  ●"));
    robotPageLayout->addWidget(robotFooter);
    m_contentStack->addWidget(robotPage);

    QWidget *dronePage = new QWidget;
    dronePage->setObjectName("dronePage");
    QVBoxLayout *droneLayout = new QVBoxLayout(dronePage);
    droneLayout->setContentsMargins(0, 0, 0, 0);
    droneLayout->setSpacing(12);

    QHBoxLayout *droneUpper = new QHBoxLayout;
    droneUpper->setSpacing(12);
    QWidget *droneCameraBody = new QWidget;
    QGridLayout *droneCameraLayout = new QGridLayout(droneCameraBody);
    droneCameraLayout->setContentsMargins(0, 0, 0, 0);
    droneCameraLayout->setSpacing(0);
    DroneCameraWidget *droneCamera = new DroneCameraWidget;
    QPushButton *captureDroneImageButton = new QPushButton(QStringLiteral("이미지 캡쳐"));
    captureDroneImageButton->setObjectName("droneCaptureButton");
    captureDroneImageButton->setCursor(Qt::PointingHandCursor);
    captureDroneImageButton->setMinimumSize(112, 30);
    droneCameraLayout->addWidget(droneCamera, 0, 0);
    droneCameraLayout->addWidget(captureDroneImageButton, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    droneUpper->addWidget(makePanel("드론 카메라", droneCameraBody), 1);

    QWidget *droneMapBody = new QWidget;
    QVBoxLayout *droneMapLayout = new QVBoxLayout(droneMapBody);
    droneMapLayout->setContentsMargins(0, 0, 0, 0);
    droneMapLayout->setSpacing(10);
    DroneMapWidget *droneMap = new DroneMapWidget;
    droneMapLayout->addWidget(droneMap, 1);
    QPushButton *applyDroneToControl = new QPushButton("맵 업데이트");
    applyDroneToControl->setObjectName("droneApplyButton");
    applyDroneToControl->setCursor(Qt::PointingHandCursor);
    applyDroneToControl->setMinimumHeight(58);
    applyDroneToControl->setMinimumWidth(280);
    connect(applyDroneToControl, &QPushButton::clicked, this, [this]() {
        if (!publishDroneMapToControl()) {
            QMessageBox::warning(this,
                                 "맵 업데이트 실패",
                                 "관제탭에 표시할 YAML 맵을 찾거나 읽지 못했습니다.");
            return;
        }
        showPage(0);
    });
    droneUpper->addWidget(makePanel("맵 생성", droneMapBody), 1);
    droneLayout->addLayout(droneUpper, 5);

    QHBoxLayout *droneLower = new QHBoxLayout;
    droneLower->setSpacing(12);

    QWidget *droneStatusBody = new QWidget;
    QHBoxLayout *droneStatusLayout = new QHBoxLayout(droneStatusBody);
    droneStatusLayout->setContentsMargins(18, 16, 18, 16);
    droneStatusLayout->setSpacing(22);
    auto makeDroneStatusMetric = [](const QString &iconKind, const QString &title,
                                    const QString &value, const QString &valueColor) {
        QWidget *metric = new QWidget;
        metric->setObjectName("droneMetric");
        metric->setAttribute(Qt::WA_StyledBackground, true);
        QHBoxLayout *metricLayout = new QHBoxLayout(metric);
        metricLayout->setContentsMargins(0, 0, 0, 0);
        metricLayout->setSpacing(16);
        DroneStatusIcon *icon = new DroneStatusIcon(iconKind, metric);
        QWidget *textColumn = new QWidget(metric);
        textColumn->setFixedHeight(58);
        QVBoxLayout *textLayout = new QVBoxLayout(textColumn);
        textLayout->setContentsMargins(0, 0, 0, 4);
        textLayout->setSpacing(10);
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setObjectName("droneMetricTitle");
        titleLabel->setFixedHeight(16);
        QLabel *valueLabel = new QLabel(value);
        valueLabel->setObjectName("droneMetricValue");
        valueLabel->setFixedHeight(28);
        valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        valueLabel->setStyleSheet(QString("color:%1").arg(valueColor));
        textLayout->addWidget(titleLabel);
        textLayout->addWidget(valueLabel);
        metricLayout->addWidget(icon, 0, Qt::AlignVCenter);
        metricLayout->addWidget(textColumn, 1, Qt::AlignVCenter);
        return metric;
    };
    auto addDroneStatusMetric = [droneStatusLayout, makeDroneStatusMetric](const QString &iconKind,
                                                                           const QString &title,
                                                                           const QString &value,
                                                                           const QString &valueColor,
                                                                           bool addSeparator = true) {
        droneStatusLayout->addWidget(makeDroneStatusMetric(iconKind, title, value, valueColor), 1);
        if (addSeparator) {
            QFrame *separator = new QFrame;
            separator->setObjectName("droneMetricSeparator");
            separator->setFrameShape(QFrame::VLine);
            separator->setFixedWidth(1);
            droneStatusLayout->addWidget(separator);
        }
    };
    addDroneStatusMetric("battery", "배터리", "89%", "#35e878");
    addDroneStatusMetric("altitude", "고도", "12 m", "#ffffff");
    addDroneStatusMetric("speed", "속도", "0.1 m/s", "#ffffff");
    addDroneStatusMetric("signal", "연결 상태", "정상", "#35e878", false);
    droneLower->addWidget(makePanel("드론 상태", droneStatusBody), 3);

    QListWidget *droneEventList = new QListWidget;
    droneEventList->setObjectName("droneEventList");
    for (const QString &row : {
             QStringLiteral("15:03:39    드론 이륙"),
             QStringLiteral("15:03:31    경로 계획 완료"),
             QStringLiteral("15:03:22    카메라 스트리밍 시작")}) {
        QListWidgetItem *item = new QListWidgetItem(row);
        item->setForeground(QColor("#d6e1ef"));
        droneEventList->addItem(item);
    }
    droneLower->addWidget(makePanel("이벤트 로그", droneEventList), 2);
    QWidget *dronePlannerBody = new QWidget;
    dronePlannerBody->setObjectName("dronePlannerBody");
    QVBoxLayout *dronePlannerLayout = new QVBoxLayout(dronePlannerBody);
    dronePlannerLayout->setContentsMargins(14, 12, 14, 12);
    dronePlannerLayout->setSpacing(10);

    QLabel *dronePlannerState = new QLabel(droneMap->statusText());
    dronePlannerState->setObjectName("dronePlannerState");
    QLabel *dronePlannerDetail = new QLabel(droneMap->detailText());
    dronePlannerDetail->setObjectName("dronePlannerDetail");
    dronePlannerDetail->setWordWrap(true);
    dronePlannerLayout->addWidget(dronePlannerState);
    dronePlannerLayout->addWidget(dronePlannerDetail);

    QGridLayout *droneToolGrid = new QGridLayout;
    droneToolGrid->setContentsMargins(0, 0, 0, 0);
    droneToolGrid->setHorizontalSpacing(8);
    droneToolGrid->setVerticalSpacing(8);
    auto makeDroneToolButton = [](const QString &text) {
        QPushButton *button = new QPushButton(text);
        button->setObjectName("droneToolButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(34);
        return button;
    };
    QPushButton *autoBuildDroneMapButton = makeDroneToolButton(QStringLiteral("자동 맵 생성"));
    autoBuildDroneMapButton->setObjectName("droneAutoBuildButton");
    autoBuildDroneMapButton->setMinimumHeight(42);
    QPushButton *originToolButton = makeDroneToolButton(QStringLiteral("원점"));
    QPushButton *xAxisToolButton = makeDroneToolButton(QStringLiteral("+X"));
    QPushButton *obstacleToolButton = makeDroneToolButton(QStringLiteral("장애물"));
    QPushButton *undoDroneMapButton = makeDroneToolButton(QStringLiteral("되돌리기"));
    QPushButton *clearDroneMapButton = makeDroneToolButton(QStringLiteral("초기화"));
    QPushButton *previewDroneMapButton = makeDroneToolButton(QStringLiteral("미리보기"));
    dronePlannerLayout->addWidget(autoBuildDroneMapButton);
    droneToolGrid->addWidget(originToolButton, 0, 0);
    droneToolGrid->addWidget(xAxisToolButton, 0, 1);
    droneToolGrid->addWidget(obstacleToolButton, 0, 2);
    droneToolGrid->addWidget(undoDroneMapButton, 1, 0);
    droneToolGrid->addWidget(clearDroneMapButton, 1, 1);
    droneToolGrid->addWidget(previewDroneMapButton, 1, 2);
    dronePlannerLayout->addLayout(droneToolGrid);

    applyDroneToControl->setMinimumHeight(44);
    applyDroneToControl->setMinimumWidth(0);
    applyDroneToControl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    dronePlannerLayout->addWidget(applyDroneToControl);

    auto refreshDronePlannerUi = [droneMap,
                                  dronePlannerState,
                                  dronePlannerDetail,
                                  originToolButton,
                                  xAxisToolButton,
                                  obstacleToolButton,
                                  previewDroneMapButton,
                                  undoDroneMapButton,
                                  clearDroneMapButton,
                                  applyDroneToControl]() {
        dronePlannerState->setText(droneMap->statusText());
        dronePlannerDetail->setText(droneMap->detailText());
        originToolButton->setProperty("active", droneMap->tool() == DroneMapWidget::OriginTool);
        xAxisToolButton->setProperty("active", droneMap->tool() == DroneMapWidget::XAxisTool);
        obstacleToolButton->setProperty("active", droneMap->tool() == DroneMapWidget::ObstacleTool
                                                   && !droneMap->previewMode());
        originToolButton->setEnabled(droneMap->hasCapturedImage());
        xAxisToolButton->setEnabled(droneMap->hasCapturedImage());
        obstacleToolButton->setEnabled(droneMap->hasCapturedImage());
        undoDroneMapButton->setEnabled(droneMap->hasCapturedImage());
        clearDroneMapButton->setEnabled(droneMap->hasCapturedImage());
        previewDroneMapButton->setEnabled(droneMap->hasCapturedImage() && droneMap->axesReady());
        applyDroneToControl->setEnabled(droneMap->previewMode());
        for (QPushButton *button : {originToolButton, xAxisToolButton, obstacleToolButton}) {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    };
    connect(originToolButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->setTool(DroneMapWidget::OriginTool);
        refreshDronePlannerUi();
    });
    connect(xAxisToolButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->setTool(DroneMapWidget::XAxisTool);
        refreshDronePlannerUi();
    });
    connect(obstacleToolButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->setTool(DroneMapWidget::ObstacleTool);
        refreshDronePlannerUi();
    });
    connect(undoDroneMapButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->undo();
        refreshDronePlannerUi();
    });
    connect(clearDroneMapButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->clearAll();
        refreshDronePlannerUi();
    });
    connect(previewDroneMapButton, &QPushButton::clicked, this, [droneMap, refreshDronePlannerUi]() {
        droneMap->generatePreview();
        refreshDronePlannerUi();
    });
    connect(autoBuildDroneMapButton, &QPushButton::clicked, this,
            [droneMap, droneEventList, refreshDronePlannerUi]() {
        if (!droneMap->generateAutoPreviewFromYaml(findMapYaml())) {
            droneMap->generateAutoPreview();
        }
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1    자동 맵 미리보기 생성 완료")
                .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        item->setForeground(QColor("#d6e1ef"));
        droneEventList->insertItem(0, item);
        refreshDronePlannerUi();
    });
    droneMap->setChangedCallback(refreshDronePlannerUi);
    connect(captureDroneImageButton, &QPushButton::clicked, this,
            [droneMap, droneEventList, refreshDronePlannerUi]() {
        droneMap->loadCapturedImage();
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1    이미지 캡쳐 완료")
                .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        item->setForeground(QColor("#d6e1ef"));
        droneEventList->insertItem(0, item);
        refreshDronePlannerUi();
    });
    refreshDronePlannerUi();

    droneLower->addWidget(makePanel("맵 빌더", dronePlannerBody), 4);
    droneLayout->addLayout(droneLower, 2);
    m_contentStack->addWidget(dronePage);

    QWidget *logPage = new QWidget;
    logPage->setObjectName("logPage");
    QVBoxLayout *logLayout = new QVBoxLayout(logPage);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(12);

    QHBoxLayout *logHeader = new QHBoxLayout;
    logHeader->setSpacing(12);
    QVBoxLayout *logTitleBlock = new QVBoxLayout;
    logTitleBlock->setSpacing(0);
    QLabel *logTitle = new QLabel("시스템 로그");
    logTitle->setObjectName("logHeroTitle");
    logTitle->setAlignment(Qt::AlignCenter);
    logTitleBlock->addStretch();
    logTitleBlock->addWidget(logTitle);
    logTitleBlock->addStretch();
    logHeader->addLayout(logTitleBlock, 1);

    auto makeLogMetric = [](const QString &title, const QString &value, const QString &delta,
                            const QString &accent, QLabel **valueOut = nullptr, QWidget *extra = nullptr) {
        QFrame *card = new QFrame;
        card->setObjectName("logMetricCard");
        QVBoxLayout *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(5);
        QLabel *titleLabel = new QLabel(title);
        titleLabel->setObjectName("logMetricTitle");
        QLabel *valueLabel = new QLabel(value);
        valueLabel->setObjectName("logMetricValue");
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (valueOut) {
            *valueOut = valueLabel;
        }
        QLabel *deltaLabel = new QLabel(delta);
        deltaLabel->setObjectName("logMetricDelta");
        deltaLabel->setStyleSheet(QString("color:%1").arg(accent));
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(valueLabel);
        if (extra) {
            cardLayout->addWidget(extra);
        }
        cardLayout->addStretch();
        cardLayout->addWidget(deltaLabel);
        return card;
    };

    logHeader->addWidget(makeLogMetric("전체 로그", "0", "실시간 집계", "#58df78", &m_logTotalValue, new LogTrendWidget), 1);
    logHeader->addWidget(makeLogMetric("활성 경보", "0", "주의 + 위험 로그", "#f6bd32", &m_logActiveValue), 1);
    logHeader->addWidget(makeLogMetric("위험 이벤트", "0", "위험 로그", "#ff453a", &m_logCriticalValue), 1);

    QFrame *severityCard = new QFrame;
    severityCard->setObjectName("logMetricCard");
    QVBoxLayout *severityLayout = new QVBoxLayout(severityCard);
    severityLayout->setContentsMargins(12, 10, 12, 10);
    severityLayout->setSpacing(6);
    QLabel *severityTitle = new QLabel("심각도별 로그");
    severityTitle->setObjectName("logMetricTitle");
    QHBoxLayout *severityBody = new QHBoxLayout;
    severityBody->setContentsMargins(0, 0, 0, 0);
    severityBody->setSpacing(10);
    severityBody->addStretch(1);
    m_logSeverityChart = new SeverityDonutWidget;
    severityBody->addWidget(m_logSeverityChart);
    m_logSeverityRows = new QLabel;
    m_logSeverityRows->setObjectName("logSeverityRows");
    m_logSeverityRows->setTextFormat(Qt::RichText);
    severityBody->addWidget(m_logSeverityRows);
    severityBody->addStretch(1);
    severityLayout->addWidget(severityTitle);
    severityLayout->addLayout(severityBody, 1);
    logHeader->addWidget(severityCard, 1);
    logLayout->addLayout(logHeader);

    QHBoxLayout *logToolbar = new QHBoxLayout;
    logToolbar->setSpacing(10);
    m_logSearch = new QLineEdit;
    m_logSearch->setObjectName("logSearch");
    m_logSearch->setPlaceholderText("로그 검색...");
    const QDate logEndDefault = QDate::currentDate();
    const QDate logStartDefault = qMin(QDate(2025, 5, 18), logEndDefault);
    m_logStartDate = new QDateEdit(logStartDefault);
    m_logStartDate->setObjectName("logDateEdit");
    m_logStartDate->setCalendarPopup(true);
    m_logStartDate->setDisplayFormat("yyyy-MM-dd");
    m_logEndDate = new QDateEdit(logEndDefault);
    m_logEndDate->setObjectName("logDateEdit");
    m_logEndDate->setCalendarPopup(true);
    m_logEndDate->setDisplayFormat("yyyy-MM-dd");
    QLabel *logStartLabel = new QLabel("시작");
    logStartLabel->setObjectName("logDateLabel");
    QLabel *logEndLabel = new QLabel("종료");
    logEndLabel->setObjectName("logDateLabel");
    QPushButton *filterAll = new QPushButton("전체");
    filterAll->setObjectName("logFilterActive");
    filterAll->setProperty("severityCode", QString());
    QPushButton *filterInfo = new QPushButton("정보");
    filterInfo->setObjectName("logFilterButton");
    filterInfo->setProperty("severityCode", "INFO");
    QPushButton *filterWarn = new QPushButton("주의");
    filterWarn->setObjectName("logFilterButton");
    filterWarn->setProperty("severityCode", "WARN");
    QPushButton *filterCritical = new QPushButton("위험");
    filterCritical->setObjectName("logFilterButton");
    filterCritical->setProperty("severityCode", "CRITICAL");
    m_logFilterButtons = {filterAll, filterInfo, filterWarn, filterCritical};
    connect(m_logSearch, &QLineEdit::textChanged, this, [this]() {
        m_logCurrentPage = 0;
        applyLogFilters();
    });
    connect(m_logStartDate, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        if (m_logEndDate && date > m_logEndDate->date()) {
            m_logEndDate->setDate(date);
        }
        m_logCurrentPage = 0;
        applyLogFilters();
    });
    connect(m_logEndDate, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        if (m_logStartDate && date < m_logStartDate->date()) {
            m_logStartDate->setDate(date);
        }
        m_logCurrentPage = 0;
        applyLogFilters();
    });
    for (QPushButton *button : m_logFilterButtons) {
        connect(button, &QPushButton::clicked, this, [this, button]() {
            setLogSeverityFilter(button->property("severityCode").toString());
        });
    }
    logToolbar->addWidget(m_logSearch, 3);
    logToolbar->addWidget(logStartLabel);
    logToolbar->addWidget(m_logStartDate, 1);
    logToolbar->addWidget(logEndLabel);
    logToolbar->addWidget(m_logEndDate, 1);
    logToolbar->addStretch();
    logToolbar->addWidget(filterAll);
    logToolbar->addWidget(filterInfo);
    logToolbar->addWidget(filterWarn);
    logToolbar->addWidget(filterCritical);
    logLayout->addLayout(logToolbar);

    m_logTable = new QTableWidget;
    m_logTable->setObjectName("logTable");
    m_logTable->setColumnCount(6);
    m_logTable->setHorizontalHeaderLabels({"", "심각도", "시간", "출처", "분류", "메시지"});
    m_logTable->verticalHeader()->hide();
    m_logTable->setShowGrid(false);
    m_logTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setFocusPolicy(Qt::NoFocus);
    m_logTable->setAlternatingRowColors(false);
    m_logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_logTable->horizontalHeader()->resizeSection(0, 42);
    m_logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_logTable->horizontalHeader()->resizeSection(1, 126);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_logTable->horizontalHeader()->resizeSection(2, 220);
    m_logTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_logTable->horizontalHeader()->resizeSection(3, 140);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_logTable->horizontalHeader()->resizeSection(4, 150);
    m_logTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setDefaultSectionSize(48);

    appendLogTableRow("INFO", "2025-05-25 14:32:18.452", "GNSS-01", "항법", "GPS 고정 완료 (위성 12개)");
    appendLogTableRow("INFO", "2025-05-25 14:32:17.991", "NET-01", "연결", "LTE 재연결 성공 (RSSI -78 dBm)");
    appendLogTableRow("WARN", "2025-05-25 14:31:52.311", "COLL-01", "안전", "충돌 경고: 2.1 m 전방 물체 감지");
    appendLogTableRow("CRITICAL", "2025-05-25 14:31:51.884", "SRS-01", "안전", "운용자 긴급 정지 명령 수신");
    appendLogTableRow("WARN", "2025-05-25 14:31:45.203", "PWR-01", "전원", "배터리 전압 낮음: 11.8 V");
    appendLogTableRow("WARN", "2025-05-25 14:31:32.774", "DRIVE-01", "구동계", "좌측 후륜 미끄러짐 감지");
    appendLogTableRow("INFO", "2025-05-25 14:31:15.661", "ODOM-01", "위치추정", "오도메트리 보정 적용");
    appendLogTableRow("INFO", "2025-05-25 14:30:44.719", "SENS-01", "인지", "센서 융합 상태 정상");
    appendLogTableRow("INFO", "2025-05-25 14:30:33.508", "LOG-01", "시스템", "로그 업로드 완료 (2.1 MB)");
    appendLogTableRow("INFO", "2025-05-25 14:30:12.247", "DIAG-01", "진단", "시스템 진단 통과");
    updateLogSummary();
    logLayout->addWidget(m_logTable, 1);

    QFrame *logFooter = new QFrame;
    logFooter->setObjectName("logFooter");
    QHBoxLayout *logFooterLayout = new QHBoxLayout(logFooter);
    logFooterLayout->setContentsMargins(14, 6, 14, 6);
    logFooterLayout->addWidget(new QLabel("페이지당 행 수: 25"));
    logFooterLayout->addStretch();
    m_logFooterRange = new QLabel;
    logFooterLayout->addWidget(m_logFooterRange);
    logFooterLayout->addStretch();
    m_logFirstPageButton = new QPushButton("|<");
    m_logPrevPageButton = new QPushButton("<");
    m_logNextPageButton = new QPushButton(">");
    m_logLastPageButton = new QPushButton(">|");
    for (QPushButton *button : {m_logFirstPageButton, m_logPrevPageButton, m_logNextPageButton, m_logLastPageButton}) {
        button->setObjectName("logPageButton");
        button->setMinimumWidth(34);
    }
    connect(m_logFirstPageButton, &QPushButton::clicked, this, [this]() { setLogPage(0); });
    connect(m_logPrevPageButton, &QPushButton::clicked, this, [this]() { setLogPage(m_logCurrentPage - 1); });
    connect(m_logNextPageButton, &QPushButton::clicked, this, [this]() { setLogPage(m_logCurrentPage + 1); });
    connect(m_logLastPageButton, &QPushButton::clicked, this, [this]() {
        int filteredCount = 0;
        for (int row = 0; row < m_logTable->rowCount(); ++row) {
            if (logRowMatchesFilters(row)) {
                ++filteredCount;
            }
        }
        setLogPage(qMax(0, (filteredCount - 1) / m_logRowsPerPage));
    });
    logFooterLayout->addWidget(m_logFirstPageButton);
    logFooterLayout->addWidget(m_logPrevPageButton);
    for (int i = 0; i < 5; ++i) {
        QPushButton *pageButton = new QPushButton(QString::number(i + 1));
        pageButton->setObjectName(i == 0 ? "logPageActive" : "logPageButton");
        pageButton->setMinimumWidth(34);
        pageButton->setProperty("pageIndex", i);
        m_logPageButtons.append(pageButton);
        connect(pageButton, &QPushButton::clicked, this, [this, pageButton]() {
            setLogPage(pageButton->property("pageIndex").toInt());
        });
        logFooterLayout->addWidget(pageButton);
    }
    logFooterLayout->addWidget(m_logNextPageButton);
    logFooterLayout->addWidget(m_logLastPageButton);
    logLayout->addWidget(logFooter);
    updateLogSummary();
    m_contentStack->addWidget(logPage);

    QWidget *settingsPage = new QWidget;
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsPage);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *settingsBody = new QWidget;
    QFormLayout *form = new QFormLayout(settingsBody);
    form->setContentsMargins(8, 4, 8, 4);
    QSpinBox *maxRobots = new QSpinBox;
    maxRobots->setRange(0, kMaxRobots);
    maxRobots->setValue(m_robotCount);
    QLineEdit *cmdIpc = new QLineEdit("SHM cmd_queue (/robot_bridge_N)");
    cmdIpc->setReadOnly(true);
    QLineEdit *shmPattern = new QLineEdit("/robot_bridge_%d");
    shmPattern->setReadOnly(true);
    QPushButton *apply = new QPushButton("공유메모리 재스캔");
    connect(apply, &QPushButton::clicked, this, [this, maxRobots] {
        setRobotCount(maxRobots->value());
        showPage(0);
    });
    form->addRow("감시 로봇 수", maxRobots);
    form->addRow("명령 IPC", cmdIpc);
    form->addRow("SHM 이름", shmPattern);
    form->addRow("", apply);
    settingsLayout->addWidget(makePanel("설정", settingsBody));
    m_contentStack->addWidget(settingsPage);

    m_mapFullscreenPage = new QWidget;
    QVBoxLayout *mapFullscreenLayout = new QVBoxLayout(m_mapFullscreenPage);
    mapFullscreenLayout->setContentsMargins(0, 0, 0, 0);
    mapFullscreenLayout->setSpacing(10);
    QHBoxLayout *mapFullscreenHeader = new QHBoxLayout;
    QLabel *mapFullscreenTitle = new QLabel("탐색 지도");
    mapFullscreenTitle->setObjectName("panelTitle");
    m_fullscreenBtn2d = new QPushButton("2D");
    m_fullscreenBtn2d->setObjectName("smallActive");
    m_fullscreenBtn3d = new QPushButton("3D");
    m_fullscreenBtn3d->setObjectName("smallButton");
    QPushButton *mapBackButton = new QPushButton("뒤로가기");
    mapBackButton->setObjectName("backButton");
    mapBackButton->setMinimumWidth(104);
    connect(mapBackButton, &QPushButton::clicked, this, &MainWindow::leaveMapFullscreen);
    mapFullscreenHeader->addWidget(mapFullscreenTitle);
    mapFullscreenHeader->addStretch();
    mapFullscreenHeader->addWidget(m_fullscreenBtn2d);
    mapFullscreenHeader->addWidget(m_fullscreenBtn3d);
    mapFullscreenHeader->addSpacing(8);
    mapFullscreenHeader->addWidget(mapBackButton);
    mapFullscreenLayout->addLayout(mapFullscreenHeader);
    m_mapFullscreenContentLayout = new QVBoxLayout;
    m_mapFullscreenContentLayout->setContentsMargins(0, 0, 0, 0);
    m_mapFullscreenContentLayout->setSpacing(0);
    mapFullscreenLayout->addLayout(m_mapFullscreenContentLayout, 1);
    m_contentStack->addWidget(m_mapFullscreenPage);

    m_cameraPage = new QWidget;
    QVBoxLayout *cameraPageLayout = new QVBoxLayout(m_cameraPage);
    cameraPageLayout->setContentsMargins(0, 0, 0, 0);
    cameraPageLayout->setSpacing(10);
    QHBoxLayout *cameraHeader = new QHBoxLayout;
    QPushButton *backButton = new QPushButton("뒤로가기");
    backButton->setObjectName("backButton");
    backButton->setMinimumWidth(104);
    connect(backButton, &QPushButton::clicked, this, &MainWindow::leaveCameraFullscreen);
    m_cameraTitle = new QLabel("카메라 스트리밍");
    m_cameraTitle->setObjectName("panelTitle");
    cameraHeader->addWidget(m_cameraTitle);
    cameraHeader->addStretch();
    cameraHeader->addWidget(backButton);
    cameraPageLayout->addLayout(cameraHeader);
    m_expandedVideoTile = new VideoTile(0);
    m_expandedVideoTile->setCursor(Qt::ArrowCursor);
    cameraPageLayout->addWidget(m_expandedVideoTile, 1);
    m_contentStack->addWidget(m_cameraPage);

    connect(navExplore, &QPushButton::clicked, this, [this] { showPage(0); });
    connect(navRobots, &QPushButton::clicked, this, [this] { showPage(1); });
    connect(navDrone, &QPushButton::clicked, this, [this] { showPage(2); });
    connect(navLogs, &QPushButton::clicked, this, [this] { showPage(3); });
    connect(navSettings, &QPushButton::clicked, this, [this] { showPage(4); });
    connect(m_btn2d, &QPushButton::clicked, this, &MainWindow::showMap2D);
    connect(m_btn3d, &QPushButton::clicked, this, &MainWindow::showMap3D);
    connect(m_fullscreenBtn2d, &QPushButton::clicked, this, &MainWindow::showMap2D);
    connect(m_fullscreenBtn3d, &QPushButton::clicked, this, &MainWindow::showMap3D);
    connect(mapMaximize, &QPushButton::clicked, this, &MainWindow::showMapFullscreen);
    QShortcut *victimTestShortcut = new QShortcut(QKeySequence("Ctrl+Shift+V"), this);
    connect(victimTestShortcut, &QShortcut::activated, this, [this] {
        appendEvent(UiEvent{m_selectedRobot,
                            4,
                            kEventTypeVictimDetected,
                            static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL,
                            "생존자 탐지 테스트"});
    });
    refreshRobotSelector();
    selectRobot(kAllRobotsSelection);
}

void MainWindow::applyStyle()
{
    setStyleSheet(R"(
        QMainWindow, QWidget { background:#03080d; color:#dce7f3; }
        QWidget#robotEmptyState {
            background:transparent;
            border:0;
        }
        #nav {
            background:#07131d;
            border:1px solid #263c4e;
            border-radius:22px;
        }
        #navSeparator {
            background:rgba(210, 222, 235, 64);
            border:0;
            min-height:1px;
            max-height:1px;
        }
        #topBar, #panel, #metric {
            background:#060d14;
            border:1px solid #1f3544;
            border-radius:8px;
        }
        #title { font-size:26px; font-weight:800; color:#ffffff; }
        #accentText { font-size:18px; font-weight:700; color:#ffd21a; }
        #systemText { font-size:15px; font-weight:700; color:#00e66b; }
        #clockText { font-size:18px; color:#c7d0db; }
        #panelTitle { font-size:20px; font-weight:800; color:#ffffff; background:transparent; border:0; }
        #controlRobotCaption {
            color:#aab7c5;
            background:transparent;
            border:0;
            font-size:15px;
            font-weight:800;
        }
        #controlRobotBadge {
            min-width:116px;
            min-height:34px;
            padding:2px 14px;
            color:#ffd21a;
            background:#071017;
            border:1px solid #bb9715;
            border-radius:8px;
            font-size:20px;
            font-weight:900;
        }
        QScrollArea#panelScrollArea {
            background:transparent;
            border:0;
        }
        QScrollArea#panelScrollArea > QWidget > QWidget {
            background:transparent;
        }
        QScrollBar:vertical {
            background:#06111a;
            width:8px;
            margin:0;
            border:0;
        }
        QScrollBar::handle:vertical {
            background:#2f4b5e;
            min-height:24px;
            border-radius:4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height:0;
            border:0;
        }
        #metric { padding:10px 14px; font-size:15px; line-height:1.35; }
        #metric b { font-size:24px; }
        #headerMetric {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #071017, stop:0.58 #06121b, stop:1 #02070c);
            border:1px solid #0089c9;
            border-radius:7px;
        }
        #headerMetricTitle {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:21px;
            font-weight:900;
        }
        #headerMetricValue {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:38px;
            font-weight:900;
        }
        #headerProgressValue {
            color:#18d878;
            background:transparent;
            border:0;
            font-size:28px;
            font-weight:900;
        }
        QProgressBar#headerProgressBar {
            background:#26333a;
            border:0;
            border-radius:7px;
            min-height:14px;
            max-height:14px;
            text-align:center;
            color:transparent;
        }
        QProgressBar#headerProgressBar::chunk {
            background:#18d878;
            border-radius:7px;
        }
        #videoTile, #statusRow {
            background:#0b141c;
            border:1px solid #263947;
            border-radius:7px;
        }
        #statusHeader {
            background:#121d27;
            border:0;
            border-radius:5px;
            min-height:28px;
            max-height:28px;
        }
        #statusHeaderLabel {
            background:transparent;
            border:0;
            color:#aeb8c8;
            font-size:12px;
            font-weight:800;
        }
        #statusRow {
            background:#071017;
            border:0;
            border-bottom:1px solid #1d3140;
            border-radius:0;
        }
        #statusRobotName {
            background:transparent;
            border:0;
            color:#dce7f3;
            font-size:13px;
            font-weight:800;
        }
        #statusRobotName[state="offline"] { color:#76828e; }
        #statusRobotName[state="warning"] { color:#ffd166; }
        #statusRobotName[state="danger"] { color:#ff736b; }
        #statusBatteryCell {
            background:transparent;
            border:0;
        }
        #statusBatteryText {
            background:transparent;
            border:0;
            color:#16d968;
            font-size:12px;
            font-weight:900;
        }
        #statusBatteryText[state="offline"] { color:#6f7a84; }
        #statusBatteryText[state="warning"] { color:#ffd166; }
        #statusBatteryText[state="danger"] { color:#ff736b; }
        QProgressBar#statusBatteryBar {
            background:#22333d;
            border:0;
            border-radius:5px;
            min-height:10px;
            max-height:10px;
        }
        QProgressBar#statusBatteryBar::chunk {
            background:#16d968;
            border-radius:5px;
        }
        QProgressBar#statusBatteryBar[state="moving"]::chunk { background:#16d968; }
        QProgressBar#statusBatteryBar[state="warning"]::chunk { background:#ffd166; }
        QProgressBar#statusBatteryBar[state="danger"]::chunk { background:#ff736b; }
        QProgressBar#statusBatteryBar[state="offline"]::chunk { background:#55616b; }
        #statusMissionChip {
            background:#10261a;
            border:1px solid #1f7b42;
            border-radius:5px;
            color:#15e36d;
            font-size:12px;
            font-weight:900;
            padding:2px 8px;
        }
        #statusMissionChip[state="moving"] {
            background:#10261a;
            border-color:#1f7b42;
            color:#15e36d;
        }
        #statusMissionChip[state="danger"] {
            background:#321515;
            border-color:#b24a44;
            color:#ff938c;
        }
        #statusMissionChip[state="warning"] {
            background:#2b2410;
            border-color:#c9952d;
            color:#ffd166;
        }
        #statusMissionChip[state="offline"] {
            background:#101820;
            border-color:#384956;
            color:#aeb8c8;
        }
        #videoImage { background:#020507; color:#61707f; font-size:18px; font-weight:800; border-radius:7px; }
        #videoTitle { color:#ffffff; font-size:17px; font-weight:800; background:rgba(0,0,0,130); padding:2px 6px; }
        #videoBadge { color:#00e66b; font-size:11px; font-weight:800; background:rgba(0,0,0,150); padding:2px 6px; }
        #videoBadge[state="wait"] { color:#9aa7b2; }
        #videoBadge[state="nosignal"] { color:#ff453a; }
        #victimAlert {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(220, 28, 42, 168),
                                        stop:0.18 rgba(182, 8, 26, 162),
                                        stop:1 rgba(112, 0, 16, 168));
            border:3px solid #ff5a52;
            border-radius:10px;
            color:#ffffff;
            font-weight:900;
            padding:0;
        }
        #victimAlert:hover {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 rgba(238, 44, 58, 186),
                                        stop:0.18 rgba(198, 15, 32, 180),
                                        stop:1 rgba(128, 0, 18, 186));
            border-color:#ffe2de;
        }
        #victimAlertTitle {
            background:transparent;
            border:0;
            color:#fff7f5;
            font-size:78px;
            font-weight:900;
        }
        #victimAlertFooter {
            background:rgba(32,0,6,70);
            border:1px solid rgba(255,190,180,66);
            border-radius:7px;
            min-height:64px;
            max-height:64px;
        }
        #victimAlertSubtitle {
            background:transparent;
            border:0;
            color:#ffffff;
            font-size:28px;
            font-weight:900;
            padding:0;
        }
        QListWidget#eventList {
            background:#071017;
            border:0;
            outline:0;
            font-size:14px;
        }
        QListWidget#packetStatsList {
            background:#071017;
            border:0;
            outline:0;
            font-size:14px;
        }
        #dronePage { background:#03080d; }
        #droneCaptureButton {
            margin:0 16px 16px 0;
            padding:4px 12px;
            background:#101c25;
            border:1px solid #ffd21a;
            border-radius:6px;
            color:#ffd21a;
            font-size:13px;
            font-weight:900;
        }
        #droneCaptureButton:hover {
            background:#1b1805;
            border-color:#fff2a8;
            color:#fff2a8;
        }
        #droneCaptureButton:pressed {
            background:#ffd21a;
            color:#1b1500;
        }
        #droneApplyButton {
            min-width:220px;
            padding:8px 28px;
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #ffe16a, stop:0.52 #ffd21a, stop:1 #d99b05);
            border:1px solid #ffe989;
            border-radius:7px;
            color:#1b1500;
            font-size:20px;
            font-weight:900;
        }
        #droneApplyButton:hover {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #fff2a8, stop:0.55 #ffdc36, stop:1 #e0aa13);
        }
        #droneApplyButton:disabled {
            background:#17232d;
            border-color:#314251;
            color:#70808f;
        }
        #dronePlannerBody {
            background:transparent;
            border:0;
        }
        #dronePlannerState {
            background:transparent;
            border:0;
            color:#ffffff;
            font-size:17px;
            font-weight:900;
        }
        #dronePlannerDetail {
            background:transparent;
            border:0;
            color:#aeb8c8;
            font-size:12px;
            font-weight:700;
        }
        #droneToolButton {
            background:#101c25;
            border:1px solid #284357;
            border-radius:6px;
            color:#dce7f3;
            font-size:14px;
            font-weight:800;
            padding:4px 8px;
        }
        #droneToolButton:hover {
            border-color:#ffd21a;
            color:#ffd21a;
        }
        #droneToolButton[active="true"] {
            background:#1b1805;
            border-color:#ffd21a;
            color:#ffd21a;
        }
        #droneToolButton:disabled {
            background:#081018;
            border-color:#1c2f3d;
            color:#546575;
        }
        #droneAutoBuildButton {
            background:#123125;
            border:1px solid #35e878;
            border-radius:6px;
            color:#dfffee;
            font-size:15px;
            font-weight:900;
            padding:6px 10px;
        }
        #droneAutoBuildButton:hover {
            background:#174532;
            border-color:#88ffb6;
            color:#ffffff;
        }
        #droneAutoBuildButton:pressed {
            background:#35e878;
            color:#04130b;
        }
        #droneMetric {
            background:transparent;
            border:0;
        }
        #droneMetricTitle {
            color:#aeb8c8;
            background:transparent;
            border:0;
            font-size:17px;
            font-weight:800;
        }
        #droneMetricValue {
            background:transparent;
            border:0;
            font-size:22px;
            font-weight:900;
        }
        #droneStatusIcon {
            background:transparent;
            border:0;
        }
        #droneMetricSeparator {
            background:#223548;
            border:0;
            min-width:1px;
            max-width:1px;
        }
        QListWidget#droneEventList {
            background:#071017;
            border:0;
            outline:0;
            font-size:16px;
            font-weight:700;
        }
        QListWidget#droneEventList::item {
            min-height:32px;
            padding:4px 8px;
        }
        #logPage { background:#03080d; }
        #logHeroTitle {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:28px;
            font-weight:900;
        }
        #logMetricCard {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #081321, stop:0.55 #0a1724, stop:1 #07101a);
            border:1px solid #2b4058;
            border-radius:7px;
        }
        #logMetricTitle {
            color:#aeb8c8;
            background:transparent;
            border:0;
            font-size:12px;
            font-weight:700;
        }
        #logMetricValue {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:24px;
            font-weight:900;
        }
        #logMetricDelta, #logSeverityRows {
            background:transparent;
            border:0;
            font-size:11px;
            font-weight:800;
        }
        QLineEdit#logSearch, #logToolbarButton {
            background:#071321;
            border:1px solid #2b4058;
            border-radius:7px;
            color:#dce7f3;
            min-height:38px;
            padding:4px 12px;
            font-size:13px;
            font-weight:700;
        }
        QLabel#logDateLabel {
            background:transparent;
            border:0;
            color:#94a4b8;
            font-size:12px;
            font-weight:800;
        }
        #logFilterButton, #logFilterActive, #logPageButton, #logPageActive {
            min-height:30px;
            min-width:34px;
            padding:3px 10px;
            border-radius:6px;
            font-size:12px;
            font-weight:800;
        }
        #logFilterButton, #logFilterActive { min-width:86px; }
        #logFilterActive, #logPageActive {
            background:#102f5f;
            border:1px solid #2279ff;
            color:#8ec1ff;
        }
        #logFilterButton, #logPageButton {
            background:#071321;
            border:1px solid #26384b;
            color:#c5cfdb;
        }
        #logPageButton:disabled {
            color:#556273;
            border-color:#1b2a3a;
        }
        QDateEdit#logDateEdit {
            background:#071321;
            border:1px solid #2b4058;
            border-radius:7px;
            color:#dce7f3;
            min-height:38px;
            padding:4px 10px;
            font-size:13px;
            font-weight:700;
        }
        QTableWidget#logTable {
            background:#06111c;
            alternate-background-color:#06111c;
            border:1px solid #2b4058;
            border-radius:8px;
            gridline-color:#1d3144;
            color:#dce7f3;
            font-size:14px;
            outline:0;
        }
        QTableWidget#logTable::item {
            border-bottom:1px solid #1b2d3f;
            padding:7px 8px;
        }
        QHeaderView::section {
            background:#081725;
            color:#b9c7d8;
            border:0;
            border-bottom:1px solid #26384b;
            padding:9px 8px;
            font-size:13px;
            font-weight:900;
        }
        #severityInfo, #severityWarn, #severityCritical {
            border-radius:5px;
            min-width:74px;
            max-width:74px;
            min-height:24px;
            max-height:24px;
            font-size:11px;
            font-weight:900;
        }
        #severityInfo {
            background:#10361e;
            border:1px solid #2a7d3b;
            color:#d9ffe4;
        }
        #severityWarn {
            background:#3c2a04;
            border:1px solid #b98000;
            color:#fff4c7;
        }
        #severityCritical {
            background:#3a1012;
            border:1px solid #c94444;
            color:#ffe3e3;
        }
        #logFooter {
            background:#071321;
            border:1px solid #2b4058;
            border-radius:7px;
            color:#aeb8c8;
            font-size:12px;
            font-weight:800;
        }
        QListWidget::item { padding:8px; border-bottom:1px solid #162633; }
        QPushButton {
            background:#0c1720;
            border:1px solid #2b4050;
            border-radius:7px;
            color:#dce7f3;
            min-height:44px;
            font-size:15px;
            font-weight:700;
            padding:6px 12px;
        }
        QComboBox {
            background:#0c1720;
            border:1px solid #2b4050;
            border-radius:7px;
            color:#dce7f3;
            min-height:44px;
            font-size:15px;
            font-weight:700;
            padding:6px 12px;
        }
        QComboBox::drop-down { border:0; width:28px; }
        QComboBox QAbstractItemView {
            background:#071017;
            color:#dce7f3;
            border:0;
            padding:0;
            margin:0;
            outline:0;
            selection-background-color:#1b1805;
            selection-color:#ffd21a;
        }
        QComboBox QAbstractItemView::item {
            background:#071017;
            color:#dce7f3;
            min-height:34px;
            padding:6px 10px;
            margin:0;
            border:0;
        }
        QComboBox QAbstractItemView::item:selected {
            background:#1b1805;
            color:#ffd21a;
        }
        QPushButton:hover { border-color:#ffd21a; }
        QPushButton:disabled {
            background:#071017;
            border-color:#1c2a34;
            color:#596978;
        }
        #backButton {
            min-height:34px;
            padding:4px 12px;
            background:#0c1720;
            border:1px solid #2b4050;
            border-radius:6px;
            color:#dce7f3;
            font-size:14px;
            font-weight:800;
        }
        #backButton:hover { border-color:#ffd21a; color:#ffd21a; }
        #manualControlButton {
            min-width:168px;
            min-height:42px;
            padding:4px 20px;
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #3a3005, stop:0.52 #211b04, stop:1 #0f160d);
            border:2px solid #ffd21a;
            border-radius:8px;
            color:#ffd21a;
            font-size:19px;
            font-weight:900;
        }
        #manualControlButton:hover {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #5a4807, stop:0.52 #2b2405, stop:1 #132010);
            border-color:#fff1a6;
            color:#fff1a6;
        }
        #manualControlButton:pressed {
            background:#4a3a03;
            border-color:#ffffff;
        }
        QPushButton:checked, #smallActive {
            background:#1b1805;
            border-color:#ffd21a;
            color:#ffd21a;
        }
        #navButtonActive {
            background:transparent;
            border-color:transparent;
            color:#f2c94c;
            min-height:96px;
            padding:0;
            font-size:12px;
            line-height:16px;
        }
        #navButton {
            background:transparent;
            border-color:transparent;
            min-height:96px;
            color:#a4b0bc;
            padding:0;
            font-size:12px;
            line-height:16px;
        }
        #yellowCommand {
            background:#241e02;
            border-color:#ffd21a;
            color:#ffd21a;
            min-height:150px;
            font-size:24px;
        }
        #yellowCommand:hover {
            background:#3a3105;
            border-color:#fff1a6;
            color:#fff1a6;
        }
        #redCommand {
            background:#2b0507;
            border-color:#ff2222;
            color:#ffffff;
            min-height:150px;
            font-size:22px;
        }
        #redCommand:hover {
            background:#4a080c;
            border-color:#ff6b6b;
            color:#ffffff;
        }
        #controlCommand {
            font-size:17px;
            font-weight:800;
        }
        #manualModeToggleButton {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #3a3005, stop:0.52 #211b04, stop:1 #0f160d);
            border:2px solid #ffd21a;
            border-radius:8px;
            color:#ffd21a;
            font-size:26px;
            font-weight:900;
        }
        #manualModeToggleButton:hover {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #5a4807, stop:0.52 #2b2405, stop:1 #132010);
            border-color:#fff1a6;
            color:#fff1a6;
        }
        #manualJoystickPanel {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #101820, stop:0.62 #071017, stop:1 #04090e);
            border:1px solid #4b3d12;
            border-radius:8px;
        }
        #manualActionGroupPanel {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #071421, stop:0.62 #06111b, stop:1 #04090e);
            border:1px solid #2a78a7;
            border-radius:8px;
        }
        #manualJoystickTitle {
            background:transparent;
            border:0;
            color:#ffd21a;
            font-size:16px;
            font-weight:900;
        }
        #manualActionPanel {
            background:transparent;
            border:0;
        }
        #manualActionButton {
            min-height:96px;
            max-height:108px;
            padding:8px 6px;
            border-radius:7px;
            font-size:21px;
            font-weight:900;
        }
        #manualActionButton {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #1f3948, stop:0.45 #102532, stop:1 #07131d);
            border:1px solid #58758a;
            color:#ecf7ff;
        }
        #manualActionButton:hover {
            border-color:#ffd21a;
            color:#ffd21a;
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #315268, stop:0.45 #173544, stop:1 #0a1b27);
        }
        #manualActionButton:pressed {
            border:2px solid #fff1a6;
            color:#fff1a6;
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #5f4d08, stop:0.45 #332907, stop:1 #101b12);
        }
        #smallButton, #smallActive { min-height:30px; min-width:70px; }
        #infoLine { color:#b9c5d0; background:#071017; border:0; padding:6px 2px; font-size:14px; }
        #robotOverviewPage { background:#03080d; }
        #robotPageTitle {
            color:#ffffff;
            font-size:24px;
            font-weight:900;
            background:transparent;
            border:0;
            padding:4px 2px 8px 2px;
        }
        #robotStatusCard {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                        stop:0 #08111b, stop:0.55 #050c13, stop:1 #03080d);
            border:1px solid #263646;
            border-radius:8px;
        }
        #robotStatusCard[state="normal"], #robotStatusCard[state="moving"] {
            border-color:#2d9f5a;
        }
        #robotStatusCard[state="warning"] { border-color:#9a7625; }
        #robotStatusCard[state="danger"] { border-color:#a94730; }
        #robotStatusCard[state="offline"] {
            background:#060a0f;
            border-color:#303a42;
        }
        #robotVisualPanel {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #07111a, stop:1 #02070c);
            border:1px solid #29394a;
            border-radius:8px;
        }
        #robotIdLabel {
            color:#7d8794;
            background:transparent;
            border:0;
            font-size:12px;
            font-weight:900;
        }
        #robotAccentLine {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #00d4ff, stop:0.55 #ffd21a, stop:1 rgba(255,210,26,0));
            border:0;
        }
        #robotDetailPanel { background:transparent; border:0; }
        #robotCardName {
            font-size:28px;
            font-weight:900;
            background:transparent;
            letter-spacing:0;
        }
        #robotStatusBadge {
            background:#101b27;
            border:1px solid #314154;
            border-radius:8px;
            color:#d9e5ef;
            font-size:14px;
            font-weight:900;
            padding:5px 16px;
        }
        #robotStatusBadge[state="normal"] {
            border-color:#2f5b3e;
            color:#74e985;
        }
        #robotStatusBadge[state="moving"] {
            border-color:#3270ad;
            color:#72b9ff;
        }
        #robotStatusBadge[state="danger"] {
            border-color:#b53a3f;
            color:#ff736b;
        }
        #robotStatusBadge[state="warning"] {
            border-color:#9a7625;
            color:#ffd166;
        }
        #robotStatusBadge[state="offline"] {
            background:#0a0f14;
            border-color:#303a42;
            color:#747f89;
        }
        #robotBatteryPanel {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #081421, stop:1 #09121b);
            border:1px solid #28394a;
            border-radius:8px;
        }
        #robotSectionIcon {
            background:transparent;
            border:0;
        }
        #robotBatteryIconBody {
            background:#0f2030;
            border:2px solid #70b9ff;
            border-radius:3px;
        }
        #robotBatteryIconNub {
            background:#70b9ff;
            border:0;
            border-radius:2px;
        }
        #robotBatteryTitle {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:18px;
            font-weight:900;
        }
        #robotBatteryValue {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:18px;
            font-weight:900;
            min-width:42px;
        }
        QProgressBar#robotBatteryBar {
            background:#142131;
            border:0;
            border-radius:3px;
            min-height:20px;
            max-height:20px;
        }
        QProgressBar#robotBatteryBar::chunk {
            background:#72b9ff;
            border-radius:3px;
        }
        QProgressBar#robotBatteryBar[state="moving"]::chunk { background:#72b9ff; }
        QProgressBar#robotBatteryBar[state="warning"]::chunk { background:#ffd166; }
        QProgressBar#robotBatteryBar[state="danger"]::chunk { background:#ff736b; }
        QProgressBar#robotBatteryBar[state="offline"]::chunk { background:#555f68; }
        #robotInfoCell {
            background:#08111a;
            border:1px solid #253645;
            border-radius:8px;
        }
        #robotInfoTitle {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:16px;
            font-weight:900;
        }
        #robotInfoIcon {
            background:transparent;
            border:0;
        }
        #robotRowDot {
            color:#68b4ff;
            background:transparent;
            border:0;
            font-size:12px;
            font-weight:900;
        }
        #robotRowLabel, #robotPartLabel {
            color:#f0f4f8;
            background:transparent;
            border:0;
            font-size:13px;
            font-weight:900;
        }
        #robotCardValue {
            color:#f3f7fc;
            background:transparent;
            border:0;
            font-size:15px;
            font-weight:900;
        }
        #robotCardSubValue {
            color:#f3f7fc;
            background:transparent;
            border:0;
            font-size:14px;
            font-weight:900;
            min-width:64px;
        }
        #robotMiniValue {
            color:#f3f7fc;
            background:transparent;
            border:0;
            font-size:14px;
            font-weight:900;
            min-width:58px;
        }
        #robotPartsPanel {
            background:qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                        stop:0 #07111c, stop:0.52 #081625, stop:1 #07101b);
            border:1px solid #253645;
            border-radius:8px;
        }
        #robotPartsTitleBlock {
            background:transparent;
            border:0;
        }
        #robotPartsIcon {
            background:rgba(30, 104, 180, 34);
            border:1px solid rgba(102, 179, 255, 72);
            border-radius:17px;
        }
        #robotPartsTitle {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:16px;
            font-weight:900;
            line-height:18px;
        }
        #robotPartsDivider {
            background:#254267;
            border:0;
        }
        #robotPartCard {
            background:#071321;
            border:1px solid #25456a;
            border-radius:6px;
        }
        #robotPartCard[state="normal"] {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #0d2c20, stop:1 #081b17);
            border:1px solid #28c76f;
        }
        #robotPartCard[state="danger"] {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #421b20, stop:1 #241015);
            border:1px solid #ff5b57;
        }
        #robotPartCard[state="offline"] {
            background:qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                        stop:0 #101c26, stop:1 #071017);
            border:1px solid #3f566a;
        }
        #robotPartCardLabel {
            color:#ffffff;
            background:transparent;
            border:0;
            font-size:13px;
            font-weight:1000;
            qproperty-alignment: AlignCenter;
        }
        QLabel#robotStatusChip, QLabel#robotStatusChipMoving, QLabel#robotStatusChipDanger, QLabel#robotStatusChipOffline {
            border-radius:4px;
            min-height:26px;
            max-height:30px;
            min-width:66px;
            font-size:13px;
            font-weight:1000;
            padding:0 2px;
            qproperty-alignment: AlignCenter;
        }
        QLabel#robotStatusChip, QLabel#robotStatusChipMoving {
            background:rgba(12, 64, 38, 90);
            border:1px solid rgba(85, 255, 160, 120);
            color:#d8ffe5;
        }
        QLabel#robotStatusChipDanger {
            background:rgba(74, 18, 26, 95);
            border:1px solid rgba(255, 122, 126, 145);
            color:#ffd6d8;
        }
        QLabel#robotStatusChipOffline {
            background:transparent;
            border:0;
            color:#ffffff;
            padding:0 4px;
            qproperty-alignment: AlignCenter;
        }
        #robotFooter {
            background:#071017;
            border:1px solid #152838;
            border-radius:7px;
            color:#c8d2dd;
            font-size:14px;
            font-weight:800;
        }
        #robotLegendItem {
            background:transparent;
            border:0;
        }
        #robotLegendLabel {
            background:transparent;
            border:0;
            color:#c8d2dd;
            font-size:14px;
            font-weight:800;
        }
    )");
}

QPushButton *MainWindow::makeNavButton(const QString &text, bool active)
{
    QPushButton *button = new SideNavButton(text);
    button->setObjectName(active ? "navButtonActive" : "navButton");
    button->setMinimumWidth(88);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

QPushButton *MainWindow::makeCommandButton(const QString &text, const QString &objectName)
{
    QPushButton *button = new CommandButton(text);
    if (!objectName.isEmpty()) {
        button->setObjectName(objectName);
    }
    button->setMinimumHeight(150);
    return button;
}

QFrame *MainWindow::makeHeaderMetricCard(const QString &title, QLabel **valueLabel,
                                         const QString &accentColor,
                                         QProgressBar **progressBar,
                                         const QString &iconKind)
{
    QFrame *card = new QFrame;
    card->setObjectName("headerMetric");
    auto *glow = new QGraphicsDropShadowEffect(card);
    glow->setColor(QColor(0, 137, 201, 112));
    glow->setBlurRadius(36.0);
    glow->setOffset(0, 0);
    card->setGraphicsEffect(glow);
    card->setFixedHeight(132);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, progressBar ? 6 : 12, 24, progressBar ? 10 : 12);
    layout->setSpacing(progressBar ? 0 : 3);

    QLabel *titleLabel = new QLabel(title);
    titleLabel->setObjectName("headerMetricTitle");
    QLabel *value = new QLabel(progressBar ? "0%" : "0");
    value->setObjectName(progressBar ? "headerProgressValue" : "headerMetricValue");
    value->setStyleSheet(QString("color:%1").arg(accentColor));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setMaximumHeight(38);
    *valueLabel = value;

    auto makeTitleRow = [&]() {
        QHBoxLayout *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(10);
        if (!iconKind.isEmpty()) {
            titleRow->addWidget(new HeaderMetricIcon(iconKind));
        }
        titleRow->addWidget(titleLabel);
        titleRow->addStretch(1);
        return titleRow;
    };

    if (progressBar) {
        QHBoxLayout *topRow = new QHBoxLayout;
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(10);

        QHBoxLayout *headerRow = makeTitleRow();
        headerRow->setContentsMargins(0, 8, 0, 0);
        topRow->addLayout(headerRow, 1);

        value->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        value->setFixedHeight(42);
        topRow->addWidget(value, 0, Qt::AlignBottom);
        layout->addLayout(topRow);
        layout->addSpacing(24);

        QProgressBar *bar = new AnimatedProgressBar;
        bar->setObjectName("headerProgressBar");
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
        *progressBar = bar;
        layout->addWidget(bar);
        layout->addStretch(1);
    } else {
        layout->addLayout(makeTitleRow());
        layout->addWidget(value);
        QFrame *line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setObjectName("headerMetricLine");
        line->setStyleSheet("background:#24313a; border:0; max-height:1px;");
        layout->addStretch(1);
        layout->addWidget(line);
    }
    return card;
}

QString MainWindow::formatMissionTime() const
{
    if (!m_missionTimerActive || !m_missionStartedAt.isValid()) {
        return "00:00";
    }
    const qint64 seconds = m_missionStartedAt.secsTo(QDateTime::currentDateTime());
    return QString("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void MainWindow::startDemoMode(int count)
{
    m_robotCount = qBound(1, count, kMaxRobots);
    m_demoTimer = new QTimer(this);
    connect(m_demoTimer, &QTimer::timeout, this, [this]() {
        updateSnapshots(makeDemoSnapshots(m_robotCount));
    });
    updateSnapshots(makeDemoSnapshots(m_robotCount));
    appendEvent(UiEvent{-1, 1, 0, 0,
                        QString("UI 테스트 모드 시작: %1대").arg(m_robotCount)});
    m_demoTimer->start(1000);
}

QVector<RobotSnapshot> MainWindow::makeDemoSnapshots(int count) const
{
    QVector<RobotSnapshot> snapshots;
    const int clamped = qBound(1, count, kMaxRobots);
    snapshots.reserve(clamped);

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const float phase = static_cast<float>((nowMs / 100) % 360) * 0.017453292f;
    const QVector<QColor> colors = {
        QColor("#18d878"), QColor("#00d4ff"), QColor("#ffd21a"), QColor("#ff5b57"),
        QColor("#a78bfa"), QColor("#f97316"), QColor("#14b8a6"), QColor("#e879f9"),
        QColor("#84cc16"), QColor("#60a5fa")
    };

    for (int i = 0; i < clamped; ++i) {
        RobotSnapshot snapshot;
        snapshot.id = i;
        snapshot.shmOpen = true;
        snapshot.connected = true;
        snapshot.mode = i % 3;
        snapshot.faultLevel = (i == 2) ? 4 : 0;
        snapshot.faultCode = (i == 2) ? 0x10u : 0u;
        snapshot.faultText = (i == 2) ? QStringLiteral("drive fault") : QString();
        snapshot.x = 2.0f + static_cast<float>(i % 5) * 1.7f + std::sin(phase + i) * 0.12f;
        snapshot.y = 1.1f + static_cast<float>(i / 5) * 2.25f + std::cos(phase + i) * 0.12f;
        snapshot.theta = phase + static_cast<float>(i) * 0.25f;
        snapshot.vx = 0.15f + static_cast<float>(i % 4) * 0.08f;
        snapshot.vy = 0.0f;
        snapshot.omega = 0.02f;
        snapshot.battery = static_cast<float>(qMax(18, 96 - i * 7));
        snapshot.linkRttMs = 17.0f + static_cast<float>(i * 2);
        snapshot.imageFps = (i == 4 || i == 9) ? 0.0f : 24.0f;
        snapshot.lidarFps = 12.0f;
        snapshot.dropRate = i == 2 ? 8.0f : 0.2f * static_cast<float>(i);
        snapshot.odomSeq = static_cast<uint32_t>(nowMs / 100 + i);
        snapshot.odomTimestampUs = static_cast<quint64>(nowMs) * 1000ULL;
        snapshot.missionId = 100 + i;
        snapshot.waypointIdx = static_cast<uint32_t>((nowMs / 1000 + i) % 8);
        snapshot.totalWaypoints = 8;
        snapshot.missionProgress = static_cast<float>((i * 9 + (nowMs / 500) % 30) % 100);
        snapshot.pathOk = i != 7;
        snapshot.poseOk = true;
        snapshot.goalReached = snapshot.missionProgress >= 95.0f;
        snapshot.distanceToGoalM = qMax(0.0f, 12.0f - snapshot.missionProgress * 0.11f);
        snapshot.rxPackets = static_cast<int>(nowMs / 100 + i * 11);
        snapshot.txCommands = i;
        snapshot.ackPackets = qMax(0, i - 1);
        snapshot.lastRxUs = static_cast<quint64>(nowMs) * 1000ULL;

        if (i != 4 && i != 9) {
            QImage image(640, 360, QImage::Format_RGB32);
            const QColor accent = colors[i % colors.size()];
            image.fill(QColor("#071017"));
            QPainter painter(&image);
            QLinearGradient bg(0, 0, 640, 360);
            bg.setColorAt(0.0, accent.darker(260));
            bg.setColorAt(1.0, QColor("#03080d"));
            painter.fillRect(image.rect(), bg);
            painter.setPen(QPen(accent, 4));
            painter.drawRect(image.rect().adjusted(18, 18, -18, -18));
            painter.setPen(Qt::white);
            painter.setFont(QFont("Noto Sans", 40, QFont::Black));
            painter.drawText(image.rect(), Qt::AlignCenter, robotName(i));
            painter.setFont(QFont("Noto Sans", 18, QFont::Bold));
            painter.setPen(QColor("#c8d7e4"));
            painter.drawText(QRect(0, 232, 640, 42), Qt::AlignCenter,
                             QString("DEMO FEED  %1 fps").arg(snapshot.imageFps, 0, 'f', 0));
            snapshot.image = image;
        }

        snapshot.globalPath = {
            {snapshot.x, snapshot.y, 0.0f, 0.0f},
            {qMin(snapshot.x + 1.2f, 11.0f), qMin(snapshot.y + 0.8f, 7.0f), 0.0f, 0.0f},
            {qMin(snapshot.x + 2.2f, 11.0f), qMin(snapshot.y + 1.4f, 7.0f), 0.0f, 0.0f}
        };

        snapshots.append(snapshot);
    }

    return snapshots;
}

void MainWindow::setRobotCount(int count)
{
    m_robotCount = qBound(0, count, kMaxRobots);
    QSet<int> retainedRouteIds;
    for (int robotId : m_routeGeneratedDisplayRobotIds) {
        if (robotId >= 0 && robotId < m_robotCount) {
            retainedRouteIds.insert(robotId);
        }
    }
    m_routeGeneratedDisplayRobotIds = retainedRouteIds;
    syncRobotUi(m_robotCount);
    clampSelectedRobot();

    if (m_victimAlertRobotId >= m_robotCount) {
        m_victimAlertRobotId = -1;
        if (m_victimAlertButton) {
            m_victimAlertButton->hide();
        }
    }
    if (m_expandedRobotId >= m_robotCount) {
        m_expandedRobotId = -1;
        if (m_contentStack && m_cameraPage && m_contentStack->currentWidget() == m_cameraPage) {
            leaveCameraFullscreen();
        }
    }

    if (m_demoMode) {
        updateSnapshots(makeDemoSnapshots(m_robotCount));
        return;
    }

    m_monitor.start(kMaxRobots);
    refreshRobotSelector();
    refreshRobotList();
    refreshGlobalPathDisplay();
}

void MainWindow::noteRouteGeneratedForDisplayRobots(const QVector<int> &robotIds)
{
    for (int robotId : robotIds) {
        if (robotId >= 0 && robotId < m_robotCount) {
            m_routeGeneratedDisplayRobotIds.insert(robotId);
        }
    }
    refreshGlobalPathDisplay();
}

void MainWindow::refreshGlobalPathDisplay()
{
    if (!m_map) {
        return;
    }

    QVector<int> visiblePathIds;
    const int count = qBound(0, m_robotCount, kMaxRobots);
    visiblePathIds.reserve(count);
    for (int robotId = 0; robotId < count; ++robotId) {
        if (showsPathOnDeploy(robotId)
            || m_routeGeneratedDisplayRobotIds.contains(robotId)) {
            visiblePathIds.append(robotId);
        }
    }

    m_map->setGlobalPathRobotIds(visiblePathIds);
    m_map->setGlobalPathsVisible(!visiblePathIds.isEmpty());
}

void MainWindow::syncRobotUi(int count)
{
    const int clamped = qBound(0, count, kMaxRobots);
    syncVideoTiles(clamped);
    syncStatusRows(clamped);
    syncRobotStatusCards(clamped);

    if (m_robotSelector) {
        m_robotSelector->setMaxVisibleItems(qMin(clamped + 1, kMaxRobots + 1));
    }
    if (m_robotTotalLabel) {
        m_robotTotalLabel->setText(QString("총 로봇: %1").arg(clamped));
    }
}

void MainWindow::syncVideoTiles(int count)
{
    if (!m_videoGrid) {
        return;
    }

    const bool resetLayout = m_videoPlacements.size() != count;
    while (m_videoTiles.size() > count) {
        VideoTile *tile = m_videoTiles.takeLast();
        m_videoGrid->removeWidget(tile);
        tile->deleteLater();
    }
    if (count <= 0) {
        if (!m_videoEmptyState) {
            m_videoEmptyState = new DashboardEmptyState(DashboardEmptyState::Kind::Video,
                                                        m_videoGrid->parentWidget());
        }
        if (m_videoGrid->indexOf(m_videoEmptyState) < 0) {
            m_videoGrid->addWidget(m_videoEmptyState, 0, 0,
                                   kVideoLayoutRows, kVideoLayoutColumns);
        }
        m_videoEmptyState->show();
        for (int row = 0; row < kVideoLayoutRows; ++row) {
            m_videoGrid->setRowStretch(row, 1);
        }
        for (int col = 0; col < kVideoLayoutColumns; ++col) {
            m_videoGrid->setColumnStretch(col, 1);
        }
        resetVideoLayout(0);
        return;
    }
    if (m_videoEmptyState) {
        m_videoGrid->removeWidget(m_videoEmptyState);
        m_videoEmptyState->hide();
    }
    while (m_videoTiles.size() < count) {
        const int robotId = m_videoTiles.size();
        VideoTile *tile = new VideoTile(robotId);
        m_videoTiles.append(tile);
        connect(tile, &VideoTile::clicked, this, &MainWindow::showCameraFullscreen);
        connect(tile, &VideoTile::moveRequested, this, &MainWindow::moveVideoTile);
        connect(tile, &VideoTile::resizeRequested, this, &MainWindow::resizeVideoTile);
        connect(tile, &VideoTile::shrinkRequested, this, &MainWindow::shrinkVideoTile);
        if (robotId < m_snapshots.size()) {
            tile->setSnapshot(m_snapshots[robotId]);
        }
    }
    if (resetLayout) {
        resetVideoLayout(count);
    }
    relayoutVideoTiles();
}

void MainWindow::syncStatusRows(int count)
{
    if (!m_robotStatusGrid) {
        return;
    }

    while (m_statusRows.size() > count) {
        StatusRow *row = m_statusRows.takeLast();
        m_robotStatusGrid->removeWidget(row);
        row->deleteLater();
    }
    if (count <= 0) {
        if (!m_robotStatusEmptyState) {
            m_robotStatusEmptyState = new DashboardEmptyState(DashboardEmptyState::Kind::RobotStatus,
                                                              m_robotStatusGrid->parentWidget());
        }
        if (m_robotStatusGrid->indexOf(m_robotStatusEmptyState) < 0) {
            m_robotStatusGrid->addWidget(m_robotStatusEmptyState, 0, 0);
        }
        m_robotStatusEmptyState->show();
        m_robotStatusGrid->setRowStretch(0, 1);
        m_robotStatusGrid->setColumnStretch(0, 1);
        if (QWidget *body = m_robotStatusGrid->parentWidget()) {
            body->setMinimumHeight(0);
            body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
        return;
    }
    if (m_robotStatusEmptyState) {
        m_robotStatusGrid->removeWidget(m_robotStatusEmptyState);
        m_robotStatusEmptyState->hide();
    }
    while (m_statusRows.size() < count) {
        const int robotId = m_statusRows.size();
        StatusRow *row = new StatusRow(robotId);
        m_statusRows.append(row);
        if (robotId < m_snapshots.size()) {
            row->setSnapshot(m_snapshots[robotId]);
        }
    }
    const GridSpec spec = statusGridSpec(count);
    for (StatusRow *row : m_statusRows) {
        m_robotStatusGrid->removeWidget(row);
    }
    for (int row = 0; row < kMaxRobots; ++row) {
        m_robotStatusGrid->setRowStretch(row, 0);
    }
    for (int col = 0; col < kMaxRobots; ++col) {
        m_robotStatusGrid->setColumnStretch(col, 0);
    }
    for (int i = 0; i < m_statusRows.size(); ++i) {
        const int row = i / spec.columns;
        const int col = i % spec.columns;
        m_robotStatusGrid->addWidget(m_statusRows[i], row, col);
        m_robotStatusGrid->setRowStretch(row, count >= 6 ? 0 : 1);
        m_robotStatusGrid->setColumnStretch(col, 1);
    }
    if (QWidget *body = m_robotStatusGrid->parentWidget()) {
        if (count >= 6) {
            body->setMinimumHeight(m_statusRows.size() * 36 + qMax(0, m_statusRows.size() - 1) * m_robotStatusGrid->spacing());
            body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        } else {
            body->setMinimumHeight(0);
            body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
    }
}

void MainWindow::syncRobotStatusCards(int count)
{
    if (!m_robotCardGrid) {
        return;
    }

    while (m_robotStatusCards.size() > count) {
        RobotStatusCard *card = m_robotStatusCards.takeLast();
        m_robotCardGrid->removeWidget(card);
        card->deleteLater();
    }
    while (m_robotStatusCards.size() < count) {
        const int robotId = m_robotStatusCards.size();
        RobotStatusCard *card = new RobotStatusCard(robotId);
        m_robotStatusCards.append(card);
        if (robotId < m_snapshots.size()) {
            card->setSnapshot(m_snapshots[robotId]);
        }
    }
    relayoutRobotStatusCards();
}

void MainWindow::resetVideoLayout(int count)
{
    const int clamped = qBound(0, count, kMaxRobots);
    const VideoLayoutSpec spec = videoLayoutSpec(clamped);
    m_videoPlacements.clear();
    m_videoPlacements.resize(clamped);
    if (clamped <= 0) {
        updateVideoLayoutBodySize();
        return;
    }

    QVector<int> ordered;
    ordered.reserve(clamped);
    const int primary = featuredVideoRobotForSelection(m_selectedRobot, clamped);
    if (spec.hasFeaturedTile) {
        ordered.append(primary);
    }
    for (int i = 0; i < clamped; ++i) {
        if (!spec.hasFeaturedTile || i != primary) {
            ordered.append(i);
        }
    }

    for (int i = 0; i < ordered.size() && i < spec.placements.size(); ++i) {
        const int robotId = ordered[i];
        const VideoLayoutSlot &slot = spec.placements[i];
        if (robotId < 0 || robotId >= m_videoPlacements.size()) {
            continue;
        }
        const VideoLayoutSlot scaledSlot = scaledVideoSlot(slot);
        m_videoPlacements[robotId] = {scaledSlot.row, scaledSlot.column,
                                      scaledSlot.rowSpan, scaledSlot.columnSpan};
    }
    updateVideoLayoutBodySize();
}

bool MainWindow::videoPlacementAvailable(int robotId, int row, int column,
                                         int rowSpan, int columnSpan) const
{
    if (row < 0 || column < 0 || rowSpan < 1 || columnSpan < 1
        || row + rowSpan > kVideoLayoutRows
        || column + columnSpan > kVideoLayoutColumns) {
        return false;
    }

    const QRect candidate(column, row, columnSpan, rowSpan);
    for (int i = 0; i < m_videoPlacements.size(); ++i) {
        if (i == robotId) {
            continue;
        }
        const VideoPlacement &placement = m_videoPlacements[i];
        const QRect occupied(placement.column, placement.row,
                             placement.columnSpan, placement.rowSpan);
        if (candidate.intersects(occupied)) {
            return false;
        }
    }
    return true;
}

bool MainWindow::setVideoPlacementIfFree(int robotId, int row, int column,
                                         int rowSpan, int columnSpan)
{
    if (robotId < 0 || robotId >= m_videoPlacements.size()) {
        return false;
    }

    if (!videoPlacementAvailable(robotId, row, column, rowSpan, columnSpan)) {
        return false;
    }

    m_videoPlacements[robotId] = {row, column, rowSpan, columnSpan};
    relayoutVideoTiles();
    return true;
}

bool MainWindow::autoArrangeVideoLayout(int fixedRobotId, int row, int column,
                                        int rowSpan, int columnSpan)
{
    if (fixedRobotId < 0 || fixedRobotId >= m_videoPlacements.size()) {
        return false;
    }

    const VideoLayoutSlot fixedSlot{row, column, rowSpan, columnSpan};
    QVector<VideoLayoutSlot> remainingSlots;
    if (!arrangeRemainingVideoSlots(fixedSlot,
                                    m_videoPlacements.size() - 1,
                                    &remainingSlots)) {
        return false;
    }

    QVector<VideoPlacement> nextPlacements;
    nextPlacements.resize(m_videoPlacements.size());
    nextPlacements[fixedRobotId] = {fixedSlot.row,
                                    fixedSlot.column,
                                    fixedSlot.rowSpan,
                                    fixedSlot.columnSpan};

    int slotIndex = 0;
    for (int robotId = 0; robotId < nextPlacements.size(); ++robotId) {
        if (robotId == fixedRobotId) {
            continue;
        }
        if (slotIndex >= remainingSlots.size()) {
            return false;
        }
        const VideoLayoutSlot &slot = remainingSlots[slotIndex++];
        nextPlacements[robotId] = {slot.row, slot.column, slot.rowSpan, slot.columnSpan};
    }

    m_videoPlacements = nextPlacements;
    relayoutVideoTiles();
    return true;
}

void MainWindow::updateVideoLayoutBodySize()
{
    if (!m_videoGrid || !m_videoGrid->parentWidget()) {
        return;
    }

    QWidget *body = m_videoGrid->parentWidget();
    body->setMinimumSize(0, 0);
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

QPoint MainWindow::videoGridCellAt(const QPoint &globalPos) const
{
    if (!m_videoGrid || !m_videoGrid->parentWidget()) {
        return QPoint(-1, -1);
    }

    QWidget *body = m_videoGrid->parentWidget();
    const QPoint local = body->mapFromGlobal(globalPos);
    if (local.x() < 0 || local.y() < 0) {
        return QPoint(-1, -1);
    }

    const int column = qBound(0, local.x() * kVideoLayoutColumns / qMax(1, body->width()),
                              kVideoLayoutColumns - 1);
    const int row = qBound(0, local.y() * kVideoLayoutRows / qMax(1, body->height()),
                           kVideoLayoutRows - 1);
    return QPoint(column, row);
}

void MainWindow::moveVideoTile(int robotId, const QPoint &globalPos)
{
    if (robotId < 0 || robotId >= m_videoPlacements.size()) {
        return;
    }

    const QPoint cell = videoGridCellAt(globalPos);
    if (cell.x() < 0 || cell.y() < 0) {
        return;
    }

    const VideoPlacement placement = m_videoPlacements[robotId];
    autoArrangeVideoLayout(robotId, cell.y(), cell.x(),
                           placement.rowSpan, placement.columnSpan);
}

void MainWindow::resizeVideoTile(int robotId, int edgeMask, const QPoint &globalPos)
{
    if (robotId < 0 || robotId >= m_videoPlacements.size() || edgeMask == VideoTile::NoEdge) {
        return;
    }

    const QPoint cell = videoGridCellAt(globalPos);
    if (cell.x() < 0 || cell.y() < 0) {
        return;
    }

    const VideoPlacement placement = m_videoPlacements[robotId];
    int row = placement.row;
    int column = placement.column;
    int rowSpan = placement.rowSpan;
    int columnSpan = placement.columnSpan;
    const int right = placement.column + placement.columnSpan - 1;
    const int bottom = placement.row + placement.rowSpan - 1;

    if (edgeMask & VideoTile::LeftEdge) {
        column = qBound(0, cell.x(), right);
        columnSpan = right - column + 1;
    } else if (edgeMask & VideoTile::RightEdge) {
        const int newRight = qBound(column, cell.x(), kVideoLayoutColumns - 1);
        columnSpan = newRight - column + 1;
    }

    if (edgeMask & VideoTile::TopEdge) {
        row = qBound(0, cell.y(), bottom);
        rowSpan = bottom - row + 1;
    } else if (edgeMask & VideoTile::BottomEdge) {
        const int newBottom = qBound(row, cell.y(), kVideoLayoutRows - 1);
        rowSpan = newBottom - row + 1;
    }

    autoArrangeVideoLayout(robotId, row, column, rowSpan, columnSpan);
}

void MainWindow::shrinkVideoTile(int robotId, int edgeMask)
{
    if (robotId < 0 || robotId >= m_videoPlacements.size() || edgeMask == VideoTile::NoEdge) {
        return;
    }

    VideoPlacement placement = m_videoPlacements[robotId];
    if ((edgeMask & VideoTile::LeftEdge) && placement.columnSpan > 1) {
        ++placement.column;
        --placement.columnSpan;
    } else if ((edgeMask & VideoTile::RightEdge) && placement.columnSpan > 1) {
        --placement.columnSpan;
    }

    if ((edgeMask & VideoTile::TopEdge) && placement.rowSpan > 1) {
        ++placement.row;
        --placement.rowSpan;
    } else if ((edgeMask & VideoTile::BottomEdge) && placement.rowSpan > 1) {
        --placement.rowSpan;
    }

    autoArrangeVideoLayout(robotId, placement.row, placement.column,
                           placement.rowSpan, placement.columnSpan);
}

void MainWindow::relayoutVideoTiles()
{
    if (!m_videoGrid) {
        return;
    }
    if (m_videoPlacements.size() != m_videoTiles.size()) {
        resetVideoLayout(m_videoTiles.size());
    }
    for (VideoTile *tile : m_videoTiles) {
        m_videoGrid->removeWidget(tile);
    }
    for (int row = 0; row < kVideoLayoutRows; ++row) {
        m_videoGrid->setRowStretch(row, 0);
    }
    for (int col = 0; col < kVideoLayoutColumns; ++col) {
        m_videoGrid->setColumnStretch(col, 0);
    }

    for (int i = 0; i < m_videoTiles.size() && i < m_videoPlacements.size(); ++i) {
        const VideoPlacement &placement = m_videoPlacements[i];
        if (!videoPlacementAvailable(i, placement.row, placement.column,
                                     placement.rowSpan, placement.columnSpan)) {
            continue;
        }
        m_videoGrid->addWidget(m_videoTiles[i], placement.row, placement.column,
                               placement.rowSpan, placement.columnSpan);
    }
    for (int row = 0; row < kVideoLayoutRows; ++row) {
        m_videoGrid->setRowStretch(row, 1);
    }
    for (int col = 0; col < kVideoLayoutColumns; ++col) {
        m_videoGrid->setColumnStretch(col, 1);
    }

    updateVideoLayoutBodySize();
}

void MainWindow::relayoutRobotStatusCards()
{
    if (!m_robotCardGrid) {
        return;
    }
    const bool scrollLayout = m_robotStatusCards.size() >= 6;
    for (RobotStatusCard *card : m_robotStatusCards) {
        m_robotCardGrid->removeWidget(card);
    }
    for (int row = 0; row <= kMaxRobots / kRobotCardGridColumns; ++row) {
        m_robotCardGrid->setRowStretch(row, 0);
    }
    for (int i = 0; i < m_robotStatusCards.size(); ++i) {
        m_robotCardGrid->addWidget(m_robotStatusCards[i], i / kRobotCardGridColumns, i % kRobotCardGridColumns);
        m_robotCardGrid->setRowStretch(i / kRobotCardGridColumns, scrollLayout ? 0 : 1);
    }
    if (scrollLayout) {
        m_robotCardGrid->setRowStretch((m_robotStatusCards.size() + kRobotCardGridColumns - 1) / kRobotCardGridColumns, 1);
    }
    if (QWidget *body = m_robotCardGrid->parentWidget()) {
        const int rows = qMax(1, (m_robotStatusCards.size() + kRobotCardGridColumns - 1) / kRobotCardGridColumns);
        if (scrollLayout) {
            body->setMinimumWidth(kRobotCardGridColumns * 620 + (kRobotCardGridColumns - 1) * m_robotCardGrid->spacing());
            body->setMinimumHeight(rows * 350 + (rows - 1) * m_robotCardGrid->spacing());
            body->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        } else {
            body->setMinimumSize(kRobotCardGridColumns * 620 + (kRobotCardGridColumns - 1) * m_robotCardGrid->spacing(),
                                 rows * 350 + (rows - 1) * m_robotCardGrid->spacing());
            body->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        }
    }
}

void MainWindow::clampSelectedRobot()
{
    if (m_robotCount <= 0) {
        m_selectedRobot = kAllRobotsSelection;
        if (m_map) {
            m_map->setSelectedRobot(m_selectedRobot);
        }
        return;
    }
    if (m_selectedRobot == kAllRobotsSelection) {
        if (m_map) {
            m_map->setSelectedRobot(m_selectedRobot);
        }
        return;
    }
    const int upper = qMax(0, m_robotCount - 1);
    const int clamped = qBound(0, m_selectedRobot, upper);
    if (clamped == m_selectedRobot) {
        return;
    }

    m_selectedRobot = clamped;
    if (m_map) {
        m_map->setSelectedRobot(m_selectedRobot);
    }
}

void MainWindow::updateMissionSummary()
{
    const QVector<RobotSnapshot> deployedSnapshots = snapshotsForDeployedRobots(m_snapshots, m_robotCount);
    int connected = 0;
    float progressTotal = 0.0f;
    int progressCount = 0;
    for (const RobotSnapshot &snapshot : deployedSnapshots) {
        if (snapshot.connected) {
            ++connected;
            progressTotal += snapshot.missionProgress > 1.0f
                ? snapshot.missionProgress
                : snapshot.missionProgress * 100.0f;
            ++progressCount;
        }
    }

    if (connected > 0 && !m_missionTimerActive) {
        m_missionStartedAt = QDateTime::currentDateTime();
        m_missionTimerActive = true;
    }

    const int progress = progressCount > 0
        ? qBound(0, qRound(progressTotal / progressCount), 100)
        : 0;
    if (m_metricProgress) {
        m_metricProgress->setText(QString("%1%").arg(progress));
    }
    if (m_metricProgressBar) {
        setProgressBarValueAnimated(m_metricProgressBar, progress);
    }
    if (m_metricMissionTime) {
        m_metricMissionTime->setText(formatMissionTime());
    }
    if (m_metricConnected) {
        m_metricConnected->setText(QString::number(connected));
    }
    if (m_metricEvents) {
        m_metricEvents->setText(QString::number(m_eventList ? m_eventList->count() : 0));
    }
}

void MainWindow::updatePacketLogPanel()
{
    if (!m_packetStatsList) {
        return;
    }
    m_packetStatsList->clear();
    const QVector<RobotSnapshot> deployedSnapshots = snapshotsForDeployedRobots(m_snapshots, m_robotCount);
    int totalRx = 0;
    int totalTx = 0;
    int totalAck = 0;
    int totalDrops = 0;
    for (const RobotSnapshot &snapshot : deployedSnapshots) {
        const int drops = static_cast<int>(snapshot.imgDropCount + snapshot.lidarDropCount);
        totalRx += snapshot.rxPackets;
        totalTx += snapshot.txCommands;
        totalAck += snapshot.ackPackets;
        totalDrops += drops;
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1  RX %2  TX %3  ACK %4  DROP %5")
                .arg(robotName(snapshot.id))
                .arg(snapshot.rxPackets)
                .arg(snapshot.txCommands)
                .arg(snapshot.ackPackets)
                .arg(drops));
        item->setForeground(snapshot.connected ? QColor("#dce7f3") : QColor("#8a96a3"));
        m_packetStatsList->addItem(item);
    }
    QListWidgetItem *total = new QListWidgetItem(
        QString("TOTAL  RX %1  TX %2  ACK %3  DROP %4")
            .arg(totalRx)
            .arg(totalTx)
            .arg(totalAck)
            .arg(totalDrops));
    total->setForeground(QColor("#00d4ff"));
    m_packetStatsList->insertItem(0, total);
}

void MainWindow::updateSnapshots(const QVector<RobotSnapshot> &snapshots)
{
    QVector<RobotSnapshot> displaySnapshots = remapSnapshotsForDisplay(snapshots);
    for (RobotSnapshot &snapshot : displaySnapshots) {
        snapshot.commandMoving = snapshot.connected && m_commandMovingRobotIds.contains(snapshot.id);
        if (snapshot.id >= 0 && snapshot.id < 4
            && snapshot.shmOpen && snapshot.connected && snapshot.image.isNull()) {
            snapshot.image = spotStartFrameForDisplayRobot(snapshot.id);
        }
    }
    const QVector<RobotSnapshot> deployedSnapshots = snapshotsForDeployedRobots(displaySnapshots, m_robotCount);
    m_snapshots = displaySnapshots;
    int connected = 0;
    for (const RobotSnapshot &snapshot : deployedSnapshots) {
        if (snapshot.connected) {
            connected++;
        }
        if (snapshot.id >= 0 && snapshot.id < m_videoTiles.size()) {
            m_videoTiles[snapshot.id]->setSnapshot(snapshot);
        }
        if (m_expandedVideoTile && snapshot.id == m_expandedRobotId) {
            m_expandedVideoTile->setSnapshot(snapshot);
        }
        if (snapshot.id >= 0 && snapshot.id < m_statusRows.size()) {
            m_statusRows[snapshot.id]->setSnapshot(snapshot);
        }
        if (snapshot.id >= 0 && snapshot.id < m_robotStatusCards.size()) {
            m_robotStatusCards[snapshot.id]->setSnapshot(snapshot);
        }
    }

    updateMissionSummary();
    updatePacketLogPanel();
    m_system->setText(connected > 0 ? "● 시스템 정상" : "● 브릿지 대기");
    m_system->setStyleSheet(connected > 0 ? "color:#00e66b" : "color:#ffd21a");
    refreshRobotSelector();
    m_map->setSnapshots(deployedSnapshots);
    refreshRobotList();
}

void MainWindow::appendLogTableRow(const QString &severityCode,
                                   const QString &timestamp,
                                   const QString &source,
                                   const QString &category,
                                   const QString &message,
                                   bool prepend)
{
    if (!m_logTable) {
        return;
    }

    auto makeCell = [](const QString &text) {
        QTableWidgetItem *item = new QTableWidgetItem(text);
        item->setForeground(QColor("#dce7f3"));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    QLabel *badge = new QLabel(logSeverityText(severityCode));
    badge->setAlignment(Qt::AlignCenter);
    badge->setObjectName(severityCode == "CRITICAL" ? "severityCritical"
                         : (severityCode == "WARN" ? "severityWarn" : "severityInfo"));

    const int row = prepend ? 0 : m_logTable->rowCount();
    m_logTable->insertRow(row);
    m_logTable->setItem(row, 0, makeCell(""));
    QTableWidgetItem *severityItem = makeCell(logSeverityText(severityCode));
    severityItem->setData(Qt::UserRole, severityCode);
    m_logTable->setItem(row, 1, severityItem);
    m_logTable->setCellWidget(row, 1, badge);
    m_logTable->setItem(row, 2, makeCell(timestamp));
    m_logTable->setItem(row, 3, makeCell(source));
    m_logTable->setItem(row, 4, makeCell(category));
    m_logTable->setItem(row, 5, makeCell(message));
    applyLogFilters();
    updateLogSummary();
}

void MainWindow::applyLogFilters()
{
    if (!m_logTable) {
        return;
    }
    int filteredIndex = 0;
    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        const bool matches = logRowMatchesFilters(row);
        const bool inPage = filteredIndex >= m_logCurrentPage * m_logRowsPerPage
            && filteredIndex < (m_logCurrentPage + 1) * m_logRowsPerPage;
        m_logTable->setRowHidden(row, !(matches && inPage));
        if (matches) {
            ++filteredIndex;
        }
    }
    updateLogSummary();
}

void MainWindow::setLogSeverityFilter(const QString &severityCode)
{
    m_logSeverityFilter = severityCode;
    m_logCurrentPage = 0;
    for (QPushButton *button : m_logFilterButtons) {
        const bool active = button->property("severityCode").toString() == severityCode;
        button->setObjectName(active ? "logFilterActive" : "logFilterButton");
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
    applyLogFilters();
    updateLogSummary();
}

void MainWindow::setLogPage(int page)
{
    if (!m_logTable) {
        return;
    }
    int filteredCount = 0;
    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        if (logRowMatchesFilters(row)) {
            ++filteredCount;
        }
    }
    const int maxPage = qMax(0, (filteredCount - 1) / m_logRowsPerPage);
    m_logCurrentPage = qBound(0, page, maxPage);
    applyLogFilters();
}

bool MainWindow::logRowMatchesFilters(int row) const
{
    if (!m_logTable) {
        return false;
    }

    QTableWidgetItem *severityItem = m_logTable->item(row, 1);
    const QString severityCode = severityItem ? severityItem->data(Qt::UserRole).toString() : QString();
    if (!m_logSeverityFilter.isEmpty() && severityCode != m_logSeverityFilter) {
        return false;
    }

    QTableWidgetItem *timeItem = m_logTable->item(row, 2);
    const QDateTime rowTime = timeItem
        ? QDateTime::fromString(timeItem->text(), "yyyy-MM-dd HH:mm:ss.zzz")
        : QDateTime();
    if (rowTime.isValid()) {
        if (m_logStartDate && rowTime.date() < m_logStartDate->date()) {
            return false;
        }
        if (m_logEndDate && rowTime.date() > m_logEndDate->date()) {
            return false;
        }
    }

    const QString query = m_logSearch ? m_logSearch->text().trimmed() : QString();
    if (query.isEmpty()) {
        return true;
    }

    QString rowText;
    for (int col = 1; col < m_logTable->columnCount(); ++col) {
        if (QTableWidgetItem *item = m_logTable->item(row, col)) {
            rowText += item->text() + QLatin1Char(' ');
        }
    }
    return rowText.contains(query, Qt::CaseInsensitive);
}

void MainWindow::updateLogSummary()
{
    if (!m_logTable) {
        return;
    }

    int infoCount = 0;
    int warnCount = 0;
    int criticalCount = 0;
    int filteredCount = 0;
    for (int row = 0; row < m_logTable->rowCount(); ++row) {
        QTableWidgetItem *severityItem = m_logTable->item(row, 1);
        const QString severityCode = severityItem ? severityItem->data(Qt::UserRole).toString() : QString();
        if (severityCode == "CRITICAL") {
            ++criticalCount;
        } else if (severityCode == "WARN") {
            ++warnCount;
        } else {
            ++infoCount;
        }
        if (logRowMatchesFilters(row)) {
            ++filteredCount;
        }
    }

    const int total = infoCount + warnCount + criticalCount;
    const int maxPage = qMax(0, (filteredCount - 1) / m_logRowsPerPage);
    if (m_logCurrentPage > maxPage) {
        m_logCurrentPage = maxPage;
        applyLogFilters();
        return;
    }
    const int active = warnCount + criticalCount;
    if (m_logTotalValue) {
        m_logTotalValue->setText(QString::number(total));
    }
    if (m_logActiveValue) {
        m_logActiveValue->setText(QString::number(active));
    }
    if (m_logCriticalValue) {
        m_logCriticalValue->setText(QString::number(criticalCount));
    }

    auto pct = [total](int count) {
        return total > 0 ? qRound(count * 100.0 / total) : 0;
    };
    if (m_logSeverityRows) {
        m_logSeverityRows->setText(
            QString("<span style='color:#58df78'>정보</span>  %1 (%2%)<br>"
                    "<span style='color:#f6bd32'>주의</span>  %3 (%4%)<br>"
                    "<span style='color:#ff5b57'>위험</span>  %5 (%6%)")
                .arg(infoCount)
                .arg(pct(infoCount))
                .arg(warnCount)
                .arg(pct(warnCount))
                .arg(criticalCount)
                .arg(pct(criticalCount)));
    }
    if (m_logSeverityChart) {
        m_logSeverityChart->setProperty("infoCount", infoCount);
        m_logSeverityChart->setProperty("warnCount", warnCount);
        m_logSeverityChart->setProperty("criticalCount", criticalCount);
        m_logSeverityChart->update();
    }
    if (m_logFooterRange) {
        const int first = filteredCount > 0 ? m_logCurrentPage * m_logRowsPerPage + 1 : 0;
        const int last = filteredCount > 0 ? qMin(filteredCount, (m_logCurrentPage + 1) * m_logRowsPerPage) : 0;
        m_logFooterRange->setText(QString("%1 - %2 / 필터 결과 %3, 전체 %4 로그")
                                      .arg(first)
                                      .arg(last)
                                      .arg(filteredCount)
                                      .arg(total));
    }
    const bool hasPrev = m_logCurrentPage > 0;
    const bool hasNext = m_logCurrentPage < maxPage;
    if (m_logFirstPageButton) {
        m_logFirstPageButton->setEnabled(hasPrev);
    }
    if (m_logPrevPageButton) {
        m_logPrevPageButton->setEnabled(hasPrev);
    }
    if (m_logNextPageButton) {
        m_logNextPageButton->setEnabled(hasNext);
    }
    if (m_logLastPageButton) {
        m_logLastPageButton->setEnabled(hasNext);
    }
    for (int i = 0; i < m_logPageButtons.size(); ++i) {
        QPushButton *button = m_logPageButtons[i];
        const int pageBase = (m_logCurrentPage / qMax(1, m_logPageButtons.size())) * m_logPageButtons.size();
        const int pageIndex = pageBase + i;
        button->setProperty("pageIndex", pageIndex);
        button->setText(QString::number(pageIndex + 1));
        button->setEnabled(pageIndex <= maxPage);
        button->setObjectName(pageIndex == m_logCurrentPage ? "logPageActive" : "logPageButton");
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void MainWindow::appendEvent(const UiEvent &event)
{
    if (isBridgeCommandSentEvent(event) || isReassemblyTimeoutEvent(event) || isCommandAckEvent(event)) {
        return;
    }
    if (event.robotId < 0 && event.message.contains(QStringLiteral("드론 생성"))) {
        return;
    }

    QString severity = "정보";
    QString color = "#8a96a3";
    if (event.severity >= 4) {
        severity = "위험";
        color = "#ff3838";
    } else if (event.severity == 3) {
        severity = "경고";
        color = "#ff3838";
    } else if (event.severity == 2) {
        severity = "주의";
        color = "#ffd21a";
    }

    const QString source = event.robotId >= 0 ? robotName(event.robotId) : QString("시스템");
    QListWidgetItem *item = new QListWidgetItem(QString("[%1] %2  %3")
                                                .arg(severity, source, event.message));
    item->setForeground(QColor(color));
    m_eventList->insertItem(0, item);
    if (isVictimDetectedEvent(event)) {
        showVictimAlert(event);
    }
    if (m_logTable) {
        const QString tableSeverity = event.severity >= 3 ? "CRITICAL" : (event.severity == 2 ? "WARN" : "INFO");
        const QDateTime eventTime = event.timestampUs > 0
            ? QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.timestampUs / 1000))
            : QDateTime::currentDateTime();
        appendLogTableRow(tableSeverity,
                          eventTime.toString("yyyy-MM-dd HH:mm:ss.zzz"),
                          source,
                          isVictimDetectedEvent(event) ? "안전" : "시스템",
                          event.message,
                          true);
        while (m_logTable->rowCount() > 200) {
            m_logTable->removeRow(m_logTable->rowCount() - 1);
        }
        updateLogSummary();
    } else if (m_logPageList) {
        QListWidgetItem *copy = new QListWidgetItem(item->text());
        copy->setForeground(QColor(color));
        m_logPageList->insertItem(0, copy);
        while (m_logPageList->count() > 200) {
            delete m_logPageList->takeItem(m_logPageList->count() - 1);
        }
    }
    while (m_eventList->count() > 80) {
        delete m_eventList->takeItem(m_eventList->count() - 1);
    }
    updateMissionSummary();
}

bool MainWindow::isVictimDetectedEvent(const UiEvent &event) const
{
    const bool messageLooksLikeVictim =
        event.message.contains("구조자", Qt::CaseInsensitive)
        || event.message.contains("요구조자", Qt::CaseInsensitive)
        || event.message.contains("생존자", Qt::CaseInsensitive)
        || event.message.contains("victim", Qt::CaseInsensitive)
        || event.message.contains("survivor", Qt::CaseInsensitive);

    if (event.type == kEventTypeVictimDetected) {
        if (event.code == 0) {
            return false;
        }
        return event.code > 0 || messageLooksLikeVictim;
    }

    return messageLooksLikeVictim;
}

void MainWindow::showVictimAlert(const UiEvent &event)
{
    if (!m_victimAlertButton || event.robotId < 0) {
        return;
    }
    m_victimAlertRobotId = event.robotId;
    const QString source = robotName(event.robotId);
    if (m_victimAlertTitle) {
        m_victimAlertTitle->setText("생존자 탐지");
    }
    if (m_victimAlertRobot) {
        m_victimAlertRobot->setText(source);
    }
    if (m_victimAlertAction) {
        m_victimAlertAction->setText("카메라 보기");
    }
    if (m_map) {
        m_map->showVictimDetection(event.robotId);
    }
    m_victimAlertButton->adjustSize();
    positionVictimAlert();
    m_victimAlertButton->raise();
    m_victimAlertButton->show();
}

void MainWindow::positionVictimAlert()
{
    if (!m_victimAlertButton || !centralWidget()) {
        return;
    }
    const QSize hint = m_victimAlertButton->sizeHint();
    const int w = qMax(700, hint.width());
    const int h = qMax(300, hint.height());
    const QRect r = centralWidget()->rect();
    m_victimAlertButton->setGeometry(r.center().x() - w / 2,
                                     r.center().y() - h / 2,
                                     w,
                                     h);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionVictimAlert();
    positionManualActionPanel();
    positionManualControlOverlay();
}

void MainWindow::showPage(int index)
{
    if (!m_contentStack || index < 0 || index >= m_contentStack->count()) {
        return;
    }
    restoreMapToDashboard();
    m_contentStack->setCurrentIndex(index);
    static const QStringList crumbs = {
        "  >  관제",
        "  >  로봇 상태",
        "  >  드론",
        "  >  로그",
        "  >  설정"
    };
    if (m_crumb && index < crumbs.size()) {
        m_crumb->setText(crumbs[index]);
    }
    for (int i = 0; i < m_navButtons.size(); ++i) {
        m_navButtons[i]->setObjectName(i == index ? "navButtonActive" : "navButton");
        m_navButtons[i]->style()->unpolish(m_navButtons[i]);
        m_navButtons[i]->style()->polish(m_navButtons[i]);
    }
    if (index == 0 && m_map && m_mapPublished) {
        QTimer::singleShot(0, this, [this]() {
            if (m_map) {
                m_map->fitToAvailableSize(kDashboardMapStartupScale);
            }
        });
    }
}

void MainWindow::showCameraFullscreen(int robotId)
{
    if (!m_contentStack || !m_cameraPage || !m_expandedVideoTile ||
        robotId < 0 || robotId >= m_robotCount) {
        return;
    }
    if (m_contentStack->currentWidget() != m_cameraPage) {
        m_hasCameraReturnTarget = true;
        m_cameraReturnIndex = m_contentStack->currentIndex();
        m_cameraReturnWidget = m_contentStack->currentWidget();
        m_cameraReturnCrumb = m_crumb ? m_crumb->text() : QString();
    }
    m_expandedRobotId = robotId;
    m_expandedVideoTile->setRobotId(robotId);
    if (m_cameraTitle) {
        m_cameraTitle->setText(QString("%1 카메라").arg(robotName(robotId)));
    }
    if (m_crumb) {
        m_crumb->setText("  >  카메라 스트리밍");
    }
    if (robotId < m_snapshots.size()) {
        m_expandedVideoTile->setSnapshot(m_snapshots[robotId]);
    }
    m_contentStack->setCurrentWidget(m_cameraPage);
}

void MainWindow::leaveCameraFullscreen()
{
    if (!m_contentStack) {
        return;
    }
    m_expandedRobotId = -1;
    if (m_hasCameraReturnTarget) {
        const int returnIndex = m_cameraReturnIndex;
        QWidget *returnWidget = m_cameraReturnWidget;
        const QString returnCrumb = m_cameraReturnCrumb;
        m_hasCameraReturnTarget = false;
        m_cameraReturnIndex = 0;
        m_cameraReturnWidget = nullptr;
        m_cameraReturnCrumb.clear();

        if (returnIndex >= 0 && returnIndex < 5 &&
            returnWidget == m_contentStack->widget(returnIndex)) {
            showPage(returnIndex);
            return;
        }
        if (returnWidget && m_contentStack->indexOf(returnWidget) >= 0) {
            m_contentStack->setCurrentWidget(returnWidget);
            if (m_crumb) {
                m_crumb->setText(returnCrumb);
            }
            return;
        }
    }
    showPage(0);
}

void MainWindow::showMapFullscreen()
{
    if (!m_contentStack || !m_mapFullscreenPage || !m_mapFullscreenContentLayout || !m_map
        || !m_mapPublished) {
        return;
    }
    m_mapFullscreenContentLayout->addWidget(m_map, 1);
    if (m_crumb) {
        m_crumb->setText("  >  탐색 지도");
    }
    m_contentStack->setCurrentWidget(m_mapFullscreenPage);
    QTimer::singleShot(0, this, [this]() {
        if (m_map) {
            m_map->fitToAvailableSize(2.2f);
        }
    });
}

void MainWindow::restoreMapToDashboard()
{
    if (!m_mapLayout || !m_map) {
        return;
    }
    if (m_mapLayout->indexOf(m_map) >= 0) {
        return;
    }
    m_mapLayout->addWidget(m_map, 1);
}

bool MainWindow::publishDroneMapToControl(const QString &preferredYamlPath)
{
    if (!m_map) {
        return false;
    }

    QString mapYaml = preferredYamlPath;
    if (mapYaml.isEmpty() || !QFileInfo::exists(mapYaml)) {
        mapYaml = findMapYaml();
    }
    if (mapYaml.isEmpty() || !m_map->loadMapConfig(mapYaml)) {
        return false;
    }

    m_mapPublished = true;
    m_map->setVisible(true);
    m_map->setSnapshots(snapshotsForDeployedRobots(m_snapshots, m_robotCount));
    m_routeGeneratedDisplayRobotIds.clear();
    refreshGlobalPathDisplay();
    if (m_addRobotButton) {
        m_addRobotButton->setEnabled(true);
    }
    appendEvent(UiEvent{-1,
                        1,
                        0,
                        static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL,
                        QStringLiteral("드론 맵 업데이트 완료")});
    QTimer::singleShot(0, this, [this]() {
        if (m_map) {
            m_map->fitToAvailableSize(kDashboardMapStartupScale);
        }
    });
    return true;
}

void MainWindow::leaveMapFullscreen()
{
    if (!m_contentStack || !m_mapLayout || !m_map) {
        return;
    }
    restoreMapToDashboard();
    showPage(0);
}

void MainWindow::showMap2D()
{
    if (!m_map || !m_mapPublished) {
        return;
    }
    m_map->setViewMode3D(false);
    if (m_contentStack && m_contentStack->currentWidget() == m_mapFullscreenPage) {
        QTimer::singleShot(0, this, [this]() {
            if (m_map) {
                m_map->fitToAvailableSize(2.2f);
            }
        });
    }
    m_btn2d->setObjectName("smallActive");
    m_btn3d->setObjectName("smallButton");
    m_fullscreenBtn2d->setObjectName("smallActive");
    m_fullscreenBtn3d->setObjectName("smallButton");
    for (QPushButton *button : {m_btn2d, m_btn3d, m_fullscreenBtn2d, m_fullscreenBtn3d}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void MainWindow::showMap3D()
{
    if (!m_map || !m_mapPublished) {
        return;
    }
    m_map->setViewMode3D(true);
    m_btn2d->setObjectName("smallButton");
    m_btn3d->setObjectName("smallActive");
    m_fullscreenBtn2d->setObjectName("smallButton");
    m_fullscreenBtn3d->setObjectName("smallActive");
    for (QPushButton *button : {m_btn2d, m_btn3d, m_fullscreenBtn2d, m_fullscreenBtn3d}) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }
}

void MainWindow::selectRobot(int robotId)
{
    if (robotId != kAllRobotsSelection && (robotId < 0 || robotId >= m_robotCount)) {
        return;
    }
    if (robotId == kAllRobotsSelection && m_manualControlEnabled) {
        setManualControlEnabled(false);
    }
    m_selectedRobot = robotId;
    if (m_robotSelector) {
        const int idx = m_robotSelector->findData(robotId);
        if (idx >= 0 && m_robotSelector->currentIndex() != idx) {
            m_updatingRobotSelector = true;
            m_robotSelector->setCurrentIndex(idx);
            m_updatingRobotSelector = false;
        }
    }
    m_map->setSelectedRobot(robotId);

    const int layoutRobotId = featuredVideoRobotForSelection(robotId, m_videoPlacements.size());
    if (layoutRobotId >= 0 && layoutRobotId < m_videoPlacements.size()) {
        int largestRobotId = layoutRobotId;
        int largestArea = 0;
        for (int i = 0; i < m_videoPlacements.size(); ++i) {
            const VideoPlacement &placement = m_videoPlacements[i];
            const int area = placement.rowSpan * placement.columnSpan;
            if (area > largestArea) {
                largestArea = area;
                largestRobotId = i;
            }
        }
        if (largestRobotId != layoutRobotId) {
            std::swap(m_videoPlacements[layoutRobotId], m_videoPlacements[largestRobotId]);
        }
    }

    relayoutVideoTiles();
    refreshRobotList();
    updateManualControlUi();
}

void MainWindow::refreshRobotSelector()
{
    if (!m_robotSelector) {
        return;
    }

    auto fitPopupHeightToItems = [this]() {
        QAbstractItemView *view = m_robotSelector->view();
        if (!view || m_robotSelector->count() <= 0) {
            return;
        }
        const int rowHeight = qMax(1, view->sizeHintForRow(0));
        view->setFrameShape(QFrame::NoFrame);
        view->setLineWidth(0);
        view->setMidLineWidth(0);
        view->setFixedHeight(rowHeight * m_robotSelector->count());
    };

    QVector<int> availableIds;
    const int count = qBound(0, m_robotCount, kMaxRobots);
    if (count > 0) {
        availableIds.append(kAllRobotsSelection);
        for (int i = 0; i < count; ++i) {
            availableIds.append(i);
        }
    }

    if (availableIds == m_availableRobotIds && m_robotSelector->count() > 0) {
        const int idx = m_robotSelector->findData(m_selectedRobot);
        if (idx >= 0 && m_robotSelector->currentIndex() != idx) {
            m_updatingRobotSelector = true;
            m_robotSelector->setCurrentIndex(idx);
            m_updatingRobotSelector = false;
        }
        fitPopupHeightToItems();
        return;
    }
    m_availableRobotIds = availableIds;

    m_updatingRobotSelector = true;
    m_robotSelector->clear();
    m_robotSelector->setEnabled(count > 0);
    if (availableIds.isEmpty()) {
        m_robotSelector->addItem(QStringLiteral("미투입"), kAllRobotsSelection);
        m_robotSelector->setCurrentIndex(0);
        m_selectedRobot = kAllRobotsSelection;
    } else {
        for (int robotId : availableIds) {
            m_robotSelector->addItem(robotId == kAllRobotsSelection ? QStringLiteral("전체")
                                                                    : robotName(robotId),
                                     robotId);
        }
    }

    const int selectedIndex = m_robotSelector->findData(m_selectedRobot);
    if (selectedIndex >= 0) {
        m_robotSelector->setCurrentIndex(selectedIndex);
    } else if (m_robotSelector->count() > 0) {
        m_robotSelector->setCurrentIndex(0);
        m_selectedRobot = m_robotSelector->itemData(0).toInt();
    }
    m_updatingRobotSelector = false;
    fitPopupHeightToItems();
}

void MainWindow::refreshRobotList()
{
    if (m_selectedRobot == kAllRobotsSelection) {
        if (!m_robotInfo || !m_missionInfo || !m_selected) {
            return;
        }
        const QVector<RobotSnapshot> deployedSnapshots = snapshotsForDeployedRobots(m_snapshots, m_robotCount);
        int connected = 0;
        float progressTotal = 0.0f;
        int progressCount = 0;
        for (const RobotSnapshot &snapshot : deployedSnapshots) {
            if (snapshot.connected) {
                ++connected;
                progressTotal += snapshot.missionProgress > 1.0f
                    ? snapshot.missionProgress
                    : snapshot.missionProgress * 100.0f;
                ++progressCount;
            }
        }
        const int progress = progressCount > 0
            ? qBound(0, qRound(progressTotal / progressCount), 100)
            : 0;
        m_robotInfo->setText(QString("로봇 상태  |  전체 선택  |  온라인 %1/%2")
                             .arg(connected)
                             .arg(deployedSnapshots.size()));
        m_missionInfo->setText(QString("임무 정보  |  전체 로봇 제어     평균 진행률: %1%")
                               .arg(progress));
        m_selected->setText(QStringLiteral("좌표  |  전체 선택  |  경로 생성 시 모든 투입 로봇 기준"));
        return;
    }
    if (m_selectedRobot < 0 || m_selectedRobot >= m_snapshots.size()) {
        return;
    }
    if (!m_robotInfo || !m_missionInfo || !m_selected) {
        return;
    }
    const RobotSnapshot &s = m_snapshots[m_selectedRobot];
    m_robotInfo->setText(QString("로봇 상태  |  %1  %2  |  배터리 %3%  |  RTT %4 ms")
                         .arg(robotName(s.id),
                              s.connected ? "온라인" : (robotConnectionLost(s) ? "연결 끊김" : "연결 안됨"))
                         .arg(displayBatteryPercent(s.id, s.battery))
                         .arg(s.linkRttMs, 0, 'f', 1));
    m_missionInfo->setText(QString("임무 정보  |  현재 임무: %1     목표거리: %2     진행률: %3%     Waypoint: %4")
                           .arg(robotMissionStateText(s),
                                pathDistanceSummary(s))
                           .arg(missionProgressPercent(s))
                           .arg(waypointSummary(s)));
    m_selected->setText(QString("좌표  |  x %1  y %2  θ %3  |  IMG %4 fps  LiDAR %5 fps")
                        .arg(s.x, 0, 'f', 2)
                        .arg(s.y, 0, 'f', 2)
                        .arg(s.theta, 0, 'f', 2)
                        .arg(s.imageFps, 0, 'f', 1)
                        .arg(s.lidarFps, 0, 'f', 1));
}

void MainWindow::sendCommand(uint8_t commandType, float vx, float vy, float omega)
{
    const int severity = commandType == CMD_TYPE_ESTOP ? 4
        : (commandType == CMD_TYPE_MOVE ? 2
           : (commandType == CMD_TYPE_STOP ? 3 : 1));
    auto noteCommandState = [this, commandType](int displayRobotId) {
        if (displayRobotId < 0) {
            return;
        }
        if (commandType == CMD_TYPE_MOVE) {
            m_commandMovingRobotIds.insert(displayRobotId);
        } else if (commandType == CMD_TYPE_STOP || commandType == CMD_TYPE_ESTOP ||
                   commandType == CMD_TYPE_PAUSE_MISSION ||
                   commandType == CMD_TYPE_CANCEL_MISSION) {
            m_commandMovingRobotIds.remove(displayRobotId);
        }
    };
    auto showMoveIndicators = [this, commandType](const QVector<int> &robotIds) {
        if (commandType == CMD_TYPE_MOVE && m_map && !robotIds.isEmpty()) {
            m_map->showMoveCommandIndicators(robotIds);
        }
    };
    int targetRobot = m_selectedRobot;
    if (m_robotSelector && m_robotSelector->currentIndex() >= 0) {
        targetRobot = m_robotSelector->currentData().toInt();
        m_selectedRobot = targetRobot;
    }

    if (m_demoMode) {
        Q_UNUSED(vx);
        Q_UNUSED(vy);
        Q_UNUSED(omega);
        QVector<int> demoMoveRobotIds;
        if (targetRobot == kAllRobotsSelection) {
            for (int robotId = 0; robotId < m_robotCount; ++robotId) {
                demoMoveRobotIds.append(robotId);
            }
        } else if (targetRobot >= 0) {
            demoMoveRobotIds.append(targetRobot);
        }
        showMoveIndicators(demoMoveRobotIds);
        appendEvent(UiEvent{targetRobot, severity, commandType, 0,
                            QString("테스트 명령: %1").arg(commandTypeText(commandType))});
        return;
    }

    if (targetRobot == kAllRobotsSelection) {
        QString failed;
        int sent = 0;
        QVector<int> sentRobotIds;
        for (int robotId = 0; robotId < m_robotCount; ++robotId) {
            if (isHardcodedDisplayRobot(robotId)) {
                continue;
            }
            const int commandRobotId = displayToPhysicalRobotId(robotId);
            QString errorMessage;
            if (!m_monitor.sendCommand(commandRobotId, commandType, vx, vy, omega, &errorMessage)) {
                failed += QString("%1: %2\n").arg(robotName(robotId), errorMessage);
                continue;
            }
            noteCommandState(robotId);
            sentRobotIds.append(robotId);
            ++sent;
        }
        showMoveIndicators(sentRobotIds);
        if (!failed.isEmpty()) {
            QMessageBox::warning(this, "명령 전송 실패",
                                 QString("일부 로봇의 브릿지 명령 큐에 쓰지 못했습니다.\n%1").arg(failed));
        }
        if (sent > 0) {
            appendEvent(UiEvent{kAllRobotsSelection, severity, commandType, 0,
                                QString("전체 명령 전송: %1 (%2대)")
                                    .arg(commandTypeText(commandType))
                                    .arg(sent)});
        }
        return;
    }

    QString errorMessage;
    if (isHardcodedDisplayRobot(targetRobot)) {
        QMessageBox::warning(this, "명령 전송 실패",
                             QString("%1은 고정 표시 로봇이라 실제 명령 대상이 아닙니다.")
                                 .arg(robotName(targetRobot)));
        return;
    }
    const int physicalTargetRobot = displayToPhysicalRobotId(targetRobot);
    if (!m_monitor.sendCommand(physicalTargetRobot, commandType, vx, vy, omega, &errorMessage)) {
        QMessageBox::warning(this, "명령 전송 실패",
                             QString("브릿지 명령 큐에 쓰지 못했습니다.\n%1").arg(errorMessage));
        return;
    }
    noteCommandState(targetRobot);
    showMoveIndicators(QVector<int>{targetRobot});
    appendEvent(UiEvent{targetRobot, severity, commandType, 0,
                        QString("명령 전송: %1").arg(commandTypeText(commandType))});
}

bool MainWindow::sendControlCommand(uint8_t commandType, float vx, float vy, float omega,
                                    const QString &logText, int severity, bool quiet)
{
    auto noteCommandState = [this, commandType, vx, vy, omega](int displayRobotId) {
        if (displayRobotId < 0) {
            return;
        }
        if (commandType == CMD_TYPE_MOVE ||
            (commandType == CMD_TYPE_MANUAL_MOVE &&
             (std::hypot(vx, vy) > 0.001f || std::fabs(omega) > 0.001f))) {
            m_commandMovingRobotIds.insert(displayRobotId);
        } else if (commandType == CMD_TYPE_STOP || commandType == CMD_TYPE_ESTOP ||
                   commandType == CMD_TYPE_PAUSE_MISSION ||
                   commandType == CMD_TYPE_CANCEL_MISSION ||
                   commandType == CMD_TYPE_SET_AUTO ||
                   commandType == CMD_TYPE_MANUAL_MOVE) {
            m_commandMovingRobotIds.remove(displayRobotId);
        }
    };

    int targetRobot = m_selectedRobot;
    if (m_robotSelector && m_robotSelector->currentIndex() >= 0) {
        targetRobot = m_robotSelector->currentData().toInt();
        m_selectedRobot = targetRobot;
    }

    if (m_demoMode) {
        if (!quiet) {
            appendEvent(UiEvent{targetRobot, severity, commandType, 0,
                                logText.isEmpty()
                                    ? QString("테스트 명령: %1").arg(commandTypeText(commandType))
                                    : logText});
        }
        return true;
    }

    auto sendOne = [this, commandType, vx, vy, omega, &noteCommandState](int displayRobotId,
                                                                         QString *errorText) {
        if (isHardcodedDisplayRobot(displayRobotId)) {
            if (errorText) {
                *errorText = QString("%1은 고정 표시 로봇이라 실제 명령 대상이 아닙니다.")
                    .arg(robotName(displayRobotId));
            }
            return false;
        }
        const int physicalRobotId = displayToPhysicalRobotId(displayRobotId);
        QString perRobotError;
        if (!m_monitor.sendCommand(physicalRobotId, commandType, vx, vy, omega, &perRobotError)) {
            if (errorText) {
                *errorText = QString("%1: %2").arg(robotName(displayRobotId), perRobotError);
            }
            return false;
        }
        noteCommandState(displayRobotId);
        return true;
    };

    bool anySent = false;
    QString failed;
    int sent = 0;
    if (targetRobot == kAllRobotsSelection) {
        for (int robotId = 0; robotId < m_robotCount; ++robotId) {
            QString perRobotError;
            if (sendOne(robotId, &perRobotError)) {
                anySent = true;
                ++sent;
            } else if (!perRobotError.isEmpty()) {
                failed += perRobotError + QLatin1Char('\n');
            }
        }
    } else {
        QString perRobotError;
        anySent = sendOne(targetRobot, &perRobotError);
        sent = anySent ? 1 : 0;
        if (!perRobotError.isEmpty()) {
            failed = perRobotError + QLatin1Char('\n');
        }
    }

    if (!failed.isEmpty() && !quiet) {
        QMessageBox::warning(this, "명령 전송 실패",
                             QString("브릿지 명령 큐에 쓰지 못했습니다.\n%1").arg(failed));
    }
    if (anySent && !quiet) {
        const QString text = logText.isEmpty()
            ? QString("명령 전송: %1").arg(commandTypeText(commandType))
            : (targetRobot == kAllRobotsSelection && sent > 1
                   ? QString("%1 (%2대)").arg(logText).arg(sent)
                   : logText);
        appendEvent(UiEvent{targetRobot, severity, commandType, 0, text});
    }
    return anySent;
}

void MainWindow::toggleManualControl()
{
    if (m_selectedRobot == kAllRobotsSelection) {
        updateManualControlUi();
        QMessageBox::information(this,
                                 QStringLiteral("수동 제어"),
                                 QStringLiteral("수동 제어는 특정 SPOT을 선택했을 때만 사용할 수 있습니다."));
        return;
    }
    setManualControlEnabled(!m_manualControlEnabled);
}

void MainWindow::setManualControlEnabled(bool enabled)
{
    if (enabled && m_selectedRobot == kAllRobotsSelection) {
        m_manualControlEnabled = false;
        updateManualControlUi();
        return;
    }
    if (m_manualControlEnabled == enabled) {
        updateManualControlUi();
        return;
    }

    if (!enabled && m_manualJoystick) {
        m_manualJoystick->resetStick();
    }

    m_manualControlEnabled = enabled;
    m_lastManualVx = 0.0f;
    m_lastManualVy = 0.0f;
    m_lastManualOmega = 0.0f;
    updateManualControlUi();

    if (enabled) {
        sendControlCommand(CMD_TYPE_MANUAL_MOVE, 0.0f, 0.0f, 0.0f,
                           QStringLiteral("수동 제어 전환"), 2, false);
    } else {
        sendControlCommand(CMD_TYPE_SET_AUTO, 0.0f, 0.0f, 0.0f,
                           QStringLiteral("자동 제어 전환"), 1, false);
    }
}

void MainWindow::updateManualControlUi()
{
    const bool robotSelected = m_selectedRobot != kAllRobotsSelection;
    if (m_manualToggleButton) {
        m_manualToggleButton->setChecked(m_manualControlEnabled);
        m_manualToggleButton->setEnabled(robotSelected);
        m_manualToggleButton->setProperty("commandKind", m_manualControlEnabled ? "auto" : "manual");
        m_manualToggleButton->setText(m_manualControlEnabled
                                          ? QStringLiteral("자동 제어")
                                          : QStringLiteral("수동 제어"));
        m_manualToggleButton->style()->unpolish(m_manualToggleButton);
        m_manualToggleButton->style()->polish(m_manualToggleButton);
    }
    if (m_manualModeToggleButton) {
        m_manualModeToggleButton->setChecked(m_manualControlEnabled);
        m_manualModeToggleButton->setEnabled(robotSelected);
        m_manualModeToggleButton->setText(QStringLiteral("자동 제어"));
        m_manualModeToggleButton->style()->unpolish(m_manualModeToggleButton);
        m_manualModeToggleButton->style()->polish(m_manualModeToggleButton);
    }
    if (m_controlRobotBadge) {
        m_controlRobotBadge->setText(robotSelected ? robotName(m_selectedRobot) : QStringLiteral("전체"));
    }
    const bool showManualControls = m_manualControlEnabled && robotSelected;
    if (m_controlRobotCaption) {
        m_controlRobotCaption->setVisible(showManualControls);
    }
    if (m_controlRobotBadge) {
        m_controlRobotBadge->setVisible(showManualControls);
    }
    if (m_manualActionPanel) {
        m_manualActionPanel->setVisible(true);
    }
    if (m_manualJoystickPanel) {
        m_manualJoystickPanel->setVisible(true);
    }
    updateCommandButtonOrder();
}

void MainWindow::positionManualActionPanel()
{
    if (m_manualActionPanel && m_manualActionPanel->parentWidget() &&
        m_manualActionPanel->parentWidget()->objectName() == QStringLiteral("manualActionGroupPanel")) {
        return;
    }
    if (m_manualActionPanel && m_manualActionPanel->parentWidget() == m_manualControlBody) {
        return;
    }
    if (!m_manualActionPanel || !m_manualActionPanel->parentWidget()) {
        return;
    }
    constexpr int panelX = 14;
    constexpr int panelW = 98;
    constexpr int panelH = 348;
    constexpr int bottomMargin = 82;
    const QRect navRect = m_manualActionPanel->parentWidget()->rect();
    const int y = qMax(12, navRect.bottom() - bottomMargin - panelH);
    m_manualActionPanel->setGeometry(panelX, y, panelW, panelH);
}

void MainWindow::updateCommandButtonOrder()
{
    if (!m_commandButtonLayout || !m_moveButton || !m_manualToggleButton ||
        !m_addRobotButton || !m_estopButton || !m_autoCommandBody ||
        !m_autoControlPage || !m_manualControlBody || !m_robotSelectRow ||
        !m_controlModeStack) {
        return;
    }
    while (QLayoutItem *item = m_commandButtonLayout->takeAt(0)) {
        delete item;
    }
    const bool robotSelected = m_selectedRobot != kAllRobotsSelection;
    const bool manualMode = m_manualControlEnabled && robotSelected;
    m_controlModeStack->setCurrentWidget(manualMode ? m_manualControlBody : m_autoControlPage);
    m_robotSelectRow->setVisible(true);
    m_autoCommandBody->setVisible(true);
    m_moveButton->setVisible(true);
    m_manualToggleButton->setVisible(true);
    m_addRobotButton->setVisible(true);
    m_estopButton->setVisible(true);
    if (m_manualModeToggleButton) {
        m_manualModeToggleButton->setVisible(true);
    }
    for (QPushButton *button : {m_moveButton, m_manualToggleButton, m_addRobotButton, m_estopButton}) {
        m_commandButtonLayout->addWidget(button);
    }
}

void MainWindow::sendManualVelocity(float vx, float vy, float omega, bool force)
{
    if (!m_manualControlEnabled || m_selectedRobot == kAllRobotsSelection) {
        return;
    }
    if (std::hypot(vx, vy) < kManualCommandDeadband * kManualMaxForwardMps) {
        vx = 0.0f;
        vy = 0.0f;
    }
    if (std::fabs(omega) < kManualCommandDeadband * kManualMaxYawRadps) {
        omega = 0.0f;
    }

    const bool wasZero = std::hypot(m_lastManualVx, m_lastManualVy) <= 0.001f
        && std::fabs(m_lastManualOmega) <= 0.001f;
    const bool isZero = std::hypot(vx, vy) <= 0.001f && std::fabs(omega) <= 0.001f;
    if (!force && wasZero && isZero) {
        return;
    }

    m_lastManualVx = vx;
    m_lastManualVy = vy;
    m_lastManualOmega = omega;
    sendControlCommand(CMD_TYPE_MANUAL_MOVE, vx, vy, omega, QString(), 1, true);
}

void MainWindow::sendManualAction(int actionCode, const QString &label)
{
    if (m_selectedRobot == kAllRobotsSelection) {
        QMessageBox::information(this,
                                 QStringLiteral("수동 제어"),
                                 QStringLiteral("수동 동작은 특정 SPOT을 선택했을 때만 사용할 수 있습니다."));
        return;
    }
    if (!m_manualControlEnabled) {
        setManualControlEnabled(true);
    }
    if (label == QStringLiteral("인사")) {
        sendControlCommand(CMD_TYPE_BODY_ACTION,
                           static_cast<float>(actionCode),
                           0.0f,
                           0.0f,
                           QString("수동 동작: %1").arg(label),
                           1,
                           false);
        return;
    }

    sendManualVelocity(0.0f, 0.0f, 0.0f, true);
    sendControlCommand(CMD_TYPE_SET_MODE,
                       static_cast<float>(actionCode),
                       0.0f,
                       0.0f,
                       QString("수동 자세: %1").arg(label),
                       1,
                       false);
}

void MainWindow::positionManualControlOverlay()
{
    if (m_manualJoystickPanel && m_manualJoystickPanel->parentWidget() == m_manualControlBody) {
        return;
    }
    if (!m_manualJoystickPanel || !centralWidget()) {
        return;
    }
    constexpr int panelW = 278;
    constexpr int panelH = 278;
    constexpr int margin = 10;
    const QRect r = centralWidget()->rect();
    const int x = qMax(12, r.right() - panelW - margin);
    const int y = qMax(12, r.bottom() - panelH - margin);
    int clippedTop = 0;
    if (m_map && m_map->isVisible()) {
        const QRect mapRect(m_map->mapTo(centralWidget(), QPoint(0, 0)), m_map->size());
        const QRect panelRect(x, y, panelW, panelH);
        if (panelRect.intersects(mapRect.adjusted(-8, -8, 8, 8))) {
            clippedTop = qBound(0, mapRect.bottom() + margin - y, panelH);
        }
    }
    if (clippedTop >= panelH) {
        m_manualJoystickPanel->hide();
        return;
    }
    if (clippedTop > 0) {
        m_manualJoystickPanel->setMask(QRegion(0, clippedTop, panelW, panelH - clippedTop));
    } else {
        m_manualJoystickPanel->clearMask();
    }
    if (m_manualControlEnabled && m_selectedRobot != kAllRobotsSelection) {
        m_manualJoystickPanel->show();
    }
    m_manualJoystickPanel->setGeometry(x, y, panelW, panelH);
}

void MainWindow::handleRouteGenerationRequested(int robotId, const QPointF &end)
{
    if (robotId != kAllRobotsSelection && (robotId < 0 || robotId >= m_robotCount)) {
        QMessageBox::warning(this, "경로 생성 실패",
                             QString("로봇 ID가 올바르지 않습니다: %1").arg(robotId));
        return;
    }
    if (isHardcodedDisplayRobot(robotId)) {
        QMessageBox::warning(this, "경로 생성 실패",
                             QString("%1은 고정 표시 로봇이라 경로 생성 대상이 아닙니다.")
                                 .arg(robotName(robotId)));
        return;
    }

    if (m_demoMode) {
        QVector<int> visiblePathIds;
        if (robotId == kAllRobotsSelection) {
            visiblePathIds = deployedRobotIds(m_snapshots, m_robotCount);
        } else {
            const QVector<RobotSnapshot> deployedSnapshots =
                snapshotsForDeployedRobots(m_snapshots, m_robotCount);
            auto it = std::find_if(deployedSnapshots.cbegin(), deployedSnapshots.cend(),
                                   [robotId](const RobotSnapshot &snapshot) {
                                       return snapshot.id == robotId;
                                   });
            if (it != deployedSnapshots.cend() && routeEligibleSnapshot(*it)) {
                visiblePathIds.append(robotId);
            }
        }
        if (visiblePathIds.isEmpty()) {
            QMessageBox::warning(this, "경로 생성 실패",
                                 "경로를 생성할 수 있는 정상 로봇이 없습니다.");
            return;
        }
        appendEvent(UiEvent{robotId, 1, CMD_TYPE_SET_ROUTE, 0,
                            QString("테스트 경로 생성: 목적지(%1, %2)")
                                .arg(end.x(), 0, 'f', 2)
                                .arg(end.y(), 0, 'f', 2)});
        noteRouteGeneratedForDisplayRobots(visiblePathIds);
        return;
    }

    QString errorMessage;
    if (robotId == kAllRobotsSelection) {
        QString failed;
        int sent = 0;
        QVector<int> sentIds;
        const QVector<RobotSnapshot> deployedSnapshots =
            snapshotsForDeployedRobots(m_snapshots, m_robotCount);
        for (const RobotSnapshot &snapshot : deployedSnapshots) {
            if (isHardcodedDisplayRobot(snapshot.id)) {
                continue;
            }
            if (!routeEligibleSnapshot(snapshot)) {
                continue;
            }
            QString perRobotError;
            const int physicalRobotId = displayToPhysicalRobotId(snapshot.id);
            if (!m_monitor.sendCommand(physicalRobotId, CMD_TYPE_SET_ROUTE,
                                       static_cast<float>(end.x()),
                                       static_cast<float>(end.y()),
                                       0.0f,
                                       &perRobotError)) {
                failed += QString("%1: %2\n").arg(robotName(snapshot.id), perRobotError);
                continue;
            }
            ++sent;
            sentIds.append(snapshot.id);
        }
        if (sentIds.isEmpty()) {
            QMessageBox::warning(this, "경로 생성 실패",
                                 "경로를 생성할 수 있는 정상 로봇이 없습니다.");
            return;
        }
        if (!failed.isEmpty()) {
            QMessageBox::warning(this, "경로 생성 실패",
                                 QString("일부 로봇의 목적지 좌표를 브릿지 명령 큐에 쓰지 못했습니다.\n%1")
                                     .arg(failed));
        }
        if (sent > 0) {
            noteRouteGeneratedForDisplayRobots(sentIds);
            appendEvent(UiEvent{kAllRobotsSelection, 1, CMD_TYPE_SET_ROUTE, 0,
                                QString("전체 경로 생성 요청: 목적지(%1, %2), %3대")
                                    .arg(end.x(), 0, 'f', 2)
                                    .arg(end.y(), 0, 'f', 2)
                                    .arg(sent)});
        }
        return;
    }

    const QVector<RobotSnapshot> deployedSnapshots =
        snapshotsForDeployedRobots(m_snapshots, m_robotCount);
    auto selectedIt = std::find_if(deployedSnapshots.cbegin(), deployedSnapshots.cend(),
                                   [robotId](const RobotSnapshot &snapshot) {
                                       return snapshot.id == robotId;
                                   });
    if (selectedIt == deployedSnapshots.cend() || !routeEligibleSnapshot(*selectedIt)) {
        QMessageBox::warning(this, "경로 생성 실패",
                             QString("%1은 현재 고장/미연결 상태라 경로를 생성하지 않았습니다.")
                                 .arg(robotName(robotId)));
        return;
    }

    const int physicalRobotId = displayToPhysicalRobotId(robotId);
    if (!m_monitor.sendCommand(physicalRobotId, CMD_TYPE_SET_ROUTE,
                               static_cast<float>(end.x()),
                               static_cast<float>(end.y()),
                               0.0f,
                               &errorMessage)) {
        QMessageBox::warning(this, "경로 생성 실패",
                             QString("목적지 좌표를 브릿지 명령 큐에 쓰지 못했습니다.\n%1")
                                 .arg(errorMessage));
        return;
    }

    appendEvent(UiEvent{robotId, 1, CMD_TYPE_SET_ROUTE, 0,
                        QString("경로 생성 요청: 목적지(%1, %2)")
                            .arg(end.x(), 0, 'f', 2)
                            .arg(end.y(), 0, 'f', 2)});
    noteRouteGeneratedForDisplayRobots(QVector<int>{robotId});
}

void MainWindow::sendMove()
{
    sendCommand(CMD_TYPE_MOVE, 0.25f, 0.0f, 0.0f);
}

void MainWindow::sendExplore()
{
    sendCommand(CMD_TYPE_START_MISSION);
}

void MainWindow::sendStandby()
{
    sendCommand(CMD_TYPE_PAUSE_MISSION);
}

void MainWindow::sendStop()
{
    sendCommand(CMD_TYPE_STOP);
}

void MainWindow::sendEstop()
{
    sendCommand(CMD_TYPE_ESTOP);
}

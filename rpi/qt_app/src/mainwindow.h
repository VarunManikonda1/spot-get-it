#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "dashboardwidgets.h"
#include "shmmonitor.h"

#include <QGridLayout>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPoint>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QVector>

class QDateEdit;
class QLineEdit;
class QScrollArea;
class QTableWidget;
class ManualJoystickWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void updateSnapshots(const QVector<RobotSnapshot> &snapshots);
    void appendEvent(const UiEvent &event);
    void selectRobot(int robotId);
    void sendMove();
    void sendExplore();
    void sendStandby();
    void sendStop();
    void sendEstop();
    void toggleManualControl();
    void handleRouteGenerationRequested(int robotId, const QPointF &end);
    void showPage(int index);
    void showMap2D();
    void showMap3D();
    void showMapFullscreen();
    void leaveMapFullscreen();
    void showCameraFullscreen(int robotId);
    void leaveCameraFullscreen();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void applyStyle();
    QPushButton *makeNavButton(const QString &text, bool active = false);
    QPushButton *makeCommandButton(const QString &text, const QString &objectName = QString());
    QFrame *makeHeaderMetricCard(const QString &title, QLabel **valueLabel,
                                 const QString &accentColor,
                                 QProgressBar **progressBar = nullptr,
                                 const QString &iconKind = QString());
    QString formatMissionTime() const;
    void updateMissionSummary();
    void updatePacketLogPanel();
    void startDemoMode(int count);
    QVector<RobotSnapshot> makeDemoSnapshots(int count) const;
    void setRobotCount(int count);
    void syncRobotUi(int count);
    void syncVideoTiles(int count);
    void syncStatusRows(int count);
    void syncRobotStatusCards(int count);
    void resetVideoLayout(int count);
    bool setVideoPlacementIfFree(int robotId, int row, int column, int rowSpan, int columnSpan);
    bool autoArrangeVideoLayout(int fixedRobotId, int row, int column, int rowSpan, int columnSpan);
    bool videoPlacementAvailable(int robotId, int row, int column, int rowSpan, int columnSpan) const;
    void updateVideoLayoutBodySize();
    QPoint videoGridCellAt(const QPoint &globalPos) const;
    void moveVideoTile(int robotId, const QPoint &globalPos);
    void resizeVideoTile(int robotId, int edgeMask, const QPoint &globalPos);
    void shrinkVideoTile(int robotId, int edgeMask);
    void relayoutVideoTiles();
    void relayoutRobotStatusCards();
    void clampSelectedRobot();
    void refreshRobotSelector();
    void refreshRobotList();
    void sendCommand(uint8_t commandType, float vx = 0.0f, float vy = 0.0f, float omega = 0.0f);
    bool sendControlCommand(uint8_t commandType, float vx, float vy, float omega,
                            const QString &logText = QString(), int severity = 1,
                            bool quiet = false);
    void setManualControlEnabled(bool enabled);
    void updateManualControlUi();
    void updateCommandButtonOrder();
    void positionManualActionPanel();
    void sendManualVelocity(float vx, float vy, float omega, bool force = false);
    void sendManualAction(int actionCode, const QString &label);
    void positionManualControlOverlay();
    void noteRouteGeneratedForDisplayRobots(const QVector<int> &robotIds);
    void refreshGlobalPathDisplay();
    bool isVictimDetectedEvent(const UiEvent &event) const;
    void showVictimAlert(const UiEvent &event);
    void positionVictimAlert();
    void restoreMapToDashboard();
    bool publishDroneMapToControl(const QString &preferredYamlPath = QString());
    void appendLogTableRow(const QString &severityCode,
                           const QString &timestamp,
                           const QString &source,
                           const QString &category,
                           const QString &message,
                           bool prepend = false);
    void applyLogFilters();
    void setLogSeverityFilter(const QString &severityCode);
    void setLogPage(int page);
    bool logRowMatchesFilters(int row) const;
    void updateLogSummary();

    ShmMonitor m_monitor;
    QVector<RobotSnapshot> m_snapshots;
    QSet<int> m_commandMovingRobotIds;
    QSet<int> m_routeGeneratedDisplayRobotIds;
    int m_robotCount = 4;
    int m_selectedRobot = -1;
    bool m_demoMode = false;
    QTimer *m_demoTimer = nullptr;

    QLabel *m_system = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_crumb = nullptr;
    QLabel *m_selected = nullptr;
    QLabel *m_missionInfo = nullptr;
    QLabel *m_robotInfo = nullptr;
    QLabel *m_metricProgress = nullptr;
    QLabel *m_metricMissionTime = nullptr;
    QLabel *m_metricConnected = nullptr;
    QLabel *m_metricEvents = nullptr;
    QProgressBar *m_metricProgressBar = nullptr;
    MapWidget *m_map = nullptr;
    QVBoxLayout *m_mapLayout = nullptr;
    QVBoxLayout *m_mapFullscreenContentLayout = nullptr;
    QWidget *m_mapFullscreenPage = nullptr;
    QListWidget *m_eventList = nullptr;
    QListWidget *m_logPageList = nullptr;
    QListWidget *m_packetStatsList = nullptr;
    QTableWidget *m_logTable = nullptr;
    QDateEdit *m_logStartDate = nullptr;
    QDateEdit *m_logEndDate = nullptr;
    QLineEdit *m_logSearch = nullptr;
    QLabel *m_logTotalValue = nullptr;
    QLabel *m_logActiveValue = nullptr;
    QLabel *m_logCriticalValue = nullptr;
    QLabel *m_logSeverityRows = nullptr;
    QLabel *m_logFooterRange = nullptr;
    QWidget *m_logSeverityChart = nullptr;
    QVector<QPushButton *> m_logFilterButtons;
    QVector<QPushButton *> m_logPageButtons;
    QPushButton *m_logFirstPageButton = nullptr;
    QPushButton *m_logPrevPageButton = nullptr;
    QPushButton *m_logNextPageButton = nullptr;
    QPushButton *m_logLastPageButton = nullptr;
    QString m_logSeverityFilter;
    int m_logCurrentPage = 0;
    int m_logRowsPerPage = 25;
    QStackedWidget *m_contentStack = nullptr;
    QGridLayout *m_videoGrid = nullptr;
    QGridLayout *m_robotStatusGrid = nullptr;
    QGridLayout *m_robotCardGrid = nullptr;
    QWidget *m_videoEmptyState = nullptr;
    QWidget *m_robotStatusEmptyState = nullptr;
    QScrollArea *m_videoScroll = nullptr;
    QScrollArea *m_robotStatusScroll = nullptr;
    QScrollArea *m_robotCardScroll = nullptr;
    QVector<VideoTile *> m_videoTiles;
    struct VideoPlacement {
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
    };
    QVector<VideoPlacement> m_videoPlacements;
    VideoTile *m_expandedVideoTile = nullptr;
    QWidget *m_cameraPage = nullptr;
    QLabel *m_cameraTitle = nullptr;
    QPushButton *m_victimAlertButton = nullptr;
    QLabel *m_victimAlertTitle = nullptr;
    QLabel *m_victimAlertRobot = nullptr;
    QLabel *m_victimAlertAction = nullptr;
    int m_expandedRobotId = -1;
    int m_victimAlertRobotId = -1;
    bool m_hasCameraReturnTarget = false;
    int m_cameraReturnIndex = 0;
    QWidget *m_cameraReturnWidget = nullptr;
    QString m_cameraReturnCrumb;
    QVector<StatusRow *> m_statusRows;
    QVector<RobotStatusCard *> m_robotStatusCards;
    QVector<QPushButton *> m_navButtons;
    QComboBox *m_robotSelector = nullptr;
    QLabel *m_robotTotalLabel = nullptr;
    QVector<int> m_availableRobotIds;
    QDateTime m_missionStartedAt;
    bool m_missionTimerActive = false;
    bool m_updatingRobotSelector = false;
    QPushButton *m_btn2d = nullptr;
    QPushButton *m_btn3d = nullptr;
    QPushButton *m_fullscreenBtn2d = nullptr;
    QPushButton *m_fullscreenBtn3d = nullptr;
    QHBoxLayout *m_commandButtonLayout = nullptr;
    QWidget *m_robotSelectRow = nullptr;
    QWidget *m_autoControlPage = nullptr;
    QWidget *m_autoCommandBody = nullptr;
    QWidget *m_manualControlBody = nullptr;
    QStackedWidget *m_controlModeStack = nullptr;
    QLabel *m_controlRobotCaption = nullptr;
    QLabel *m_controlRobotBadge = nullptr;
    QPushButton *m_moveButton = nullptr;
    QPushButton *m_addRobotButton = nullptr;
    QPushButton *m_estopButton = nullptr;
    QPushButton *m_manualToggleButton = nullptr;
    QPushButton *m_manualModeToggleButton = nullptr;
    QWidget *m_manualActionPanel = nullptr;
    QWidget *m_manualJoystickPanel = nullptr;
    ManualJoystickWidget *m_manualJoystick = nullptr;
    bool m_manualControlEnabled = false;
    bool m_mapPublished = false;
    float m_lastManualVx = 0.0f;
    float m_lastManualVy = 0.0f;
    float m_lastManualOmega = 0.0f;
};

#endif

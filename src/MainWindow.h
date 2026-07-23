// SPDX-FileCopyrightText: 2026 Rao Jing
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>
#include <QColor>
#include <QPixmap>
#include <QSqlDatabase>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;

class QChart;
class QChartView;
class QLineSeries;
class QValueAxis;

struct FatigueSample {
    QString timestamp;
    QString mode;
    QString level;
    QString reason;
    QString framePath;
    QString snapshotPath;
    QString attentionState;
    QString riskFactors;
    QString detectorBackend;
    QString featureOrigin;
    QString perceptionState;
    bool measurementValid = false;
    double processingFps = 0.0;
    double latencyMs = 0.0;
    int protocolVersion = 1;
    int score = 0;
    int qualityScore = 0;
    int calibrationProgress = 0;
    double ear = 0.0;
    double mar = 0.0;
    double perclos = 0.0;
    double adaptiveEarThreshold = 0.20;
    double adaptiveMarThreshold = 0.62;
    double blinkRate = 0.0;
    int yawnCount = 0;
    double eyeClosedSeconds = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void startSimulation();
    void startCamera();
    void openVideoFile();
    void continueDetector();
    void exportHistoryCsv();
    void exportHtmlReport();
    void loadSafetyEvents();
    void acknowledgeSelectedEvent();
    void markSelectedEventFalsePositive();
    void handleEventSelectionChanged();
    void stopDetector();
    void readDetectorOutput();
    void readDetectorError();
    void handleDetectorFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleDetectorError(QProcess::ProcessError error);
    void handleDetectorStarted();
    void handleDetectorStateChanged(QProcess::ProcessState state);

private:
    void buildUi();
    QWidget *createMonitorPage();
    QWidget *createEventsPage();
    QWidget *createReportPage();
    QWidget *createHealthPage();
    QWidget *createCalibrationPage();
    QWidget *createHistoryPage();
    QWidget *createStatusCard();
    QWidget *createMetricsCard();
    QWidget *createAiInsightCard();
    QWidget *createControlBar();
    void applyTheme();

    void initDatabase();
    void createTables();
    void insertRecord(const FatigueSample &sample);
    void insertSafetyEvent(const FatigueSample &sample, const QString &eventType);
    void loadHistory();
    void refreshReportSummary();
    void updateHealthPanel(const FatigueSample &sample);
    void updateCalibrationPanel(const FatigueSample &sample);
    void refreshEventDetail(int row);

    void startDetector(const QString &mode, const QString &source = QString());
    void processJsonLine(const QByteArray &line);
    FatigueSample sampleFromJson(const QJsonObject &object) const;
    void updateSample(const FatigueSample &sample);
    void updateVideoFrame(const QString &path);
    void refreshVideoPixmap();
    void updateChart(const FatigueSample &sample);
    void appendLog(const QString &message);
    void setRunningUi(bool running);
    void setupVoiceAlerts();
    void playVoiceAlert(const FatigueSample &sample);

    QString eventTypeForSample(const FatigueSample &sample) const;
    QString levelText(const QString &level) const;
    QColor levelColor(const QString &level) const;
    QString projectRoot() const;
    QString runtimeDir() const;
    QString detectorScriptPath() const;
    QString resolvePythonExecutable() const;

    QTabWidget *m_tabs = nullptr;
    QLabel *m_videoLabel = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_reasonLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_scoreLabel = nullptr;
    QProgressBar *m_scoreBar = nullptr;
    QLabel *m_earLabel = nullptr;
    QLabel *m_marLabel = nullptr;
    QLabel *m_perclosLabel = nullptr;
    QLabel *m_blinkLabel = nullptr;
    QLabel *m_yawnLabel = nullptr;
    QLabel *m_eyeClosedLabel = nullptr;
    QLabel *m_poseLabel = nullptr;
    QLabel *m_attentionLabel = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QLabel *m_calibrationLabel = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QLabel *m_riskLabel = nullptr;
    QLabel *m_snapshotLabel = nullptr;
    QProgressBar *m_qualityBar = nullptr;
    QProgressBar *m_calibrationBar = nullptr;
    QLabel *m_alarmLabel = nullptr;
    QPushButton *m_simButton = nullptr;
    QPushButton *m_cameraButton = nullptr;
    QPushButton *m_videoButton = nullptr;
    QPushButton *m_continueButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QTableWidget *m_eventTable = nullptr;
    QLabel *m_eventPreviewLabel = nullptr;
    QLabel *m_eventDetailLabel = nullptr;
    QLabel *m_totalEventsLabel = nullptr;
    QLabel *m_highRiskEventsLabel = nullptr;
    QLabel *m_pendingEventsLabel = nullptr;
    QLabel *m_reportSummaryLabel = nullptr;
    QLabel *m_healthProcessLabel = nullptr;
    QLabel *m_healthDbLabel = nullptr;
    QLabel *m_healthModeLabel = nullptr;
    QLabel *m_healthFrameLabel = nullptr;
    QLabel *m_healthQualityLabel = nullptr;
    QLabel *m_healthModelLabel = nullptr;
    QLabel *m_calibrationStateLabel = nullptr;
    QLabel *m_calibrationThresholdLabel = nullptr;
    QLabel *m_calibrationGuideLabel = nullptr;
    QProgressBar *m_calibrationPageBar = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QLineSeries *m_scoreSeries = nullptr;
    QLineSeries *m_perclosSeries = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    QProcess *m_detector = nullptr;
    QByteArray m_stdoutBuffer;
    QSqlDatabase m_db;
    QPixmap m_currentPixmap;
    QString m_currentFramePath;
    int m_sampleIndex = 0;
    int m_lastAlarmScore = 0;
    QString m_lastMode;
    QString m_lastSource;
    QString m_lastEventType;
    QString m_lastVoiceKey;
    QString m_currentVoicePath;
    QElapsedTimer m_dbWriteClock;
    QElapsedTimer m_alarmClock;
    QElapsedTimer m_eventClock;
    QElapsedTimer m_voiceClock;
};
